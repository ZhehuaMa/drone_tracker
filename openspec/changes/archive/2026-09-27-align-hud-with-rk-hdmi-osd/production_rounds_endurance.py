"""生产板同一节点进程 20 轮 ON/OFF 与额外 610 秒满负载验收。"""
import json
import os
import signal
import subprocess
import time
from pathlib import Path

import rclpy
import yaml
from nav_msgs.msg import Odometry
from sensor_msgs.msg import Imu
from std_msgs.msg import Header
from vision_msgs.msg import Detection2D


ROOT = Path('/home/hzhy/hdmi-hud-acceptance-20260924')
OUT = ROOT / 'rounds-endurance'
OUT.mkdir(exist_ok=True)
ORIGINAL = Path('/opt/drone_tracker/config/camera.yaml').read_bytes()
RELEASE = Path('/opt/drone_tracker/current').resolve()
SERVICE_ACTIVE = subprocess.run(['systemctl', 'is-active', '--quiet', 'drone-tracker.service']).returncode == 0
os.environ.update(ROS_DOMAIN_ID='197', ROS_LOCALHOST_ONLY='1', RCUTILS_COLORIZED_OUTPUT='0')

params = yaml.safe_load(ORIGINAL)
cfg = params['drone_tracker_node']['ros__parameters']
cfg['video_source'] = str(ROOT / 'fixed-drone-1920x1200-650s-60fps.avi')
cfg['tracker']['yolo_engine'] = '/opt/drone_tracker/models/yolo.rknn'
cfg['tracker']['yolo_target_class_id'] = 2
cfg['camera'].update(fx=1875., fy=1875., cx=960., cy=600.)
cfg['recording']['enabled'] = False
cfg['visualized_result'] = False
cfg['need_save'] = False
cfg['osd'].update(enabled=True, render_path='cached-overlay', timing_enabled=True)
config = OUT / 'rounds-endurance.yaml'
config.write_text(yaml.safe_dump(params, sort_keys=False))

rclpy.init()
node = rclpy.create_node('hud_rounds_endurance_acceptance')
trigger = node.create_publisher(Header, '/fpv/tracker_trigger', 10)
imu_pub = node.create_publisher(Imu, '/fpv/gimbal', 10)
odom_pub = node.create_publisher(Odometry, '/fpv/odom', 10)
boxes = []
box_sub = node.create_subscription(
    Detection2D, '/fpv/target_bbox',
    lambda box: boxes.append((time.monotonic(), bool(box.header.frame_id))), 100)
process = None
rounds = []
endurance = []


def spin(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if process is not None and process.poll() is not None:
            raise RuntimeError(f'node exited: {process.returncode}')
        stamp = node.get_clock().now().to_msg()
        imu = Imu()
        imu.header.stamp = stamp
        imu.orientation.w = 1.0
        imu_pub.publish(imu)
        odom = Odometry()
        odom.header.stamp = stamp
        odom.pose.pose.orientation.w = 1.0
        odom_pub.publish(odom)
        rclpy.spin_once(node, timeout_sec=0.08)


def command(value):
    header = Header(frame_id=value)
    header.stamp = node.get_clock().now().to_msg()
    trigger.publish(header)


def resources():
    proc = Path('/proc') / str(process.pid)
    status = dict(line.split(':', 1) for line in (proc / 'status').read_text().splitlines()
                  if ':' in line)
    return dict(mono=time.monotonic(), rss_kb=int(status['VmRSS'].split()[0]),
                threads=int(status['Threads']), fds=len(list((proc / 'fd').iterdir())),
                boxes=len(boxes), valid_boxes=sum(valid for _, valid in boxes))


try:
    subprocess.run(['systemctl', 'stop', 'drone-tracker.service'], check=True)
    env = dict(os.environ, ROS_LOG_DIR=str(OUT / 'ros-log'))
    with (OUT / 'node.log').open('w') as log:
        process = subprocess.Popen([str(RELEASE / 'lib/drone_tracker/drone_tracker_node'),
                                    '--ros-args', '--params-file', str(config)],
                                   stdout=log, stderr=subprocess.STDOUT, env=env)
    deadline = time.monotonic() + 35
    while trigger.get_subscription_count() == 0:
        if time.monotonic() > deadline:
            raise RuntimeError('trigger discovery timeout')
        spin(0.1)
    spin(0.3)
    for number in range(1, 21):
        before = len(boxes)
        command('ON')
        spin(3.0 if number == 1 else 2.5)
        if len(boxes) <= before:
            raise RuntimeError(f'round {number} produced no frame')
        command('OFF')
        spin(0.6)
        sample = resources()
        sample.update(round=number, new_boxes=len(boxes) - before)
        rounds.append(sample)
        (OUT / 'rounds.json').write_text(json.dumps(rounds, indent=2))
        print(f'ROUND {number} boxes={sample["new_boxes"]}', flush=True)
    before = len(boxes)
    command('ON')
    deadline = time.monotonic() + 35
    while len(boxes) == before:
        if time.monotonic() > deadline:
            raise RuntimeError('endurance round produced no frame')
        spin(0.1)
    live = time.monotonic()
    live_wall = time.time()
    while time.monotonic() - live < 610:
        spin(0.2)
        if not endurance or time.monotonic() - endurance[-1]['mono'] >= 1:
            endurance.append(resources())
            (OUT / 'endurance-resources.json').write_text(json.dumps(endurance, indent=2))
    end = time.monotonic()
    end_wall = time.time()
    command('OFF')
    spin(0.6)
    (OUT / 'endurance.json').write_text(json.dumps({
        'live': live, 'live_wall': live_wall, 'end': end, 'end_wall': end_wall,
        'duration_sec': end - live, 'new_boxes': len(boxes) - before,
        'valid_boxes': sum(valid for _, valid in boxes[before:]),
        'rounds_same_process': len(rounds), 'pid': process.pid,
    }, indent=2))
finally:
    if process is not None and process.poll() is None:
        process.send_signal(signal.SIGINT)
        process.wait(timeout=30)
    assert Path('/opt/drone_tracker/config/camera.yaml').read_bytes() == ORIGINAL
    assert Path('/opt/drone_tracker/current').resolve() == RELEASE
    if SERVICE_ACTIVE:
        subprocess.run(['systemctl', 'start', 'drone-tracker.service'], check=True)
    (OUT / 'restore.json').write_text(json.dumps({
        'service_active': subprocess.run(['systemctl', 'is-active', '--quiet',
                                          'drone-tracker.service']).returncode == 0,
        'config_and_release_unchanged': True,
        'node_exit_code': None if process is None else process.returncode,
        'rounds': len(rounds),
    }, indent=2))
    node.destroy_node()
    rclpy.shutdown()

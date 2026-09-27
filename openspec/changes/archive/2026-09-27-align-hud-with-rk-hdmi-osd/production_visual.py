"""在生产板用真实 DRM 扫描平面记录固定回放的 HUD 状态。"""
import json
import os
import signal
import subprocess
import time
from pathlib import Path

import rclpy
import yaml
from mavros_msgs.msg import GPSRAW, State
from nav_msgs.msg import Odometry
from sensor_msgs.msg import BatteryState, Imu, NavSatFix
from std_msgs.msg import Bool, Header
from vision_msgs.msg import Detection2D


ROOT = Path('/home/hzhy/hdmi-hud-acceptance-20260924')
OUT = ROOT / 'visual'
OUT.mkdir(exist_ok=True)
ORIGINAL = Path('/opt/drone_tracker/config/camera.yaml').read_bytes()
RELEASE = Path('/opt/drone_tracker/current').resolve()
SERVICE_ACTIVE = subprocess.run(['systemctl', 'is-active', '--quiet', 'drone-tracker.service']).returncode == 0
os.environ.update(ROS_DOMAIN_ID='197', ROS_LOCALHOST_ONLY='1', RCUTILS_COLORIZED_OUTPUT='0')

params = yaml.safe_load(ORIGINAL)
cfg = params['drone_tracker_node']['ros__parameters']
cfg['video_source'] = str(ROOT / 'visual-loss-1920x1200.avi')
cfg['tracker']['yolo_engine'] = '/opt/drone_tracker/models/yolo.rknn'
cfg['tracker']['yolo_target_class_id'] = 2
cfg['camera'].update(fx=1875., fy=1875., cx=960., cy=600.)
cfg['recording']['enabled'] = False
cfg['visualized_result'] = False
cfg['need_save'] = False
cfg['osd'].update(enabled=True, render_path='cached-overlay', timing_enabled=False)
config = OUT / 'visual.yaml'
config.write_text(yaml.safe_dump(params, sort_keys=False))

rclpy.init()
node = rclpy.create_node('hud_visual_acceptance')
trigger = node.create_publisher(Header, '/fpv/tracker_trigger', 10)
state_pub = node.create_publisher(State, '/mavros/state', 10)
gps_pub = node.create_publisher(GPSRAW, '/mavros/gpsstatus/gpsraw', 10)
position_pub = node.create_publisher(NavSatFix, '/mavros/global_position/global', 10)
imu_pub = node.create_publisher(Imu, '/mavros/imu/data', 10)
battery_pub = node.create_publisher(BatteryState, '/mavros/battery', 10)
failsafe_pub = node.create_publisher(Bool, '/fpv/failsafe', 10)
odom_pub = node.create_publisher(Odometry, '/fpv/odom', 10)
events = []
commands = []
valid_count = 0
lost_count = 0
seen_valid = False
consecutive_missing = 0


def receive_box(box):
    global valid_count, lost_count, seen_valid, consecutive_missing
    valid = bool(box.header.frame_id)
    valid_count += int(valid)
    lost_count += int(seen_valid and not valid)
    consecutive_missing = 0 if valid else consecutive_missing + int(seen_valid)
    seen_valid |= valid


box_sub = node.create_subscription(Detection2D, '/fpv/target_bbox', receive_box, 100)
process = None
armed = False
send_telemetry = True
failsafe = False
longitude = 120.0


def tick(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if process is not None and process.poll() is not None:
            raise RuntimeError(f'node exited: {process.returncode}')
        stamp = node.get_clock().now().to_msg()
        if send_telemetry:
            state = State(connected=True, armed=armed, mode='GUIDED')
            state_pub.publish(state)
            gps = GPSRAW(fix_type=3, satellites_visible=12, vel=830, cog=9000)
            gps_pub.publish(gps)
            position = NavSatFix()
            position.header.stamp = stamp
            position.status.status = 0
            position.latitude = 30.0
            position.longitude = longitude
            position_pub.publish(position)
            imu = Imu()
            imu.header.stamp = stamp
            imu.orientation.w = 1.0
            imu_pub.publish(imu)
            battery_pub.publish(BatteryState(present=True, voltage=22.2, current=4.3,
                                             percentage=0.72, capacity=10.0, charge=6.0))
            failsafe_pub.publish(Bool(data=failsafe))
        odom = Odometry()
        odom.header.stamp = stamp
        odom.pose.pose.orientation.w = 1.0
        odom_pub.publish(odom)
        rclpy.spin_once(node, timeout_sec=0.08)


def command(value):
    header = Header(frame_id=value)
    header.stamp = node.get_clock().now().to_msg()
    commands.append(dict(value=value, wall=time.time(), mono=time.monotonic()))
    (OUT / 'commands.json').write_text(json.dumps(commands, indent=2))
    trigger.publish(header)


def capture(name):
    for plane, label in [(58, 'video'), (103, 'overlay')]:
        path = OUT / f'{name}-{label}.bgra'
        with path.open('wb') as dst:
            subprocess.run(['kmsgrab', '--plane', str(plane)], stdout=dst, check=True,
                           stderr=subprocess.DEVNULL)
        if path.stat().st_size != 720 * 480 * 4:
            raise RuntimeError(f'invalid DRM readback: {path}')
    events.append(dict(name=name, wall=time.time(), mono=time.monotonic(),
                       valid_boxes=valid_count, lost_boxes=lost_count,
                       telemetry=send_telemetry, armed=armed, failsafe=failsafe))
    (OUT / 'events.json').write_text(json.dumps(events, indent=2))


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
        tick(0.1)
    tick(0.5)
    armed = True
    tick(0.5)
    longitude = 120.002
    command('ON')
    while valid_count == 0:
        if time.monotonic() > deadline:
            raise RuntimeError('first valid target timeout')
        tick(0.1)
    tick(0.5)
    capture('normal-target-home')
    failsafe = True
    tick(0.15)
    capture('warning-a')
    for name in ('warning-b', 'warning-c', 'warning-d', 'warning-e', 'warning-f'):
        tick(0.15)
        capture(name)
    failsafe = False
    send_telemetry = False
    tick(2.5)
    capture('missing-telemetry')
    command('OFF')
    tick(0.7)
    capture('off-frozen')
    previous_valid = valid_count
    command('ON')
    restart_deadline = time.monotonic() + 15
    while valid_count == previous_valid:
        if time.monotonic() > restart_deadline:
            raise RuntimeError('new round did not reacquire a target')
        tick(0.1)
    tick(0.5)
    capture('restart-round')
    loss_deadline = time.monotonic() + 25
    while consecutive_missing < 10:
        if time.monotonic() > loss_deadline:
            raise RuntimeError('target loss not observed in controlled video')
        tick(0.1)
    tick(0.5)
    capture('target-lost')
    command('OFF')
    tick(0.5)
finally:
    stop_seconds = None
    if process is not None and process.poll() is None:
        stop_begin = time.monotonic()
        process.send_signal(signal.SIGINT)
        process.wait(timeout=30)
        stop_seconds = time.monotonic() - stop_begin
    assert Path('/opt/drone_tracker/config/camera.yaml').read_bytes() == ORIGINAL
    assert Path('/opt/drone_tracker/current').resolve() == RELEASE
    if SERVICE_ACTIVE:
        subprocess.run(['systemctl', 'start', 'drone-tracker.service'], check=True)
    (OUT / 'restore.json').write_text(json.dumps({
        'service_active': subprocess.run(['systemctl', 'is-active', '--quiet',
                                          'drone-tracker.service']).returncode == 0,
        'config_and_release_unchanged': True,
        'node_exit_code': None if process is None else process.returncode,
        'process_stop_seconds': stop_seconds,
        'valid_boxes': valid_count,
        'lost_boxes': lost_count,
    }, indent=2))
    node.destroy_node()
    rclpy.shutdown()

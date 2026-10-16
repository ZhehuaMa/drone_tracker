"""Production board: 20 ON/OFF rounds and 610 seconds in the same node."""
import json
import os
import signal
import subprocess
import time
from pathlib import Path

import rclpy
import yaml
from nav_msgs.msg import Odometry
from sensor_msgs.msg import Imu, NavSatFix
from mavros_msgs.msg import State
from std_msgs.msg import Header
from vision_msgs.msg import Detection2D
from acceptance_safety import Diagnostics, cleanup_and_restore, service_states


ROOT = Path('/home/hzhy/osd-gps-armed-status-20261016')
OUT = ROOT / 'rounds-endurance'
OUT.mkdir(exist_ok=True)
ORIGINAL = Path('/opt/drone_tracker/config/camera.yaml').read_bytes()
RELEASE = Path('/opt/drone_tracker/current').resolve()
SERVICE_ACTIVE = subprocess.run(['systemctl', 'is-active', '--quiet', 'drone-tracker.service']).returncode == 0
os.environ.update(ROS_DOMAIN_ID='197', ROS_LOCALHOST_ONLY='1', RCUTILS_COLORIZED_OUTPUT='0')

params = yaml.safe_load(ORIGINAL)
cfg = params['drone_tracker_node']['ros__parameters']
cfg['video_source'] = '/home/hzhy/hdmi-hud-acceptance-20260924/fixed-drone-1920x1200-650s-60fps.avi'
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
flight_pub = node.create_publisher(State, '/mavros/state', 10)
position_pub = node.create_publisher(NavSatFix, '/mavros/global_position/global', 10)
boxes = []
box_sub = node.create_subscription(
    Detection2D, '/fpv/target_bbox',
    lambda box: boxes.append((time.monotonic(), bool(box.header.frame_id))), 100)
process = None
rounds = []
endurance = []
commands = []
diagnostics = Diagnostics(OUT / 'checkpoints.jsonl')
initial_services = service_states()


def spin(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        diagnostics.check(resources if process is not None and process.poll() is None else None)
        if process is not None and process.poll() is not None:
            raise RuntimeError(f'node exited: {process.returncode}')
        flight_pub.publish(State(connected=True, armed=True, mode='OFFBOARD'))
        fix = NavSatFix(); fix.status.status = 0; fix.latitude = -90.; fix.longitude = -180.
        position_pub.publish(fix)
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
    commands.append(dict(value=value, mono=time.monotonic(), wall=time.time()))
    (OUT / 'commands.json').write_text(json.dumps(commands, indent=2))
    header = Header(frame_id=value)
    header.stamp = node.get_clock().now().to_msg()
    trigger.publish(header)


def resources():
    proc = Path('/proc') / str(process.pid)
    status = dict(line.split(':', 1) for line in (proc / 'status').read_text().splitlines()
                  if ':' in line)
    return dict(main_affinity=status['Cpus_allowed_list'].strip(),
                thread_affinities=sorted(set(next(line.split(':')[1].strip() for line in f.read_text().splitlines() if line.startswith('Cpus_allowed_list:')) for f in (proc / 'task').glob('*/status'))),
                cpu_ticks=sum(int(x) for x in (proc/'stat').read_text().split()[13:15]), mono=time.monotonic(), rss_kb=int(status['VmRSS'].split()[0]),
                threads=int(status['Threads']), fds=len(list((proc / 'fd').iterdir())),
                boxes=len(boxes), valid_boxes=sum(valid for _, valid in boxes),
                temps={str(f): f.read_text().strip() for f in Path('/sys/class/thermal').glob('thermal_zone*/temp')})


diagnostics.record('identity', initial_services=initial_services, original_release=str(RELEASE))
diagnostics.install_signal_handlers()
try:
    diagnostics.check(force=True)
    subprocess.run(['systemctl', 'stop', 'drone-tracker.service'], check=True)
    env = dict(os.environ, ROS_LOG_DIR=str(OUT / 'ros-log'), DRONE_TRACKER_CPU_CONFIG='/opt/drone_tracker/config/cpu-affinity.yaml', LD_LIBRARY_PATH='/opt/drone_tracker/current/lib/bundled-ros:'+os.environ.get('LD_LIBRARY_PATH',''))
    with (OUT / 'node.log').open('w') as log:
        process = subprocess.Popen(['taskset', '-c', '4-7', str(ROOT / 'install/lib/drone_tracker/drone_tracker_node'),
                                    '--ros-args', '--params-file', str(config)],
                                   stdout=log, stderr=subprocess.STDOUT, env=env, start_new_session=True)
    diagnostics.record('node_started', pid=process.pid, command=process.args)
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
except BaseException as error:
    diagnostics.record('failed', error=str(error), acceptance_completed=False)
    raise
finally:
    def restore():
        if SERVICE_ACTIVE:
            subprocess.run(['systemctl', 'start', 'drone-tracker.service'], check=True, timeout=60)

    def verify_restore():
        evidence = dict(services=service_states(),
                        config_and_release_unchanged=Path('/opt/drone_tracker/config/camera.yaml').read_bytes() == ORIGINAL and Path('/opt/drone_tracker/current').resolve() == RELEASE,
                        node_exit_code=None if process is None else process.returncode, rounds=len(rounds))
        evidence['service_active'] = evidence['services']['drone-tracker'] == 'active'
        evidence['services_restored'] = evidence['services'] == initial_services
        (OUT / 'restore.json').write_text(json.dumps(evidence, indent=2))
        assert evidence['config_and_release_unchanged'], 'production config/release changed'
        assert evidence['services_restored'], 'service states differ from baseline'
        return evidence

    try:
        cleanup_and_restore(process, diagnostics, restore, verify_restore)
    finally:
        try:
            node.destroy_node()
        finally:
            rclpy.shutdown()
diagnostics.record('completed', acceptance_completed=True)

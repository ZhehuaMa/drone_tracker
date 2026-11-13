#!/usr/bin/env python3
"""Bounded production-board workload; use matching ROS Python and an isolated domain."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import signal
import subprocess
import time
import threading

import yaml


CATALOG = (
    ('body_imu', '/mavros/imu/data', 'sensor_msgs/msg/Imu', 200),
    ('gimbal', '/fpv/gimbal', 'sensor_msgs/msg/Imu', 200),
    ('odom', '/fpv/odom', 'nav_msgs/msg/Odometry', 100),
    ('rc_in', '/mavros/rc/in', 'mavros_msgs/msg/RCIn', 50),
    ('flight_state', '/mavros/state', 'mavros_msgs/msg/State', 1),
    ('extended_state', '/mavros/extended_state', 'mavros_msgs/msg/ExtendedState', 1),
    ('battery', '/mavros/battery', 'sensor_msgs/msg/BatteryState', 1),
    ('pressure', '/fpv/static_pressure', 'sensor_msgs/msg/FluidPressure', 50),
    ('temperature', '/fpv/temperature_baro', 'sensor_msgs/msg/Temperature', 50),
    ('low_level_feedback', '/fpv/low_level_feedback', 'quadrotor_msgs/msg/LowLevelFeedback', 50),
    ('gimbal_mode', '/fpv/gimbal_mode', 'std_msgs/msg/String', 1),
    ('altitude', '/fpv/altitude', 'std_msgs/msg/Float64', 50),
    ('gps_raw', '/mavros/gpsstatus/gpsraw', 'mavros_msgs/msg/GPSRAW', 5),
    ('gps_fix', '/mavros/global_position/global', 'sensor_msgs/msg/NavSatFix', 5),
    ('failsafe', '/fpv/failsafe', 'std_msgs/msg/Bool', 1),
    ('motor_rpm', '/fpv/motor_rpm', 'std_msgs/msg/Float32MultiArray', 50),
    ('control', '/fpv/control_command', 'quadrotor_msgs/msg/ControlCommand', 50),
    ('control_raw', '/fpv/control_command_raw', 'quadrotor_msgs/msg/ControlCommand', 50),
    ('arm', '/fpv/arm', 'std_msgs/msg/Bool', 1),
    ('offboard', '/fpv/offboard', 'std_msgs/msg/Bool', 1),
    ('radar_state', '/fpv/radar_state', 'std_msgs/msg/Int8', 1),
    ('mission_state', '/fpv/mission_state', 'std_msgs/msg/Bool', 1),
    ('mission_start', '/fpv/mission_start', 'std_msgs/msg/Bool', 1),
    ('status_text', '/mavros/statustext/send', 'mavros_msgs/msg/StatusText', 1),
    ('radar_target', '/fpv/radar/target', 'nav_msgs/msg/Odometry', 20),
    ('radar_interceptor', '/fpv/radar/interceptor', 'nav_msgs/msg/Odometry', 20),
)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def process_sample(pid):
    root = Path('/proc') / str(pid)
    status = dict(line.split(':', 1) for line in (root / 'status').read_text().splitlines())
    ticks = (root / 'stat').read_text().rsplit(')', 1)[1].split()
    io_path = root / 'io'
    io = dict(line.split(':', 1) for line in io_path.read_text().splitlines()) if io_path.exists() else {}
    return dict(monotonic_ns=time.monotonic_ns(), cpu_ticks=int(ticks[11]) + int(ticks[12]),
                pid=pid,
                rss_kib=int(status['VmRSS'].split()[0]), threads=int(status['Threads']),
                fds=len(list((root / 'fd').iterdir())),
                read_bytes=int(io['read_bytes']) if io else None,
                write_bytes=int(io['write_bytes']) if io else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--config', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--profile', choices=('previous', 'expanded', 'disabled', 'images'), required=True)
    parser.add_argument('--rounds', type=int, default=1)
    parser.add_argument('--active-seconds', type=float, default=15)
    args = parser.parse_args()
    if args.rounds < 1 or args.active_seconds <= 0 or args.rounds * (args.active_seconds + 1) + 20 > 170:
        parser.error('Keep each complete workload, including shutdown, below 180 seconds')
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=False)
    os.sched_setaffinity(0, {0, 1, 2, 3})
    domain = secrets.randbelow(200) + 1
    os.environ['ROS_DOMAIN_ID'] = str(domain)
    os.environ['ROS_LOCALHOST_ONLY'] = '1'
    import rclpy
    from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
    from rclpy.serialization import serialize_message
    from rosidl_runtime_py.utilities import get_message
    from std_msgs.msg import Header, Int8
    document = yaml.safe_load(Path(args.config).read_text())
    params = document['drone_tracker_node']['ros__parameters']
    if args.profile == 'previous':
        params.pop('bag', None)
        for name in list(params):
            if name.startswith('bag.'):
                del params[name]
    params['need_save'] = args.profile != 'disabled'
    params['visualized_result'] = args.profile == 'images'
    params['save_path'] = str(output / 'bags')
    params['recording']['directory'] = str(output / 'video')
    params['diagnostics']['socket'] = str(output / 'diagnostics.sock')
    config = output / 'camera.yaml'
    config.write_text(yaml.safe_dump(document, sort_keys=False))
    env = dict(os.environ, DRONE_TRACKER_ACCEPTANCE_DIRECTORY=str(output / 'measurements'))
    (output / 'measurements').mkdir()
    report = dict(profile=args.profile, domain=domain, binary=args.binary,
                  binary_sha256=digest(args.binary), config_sha256=digest(config),
                  source_config_sha256=digest(args.config), clock_ticks=os.sysconf('SC_CLK_TCK'),
                  fixture_cpu_affinity=sorted(os.sched_getaffinity(0)),
                  samples=[], rounds=[], inputs={}, errors=[])
    started = time.monotonic()
    process = None
    collector = None
    publisher_thread = None
    publisher_stop = threading.Event()
    active_flag = threading.Event()
    node = None
    rclpy.init()
    try:
        diagnostic_log = (output / 'diagnostics.log').open('w')
        collector = subprocess.Popen(['taskset', '-c', '6',
            '/opt/drone_tracker/current/lib/drone_tracker/rk3576_diagnostic_worker',
            '--socket', params['diagnostics']['socket'], '--directory', str(output / 'diagnostics'),
            '--sync-ms', '100', '--segment-bytes', '67108864', '--total-bytes', '1073741824',
            '--minimum-free-bytes', '5368709120'], env=env, stdout=diagnostic_log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 5
        while not Path(params['diagnostics']['socket']).exists():
            if collector.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError('temporary diagnostic collector did not start')
            time.sleep(.02)
        node = rclpy.create_node('session_recording_board_source')
        state_times = []
        node.create_subscription(Int8, '/fpv/tracker_state', lambda value: state_times.append(time.monotonic()), 10)
        trigger = node.create_publisher(Header, '/fpv/tracker_trigger',
                                        QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT))
        sources = []
        for identifier, name, type_name, hz in CATALOG:
            qos = QoSProfile(depth=10)
            if identifier == 'gimbal_mode':
                qos.depth = 1
                qos.durability = DurabilityPolicy.TRANSIENT_LOCAL
            message_type = get_message(type_name)
            publisher = node.create_publisher(message_type, name, qos)
            sources.append((identifier, publisher, message_type, hz))
            report['inputs'][identifier] = dict(topic=name, type=type_name, requested_hz=hz,
                                                count=0, serialized_bytes=0, first_ns=None, last_ns=None)
        log = (output / 'tracker.log').open('w')
        process = subprocess.Popen(['taskset', '-c', '4-7', args.binary, '--ros-args', '--params-file', str(config)],
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
        next_sample = 0
        next_messages = {}

        def publish_loop():
            while not publisher_stop.is_set():
                now = time.monotonic()
                for identifier, publisher, message_type, hz in sources:
                    if not active_flag.is_set() and identifier != 'gimbal_mode':
                        continue
                    if now < next_messages.get(identifier, 0):
                        continue
                    next_messages[identifier] = now + 1 / hz
                    value = message_type()
                    stamp = node.get_clock().now().to_msg()
                    if hasattr(value, 'header'):
                        value.header.stamp = stamp
                        value.header.frame_id = 'fixture_' + identifier
                    if identifier in ('body_imu', 'gimbal'):
                        value.orientation.w = 1.0
                    if identifier == 'gimbal_mode':
                        value.data = 'AIM'
                    if identifier in ('control', 'control_raw'):
                        value.rotor_thrusts = [1., 2., 3., 4.]
                        execution = stamp.sec * 1_000_000_000 + stamp.nanosec + 100_000_000
                        value.expected_execution_time.sec = execution // 1_000_000_000
                        value.expected_execution_time.nanosec = execution % 1_000_000_000
                    if identifier == 'low_level_feedback':
                        value.motor_speeds = [100, 200, 300, 400]
                        value.thrust_mapping_coeffs = [1., 2., 3.]
                    if identifier == 'motor_rpm':
                        value.data = [100., 200., 300., 400.]
                    if identifier == 'flight_state':
                        value.connected = True
                        value.mode = 'STABILIZE'
                    if identifier == 'status_text':
                        value.text = 'recording acceptance fixture'
                    publisher.publish(value)
                    entry = report['inputs'][identifier]
                    entry['count'] += 1
                    entry['serialized_bytes'] += len(serialize_message(value))
                    emitted = time.monotonic_ns()
                    entry['first_ns'] = entry['first_ns'] or emitted
                    entry['last_ns'] = emitted
                time.sleep(.0005)

        publisher_thread = threading.Thread(target=publish_loop, name='typed-fixture-publisher')
        publisher_thread.start()

        def pump(seconds, active=False):
            nonlocal next_sample
            if active:
                active_flag.set()
            else:
                active_flag.clear()
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                if time.monotonic() - started > 170:
                    raise RuntimeError('170-second workload guard reached')
                if not publisher_thread.is_alive():
                    raise RuntimeError('fixture publisher thread stopped')
                if process.poll() is not None:
                    raise RuntimeError(f'tracker exited early: {process.returncode}')
                rclpy.spin_once(node, timeout_sec=0)
                now = time.monotonic()
                if now >= next_sample:
                    try:
                        sample = process_sample(process.pid)
                        sample['children'] = []
                        for child in Path('/proc').glob('[0-9]*'):
                            try:
                                fields = (child / 'stat').read_text().rsplit(')', 1)[1].split()
                                if int(fields[1]) == process.pid:
                                    sample['children'].append(process_sample(int(child.name)))
                            except FileNotFoundError:
                                pass
                        sample['collector'] = process_sample(collector.pid)
                        sample['system_cpu'] = [int(value) for value in Path('/proc/stat').read_text().splitlines()[0].split()[1:]]
                        device = os.stat(output).st_dev
                        disk_stat = Path(f'/sys/dev/block/{os.major(device)}:{os.minor(device)}/stat')
                        sample['filesystem_device'] = str(disk_stat.resolve())
                        sample['filesystem_io'] = [int(value) for value in disk_stat.read_text().split()]
                        report['samples'].append(sample)
                    except FileNotFoundError:
                        # /proc entries can disappear during exec/exit; the next poll reports process state.
                        pass
                    next_sample = now + .2
                time.sleep(.001)

        pump(3)
        if (output / 'bags').exists() and list((output / 'bags').iterdir()):
            raise RuntimeError('startup preview created a bag')
        if trigger.get_subscription_count() == 0:
            raise RuntimeError('trigger did not connect')
        for index in range(args.rounds):
            before = time.monotonic()
            on = Header(stamp=node.get_clock().now().to_msg(), frame_id='ON')
            trigger.publish(on)
            pump(args.active_seconds, active=True)
            off = Header(stamp=node.get_clock().now().to_msg(), frame_id='OFF')
            trigger.publish(off)
            report['rounds'].append(dict(index=index + 1, start_monotonic=before,
                                         off_monotonic=time.monotonic(),
                                         state_messages=sum(value >= before for value in state_times)))
            pump(.6)
        pump(2)
        process.send_signal(signal.SIGINT)
        process.wait(timeout=20)
        report['exit_status'] = process.returncode
        report['duration_seconds'] = time.monotonic() - started
        if process is not None:
            report['exit_status'] = process.returncode
        if process.returncode != 0:
            raise RuntimeError(f'nonzero normal shutdown: {process.returncode}')
        if 'Camera recording phase=failed' in (output / 'tracker.log').read_text():
            raise RuntimeError('independent camera recording failed; this is not combined-workload acceptance')
        if 'Diagnostics degraded=1' in (output / 'tracker.log').read_text():
            raise RuntimeError('diagnostics degraded; this is not healthy combined-workload acceptance')
        if not report['samples']:
            raise RuntimeError('no resource samples were available')
        if any(item['state_messages'] == 0 for item in report['rounds']):
            raise RuntimeError('one or more ON rounds produced no tracker state')
    except Exception as error:
        report['errors'].append(str(error))
        raise
    finally:
        publisher_stop.set()
        if publisher_thread is not None:
            publisher_thread.join(timeout=5)
        if process is not None and process.poll() is None:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=5)
        report['duration_seconds'] = time.monotonic() - started
        (output / 'workload.json').write_text(json.dumps(report, indent=2) + '\n')
        if node is not None:
            node.destroy_node()
        rclpy.shutdown()
        if collector is not None and collector.poll() is None:
            collector.send_signal(signal.SIGINT)
            collector.wait(timeout=10)


if __name__ == '__main__':
    main()

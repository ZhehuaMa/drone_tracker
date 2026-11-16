"""Bounded production-camera acceptance with a live balloon target."""
import json
import os
import pathlib
import signal
import subprocess
import sys
import time

import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy
from std_msgs.msg import Header


def resources(pid):
    root = pathlib.Path('/proc') / str(pid)
    if not root.exists():
        return None
    status = root.joinpath('status').read_text()
    return dict(status=status, descriptors=len(list(root.joinpath('fd').iterdir())),
                threads=len(list(root.joinpath('task').iterdir())))


def main():
    root = pathlib.Path(sys.argv[1])
    release = root / 'release' / 'payload'
    recordings = root / 'recordings'
    recordings.mkdir(exist_ok=True)
    config = root / 'camera.yaml'
    environment = os.environ.copy()
    environment['LD_LIBRARY_PATH'] = str(release / 'lib/bundled-ros') + ':' + environment.get('LD_LIBRARY_PATH', '')
    environment['AMENT_PREFIX_PATH'] = str(release) + ':' + environment.get('AMENT_PREFIX_PATH', '')
    environment['DRONE_TRACKER_CPU_CONFIG'] = str(root / 'release/config/cpu-affinity.yaml')
    command = ['taskset', '-c', '2,4,5,6,7', str(release / 'lib/drone_tracker/drone_tracker_node'),
               '--ros-args', '--params-file', str(config)]
    results = dict(command=command, rounds=[], startup=time.monotonic_ns())
    log = root.joinpath('node.log').open('w')
    process = subprocess.Popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    rclpy.init()
    node = rclpy.create_node('recording_start_acceptance')
    publisher = node.create_publisher(Header, '/fpv/tracker_trigger',
                                     QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT))
    deadline = time.monotonic() + 145

    def spin(seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if process.poll() is not None:
                raise RuntimeError('Tracker exited during acceptance')
            if time.monotonic() > deadline:
                raise RuntimeError('Acceptance exceeded 145-second budget')
            rclpy.spin_once(node, timeout_sec=0.02)

    def trigger(command):
        message = Header()
        message.stamp = node.get_clock().now().to_msg()
        message.frame_id = command
        publisher.publish(message)
        return dict(command=command, monotonic_ns=time.monotonic_ns(), realtime_ns=time.time_ns())

    try:
        until = time.monotonic() + 15
        while publisher.get_subscription_count() < 1 or 'Camera service started' not in root.joinpath('node.log').read_text():
            if time.monotonic() > until:
                raise RuntimeError('Camera service did not become ready')
            spin(0.05)
        spin(0.5)
        results['before'] = resources(process.pid)
        for round_id in range(1, 21):
            item = dict(round=round_id, on=trigger('ON'))
            spin(3.2)
            item['off'] = trigger('OFF')
            until = time.monotonic() + 4
            while len(list(recordings.glob('session-*/complete.json'))) < round_id:
                if time.monotonic() > until:
                    raise RuntimeError('Recording did not complete round ' + str(round_id))
                spin(0.02)
            spin(0.4)
            item['resources'] = resources(process.pid)
            results['rounds'].append(item)
            root.joinpath('results.json').write_text(json.dumps(results, indent=2))
        results['after'] = resources(process.pid)
        results['combined_on_seconds'] = sum((x['off']['monotonic_ns'] - x['on']['monotonic_ns']) / 1e9
                                             for x in results['rounds'])
        results['success'] = True
    except BaseException as error:
        results['error'] = repr(error)
        raise
    finally:
        publisher.publish(Header(frame_id='OFF'))
        node.destroy_node()
        rclpy.shutdown()
        os.killpg(process.pid, signal.SIGINT)
        try:
            results['node_exit_status'] = process.wait(timeout=12)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            results['node_exit_status'] = process.wait(timeout=3)
        results['elapsed_seconds'] = (time.monotonic_ns() - results['startup']) / 1e9
        root.joinpath('results.json').write_text(json.dumps(results, indent=2))
        log.close()


if __name__ == '__main__':
    main()

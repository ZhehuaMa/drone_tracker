#!/usr/bin/env python3
"""指定生产板隔离验收。所有进程属于本实例；由 bounded_run.py 提供外部截止。"""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import time
import sys

import yaml
import rclpy
from rclpy.qos import qos_profile_sensor_data
from std_msgs.msg import Header, Int8
from sensor_msgs.msg import Imu
from nav_msgs.msg import Odometry
from vision_msgs.msg import Detection2D


ROOT = Path(os.environ.get('DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT', '/home/hzhy/drone-diagnostic-acceptance-20261012'))
BIN = ROOT / 'drone_tracker/lib/drone_tracker'
CPUS = ','.join(str(cpu) for cpu in yaml.safe_load(
    Path('/opt/drone_tracker/config/cpu-affinity.yaml').read_text())['allowed_cpus'])


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def resource(pid):
    base = Path('/proc') / str(pid)
    if not base.exists():
        return {'pid': pid, 'gone': True}
    links = []
    for item in (base / 'fd').glob('*'):
        try:
            links.append(os.readlink(item))
        except FileNotFoundError:
            pass
    try:
        children = base / 'task' / str(pid) / 'children'
        return {'pid': pid, 'threads': len(list((base / 'task').glob('*'))), 'fds': len(links),
                'shared_regions': sum('memfd:drone-diagnostics' in x for x in links),
                'maps_regions': sum('memfd:drone-diagnostics' in x for x in (base / 'maps').read_text().splitlines()),
                'children': children.read_text().split() if children.exists() else None,
                'rss': [x for x in (base / 'status').read_text().splitlines() if x.startswith('VmRSS:')],
                'affinity': sorted(os.sched_getaffinity(pid)), 'scheduler': os.sched_getscheduler(pid)}
    except (FileNotFoundError, ProcessLookupError):
        return {'pid': pid, 'gone': True}


class Case:
    def __init__(self, directory):
        self.directory = directory
        directory.mkdir(parents=True, exist_ok=False)
        self.processes = []
        self.logs = []
        self.result = {'start_ns': time.monotonic_ns(), 'resources': [], 'rounds': [], 'boxes': [], 'states': []}
        os.environ['ROS_LOG_DIR'] = str(directory / 'control-ros')
        rclpy.init()
        self.node = rclpy.create_node('diagnostic_acceptance_control')
        self.trigger = self.node.create_publisher(Header, '/fpv/tracker_trigger', qos_profile_sensor_data)
        self.imu = self.node.create_publisher(Imu, '/fpv/gimbal', qos_profile_sensor_data)
        self.odom = self.node.create_publisher(Odometry, '/fpv/odom', qos_profile_sensor_data)
        self.subs = [
            self.node.create_subscription(Detection2D, '/fpv/target_bbox', self.box, 100),
            self.node.create_subscription(Int8, '/fpv/tracker_state', self.state, 100)]

    def box(self, msg):
        self.result['boxes'].append({'mono_ns': time.monotonic_ns(), 'stamp_sec': msg.header.stamp.sec,
                                    'stamp_ns': msg.header.stamp.nanosec, 'frame_id': msg.header.frame_id,
                                    'result_count': len(msg.results)})

    def state(self, msg):
        self.result['states'].append({'mono_ns': time.monotonic_ns(), 'state': msg.data})

    def spawn(self, name, command, env=None):
        log = (self.directory / (name + '.log')).open('w')
        self.logs.append(log)
        merged = dict(os.environ, ROS_LOG_DIR=str(self.directory / ('ros-' + name)), RCUTILS_COLORIZED_OUTPUT='0')
        if env:
            merged.update(env)
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=merged, start_new_session=True)
        self.processes.append((name, process))
        self.result.setdefault('commands', {})[name] = {'pid': process.pid, 'command': command, 'env': env}
        return process

    def stop(self, process, sig=signal.SIGINT):
        if process.poll() is None:
            # 正常退出只通知父业务，由既有所有权边界有限关闭录像子进程。
            process.send_signal(sig)
            try:
                process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=1)
                raise RuntimeError('隔离进程未在有限收尾内退出')

    def spin(self, seconds, business=None):
        end = time.monotonic() + seconds
        next_resource = 0
        while time.monotonic() < end:
            if business and business.poll() is not None:
                raise RuntimeError('隔离业务提前退出')
            stamp = self.node.get_clock().now().to_msg()
            imu = Imu()
            imu.header.stamp = stamp
            imu.orientation.w = 1.0
            self.imu.publish(imu)
            odom = Odometry()
            odom.header.stamp = stamp
            odom.pose.pose.orientation.w = 1.0
            self.odom.publish(odom)
            rclpy.spin_once(self.node, timeout_sec=0.02)
            if time.monotonic() >= next_resource:
                processes = {name: resource(p.pid) for name, p in self.processes if p.poll() is None}
                parents = {p.pid for _, p in self.processes if p.poll() is None}
                # 此内核没有 children 文件；用实际 PPid 关联已登记实例的子进程。
                children = []
                for directory in Path('/proc').glob('[0-9]*'):
                    try:
                        fields = (directory / 'stat').read_text().rsplit(') ', 1)[1].split()
                        if int(fields[1]) in parents:
                            children.append(resource(int(directory.name)))
                    except (FileNotFoundError, ProcessLookupError):
                        pass
                self.result['resources'].append({'mono_ns': time.monotonic_ns(),
                    'processes': processes, 'owned_children': children})
                next_resource = time.monotonic() + 1

    def command(self, text):
        msg = Header()
        msg.frame_id = text
        msg.stamp = self.node.get_clock().now().to_msg()
        self.trigger.publish(msg)
        self.result.setdefault('triggers', []).append({'command': text, 'mono_ns': time.monotonic_ns()})

    def worker(self, name, fault=None):
        command = ['taskset', '-c', CPUS, str(BIN / 'rk3576_diagnostic_worker'),
                   '--socket', str(self.directory / 'diagnostics.sock'),
                   '--directory', str(self.directory / 'diagnostics')]
        env = {'LD_PRELOAD': str(ROOT / 'fault_io.so'), 'DRONE_DIAGNOSTIC_TEST_FAULT': fault,
               'DRONE_DIAGNOSTIC_FAULT_ARM_PATH': str(self.directory / 'fault-armed')} if fault else None
        process = self.spawn(name, command, env)
        end = time.monotonic() + 3
        while not (self.directory / 'diagnostics.sock').exists():
            if time.monotonic() > end or process.poll() is not None:
                raise RuntimeError('日志实例启动失败')
            time.sleep(0.02)
        return process

    def business(self, name, mode, camera=False, hdmi=True):
        config = yaml.safe_load(Path('/opt/drone_tracker/config/camera.yaml').read_text())
        params = config['drone_tracker_node']['ros__parameters']
        params.update(need_save=False, visualized_result=True)
        params['diagnostics'] = {'mode': mode, 'socket': str(self.directory / 'diagnostics.sock')}
        params['osd']['enabled'] = hdmi
        params['osd']['timing_enabled'] = False
        params['recording']['enabled'] = camera
        params['recording']['directory'] = str(self.directory / 'recordings')
        params['recording']['segment_seconds'] = 10
        if not camera:
            params['video_source'] = str(ROOT / 'fixed-balloon.avi')
        config_file = self.directory / (name + '.yaml')
        config_file.write_text(yaml.safe_dump(config, sort_keys=False))
        samples = self.directory / name
        samples.mkdir()
        process = self.spawn(name, ['taskset', '-c', CPUS, str(BIN / 'drone_tracker_node'),
                          '--ros-args', '--params-file', str(config_file)],
                          {'DRONE_TRACKER_ACCEPTANCE_DIRECTORY': str(samples)})
        end = time.monotonic() + 10
        while not self.trigger.get_subscription_count():
            if time.monotonic() > end or process.poll() is not None:
                raise RuntimeError('业务启动或模型加载失败')
            self.spin(0.05, process)
        self.spin(0.3, process)
        return process

    def bag(self):
        return self.spawn('bag', ['ros2', 'bag', 'record', '-o', str(self.directory / 'bag'),
              '/fpv/target_bbox', '/fpv/tracker_state', '/fpv/odom'])

    def measure(self, process, mode, seconds=45, warmup=4):
        before = len(self.result['boxes'])
        self.command('ON')
        self.spin(warmup, process)
        start = time.monotonic_ns()
        self.spin(seconds, process)
        finish = time.monotonic_ns()
        self.command('OFF')
        self.spin(1.5, process)
        self.result['rounds'].append({'mode': mode, 'measurement_start_ns': start,
            'measurement_end_ns': finish, 'new_boxes': len(self.result['boxes']) - before})
        return start, finish

    def close(self):
        failures = []
        for name, process in reversed(self.processes):
            try:
                self.stop(process, signal.SIGCONT if process.poll() is None and name == 'paused' else signal.SIGINT)
            except Exception as error:
                failures.append(str(error))
            self.result.setdefault('exit', {})[name] = process.returncode
            self.result.setdefault('after', {})[name] = resource(process.pid)
        for log in self.logs:
            log.close()
        self.result['finish_ns'] = time.monotonic_ns()
        self.result['cleanup_errors'] = failures
        save(self.directory / 'case.json', self.result)
        self.node.destroy_node()
        rclpy.shutdown()
        if failures:
            raise RuntimeError('; '.join(failures))


def performance(case, modes):
    worker = case.worker('worker')
    bag = case.bag()
    for number, mode in enumerate(modes):
        name = str(number) + '-' + mode
        process = case.business(name, mode)
        case.measure(process, mode)
        case.stop(process)
        if process.returncode != 0:
            raise RuntimeError('性能实例收尾失败')
    case.stop(bag)
    case.stop(worker, signal.SIGTERM)


def lifecycle(case):
    worker = case.worker('worker')
    case.bag()
    business = case.business('camera-basic', 'basic', camera=True)
    for seconds in (18, 18, 18):
        case.measure(business, 'basic-camera', seconds=seconds, warmup=2)
    case.stop(business)
    case.stop(worker, signal.SIGTERM)


def fault(case, kind):
    worker = case.worker('worker', kind if kind in ('write', 'sync') else None)
    business = case.business('fault-business', 'basic', hdmi=False)
    case.command('ON')
    case.spin(4, business)
    if kind in ('write', 'sync'):
        (case.directory / 'fault-armed').touch()
        case.result['fault_armed_ns'] = time.monotonic_ns()
    if kind == 'business-kill':
        case.stop(business, signal.SIGKILL)
        case.spin(3)
        case.result['worker_after_business_kill'] = resource(worker.pid)
    elif kind == 'worker-kill':
        case.stop(worker, signal.SIGKILL)
        case.spin(2, business)
        replacement = case.worker('replacement')
        case.spin(9, business)
        case.result['reconnected'] = {'business': resource(business.pid), 'replacement': resource(replacement.pid)}
        case.command('OFF')
        case.spin(1, business)
        case.stop(business)
        case.stop(replacement, signal.SIGTERM)
    elif kind == 'full':
        os.kill(worker.pid, signal.SIGSTOP)
        try:
            case.spin(12, business)
        finally:
            os.kill(worker.pid, signal.SIGCONT)
        case.spin(4, business)
    else:
        case.spin(14, business)
    if business.poll() is None:
        case.command('OFF')
        case.spin(1, business)
        case.stop(business)
    case.stop(worker, signal.SIGTERM)


def inspect(directory):
    raw = directory / 'diagnostics'
    if not raw.exists():
        return
    for run in sorted(x for x in raw.iterdir() if x.is_dir()):
        output = directory / ('export-' + run.name)
        result = subprocess.run([str(BIN / 'rk3576_diagnostic_inspect'), str(run), '--output', str(output)],
                                capture_output=True, text=True, timeout=15)
        (directory / ('inspect-' + run.name + '.txt')).write_text(result.stdout + result.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=tuple('pair' + str(index) for index in range(1, 11)) + ('detail', 'lifecycle',
                                       'business-kill', 'worker-kill', 'full', 'write', 'sync',
                                       'write-after-data', 'sync-after-data'))
    args = parser.parse_args()
    case = Case(ROOT / 'evidence' / args.kind)
    def terminate(signum, frame):
        raise RuntimeError('外部截止已请求有限收尾')
    signal.signal(signal.SIGTERM, terminate)
    try:
        if args.kind.startswith('pair'):
            performance(case, ['basic', 'off'] if int(args.kind[4:]) % 2 == 0 else ['off', 'basic'])
        elif args.kind == 'detail':
            performance(case, ['basic', 'detail'])
        elif args.kind == 'lifecycle':
            lifecycle(case)
        else:
            fault(case, args.kind.removesuffix('-after-data'))
        case.result['execution_passed'] = True
    except Exception as error:
        case.result['execution_passed'] = False
        case.result['error'] = repr(error)
        raise
    finally:
        case.close()
        inspect(case.directory)


if __name__ == '__main__':
    main()

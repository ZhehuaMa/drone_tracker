#!/usr/bin/env python3
"""外部限定全部后代与登记服务的总窗口；实际回收失败必须失败。"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


def descendants(root):
    """PID 加启动时间防复用；Linux 子收割者会接管脱离会话的孤儿。"""
    table = {}
    for directory in Path('/proc').glob('[0-9]*'):
        try:
            fields = (directory / 'stat').read_text().rsplit(') ', 1)[1].split()
            table[int(directory.name)] = (int(fields[1]), fields[19], fields[0])
        except (OSError, ValueError, IndexError):
            pass
    owned = {root}
    while True:
        added = {pid for pid, (parent, _, _) in table.items() if parent in owned}
        if added <= owned:
            break
        owned |= added
    return {pid: table[pid][1:] for pid in owned - {root} if pid in table}


def signal_owned(owned, sig):
    for pid, (birth, _) in owned.items():
        try:
            fields = Path(f'/proc/{pid}/stat').read_text().rsplit(') ', 1)[1].split()
            if fields[19] == birth:
                os.kill(pid, sig)
        except (OSError, IndexError):
            pass


def reap(controller, owned=None):
    while True:
        try:
            children = [pid for pid, (_, state) in (owned if owned is not None else descendants(os.getpid())).items()
                        if pid != controller and state == "Z"]
            if not children:
                return
            for pid in children:
                try:
                    os.waitpid(pid, os.WNOHANG)
                except ChildProcessError:
                    pass
            return
        except ChildProcessError:
            return


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, default=150)
    parser.add_argument('--prepare-seconds', type=float, default=0)
    parser.add_argument('--warmup-seconds', type=float, default=0)
    parser.add_argument('--measurement-seconds', type=float)
    parser.add_argument('--cleanup-seconds', type=float, default=5)
    parser.add_argument('--camera', action='store_true')
    parser.add_argument('--fault', action='store_true')
    parser.add_argument('--output', required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    limit = 120 if args.fault else (180 if args.camera else 600)
    if not 0 < args.cleanup_seconds < args.seconds <= limit or not args.command:
        parser.error('预算含有限收尾；故障≤120秒、相机≤180秒、其他≤600秒')
    measurement = args.measurement_seconds
    if measurement is None:
        measurement = args.seconds - args.prepare_seconds - args.warmup_seconds - args.cleanup_seconds
    if min(args.prepare_seconds, args.warmup_seconds, measurement) < 0 or (
            args.prepare_seconds + args.warmup_seconds + measurement + args.cleanup_seconds > args.seconds):
        parser.error('各阶段之和不得超过总窗口')
    destination = Path(args.output)
    if destination.exists():
        parser.error('输出文件已存在，禁止覆盖')
    if sys.platform != 'linux' or ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0) != 0:
        parser.error('完整回收要求 Linux PR_SET_CHILD_SUBREAPER；不支持时拒绝启动测试')
    started = time.monotonic()
    deadline = started + args.seconds
    cutoff = deadline - args.cleanup_seconds
    registry = tempfile.NamedTemporaryFile(prefix='diagnostic-units-', delete=False)
    registry.close()
    env = dict(os.environ, DRONE_DIAGNOSTIC_BOUNDED_UNITS=registry.name)
    process = subprocess.Popen(args.command, start_new_session=True, env=env)
    timed_out, returncode, units = False, None, set()
    observed = set()
    def read_units():
        units.update(line for line in Path(registry.name).read_text().splitlines()
                     if line.startswith('drone-diag-') and line.endswith('.service'))
    def unit_action(action, budget):
        if not units:
            return
        command = ['systemctl', 'kill', '--kill-who=all', '--signal=' + action, *sorted(units)]
        try:
            subprocess.run(command, capture_output=True, timeout=max(0.001, min(budget, deadline-time.monotonic())))
        except subprocess.TimeoutExpired:
            pass
    while time.monotonic() < cutoff:
        read_units()
        observed.update(descendants(os.getpid()))
        returncode = process.poll()
        if returncode is not None:
            break
        time.sleep(min(0.01, max(0, cutoff-time.monotonic())))
    timed_out = returncode is None
    read_units()
    def unit_members():
        groups = {}
        for unit in sorted(units):
            try:
                group = subprocess.check_output(['systemctl', 'show', unit, '--property=ControlGroup', '--value'],
                    text=True, timeout=max(0.001, min(0.1, deadline-time.monotonic()))).strip()
                pids = set()
                if group:
                    for base in ('/sys/fs/cgroup', '/sys/fs/cgroup/systemd'):
                        root = Path(base) / group.lstrip('/')
                        for path in root.glob('**/cgroup.procs'):
                            try:
                                pids.update(int(pid) for pid in path.read_text().split())
                            except FileNotFoundError:
                                pass
                groups[unit] = sorted(pids)
            except (OSError, subprocess.SubprocessError):
                groups[unit] = ['unknown']
        return groups
    # 控制器返回也不意味着子孙或 systemd 实例已退出。先停止服务避免 Restart 再生。
    if units:
        try:
            subprocess.run(['systemctl', 'stop', '--no-block', *sorted(units)], capture_output=True,
                           timeout=max(0.001, min(0.1, deadline-time.monotonic())))
        except subprocess.TimeoutExpired:
            pass
    unit_action('SIGTERM', args.cleanup_seconds / 4)
    reserve = min(0.4, args.cleanup_seconds / 2)
    term_deadline = min(deadline - reserve, time.monotonic() + args.cleanup_seconds / 4)
    killed = False
    groups = unit_members()
    while time.monotonic() < deadline - reserve:
        owned = descendants(os.getpid())
        observed.update(owned)
        signal_owned(owned, signal.SIGTERM if time.monotonic() < term_deadline else signal.SIGKILL)
        if returncode is None:
            returncode = process.poll()
        reap(process.pid, owned)
        if time.monotonic() >= term_deadline and not killed:
            unit_action('SIGKILL', 0.05)
            killed = True
        groups = unit_members()
        if not descendants(os.getpid()) and all(not pids for pids in groups.values()):
            break
        time.sleep(0.005)
    remaining = descendants(os.getpid())
    elapsed = time.monotonic() - started
    recovered = not remaining and all(not pids for pids in groups.values())
    success = not timed_out and returncode == 0 and recovered and elapsed <= args.seconds
    destination.write_text(json.dumps({'command': args.command, 'budget_seconds': args.seconds,
        'elapsed_seconds': elapsed, 'phase_budgets': {'prepare': args.prepare_seconds,
        'warmup': args.warmup_seconds, 'measurement': measurement, 'cleanup': args.cleanup_seconds},
        'timeout': timed_out, 'returncode': returncode, 'observed_pids': sorted(observed),
        'remaining_pids': sorted(remaining), 'unit_members': groups,
        'resources_recovered': recovered, 'passed': success}, ensure_ascii=False, indent=2) + '\n')
    Path(registry.name).unlink()
    return 0 if success else 1


if __name__ == '__main__':
    raise SystemExit(main())

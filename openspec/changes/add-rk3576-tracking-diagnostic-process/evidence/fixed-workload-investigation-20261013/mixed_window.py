#!/usr/bin/env python3
"""十组成对验收窗口；每组由独立 systemd 控制组约束所有后代。"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(os.environ['DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT'])
EVIDENCE = ROOT / 'evidence'
PREFIX = os.environ.get('DRONE_DIAGNOSTIC_UNIT_PREFIX', 'drone-diag-ten-batch01')


def save(name, value):
    (EVIDENCE / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def identity():
    paths = [Path('/usr/lib/librknnrt.so')]
    paths += sorted(path for path in Path('/opt/drone_tracker/config').rglob('*') if path.is_file())
    paths += sorted(path for path in Path('/opt/drone_tracker/models').rglob('*') if path.is_file())
    return {'current': os.path.realpath('/opt/drone_tracker/current'),
            'files': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}}


def state(unit):
    text = subprocess.check_output(['systemctl', 'show', unit,
        '--property=ActiveState,SubState,MainPID,UnitFileState,ControlGroup,Result,ExecMainStatus'], text=True, timeout=3)
    return dict(line.split('=', 1) for line in text.splitlines() if '=' in line)


def members(group):
    if not group:
        return []
    roots = [Path('/sys/fs/cgroup') / group.lstrip('/'),
             Path('/sys/fs/cgroup/systemd') / group.lstrip('/')]
    pids = set()
    for root in roots:
        if root.exists():
            for path in [root / 'cgroup.procs'] + list(root.glob('**/cgroup.procs')):
                try:
                    pids.update(int(value) for value in path.read_text().split())
                except FileNotFoundError:
                    pass
    return sorted(pids)


def run_pair(number):
    name = 'pair' + str(number)
    unit = PREFIX + '-' + name + '.service'
    started = time.monotonic_ns()
    group = ''
    command = ['systemd-run', '--unit', unit, '--property=RuntimeMaxSec=140s',
        '--property=TimeoutStopSec=5s', '--property=KillMode=control-group',
        '--property=SendSIGKILL=yes', '--property=Type=exec',
        '--property=StandardOutput=append:' + str(EVIDENCE / (name + '-controller-direct.log')),
        '--property=StandardError=inherit',
        '--setenv=DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT=' + str(ROOT),
        '--setenv=DRONE_TRACKER_CPU_CONFIG=/opt/drone_tracker/config/cpu-affinity.yaml',
        '--setenv=ROS_DOMAIN_ID=198', '--setenv=ROS_LOCALHOST_ONLY=1',
        '--setenv=ROS_LOG_DIR=' + str(EVIDENCE / (name + '-controller-ros')),
        '/bin/bash', '-c', 'source /opt/ros/humble/setup.bash; source "$DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT/drone_tracker/share/drone_tracker/local_setup.bash"; exec taskset -c 0-3 python3 "$1" "$2"',
        'ten-round', str(ROOT / 'mixed_case.py'), name]
    output = subprocess.run(command, capture_output=True, text=True, timeout=5)
    (EVIDENCE / (name + '-launch.txt')).write_text(output.stdout + output.stderr)
    timed_out = False
    final = {}
    try:
        if output.returncode:
            raise RuntimeError('隔离单组启动失败')
        while True:
            final = state(unit)
            group = final.get('ControlGroup') or group
            elapsed = (time.monotonic_ns() - started) / 1e9
            if final.get('ActiveState') in ('inactive', 'failed'):
                break
            if elapsed >= 143:
                timed_out = True
                subprocess.run(['systemctl', 'kill', '--kill-who=all', '--signal=SIGKILL', unit],
                               capture_output=True, timeout=2)
                subprocess.run(['systemctl', 'stop', '--no-block', unit], capture_output=True, timeout=2)
                break
            time.sleep(0.2)
    finally:
        remaining = members(group)
        if remaining:
            subprocess.run(['systemctl', 'kill', '--kill-who=all', '--signal=SIGKILL', unit],
                           capture_output=True, timeout=2)
            time.sleep(0.1)
            remaining = members(group)
        elapsed = (time.monotonic_ns() - started) / 1e9
        result = {'unit': unit, 'command': command, 'start_ns': started,
            'finish_ns': time.monotonic_ns(), 'elapsed_seconds': elapsed,
            'timeout': timed_out or final.get('Result') == 'timeout' or elapsed > 150,
            'resources_recovered': not remaining, 'remaining_pids': remaining,
            'control_group': group, 'systemd_state': final, 'launch_returncode': output.returncode}
        save(name + '-window.json', result)
        journal = subprocess.run(['journalctl', '-u', unit, '--no-pager'], capture_output=True, text=True, timeout=3)
        (EVIDENCE / (name + '-controller.log')).write_text(journal.stdout + journal.stderr)
    return result


def main():
    if not (EVIDENCE / 'guard-ready.json').exists():
        raise RuntimeError('独立恢复守护未就绪')
    original = identity()
    save('protected-before.json', original)
    before = state('drone-tracker.service')
    save('formal-before.json', before)
    if before.get('ActiveState') != 'active' or before.get('SubState') != 'running' or before.get('UnitFileState') != 'enabled':
        raise RuntimeError('正式服务未处于原运行状态')
    started = time.monotonic_ns()
    armed_temporary = EVIDENCE / 'armed.tmp'
    armed_temporary.write_text(str(started))
    os.replace(armed_temporary, EVIDENCE / 'armed')
    save('maintenance-deadline.json', {'start_ns': started, 'cleanup_start_ns': started + 1720 * 10**9,
                                      'deadline_ns': started + 1800 * 10**9})
    results = []
    try:
        subprocess.run(['systemctl', 'stop', 'drone-tracker.service'], check=True, timeout=40)
        for number in [1, 2]:
            if (time.monotonic_ns() - started) / 1e9 > 1550:
                raise RuntimeError('维护剩余预算不足，拒绝开始下一组')
            results.append(run_pair(number))
            save('window-progress.json', results)
            if results[-1]["systemd_state"].get("ExecMainStatus") != "0" or results[-1]["timeout"] or not results[-1]["resources_recovered"]:
                raise RuntimeError("归因组失败，保留原件并恢复")
    finally:
        try:
            save('protected-after.json', identity())
            save('window-results.json', results)
        finally:
            (EVIDENCE / 'restore-requested').touch()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""独立恢复守护；单调 armed 起点起最多 1800 秒，清理异常也恢复正式服务。"""
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(os.environ['DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT'])
EVIDENCE = ROOT / 'evidence'
PREFIX = os.environ.get('DRONE_DIAGNOSTIC_UNIT_PREFIX', 'drone-diag-ten-batch01')


def main():
    (EVIDENCE / 'guard-ready.json').write_text(json.dumps({'pid': os.getpid(), 'ready_ns': time.monotonic_ns()}))
    while not (EVIDENCE / 'armed').exists():
        time.sleep(0.2)
    started = int((EVIDENCE / 'armed').read_text())
    deadline = started + 1800 * 10**9
    reason = 'deadline'
    while time.monotonic_ns() < started + 1720 * 10**9:
        if (EVIDENCE / 'restore-requested').exists():
            reason = 'requested'
            break
        time.sleep(0.2)
    units = [PREFIX + '-pair' + str(number) + '.service' for number in range(1, 11)]
    units.append(PREFIX + '-manager.service')
    cleanup, start_result, service = [], {}, ''
    def remaining(maximum):
        return max(0.01, min(maximum, (deadline - time.monotonic_ns()) / 1e9))
    try:
        for command in (['systemctl', 'stop', '--no-block'] + units,
                        ['systemctl', 'kill', '--kill-who=all', '--signal=SIGKILL'] + units):
            try:
                outcome = subprocess.run(command, capture_output=True, text=True, timeout=remaining(5))
                cleanup.append({'command': command, 'returncode': outcome.returncode,
                                'output': outcome.stdout + outcome.stderr})
            except Exception as error:
                cleanup.append({'error': repr(error)})
        time.sleep(remaining(1))
    finally:
        try:
            outcome = subprocess.run(['systemctl', 'start', 'drone-tracker.service'],
                                     capture_output=True, text=True, timeout=remaining(45))
            start_result = {'returncode': outcome.returncode, 'output': outcome.stdout + outcome.stderr}
        except Exception as error:
            start_result = {'error': repr(error)}
        try:
            service = subprocess.check_output(['systemctl', 'show', 'drone-tracker.service',
                '--property=ActiveState,SubState,MainPID,UnitFileState'], text=True, timeout=remaining(3))
        except Exception as error:
            service = repr(error)
        (EVIDENCE / 'restore.json').write_text(json.dumps({'reason': reason, 'start_ns': started,
            'restore_finished_ns': time.monotonic_ns(), 'elapsed_seconds': (time.monotonic_ns() - started) / 1e9,
            'cleanup': cleanup, 'start': start_result, 'formal_service': service}, ensure_ascii=False, indent=2))
    if start_result.get('returncode') != 0 or 'ActiveState=active' not in service or 'SubState=running' not in service or 'UnitFileState=enabled' not in service:
        raise RuntimeError('正式服务恢复失败')


if __name__ == '__main__':
    main()

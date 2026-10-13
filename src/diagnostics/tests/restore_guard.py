#!/usr/bin/env python3
"""板端独立恢复守护：不依赖 SSH，维护截止、完成或错误均恢复原服务。"""
import json
import os
from pathlib import Path
import signal
import subprocess
import time

ROOT = Path('/home/hzhy/drone-diagnostic-acceptance-20261012')
EVIDENCE = ROOT / 'evidence'


def main():
    EVIDENCE.mkdir(exist_ok=True)
    (EVIDENCE / 'guard-ready.json').write_text(json.dumps({'pid': os.getpid(), 'ready_ns': time.monotonic_ns()}))
    while not (EVIDENCE / 'armed').exists():
        time.sleep(0.2)
    started = int((EVIDENCE / 'armed').read_text())
    deadline = started + 1800 * 10**9
    reason = 'deadline'
    # 预留 80 秒；有限清理最多 24+3 秒，启动最多 45 秒，全部包含于 30 分钟。
    while (time.monotonic_ns() - started) < 1720 * 10**9:
        if (EVIDENCE / 'restore-requested').exists():
            reason = 'requested'
            break
        time.sleep(0.2)
    cleanup = []
    selected = []
    def remaining(maximum):
        return max(0.01, min(maximum, (deadline - time.monotonic_ns()) / 1e9))
    result_code, result_output, state = -1, '', ''
    try:
        for unit in ('drone-diag-isolated-main.service', 'drone-diag-isolated-worker.service'):
            try:
                outcome = subprocess.run(['systemctl', 'stop', unit], capture_output=True, text=True,
                                         timeout=remaining(12))
                cleanup.append({'unit': unit, 'returncode': outcome.returncode})
            except Exception as error:
                cleanup.append({'unit': unit, 'error': repr(error)})
        for directory in Path('/proc').glob('[0-9]*'):
            pid = int(directory.name)
            if pid == os.getpid():
                continue
            try:
                command = (directory / 'cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
                if str(ROOT) in command and 'restore_guard.py' not in command:
                    selected.append(pid)
                    os.kill(pid, signal.SIGTERM)
            except (FileNotFoundError, ProcessLookupError, PermissionError):
                pass
        time.sleep(remaining(3))
        for pid in selected:
            try:
                command = Path(f'/proc/{pid}/cmdline').read_bytes().decode(errors='replace')
                if str(ROOT) in command:
                    os.kill(pid, signal.SIGKILL)
            except (FileNotFoundError, ProcessLookupError):
                pass
    except Exception as error:
        cleanup.append({'error': repr(error)})
    finally:
        # 任一清理步骤失败仍必须尝试恢复，且不能越过总截止无限等待。
        try:
            result = subprocess.run(['systemctl', 'start', 'drone-tracker.service'], capture_output=True,
                                    text=True, timeout=remaining(45))
            result_code, result_output = result.returncode, result.stdout + result.stderr
        except Exception as error:
            result_output = repr(error)
        try:
            state = subprocess.check_output(['systemctl', 'show', 'drone-tracker.service',
                '--property=ActiveState,SubState,MainPID,UnitFileState'], text=True, timeout=remaining(3))
        except Exception as error:
            state = repr(error)
    (EVIDENCE / 'restore.json').write_text(json.dumps({'reason': reason, 'guard_started_ns': started,
        'restore_finished_ns': time.monotonic_ns(), 'elapsed_seconds': (time.monotonic_ns() - started) / 1e9,
        'cleanup_returncodes': cleanup, 'terminated_isolated_pids': selected, 'start_returncode': result_code,
        'start_output': result_output, 'formal_service': state}, ensure_ascii=False, indent=2))
    if result_code or 'ActiveState=active' not in state or 'UnitFileState=enabled' not in state:
        raise RuntimeError('正式服务恢复失败')


if __name__ == '__main__':
    main()

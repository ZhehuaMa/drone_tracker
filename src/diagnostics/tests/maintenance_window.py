#!/usr/bin/env python3
"""仅在明确批准后执行临时停服窗口；恢复由独立 restore_guard.py 保证。"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path('/home/hzhy/drone-diagnostic-acceptance-20261012')
EVIDENCE = ROOT / 'evidence'


def identity():
    paths = [Path('/opt/drone_tracker/config/camera.yaml'), Path('/opt/drone_tracker/config/cpu-affinity.yaml'),
             Path('/usr/lib/librknnrt.so')]
    paths += sorted(Path('/opt/drone_tracker/models').rglob('*.rknn'))
    return {'current': os.path.realpath('/opt/drone_tracker/current'),
            'files': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}}


def main():
    if not (EVIDENCE / 'guard-ready.json').exists():
        raise RuntimeError('独立恢复守护尚未就绪')
    original = identity()
    (EVIDENCE / 'protected-before.json').write_text(json.dumps(original, indent=2))
    before = subprocess.check_output(['systemctl', 'show', 'drone-tracker.service'], text=True)
    (EVIDENCE / 'formal-before.txt').write_text(before)
    if 'ActiveState=active' not in before or 'UnitFileState=enabled' not in before:
        raise RuntimeError('正式服务状态已变化，停止进入维护窗口')
    if not (EVIDENCE / 'armed').exists():
        armed_temporary = EVIDENCE / 'armed.tmp'
        armed_temporary.write_text(str(time.monotonic_ns()))
        os.replace(armed_temporary, EVIDENCE / 'armed')
    if (time.monotonic_ns() - int((EVIDENCE / 'armed').read_text())) / 1e9 > 1720:
        raise RuntimeError('原维护窗口已进入恢复预算，拒绝再次停服')
    results = []
    try:
        subprocess.run(['systemctl', 'stop', 'drone-tracker.service'], check=True, timeout=40)
        for kind in ('pair1', 'pair2', 'pair3', 'detail', 'lifecycle', 'business-kill', 'worker-kill', 'full', 'write', 'sync'):
            fault = kind in ('business-kill', 'worker-kill', 'full', 'write', 'sync')
            budget = 120 if fault else 150
            arguments = ['python3', str(ROOT / 'bounded_run.py'), '--seconds', str(budget),
                '--cleanup-seconds', '15', '--output', str(EVIDENCE / (kind + '-window.json'))]
            arguments += ['--fault'] if fault else ['--camera']
            arguments += ['python3', str(ROOT / 'board_acceptance.py'), kind]
            with (EVIDENCE / (kind + '-controller.log')).open('w') as log:
                outcome = subprocess.run(arguments, stdout=log, stderr=subprocess.STDOUT, timeout=budget + 1)
            results.append({'kind': kind, 'returncode': outcome.returncode})
            (EVIDENCE / 'window-progress.json').write_text(json.dumps(results, indent=2))
            if outcome.returncode:
                raise RuntimeError(kind + ' 执行失败，立即恢复正式服务')
        with (EVIDENCE / 'services-controller.log').open('w') as log:
            arguments = ['python3', str(ROOT / 'bounded_run.py'), '--fault', '--seconds', '120',
                '--cleanup-seconds', '15', '--output', str(EVIDENCE / 'services-window.json'),
                'python3', str(ROOT / 'service_acceptance.py')]
            outcome = subprocess.run(arguments, stdout=log, stderr=subprocess.STDOUT, timeout=121)
            results.append({'kind': 'services', 'returncode': outcome.returncode})
    finally:
        try:
            (EVIDENCE / 'protected-after.json').write_text(json.dumps(identity(), indent=2))
        finally:
            (EVIDENCE / 'restore-requested').touch()
            (EVIDENCE / 'window-results.json').write_text(json.dumps(results, indent=2))
    if identity() != original:
        raise RuntimeError('正式受保护文件指纹发生变化')


if __name__ == '__main__':
    main()

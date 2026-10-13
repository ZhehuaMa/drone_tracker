#!/usr/bin/env python3
"""验证控制器硬退出/无法finally收尾时，外部同时回收独立session与systemd控制组。"""
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=False)
    unit = 'drone-diag-deadline-warning-probe-' + str(os.getpid()) + '.service'
    controller = directory / 'controller.py'
    controller.write_text('''import os,signal,subprocess,sys,time
from pathlib import Path
unit=sys.argv[1]
registry=Path(os.environ['DRONE_DIAGNOSTIC_BOUNDED_UNITS'])
with registry.open('a') as output: output.write(unit+'\\n')
subprocess.run(['systemd-run','--unit',unit,'--property=KillMode=control-group','--property=TimeoutStopSec=0.2s','--property=RuntimeMaxSec=10s','/usr/bin/python3','-c','import os,signal,time;child=os.fork();os.setsid() if child==0 else None;signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(20)'],check=True)
subprocess.Popen([sys.executable,'-c','import os,signal,time;child=os.fork();os.setsid() if child==0 else None;signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(20)'],start_new_session=True)
signal.signal(signal.SIGTERM,signal.SIG_IGN)
time.sleep(20)
''')
    output = directory / 'window.json'
    completed = subprocess.run([sys.executable, str(Path(__file__).with_name('bounded_run.py')),
        '--seconds', '3', '--cleanup-seconds', '1', '--fault', '--output', str(output),
        sys.executable, str(controller), unit], capture_output=True, text=True, timeout=5)
    (directory / 'console.txt').write_text(completed.stdout + completed.stderr)
    value = json.loads(output.read_text())
    assert completed.returncode != 0 and value['timeout'] and value['resources_recovered']
    assert value['elapsed_seconds'] <= 3 and value['remaining_pids'] == [] and value['unit_members'][unit] == []
    assert len(value['observed_pids']) >= 3
    (directory / 'unit-state.txt').write_text(subprocess.check_output(
        ['systemctl', 'show', unit, '--property=ActiveState,SubState,ControlGroup,MainPID'], text=True))
    (directory / 'result.json').write_text(json.dumps({'passed': True, 'controller_finally_bypassed': True,
        'detached_sessions_recovered': True, 'systemd_descendants_recovered': True}, indent=2))


if __name__ == '__main__':
    main()

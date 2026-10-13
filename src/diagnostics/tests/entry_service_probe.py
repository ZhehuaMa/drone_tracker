#!/usr/bin/env python3
"""生产板隔离验证真实诊断入口；使用替身worker，不加载模型或停止正式服务。"""
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import yaml
from diagnostic_entry_fixture import prepare


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=5)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def main():
    directory = Path(sys.argv[1])
    deployment = Path(sys.argv[2])
    directory.mkdir(parents=True, exist_ok=False)
    installation = directory / 'payload'
    binary = installation / 'lib/drone_tracker/rk3576_diagnostic_worker'
    binary.parent.mkdir(parents=True)
    binary.write_text("#!/usr/bin/python3\nimport json,os,sys,time\nfrom pathlib import Path\nPath(os.environ['ENTRY_RESULT']).write_text(json.dumps({'argv':sys.argv,'affinity':sorted(os.sched_getaffinity(0)),'scheduler':os.sched_getscheduler(0)}))\ntime.sleep(float(os.environ.get('ENTRY_SLEEP','0')))\n")
    binary.chmod(0o755)
    direct = directory / 'direct'
    root, runtime, state = prepare(direct, deployment, installation, '/opt/drone_tracker/config/cpu-affinity.yaml')
    expected = yaml.safe_load((root / 'config/cpu-affinity.yaml').read_text())['allowed_cpus']
    output = direct / 'result.json'
    completed = subprocess.run([str(root / 'bin/run-diagnostics'), '--probe-token', 'entry-chain'],
        env=dict(os.environ, ENTRY_RESULT=str(output)), capture_output=True, text=True, timeout=5)
    (direct / 'console.txt').write_text(completed.stdout + completed.stderr)
    assert completed.returncode == 0 and runtime.is_dir() and state.is_dir()
    value = json.loads(output.read_text())
    assert value['affinity'] == expected and value['scheduler'] == os.SCHED_OTHER
    assert value['argv'][-2:] == ['--probe-token', 'entry-chain']
    hooks = direct / 'hooks'
    hooks.mkdir()
    (hooks / 'sitecustomize.py').write_text("import os\ndef fail(pid,cpus): raise PermissionError('affinity injection')\nos.sched_setaffinity=fail\n")
    failed = subprocess.run([str(root / 'bin/run-diagnostics')],
        env=dict(os.environ, ENTRY_RESULT=str(direct / 'must-not-exec.json'), PYTHONPATH=str(hooks)),
        capture_output=True, text=True, timeout=5)
    assert failed.returncode != 0 and not (direct / 'must-not-exec.json').exists()
    (direct / 'failure.txt').write_text(failed.stdout + failed.stderr)
    service = directory / 'service'
    root, runtime, state = prepare(service, deployment, installation, '/opt/drone_tracker/config/cpu-affinity.yaml',
        '/run/drone-diag-warning-entry', service / 'state')
    assert not runtime.exists(), '旧RuntimeDirectory不能用于本轮证据'
    unit = 'drone-diag-warning-entry.service'
    registry = os.environ.get('DRONE_DIAGNOSTIC_BOUNDED_UNITS')
    if registry:
        with open(registry, 'a') as file:
            file.write(unit + '\n')
    text = (deployment / 'drone-tracker-diagnostics.service').read_text()
    text = text.replace('/opt/drone_tracker/bin/run-diagnostics', str(root / 'bin/run-diagnostics'))
    text = text.replace('WorkingDirectory=/opt/drone_tracker', 'WorkingDirectory=' + str(root))
    text = text.replace('RuntimeDirectory=drone_tracker', 'RuntimeDirectory=drone-diag-warning-entry')
    text = text.replace('StateDirectory=drone_tracker/diagnostics', 'StateDirectory=drone-diag-warning-entry/diagnostics')
    text = text.replace('Restart=on-failure', 'Restart=no').replace('KillMode=mixed', 'KillMode=control-group')
    text = text.replace('[Service]', '[Service]\nEnvironment=ENTRY_RESULT=' + str(service / 'result.json') +
        '\nEnvironment=ENTRY_SLEEP=5\nExecStartPre=/usr/bin/test -d ' + str(runtime))
    target = Path('/run/systemd/system') / unit
    target.write_text(text)
    (service / unit).write_text(text)
    try:
        run(['systemctl', 'daemon-reload'])
        run(['systemctl', 'start', unit])
        cutoff = time.monotonic() + 3
        while not (service / 'result.json').exists() and time.monotonic() < cutoff:
            time.sleep(0.01)
        value = json.loads((service / 'result.json').read_text())
        assert value['affinity'] == expected and value['scheduler'] == os.SCHED_OTHER
        assert runtime.is_dir() and (runtime.stat().st_mode & 0o777) == 0o700
        (service / 'state.txt').write_text(run(['systemctl', 'show', unit, '--property=ActiveState,SubState,MainPID,RuntimeDirectory,RuntimeDirectoryMode']))
        assert value['argv'][0] == str(root / 'current/lib/drone_tracker/rk3576_diagnostic_worker')
    finally:
        run(['systemctl', 'stop', unit])
        (service / 'journal.txt').write_text(run(['journalctl', '-u', unit, '--no-pager']))
        target.unlink()
        run(['systemctl', 'daemon-reload'])
    assert not runtime.exists(), '服务停止后RuntimeDirectory未回收'
    (directory / 'result.json').write_text(json.dumps({'passed': True, 'direct_entry': True, 'service_entry': True,
        'affinity': expected, 'scheduler': 'SCHED_OTHER', 'affinity_failure_rejected': True,
        'runtime_directory_created_by_systemd': True, 'runtime_directory_removed': True}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

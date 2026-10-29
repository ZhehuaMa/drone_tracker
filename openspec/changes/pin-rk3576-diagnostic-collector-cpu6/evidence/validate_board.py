#!/usr/bin/env python3
"""Check isolated installed entries with a real worker; never change formal services."""
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from diagnostic_entry_fixture import prepare


def command(*args):
    result = subprocess.run(args, capture_output=True, text=True, timeout=8)
    if result.returncode:
        raise RuntimeError(f'{args}: {result.stdout}{result.stderr}')
    return result.stdout


def save(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def threads(pid):
    base = Path(f'/proc/{pid}')
    return {'pid': pid, 'start_ticks': base.joinpath('stat').read_text().split(') ')[1].split()[19],
            'executable': os.readlink(base / 'exe'), 'threads': {
                task.name: {'affinity': sorted(os.sched_getaffinity(int(task.name))),
                            'scheduler': os.sched_getscheduler(int(task.name)),
                            'status': (task / 'status').read_text()}
                for task in sorted(base.joinpath('task').iterdir())}}


def business():
    found = {}
    for proc in Path('/proc').iterdir():
        if not proc.name.isdecimal():
            continue
        try:
            executable = Path(os.readlink(proc / 'exe')).name
            if any(name in executable for name in ('drone_tracker_node', 'interceptor', 'bridge')):
                found[proc.name] = threads(int(proc.name))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            pass
    return found


def formal_state():
    protected = [Path('/usr/lib/librknnrt.so')]
    for directory in ('/opt/drone_tracker/config', '/opt/drone_tracker/bin', '/opt/drone_tracker/models'):
        protected.extend(path for path in Path(directory).rglob('*') if path.is_file())
    return {'current': os.readlink('/opt/drone_tracker/current'),
            'files': {str(path): digest(path) for path in protected},
            'services': command('systemctl', 'show', 'drone-tracker.service',
                'drone-tracker-diagnostics.service', '-p', 'Id', '-p', 'LoadState',
                '-p', 'ActiveState', '-p', 'SubState', '-p', 'UnitFileState', '-p', 'MainPID')}


def unchanged(before, after):
    assert before['start_ticks'] == after['start_ticks']
    for tid, value in before['threads'].items():
        assert tid in after['threads'], f'Existing thread disappeared: {tid}'
        assert value['affinity'] == after['threads'][tid]['affinity'], tid
        assert value['scheduler'] == after['threads'][tid]['scheduler'], tid


def main():
    started = time.monotonic()
    output, deployment, installation = map(Path, sys.argv[1:4])
    output.mkdir(parents=True, exist_ok=False)
    unit = 'drone-diag-cpu6-validation.service'
    unit_path = Path('/run/systemd/system') / unit
    assert not unit_path.exists(), 'Refuse an existing test service'
    assert command('systemctl', 'show', unit, '-p', 'LoadState', '--value').strip() == 'not-found'
    source_binary = installation / 'lib/drone_tracker/rk3576_diagnostic_worker'
    before = formal_state()
    save(output / 'formal-before.json', before)
    save(output / 'board.json', {'model': Path('/proc/device-tree/model').read_text().strip('\0'),
        'platform': platform.platform(), 'machine_id': Path('/etc/machine-id').read_text().strip(),
        'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
        'worker': str(source_binary), 'worker_sha256': digest(source_binary),
        'worker_file': command('file', str(source_binary)), 'models_loaded': False, 'input': 'no client sessions'})
    assert Path('/proc/device-tree/model').read_text().strip('\0') == 'HZ-RK3576_WuKong Board'
    direct = None
    bystander = None
    installed = False
    worker_pids = []
    result = {'passed': False, 'formal_services_stopped': []}
    try:
        ready = output / 'bystander-ready'
        bystander = subprocess.Popen([sys.executable, '-c',
            'import pathlib,sys,threading,time\n'
            'threading.Thread(target=time.sleep,args=(120,),daemon=True).start()\n'
            'pathlib.Path(sys.argv[1]).touch()\ntime.sleep(120)\n', str(ready)])
        deadline = time.monotonic() + 3
        while not ready.exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        assert ready.exists()
        observers_before = {'caller': threads(os.getpid()), 'bystander': threads(bystander.pid),
                            'business': business()}
        assert len(observers_before['bystander']['threads']) >= 2
        save(output / 'observers-before.json', observers_before)

        def check_observers(name):
            after = {'caller': threads(os.getpid()), 'bystander': threads(bystander.pid), 'business': business()}
            unchanged(observers_before['caller'], after['caller'])
            unchanged(observers_before['bystander'], after['bystander'])
            for pid, snapshot in observers_before['business'].items():
                assert pid in after['business'], f'Business process disappeared: {pid}'
                unchanged(snapshot, after['business'][pid])
            save(output / f'observers-{name}.json', after)

        def worker_snapshot(pid, name):
            snapshot = threads(pid)
            assert Path(snapshot['executable']).resolve() == source_binary.resolve()
            assert snapshot['threads']
            for value in snapshot['threads'].values():
                assert value['affinity'] == [6] and value['scheduler'] == os.SCHED_OTHER
                assert 'Cpus_allowed_list:\t6\n' in value['status']
            save(output / f'worker-{name}.json', snapshot)
            worker_pids.append(pid)
            check_observers(name)

        assert not Path('/run/drone-diag-cpu6-direct').exists(), 'Refuse an existing direct runtime'
        root, runtime, state = prepare(output / 'direct', deployment, installation,
            '/opt/drone_tracker/config/cpu-affinity.yaml', '/run/drone-diag-cpu6-direct')
        with (output / 'direct-console.txt').open('w') as console:
            direct = subprocess.Popen([str(root / 'bin/run-diagnostics')], stdout=console, stderr=console)
            deadline = time.monotonic() + 5
            while not (runtime / 'diagnostics.sock').exists() and time.monotonic() < deadline:
                assert direct.poll() is None
                time.sleep(0.01)
            assert (runtime / 'diagnostics.sock').exists()
            worker_snapshot(direct.pid, 'direct')
            direct.terminate()
            assert direct.wait(timeout=3) == 0
            check_observers('direct-stopped')

        root, runtime, state = prepare(output / 'service', deployment, installation,
            '/opt/drone_tracker/config/cpu-affinity.yaml', '/run/drone-diag-cpu6-validation',
            output / 'service-state')
        text = (deployment / 'drone-tracker-diagnostics.service').read_text()
        replacements = {'/opt/drone_tracker/bin/run-diagnostics': str(root / 'bin/run-diagnostics'),
            'WorkingDirectory=/opt/drone_tracker': 'WorkingDirectory=' + str(root),
            'RuntimeDirectory=drone_tracker': 'RuntimeDirectory=drone-diag-cpu6-validation',
            'StateDirectory=drone_tracker/diagnostics': 'StateDirectory=drone-diag-cpu6-validation/diagnostics'}
        for old, new in replacements.items():
            text = text.replace(old, new)
        save(output / 'service-path-replacements.json', replacements)
        (output / unit).write_text(text)
        registry = os.environ.get('DRONE_DIAGNOSTIC_BOUNDED_UNITS')
        assert registry, 'The external deadline must own the test service'
        with open(registry, 'a') as stream:
            stream.write(unit + '\n')
        unit_path.write_text(text)
        installed = True
        command('systemctl', 'daemon-reload')
        for name, action in (('service', 'start'), ('restart', 'restart')):
            command('systemctl', action, unit)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                pid = int(command('systemctl', 'show', unit, '-p', 'MainPID', '--value').strip())
                if pid and (runtime / 'diagnostics.sock').exists():
                    break
                time.sleep(0.02)
            assert pid and command('systemctl', 'is-active', unit).strip() == 'active'
            worker_snapshot(pid, name)
        assert worker_pids[-1] != worker_pids[-2], 'Restart must create a new process'
        result['passed'] = True
    except BaseException as error:
        result['error'] = repr(error)
        raise
    finally:
        if direct is not None and direct.poll() is None:
            direct.terminate()
            direct.wait(timeout=3)
        if installed:
            command('systemctl', 'stop', unit)
            (output / 'service-journal.txt').write_text(command('journalctl', '-u', unit, '--no-pager'))
            result['service_after_stop'] = command('systemctl', 'show', unit, '-p', 'MainPID', '-p', 'ActiveState')
            assert 'MainPID=0' in result['service_after_stop']
            unit_path.unlink()
            command('systemctl', 'daemon-reload')
        if bystander is not None:
            bystander.terminate()
            bystander.wait(timeout=3)
        result['remaining_worker_pids'] = [pid for pid in worker_pids if Path(f'/proc/{pid}').exists()]
        assert not result['remaining_worker_pids']
        after = formal_state()
        save(output / 'formal-after.json', after)
        result['formal_state_unchanged'] = before == after
        direct_runtime = Path('/run/drone-diag-cpu6-direct')
        if direct_runtime.exists():
            (direct_runtime / 'diagnostics.sock.lock').unlink(missing_ok=True)
            direct_runtime.rmdir()
        result['runtime_removed'] = not Path('/run/drone-diag-cpu6-validation').exists() and not direct_runtime.exists()
        for path in (Path('/var/lib/drone-diag-cpu6-validation/diagnostics'),
                     Path('/var/lib/drone-diag-cpu6-validation')):
            if path.exists():
                path.rmdir()
        result['elapsed_seconds'] = time.monotonic() - started
        save(output / 'result.json', result)
        assert result['formal_state_unchanged'] and result['runtime_removed']
        assert result['elapsed_seconds'] <= 180


if __name__ == '__main__':
    main()

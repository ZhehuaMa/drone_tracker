#!/usr/bin/env python3
"""用真实 systemd 隔离实例验证独立生命周期；回退脚本仅重定向根和 unit 名。"""
import json
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import tarfile
import time

from diagnostic_entry_fixture import prepare

from board_acceptance import Case, ROOT, BIN, CPUS, inspect, save, resource

MAIN = 'drone-diag-isolated-main.service'
WORKER = 'drone-diag-isolated-worker.service'


def ctl(*arguments, check=True):
    return subprocess.run(['systemctl', *arguments], check=check, capture_output=True, text=True, timeout=12)


def pid(unit):
    return int(ctl('show', unit, '--property=MainPID', '--value').stdout.strip())


def active(unit):
    return ctl('is-active', '--quiet', unit, check=False).returncode == 0


def main():
    case = Case(ROOT / 'evidence' / os.environ.get('DRONE_DIAGNOSTIC_SERVICE_CASE', 'services'))
    installed = []
    registry = os.environ.get('DRONE_DIAGNOSTIC_BOUNDED_UNITS')
    if registry:
        with open(registry, 'a') as output:
            output.write(MAIN + '\n' + WORKER + '\n')
    try:
        template = case.business('configuration', 'basic', hdmi=False)
        case.stop(template)
        config = case.directory / 'configuration.yaml'
        entry_root, runtime, state = prepare(case.directory / 'entry', ROOT / 'deployment', ROOT / 'drone_tracker',
            '/opt/drone_tracker/config/cpu-affinity.yaml', '/run/drone-diag-services',
            case.directory / 'diagnostics')
        document = __import__('yaml').safe_load(config.read_text())
        document['drone_tracker_node']['ros__parameters']['diagnostics']['socket'] = str(runtime / 'diagnostics.sock')
        config.write_text(__import__('yaml').safe_dump(document, sort_keys=False))
        env = ('Environment=DRONE_TRACKER_CPU_CONFIG=/opt/drone_tracker/config/cpu-affinity.yaml\nEnvironment=ROS_DOMAIN_ID=198\nEnvironment=ROS_LOCALHOST_ONLY=1\n'
               f'Environment=ROS_LOG_DIR={case.directory}/service-ros\n'
               f'Environment=AMENT_PREFIX_PATH={ROOT}/drone_tracker:/opt/ros/humble\n'
               f'Environment=LD_LIBRARY_PATH={ROOT}/drone_tracker/lib:/opt/ros/humble/lib\n')
        for source, unit, command in [
            ('drone-tracker-diagnostics.service', WORKER, [str(entry_root / 'bin/run-diagnostics')]),
            ('drone-tracker.service', MAIN, [str(BIN / 'drone_tracker_node'), '--ros-args', '--params-file', str(config)])]:
            text = (ROOT / 'deployment' / source).read_text()
            text = text.replace('drone-tracker-diagnostics.service', WORKER)
            lines = []
            for line in text.splitlines():
                if line.startswith('ExecStart='):
                    line = 'ExecStart=' + ('/usr/bin/taskset -c ' + CPUS + ' ' if unit == MAIN else '') + ' '.join(shlex.quote(x) for x in command)
                elif line.startswith('WorkingDirectory='):
                    line = 'WorkingDirectory=' + str(ROOT)
                elif line.startswith('RuntimeDirectory='):
                    line = 'RuntimeDirectory=drone-diag-services'
                elif line.startswith(('Environment=', 'LogsDirectory=', 'StateDirectory=')):
                    continue
                elif line.startswith('TimeoutStopSec='):
                    line = 'TimeoutStopSec=8'
                lines.append(line)
            text = '\n'.join(lines).replace('[Service]', '[Service]\n' + env) + '\n'
            target = Path('/run/systemd/system') / unit
            target.write_text(text)
            (case.directory / unit).write_text(text)
            installed.append(unit)
        ctl('daemon-reload')
        ctl('start', MAIN)
        case.spin(2)
        first_main, first_worker = pid(MAIN), pid(WORKER)
        assert first_main and first_worker and active(MAIN) and active(WORKER)
        case.result['service_initial_resources'] = {'main': resource(first_main), 'worker': resource(first_worker)}
        assert os.sched_getaffinity(first_worker) == {6}
        assert os.sched_getscheduler(first_worker) == os.SCHED_OTHER
        case.command('ON')
        case.spin(3)
        # 日志被强制终止后，健康业务 PID 保持不变；日志仅由自身 Restart 恢复。
        ctl('kill', '--signal=SIGKILL', WORKER)
        case.spin(5)
        assert active(MAIN) and pid(MAIN) == first_main and active(WORKER) and pid(WORKER) != first_worker
        replacement = pid(WORKER)
        assert os.sched_getaffinity(replacement) == {6}
        assert os.sched_getscheduler(replacement) == os.SCHED_OTHER
        case.spin(6)
        # 第二消费者必须拒绝，不得夺取活跃端点。
        duplicate = subprocess.run([str(BIN / 'rk3576_diagnostic_worker'), '--socket', str(runtime / 'diagnostics.sock'),
            '--directory', str(case.directory / 'duplicate')], capture_output=True, text=True, timeout=2)
        assert duplicate.returncode != 0
        case.result['duplicate'] = {'returncode': duplicate.returncode, 'message': duplicate.stderr}
        # 主服务重启和异常退出均不能联动终止日志。
        ctl('restart', MAIN)
        case.spin(2)
        assert active(WORKER) and pid(WORKER) == replacement and pid(MAIN) != first_main
        before_kill = pid(MAIN)
        ctl('kill', '--signal=SIGKILL', MAIN)
        case.spin(5)
        assert active(WORKER) and pid(WORKER) == replacement and active(MAIN) and pid(MAIN) != before_kill
        # 显式日志重启保持主服务实例不变。
        stable_main = pid(MAIN)
        ctl('restart', WORKER)
        case.spin(6)
        assert active(MAIN) and pid(MAIN) == stable_main and active(WORKER)
        assert os.sched_getaffinity(pid(WORKER)) == {6}
        assert os.sched_getscheduler(pid(WORKER)) == os.SCHED_OTHER
        case.result['lifecycle'] = {'first_main': first_main, 'first_worker': first_worker,
            'replacement_worker': replacement, 'stable_main': stable_main, 'final_worker': pid(WORKER)}
        ctl('stop', MAIN)
        assert active(WORKER)

        # 缺程序预检使用真实 deploy.sh --check；只在新建缺件副本中执行。
        package = case.directory / 'missing-package'
        package.mkdir()
        shutil.copytree(ROOT / 'drone_tracker', package / 'payload')
        for name in ('cpu-policy.py', 'diagnostic-launch.py', 'drone-tracker.service',
                     'drone-tracker-diagnostics.service', 'deploy.sh'):
            shutil.copy2(ROOT / 'deployment' / name, package / name)
        (package / 'config').mkdir()
        shutil.copy2('/opt/drone_tracker/config/cpu-affinity.yaml', package / 'config/cpu-affinity.yaml')
        shutil.copy2(ROOT / 'deployment/diagnostics.yaml', package / 'config/diagnostics.yaml')
        (package / 'DEVICES').write_text('')
        (package / 'SHA256SUMS').write_text(hashlib.sha256((package / 'deploy.sh').read_bytes()).hexdigest()
                                          + '  deploy.sh\n')
        (package / 'models').mkdir()
        (package / 'models/sentinel').write_text('isolated precheck\n')
        (package / 'MODELS.sha256').write_text(hashlib.sha256((package / 'models/sentinel').read_bytes()).hexdigest()
                                             + '  models/sentinel\n')
        (package / 'payload/lib/drone_tracker/rk3576_diagnostic_worker').unlink()
        state_before = ctl('show', MAIN, WORKER).stdout
        precheck = subprocess.run(['bash', str(package / 'deploy.sh'), '--check'], capture_output=True, text=True, timeout=12)
        state_after = ctl('show', MAIN, WORKER).stdout
        assert precheck.returncode != 0 and '程序不可执行: rk3576_diagnostic_worker' in precheck.stderr + precheck.stdout
        assert pid(WORKER) and active(WORKER) and not active(MAIN)
        case.result['missing_precheck'] = {'returncode': precheck.returncode,
            'output': precheck.stdout + precheck.stderr, 'state_before': state_before, 'state_after': state_after}

        # 旧回退在私有安装根执行。改动仅为固定路径和 unit 名重定向，保存变体供审核。
        isolated = case.directory / 'installation'
        isolated.mkdir()
        (isolated / 'config').mkdir()
        (isolated / 'bin').mkdir()
        old_release = os.path.realpath('/opt/drone_tracker/current')
        (isolated / 'current').symlink_to(old_release)
        old_config = json.loads(json.dumps(__import__('yaml').safe_load(config.read_text())))
        old_config['drone_tracker_node']['ros__parameters'].pop('diagnostics', None)
        (isolated / 'config/camera.yaml').write_text(__import__('yaml').safe_dump(old_config, sort_keys=False))
        old_entry = isolated / 'bin/run-old'
        old_entry.write_text('#!/bin/bash\nsource /opt/ros/humble/setup.bash\nexec ' + old_release +
                            '/lib/drone_tracker/drone_tracker_node --ros-args --params-file ' + str(isolated / 'config/camera.yaml') + '\n')
        old_entry.chmod(0o755)
        backup = isolated / 'backups/old'
        backup.mkdir(parents=True)
        with tarfile.open(backup / 'installation.tar', 'w') as archive:
            for name in ('config', 'bin', 'current'):
                archive.add(isolated / name, arcname=name)
        old_unit = (case.directory / MAIN).read_text()
        old_unit = '\n'.join(line for line in old_unit.splitlines() if not line.startswith(('Wants=', 'After=')))
        old_unit = '\n'.join(('ExecStart=' + str(old_entry)) if line.startswith('ExecStart=') else line
                             for line in old_unit.splitlines()) + '\n'
        (backup / MAIN).write_text(old_unit)
        (backup / 'service-enabled.txt').write_text('disabled\n')
        (backup / 'service-active.txt').write_text('1\n')
        rollback = (ROOT / 'deployment/rollback.sh').read_text().replace('/opt/drone_tracker', str(isolated))
        rollback = rollback.replace('drone-tracker-diagnostics.service', WORKER).replace('drone-tracker.service', MAIN)
        rollback = rollback.replace('/etc/systemd/system', '/run/systemd/system').replace(
            '/run/lock/drone-tracker-deploy.lock', '/run/lock/drone-diag-isolated-deploy.lock')
        script = case.directory / 'rollback-isolated.sh'
        script.write_text(rollback)
        raw = {str(p): p.read_bytes() for p in (case.directory / 'diagnostics').rglob('*.dtd')}
        outcome = subprocess.run(['bash', str(script), str(backup)], capture_output=True, text=True, timeout=20)
        assert outcome.returncode == 0 and active(MAIN) and not active(WORKER)
        assert os.path.realpath(isolated / 'current') == old_release
        assert all(Path(path).read_bytes() == data for path, data in raw.items())
        case.result['rollback'] = {'returncode': outcome.returncode, 'output': outcome.stdout + outcome.stderr,
            'old_release': old_release, 'main_pid': pid(MAIN), 'raw_files_preserved': len(raw)}
        case.result['execution_passed'] = True
    except Exception as error:
        case.result['error'] = repr(error)
        raise
    finally:
        for unit in installed:
            journal = subprocess.run(['journalctl', '-u', unit, '--no-pager'], capture_output=True, text=True)
            (case.directory / (unit + '.journal')).write_text(journal.stdout + journal.stderr)
            ctl('stop', unit, check=False)
            ctl('disable', unit, check=False)
            path = Path('/run/systemd/system') / unit
            if path.exists():
                path.unlink()
        ctl('daemon-reload')
        case.close()
        inspect(case.directory)


if __name__ == '__main__':
    main()

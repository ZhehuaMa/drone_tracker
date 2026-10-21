"""验证诊断包边界、服务关系和有时限控制；不操作实机或 systemd 实例。"""
import importlib.machinery
import json
import os
import shutil
from unittest import mock
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DEPLOYMENT = ROOT / 'deployment'
LAUNCH = importlib.machinery.SourceFileLoader('diagnostic_launch_test', str(DEPLOYMENT / 'diagnostic-launch.py')).load_module()
sys.path.insert(0, str(DEPLOYMENT))
import package_core
sys.path.insert(0, str(ROOT / 'src/diagnostics/tests'))
from diagnostic_entry_fixture import prepare


class DiagnosticsTest(unittest.TestCase):
    def test_configuration_and_package_programs(self):
        value = LAUNCH.parse(DEPLOYMENT / 'diagnostics.yaml')
        self.assertEqual(value['sync_ms'], 100)
        self.assertIn('rk3576_diagnostic_worker', package_core.PROGRAMS)
        self.assertIn('rk3576_diagnostic_inspect', package_core.PROGRAMS)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'config.yaml'
            for bad in ('version: 2', 'version: 1\nprotocol_version: 1'):
                path.write_text(bad)
                with self.assertRaises(ValueError):
                    LAUNCH.parse(path)

    def test_independent_services_and_side_effect_boundary(self):
        collector = (DEPLOYMENT / 'drone-tracker-diagnostics.service').read_text()
        main = (DEPLOYMENT / 'drone-tracker.service').read_text()
        for forbidden in ('PartOf=', 'BindsTo=', 'Requires=drone-tracker.service'):
            self.assertNotIn(forbidden, collector)
        self.assertIn('drone-tracker-diagnostics.service', main)
        deploy = (DEPLOYMENT / 'deploy.sh').read_text()
        boundary = deploy.index('[[ $mode != check ]] || exit 0')
        for mutation in ('systemctl stop', 'systemctl start', 'systemctl restart', 'systemctl enable', 'systemctl daemon-reload'):
            self.assertNotIn(mutation, deploy[:boundary])
        for required in ('rk3576_diagnostic_worker', 'rk3576_diagnostic_inspect', 'config/diagnostics.yaml', '--protocol-version'):
            self.assertIn(required, deploy[:boundary])
        self.assertIn('diagnostic-active.txt', deploy)
        rollback = (DEPLOYMENT / 'rollback.sh').read_text()
        self.assertIn('drone-tracker-diagnostics.service', rollback)
        self.assertNotIn('rm -rf', rollback)
        self.assertNotIn('rm /var/lib/drone_tracker/diagnostics', rollback)

    @unittest.skipUnless(sys.platform == 'linux', '实际亲和性与子收割者检查要求Linux')
    def test_real_installed_entry_chain(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            installation = directory / 'payload'
            binary = installation / 'lib/drone_tracker/rk3576_diagnostic_worker'
            binary.parent.mkdir(parents=True)
            binary.write_text("#!/usr/bin/env python3\nimport json,os,sys\nfrom pathlib import Path\nPath(os.environ['ENTRY_RESULT']).write_text(json.dumps({'argv':sys.argv,'affinity':sorted(os.sched_getaffinity(0)),'scheduler':os.sched_getscheduler(0)}))\n")
            binary.chmod(0o755)
            cpus = sorted(os.sched_getaffinity(0))[:2]
            policy = directory / 'cpu.yaml'
            policy.write_text('version: 1\nmode: inherit\nallowed_cpus: ' + str(cpus) + '\n')
            root, runtime, state = prepare(directory, DEPLOYMENT, installation, policy)
            output = directory / 'result.json'
            env = dict(os.environ, ENTRY_RESULT=str(output))
            result = subprocess.run([str(root / 'bin/run-diagnostics'), '--probe-token', 'exact-value'],
                                    env=env, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            value = json.loads(output.read_text())
            self.assertEqual(value['affinity'], cpus)
            self.assertEqual(value['scheduler'], os.SCHED_OTHER)
            self.assertEqual(value['argv'], [str(root / 'current/lib/drone_tracker/rk3576_diagnostic_worker'),
                '--socket', str(runtime / 'diagnostics.sock'), '--directory', str(state),
                '--sync-ms', '100', '--segment-bytes', '67108864', '--total-bytes', '1073741824',
                '--minimum-free-bytes', '5368709120', '--probe-token', 'exact-value'])
            self.assertIn('读回=', result.stdout)
            self.assertTrue(runtime.is_dir() and state.is_dir())
            output.unlink()
            hooks = directory / 'hooks'
            hooks.mkdir()
            (hooks / 'sitecustomize.py').write_text("import os\ndef reject(pid,cpus): raise PermissionError('affinity injection')\nos.sched_setaffinity=reject\n")
            failed = subprocess.run([str(root / 'bin/run-diagnostics')],
                env=dict(env, PYTHONPATH=str(hooks)), capture_output=True, text=True, timeout=5)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn('诊断不可用', failed.stderr)
            self.assertFalse(output.exists(), '亲和性失败仍exec')

    @unittest.skipUnless(sys.platform == 'linux', '实际外部回收要求Linux')
    def test_detached_descendants_and_controller_kill(self):
        for exits in (False, True):
            with tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                script = directory / 'controller.py'
                script.write_text("import os,signal,subprocess,sys,time\nchild=subprocess.Popen([sys.executable,'-c',\"import os,signal,time;os.setsid();signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(20)\"],start_new_session=True)\n" +
                    ("time.sleep(0.1)\nos._exit(0)\n" if exits else "signal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(20)\n"))
                # 子进程已经由Popen建立session；第二次setsid在其fork孙代中执行。
                script.write_text(script.read_text().replace('os.setsid();signal',
                    'child=os.fork();os.setsid() if child==0 else None;signal'))
                output = directory / 'window.json'
                result = subprocess.run([sys.executable, str(ROOT / 'src/diagnostics/tests/bounded_run.py'),
                    '--seconds', '3', '--cleanup-seconds', '1', '--output', str(output), sys.executable, str(script)],
                    capture_output=True, text=True, timeout=6)
                self.assertTrue(output.exists(), result.stderr)
                value = json.loads(output.read_text())
                self.assertTrue(value['resources_recovered'], value)
                self.assertGreaterEqual(len(value['observed_pids']), 2)
                self.assertEqual(value['remaining_pids'], [])
                self.assertLessEqual(value['elapsed_seconds'], 3)
                if not exits:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertTrue(value['timeout'])

    @unittest.skipUnless(sys.platform == 'linux', '实际外部回收要求Linux')
    def test_failed_recovery_is_not_parent_exit(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'unrecovered.json'
            runner = Path(temporary) / 'inject.py'
            runner.write_text("import sys,time,os,signal\nfrom pathlib import Path\nsys.path.insert(0," + repr(str(ROOT / 'src/diagnostics/tests')) + ")\nimport bounded_run as b\nb.signal_owned=lambda owned,sig:None\nsys.argv=['bounded','--seconds','0.5','--cleanup-seconds','0.2','--output'," + repr(str(output)) + ",sys.executable,'-c','import time;time.sleep(20)']\nstatus=b.main()\nfor pid in b.descendants(os.getpid()):\n try: os.kill(pid,signal.SIGKILL)\n except ProcessLookupError: pass\ntime.sleep(0.05)\nb.reap(-1)\nsys.exit(status)\n")
            result = subprocess.run([sys.executable, str(runner)], timeout=3, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            value = json.loads(output.read_text())
            self.assertFalse(value['resources_recovered'])
            self.assertFalse(value['passed'])
            self.assertTrue(value['remaining_pids'])

    @unittest.skipUnless(sys.platform == 'linux', '实际外部回收要求Linux')
    def test_deadline_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'timeout.json'
            arguments = [sys.executable, str(ROOT / 'src/diagnostics/tests/bounded_run.py'),
                         '--seconds', '3', '--cleanup-seconds', '1', '--output', str(output),
                         sys.executable, '-c', 'import time; time.sleep(5)']
            result = subprocess.run(arguments, timeout=3, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            record = json.loads(output.read_text())
            self.assertTrue(record['timeout'])
            self.assertTrue(record['resources_recovered'])
            self.assertLessEqual(record['elapsed_seconds'], 3)
            before = output.read_bytes()
            subprocess.run(arguments, timeout=3, capture_output=True)
            self.assertEqual(output.read_bytes(), before)
            arguments[arguments.index('3')] = '181'
            arguments.insert(2, '--camera')
            result = subprocess.run(arguments, timeout=3, capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()

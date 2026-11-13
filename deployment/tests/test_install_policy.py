"""Exercise production install/rollback logic in isolated paths with hardware/service stubs."""
import hashlib
import importlib.machinery
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import yaml

DEPLOYMENT = Path(__file__).resolve().parents[1]
POLICY = importlib.machinery.SourceFileLoader('cpu_policy_install_test', str(DEPLOYMENT / 'cpu-policy.py')).load_module()


class PolicyInstallTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.bundle = self.base / 'bundle'
        self.bundle.mkdir()
        self.target = self.base / 'installed'
        self.services = self.base / 'services'
        self.services.mkdir()
        self.systemctl_log = self.base / 'systemctl.log'
        self.service_state = self.base / 'service-state.json'
        self.service_state.write_text('{}')
        self.bin = self.base / 'bin'
        self.bin.mkdir()
        self.env = dict(os.environ, PATH=f'{self.bin}:{os.environ["PATH"]}',
                        SERVICE_STATE=str(self.service_state), SYSTEMCTL_LOG=str(self.systemctl_log))
        (self.base / 'model').write_text('HZ-RK3576_WuKong Board')
        (self.base / 'systemd').mkdir()
        (self.base / 'lock').mkdir()
        setup = self.base / 'ros/setup.bash'
        setup.parent.mkdir()
        setup.write_text('')
        self.replacements = {'/opt/drone_tracker': str(self.target), '/opt/ros/humble': str(setup.parent),
            '/proc/device-tree/model': str(self.base / 'model'), '/run/systemd/system': str(self.base / 'systemd'),
            '/run/lock': str(self.base / 'lock'), '/etc/systemd/system': str(self.services),
            '/var/lib/drone_tracker': str(self.base / 'state'), '/usr/lib/librknnrt.so': str(self.base / 'runtime.so'),
            '$EUID == 0': '0 == 0'}
        for name in ('deploy.sh', 'rollback.sh', 'run-camera', 'run-diagnostics', 'prepare-display.py',
                     'cpu-policy.py', 'diagnostic-launch.py', 'drone-tracker.service', 'drone-tracker-diagnostics.service'):
            text = (DEPLOYMENT / name).read_text()
            for before, after in self.replacements.items():
                text = text.replace(before, after)
            (self.bundle / name).write_text(text)
        self.command('python3', '#!/bin/sh\nexec ' + sys.executable + ' \"$@\"\n')
        self.command('uname', '#!/bin/sh\necho aarch64\n')
        self.command('ldd', '#!/bin/sh\nexit 0\n')
        self.command('readelf', '#!/bin/sh\necho "Machine: AArch64"\n')
        self.command('systemd-analyze', '#!/bin/sh\nexit 0\n')
        self.command('taskset', '#!/bin/sh\n[ "$2" != "$FAIL_CPU" ]\n')
        self.command('sha256sum', '#!/bin/sh\nif [ "$1" = "' + str(self.base / 'runtime.so') + '" ]; then\n'
                     'echo "d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8  $1"\n'
                     'else exec /usr/bin/sha256sum "$@"; fi\n')
        self.command('systemctl', '#!' + sys.executable + '\nimport json,os,sys\nfrom pathlib import Path\n'
            'p=Path(os.environ["SERVICE_STATE"]); d=json.loads(p.read_text()); a=sys.argv[1]; n=sys.argv[-1]\n'
            'with Path(os.environ["SYSTEMCTL_LOG"]).open("a") as f:f.write(" ".join(sys.argv[1:])+"\\n")\n'
            'if a=="is-active":sys.exit(0 if d.get(n,{}).get("active") else 3)\n'
            'if a=="is-enabled":\n print("enabled" if d.get(n,{}).get("enabled") else "disabled");sys.exit(0 if d.get(n,{}).get("enabled") else 1)\n'
            'if a in ("start","restart","stop","enable","disable"):\n'
            ' v=d.setdefault(n,{});v["active" if a in ("start","restart","stop") else "enabled"]=a not in ("stop","disable");p.write_text(json.dumps(d))\n')
        for directory in ('config', 'models', 'payload/lib/drone_tracker', 'payload/share/drone_tracker'):
            (self.bundle / directory).mkdir(parents=True, exist_ok=True)
        (self.bundle / 'payload/share/drone_tracker/local_setup.bash').write_text('')
        for name in ('drone_tracker_node', 'rk3576_recording_worker', 'rk3576_recording_inspect',
                     'rk3576_diagnostic_worker', 'rk3576_diagnostic_inspect'):
            path = self.bundle / 'payload/lib/drone_tracker' / name
            path.write_text('#!/bin/sh\nif [ "$DRONE_TRACKER_RELEASE_CAPABILITIES" = 1 ]; then\n'
                            'echo \'{"diagnostic_protocol_version":2,"topic_recording_affinity":1}\'\n'
                            'else echo 2; fi\n')
            path.chmod(0o755)
        (self.bundle / 'models/head.rknn').write_bytes(b'fixture-model')
        digest = hashlib.sha256(b'fixture-model').hexdigest()
        (self.bundle / 'MODELS.sha256').write_text(digest + '  models/head.rknn\n')
        (self.bundle / 'SOURCE.json').write_text('{}')
        (self.bundle / 'DEVICES').write_text('')
        diagnostic_text = (DEPLOYMENT / 'diagnostics.yaml').read_text()
        for before, after in self.replacements.items():
            diagnostic_text = diagnostic_text.replace(before, after)
        (self.bundle / 'config/diagnostics.yaml').write_text(diagnostic_text)
        shutil.copyfile(DEPLOYMENT / 'cpu-affinity.yaml', self.bundle / 'config/cpu-affinity.yaml')
        self.camera(True)
        self.checksums()

    def command(self, name, text):
        path = self.bin / name
        path.write_text(text)
        path.chmod(0o755)

    def camera(self, enabled):
        (self.bundle / 'config/camera.yaml').write_text(yaml.safe_dump({'drone_tracker_node': {'ros__parameters': {'need_save': enabled}}}))

    def checksums(self):
        lines = [hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + str(p.relative_to(self.bundle))
                 for p in sorted(self.bundle.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS']
        (self.bundle / 'SHA256SUMS').write_text('\n'.join(lines) + '\n')

    def invoke(self, *args, **env):
        self.checksums()
        return subprocess.run(['bash', str(self.bundle / 'deploy.sh'), *args], env=dict(self.env, **env),
                              capture_output=True, text=True, timeout=15)

    def assert_rejected_without_mutation(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse(self.target.exists())
        self.assertFalse(self.systemctl_log.exists())

    def test_fresh_install_preserves_policy_bytes(self):
        selected = (self.bundle / 'config/cpu-affinity.yaml').read_bytes()
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.target / 'config/cpu-affinity.yaml').read_bytes(), selected)
        self.assertTrue((self.target / 'current').is_symlink())
        self.assertIn('taskset -c "$cpus"', (self.target / 'bin/run-camera').read_text())

    def test_upgrade_and_matching_rollback(self):
        (self.target / 'config').mkdir(parents=True)
        (self.target / 'old-release').mkdir()
        (self.target / 'current').symlink_to(self.target / 'old-release')
        prior = b'version: 1\nmode: role-pinned\nallowed_cpus: [4,5,6,7]\n# old policy retained byte for byte\n'
        (self.target / 'config/cpu-affinity.yaml').write_bytes(prior)
        (self.target / 'config/camera.yaml').write_text('old camera')
        for name in ('drone-tracker.service', 'drone-tracker-diagnostics.service'):
            (self.services / name).write_text('old ' + name)
        self.service_state.write_text(json.dumps({name: dict(active=True, enabled=True)
            for name in ('drone-tracker.service', 'drone-tracker-diagnostics.service')}))
        selected = (self.bundle / 'config/cpu-affinity.yaml').read_bytes()
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.target / 'config/cpu-affinity.yaml').read_bytes(), selected)
        backup = next(p for p in (self.target / 'backups').iterdir() if (p / 'installation.tar').exists())
        result = subprocess.run(['bash', str(self.bundle / 'rollback.sh'), str(backup)], env=self.env,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.target / 'config/cpu-affinity.yaml').read_bytes(), prior)
        self.assertEqual((self.target / 'config/camera.yaml').read_text(), 'old camera')
        self.assertEqual((self.target / 'current').resolve(), self.target / 'old-release')

    def test_missing_role_rejected_before_check_or_install_mutates(self):
        policy = yaml.safe_load((self.bundle / 'config/cpu-affinity.yaml').read_text())
        del policy['roles']['topic_recording']
        (self.bundle / 'config/cpu-affinity.yaml').write_text(yaml.safe_dump(policy))
        for arguments in (('--check',), ()):
            result = self.invoke(*arguments)
            self.assert_rejected_without_mutation(result)
            self.assertIn('roles.topic_recording', result.stderr)

    def test_unavailable_cpu_fails_before_mutation(self):
        result = self.invoke('--check', FAIL_CPU='2')
        self.assert_rejected_without_mutation(result)
        self.assertIn('CPU 2', result.stderr)

    def test_older_node_rejected_before_check_or_install_mutates(self):
        node = self.bundle / 'payload/lib/drone_tracker/drone_tracker_node'
        node.write_text('#!/bin/sh\necho 2\n')
        for arguments in (('--check',), ()):
            result = self.invoke(*arguments)
            self.assert_rejected_without_mutation(result)
            self.assertIn('cannot confirm roles.topic_recording support', result.stderr)

    def test_custom_inherit_and_disabled_legacy_are_preserved(self):
        for inherit in (True, False):
            policy = yaml.safe_load((DEPLOYMENT / 'cpu-affinity.yaml').read_text())
            del policy['roles']['topic_recording']
            if inherit:
                policy['mode'] = 'inherit'
            self.camera(inherit)
            selected = yaml.safe_dump(policy).encode()
            (self.bundle / 'config/cpu-affinity.yaml').write_bytes(selected)
            result = self.invoke()
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((self.target / 'config/cpu-affinity.yaml').read_bytes(), selected)

    def test_custom_core_and_offline_topology(self):
        policy = yaml.safe_load((self.bundle / 'config/cpu-affinity.yaml').read_text())
        policy['allowed_cpus'].append(3)
        policy['roles']['topic_recording'] = 3
        path = self.bundle / 'config/cpu-affinity.yaml'
        path.write_text(yaml.safe_dump(policy))
        selected = path.read_bytes()
        with mock.patch.object(Path, 'is_dir', return_value=False):
            self.assertEqual(POLICY.parse(str(path), check_available=False, require_topic_recording=True), policy['allowed_cpus'])
            with self.assertRaisesRegex(ValueError, 'unavailable'):
                POLICY.parse(str(path), check_available=True, require_topic_recording=True)
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.target / 'config/cpu-affinity.yaml').read_bytes(), selected)


if __name__ == '__main__':
    unittest.main()

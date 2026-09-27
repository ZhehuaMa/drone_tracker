"""本机打包入口、模型路径和失败时归档清理测试。"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

DEPLOYMENT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(DEPLOYMENT))
import package_core  # noqa: E402


class ModelMappingTest(unittest.TestCase):
    def test_valid_and_unsafe_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'fp16').mkdir()
            (root / 'fp16/head.rknn').write_bytes(b'head')
            valid = '/opt/drone_tracker/models/fp16/head.rknn'
            params = {'tracker': {key: valid for key in package_core.MODEL_KEYS}}
            self.assertEqual(package_core.model_sources(params, root),
                             {Path('fp16/head.rknn'): (root / 'fp16/head.rknn').resolve()})
            for invalid in ('/opt/drone_tracker/models/missing.rknn',
                            '/opt/drone_tracker/models/../escape.rknn',
                            '/tmp/escape.rknn', ''):
                params['tracker']['nano_head'] = invalid
                with self.assertRaises(RuntimeError, msg=invalid):
                    package_core.model_sources(params, root)
            (root / 'escape.rknn').symlink_to('/etc/hosts')
            params['tracker']['nano_head'] = '/opt/drone_tracker/models/escape.rknn'
            with self.assertRaises(RuntimeError):
                package_core.model_sources(params, root)


class EntryTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.models = self.root / 'models'
        self.models.mkdir()
        fake_bin = self.root / 'bin'
        fake_bin.mkdir()
        docker = fake_bin / 'docker'
        docker.write_text('#!/bin/sh\n'
                          'case "$1" in\n'
                          '  image) printf "sha256:test-image\\n";;\n'
                          '  run) printf "%s\\n" "$@" > "$FAKE_DOCKER_LOG";;\n'
                          'esac\n')
        docker.chmod(0o755)
        git = fake_bin / 'git'
        git.write_text('#!/bin/sh\n'
                       'case "$3" in\n'
                       '  rev-parse) printf "test-commit\\n";;\n'
                       '  status) exit 0;;\n'
                       'esac\n')
        git.chmod(0o755)
        self.log = self.root / 'docker-args.txt'
        self.env = dict(os.environ, PATH=f'{fake_bin}:{os.environ["PATH"]}',
                        FAKE_DOCKER_LOG=str(self.log))

    def invoke(self, *arguments):
        return subprocess.run([str(DEPLOYMENT / 'package.sh'), *arguments],
                              env=self.env, text=True, capture_output=True)

    def test_defaults_and_explicit_overrides(self):
        output = str(self.root / 'output')
        base = self.invoke(output, '--model-root', str(self.models))
        self.assertEqual(base.returncode, 0, base.stderr)
        args = self.log.read_text()
        self.assertIn('rk3576_sc233hgs_production_params.yaml', args)
        self.assertIn('.docker-artifacts/install/drone_tracker', args)
        self.assertIn('deployment/cpu-affinity.yaml', args)
        for name, option in [('camera.yaml', '--camera-config'),
                             ('policy.yaml', '--cpu-policy')]:
            (self.root / name).write_text('test')
        install = self.root / 'install'
        install.mkdir()
        explicit = self.invoke(output, '--model-root', str(self.models),
                               '--camera-config', str(self.root / 'camera.yaml'),
                               '--cpu-policy', str(self.root / 'policy.yaml'),
                               '--install-root', str(install), '--without-models')
        self.assertEqual(explicit.returncode, 0, explicit.stderr)
        args = self.log.read_text()
        for path in (self.root / 'camera.yaml', self.root / 'policy.yaml', install):
            self.assertIn(str(path), args)
        self.assertIn('PACKAGE_WITHOUT_MODELS=1', args)

    def test_missing_inputs_fail_before_docker(self):
        output = str(self.root / 'output')
        self.assertEqual(self.invoke(output).returncode, 2)
        self.assertEqual(self.invoke(output, '--model-root', str(self.root / 'absent')).returncode, 2)
        self.assertFalse(self.log.exists())


class AtomicOutputTest(unittest.TestCase):
    def test_changed_input_leaves_no_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'input'
            source.write_bytes(b'original')

            def changed(bundle, _info):
                (bundle / 'payload').write_bytes(b'data')
                expected = package_core.digest(source)
                source.write_bytes(b'changed')
                return {source: expected}

            with (mock.patch.object(package_core.os, 'uname', return_value=mock.Mock(machine='aarch64')),
                  mock.patch.object(package_core, 'make_bundle', side_effect=changed),
                  mock.patch.dict(os.environ, self.provenance())):
                with self.assertRaisesRegex(RuntimeError, '输入文件变化'):
                    package_core.package(root)
            self.assertFalse(list(root.glob('*.tar.gz')))
            self.assertFalse(list(root.glob('*.sha256')))

    def test_archive_failure_leaves_no_success_name(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'input'
            source.write_bytes(b'original')

            def stable(bundle, _info):
                (bundle / 'payload').write_bytes(b'data')
                return {source: package_core.digest(source)}

            with (mock.patch.object(package_core.os, 'uname', return_value=mock.Mock(machine='aarch64')),
                  mock.patch.object(package_core, 'make_bundle', side_effect=stable),
                  mock.patch.object(package_core.tarfile, 'open', side_effect=OSError('archive failed')),
                  mock.patch.dict(os.environ, self.provenance())):
                with self.assertRaisesRegex(OSError, 'archive failed'):
                    package_core.package(root)
            self.assertFalse(list(root.glob('*.tar.gz')))
            self.assertFalse(list(root.glob('*.sha256')))

    @staticmethod
    def provenance():
        return {key: 'test' for key in (
            'PACKAGE_CAMERA_SOURCE', 'PACKAGE_POLICY_SOURCE', 'PACKAGE_INSTALL_SOURCE',
            'PACKAGE_MODEL_SOURCE', 'PACKAGE_GIT_COMMIT', 'PACKAGE_GIT_DIRTY',
            'PACKAGE_IMAGE', 'PACKAGE_IMAGE_ID', 'PACKAGE_WITHOUT_MODELS',
            'PACKAGE_OUTPUT_SOURCE')}


if __name__ == '__main__':
    unittest.main()

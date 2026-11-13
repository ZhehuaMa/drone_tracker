"""Host build contracts: architecture selection, cache preservation, and error propagation."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]


class BuildBackendContractTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.calls = self.root / "docker-calls.jsonl"
        docker = self.bin / "docker"
        docker.write_text("""#!/usr/bin/python3
import json, os, sys
with open(os.environ['FAKE_DOCKER_CALLS'], 'a') as output:
    output.write(json.dumps(sys.argv[1:]) + '\\n')
arguments = sys.argv[1:]
if arguments[:2] == ['image', 'inspect']:
    print(os.environ.get('FAKE_IMAGE_PLATFORM', 'linux/amd64') if arguments[-1] == '{{.Os}}/{{.Architecture}}' else os.environ.get('FAKE_IMAGE_ID', 'sha256:test-image'))
elif arguments[:2] == ['container', 'inspect']:
    sys.exit(1)
elif arguments[0] == 'run':
    sys.exit(int(os.environ.get('FAKE_RUN_STATUS', '0')))
""")
        docker.chmod(0o755)
        self.environment = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"],
                                FAKE_DOCKER_CALLS=str(self.calls))
        self.artifacts = self.root / "artifacts"
        self.messages = self.root / "quadrotor_msgs"
        self.messages.mkdir()
        (self.messages / "package.xml").write_text('<package><name>quadrotor_msgs</name></package>')

    def run_build(self, *arguments, **environment):
        return subprocess.run(["bash", str(REPO / "build.sh"), "--artifacts-dir", str(self.artifacts),
                               "--quadrotor-msgs", str(self.messages),
                               *arguments], env=dict(self.environment, **environment), text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def commands(self):
        return [json.loads(line) for line in self.calls.read_text().splitlines()]

    def sentinel(self, relative):
        target = self.artifacts / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(relative)
        return target

    def test_default_clean_preserves_other_caches_and_readonly_source(self):
        legacy = self.sentinel("build/legacy")
        other = self.sentinel("backends/arm64/build/other")
        selected = self.sentinel("backends/amd64/build/selected")
        result = self.run_build("--clean")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertTrue(legacy.exists())
        self.assertTrue(other.exists())
        self.assertFalse(selected.exists())
        self.assertEqual(len(list((self.artifacts / "backups/amd64").glob("*/build/selected"))), 1)
        invocation = next(command for command in self.commands() if command[0] == "run")
        self.assertIn("linux/amd64", invocation)
        self.assertEqual(invocation[invocation.index("--network") + 1], "none")
        self.assertIn(f"type=bind,src={REPO},dst={REPO},readonly", invocation)
        self.assertIn("DRONE_BUILD_BACKEND=amd64", invocation)

    def test_explicit_arm64_and_test_selection(self):
        result = self.run_build("--backend", "arm64", "--regression-only",
                                FAKE_IMAGE_PLATFORM="linux/arm64")
        self.assertEqual(result.returncode, 0, result.stdout)
        invocation = next(command for command in self.commands() if command[0] == "run")
        self.assertIn("linux/arm64", invocation)
        self.assertIn("DRONE_BUILD_TESTS=ON", invocation)
        self.assertIn("DRONE_TEST_REGEX=tracking_regression", invocation)

    def test_wrong_image_architecture_never_runs(self):
        result = self.run_build(FAKE_IMAGE_PLATFORM="linux/arm64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No fallback attempted", result.stdout)
        self.assertFalse(any(command[0] == "run" for command in self.commands()))

    def test_container_failure_propagates_without_retry(self):
        result = self.run_build("--test", FAKE_RUN_STATUS="29")
        self.assertEqual(result.returncode, 29, result.stdout)
        self.assertEqual(sum(command[0] == "run" for command in self.commands()), 1)

    def test_changed_image_requires_clean_and_preserves_cache(self):
        first = self.run_build()
        self.assertEqual(first.returncode, 0, first.stdout)
        selected = self.sentinel("backends/amd64/build/selected")
        changed = self.run_build(FAKE_IMAGE_ID="sha256:new-image")
        self.assertNotEqual(changed.returncode, 0)
        self.assertIn("Rerun with --clean", changed.stdout)
        self.assertTrue(selected.exists())
        self.assertEqual(sum(command[0] == "run" for command in self.commands()), 1)
        clean = self.run_build("--clean", FAKE_IMAGE_ID="sha256:new-image")
        self.assertEqual(clean.returncode, 0, clean.stdout)
        self.assertEqual(len(list((self.artifacts / "backups/amd64").glob("*/build/selected"))), 1)
        self.assertIn("image=sha256:new-image", (self.artifacts / "backends/amd64/build-environment.txt").read_text())

    def test_locked_cache_is_not_cleaned(self):
        selected = self.sentinel("backends/amd64/build/selected")
        key = subprocess.check_output(["cksum"], input=str(self.artifacts / "backends/amd64").encode()).split()[0].decode()
        lock = self.artifacts / "locks" / f"amd64-{key}"
        lock.mkdir(parents=True)
        (lock / "pid").write_text("12345")
        result = self.run_build("--clean")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cache is locked", result.stdout)
        self.assertTrue(selected.exists())
        self.assertFalse(any(command[0] == "run" for command in self.commands()))


if __name__ == "__main__":
    unittest.main()

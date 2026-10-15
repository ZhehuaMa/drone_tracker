#!/usr/bin/env python3
"""Run portable Python contracts natively and real ARM ctypes checks on ARM Python."""
import argparse
import json
from pathlib import Path
import platform
import subprocess
import sys
import unittest

TEST_DIRECTORY = Path(__file__).resolve().parent / "tests"
TARGET_TEST_IDS = frozenset({
    "test_identity.DynamicIdentityTests.test_actual_ros_middleware_and_missing_selector",
})


def flatten(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from flatten(item)
        else:
            yield item


def execute(tests):
    result = unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(tests))
    if result.skipped:
        print("SDK Python coverage must not skip discovered cases", file=sys.stderr)
        return 1
    return 0 if result.wasSuccessful() else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-launcher", type=Path)
    parser.add_argument("--target-only", action="store_true")
    arguments = parser.parse_args()
    sys.path.insert(0, str(TEST_DIRECTORY))
    if arguments.target_only:
        if platform.machine() != "aarch64":
            raise RuntimeError("ARM middleware tests require genuine ARM Python")
        # identity.py starts ctypes probes with sys.executable. Make those children
        # enter the same selected emulator rather than the host binfmt handler.
        sys.executable = str(Path(__file__).resolve().parent / "target_python.sh")
        suite = unittest.defaultTestLoader.loadTestsFromNames(sorted(TARGET_TEST_IDS))
        tests = list(flatten(suite))
        if {test.id() for test in tests} != TARGET_TEST_IDS or len(tests) != len(TARGET_TEST_IDS):
            raise RuntimeError("Target Python test IDs differ from the required partition")
        return execute(tests)
    if not arguments.target_launcher:
        parser.error("--target-launcher is required for native coordination")
    suite = unittest.defaultTestLoader.discover(str(TEST_DIRECTORY), pattern="test_*.py")
    tests = list(flatten(suite))
    ids = [test.id() for test in tests]
    discovered = set(ids)
    if len(ids) != len(discovered) or not TARGET_TEST_IDS.issubset(discovered):
        raise RuntimeError("Python discovery duplicated IDs or lost a required ARM case")
    native = [test for test in tests if test.id() not in TARGET_TEST_IDS]
    native_ids = {test.id() for test in native}
    if native_ids & TARGET_TEST_IDS or native_ids | TARGET_TEST_IDS != discovered:
        raise RuntimeError("Python architecture partition lost or duplicated a test")
    print(json.dumps({"native_test_ids": sorted(native_ids),
                      "arm64_test_ids": sorted(TARGET_TEST_IDS),
                      "total": len(ids)}, indent=2), flush=True)
    if execute(native):
        return 1
    return subprocess.run([str(arguments.target_launcher), str(Path(__file__).resolve()),
                           "--target-only"], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())

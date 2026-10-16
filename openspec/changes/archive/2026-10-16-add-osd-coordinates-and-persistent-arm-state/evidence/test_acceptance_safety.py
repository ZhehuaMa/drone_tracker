"""Native tests: no ROS, board, thermal policy or production services required."""
import json
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from acceptance_safety import AcceptanceAborted, Diagnostics, cleanup_and_restore, stop_owned_process


class SafetyTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.sensor = self.root / 'thermal_zone0' / 'temp'
        self.sensor.parent.mkdir()
        self.sensor.write_text('70000')
        self.log = self.root / 'diagnostics.jsonl'
        self.diagnostics = Diagnostics(self.log, self.root)

    def events(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_temperature_abort_is_durable_and_incomplete(self):
        self.diagnostics.check(force=True)
        self.sensor.write_text('95000')
        with self.assertRaises(AcceptanceAborted):
            self.diagnostics.check(force=True)
        self.assertEqual(self.events()[-1]['event'], 'thermal_abort')
        self.assertFalse(self.events()[-1]['acceptance_completed'])
        self.assertEqual(self.events()[-2]['temps'][str(self.sensor)], 95000)

    def test_missing_sensor_aborts(self):
        self.sensor.unlink()
        with self.assertRaises(AcceptanceAborted):
            self.diagnostics.check(force=True)
        self.assertEqual(self.events()[-1]['event'], 'thermal_monitor_failed')

    def test_thermal_abort_still_restores_services(self):
        calls = []
        self.sensor.write_text('95000')
        with self.assertRaises(AcceptanceAborted):
            try:
                self.diagnostics.check(force=True)
            finally:
                cleanup_and_restore(None, self.diagnostics, lambda: calls.append('restore'),
                                    lambda: {'active': True})
        self.assertEqual(calls, ['restore'])
        self.assertFalse(any(event.get('acceptance_completed') is True for event in self.events()))

    def test_node_timeout_kills_only_owned_process(self):
        child = subprocess.Popen([sys.executable, '-u', '-c',
                                  'import signal,time; signal.signal(signal.SIGINT, signal.SIG_IGN); print("ready",flush=True); time.sleep(60)'],
                                 stdout=subprocess.PIPE, text=True)
        unrelated = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])
        self.addCleanup(lambda: unrelated.kill() if unrelated.poll() is None else None)
        self.addCleanup(unrelated.wait)
        try:
            self.assertEqual(child.stdout.readline().strip(), 'ready')
            with self.assertRaisesRegex(AcceptanceAborted, 'stop timeout'):
                stop_owned_process(child, self.diagnostics, timeout=0.03)
            self.assertEqual(child.returncode, -signal.SIGKILL)
            self.assertIsNone(unrelated.poll())
        finally:
            if child.poll() is None:
                child.kill()
            child.wait()
            child.stdout.close()
            unrelated.kill()
            unrelated.wait()

    def test_cleanup_failure_cannot_skip_restore_or_verify(self):
        calls = []
        with patch('acceptance_safety.stop_owned_process', side_effect=RuntimeError('timeout')):
            with self.assertRaisesRegex(AcceptanceAborted, 'node cleanup'):
                cleanup_and_restore(None, self.diagnostics,
                                    lambda: calls.append('restore'),
                                    lambda: calls.append('verify') or {'active': True})
        self.assertEqual(calls, ['restore', 'verify'])
        self.assertEqual(self.events()[-1]['evidence'], {'active': True})

    def test_failed_restore_is_reported_and_still_verified(self):
        calls = []
        def restore():
            raise RuntimeError('service failed')
        with self.assertRaisesRegex(AcceptanceAborted, 'service restoration'):
            cleanup_and_restore(None, self.diagnostics, restore,
                                lambda: calls.append('verify') or {'active': False})
        self.assertEqual(calls, ['verify'])
        self.assertFalse(self.events()[-1]['evidence']['active'])

    def test_invariant_failure_runs_after_restore(self):
        calls = []
        def verify():
            calls.append('verify')
            raise AssertionError('config changed')
        with self.assertRaisesRegex(AcceptanceAborted, 'config changed'):
            cleanup_and_restore(None, self.diagnostics, lambda: calls.append('restore'), verify)
        self.assertEqual(calls, ['restore', 'verify'])

    def test_signal_uses_same_abort_path(self):
        with patch('acceptance_safety.signal.signal') as install:
            self.diagnostics.install_signal_handlers()
        self.assertEqual({call.args[0] for call in install.call_args_list},
                         {signal.SIGINT, signal.SIGTERM, signal.SIGHUP})
        handler = install.call_args_list[0].args[1]
        with self.assertRaises(AcceptanceAborted):
            handler(signal.SIGINT, None)
        self.assertFalse(self.events()[-1]['acceptance_completed'])


if __name__ == '__main__':
    unittest.main()

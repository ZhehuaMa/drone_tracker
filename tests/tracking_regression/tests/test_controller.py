from pathlib import Path
import subprocess
import sys
import time
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_batch

@unittest.skipUnless(sys.platform=='linux','Linux process-tree controller required')
class ControllerTests(unittest.TestCase):
    def test_new_session_descendant_is_recovered(self):
        run_batch.enable_subreaper()
        process=subprocess.Popen([sys.executable,'-c',
            'import subprocess,sys;subprocess.Popen([sys.executable,"-c","import time;time.sleep(30)"],start_new_session=True)'],start_new_session=True)
        process.wait(timeout=2)
        until=time.monotonic()+1
        while not run_batch.descendants() and time.monotonic()<until:time.sleep(.01)
        self.assertTrue(run_batch.descendants())
        start=time.monotonic()
        self.assertTrue(run_batch.stop_children([process],start,start+3))
        self.assertLess(time.monotonic()-start,3)
        self.assertFalse(run_batch.descendants())

    def test_controller_finalization_is_externally_bounded(self):
        import tempfile
        from unittest.mock import patch
        original=subprocess.Popen
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'attempt'
            def blocked_controller(argv,**kwargs):
                return original([sys.executable,'-c','import time;time.sleep(30)'],**kwargs)
            begin=time.monotonic()
            with patch.object(run_batch.subprocess,'Popen',blocked_controller):
                self.assertFalse(run_batch.supervise({'output':str(output)},total_seconds=2))
            self.assertLess(time.monotonic()-begin,2)
            self.assertFalse(run_batch.descendants())
            import json
            completion=json.loads((output/'completion.json').read_text())
            self.assertFalse(completion['qualified'])
            self.assertIn('external_controller_deadline_or_failure',completion['reasons'])

    def test_qualified_file_then_hang_is_invalidated(self):
        import json,tempfile
        from unittest.mock import patch
        original=subprocess.Popen
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'attempt'
            body='import pathlib,json,time; pathlib.Path('+repr(str(output/'completion.json'))+').write_text(json.dumps({"schema_version":1,"qualified":True,"normal_end":True,"reasons":[],"files":{}}));time.sleep(30)'
            def hung(argv,**kwargs):return original([sys.executable,'-c',body],**kwargs)
            with patch.object(run_batch.subprocess,'Popen',hung):
                self.assertFalse(run_batch.supervise({'output':str(output)},total_seconds=2))
            completion=json.loads((output/'completion.json').read_text())
            self.assertFalse(completion['qualified'])
            self.assertIn('external_controller_deadline',completion['reasons'])
            self.assertTrue((output/'deadline.json').exists())
            self.assertFalse(run_batch.descendants())
            # A later planned attempt remains possible and leaves its own failure evidence.
            with patch.object(run_batch.subprocess,'Popen',hung):
                self.assertFalse(run_batch.supervise({'output':str(Path(tmp)/'next')},total_seconds=2))
            self.assertTrue((Path(tmp)/'next/deadline.json').exists())



class FrozenConfigurationTests(unittest.TestCase):
    def test_node_uses_recorded_config_if_external_file_changes(self):
        import json
        import socket
        import tempfile
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / 'run'
            fixture = root / 'fixture'
            output.mkdir()
            fixture.mkdir()
            (fixture / 'fixture.json').write_text('{}')
            config = {'schema_version': 2, 'max_supply_lateness_us': 50000, 'queue_capacity': 32,
                      'input_mode': 'predecode_all', 'clock_jump_tolerance_us': 5000, 'ros_domain_id': 83}
            external = root / 'config.json'
            external.write_text(json.dumps(config))
            launched = []
            listening = socket.socket(socket.AF_UNIX)

            class Process:
                def __init__(self, worker):
                    self.returncode = None if worker else 0
                def poll(self): return self.returncode
                def wait(self, timeout=None): return self.returncode
                def send_signal(self, signal): self.returncode = 0
                def kill(self): self.returncode = -9

            def launch(argv, **kwargs):
                launched.append(argv)
                worker = len(launched) == 1
                if worker:
                    listening.bind(str(output / 'diagnostics.sock'))
                    external.write_text(json.dumps({**config, 'max_supply_lateness_us': 999999}))
                else:
                    self.assertEqual(len(argv),2)
                    request=json.loads(Path(argv[1]).read_text())
                    self.assertEqual(Path(request['config']),output/'run-config.json')
                    self.assertEqual(json.loads(Path(request['config']).read_text()),config)
                return Process(worker)

            request = {'output': str(output), 'fixture': str(fixture), 'repo': str(root),
                       'config': str(external), 'worker': '/worker', 'binary': '/node'}
            try:
                with patch.object(run_batch, 'verify', return_value={'files': {}}), \
                     patch.object(run_batch, 'production_identity', return_value={}), \
                     patch.object(run_batch.subprocess, 'Popen', side_effect=launch), \
                     patch.object(run_batch, 'collect', return_value={'qualified': True}):
                    self.assertEqual(run_batch.worker_round(request), 0)
                self.assertEqual(len(launched), 2)
                self.assertNotEqual(json.loads(external.read_text()), config)
            finally:
                listening.close()

if __name__ == '__main__':
    unittest.main()

class PreparationDeadlineTests(unittest.TestCase):
    def test_outer_preparation_timeout_reaps_hung_controller(self):
        import tempfile
        from unittest.mock import patch
        import common
        original=subprocess.Popen
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'attempt'
            def launch(argv,**kwargs):
                return original([sys.executable,'-c','import time; time.sleep(30)'],**kwargs)
            request={'output':str(output),'duration_us':20_000_000,
                     'deadlines':{'preparation_seconds':.3,'cleanup_seconds':.5}}
            with patch.object(run_batch.subprocess,'Popen',side_effect=launch):
                self.assertFalse(run_batch.supervise(request))
            deadline=common.load(output/'deadline.json')
            self.assertTrue(deadline['preparation_timed_out'])
            self.assertTrue(deadline['resources_recovered'])
            self.assertIn('preparation_deadline',common.load(output/'completion.json')['reasons'])

class PlaybackBudgetTests(unittest.TestCase):
    def test_slow_preparation_and_long_playback_use_independent_budgets(self):
        import tempfile
        from unittest.mock import patch
        import common
        for duration in (20,180):
            with self.subTest(duration=duration), tempfile.TemporaryDirectory() as tmp:
                output=Path(tmp)/'attempt'
                elapsed=[0.0]
                class Process:
                    returncode=None
                    def poll(self):
                        if elapsed[0]>=45 and not (output/'input-clock.json').exists():
                            common.write(output/'input-clock.json',{'m0_ns':45000000000})
                        if elapsed[0]>=45+duration:
                            self.returncode=0
                        return self.returncode
                def launch(*args,**kwargs):
                    common.write(output/'collection.json',{'schema_version':2,'qualified':True,
                        'normal_end':True,'reasons':[],'files':{}})
                    return Process()
                def advance(seconds):elapsed[0]+=seconds
                request={'output':str(output),'duration_us':duration*1000000,
                         'configuration':{'schema_version':3},
                         'deadlines':{'preparation_seconds':90,'cleanup_seconds':10}}
                with patch.object(run_batch,'enable_subreaper'), patch.object(run_batch,'stop_children',return_value=True), \
                     patch.object(run_batch.time,'monotonic',side_effect=lambda:elapsed[0]), \
                     patch.object(run_batch.time,'sleep',side_effect=advance), \
                     patch.object(run_batch.subprocess,'Popen',side_effect=launch):
                    self.assertTrue(run_batch.bounded_attempt(request))
                deadline=common.load(output/'deadline.json')
                self.assertFalse(deadline['timed_out'])
                self.assertFalse(deadline['preparation_timed_out'])
                self.assertGreaterEqual(deadline['elapsed_seconds'],45+duration)

"""Serial execution and reports retain every planned round and case."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import common
import run_suite
import run_batch
from test_configuration import example
import configuration


class SuiteTests(unittest.TestCase):
    def test_prepare_failure_continues_and_recovery_failure_stops(self):
        for recovered,expected in ((True,2),(False,1)):
            with tempfile.TemporaryDirectory() as tmp:
                value=example();value['output_root']=str(Path(tmp)/'out')
                second=copy.deepcopy(value['clips'][0]);second['case_id']='b';value['clips'].append(second)
                path=Path(tmp)/'suite.yaml';path.write_text(json.dumps(value))
                with patch.object(run_suite,'preflight_inputs'), patch.object(run_suite,'prepare_case',return_value={
                    'operation_success':False,'resources_recovered':recovered,'reason':'test failure'}) as prepare:
                    report=run_suite.execute(configuration.load_config(path))
                self.assertEqual(prepare.call_count,expected)
                self.assertEqual(len(report['cases']),2)
                self.assertFalse(report['operation_success'])
                self.assertEqual(report['cases'][1]['status'],'incomplete' if recovered else 'not_executed')

    def test_run_keeps_all_rounds_and_uses_case_only_snapshots(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);value=example();value['operation']='run';value['rounds']=3
            value['output_root']=str(root/'out')
            value['run']={'cohort_id':'ref','source_repo':str(root),'binary':'node','worker':'worker',
                'settings':{'input_mode':'predecode_all','max_supply_lateness_us':50000,'queue_capacity':32,
                            'clock_jump_tolerance_us':5000,'ros_domain_id':83}}
            second=copy.deepcopy(value['clips'][0]);second['case_id']='b';value['clips'].append(second)
            for case in ('a','b'):
                directory=root/'out/fixtures'/case;directory.mkdir(parents=True)
                common.write(directory/'fixture.json',{'schema_version':2,'case_id':case,'frame_count':1,
                  'width':2,'height':2,'pixel_format':'gray8','duration_us':20000000,'start_us':55000000,
                  'end_us':75000000,'source_epoch_us':100000000,'sources':[{'path':'source','sha256':'a'*64}],
                  'files':{},'tool_sha256':'b'*64,'ffmpeg_version':'test','selection_mode':'interval',
                  'source_start_us':0,'source_end_us':180000000,
                  'analysis_windows':{'full':{'start_us':0,'end_us':20000000}}})
                common.write_lines(directory/'frames.jsonl',[{'pts_us':55000000,'source_timestamp_us':155000000,'driver_sequence':0}])
            path=root/'config.yaml';path.write_text(json.dumps(value));calls=[]
            def supervise(request):
                calls.append(request)
                directory=Path(request['output']);directory.mkdir()
                common.write(directory/'deadline.json',{'resources_recovered':True})
                return len(calls)!=1
            with patch.object(run_suite,'preflight_inputs'), patch.object(run_batch,'supervise',side_effect=supervise):
                report=run_suite.execute(configuration.load_config(path))
            self.assertEqual([request['configuration']['case_id'] for request in calls],['a']*3+['b']*3)
            self.assertEqual(len(report['cases'][0]['rounds']),3)
            self.assertFalse(report['operation_success'])
            self.assertNotIn('clips',calls[0]['configuration'])

    def test_missing_and_unexpected_comparison_cases_remain_visible(self):
        with tempfile.TemporaryDirectory() as tmp:
            value=example();value['operation']='compare';value['output_root']=str(Path(tmp)/'out')
            value['compare']={'comparison_id':'c','cases':{'extra':{'baseline':'base','candidates':['one']}}}
            path=Path(tmp)/'config.yaml';path.write_text(json.dumps(value))
            report=run_suite.execute(configuration.load_config(path))
            self.assertEqual({case['status'] for case in report['cases']},{'unexpected_case','missing_case'})
            self.assertEqual(report['performance_status'],'not_passed')

    def test_deadlines_use_duration_and_independent_preparation(self):
        self.assertEqual(run_batch.deadline_plan({'duration_us':20_000_000})['total_seconds'],120)
        self.assertEqual(run_batch.deadline_plan({'duration_us':180_000_000})['total_seconds'],280)
        self.assertEqual(run_batch.deadline_plan({'duration_us':20_000_001})['total_seconds'],121)
        plan=run_batch.deadline_plan({'duration_us':20_000_000})
        self.assertLess(45,plan['preparation_seconds'])

class HistoricalFreezeTests(unittest.TestCase):
    def test_new_suite_never_writes_a_historical_baseline(self):
        with tempfile.TemporaryDirectory() as tmp:
            value=example();value['operation']='freeze';value['output_root']=str(Path(tmp)/'out')
            value['freeze']={'baseline_id':'base','cases':{'a':{'runs':['one','two','three']}}}
            path=Path(tmp)/'config.yaml';path.write_text(json.dumps(value))
            with patch.object(run_suite.baseline,'validate_set',return_value=[{'identity':{'schema_version':1,'case_id':'a'}}]), \
                 patch.object(run_suite.baseline,'freeze') as freeze:
                report=run_suite.execute(configuration.load_config(path))
            self.assertFalse(report['operation_success'])
            self.assertIn('read-only',report['cases'][0]['reason'])
            freeze.assert_not_called()

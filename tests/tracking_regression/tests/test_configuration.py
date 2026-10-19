"""Whole-request validation must finish before any case can start."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import configuration
import run_suite


def example():
    return {'schema_version':1,'suite_id':'suite','operation':'prepare','output_root':'out',
            'prepare':{},'deadlines':{'preparation_seconds':90,'cleanup_seconds':10},
            'clips':[{'case_id':'a','source':{'video':'video.mkv','frame_logs':['frames.jsonl'],
                       'attitude_bags':['imu.db3'],'session':'session.json'},
                      'frame_selection_interval':{'start_seconds':55,'end_seconds':75}}]}


class ConfigurationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.path = self.root / 'config.yaml'

    def load(self, value):
        self.path.write_text(json.dumps(value))
        return configuration.load_config(self.path)

    def test_relative_paths_exact_microseconds_and_end(self):
        result = self.load(example())
        self.assertEqual(result['parsed']['clips'][0]['source']['video'],str(self.root/'video.mkv'))
        self.assertEqual(result['parsed']['clips'][0]['frame_selection_interval'],
                         {'start_us':55000000,'end_us':75000000})
        value = example()
        value['clips'][0]['frame_selection_interval'] = {'start_seconds':165,'end_seconds':'end'}
        self.assertEqual(self.load(value)['parsed']['clips'][0]['frame_selection_interval']['end_us'],'end')
        self.path.write_text(json.dumps(example()).replace('55,','55.000001,'))
        self.assertEqual(configuration.load_config(self.path)['parsed']['clips'][0]['frame_selection_interval']['start_us'],55000001)
        self.path.write_text(json.dumps(example()).replace('55,','1e-6,'))
        self.assertEqual(configuration.load_config(self.path)['parsed']['clips'][0]['frame_selection_interval']['start_us'],1)

    def test_rejections_point_to_fields(self):
        cases = []
        value = example();value['clips']=[];cases.append((value,'clips'))
        value = example();value['clips']*=2;cases.append((value,'case_id'))
        value = example();value['surprise']=True;cases.append((value,'surprise'))
        for bad in (None,{}, {'start_seconds':55}, {'end_seconds':75},
                    {'start_seconds':True,'end_seconds':75}, {'start_seconds':-1,'end_seconds':75},
                    {'start_seconds':55.0000001,'end_seconds':75},
                    {'start_seconds':55,'end_seconds':'END'}, {'start_seconds':75,'end_seconds':55}):
            value=example();value['clips'][0]['frame_selection_interval']=bad
            cases.append((value,'frame_selection_interval'))
        value=example();value['output_root']='.';value['clips'][0]['source']['video']='fixtures/source.mkv'
        cases.append((value,'output_root'))
        for value, field in cases:
            with self.subTest(value=value), self.assertRaisesRegex(ValueError,field):self.load(value)
        self.path.write_text('schema_version: 1\nschema_version: 1\n')
        with self.assertRaisesRegex(ValueError,'duplicate'):configuration.load_config(self.path)
        for number in ('.nan','.inf','-.inf'):
            self.path.write_text(json.dumps(example()).replace('55,',number+','))
            with self.assertRaisesRegex(ValueError,'frame_selection_interval.start_seconds'):configuration.load_config(self.path)

    def test_second_invalid_case_never_starts_first(self):
        value=example();second=copy.deepcopy(value['clips'][0]);second['case_id']='b'
        second['frame_selection_interval']['end_seconds']=1;value['clips'].append(second)
        self.path.write_text(json.dumps(value))
        with patch.object(run_suite,'prepare_case') as prepare, self.assertRaisesRegex(ValueError,r'clips\[1\]'):
            run_suite.main([str(self.path)])
        prepare.assert_not_called()
        self.assertFalse((self.root/'out').exists())

    def test_snapshot_survives_external_change_and_no_cli_override(self):
        value=example();second=copy.deepcopy(value['clips'][0]);second['case_id']='b';value['clips'].append(second)
        record=self.load(value)
        observed=[]
        def prepare(clip, output, budget):
            observed.append(clip['frame_selection_interval']['end_us'])
            self.path.write_text('invalid after operation started')
            return {'operation_success':True,'resources_recovered':True}
        with patch.object(run_suite,'preflight_inputs'), patch.object(run_suite,'prepare_case',side_effect=prepare):run_suite.execute(record)
        self.assertEqual(observed,[75000000,75000000])
        output=self.root/'out/fixtures'
        self.assertEqual((output/'config-source.yaml').read_text(),record['raw'])
        with self.assertRaisesRegex(ValueError,'Usage'):run_suite.main([str(self.path),'--rounds','5'])

class RetainedOperationSectionTests(unittest.TestCase):
    def test_future_freeze_paths_do_not_conflict_with_current_run_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            value=example();value['operation']='run';value['rounds']=3
            value['run']={'cohort_id':'ref','source_repo':'.','binary':'node','worker':'worker',
                          'settings':{'input_mode':'predecode_all','max_supply_lateness_us':50000,
                                      'queue_capacity':32,'clock_jump_tolerance_us':5000,'ros_domain_id':83}}
            value['freeze']={'baseline_id':'base','cases':{'a':{'runs':[
                f'out/runs/ref/a/run-{index:03d}' for index in range(1,4)]}}}
            path=Path(tmp)/'config.yaml';path.write_text(json.dumps(value))
            configuration.load_config(path)

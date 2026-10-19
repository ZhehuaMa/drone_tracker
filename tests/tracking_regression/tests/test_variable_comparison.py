"""New records are independent per case and cannot mix with historical evidence."""
import copy
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import baseline
import compare
import test_compare


class VariableComparisonTests(unittest.TestCase):
    setUp = test_compare.ComparisonTests.setUp
    run_fixture = test_compare.ComparisonTests.run_fixture
    rehash = test_compare.ComparisonTests.rehash

    def modern(self, name, case='short', duration=5_000_000):
        path=self.run_fixture(name,10)
        identity=baseline.read_json(path/'identity.json')
        identity.update(schema_version=2,case_id=case,duration_us=duration)
        windows={'full':{'start_us':0,'end_us':duration}}
        identity['input']={'frame_count':4,'width':32,'height':24,'start_us':10000000,
                           'end_us':10000000+duration,'analysis_windows':windows}
        identity['comparison_group']=copy.deepcopy(identity['comparison_group'])
        identity['comparison_group']['fixture']=baseline.canonical_digest({'case':case,'duration':duration})
        identity['comparison_group']['test_driver']={'configuration':{
            'schema_version':3,'case_id':case,'analysis_windows':windows,
            'deadlines':{'preparation_seconds':90,'cleanup_seconds':10}}}
        (path/'identity.json').write_text(json.dumps(identity))
        metrics=baseline.read_json(path/'metrics.json');metrics['schema_version']=2
        metrics['windows'].pop('core')
        for descriptor in metrics['windows']['full'].values():descriptor['window']=windows['full']
        (path/'metrics.json').write_text(json.dumps(metrics))
        self.rehash(path)
        completion=baseline.read_json(path/'completion.json');completion['schema_version']=2
        (path/'completion.json').write_text(json.dumps(completion))
        return path

    def test_short_case_freeze_compare_and_baseline_immutability(self):
        refs=[self.modern('reference-'+str(index)) for index in range(3)]
        candidates=[self.modern('candidate-'+str(index)) for index in range(3)]
        output=self.root/'base';baseline.freeze(refs,output)
        before=baseline.sha256(output/'baseline.json')
        report=compare.compare(output,candidates)
        self.assertEqual(report['status'],'comparable')
        self.assertEqual(report['schema_version'],2)
        self.assertEqual(report['decision']['status'],'not_set')
        self.assertEqual(before,baseline.sha256(output/'baseline.json'))
        with self.assertRaises(FileExistsError):baseline.freeze(refs,output)

    def test_cases_versions_and_ranges_cannot_mix(self):
        refs=[self.modern('reference-'+str(index)) for index in range(3)]
        baseline.freeze(refs,self.root/'base')
        changed=self.modern('candidate-other',case='other')
        with self.assertRaisesRegex(ValueError,'mixed'):baseline.validate_set([refs[0],refs[1],changed])
        historical=self.run_fixture('historical',10)
        with self.assertRaisesRegex(ValueError,'mixed'):baseline.validate_set([refs[0],refs[1],historical])
        candidates=[self.modern('candidate-'+str(index),duration=4_000_000) for index in range(3)]
        self.assertEqual(compare.compare(self.root/'base',candidates)['status'],'incomparable')
        self.assertEqual(compare.compare(self.root/'base',refs[:2])['status'],'incomparable')

    def test_unknown_version_and_bad_window_rejected(self):
        path=self.modern('reference-one')
        identity=baseline.read_json(path/'identity.json');identity['schema_version']=999
        (path/'identity.json').write_text(json.dumps(identity))
        with self.assertRaisesRegex(ValueError,'schema_version'):baseline.load_run(path)

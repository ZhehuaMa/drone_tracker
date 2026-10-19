"""Supplier placement must stay explicit, comparable, and independent of caller affinity."""
import copy
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
import collect
import common
import configuration
import identity
from test_configuration import example


class SupplierCpuTests(unittest.TestCase):
    def test_config_accepts_explicit_cpu_and_preserves_historical_schema(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=pathlib.Path(temporary)/'config.json'
            value=example();value.update(operation='run',rounds=3)
            settings={'input_mode':'predecode_all','max_supply_lateness_us':50000,'queue_capacity':32,
                      'clock_jump_tolerance_us':5000,'ros_domain_id':83,'supplier_cpu':4}
            value['run']={'cohort_id':'ref','source_repo':'.','binary':'node','worker':'worker','settings':settings}
            path.write_text(json.dumps(value))
            self.assertEqual(configuration.load_config(path)['parsed']['run']['settings']['supplier_cpu'],4)
            for bad in (True,-1,None,1.5,'4'):
                settings['supplier_cpu']=bad;path.write_text(json.dumps(value))
                with self.subTest(bad=bad),self.assertRaisesRegex(ValueError,'supplier_cpu'):
                    configuration.load_config(path)
            settings['supplier_cpu']=4
            with self.assertRaisesRegex(ValueError,'supplier_cpu'):
                common.validate_record({'schema_version':2,**settings},'run_config')

    def test_preflight_rejects_offline_and_disallowed_cores(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=pathlib.Path(temporary);(root/'cpu4').mkdir();(root/'cpu4/online').write_text('1\n')
            self.assertEqual(identity.supplier_cpu_condition({'supplier_cpu':4},[0,4],root),
                             {'mode':'single_cpu','cpu':4})
            self.assertEqual(identity.supplier_cpu_condition({},[0,4],root),{'mode':'inherit'})
            for bad in (True,-1,3,1024):
                with self.subTest(bad=bad),self.assertRaisesRegex(ValueError,'supplier_cpu'):
                    identity.supplier_cpu_condition({'supplier_cpu':bad},[0,4],root)
            (root/'cpu4/online').write_text('0\n')
            with self.assertRaisesRegex(ValueError,'not online'):
                identity.supplier_cpu_condition({'supplier_cpu':4},[0,4],root)

    def test_readback_is_required_and_must_precede_clock(self):
        config={'supplier_cpu':4};clock={'m0_ns':100}
        proof={'requested_cpu':4,'allowed_cpus':[4],'inherited_cpus':[0,1,2,3,4,5,6,7],
               'tid':42,'policy':0,'priority':0,'configured_mono_ns':90}
        self.assertEqual(collect.qualify_supplier(config,proof,clock),[])
        self.assertTrue(collect.qualify_supplier(config,None,clock))
        for key,value in [('allowed_cpus',[0,4]),('requested_cpu',3),('configured_mono_ns',100),
                          ('tid',0),('policy',-1),('inherited_cpus',[0])]:
            bad=copy.deepcopy(proof);bad[key]=value
            with self.subTest(key=key):self.assertTrue(collect.qualify_supplier(config,bad,clock))
        self.assertEqual(collect.qualify_supplier({},None,clock),[])

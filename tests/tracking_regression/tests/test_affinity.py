"""Reject CPU treatments that hide unrelated changes or lack actual placement evidence."""
import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import affinity
import affinity_experiment as experiment
import baseline
import common
import configuration
import cpu_evidence
import compare
from test_configuration import example
import test_compare as comparison_fixtures


def pinned():
    return {'version':1,'mode':'role-pinned','allowed_cpus':list(range(8)), 'control_cpu':1,
            'tracking_cpu':5,'yolo_cpu':6,'logger_cpu':7,'supervision_cpus':[0,1,2,3]}


def settings():
    return {'supplier_cpu':4,'input_mode':'predecode_all','max_supply_lateness_us':50000,'queue_capacity':32,
            'clock_jump_tolerance_us':5000,'ros_domain_id':83,'cpu_affinity':pinned(),
            'cpu_observation':{'version':1,'capacity_per_writer':262144,'sampling':'all_managed_points'}}


class AffinityTests(unittest.TestCase):
    def test_config_versions_fields_and_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); path=root/'suite.json'; config=example()
            config.update(operation='run', rounds=3)
            config['run']={'cohort_id':'cpu','source_repo':'.','binary':'node','worker':'worker','settings':settings()}
            path.write_text(json.dumps(config))
            parsed=configuration.load_config(path)['parsed']
            self.assertEqual(parsed['run']['settings'],settings())
            for key,value in [('tracking_cpu',True),('yolo_cpu',-1),('control_cpu',8),('logger_cpu',6),('extra',0),('allowed_cpus',[0,0,4,5,6,7])]:
                bad=copy.deepcopy(config); bad['run']['settings']['cpu_affinity'][key]=value;path.write_text(json.dumps(bad))
                with self.subTest(key=key),self.assertRaisesRegex(ValueError,r'\$\.run\.settings\.cpu_affinity'):
                    configuration.load_config(path)
            for mode in ({'version':1,'mode':'inherit'},None):
                value=copy.deepcopy(config)
                if mode:value['run']['settings']['cpu_affinity']=mode
                else:value['run']['settings'].pop('cpu_affinity');value['run']['settings'].pop('cpu_observation')
                path.write_text(json.dumps(value));configuration.load_config(path)
            for capacity in (True,0,-1):
                bad=copy.deepcopy(config);bad['run']['settings']['cpu_observation']['capacity_per_writer']=capacity
                path.write_text(json.dumps(bad))
                with self.assertRaisesRegex(ValueError,'cpu_observation.capacity_per_writer'):configuration.load_config(path)

    def test_online_initial_cgroup_and_environment(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);(root/'online').write_text('0-7\n')
            for cpu in range(8): (root/f'cpu{cpu}').mkdir();(root/f'cpu{cpu}/online').write_text('1\n')
            def check(**kw):return affinity.preflight(settings(),environment={},allowed=list(range(8)),cpu_root=root,**kw)
            self.assertEqual(check(effective={'cpus':list(range(8))})['initial_allowed'],list(range(8)))
            with self.assertRaisesRegex(ValueError,'cgroup'):check(effective={'cpus':[5]})
            (root/'cpu4/online').write_text('0\n')
            with self.assertRaisesRegex(ValueError,'not online'):check(effective={'cpus':list(range(8))})
            with self.assertRaisesRegex(ValueError,'external'):affinity.preflight(settings(),environment={'DRONE_TRACKER_CPU_CONFIG':'/production'})
            self.assertIsNone(affinity.preflight({'supplier_cpu':4},environment={'DRONE_TRACKER_CPU_CONFIG':'old'}))

    def test_cgroup_without_delegated_cpuset_inherits_nearest_effective_limit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);proc=root/'proc';cgroup=root/'cgroup'
            (proc/'self').mkdir(parents=True);child=cgroup/'system.slice/usb-gadget.service';child.mkdir(parents=True)
            (proc/'self/cgroup').write_text('0::/system.slice/usb-gadget.service\n')
            (cgroup/'cpuset.cpus.effective').write_text('0-7\n')
            inherited=affinity.effective_cpuset(proc,cgroup)
            self.assertEqual(inherited['cpus'],list(range(8)))
            self.assertEqual(inherited['path'],str(cgroup/'cpuset.cpus.effective'))
            (child/'cpuset.cpus.effective').write_text('4-5\n')
            self.assertEqual(affinity.effective_cpuset(proc,cgroup)['cpus'],[4,5])

    def test_supervisor_restores_mask_when_attempt_raises(self):
        initial=os.sched_getaffinity(0)
        @affinity.restore_mask_after
        def failed(request):
            affinity.restrict_self([min(initial)])
            raise RuntimeError('injected controller failure')
        with self.assertRaisesRegex(RuntimeError,'injected'):
            failed({'configuration':{'cpu_affinity':{'mode':'role-pinned'}}})
        self.assertEqual(os.sched_getaffinity(0),initial)

    def test_unknown_threads_and_exit_races_are_not_managed_proof(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);(root/'12/task/14').mkdir(parents=True)
            row=affinity.snapshot({'node':12},'model-ready',proc=root)['threads'][0]
            self.assertTrue(row['exited_during_read']);self.assertEqual(row['attribution'],'unknown_library_or_middleware')
            directory=root/'12/task/14';(directory/'status').write_text('Name:\trknn-library\nCpus_allowed_list:\t1,4-5\n')
            tail=['0']*40;tail[36]='4';tail[38]='0'
            (directory/'stat').write_text('14 (rknn-library) '+' '.join(tail))
            snapshot=affinity.snapshot({'node':12},'periodic',proc=root)
            row=snapshot['threads'][0]
            self.assertEqual(row['allowed_cpus'],[1,4,5]);self.assertEqual(row['last_cpu'],4)
            self.assertEqual(row['attribution'],'unknown_library_or_middleware')
            self.assertGreater(row['observed_mono_ns'],0)
            self.assertLessEqual(row['observed_mono_ns'],snapshot['mono_ns'])

    def test_evidence_requires_exact_points_masks_and_capacity(self):
        config=settings();clock={'m0_ns':100}
        writers=[{'writer':i,'pid':10,'tid':20+i,'registered_ns':50,'policy':0,'priority':0,'valid':True,
                  'count':1 if i in (0,2,3) else 0,'overflow':0,'allowed_cpus':[{0:5,2:6,3:1}.get(i,0)]} for i in range(4)]
        meta={'schema_version':1,'sampling':'all_managed_points','capacity_per_writer':262144,'writers':writers}
        points=[{'writer':i,'pid':10,'tid':20+i,'cpu':{0:5,2:6,3:1}[i],'point':200,'round':0,'update_id':0,'request_id':0,'detail':0} for i in (0,2,3)]
        snapshots=[{'phase':p,'mono_ns':50 if p=='model-ready' else 150,'threads':[{'process_role':'logger','allowed_cpus':[7],'pid':11,'tid':11}]} for p in ('model-ready','playback-ended')]
        supervision={'allowed_cpus':[0,1,2,3],'mono_ns':50}
        self.assertTrue(cpu_evidence.qualify(config,meta,points,snapshots,clock,[],supervision))
        for key,value in [('overflow',1),('allowed_cpus',[4]),('registered_ns',101)]:
            bad=copy.deepcopy(meta);bad['writers'][0][key]=value
            self.assertTrue(cpu_evidence.qualify(config,bad,points,snapshots,clock,[],supervision))
        for bad_points in ([],[{**p,'cpu':0} for p in points]):
            self.assertTrue(cpu_evidence.qualify(config,meta,bad_points,snapshots,clock,[],supervision))
        events=[{'version':1,'type':'CatchupStep','round_id':1,'update_id':3,'request_id':9,'frame_time_us':i} for i in range(20)]
        events += [{'version':1,'type':name,'round_id':1,'update_id':3,'request_id':9} for name in ('UpdateStart','UpdateEnd','DetectStart','DetectStage','DetectEnd')]
        for e in events:
            writer=2 if e['type'].startswith('Detect') else 0
            points.append({'writer':writer,'pid':10,'tid':20+writer,'cpu':5 if writer==0 else 6,
                           'point':cpu_evidence.REQUIRED_TYPES[e['type']],'round':1,'update_id':3,'request_id':9,
                           'detail':0,'frame_time_us':e.get('frame_time_us',0)})
        for point in range(100,106):
            points.append({'writer':2,'pid':10,'tid':22,'cpu':6,'point':point,'round':1,'update_id':3,'request_id':9,'detail':0})
        for writer in (0,2,3):meta['writers'][writer]['count']=sum(p['writer']==writer for p in points)
        self.assertEqual(cpu_evidence.qualify(config,meta,points,snapshots,clock,events,supervision),[])
        preparation={'phase':'periodic','mono_ns':25,'threads':[
            {'process_role':'node','pid':10,'tid':23,'allowed_cpus':list(range(8))}]}
        def check_snapshot(snapshot):
            return cpu_evidence.qualify(config,meta,points,[snapshot,*snapshots],clock,events,supervision)
        self.assertEqual(check_snapshot(preparation),[])
        crossing=copy.deepcopy(preparation)
        crossing['mono_ns']=75;crossing['threads'][0]['observed_mono_ns']=25
        self.assertEqual(check_snapshot(crossing),[])
        for observed in (50,55):
            changed=copy.deepcopy(crossing);changed['threads'][0]['observed_mono_ns']=observed
            self.assertIn('cpu_observation: managed mask changed during playback',check_snapshot(changed))
        legacy=copy.deepcopy(preparation);legacy['mono_ns']=75
        self.assertIn('cpu_observation: managed mask changed during playback',check_snapshot(legacy))
        for observed in (0,True,76):
            malformed=copy.deepcopy(crossing);malformed['threads'][0]['observed_mono_ns']=observed
            self.assertIn('cpu_observation: invalid thread snapshot timestamp',check_snapshot(malformed))
        # Removing one of twenty otherwise identical recovery steps must be detected by exact frame identity.
        self.assertTrue(cpu_evidence.qualify(config,meta,points[1:],snapshots,clock,events,supervision))


class ExperimentTests(unittest.TestCase):
    def test_strict_leaves_and_common_driver(self):
        left={'cpu_policy':{'regression':{'mode':'inherit'},'supplier':{'cpu':4}},
              'test_driver':{'sources':[['main.cpp','a'],['metrics.py','z']], 'configuration':{'queue_capacity':32}},
              'models':{'head':'h'},'algorithm':'algo','dependencies':{'runtime':'2.3.2'},
              'observation':{'protocol_version':2,'timestamp':'capture','box':'double','los':'paired'}}
        right=copy.deepcopy(left);right['cpu_policy']['regression']['mode']='role-pinned'
        rows=[{'field':'comparison_group.cpu_policy.regression.mode','baseline':'inherit','candidate':'role-pinned','audit':'private CPU policy'}]
        self.assertEqual(experiment.compare_groups({'allowed_differences':rows},left,right)[0],[])
        for path,value in [('models','changed'),('algorithm','changed'),('dependencies','changed')]:
            bad=copy.deepcopy(right);bad[path]=value
            self.assertTrue(experiment.compare_groups({'allowed_differences':rows},left,bad)[0])
        bad=copy.deepcopy(right);bad['test_driver']['sources'][1][1]='changed'
        self.assertTrue(experiment.compare_groups({'allowed_differences':rows},left,bad)[0])
        bad=copy.deepcopy(right);bad['test_driver']['configuration']['queue_capacity']=64
        self.assertTrue(experiment.compare_groups({'allowed_differences':rows},left,bad)[0])
        for field in left['observation']:
            bad=copy.deepcopy(right);bad['observation'][field]='changed'
            self.assertTrue(experiment.compare_groups({'allowed_differences':rows},left,bad)[0])
            self.assertFalse(experiment.allowed_path('comparison_group.observation.'+field))
        bad=copy.deepcopy(rows);bad[0]['candidate']='wrong'
        self.assertTrue(experiment.compare_groups({'allowed_differences':bad},left,right)[0])
        for path in ('comparison_group.cpu_policy','comparison_group.test_driver','*','comparison_group.models.head',
                     'comparison_group.test_driver.sources.metrics.py','comparison_group.cpu_policy.governors'):
            self.assertFalse(experiment.allowed_path(path))

    def test_formal_comparison_ordinary_rejects_and_explicit_checks_builds(self):
        sample=comparison_fixtures.ComparisonTests();sample.setUp();self.addCleanup(sample.doCleanups)
        sample.group.update(cpu_policy={'regression':{'version':1,'mode':'inherit'}},
                            test_driver={'sources':[['main.cpp','a']], 'configuration':{'queue_capacity':32}},
                            observation={'cpu_observation':settings()['cpu_observation']})
        refs=[sample.run_fixture(f'reference-{i}',10,group=copy.deepcopy(sample.group)) for i in range(3)]
        candidate_group=copy.deepcopy(sample.group);candidate_group['cpu_policy']['regression']['mode']='role-pinned'
        cands=[sample.run_fixture(f'candidate-{i}',9,group=copy.deepcopy(candidate_group)) for i in range(3)]
        for paths,binary in ((refs,'a'),(cands,'b')):
            for path in paths:
                record=common.load(path/'identity.json');record['code'].update(binary_sha256=binary*64,project_libraries={})
                (path/'identity.json').write_text(json.dumps(record));sample.rehash(path)
        frozen=sample.root/'frozen';baseline.freeze(refs,frozen)
        self.assertEqual(compare.compare(frozen,cands)['status'],'incomparable')
        side=lambda paths:{'algorithm_commit':'4871442dda37d3d64087fda0b7756692c8f7e473',
                           **{k:common.load(paths[0]/'identity.json')['code'][k] for k in ('source_sha256','binary_sha256','project_libraries')},'source_patch_sha256':'c'*64}
        manifest={'schema_version':1,'experiment_id':'test','reference':side(refs),'candidate':side(cands),
                  'build':{'compiler':'GCC11','options':['Release']},'observation_patch_sha256':'d'*64,
                  'cpu_observation':settings()['cpu_observation'], 'allowed_differences':[
                      {'field':'comparison_group.cpu_policy.regression.mode','baseline':'inherit','candidate':'role-pinned','audit':'fixed CPU treatment'}]}
        path=sample.root/'experiment.json';path.write_text(json.dumps(manifest))
        result=compare.compare(frozen,cands,affinity_experiment=path)
        self.assertEqual(result['status'],'comparable',result['differences']);self.assertEqual(result['decision']['status'],'not_set')
        manifest['candidate']['binary_sha256']='e'*64;path.write_text(json.dumps(manifest))
        self.assertEqual(compare.compare(frozen,cands,affinity_experiment=path)['status'],'incomparable')
        manifest['allowed_differences'][0]['field']='comparison_group.cpu_policy';path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError,'unsupported'):experiment.load_manifest(path)

"""Exercise real identity construction before freezing/comparing synthetic cohorts."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import baseline
import compare
import common
import identity
import test_compare as comparison_fixtures


class InputIdentityTests(unittest.TestCase):
    run_fixture = comparison_fixtures.ComparisonTests.run_fixture
    rehash = comparison_fixtures.ComparisonTests.rehash

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        sample = comparison_fixtures.ComparisonTests()
        sample.setUp()
        self.addCleanup(sample.doCleanups)
        self.group = copy.deepcopy(sample.group)
        self.models = self.root / 'original-models'
        self.models.mkdir()
        self.algorithm = {'schema_version': 1, 'parameters': {'tracker.lost_max_count': 20},
                          'models': {}, 'nano_constants': {'score_size': 15}}
        for key in identity.MODEL_KEYS:
            path = self.models / key
            path.write_bytes(('model-content-' + key).encode())
            self.algorithm['parameters'][key] = str(path)
            self.algorithm['models'][key] = {'path': str(path), 'sha256': common.sha256(path)}

    def fixture(self, name, algorithm=None):
        directory = self.root / name
        directory.mkdir()
        algorithm = algorithm or self.algorithm
        common.write(directory / 'algorithm.yaml', algorithm)
        common.write(directory / 'fixture.json', {
            'schema_version': 1, 'case_id': common.CASE_ID, 'start_us': 55000000,
            'end_us': 75000000, 'frame_count': 1200, 'width': 1920, 'height': 1200,
            'pixel_format': 'gray8', 'source_epoch_us': 100000000,
            'files': {'clip.mkv': 'a' * 64, 'frames.jsonl': 'b' * 64,
                      'attitude.jsonl': 'c' * 64, 'algorithm.yaml': common.sha256(directory / 'algorithm.yaml')},
            'sources': [{'path': '/original/source.mp4', 'sha256': 'd' * 64}],
            'encoding': {'codec': 'ffv1', 'slices': 24}, 'tool_sha256': 'e' * 64})
        return directory

    def cohort_comparison(self, reference_group, candidate_group):
        refs = [self.run_fixture('reference-' + str(i), 400, group={**self.group, **reference_group}) for i in range(3)]
        candidates = [self.run_fixture('candidate-' + str(i), 250, group={**self.group, **candidate_group}) for i in range(3)]
        output = self.root / 'baseline'
        baseline.freeze(refs, output)
        return compare.compare(output, candidates)

    def test_changed_model_locators_still_compare_real_constructed_groups(self):
        first = self.fixture('first')
        moved = copy.deepcopy(self.algorithm)
        relocated = self.root / 'relocated-models'
        relocated.mkdir()
        for key in identity.MODEL_KEYS:
            new = relocated / key
            shutil.copyfile(self.models / key, new)
            moved['models'][key]['path'] = str(new)
            moved['parameters'][key] = str(new)
        second = self.fixture('second', moved)
        left, provenance_left, _ = identity.semantic_inputs(first, {})
        right, provenance_right, _ = identity.semantic_inputs(second, {})
        self.assertNotEqual(provenance_left['algorithm_file_sha256'], provenance_right['algorithm_file_sha256'])
        self.assertNotEqual(provenance_left['fixture_manifest_sha256'], provenance_right['fixture_manifest_sha256'])
        self.assertEqual(left, right)
        self.assertEqual(self.cohort_comparison(left, right)['status'], 'comparable')

    def test_runtime_mapping_preserves_immutable_fixture_and_configuration_identity(self):
        fixture = self.fixture('fixture')
        before = common.sha256(fixture / 'algorithm.yaml')
        relocated = self.root / 'mapped-models'
        shutil.copytree(self.models, relocated)
        config = {'max_supply_lateness_us': 50000}
        left, _, fixed = identity.semantic_inputs(fixture, config)
        right, paths, mapped = identity.semantic_inputs(fixture, {
            **config, 'model_paths': {key: str(relocated / key) for key in identity.MODEL_KEYS}})
        self.assertEqual(left, right)
        self.assertEqual(fixed, mapped)
        self.assertEqual(before, common.sha256(fixture / 'algorithm.yaml'))
        self.assertEqual(paths['models'][identity.MODEL_KEYS[0]]['run_path'], str(relocated / identity.MODEL_KEYS[0]))
        (relocated / identity.MODEL_KEYS[0]).write_bytes(b'different model')
        with self.assertRaisesRegex(ValueError, 'sha256 mismatch'):
            identity.semantic_inputs(fixture, {'model_paths': {identity.MODEL_KEYS[0]: str(relocated / identity.MODEL_KEYS[0])}})
        with self.assertRaisesRegex(ValueError, 'only frozen'):
            identity.semantic_inputs(fixture, {'model_paths': {'tracker.lost_max_count': 'x'}})

    def test_supplier_placement_change_requires_new_reference_conditions(self):
        fixture = self.fixture('supplier-fixture')
        reference, _, reference_config = identity.semantic_inputs(fixture, {'supplier_cpu': 4})
        candidate, _, candidate_config = identity.semantic_inputs(fixture, {'supplier_cpu': 5})
        result = self.cohort_comparison(
            {**reference, 'test_driver': {'configuration': reference_config}},
            {**candidate, 'test_driver': {'configuration': candidate_config}})
        self.assertEqual(result['status'], 'incomparable')

    def test_true_model_content_change_is_incomparable(self):
        first = self.fixture('first')
        left, _, _ = identity.semantic_inputs(first, {})
        changed = copy.deepcopy(self.algorithm)
        path = self.root / 'new-model'
        path.write_bytes(b'new model weights')
        key = identity.MODEL_KEYS[0]
        changed['parameters'][key] = str(path)
        changed['models'][key] = {'path': str(path), 'sha256': common.sha256(path)}
        right, _, _ = identity.semantic_inputs(self.fixture('second', changed), {})
        report = self.cohort_comparison(left, right)
        self.assertEqual(report['status'], 'incomparable')
        self.assertTrue(any(row['field'].endswith('comparison_group.models.' + key) for row in report['differences']))

    def test_actual_parameter_and_timing_changes_are_incomparable(self):
        first = self.fixture('first')
        left, _, _ = identity.semantic_inputs(first, {})
        changed = copy.deepcopy(self.algorithm)
        changed['parameters']['tracker.lost_max_count'] = 21
        second = self.fixture('second', changed)
        manifest = common.load(second / 'fixture.json')
        manifest['end_us'] += 1
        (second / 'fixture.json').write_text(json.dumps(manifest))
        right, _, _ = identity.semantic_inputs(second, {})
        report = self.cohort_comparison(left, right)
        self.assertEqual(report['status'], 'incomparable')
        fields = {row['field'] for row in report['differences']}
        self.assertTrue(any(field.endswith('comparison_group.algorithm') for field in fields))
        self.assertTrue(any(field.endswith('comparison_group.fixture') for field in fields))

    def test_only_pixel_manifest_change_is_incomparable(self):
        left, _, _ = identity.semantic_inputs(self.fixture('first'), {})
        second = self.fixture('second')
        manifest = common.load(second / 'fixture.json')
        manifest['files']['frames.jsonl'] = 'f' * 64
        (second / 'fixture.json').write_text(json.dumps(manifest))
        right, _, _ = identity.semantic_inputs(second, {})
        self.assertEqual(left['algorithm'], right['algorithm'])
        self.assertNotEqual(left['fixture'], right['fixture'])
        self.assertEqual(self.cohort_comparison(left, right)['status'], 'incomparable')


    def test_only_time_mapping_change_is_incomparable(self):
        left, _, _ = identity.semantic_inputs(self.fixture('first'), {})
        second = self.fixture('second')
        manifest = common.load(second / 'fixture.json')
        manifest['source_epoch_us'] += 1
        (second / 'fixture.json').write_text(json.dumps(manifest))
        right, _, _ = identity.semantic_inputs(second, {})
        self.assertEqual(left['algorithm'], right['algorithm'])
        self.assertNotEqual(left['fixture'], right['fixture'])
        self.assertEqual(self.cohort_comparison(left, right)['status'], 'incomparable')


class WorkloadIdentityTests(unittest.TestCase):
    def test_kernel_workers_are_observations_but_extra_userspace_is_a_condition(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            proc = root / 'proc'
            proc.mkdir()
            repo = root / 'test-tree/source'
            repo.mkdir(parents=True)
            def process(pid, name, flags=0, args=None, parent=1):
                directory = proc / str(pid)
                directory.mkdir(exist_ok=True)
                (directory / 'stat').write_text(f'{pid} ({name}) S {parent} 0 0 0 0 {flags} 0 0')
                (directory / 'cmdline').write_bytes(b'\0'.join(value.encode() for value in (args or [])))
            process(100, 'python3', args=['python3', str(repo / 'tests/tracking_regression/run_batch.py')])
            process(101, 'rkaiq_3A_server', args=['/usr/bin/rkaiq_3A_server'])
            # A misleading name is still a kernel thread when PF_KTHREAD is set.
            process(102, 'worker-original', 0x00200000)
            process(105, 'python3', args=['python3',str(repo / 'tests/tracking_regression/run_suite.py')])
            first, observations = identity.workload_processes(repo, proc, 100)
            self.assertEqual(first, ['rkaiq_3A_server'])
            self.assertTrue(next(row for row in observations if row['pid'] == 105)['control_process'])
            self.assertTrue(next(row for row in observations if row['pid'] == 102)['kernel_thread'])
            process(102, 'worker-renamed', 0x00200000)
            second, _ = identity.workload_processes(repo, proc, 100)
            self.assertEqual(first, second)
            # A userspace process named like a kernel worker must remain a condition.
            process(103, 'kworker/fake', args=['/external/load-generator'])
            process(104, 'python3', args=['python3', '/external/load-generator.py'])
            third, _ = identity.workload_processes(repo, proc, 100)
            differences = baseline.field_differences({'fixed_processes': first}, {'fixed_processes': third})
            self.assertTrue(differences)
            self.assertIn('python3', third)
            self.assertIn('kworker/fake', third)



@unittest.skipUnless(sys.platform == 'linux', 'ELF dynamic loader required')
class DynamicIdentityTests(unittest.TestCase):
    def test_actual_ros_middleware_and_missing_selector(self):
        binary = os.environ.get('DRONE_TRACKER_TEST_PROGRAM')
        self.assertTrue(binary, 'CMake must provide actual regression program for SDK middleware validation')
        _, paths, _ = identity.resolved_dependencies({'node': Path(binary)}, os.environ)
        loader = paths['node']['librmw_implementation.so']['resolved_path']
        selected, locations = identity.middleware_identity(loader, dict(os.environ))
        self.assertTrue(selected['effective'].startswith('rmw_'))
        self.assertIn('lib' + selected['effective'] + '.so', selected['libraries'])
        self.assertTrue(locations)
        with self.assertRaisesRegex(ValueError, 'probe failed'):
            identity.middleware_identity(loader, dict(os.environ, RMW_IMPLEMENTATION='rmw_missing_test'))

    def test_real_loader_alternative_content_and_locator_only_changes(self):
        # The executable and system libraries stay fixed. Only LD_LIBRARY_PATH selects
        # an alternative shared dependency, which hardcoded system hashing would miss.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler = shutil.which('cc')
            self.assertIsNotNone(compiler, 'SDK C compiler required')
            for name, value in [('system', 1), ('bundled', 2), ('copy', 1)]:
                directory = root / name
                directory.mkdir()
                source = directory / 'dependency.c'
                source.write_text('int dependency(void){return ' + str(value) + ';}')
                subprocess.run([compiler, '-shared', '-fPIC', str(source), '-Wl,-soname,libidentity.so',
                                '-o', str(directory / 'libidentity.so')], check=True)
            # Copy identical bytes to ensure the locator-only case has exactly equal content.
            shutil.copyfile(root / 'system/libidentity.so', root / 'copy/libidentity.so')
            source = root / 'program.c'
            source.write_text('extern int dependency(void); int main(void){return dependency();}')
            binary = root / 'program'
            subprocess.run([compiler, str(source), '-L' + str(root / 'system'), '-lidentity', '-o', str(binary)], check=True)
            binaries = {'node': binary, 'worker': binary}
            original_hash = common.sha256(root / 'system/libidentity.so')
            first, first_paths, _ = identity.resolved_dependencies(binaries, dict(os.environ, LD_LIBRARY_PATH=str(root / 'system')))
            second, _, _ = identity.resolved_dependencies(binaries, dict(os.environ, LD_LIBRARY_PATH=str(root / 'bundled')))
            relocated, paths, _ = identity.resolved_dependencies(binaries, dict(os.environ, LD_LIBRARY_PATH=str(root / 'copy')))
            self.assertNotEqual(first, second)
            self.assertEqual(first, relocated)
            self.assertNotEqual(first_paths['node']['libidentity.so']['path'], paths['node']['libidentity.so']['path'])
            self.assertEqual(original_hash, common.sha256(root / 'system/libidentity.so'))
            owned, _, code = identity.resolved_dependencies(binaries, dict(os.environ, LD_LIBRARY_PATH=str(root / 'bundled')), {'libidentity.so'})
            self.assertNotIn('libidentity.so', owned['node'])
            self.assertEqual(code['node']['libidentity.so']['sha256'], second['node']['libidentity.so'])
            with self.assertRaisesRegex(ValueError, 'resolution failed'):
                identity.resolved_dependencies(binaries, dict(os.environ, LD_LIBRARY_PATH=str(root / 'absent')))


if __name__ == '__main__':
    unittest.main()

"""Qualify actual C++ observer exports against the diagnostic producer layout."""
import copy
import csv
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import collect
import common
import cpu_evidence


class CpuObserverIntegrationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        subprocess.run([*LAUNCHER, EXPORTER, str(self.root)], check=True, timeout=10)
        self.metadata = common.load(self.root / 'cpu-observation.json')
        with (self.root / 'cpu-points.csv').open() as stream:
            self.points = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(stream)]
        raw = (self.root / 'events.raw').read_bytes()
        self.events = [collect.decode_event(raw[index:index + collect.EVENT.size])
                       for index in range(0, len(raw), collect.EVENT.size)]
        self.clock = common.load(self.root / 'clock.json')
        self.config = {'cpu_observation': {'capacity_per_writer': 32}}
        self.snapshots = [{'phase': phase, 'mono_ns': self.clock[key], 'threads': []}
                          for phase, key in (('model-ready', 'm0_ns'), ('playback-ended', 'end_ns'))]
        self.snapshots[0]['mono_ns'] -= 1

    def qualify(self, metadata=None, points=None, events=None):
        return cpu_evidence.qualify(self.config, metadata if metadata is not None else self.metadata,
                                    points if points is not None else self.points, self.snapshots, self.clock,
                                    events if events is not None else self.events, {})

    def test_real_format_two_export_and_legacy_layout(self):
        self.assertEqual({event['version'] for event in self.events}, {4})
        self.assertEqual([row['writer'] for row in self.metadata['writers']], list(range(5)))
        self.assertTrue(any(point['writer'] == 4 for point in self.points))
        self.assertEqual(self.qualify(), [])
        # Historical format 1 had four slots, with the same CPU schema version.
        legacy = copy.deepcopy(self.metadata)
        legacy['writers'].pop()
        old_points = [point for point in self.points if point['writer'] != 4]
        old_events = [{**event, 'version': 1} for event in self.events if event['producer_id'] != 4]
        self.assertEqual(self.qualify(legacy, old_points, old_events), [])
        self.assertTrue(self.qualify(legacy))  # A truncated current export is not historical evidence.
        self.assertTrue(self.qualify(events=old_events))
        unused = copy.deepcopy(self.metadata)
        unused['writers'][4] = {**unused['writers'][1], 'writer': 4}
        self.assertEqual(self.qualify(unused, old_points), [])

    def test_missing_duplicate_unknown_and_mixed_slots_rejected(self):
        for writers in (self.metadata['writers'][:-1],
                        self.metadata['writers'] + [self.metadata['writers'][-1]],
                        [*self.metadata['writers'][:-1], {**self.metadata['writers'][-1], 'writer': 5}]):
            with self.subTest(writers=writers):
                self.assertTrue(self.qualify({**self.metadata, 'writers': writers}))
        self.assertTrue(self.qualify(points=[*self.points, {**self.points[0], 'writer': 5}]))
        for version in (1, 99):
            self.assertTrue(self.qualify(events=[*self.events, {**self.events[0], 'version': version}]))
        self.assertTrue(self.qualify(events=[{key: value for key, value in event.items() if key != 'version'}
                                             for event in self.events]))

    def test_dispatcher_loss_and_identity_are_checked(self):
        for field, value in (('overflow', 1), ('count', 0), ('valid', False), ('tid', 0),
                             ('allowed_cpus', [])):
            with self.subTest(field=field):
                metadata = copy.deepcopy(self.metadata)
                metadata['writers'][4][field] = value
                self.assertTrue(self.qualify(metadata))
        for writer in (0, 2, 3):
            metadata = copy.deepcopy(self.metadata)
            metadata['writers'][writer]['registered_ns'] = self.clock['m0_ns'] + 1
            self.assertTrue(self.qualify(metadata))


if __name__ == '__main__':
    EXPORTER, *LAUNCHER = sys.argv[1:]
    unittest.main(argv=[sys.argv[0]])

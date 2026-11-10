from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import unittest
import validity_metrics as validity
import collect


def event(kind, time, values=None, counts=None, **extra):
    return dict(version=3, type=kind, mono_ns=time * 1000000, round_id=1,
                values=values or [0] * 7, counts=counts or [0] * 4, **extra)


class ValidityTests(unittest.TestCase):
    def test_recovery_milestones_and_incomplete(self):
        rows = [event('TrustLoss', 1090, [1, 2, 1090000000, 1090000, 1161000000, 0, 0]),
                event('ValidityState', 1160, [2, 3, 2, 2, 2100000000, 1160000000, 3], [3, 1, 0, 0]),
                event('ValidAdmission', 1180, [2, 3, 1180000000, 1250000000, 4, 0, 0], [1, 0, 0, 0])]
        result = validity.compute(rows, 0, 2000000000, True)
        episode = result['recovery_episodes'][0]
        self.assertEqual((episode['to_verification_ms'], episode['to_admission_ms']), (70, 90))
        self.assertTrue(episode['complete'])
        result = validity.compute(rows[:1], 0, 2000000000, True)
        self.assertEqual(result['incomplete_episodes'], 1)
        self.assertIsNone(result['recovery_episodes'][0]['to_admission_ms'])
        self.assertEqual(result['performance_criteria'], 'unset')

    def test_zero_availability_and_historical(self):
        rows = [event('UpdateEnd', 100, flags=256, update_id=1),
                event('ObservationBbox', 100, flags=0, update_id=1),
                event('EvidenceClock', 100, [1, 1, 100000000, 100000001, 1, 1, 1], [1, 0, 0, 0])]
        result = validity.compute(rows, 0, 200000000, True)
        self.assertEqual(result['frame_output']['valid_ratio'], 0)
        self.assertEqual(result['frame_output']['sample_count'], 1)
        self.assertEqual(result['performance_criteria'], 'unset')
        historical = validity.compute([dict(rows[0], version=2)], 0, 200000000, True)
        self.assertFalse(historical['available'])
        self.assertIsNone(historical['frame_output']['valid_ratio'])
        self.assertIn('age_observation_only', historical['historical_meaning'])

    def test_stale_seed_is_distinct_from_verification(self):
        row = event('CandidateMatch', 120, [1, 1, 0, 0, 120000000, 0, 1], [0,0,1,0], flags=131072)
        parsed = validity.fields(row)
        self.assertEqual(parsed['disposition'], 'stale_seed')
        self.assertFalse(parsed['fresh'])
        self.assertEqual(parsed['counter_before'], parsed['counter_after'])
        row['flags'] = 0
        self.assertEqual(validity.fields(row)['disposition'], 'verification_or_diagnostic')

    def test_mapping_and_exact_integer_contract(self):
        self.assertFalse(validity.compatible('capture-monotonic-v1', 'standalone-admission-v1'))
        self.assertTrue(validity.compatible('capture-monotonic-v1', 'capture-monotonic-v1'))
        with self.assertRaises(ValueError):
            validity.fields(event('EvidenceClock', 1, [1, 1.5, 2, 3, 4, 5, 6]))
        raw = bytearray(256)
        raw[0:2] = (4).to_bytes(2, 'little')
        with self.assertRaises(ValueError):
            collect.decode_event(raw)

    def test_invalid_suppressed_absent_and_event_pairs(self):
        rows = [event('EvidenceClock', 1, [2, 1, 2, 3, 4, 1, 1], [1, 0, 0, 0])]
        for update in range(1, 5):
            rows.append(event('UpdateEnd', update, flags=256, update_id=update))
        rows.extend([event('ObservationBbox', 1, flags=16, update_id=1),
                     event('ObservationBbox', 2, flags=0, update_id=2),
                     event('ObservationSuppressed', 3, flags=0, update_id=3),
                     event('ObservationBbox', 4, flags=32768, update_id=4)])
        output = validity.compute(rows, 0, 10000000, True)['frame_output']
        self.assertEqual([output[key] for key in ('valid','invalid','suppressed','absent')], [1,1,1,1])
        self.assertEqual(output['valid_ratio'], 0.25)

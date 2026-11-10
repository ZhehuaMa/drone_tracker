"""Double observation payloads, independent topic outcomes and legacy evidence."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import collect
import metrics
from test_metrics import event, CLOCK, publication


def paired_events(control=False, sequence=1, update_id=1):
    """Mirror the production writer's six records per accepted observation."""
    flags = 32768 if control else 0
    update_id = 0 if control else update_id
    fields = dict(version=2, update_id=update_id, values=[sequence, 1000000001],
                  producer_id=3 if control else 0, producer_seq=0, frame_time_us=0, reason_id=0)
    rows = [] if control else [event('Output', 9.5, **fields)]
    rows += [event('ObservationBbox', 9.7, **fields, stamp_ns=1000000001,
                   flags=flags | (0 if control else 16),
                   observation_values=[0., 0., 0., 0.] if control else [768.5, 480.25, 20.5, 30.25]),
             event('ObservationLos', 9.8, **fields, flags=flags, stamp_ns=1000000001,
                   observation_values=[0., 0., 0., 0.])]
    for prefix, milliseconds in ((('ControlBbox' if control else 'Publish'), 10),
                                  (('ControlLos' if control else 'LosPublish'), 10.2)):
        rows += [event(prefix + 'Start', milliseconds - .1, **fields, flags=flags),
                 event(prefix + 'End', milliseconds, **fields, flags=flags | 128)]
    return rows


class ObservationEvidenceTests(unittest.TestCase):
    def test_double_payload_and_version_support(self):
        values = [768.5, 480.25, 20.5, 30.25]
        packed = struct.unpack('<4Q', struct.pack('<4d', *values))
        scalars = [2, 256, metrics.TYPE_NAMES.index('ObservationBbox'), 0, 16, 4,
                   7, 1, 1, 123, 0, 0, 0, 0, 3, 3, 0, 0, 0, 100, 200, 0]
        raw = collect.EVENT.pack(*scalars, *([0.0] * 12), 11, 1000000001, *packed, 0, *([0] * 4))
        decoded = collect.decode_event(raw)
        self.assertEqual(decoded['observation_values'], values)
        self.assertEqual(decoded['stamp_ns'], 1000000001)
        self.assertEqual(decoded['observation_id'], 11)
        with self.assertRaisesRegex(ValueError, 'unsupported version'):
            collect.decode_event(struct.pack('<H', 99) + raw[2:])

    def test_two_topic_calls_and_off_do_not_double_frame_fps(self):
        rows = paired_events()
        rows += [event('ObservationSuppressed', 11, version=2, update_id=2, reason='RepeatedTime')]
        rows += paired_events(control=True, sequence=2)
        result = metrics.compute(rows, [], CLOCK)
        self.assertTrue(result['record_complete'], result['issues'])
        full = result['windows']['full']
        self.assertEqual(full['publication_count']['value'], 1)
        self.assertEqual(full['valid_publication_ratio']['value'], 1)
        self.assertEqual(full['los_publication_count']['value'], 1)
        self.assertEqual(full['los_valid_count']['value'], 0)
        self.assertEqual(full['observation_suppressed_count']['value'], 1)
        self.assertEqual(full['off_notification_count']['value'], 1)
        self.assertEqual(full['published_float_box']['samples'][0]['box'], [768.5, 480.25, 20.5, 30.25])

    def test_missing_whole_topic_or_payload_prevents_complete_metrics_and_qualification(self):
        for control in (False, True):
            for missing in ({'ObservationLos'},
                            {'ControlLosStart', 'ControlLosEnd'} if control else {'LosPublishStart', 'LosPublishEnd'},
                            {'ObservationBbox', 'ObservationLos'}):
                with self.subTest(control=control, missing=missing):
                    rows = [row for row in paired_events(control) if row['type'] not in missing]
                    result = metrics.compute(rows, [], CLOCK)
                    self.assertFalse(result['record_complete'])
                    self.assertIsNone(result['windows']['full']['los_valid_count']['value'])
                    reasons = collect.qualify(rows, [], CLOCK, {}, {'max_supply_lateness_us': 50000})
                    self.assertTrue(any('observation integrity' in reason for reason in reasons))

    def test_trust_loss_does_not_change_historical_off_count(self):
        rows = paired_events(control=True, sequence=1)
        loss = paired_events(control=True, sequence=2)
        for row in loss:
            row['version'] = 3
            row['flags'] |= 262144
            row['mono_ns'] += 1000000
            row['stamp_ns'] = 1001000001
            row['values'] = [2, 1001000001]
        result = metrics.compute(rows + loss, [], CLOCK)['windows']['full']
        self.assertEqual(result['off_notification_count']['value'], 1)
        self.assertEqual(result['trust_loss_notification_count']['value'], 1)
        historical = metrics.compute(rows, [], CLOCK)['windows']['full']
        self.assertIsNone(historical['trust_loss_notification_count']['value'])

    def test_duplicate_payload_timestamp_and_context_are_rejected(self):
        rows = paired_events()
        mutations = [rows + [dict(rows[1])]]
        for field, value in (('stamp_ns', 1000000002), ('update_id', 99), ('observation_values', None)):
            changed = [dict(row) for row in rows]
            changed[2][field] = value
            mutations.append(changed)
        for changed in mutations:
            with self.subTest(changed=changed):
                self.assertFalse(metrics.compute(changed, [], CLOCK)['record_complete'])

    def test_control_los_failure_retains_separate_outcomes(self):
        rows = paired_events(control=True)
        rows[-1].update(reason='PublishException', flags=32768 | 1024)
        result = metrics.compute(rows, [], CLOCK)
        self.assertFalse(result['record_complete'])
        self.assertTrue(any(issue['reason'] == 'control_publication_error' for issue in result['issues']))
        self.assertTrue(rows[-3]['flags'] & 128)  # The bbox call still succeeded.

    def test_legacy_has_no_float_or_los_evidence(self):
        full = metrics.compute(publication(10, 1), [], CLOCK)['windows']['full']
        self.assertEqual(full['published_float_box']['availability'], 'unavailable')
        self.assertEqual(full['los_publication_count']['availability'], 'unavailable')

    def test_los_failure_is_independent(self):
        rows = publication(10, 1)
        rows += [event('LosPublishStart', 10.1, update_id=1),
                 event('LosPublishEnd', 10.2, update_id=1, reason='PublishException', flags=1024)]
        # Preserve bbox statistics while surfacing the independent LOS failure.
        result = metrics.compute(rows, [], CLOCK)
        self.assertTrue(any(issue['reason'] == 'los_publication_error' for issue in result['issues']))


if __name__ == '__main__':
    unittest.main()

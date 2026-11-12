"""One-call evidence keeps job, plan, seed and historic watchdog meanings separate."""
import unittest
import scheduler_metrics


def event(kind, values=None, counts=None, version=4, reason='None'):
    return dict(version=version, type=kind, mono_ns=100, run_id=1, round_id=2,
                source_tracker_frame_id=32, reason=reason,
                values=values or [0] * 7, counts=counts or [0] * 4)


class SchedulerMetricsTest(unittest.TestCase):
    def test_original_and_seed_attempts_and_one_disposition(self):
        rows = [event('StageDisposition', [9, 4, 0, 1, 2, 1, 7], [1, 0, 1, 0]),
                event('CleanupPolicy', [1, 0, 1000, 100, 1, 2, 1]),
                event('DetectStart', [9, 4, 0, 1, 7, 0, 0]),
                event('DetectEnd', [9, 4, 0, 1, 7, 0, 0]),
                event('PlanOutcome', [4, 2, 32, 9, 0, 0, 0])]
        result = scheduler_metrics.compute(rows, 0, 200)
        self.assertEqual(result['executed_jobs'], 1)
        self.assertEqual(result['completed_jobs'], 1)
        self.assertEqual(result['parent_verdicts'], 1)
        self.assertEqual(result['no_result_frame_cleanup'], 'inactive')
        self.assertEqual(result['dispositions'][0]['request_attempt'], 1)
        self.assertEqual(result['dispositions'][0]['seed_attempt'], 2)
        self.assertFalse(result['dispositions'][0]['confirmation_eligible'])
        self.assertEqual(result['duplicate_dispositions'], [])
        rows.append(rows[0])
        self.assertEqual(len(scheduler_metrics.compute(rows, 0, 200)['duplicate_dispositions']), 1)

    def test_historic_timeout_is_not_candidate_expiry(self):
        result = scheduler_metrics.compute([event('Decision', version=3, reason='NoResultTimeout')], 0, 200)
        self.assertFalse(result['available'])
        self.assertEqual(result['historical_watchdog_events'], 1)
        self.assertEqual(result['no_result_frame_cleanup'], 'unavailable')
        self.assertIsNone(result['executed_jobs'])
        self.assertEqual(result['dispositions'], [])

    def test_missing_components_remain_unavailable(self):
        result = scheduler_metrics.compute([event('SchedulerTiming', [9, 4, 120, 300, 0, 0, 0])], 0, 200)
        self.assertEqual(result['timing'][0]['preprocessing_ns'], 120)
        self.assertIsNone(result['timing'][0]['postprocessing_ns'])
        self.assertIsNone(result['timing'][0]['capacity_wait_ns'])


if __name__ == '__main__':
    unittest.main()

"""Exact synthetic evidence checks, without models or target hardware."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import metrics

CLOCK = {"m0_ns": 1_000_000_000, "e0_us": 1_000_000}


def event(kind, milliseconds, **fields):
    return {"type": kind, "reason": "None", "mono_ns": CLOCK["m0_ns"] + round(milliseconds * 1e6),
            "flags": 0, "run_id": 1, "round_id": 1, **fields}


def publication(milliseconds, update_id, valid=True, **fields):
    fields = {"flags": 128, **fields}
    return [event("Output", milliseconds - .2, update_id=update_id,
                  flags=32 if valid else 0, values=[2, int(valid)],
                  box=[900, 800, 99, 98], other_box=[10, 20, 30, 40] if valid else [0, 0, 0, 0]),
            event("PublishStart", milliseconds - .1, update_id=update_id),
            event("PublishEnd", milliseconds, update_id=update_id, **fields)]


class MetricsTests(unittest.TestCase):
    def test_source_context_and_supersession_extensions(self):
        events = [event("RunStart", 0, values=[0,0,0,0,0,0,2]),
                  event("RequestDecision", 1, request_id=1, flags=128),
                  event("RequestDecision", 2, request_id=2, flags=128),
                  event("RequestDecision", 3, request_id=3, flags=128),
                  event("RequestCancel", 2.1, request_id=1, reason="Superseded", values=[2]),
                  event("RequestCancel", 3.1, request_id=2, reason="Superseded", values=[3]),
                  event("DetectStart", 500, request_id=3), event("DetectEnd", 573, request_id=3, flags=128|512),
                  event("SourceContext", 574, request_id=3, flags=16, iou=.584, values=[0]),
                  event("HistoryLookup", 574, request_id=3, reason="HistoryMiss")]
        result = metrics.compute(events, [], CLOCK)["windows"]["full"]
        self.assertEqual(result["request_superseded_count"]["value"], 2)
        self.assertEqual(result["request_executed_count"]["value"], 1)
        self.assertEqual(result["source_metadata_available_count"]["value"], 1)
        self.assertEqual(result["successful_result_history_miss_count"]["value"], 1)
        self.assertEqual(result["request_queue_wait_ms"]["statistics"]["max"], 497)
        old = metrics.compute(events[1:], [], CLOCK)["windows"]["full"]
        self.assertIsNone(old["source_metadata_available_count"]["value"])
        self.assertEqual(old["source_metadata_available_count"]["availability"], "unavailable")
        self.assertEqual(old["successful_result_history_miss_count"]["value"], 1)

    def test_quantiles_use_nearest_rank(self):
        stats = metrics.statistics(list(range(1, 21)))
        self.assertEqual(stats["mean"], 10.5)
        self.assertEqual(stats["p50"], 10)
        self.assertEqual(stats["p95"], 19)
        self.assertEqual(stats["p99"], 20)
        self.assertIsNone(metrics.statistics([]))

    def test_no_publication_is_gap_not_invalid(self):
        full = metrics.compute([], [], CLOCK)["windows"]["full"]
        self.assertEqual(full["max_no_publication_gap_ms"]["value"], 20000)
        self.assertEqual(full["max_invalid_span_ms"]["value"], 0)
        self.assertEqual(full["no_output_prefix_ms"]["value"], 20000)
        self.assertIsNone(full["valid_publication_ratio"]["value"])
        self.assertEqual(full["valid_publication_ratio"]["availability"], "no_event")
        self.assertEqual(full["catchup_step_count"]["value"], 0)
        self.assertIsNone(full["catchup_duration_ms"]["statistics"])
        self.assertEqual(full["catchup_duration_ms"]["reason"], "no_event")

    def test_unknown_record_does_not_infer_twenty_second_gap(self):
        full = metrics.compute([], [], CLOCK, complete=False)["windows"]["full"]
        self.assertIsNone(full["max_no_publication_gap_ms"]["value"])
        self.assertEqual(full["max_no_publication_gap_ms"]["availability"], "incomplete")

    def test_cross_window_invalid_span_and_unrecovered_clear(self):
        events = publication(13000, 1, False) + publication(14000, 2, False)
        events += [event("Decision", 13400, flags=1024, reason="LowIou", update_id=3)]
        events += publication(18000, 4, True)
        result = metrics.compute(events, [], CLOCK)
        core = result["windows"]["core"]
        self.assertEqual(core["max_invalid_span_ms"]["value"], 4000)
        interval = core["max_invalid_span_ms"]["intervals"][0]
        self.assertTrue(interval["left_truncated"])
        self.assertTrue(interval["right_truncated"])
        self.assertEqual(core["recovery_latency_ms"]["inherited"][0]["value_ms"], 4600)
        self.assertEqual(core["clear_count"]["value"], 0)
        full = result["windows"]["full"]
        self.assertEqual(full["recovery_latency_ms"]["statistics"]["max"], 4600)
        # A final unrecovered clear stays censored, with a lower bound rather than invented latency.
        events += [event("Decision", 19000, reason="NoResultTimeout", flags=1024, update_id=5)]
        full = metrics.compute(events, [], CLOCK)["windows"]["full"]
        self.assertEqual(full["unrecovered_count"]["value"], 1)
        censored = full["recovery_latency_ms"]["censored"][0]
        self.assertIsNone(censored["value_ms"])
        self.assertEqual(censored["wait_lower_bound_ms"], 1000)
        self.assertEqual(full["recovery_latency_ms"]["sample_count"], 1)

    def test_frame_age_and_prediction_are_distinct(self):
        row = {"fixture_frame_id": 0, "relative_us": 15_721,
               "planned_mono_ns": CLOCK["m0_ns"] + 15_721_000,
               "supplied_mono_ns": CLOCK["m0_ns"] + 15_721_000, "enqueued": True}
        events = publication(403.721, 1, flags=128 | 4 | 8,
                             frame_time_us=CLOCK["e0_us"] + 15_721,
                             prediction_us=CLOCK["e0_us"] + 100_000)
        full = metrics.compute(events, [row], CLOCK)["windows"]["full"]
        self.assertEqual(full["frame_age_at_publish_ms"]["statistics"]["mean"], 388)
        self.assertEqual(full["prediction_lag_at_publish_ms"]["statistics"]["mean"], 303.721)
        self.assertEqual(full["valid_publication_ratio"]["value"], 1)
        self.assertEqual(full["input_supply_lateness_ms"]["statistics"]["max"], 0)

    def test_slow_missing_end_is_not_deleted_from_quantiles(self):
        events = [event("UpdateStart", 10, update_id=1), event("UpdateEnd", 11, update_id=1),
                  event("UpdateStart", 20, update_id=2)]
        result = metrics.compute(events, [], CLOCK)
        self.assertFalse(result["record_complete"])
        duration = result["windows"]["full"]["update_duration_ms"]
        self.assertEqual(duration["sample_count"], 2)
        self.assertIsNone(duration["statistics"])
        self.assertIsNone(duration["samples"][1]["value_ms"])

    def test_update_start_selects_window_and_full_duration(self):
        events = [event("UpdateStart", 17499, update_id=1), event("UpdateEnd", 17887, update_id=1)]
        core = metrics.compute(events, [], CLOCK)["windows"]["core"]
        self.assertEqual(core["update_duration_ms"]["statistics"]["max"], 388)
        self.assertEqual(core["update_throughput_hz"]["value"], .25)

    def test_request_lifecycle_coverage_and_multistage_inference(self):
        events = [event("RequestDecision", 1, request_id=1, flags=128),
                  event("RequestDecision", 2, request_id=2, reason="UnreadResult"),
                  event("DetectStart", 3, request_id=1),
                  event("DetectStage", 4, request_id=1, values=[0, 0], flags=2048),
                  event("DetectStage", 5, request_id=1, values=[1, 1], flags=2048),
                  event("DetectStage", 6, request_id=1, values=[2, 2], reason="ImagePrepareFailed"),
                  event("DetectEnd", 7, request_id=1, flags=128 | 512),
                  event("ResultRead", 8, request_id=1),
                  event("HistoryLookup", 8.5, request_id=1, reason="HistoryMiss"),
                  event("ResultOverwrite", 9, request_id=1),
                  event("RequestDecision", 9.5, request_id=3, flags=128),
                  event("RequestCancel", 10, request_id=3)]
        full = metrics.compute(events, [], CLOCK)["windows"]["full"]
        self.assertEqual(full["request_accepted_count"]["value"], 2)
        self.assertEqual(full["request_rejected_count_by_reason"]["value"], {"UnreadResult": 1})
        self.assertEqual(full["request_executed_count"]["value"], 1)
        self.assertEqual(full["detector_inference_count"]["value"], 2)
        self.assertEqual(full["detector_stage_count"]["value"], 3)
        self.assertEqual(full["successful_result_history_miss_count"]["value"], 1)
        self.assertEqual(full["result_overwritten_count"]["value"], 1)
        self.assertEqual(full["request_cancelled_count"]["value"], 1)
        self.assertEqual(full["initialization_failure_count"]["value"], 0)

    def test_nested_initialization_catchup_and_no_double_sum(self):
        events = [event("UpdateStart", 1, update_id=1),
                  event("InitializeStart", 10, update_id=1),
                  event("InitializeEnd", 20, update_id=1, flags=128),
                  event("CatchupStep", 25, update_id=1, flags=128, duration_ns=5_000_000),
                  event("CatchupStep", 30, update_id=1, duration_ns=5_000_000),
                  event("CatchupEnd", 30, update_id=1, flags=128, counts=[2, 1, 1]),
                  event("UpdateEnd", 31, update_id=1)]
        full = metrics.compute(events, [], CLOCK)["windows"]["full"]
        self.assertEqual(full["update_duration_ms"]["statistics"]["max"], 30)
        self.assertEqual(full["initialization_duration_ms"]["statistics"]["max"], 10)
        self.assertEqual(full["catchup_duration_ms"]["statistics"]["max"], 10)
        self.assertEqual(full["recovery_update_duration_ms"]["statistics"]["max"], 30)
        self.assertEqual(full["catchup_step_failure_count"]["value"], 1)
        self.assertEqual(full["catchup_success_count"]["value"], 1)
        # The inner failure detail is not another attempt or an unpaired outer end.
        failed = [event("InitializeStart", 10, update_id=2),
                  event("InitializeEnd", 11, update_id=2, reason="InitFailed", values=[3, 2, 5], flags=4096),
                  event("InitializeEnd", 12, update_id=2, reason="InitFailed")]
        result = metrics.compute(failed, [], CLOCK)
        self.assertTrue(result["record_complete"])
        self.assertEqual(result["windows"]["full"]["initialization_failure_count"]["value"], 1)

    def test_initialization_without_followup_is_not_catchup(self):
        events = [event("InitializeStart", 1, update_id=1), event("InitializeEnd", 2, update_id=1, flags=128),
                  event("CatchupEnd", 2, update_id=1, flags=128, counts=[0, 0, 0])]
        full = metrics.compute(events, [], CLOCK)["windows"]["full"]
        self.assertEqual(full["catchup_attempt_count"]["value"], 0)
        self.assertEqual(full["catchup_duration_ms"]["availability"], "no_event")

    def test_only_critical_clear_counts_and_invalid_coordinates(self):
        events = [event("Decision", 1, reason="NoTarget", update_id=1),
                  event("Decision", 2, reason="NoTarget", update_id=2, flags=1024)]
        events += publication(3, 3, tracking=True, box=[1, 2, float("nan"), 4])
        full = metrics.compute(events, [], CLOCK)["windows"]["full"]
        self.assertEqual(full["clear_count_by_reason"]["value"], {"NoTarget": 1})
        self.assertEqual(full["valid_publication_ratio"]["value"], 0)

    def test_inspector_chinese_names_and_unknown_fields(self):
        event_data = event("调用开始", 1, update_id=1, reason="无")
        result = metrics.compute([event_data], [], CLOCK)
        self.assertFalse(result["record_complete"])
        with self.assertRaisesRegex(ValueError, "clock.m0_ns"):
            metrics.compute([], [], {"m0_ns": None, "e0_us": 1})

    def test_old_inference_trace_without_execution_flag_is_incomplete(self):
        events = [event("DetectStart", 1, request_id=1),
                  event("DetectStage", 2, request_id=1, values=[0, 0]),
                  event("DetectEnd", 3, request_id=1, flags=128)]
        result = metrics.compute(events, [], CLOCK)
        self.assertFalse(result["record_complete"])
        self.assertIsNone(result["windows"]["full"]["detector_inference_count"]["value"])
        self.assertEqual(result["issues"][0]["reason"], "inference_execution_evidence_missing")

    def test_cutoff_unsupplied_times_are_unknown(self):
        rows=[{'fixture_frame_id':0,'relative_us':15721,'supplied':False,'enqueued':False,
               'planned_mono_ns':0,'supplied_mono_ns':0}]
        descriptor=metrics.compute([],rows,CLOCK,complete=False)['windows']['full']['input_supply_lateness_ms']
        self.assertEqual(descriptor['samples'][0]['value_ms'],None)
        self.assertIsNone(descriptor['statistics'])
        self.assertEqual(descriptor.get('observed_statistics'),None)

    def test_detector_completion_uses_execution_start_window(self):
        events = [event("DetectStart", 17499, request_id=1),
                  event("DetectStage", 17880, request_id=1, values=[0, 0], flags=2048),
                  event("DetectEnd", 17887, request_id=1, flags=128)]
        core = metrics.compute(events, [], CLOCK)["windows"]["core"]
        self.assertEqual(core["request_completed_count"]["value"], 1)
        self.assertEqual(core["detector_inference_count"]["value"], 1)
        self.assertEqual(core["detector_duration_ms"]["statistics"]["max"], 388)


if __name__ == "__main__":
    unittest.main()

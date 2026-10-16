"""Freeze/compare tests preserve evidence, slow rounds and null events."""

import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import baseline
import compare


def descriptor(value, unit="ms", available=True):
    return {"metric_version": 1, "unit": unit, "window": {"start_us": 0, "end_us": 20_000_000},
            "sample_count": 1 if available else 0, "availability": "available" if available else "no_event",
            "reason": None if available else "no_event", "value": value}


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        self.group = {field: f"identity-{field}" for field in baseline.GROUP_FIELDS}
        self.group["identity_version"] = 2
        self.group["dependencies"] = {"identity_version": 2,
            "libraries": {"node": {"libstub.so": "a" * 64}, "worker": {"libc.so.6": "b" * 64}},
            "middleware": {"effective": "rmw_test", "libraries": {"librmw_test.so": "c" * 64}}}
        self.group["models"] = {"yolo": "a" * 64, "backbone": "b" * 64, "head": "c" * 64}
        self.group["board"] = {"product": "HZ-RK3576_WuKong Board", "serial": "production-1"}

    def run_fixture(self, name, gap, count=0, ratio=.8, duration=1, group=None):
        path = self.root / name
        path.mkdir()
        identity = {"schema_version": 1, "case_id": baseline.CASE_ID, "mode": "realtime", "duration_us": 20_000_000,
                    "clock": {"m0_ns": 1000, "e0_us": 1000},
                    "run_id": name, "comparison_group": group or self.group,
                    "code": {"source_sha256": baseline.canonical_digest("candidate" if name.startswith("candidate") else "reference"),
                             "binary_sha256": "e" * 64}}
        (path / "identity.json").write_text(json.dumps(identity), encoding="utf-8")
        for file in ("input.jsonl", "events.jsonl", "events.bin", "frames.csv", "messages.csv"):
            (path / file).write_text("synthetic test evidence " + name, encoding="utf-8")
        metrics = {"schema_version": 1, "metric_version": 1, "record_complete": True, "issues": [], "windows": {}}
        for window in ("full", "core"):
            metrics["windows"][window] = {
                "max_no_publication_gap_ms": descriptor(gap),
                "result_overwritten_count": descriptor(count, "count"),
                "valid_publication_ratio": descriptor(ratio, "ratio"),
                "catchup_duration_ms": {**descriptor(None, available=duration is not None),
                                        "statistics": {key: duration for key in ("mean", "min", "max", "p50", "p95", "p99")}
                                        if duration is not None else None},
                "clear_count_by_reason": descriptor({"LowIou": count} if count else {}, "count"),
            }
            for metric in metrics["windows"][window].values():
                metric["window"] = {"start_us": 0 if window == "full" else 13_500_000,
                                    "end_us": 20_000_000 if window == "full" else 17_500_000}
        (path / "metrics.json").write_text(json.dumps(metrics), encoding="utf-8")
        self.rehash(path)
        return path

    def rehash(self, path):
        files = {file.name: baseline.sha256(file) for file in path.iterdir() if file.name != "completion.json"}
        (path / "completion.json").write_text(json.dumps({"schema_version": 1, "normal_end": True,
                                                        "qualified": True, "reasons": [], "files": files}), encoding="utf-8")

    def frozen(self, values=(380, 400, 420), **fields):
        refs = [self.run_fixture(f"reference-{index}", value, **fields) for index, value in enumerate(values)]
        output = self.root / "baseline"
        baseline.freeze(refs, output)
        return refs, output

    def candidates(self, values=(240, 250, 270), **fields):
        return [self.run_fixture(f"candidate-{index}", value, **fields) for index, value in enumerate(values)]

    def test_historical_extension_absence_keeps_old_metrics_comparable(self):
        refs = [self.run_fixture(f"reference-{i}", 10) for i in range(3)]
        candidates = self.candidates((5, 5, 5))
        for path in refs + candidates:
            data = baseline.read_json(path / "metrics.json")
            for values in data["windows"].values():
                values["request_superseded_count"] = {
                    **descriptor(None if path in refs else 4, "count"),
                    "metric_version": 2, "window": values["max_no_publication_gap_ms"]["window"],
                    "availability": "unavailable" if path in refs else "available",
                    "reason": "historical_field_absent" if path in refs else None}
                values["source_capture_to_consumption_ms"] = {
                    **descriptor(None), "metric_version": 2,
                    "window": values["max_no_publication_gap_ms"]["window"],
                    "availability": "incomplete", "reason": "unknown_sample", "statistics": None}
            (path / "metrics.json").write_text(json.dumps(data))
            self.rehash(path)
        frozen = self.root / "extended-baseline"
        baseline.freeze(refs, frozen)
        report = compare.compare(frozen, candidates)
        self.assertEqual(report["status"], "comparable")
        rows = {row["metric"]: row for row in report["metrics"]}
        self.assertEqual(rows["full.max_no_publication_gap_ms"]["delta"], -5)
        self.assertIsNone(rows["full.request_superseded_count"]["delta"])
        self.assertIsNone(rows["full.request_superseded_count"]["baseline"]["median"])
        self.assertEqual(rows["full.request_superseded_count"]["candidate"]["median"], 4)
        self.assertEqual(rows["full.request_superseded_count"]["metric_version"], 2)
        data = baseline.read_json(candidates[0] / "metrics.json")
        data["windows"]["full"]["max_no_publication_gap_ms"]["availability"] = "incomplete"
        (candidates[0] / "metrics.json").write_text(json.dumps(data)); self.rehash(candidates[0])
        with self.assertRaisesRegex(ValueError, "incomplete"):
            baseline.load_run(candidates[0])

    def test_numeric_examples_and_even_median(self):
        result = compare.compare_values([380, 400, 420], [240, 250, 270])
        self.assertEqual(result["delta"], -150)
        self.assertEqual(result["relative_percent"], -37.5)
        self.assertIsNone(compare.compare_values([0, 0, 0], [2, 2, 2], "count")["relative_percent"])
        self.assertEqual(compare.compare_values([0, 0, 0], [0, 0, 0])["relative_percent"], 0)
        ratio = compare.compare_values([.8, .8, .8], [.9, .9, .9], "ratio")
        self.assertAlmostEqual(ratio["percentage_points"], 10)
        self.assertAlmostEqual(ratio["relative_percent"], 12.5)
        self.assertEqual(compare.compare_values([1, 2, 3, 100], [5, 6, 7, 8])["baseline_median"], 2.5)

    def test_frozen_baseline_unchanged_and_no_automatic_pass(self):
        refs, output = self.frozen()
        frozen_hash = baseline.sha256(output / "baseline.json")
        reference_hashes = [baseline.sha256(path / "metrics.json") for path in refs]
        candidates = self.candidates(count=2, ratio=.9)
        report = compare.compare(output, candidates)
        self.assertEqual(report["status"], "comparable")
        self.assertEqual(report["decision"]["status"], "not_set")
        rows = {row["metric"]: row for row in report["metrics"]}
        self.assertEqual(rows["full.max_no_publication_gap_ms"]["relative_percent"], -37.5)
        self.assertIsNone(rows["full.result_overwritten_count"]["relative_percent"])
        self.assertAlmostEqual(rows["full.valid_publication_ratio"]["percentage_points"], 10)
        self.assertEqual(rows["full.clear_count_by_reason.LowIou"]["baseline"]["values"], [0, 0, 0])
        destination = self.root / "comparison"
        compare.write_report(report, destination)
        self.assertIn("未设置判定规则", (destination / "report.md").read_text(encoding="utf-8"))
        self.assertTrue((destination / "comparison.json").exists())
        self.assertTrue((destination / "comparison.csv").exists())
        self.assertEqual(baseline.sha256(output / "baseline.json"), frozen_hash)
        self.assertEqual([baseline.sha256(path / "metrics.json") for path in refs], reference_hashes)
        with self.assertRaises(FileExistsError):
            baseline.freeze(candidates, output)
        with self.assertRaises(FileExistsError):
            compare.write_report(report, destination)

    def test_at_least_three_and_duplicate_processes(self):
        two = [self.run_fixture("a", 1), self.run_fixture("b", 2)]
        with self.assertRaisesRegex(ValueError, "at least three"):
            baseline.freeze(two, self.root / "bad-baseline")
        with self.assertRaisesRegex(ValueError, "duplicate run"):
            baseline.freeze(two + [two[0]], self.root / "bad-baseline")
        self.assertFalse((self.root / "bad-baseline").exists())

    def test_reference_runs_cannot_stand_in_for_new_candidate_processes(self):
        refs, output = self.frozen()
        report = compare.compare(output, refs)
        self.assertEqual(report["status"], "incomparable")
        self.assertTrue(any(d["reason"] == "reference_run_reused_as_candidate" for d in report["differences"]))

    def test_frozen_evidence_tamper_is_incomparable(self):
        refs, output = self.frozen()
        candidates = self.candidates()
        (refs[1] / "events.bin").write_text("altered", encoding="utf-8")
        report = compare.compare(output, candidates)
        self.assertEqual(report["status"], "incomparable")
        self.assertIn("events.bin", report["differences"][0]["detail"])
        self.assertEqual(report["metrics"], [])

    def test_mixed_builds_within_candidate_set_are_incomparable(self):
        _, output = self.frozen()
        candidates = self.candidates()
        data = baseline.read_json(candidates[1] / "identity.json")
        data["code"]["binary_sha256"] = "9" * 64
        (candidates[1] / "identity.json").write_text(json.dumps(data), encoding="utf-8")
        self.rehash(candidates[1])
        report = compare.compare(output, candidates)
        self.assertEqual(report["status"], "incomparable")
        self.assertTrue(any(d["field"].endswith("code.binary_sha256") for d in report["differences"]))

    def test_project_library_code_changes_allowed_between_cohorts_not_within(self):
        _, output = self.frozen()
        candidates = self.candidates()
        for path in candidates:
            data = baseline.read_json(path / "identity.json")
            data["code"]["project_libraries"] = {"node": {"libhybrid_tracker.so": "a" * 64}}
            (path / "identity.json").write_text(json.dumps(data), encoding="utf-8")
            self.rehash(path)
        self.assertEqual(compare.compare(output, candidates)["status"], "comparable")
        data = baseline.read_json(candidates[1] / "identity.json")
        data["code"]["project_libraries"]["node"]["libhybrid_tracker.so"] = "b" * 64
        (candidates[1] / "identity.json").write_text(json.dumps(data), encoding="utf-8")
        self.rehash(candidates[1])
        report = compare.compare(output, candidates)
        self.assertEqual(report["status"], "incomparable")
        self.assertTrue(any(row["field"].endswith("code.project_libraries") for row in report["differences"]))

    def test_group_difference_names_precise_model_board_and_configuration(self):
        _, output = self.frozen()
        group = copy.deepcopy(self.group)
        group["models"]["yolo"] = "f" * 64
        group["board"]["serial"] = "production-2"
        group["algorithm"] = "different"
        report = compare.compare(output, self.candidates(group=group))
        self.assertEqual(report["status"], "incomparable")
        fields = [difference["field"] for difference in report["differences"]]
        self.assertTrue(any(field.endswith("models.yolo") for field in fields))
        self.assertTrue(any(field.endswith("board.serial") for field in fields))
        self.assertTrue(any(field.endswith("algorithm") for field in fields))
        self.assertEqual(report["decision"]["status"], "not_evaluated")

    def test_legacy_dependency_identity_cannot_be_frozen(self):
        path = self.run_fixture("legacy", 1)
        data = baseline.read_json(path / "identity.json")
        del data["comparison_group"]["identity_version"]
        (path / "identity.json").write_text(json.dumps(data), encoding="utf-8")
        self.rehash(path)
        with self.assertRaisesRegex(ValueError, "identity_version"):
            baseline.load_run(path)
        data["comparison_group"]["identity_version"] = 2
        data["comparison_group"]["dependencies"] = {"ros": "humble", "libraries": {"system": "a" * 64}}
        (path / "identity.json").write_text(json.dumps(data), encoding="utf-8")
        self.rehash(path)
        with self.assertRaisesRegex(ValueError, "dependencies.identity_version"):
            baseline.load_run(path)

    def test_missing_identity_sequential_mode_and_unqualified_rejected(self):
        path = self.run_fixture("bad", 1)
        identity = baseline.read_json(path / "identity.json")
        del identity["comparison_group"]["workload"]
        (path / "identity.json").write_text(json.dumps(identity), encoding="utf-8")
        self.rehash(path)
        with self.assertRaisesRegex(ValueError, "workload"):
            baseline.load_run(path)
        identity["comparison_group"]["workload"] = "fixed"
        identity["mode"] = "sequential"
        (path / "identity.json").write_text(json.dumps(identity), encoding="utf-8")
        self.rehash(path)
        with self.assertRaisesRegex(ValueError, "case_id/mode"):
            baseline.load_run(path)
        identity["mode"] = "realtime"
        (path / "identity.json").write_text(json.dumps(identity), encoding="utf-8")
        self.rehash(path)
        completion = baseline.read_json(path / "completion.json")
        completion["qualified"] = False
        completion["reasons"] = ["deadline_exceeded"]
        (path / "completion.json").write_text(json.dumps(completion), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "deadline_exceeded"):
            baseline.load_run(path)

    def test_slow_and_empty_rounds_never_dropped(self):
        _, output = self.frozen(values=(380, 400, 1000), duration=None)
        report = compare.compare(output, self.candidates(duration=2))
        rows = {row["metric"]: row for row in report["metrics"]}
        gap = rows["full.max_no_publication_gap_ms"]
        self.assertEqual(gap["baseline"]["values"], [380, 400, 1000])
        self.assertEqual(gap["baseline"]["max"], 1000)
        duration = rows["full.catchup_duration_ms.p95"]
        self.assertEqual(duration["baseline"]["availability"], "not_aggregated")
        self.assertEqual(duration["candidate"]["median"], 2)
        self.assertIsNone(duration["delta"])
        self.assertEqual(duration["availability_transition"]["baseline"], ["no_event"] * 3)

    def test_explicit_threshold_file_frozen_in_report(self):
        _, output = self.frozen()
        candidates = self.candidates(ratio=.9)
        rule = {"schema_version": 1, "threshold_version": "limits-1", "rules": [
            {"metric": "full.max_no_publication_gap_ms", "direction": "lower", "required": True,
             "allowed_change": {"type": "relative_percent", "value": 5}},
            {"metric": "full.valid_publication_ratio", "direction": "higher", "required": True,
             "allowed_change": {"type": "percentage_points", "value": 0}},
        ]}
        path = self.root / "thresholds.json"
        path.write_text(json.dumps(rule), encoding="utf-8")
        report = compare.compare(output, candidates, path)
        self.assertEqual(report["decision"]["status"], "passed")
        self.assertEqual(report["thresholds"]["sha256"], baseline.sha256(path))
        self.assertEqual(report["thresholds"]["content"], rule)
        self.assertEqual(report["decision"]["threshold_version"], "limits-1")

    def test_threshold_on_zero_baseline_cannot_fake_pass(self):
        _, output = self.frozen()
        path = self.root / "limits.json"
        path.write_text(json.dumps({"schema_version": 1, "threshold_version": "v1", "rules": [
            {"metric": "full.result_overwritten_count", "direction": "lower", "required": True,
             "allowed_change": {"type": "relative_percent", "value": 10}}]}), encoding="utf-8")
        report = compare.compare(output, self.candidates(count=2), path)
        self.assertEqual(report["decision"]["status"], "not_evaluated")

    def test_partial_no_event_round_disables_aggregate(self):
        _, output = self.frozen()
        candidates = self.candidates()
        data = baseline.read_json(candidates[1] / "metrics.json")
        data["windows"]["full"]["catchup_duration_ms"] = {
            **descriptor(None, available=False), "statistics": None}
        (candidates[1] / "metrics.json").write_text(json.dumps(data), encoding="utf-8")
        self.rehash(candidates[1])
        report = compare.compare(output, candidates)
        row = next(row for row in report["metrics"] if row["metric"] == "full.catchup_duration_ms.p99")
        self.assertEqual(row["candidate"]["values"], [1, None, 1])
        self.assertIsNone(row["candidate"]["median"])
        self.assertIsNone(row["delta"])


if __name__ == "__main__":
    unittest.main()

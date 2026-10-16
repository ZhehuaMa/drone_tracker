#!/usr/bin/env python3
"""Compare qualified candidate runs with an explicitly frozen baseline.

Default reports describe changes only. Performance decisions require an explicit,
versioned threshold file. This command never updates a baseline or source run.
"""

import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import sys

try:
    from . import baseline
except ImportError:
    import baseline


def _finite_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _flatten(metrics):
    rows = {}
    for window, descriptors in metrics["windows"].items():
        for name, descriptor in descriptors.items():
            root = window + "." + name
            metadata = {field: descriptor[field] for field in
                        ("metric_version", "unit", "window", "availability", "reason", "sample_count")}
            if "statistics" in descriptor:
                stats = descriptor["statistics"] or {}
                for statistic in ("mean", "min", "max", "p50", "p95", "p99"):
                    rows[root + "." + statistic] = {**metadata, "value": stats.get(statistic)}
            elif isinstance(descriptor.get("value"), dict):
                # Store the parent so sparse reason dictionaries can supply genuine zeroes.
                rows[root] = {**metadata, "value": descriptor["value"], "reason_counts": True}
                for reason, value in descriptor["value"].items():
                    rows[root + "." + reason] = {**metadata, "value": value}
            else:
                rows[root] = {**metadata, "value": descriptor.get("value")}
    return rows


def _aggregate(samples):
    # A single missing/no-event round invalidates aggregation; never quietly omit it.
    numeric = all(sample["availability"] == "available" and _finite_number(sample["value"])
                  for sample in samples)
    values = [sample["value"] for sample in samples]
    return {"runs": samples, "values": values,
            "median": statistics.median(values) if numeric else None,
            "min": min(values) if numeric else None, "max": max(values) if numeric else None,
            "availability": "available" if numeric else "not_aggregated",
            "reason": None if numeric else "one_or_more_rounds_unavailable"}


def _sample(rows, key, index):
    found = rows.get(key)
    if found is None:
        parent = rows.get(key.rsplit(".", 1)[0])
        if parent and parent.get("reason_counts"):
            found = {**parent, "value": 0}
        else:
            return {"run_index": index, "value": None, "availability": "incomplete",
                    "reason": "metric_missing", "sample_count": 0}
    return {"run_index": index, **{field: found[field] for field in
                                   ("value", "availability", "reason", "sample_count")}}


def compare_values(base_values, candidate_values, unit="ms"):
    """Small numeric primitive exposed for tests and report generation."""
    before, after = statistics.median(base_values), statistics.median(candidate_values)
    delta = after - before
    percentage = 100 * delta / before if before != 0 else 0 if after == 0 else None
    return {"baseline_median": before, "candidate_median": after, "delta": delta,
            "relative_percent": percentage, "percentage_points": 100 * delta if unit == "ratio" else None,
            "change": "increase" if delta > 0 else "decrease" if delta < 0 else "unchanged",
            "reason": "zero_baseline_new_event" if before == 0 and after != 0 else None}


def load_thresholds(path):
    source = Path(path)
    data = baseline.read_json(source)
    if data.get("schema_version") != 1 or not data.get("threshold_version"):
        raise ValueError(f"{source}: schema_version=1 and nonempty threshold_version required")
    if not isinstance(data.get("rules"), list):
        raise ValueError(f"{source}: rules must be a list")
    seen = set()
    for index, rule in enumerate(data["rules"]):
        field = f"{source}:rules[{index}]"
        if not isinstance(rule, dict) or not isinstance(rule.get("metric"), str) or not rule["metric"]:
            raise ValueError(f"{field}.metric: nonempty metric path required")
        if rule["metric"] in seen:
            raise ValueError(f"{field}.metric: duplicate rule for {rule['metric']}")
        seen.add(rule["metric"])
        if rule.get("direction") not in ("lower", "higher"):
            raise ValueError(f"{field}.direction: expected lower or higher")
        if not isinstance(rule.get("required"), bool):
            raise ValueError(f"{field}.required: boolean required")
        allowed = rule.get("allowed_change", {})
        if (allowed.get("type") not in ("absolute", "relative_percent", "percentage_points")
                or not _finite_number(allowed.get("value")) or allowed["value"] < 0):
            raise ValueError(f"{field}.allowed_change: nonnegative value and explicit type required")
    return {"path": str(source.resolve()), "sha256": baseline.sha256(source), "content": data}


def _apply_thresholds(rows, thresholds):
    if thresholds is None or not thresholds["content"]["rules"]:
        return {"status": "not_set", "reason": "未设置判定规则", "rules": []}
    indexed = {row["metric"]: row for row in rows}
    results = []
    for rule in thresholds["content"]["rules"]:
        row = indexed.get(rule["metric"])
        kind = rule["allowed_change"]["type"]
        field = {"absolute": "delta", "relative_percent": "relative_percent",
                 "percentage_points": "percentage_points"}[kind]
        value = row.get(field) if row else None
        if value is None:
            results.append({**rule, "status": "not_evaluated", "value": None,
                            "reason": "metric_missing_or_unavailable"})
            continue
        deterioration = value if rule["direction"] == "lower" else -value
        passed = deterioration <= rule["allowed_change"]["value"]
        results.append({**rule, "status": "passed" if passed else "failed", "value": value,
                        "reason": None})
    status = "failed" if any(result["status"] == "failed" for result in results) else (
        "not_evaluated" if any(result["required"] and result["status"] == "not_evaluated"
                               for result in results) or not any(result["status"] == "passed" for result in results)
        else "passed")
    return {"status": status, "rules": results, "threshold_version": thresholds["content"]["threshold_version"],
            "threshold_sha256": thresholds["sha256"]}


def compare(baseline_path, candidate_paths, threshold_path=None):
    """Return incomparable evidence instead of issuing a performance conclusion."""
    result = {"schema_version": 1, "comparison_version": 1,
              "status": "incomparable", "differences": [], "metrics": [],
              "decision": {"status": "not_evaluated", "reason": "运行集合不可比"},
              "limitations": "没有人工目标标注；仅评价输出连续性与运行开销，不评价准确率。发布返回不证明订阅端收到。"}
    thresholds = load_thresholds(threshold_path) if threshold_path else None
    if thresholds:
        result["thresholds"] = thresholds
    try:
        manifest, reference = baseline.load_baseline(baseline_path)
    except (OSError, ValueError, KeyError, TypeError) as error:
        result["differences"].append({"field": "baseline", "reason": "invalid_frozen_baseline", "detail": str(error)})
        return result
    result["baseline_manifest_sha256"] = manifest["manifest_sha256"]
    candidates = []
    paths = list(candidate_paths)
    if len(paths) < 3:
        result["differences"].append({"field": "candidate.runs", "reason": "at_least_three_qualified_runs_required"})
    for index, path in enumerate(paths):
        try:
            run = baseline.load_run(path)
            candidates.append(run)
        except (OSError, ValueError, KeyError, TypeError) as error:
            result["differences"].append({"field": f"candidate.runs[{index}]", "reason": "run_not_qualified", "detail": str(error)})
    if len({run["path"] for run in candidates}) != len(candidates):
        result["differences"].append({"field": "candidate.runs", "reason": "duplicate_run_directory"})
    if len({run["checksums"]["identity.json"] for run in candidates}) != len(candidates):
        result["differences"].append({"field": "candidate.runs", "reason": "duplicate_process_identity"})
    reference_ids = {run["checksums"]["identity.json"] for run in reference}
    for index, run in enumerate(candidates):
        if run["checksums"]["identity.json"] in reference_ids:
            result["differences"].append({"field": f"candidate.runs[{index}]", "reason": "reference_run_reused_as_candidate"})
    for index, run in enumerate(candidates):
        result["differences"].extend(baseline.field_differences(
            manifest["comparison_group"], run["identity"]["comparison_group"], f"candidate.runs[{index}].comparison_group"))
        for field in ("source_sha256", "binary_sha256", "project_libraries"):
            if candidates[0]["identity"]["code"].get(field, {}) != run["identity"]["code"].get(field, {}):
                result["differences"].append({"field": f"candidate.runs[{index}].code.{field}",
                                              "reason": "mixed_builds_within_candidate_set"})
    result["reference_runs"] = [{"path": run["path"], "checksums": run["checksums"], "code": run["identity"]["code"]}
                                for run in reference]
    result["candidate_runs"] = [{"path": run["path"], "checksums": run["checksums"], "code": run["identity"]["code"]}
                                for run in candidates]
    if result["differences"]:
        return result
    before_rows = [_flatten(run["metrics"]) for run in reference]
    after_rows = [_flatten(run["metrics"]) for run in candidates]
    # Metric contracts are checked in addition to the declared group fingerprint.
    first_metrics = reference[0]["metrics"]["windows"]
    for side, runs in (("baseline", reference), ("candidate", candidates)):
        for index, run in enumerate(runs):
            current = run["metrics"]["windows"]
            for window in ("full", "core"):
                if current[window].keys() != first_metrics[window].keys():
                    result["differences"].append({"field": f"{side}.runs[{index}].metrics.{window}",
                                                  "reason": "metric_set_mismatch"})
                for name in current[window].keys() & first_metrics[window].keys():
                    for field in ("metric_version", "unit", "window"):
                        result["differences"].extend(baseline.field_differences(
                            first_metrics[window][name][field], current[window][name][field],
                            f"{side}.runs[{index}].metrics.{window}.{name}.{field}"))
    if result["differences"]:
        return result
    keys = set().union(*(rows.keys() for rows in before_rows + after_rows))
    for key in sorted(keys):
        descriptor = next(rows[key] for rows in before_rows + after_rows if key in rows)
        if descriptor.get("reason_counts"):
            continue
        before = _aggregate([_sample(rows, key, index) for index, rows in enumerate(before_rows)])
        after = _aggregate([_sample(rows, key, index) for index, rows in enumerate(after_rows)])
        row = {"metric": key, "metric_version": descriptor["metric_version"], "unit": descriptor["unit"], "baseline": before, "candidate": after,
               "delta": None, "relative_percent": None, "percentage_points": None,
               "change": "unavailable", "reason": "one_or_more_rounds_unavailable"}
        if before["median"] is not None and after["median"] is not None:
            numeric = compare_values(before["values"], after["values"], descriptor["unit"])
            row.update({field: value for field, value in numeric.items() if field not in ("baseline_median", "candidate_median")})
        row["availability_transition"] = {
            "baseline": [sample["availability"] for sample in before["runs"]],
            "candidate": [sample["availability"] for sample in after["runs"]]}
        result["metrics"].append(row)
    result["status"] = "comparable"
    result["comparison_group"] = manifest["comparison_group"]
    result["decision"] = _apply_thresholds(result["metrics"], thresholds)
    return result


def _format(value, signed=False):
    if value is None:
        return "null"
    return f"{value:+.6g}" if signed else f"{value:.6g}"


def _range(aggregate):
    return f"{_format(aggregate['median'])} [{_format(aggregate['min'])}, {_format(aggregate['max'])}]"


def markdown_report(report):
    lines = ["# 固定片段跟踪回归版本比较", "", report["limitations"], "",
             "可比状态：" + ("可比" if report["status"] == "comparable" else "不可比") + "。", ""]
    if report["status"] != "comparable":
        lines.extend(["运行完整性或身份检查失败；不输出性能通过结论。", ""])
        for difference in report["differences"]:
            lines.append(f"- `{difference['field']}`：{difference.get('detail', difference['reason'])}；"
                         f"参考={json.dumps(difference.get('baseline'), ensure_ascii=False)}，"
                         f"候选={json.dumps(difference.get('candidate'), ensure_ascii=False)}")
        return "\n".join(lines) + "\n"
    decision = report["decision"]
    statuses = {"not_set": "未设置判定规则", "passed": "显式门槛通过", "failed": "显式门槛未通过",
                "not_evaluated": "显式门槛无法完整判定"}
    lines.extend(["判定：" + statuses[decision["status"]] + "。", ""])
    for heading, field in (("参考运行（表内按此顺序）", "reference_runs"), ("候选运行（表内按此顺序）", "candidate_runs")):
        lines.extend([heading + "：", ""])
        for index, run in enumerate(report[field]):
            lines.append(f"- 第 {index + 1} 轮：`{run['path']}`；源码 SHA-256 `{run['code']['source_sha256']}`；"
                         f"二进制 SHA-256 `{run['code']['binary_sha256']}`。")
        lines.append("")
    lines.extend([
                  "差值为候选减参考。中位数旁的方括号为各轮最小值和最大值。缺失轮不被删除，",
                  "任一轮没有该耗时事件时不计算跨轮数值差。零基数的新增事件百分比为 null。", "",
                  "| 指标 | 单位 | 参考各轮 | 候选各轮 | 参考中位数 [范围] | 候选中位数 [范围] | 差值 | 相对变化 | 状态 |",
                  "|---|---|---|---|---|---|---|---|---|"])
    changes = {"increase": "增加", "decrease": "减少", "unchanged": "不变", "unavailable": "不可用"}
    for row in report["metrics"]:
        delta = (_format(row["percentage_points"], True) + " 个百分点" if row["unit"] == "ratio" else _format(row["delta"], True))
        relative = _format(row["relative_percent"], True) + "%" if row["relative_percent"] is not None else "null（零基数或不可用）"
        def rounds(aggregate):
            return "; ".join(_format(sample["value"]) if _finite_number(sample["value"]) else
                             f"null ({sample['availability']}:{sample['reason']})" for sample in aggregate["runs"])
        lines.append(f"| `{row['metric']}` | {row['unit']} | {rounds(row['baseline'])} | {rounds(row['candidate'])} | "
                     f"{_range(row['baseline'])} | {_range(row['candidate'])} | {delta} | {relative} | {changes[row['change']]} |")
    if decision.get("rules"):
        lines.extend(["", "## 显式门槛", "",
                      f"版本：`{decision['threshold_version']}`；文件 SHA-256：`{decision['threshold_sha256']}`。", ""])
        for rule in decision["rules"]:
            lines.append(f"- `{rule['metric']}`：方向 `{rule['direction']}`，允许变化 "
                         f"`{rule['allowed_change']}`，必需 `{rule['required']}`，结果 `{rule['status']}`。")
    return "\n".join(lines) + "\n"


def write_report(report, output):
    destination = Path(output)
    destination.mkdir(parents=True, exist_ok=False)
    with (destination / "comparison.json").open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, ensure_ascii=False, allow_nan=False)
        stream.write("\n")
    (destination / "report.md").write_text(markdown_report(report), encoding="utf-8")
    fields = ["metric", "unit", "baseline_rounds", "candidate_rounds", "baseline_median", "baseline_min", "baseline_max",
              "candidate_median", "candidate_min", "candidate_max", "delta", "relative_percent", "percentage_points", "change", "reason",
              "comparison_status", "identity_field", "baseline_identity", "candidate_identity", "detail"]
    with (destination / "comparison.csv").open("x", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for difference in report["differences"]:
            writer.writerow({"comparison_status": report["status"], "identity_field": difference["field"],
                             "baseline_identity": json.dumps(difference.get("baseline"), ensure_ascii=False),
                             "candidate_identity": json.dumps(difference.get("candidate"), ensure_ascii=False),
                             "reason": difference["reason"], "detail": difference.get("detail")})
        for row in report["metrics"]:
            data = {field: row.get(field) for field in ("metric", "unit", "delta", "relative_percent", "percentage_points", "change", "reason")}
            data["comparison_status"] = report["status"]
            for side in ("baseline", "candidate"):
                data[side + "_rounds"] = json.dumps(row[side]["runs"], ensure_ascii=False, allow_nan=False)
                for field in ("median", "min", "max"):
                    data[side + "_" + field] = row[side][field]
            writer.writerow(data)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidates", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--thresholds", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.output.exists():
            raise FileExistsError(f"comparison output already exists: {args.output}")
        report = compare(args.baseline, args.candidates, args.thresholds)
        write_report(report, args.output)
        print(args.output / "report.md")
        return 2 if report["status"] == "incomparable" else 1 if report["decision"]["status"] in ("failed", "not_evaluated") else 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"compare: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

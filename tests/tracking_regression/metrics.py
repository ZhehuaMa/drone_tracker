#!/usr/bin/env python3
"""Recompute versioned behavior metrics from complete diagnostic events.

No metric measures target accuracy. Event association uses exact identities.
"""

import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import sys
import validity_metrics

SCHEMA_VERSION = 1
METRIC_VERSION = 1
ARBITRATION_METRIC_VERSION = 2
WINDOWS = {"full": (0, 20_000_000), "core": (13_500_000, 17_500_000)}
TYPE_NAMES = (
    "RunStart", "RunEnd", "RoundStart", "RoundEnd", "Capture", "FrameSkip",
    "FrameSelect", "UpdateStart", "UpdateEnd", "HistoryAdd", "HistoryEvict",
    "HistoryLookup", "RequestSkipped", "RequestDecision", "DetectStart",
    "DetectStage", "DetectEnd", "ResultRead", "ResultOverwrite", "RequestCancel",
    "Decision", "InitializeStart", "InitializeEnd", "CatchupStep", "CatchupEnd",
    "Output", "PublishStart", "PublishEnd", "PublishSkipped", "Timing", "Gap",
    "AbnormalEnd", "ClockRelation", "Health", "JumpComparison", "SourceContext",
    "ObservationBbox", "ObservationLos", "LosPublishStart", "LosPublishEnd", "ObservationSuppressed",
    "ControlBboxStart", "ControlBboxEnd", "ControlLosStart", "ControlLosEnd",
    "ValidityState", "EvidenceClock", "CandidateMatch", "TrustLoss", "TrustLossDisposed", "ValidAdmission",
)
TYPE_ZH = (
    "运行开始", "运行结束", "轮次开始", "轮次结束", "采集", "节点跳过",
    "节点选择", "调用开始", "调用结束", "缓存追加", "缓存淘汰", "缓存查找",
    "未投递", "请求决策", "检测开始", "检测阶段", "检测结束", "结果读取",
    "结果覆盖", "请求取消", "仲裁决策", "初始化开始", "初始化结束", "补跟踪步骤",
    "补跟踪汇总", "输出", "发布开始", "发布结束", "跳过发布", "阶段耗时",
    "诊断缺口", "异常结束", "时钟对应", "健康", "跳变比较", "SourceContext",
    "ObservationBbox", "ObservationLos", "LosPublishStart", "LosPublishEnd", "ObservationSuppressed",
    "ControlBboxStart", "ControlBboxEnd", "ControlLosStart", "ControlLosEnd",
    "ValidityState", "EvidenceClock", "CandidateMatch", "TrustLoss", "TrustLossDisposed", "ValidAdmission",
)
REASON_NAMES = (
    "None", "Unknown", "InvalidInput", "RepeatedTime", "Stopped", "Cleaning",
    "PendingOccupied", "UnreadResult", "BusyPending", "Interval", "Warmup",
    "Termination", "DriverGap", "QueueFull", "LatestSkip", "NoTarget", "LowIou",
    "YoloJump", "NanoJump", "NoResultTimeout", "HistoryMiss", "InitFailed",
    "ImagePrepareFailed", "InferenceException", "PublishException", "NotConsumed",
    "BufferFull", "StorageError", "SyncError", "SpaceProtection", "Incomplete",
    "NanoUpdateFailed", "Superseded", "ContextUnavailable", "RecoveryUnavailable", "PreviousRound",
)
REASON_ZH = (
    "无", "未知", "输入无效", "时间重复或倒退", "已停止", "正在清理", "待检测槽占用",
    "未读结果", "待检测槽忙", "未到检测间隔", "预热丢弃", "终止丢弃", "驱动序号缺口",
    "队列满", "取最新帧跳过", "无目标", "低IoU", "检测跳变", "跟踪跳变",
    "没有新结果超限", "缓存未命中", "初始化失败", "图像准备失败", "推理异常",
    "发布异常", "输入未消费", "记录缓冲满", "写入失败", "同步失败", "空间保护",
    "不完整", "跟踪更新失败", "Superseded", "ContextUnavailable", "RecoveryUnavailable", "PreviousRound",
)
CLEAR_REASONS = {"NoTarget", "LowIou", "YoloJump", "NanoJump", "NoResultTimeout", "NanoUpdateFailed"}


def _name(event, field, names, translated):
    value = event.get(field + "_id", event.get(field))
    if isinstance(value, int) and not isinstance(value, bool) and 0 <= value < len(names):
        return names[value]
    if value in names:
        return value
    if value in translated:
        return names[translated.index(value)]
    raise ValueError(f"event.{field}: unknown value {value!r}")


def event_type(event):
    return _name(event, "type", TYPE_NAMES, TYPE_ZH)


def event_reason(event):
    return _name(event, "reason", REASON_NAMES, REASON_ZH)


def _number(value, field):
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{field}: expected integer, got {value!r}")
    return value


def _array(event, field, index, default=0):
    values = event.get(field, [])
    return values[index] if len(values) > index else default


def identity(event, field="update_id"):
    return (event.get("run_id", 0), event.get("round_id", 0), event.get(field, 0))


def evidence(event):
    """Keep event and causal identities beside every sample."""
    return {key: event.get(key) for key in (
        "run_id", "round_id", "producer_id", "producer_seq", "update_id", "request_id",
        "tracker_frame_id", "source_update_id", "source_tracker_frame_id", "fixture_frame_id",
        "mono_ns", "frame_time_us", "prediction_us",
    )}


def statistics(values):
    """Nearest-rank quantiles (ceil(p*N), one-based); no empty zeroes."""
    if not values:
        return None
    ordered = sorted(values)
    return {
        "sample_count": len(ordered), "mean": math.fsum(ordered) / len(ordered),
        "min": ordered[0], "max": ordered[-1],
        **{name: ordered[math.ceil(p * len(ordered)) - 1]
           for name, p in (("p50", .50), ("p95", .95), ("p99", .99))},
    }


def _descriptor(window, unit, count, complete, reason=None, **contents):
    result = {"metric_version": METRIC_VERSION, "unit": unit, "window": window,
              "sample_count": count, "availability": "available", "reason": None}
    if not complete:
        result.update(availability="incomplete", reason=reason or "record_incomplete")
        for key in ("value", "statistics"):
            if key in contents:
                contents["observed_" + key] = contents[key]
                contents[key] = None
    elif reason:
        result.update(availability="no_event" if reason == "no_event" else "incomplete", reason=reason)
    result.update(contents)
    return result


def _duration(window, samples, complete, reason=None):
    values = [sample["value_ms"] for sample in samples if sample.get("value_ms") is not None]
    unknown = len(values) != len(samples)
    return _descriptor(window, "ms", len(samples), complete and not unknown,
                       reason or ("unknown_sample" if unknown else "no_event" if not samples else None),
                       statistics=statistics(values), samples=samples)


def _pair(events, start_type, end_type, field, issues, detail_filter=None):
    starts, ends = {}, {}
    for event in events:
        kind = event["_type"]
        key = identity(event, field)
        if kind == start_type:
            if not key[-1] or key in starts:
                issues.append({"reason": "ambiguous_pair_identity", "type": start_type, "identity": key})
            starts[key] = event
        elif kind == end_type and not (detail_filter and detail_filter(event)):
            if key in ends or not key[-1]:
                issues.append({"reason": "ambiguous_pair_identity", "type": end_type, "identity": key})
            ends[key] = event
    pairs = []
    for key in sorted(starts.keys() | ends.keys()):
        start, end = starts.get(key), ends.get(key)
        if not start or not end or end["mono_ns"] < start["mono_ns"]:
            issues.append({"reason": "unpaired_event", "type": start_type, "identity": key})
        pairs.append((start, end))
    return pairs


def _initialization_detail(event):
    # NanoTrack can emit a nested failure detail before the outer matching end.
    return bool(event.get("flags", 0) & 4096) or _array(event, "values", 0) > 0


def _is_clear(event):
    return (event["_type"] == "Decision" and event["_reason"] in CLEAR_REASONS
            and bool(event.get("flags", 0) & 1024))


def observation_issues(events):
    """Require both payloads and both topic calls for each format-2 observation.

    A missing topic is missing evidence, not a known invalid LOS sample. Legacy
    records do not contain this protocol and retain their established checks.
    """
    groups = defaultdict(lambda: defaultdict(list))
    payload_types = {"ObservationBbox", "ObservationLos"}
    frame_calls = {"PublishStart", "PublishEnd", "LosPublishStart", "LosPublishEnd"}
    control_calls = {"ControlBboxStart", "ControlBboxEnd", "ControlLosStart", "ControlLosEnd"}
    issues = []
    for event in events:
        kind = event_type(event)
        if event.get("version", 1) < 2 or kind not in payload_types | frame_calls | control_calls:
            continue
        key = (event.get("run_id", 0), event.get("round_id", 0), _array(event, "values", 0))
        if type(key[-1]) is not int or key[-1] <= 0:
            issues.append({"reason": "observation_identity_missing", "event": evidence(event)})
        groups[key][kind].append(event)
    for key, kinds in groups.items():
        control = bool(control_calls & kinds.keys()) or any(
            event.get("flags", 0) & 32768 for rows in kinds.values() for event in rows)
        expected = payload_types | (control_calls if control else frame_calls)
        if set(kinds) != expected or any(len(rows) != 1 for rows in kinds.values()):
            issues.append({"reason": "observation_evidence_incomplete", "identity": key,
                           "counts": {kind: len(kinds.get(kind, [])) for kind in sorted(expected | kinds.keys())}})
            continue
        rows = [kinds[kind][0] for kind in sorted(expected)]
        bbox, los = kinds["ObservationBbox"][0], kinds["ObservationLos"][0]
        stamp = bbox.get("stamp_ns", _array(bbox, "values", 1))
        if (type(stamp) is not int or stamp <= 0 or any(
                event.get("stamp_ns", _array(event, "values", 1)) != stamp for event in rows)):
            issues.append({"reason": "observation_timestamp_mismatch", "identity": key})
        updates = {event.get("update_id", 0) for event in rows}
        if ((control and updates != {0}) or (not control and (len(updates) != 1 or 0 in updates))
                or any(bool(event.get("flags", 0) & 32768) != control for event in rows)):
            issues.append({"reason": "observation_context_mismatch", "identity": key})
        if control and any(event.get(field, 0) for event in rows
                           for field in ("capture_id", "tracker_frame_id", "request_id")):
            issues.append({"reason": "control_observation_has_frame_identity", "identity": key})
        for payload in (bbox, los):
            values = payload.get("observation_values")
            if (not isinstance(values, list) or len(values) != 4
                    or not all(type(value) in (int, float) and math.isfinite(value) for value in values)):
                issues.append({"reason": "observation_payload_missing", "identity": key,
                               "type": event_type(payload)})
            if control and payload.get("flags", 0) & 16:
                issues.append({"reason": "control_observation_valid", "identity": key})
        for prefix in (("ControlBbox", "ControlLos") if control else ("Publish", "LosPublish")):
            if kinds[prefix + "End"][0]["mono_ns"] < kinds[prefix + "Start"][0]["mono_ns"]:
                issues.append({"reason": "observation_call_time_backwards", "identity": key, "type": prefix})
    return issues


def _publication(event, outputs):
    if event["_reason"] != "None" or not event.get("flags", 0) & 128:
        return None
    result = dict(event)
    output = outputs.get(identity(event))
    if event.get("version", 1) >= 2:
        tracking = event.get("observation_valid", False)
        box = event.get("observation_values")
        known = isinstance(box, list) and len(box) == 4
    elif "tracking" in event:
        tracking = event["tracking"]
        box = event.get("box")
        known = isinstance(tracking, bool) and isinstance(box, list) and len(box) == 4
    elif output:
        tracking = bool(_array(output, "values", 1))
        box = output.get("other_box")
        box_flag = bool(output.get("flags", 0) & 32)
        known = isinstance(box, list) and len(box) == 4
        # An invalid realtime box is a known invalid output, including when tracking is true.
        tracking = tracking and box_flag
    else:
        tracking, box, known = False, None, False
    result["_valid"] = (bool(tracking) and known and
                        all(isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) for v in box)
                        and box[2] > 0 and box[3] > 0)
    result["_known"] = known
    result["_box"] = box
    return result


def compute(events, inputs, clock, complete=True, windows=None):
    """Return full/core metrics, keeping unknown samples and censoring evidence.

    ``complete`` is the collector's integrity verdict, never inferred from files.
    The calculator independently rejects gaps, ambiguous identities and missing
    necessary event pairs. Clock origins are integer nanoseconds/microseconds.
    """
    modern = windows is not None
    selected_windows = WINDOWS if windows is None else {name:(item['start_us'],item['end_us'])
                                                       for name,item in windows.items()}
    if 'full' not in selected_windows or selected_windows['full'][0] != 0:
        raise ValueError('windows.full: complete [0,D) window required')
    duration = selected_windows['full'][1]
    for name, (start, end) in selected_windows.items():
        if type(start) is not int or type(end) is not int or not 0 <= start < end <= duration:
            raise ValueError(f'windows.{name}: invalid range')
    m0 = _number(clock.get("m0_ns"), "clock.m0_ns")
    e0 = _number(clock.get("e0_us"), "clock.e0_us")
    normalized, issues = [], []
    sequences = set()
    for index, original in enumerate(events):
        event = dict(original)
        if event.get("version", 1) not in (1, 2, 3):
            raise ValueError("event: unsupported diagnostic format")
        _number(event.get("mono_ns"), f"events[{index}].mono_ns")
        event["_type"], event["_reason"] = event_type(event), event_reason(event)
        if event["_type"] in ("Gap", "AbnormalEnd"):
            issues.append({"reason": "diagnostic_gap" if event["_type"] == "Gap" else "abnormal_end",
                           "event": evidence(event)})
        if event["_type"] == "Health" and (_array(event, "values", 3) or _array(event, "values", 6)):
            issues.append({"reason": "diagnostic_health", "event": evidence(event)})
        if (event["_type"] == "DetectStage" and not event.get("flags", 0) & 2048
                and event["_reason"] != "ImagePrepareFailed"):
            issues.append({"reason": "inference_execution_evidence_missing", "event": evidence(event)})
        if event.get("producer_seq"):
            key = (event.get("run_id"), event.get("producer_id"), event["producer_seq"])
            if key in sequences:
                issues.append({"reason": "duplicate_event", "event": evidence(event)})
            sequences.add(key)
        normalized.append(event)
    normalized.sort(key=lambda item: item["mono_ns"])
    issues.extend(observation_issues(normalized))
    updates = _pair(normalized, "UpdateStart", "UpdateEnd", "update_id", issues)
    detects = _pair(normalized, "DetectStart", "DetectEnd", "request_id", issues)
    initializations = _pair(normalized, "InitializeStart", "InitializeEnd", "update_id", issues, _initialization_detail)
    _pair(normalized, "PublishStart", "PublishEnd", "update_id", issues)
    outputs = {}
    for event in normalized:
        if event["_type"] == "Output":
            key = identity(event)
            if key in outputs:
                issues.append({"reason": "ambiguous_output", "event": evidence(event)})
            outputs[key] = event
    observation_v2 = any(e.get("version", 1) >= 2 for e in normalized)
    payloads = {}
    for event in normalized:
        if event["_type"] in ("ObservationBbox", "ObservationLos"):
            key = (event.get("run_id", 0), event.get("round_id", 0), _array(event, "values", 0), event["_type"])
            if key in payloads:
                issues.append({"reason": "duplicate_observation_payload", "event": evidence(event)})
            payloads[key] = event
    for event in normalized:
        if event["_type"] in ("PublishEnd", "LosPublishEnd") and event.get("version", 1) >= 2:
            kind = "ObservationBbox" if event["_type"] == "PublishEnd" else "ObservationLos"
            payload = payloads.get((event.get("run_id", 0), event.get("round_id", 0),
                                    _array(event, "values", 0), kind))
            if payload:
                event["observation_values"] = payload.get("observation_values")
                event["observation_valid"] = bool(payload.get("flags", 0) & 16)
                event["stamp_ns"] = payload.get("stamp_ns", _array(payload, "values", 1))
    _pair(normalized, "LosPublishStart", "LosPublishEnd", "update_id", issues)
    for event in normalized:
        if event["_type"] in ("LosPublishEnd", "ControlBboxEnd", "ControlLosEnd") and (
                event["_reason"] != "None" or not event.get("flags", 0) & 128):
            issues.append({"reason": "los_publication_error" if event["_type"] == "LosPublishEnd"
                           else "control_publication_error", "event": evidence(event)})
    pubs = []
    for event in normalized:
        if event["_type"] == "PublishEnd":
            pub = _publication(event, outputs)
            if pub is None:
                issues.append({"reason": "publication_error", "event": evidence(event)})
            else:
                pubs.append(pub)
                if not pub["_known"]:
                    issues.append({"reason": "publication_output_missing", "event": evidence(event)})
    input_by_time, input_ids = {}, set()
    for index, row in enumerate(inputs):
        frame_id = row.get("fixture_frame_id")
        relative = _number(row.get("relative_us"), f"input[{index}].relative_us")
        if frame_id is None or frame_id in input_ids or e0 + relative in input_by_time:
            issues.append({"reason": "ambiguous_input_identity", "fixture_frame_id": frame_id})
        input_ids.add(frame_id)
        input_by_time[e0 + relative] = row
    clear_events = [event for event in normalized if _is_clear(event)]
    valid_pubs = [pub for pub in pubs if pub["_valid"]]
    detect_starts = {identity(start, "request_id"): start for start, _ in detects if start}
    catchup_ends = defaultdict(list)
    steps = defaultdict(list)
    for event in normalized:
        if event["_type"] == "CatchupEnd":
            catchup_ends[identity(event)].append(event)
        elif event["_type"] == "CatchupStep":
            steps[identity(event)].append(event)
    for begin, finish in initializations:
        if not begin or not finish or not finish.get("flags", 0) & 128:
            continue
        summaries = catchup_ends.get(identity(begin), [])
        actual_steps = steps.get(identity(begin), [])
        if len(summaries) != 1:
            issues.append({"reason": "unpaired_catchup", "event": evidence(begin)})
        elif (_array(summaries[0], "counts", 0) != len(actual_steps)
              or _array(summaries[0], "counts", 1) != sum(bool(e.get("flags", 0) & 128) for e in actual_steps)
              or _array(summaries[0], "counts", 2) != sum(not bool(e.get("flags", 0) & 128) for e in actual_steps)):
            issues.append({"reason": "catchup_step_count_mismatch", "event": evidence(summaries[0])})
    record_complete = bool(complete) and not issues
    invalid_intervals = []
    invalid_start = None
    for pub in pubs:
        if not pub["_known"]:
            continue
        if not pub["_valid"] and invalid_start is None:
            invalid_start = pub
        elif pub["_valid"] and invalid_start is not None:
            invalid_intervals.append((invalid_start, pub))
            invalid_start = None
    if invalid_start is not None:
        invalid_intervals.append((invalid_start, None))
    result = {"schema_version": 2 if modern else SCHEMA_VERSION, "metric_version": METRIC_VERSION,
              "record_complete": record_complete, "issues": issues, "windows": {}}
    for window_name, (start_us, end_us) in selected_windows.items():
        start_ns, end_ns = m0 + start_us * 1000, m0 + end_us * 1000
        window = {"start_us": start_us, "end_us": end_us, "boundary": "[start,end)"}
        metrics = {}

        def inside(event):
            return start_ns <= event["mono_ns"] < end_ns

        def count(name, value, sample_count=None, unit="count", **extra):
            metrics[name] = _descriptor(window, unit, value if sample_count is None else sample_count,
                                        record_complete, value=value, **extra)

        def durations(name, samples, reason=None):
            metrics[name] = _duration(window, samples, record_complete, reason)

        window_pubs = [pub for pub in pubs if inside(pub)]
        valid = sum(pub["_valid"] for pub in window_pubs)
        count("publication_count", len(window_pubs))
        count("valid_publication_count", valid)
        count("invalid_publication_count", len(window_pubs) - valid)
        metrics["valid_publication_ratio"] = _descriptor(
            window, "ratio", len(window_pubs), record_complete,
            None if window_pubs else "no_event", value=valid / len(window_pubs) if window_pubs else None,
            total_count=len(window_pubs), valid_count=valid, invalid_count=len(window_pubs) - valid)
        times = [start_ns] + [pub["mono_ns"] for pub in window_pubs] + [end_ns]
        gap = max(right - left for left, right in zip(times, times[1:])) / 1e6
        count("max_no_publication_gap_ms", gap, len(window_pubs), unit="ms")
        spans = []
        for initial, final in invalid_intervals:
            left, right = initial["mono_ns"], final["mono_ns"] if final else end_ns
            overlap_start, overlap_end = max(left, start_ns), min(right, end_ns)
            if overlap_start < overlap_end:
                spans.append({"value_ms": (overlap_end - overlap_start) / 1e6,
                              "left_truncated": left < start_ns,
                              "right_truncated": final is None or right > end_ns,
                              "start": evidence(initial), "end": evidence(final) if final else None})
        count("max_invalid_span_ms", max((span["value_ms"] for span in spans), default=0),
              len(spans), unit="ms", intervals=spans)
        first_output_ns = pubs[0]["mono_ns"] if pubs else end_ns
        prefix_end = min(first_output_ns, end_ns)
        count("no_output_prefix_ms", max(0, prefix_end - start_ns) / 1e6,
              1 if prefix_end > start_ns else 0, unit="ms", right_truncated=not pubs or first_output_ns >= end_ns)
        selected_clears = [event for event in clear_events if inside(event)]
        reasons = Counter(event["_reason"] for event in selected_clears)
        count("clear_count", len(selected_clears))
        metrics["clear_count_by_reason"] = _descriptor(window, "count", len(selected_clears),
                                                       record_complete, value=dict(sorted(reasons.items())))
        recoveries, inherited = [], []
        for event in clear_events:
            next_valid = next((pub for pub in valid_pubs if pub["mono_ns"] >= event["mono_ns"]), None)
            finish_ns = next_valid["mono_ns"] if next_valid else end_ns
            if event["mono_ns"] >= end_ns or finish_ns <= start_ns:
                continue
            sample = {"clear": evidence(event), "publication": evidence(next_valid) if next_valid else None,
                      "value_ms": (finish_ns - event["mono_ns"]) / 1e6 if next_valid else None,
                      "wait_lower_bound_ms": max(0, end_ns - event["mono_ns"]) / 1e6 if not next_valid else None,
                      "left_truncated": event["mono_ns"] < start_ns,
                      "right_truncated": not next_valid or finish_ns > end_ns,
                      "window_span_ms": max(0, min(finish_ns, end_ns) - max(event["mono_ns"], start_ns)) / 1e6}
            (recoveries if inside(event) else inherited).append(sample)
        # Unrecovered events are censored, not unknown diagnostics. Keep them outside completed quantiles.
        completed_recoveries = [sample for sample in recoveries if sample["value_ms"] is not None]
        durations("recovery_latency_ms", completed_recoveries)
        metrics["recovery_latency_ms"].update(censored=[s for s in recoveries if s["value_ms"] is None],
                                               inherited=inherited, attempt_count=len(recoveries))
        count("recovered_count", len(completed_recoveries))
        count("unrecovered_count", len(recoveries) - len(completed_recoveries))
        age_samples, lag_samples = [], []
        for pub in window_pubs:
            source = input_by_time.get(pub.get("frame_time_us")) if pub.get("flags", 0) & 4 else None
            age = (pub["mono_ns"] - (m0 + source["relative_us"] * 1000)) / 1e6 if source else None
            prediction = pub.get("prediction_us") if pub.get("flags", 0) & 8 else None
            lag = (pub["mono_ns"] - (m0 + (prediction - e0) * 1000)) / 1e6 if prediction is not None else None
            age_samples.append({"event": evidence(pub), "fixture_frame_id": source.get("fixture_frame_id") if source else None,
                                "value_ms": age, "reason": None if source else "source_identity_unknown"})
            lag_samples.append({"event": evidence(pub), "value_ms": lag,
                                "reason": None if lag is not None else "prediction_unknown"})
        durations("frame_age_at_publish_ms", age_samples)
        durations("prediction_lag_at_publish_ms", lag_samples)
        for label, pairs in (("update", updates), ("detector", detects), ("initialization", initializations)):
            chosen = [(begin, finish) for begin, finish in pairs if begin and inside(begin)]
            samples = [{"start": evidence(begin), "end": evidence(finish) if finish else None,
                        "value_ms": (finish["mono_ns"] - begin["mono_ns"]) / 1e6 if finish else None}
                       for begin, finish in chosen]
            count(label + "_count", len(chosen))
            durations(label + "_duration_ms", samples)
            if label == "initialization":
                count("initialization_success_count", sum(bool(finish and finish.get("flags", 0) & 128) for _, finish in chosen))
                count("initialization_failure_count", sum(bool(finish and not finish.get("flags", 0) & 128) for _, finish in chosen))
        count("update_throughput_hz", metrics["update_count"].get("observed_value", metrics["update_count"]["value"]) /
              ((end_us - start_us) / 1e6), metrics["update_count"]["sample_count"], unit="Hz")
        for label, kind, predicate in (
            ("request_accepted_count", "RequestDecision", lambda e: bool(e.get("flags", 0) & 128)),
            ("request_rejected_count", "RequestDecision", lambda e: not e.get("flags", 0) & 128),
            ("request_executed_count", "DetectStart", lambda e: True),
            ("request_completed_count", "DetectEnd", lambda e: bool(e.get("flags", 0) & 128)),
            ("result_read_count", "ResultRead", lambda e: True),
            ("result_overwritten_count", "ResultOverwrite", lambda e: True),
            ("request_cancelled_count", "RequestCancel", lambda e: True),
            ("successful_result_history_miss_count", "HistoryLookup", lambda e: e["_reason"] == "HistoryMiss"),
        ):
            chosen = [e for e in normalized if e["_type"] == kind and predicate(e) and
                      (inside(detect_starts[identity(e, "request_id")])
                       if kind == "DetectEnd" and identity(e, "request_id") in detect_starts else inside(e))]
            if kind == "HistoryLookup":
                successful_requests = {identity(e, "request_id") for e in normalized
                                       if e["_type"] == "DetectEnd" and e.get("flags", 0) & 512}
                chosen = [e for e in chosen if identity(e, "request_id") in successful_requests]
            ids = sorted({identity(e, "request_id") for e in chosen})
            count(label, len(ids), identities=ids)
        for label, kind in (("los_publication_count", "LosPublishEnd"),
                            ("observation_suppressed_count", "ObservationSuppressed"),
                            ("off_notification_count", "ControlBboxEnd"),
                            ("trust_loss_notification_count", "ControlBboxEnd")):
            chosen = [e for e in normalized if e["_type"] == kind and inside(e)]
            if label == "off_notification_count":
                chosen = [e for e in chosen if not e.get("flags", 0) & 262144]
            elif label == "trust_loss_notification_count":
                chosen = [e for e in chosen if e.get("flags", 0) & 262144]
            descriptor = _descriptor(window, "count", len(chosen), record_complete,
                                     value=len(chosen), samples=[evidence(e) for e in chosen])
            descriptor["metric_version"] = 3
            if not observation_v2:
                descriptor.update(availability="unavailable", reason="historical_field_absent", value=None)
            if label == "trust_loss_notification_count" and not any(e.get("version", 1) >= 3 for e in normalized):
                descriptor.update(availability="unavailable", reason="historical_field_absent", value=None)
            metrics[label] = descriptor
        los_pubs = [e for e in normalized if e["_type"] == "LosPublishEnd" and inside(e)]
        metrics["los_valid_count"] = _descriptor(window, "count", len(los_pubs), record_complete,
            value=sum(bool(e.get("observation_valid")) for e in los_pubs))
        if not observation_v2:
            metrics["los_valid_count"].update(availability="unavailable", reason="historical_field_absent", value=None)
        metrics["published_float_box"] = _descriptor(window, "pixels", len(window_pubs), record_complete,
            samples=[dict(evidence(e), box=e.get("observation_values"), stamp_ns=e.get("stamp_ns")) for e in window_pubs])
        if not observation_v2:
            metrics["published_float_box"].update(availability="unavailable", reason="historical_field_absent")
        # Extensions retain v1 formulas above; a capability marker distinguishes old logs from a real zero.
        capability = any(e["_type"] == "RunStart" and _array(e, "values", 6) >= 2 for e in normalized)
        for label, predicate in (
            ("request_superseded_count", lambda e: e["_type"] == "RequestCancel" and e["_reason"] == "Superseded"),
            ("request_cleanup_cancelled_count", lambda e: e["_type"] == "RequestCancel" and e["_reason"] == "Cleaning"),
            ("source_metadata_available_count", lambda e: e["_type"] == "SourceContext" and e["_reason"] == "None"),
            ("source_metadata_unavailable_count", lambda e: e["_type"] == "SourceContext" and e["_reason"] == "ContextUnavailable"),
            ("recovery_unavailable_count", lambda e: e["_type"] == "SourceContext" and e["_reason"] == "RecoveryUnavailable"),
        ):
            chosen = [e for e in normalized if inside(e) and predicate(e)]
            descriptor = _descriptor(window, "count", len(chosen), record_complete,
                                     value=len(chosen), samples=[evidence(e) for e in chosen])
            descriptor["metric_version"] = ARBITRATION_METRIC_VERSION
            if not capability:
                descriptor.update(availability="unavailable", reason="historical_field_absent", value=None)
            metrics[label] = descriptor
        durations("detection_execution_ms", [
            {"start": evidence(begin), "end": evidence(end) if end else None,
             "value_ms": (end["mono_ns"] - begin["mono_ns"]) / 1e6 if end else None}
            for begin, end in detects if begin and inside(begin)])
        metrics["detection_execution_ms"]["metric_version"] = ARBITRATION_METRIC_VERSION
        accepted_by_id = {identity(e, "request_id"): e for e in normalized
                          if e["_type"] == "RequestDecision" and e.get("flags", 0) & 128}
        for label, kind in (("request_queue_wait_ms", "DetectStart"), ("source_capture_to_consumption_ms", "ResultRead")):
            samples = []
            for event in normalized:
                if event["_type"] != kind or not inside(event):
                    continue
                accepted = accepted_by_id.get(identity(event, "request_id"))
                value = None
                if kind == "DetectStart" and accepted:
                    value = (event["mono_ns"] - accepted["mono_ns"]) / 1e6
                elif event.get("flags", 0) & 4 and event.get("frame_time_us") in input_by_time:
                    planned = m0 + (event["frame_time_us"] - e0) * 1000
                    value = (event["mono_ns"] - planned) / 1e6
                samples.append({"event": evidence(event), "value_ms": value})
            durations(label, samples)
            metrics[label]["metric_version"] = ARBITRATION_METRIC_VERSION
        rejected = [e for e in normalized if e["_type"] == "RequestDecision" and inside(e) and not e.get("flags", 0) & 128]
        rejection_reasons = defaultdict(set)
        for event in rejected:
            rejection_reasons[event["_reason"]].add(identity(event, "request_id"))
        metrics["request_rejected_count_by_reason"] = _descriptor(
            window, "count", len(rejected), record_complete,
            value={reason: len(ids) for reason, ids in sorted(rejection_reasons.items())})
        stages = [e for e in normalized if e["_type"] == "DetectStage" and
                  identity(e, "request_id") in detect_starts and inside(detect_starts[identity(e, "request_id")])]
        infer_stages = [e for e in stages if e.get("flags", 0) & 2048]
        count("detector_stage_count", len(stages))
        count("detector_inference_count", len(infer_stages))
        metrics["detector_inference_count"]["identities"] = [
            {"request": identity(e, "request_id"), "stage_kind": _array(e, "values", 0),
             "stage_index": _array(e, "values", 1)} for e in infer_stages]
        catchup_samples, catchup_step_samples, recovery_update_samples = [], [], []
        catchup_attempts = catchup_successes = catchup_failures = step_successes = step_failures = 0
        update_by_id = {identity(begin): (begin, end) for begin, end in updates if begin}
        for begin, finish in initializations:
            if not begin or not inside(begin):
                continue
            outer = update_by_id.get(identity(begin))
            if outer:
                ustart, uend = outer
                recovery_update_samples.append({"start": evidence(ustart), "end": evidence(uend) if uend else None,
                                                "value_ms": (uend["mono_ns"] - ustart["mono_ns"]) / 1e6 if uend else None})
            if not finish or not finish.get("flags", 0) & 128:
                continue
            summaries = catchup_ends.get(identity(begin), [])
            summary = summaries[0] if len(summaries) == 1 else None
            if summary and _array(summary, "counts", 0) == 0:
                # Initialization without follow-up frames is not a catchup attempt.
                continue
            catchup_attempts += 1
            sample = {"start": evidence(finish), "end": evidence(summary) if summary else None,
                      "value_ms": (summary["mono_ns"] - finish["mono_ns"]) / 1e6 if summary else None}
            catchup_samples.append(sample)
            if summary:
                catchup_successes += bool(summary.get("flags", 0) & 128)
                catchup_failures += not bool(summary.get("flags", 0) & 128)
            for step in steps.get(identity(begin), []):
                success = bool(step.get("flags", 0) & 128)
                step_successes += success
                step_failures += not success
                catchup_step_samples.append({"event": evidence(step), "success": success,
                                             "value_ms": step["duration_ns"] / 1e6 if "duration_ns" in step else None})
        count("catchup_attempt_count", catchup_attempts)
        count("catchup_success_count", catchup_successes)
        count("catchup_failure_count", catchup_failures)
        count("catchup_step_count", step_successes + step_failures)
        count("catchup_step_success_count", step_successes)
        count("catchup_step_failure_count", step_failures)
        durations("catchup_duration_ms", catchup_samples)
        durations("catchup_step_duration_ms", catchup_step_samples)
        durations("recovery_update_duration_ms", recovery_update_samples)
        window_inputs = [row for row in inputs if start_us <= row["relative_us"] < end_us]
        count("input_planned_count", len(window_inputs))
        count("input_enqueued_count", sum(row.get("enqueued") is True for row in window_inputs))
        count("input_rejected_count", sum(row.get("enqueued") is False and row.get("supplied", True)
                                           for row in window_inputs))
        count("input_unsupplied_count", sum(row.get("supplied") is False for row in window_inputs))
        count("input_residual_count", sum(row.get("residual") is True for row in window_inputs))
        supply_samples = []
        for row in window_inputs:
            supplied = row.get("supplied_mono_ns") if row.get("supplied", True) else None
            planned = row.get("planned_mono_ns", m0 + row["relative_us"] * 1000)
            supply_samples.append({"fixture_frame_id": row["fixture_frame_id"],
                                   "value_ms": (supplied - planned) / 1e6 if supplied is not None else None})
        count("input_late_count", sum(s["value_ms"] is not None and s["value_ms"] > 0 for s in supply_samples))
        durations("input_supply_lateness_ms", supply_samples)
        for label, kind, predicate in (
            ("frame_selected_count", "FrameSelect", lambda e: True),
            ("frame_skipped_count", "FrameSkip", lambda e: e["_reason"] == "LatestSkip"),
            ("frame_residual_count", "FrameSkip", lambda e: e["_reason"] == "Termination"),
        ):
            selected = [e for e in normalized if e["_type"] == kind and inside(e) and predicate(e)]
            frame_ids = []
            for event in selected:
                source = input_by_time.get(event.get("frame_time_us"))
                frame_ids.append(source.get("fixture_frame_id") if source else event.get("fixture_frame_id"))
            count(label, len(selected), identities=frame_ids)
        result["windows"][window_name] = metrics
    result["validity"] = {name: validity_metrics.compute(normalized,
        m0 + start * 1000, m0 + end * 1000, record_complete)
        for name, (start, end) in selected_windows.items()}
    return result


def read_jsonl(path):
    records = []
    with Path(path).open(encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{number}: {error.msg}") from error
            if not isinstance(value, dict):
                raise ValueError(f"{path}:{number}: expected object")
            records.append(value)
    return records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_directory", type=Path)
    args = parser.parse_args(argv)
    try:
        run = args.run_directory
        identity_data = json.loads((run / "identity.json").read_text(encoding="utf-8"))
        clock = identity_data.get("clock")
        if clock is None:
            clock = json.loads((run / "input-clock.json").read_text(encoding="utf-8"))
        completion_path = run / "completion.json"
        completion = json.loads(completion_path.read_text(encoding="utf-8")) if completion_path.exists() else {}
        result = compute(read_jsonl(run / "events.jsonl"), read_jsonl(run / "input.jsonl"), clock,
                         complete=completion.get("normal_end") is True and completion.get("qualified") is True)
        with (run / "metrics.json").open("x", encoding="utf-8") as stream:
            json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
        return 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"metrics: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

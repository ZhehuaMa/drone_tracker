#!/usr/bin/env python3
"""Explicitly freeze at least three qualified fixed-case reference runs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import common

CASE_ID = "wlcb-1513-loss-recovery"
GROUP_FIELDS = (
    "identity_version", "fixture", "attitude", "models", "algorithm", "board", "runtime", "driver",
    "system_library", "dependencies", "cpu_policy", "workload", "observation",
    "test_driver", "metric_schema", "metric_calculator",
)
REQUIRED_FILES = {"identity.json", "input.jsonl", "events.jsonl", "frames.csv", "messages.csv", "metrics.json"}
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}\Z")


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     ensure_ascii=False, allow_nan=False).encode("utf-8")).hexdigest()


def read_json(path):
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"{path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected JSON object")
    return value


def _missing(value):
    if value is None or value == "" or value == {} or value == []:
        return True
    if isinstance(value, str) and value.lower() in ("unknown", "unavailable", "not_verified"):
        return True
    if isinstance(value, dict):
        return any(_missing(child) for child in value.values())
    if isinstance(value, list):
        return any(_missing(child) for child in value)
    return False


def validate_group(group):
    if not isinstance(group, dict):
        raise ValueError("identity.comparison_group: expected object")
    missing = [field for field in GROUP_FIELDS if field not in group or _missing(group[field])]
    if missing:
        raise ValueError("identity.comparison_group: missing identity field(s): " + ", ".join(missing))
    if group['identity_version'] != 2:
        raise ValueError("identity.comparison_group.identity_version: actual dependency/semantic identity version 2 required")
    dependencies = group['dependencies']
    if not isinstance(dependencies, dict) or dependencies.get('identity_version') != 2:
        raise ValueError("identity.comparison_group.dependencies.identity_version: actual library identity required")
    libraries = dependencies.get('libraries', {})
    for role in ('node', 'worker'):
        if not isinstance(libraries.get(role), dict) or not libraries[role]:
            raise ValueError("identity.comparison_group.dependencies.libraries." + role + ": missing actual library contents")
        if any(not SHA256_PATTERN.fullmatch(value) for value in libraries[role].values() if isinstance(value, str)) \
                or any(not isinstance(value, str) for value in libraries[role].values()):
            raise ValueError("identity.comparison_group.dependencies.libraries." + role + ": SHA-256 required")
    middleware = dependencies.get('middleware', {})
    if not isinstance(middleware, dict) or not middleware.get('effective') or not middleware.get('libraries'):
        raise ValueError("identity.comparison_group.dependencies.middleware: effective selector and contents required")
    if any(not isinstance(value, str) or not SHA256_PATTERN.fullmatch(value)
           for value in middleware['libraries'].values()):
        raise ValueError("identity.comparison_group.dependencies.middleware.libraries: SHA-256 required")
    return group


def field_differences(left, right, prefix="comparison_group"):
    """Return every differing leaf; code identity is intentionally outside this tree."""
    if isinstance(left, dict) and isinstance(right, dict):
        differences = []
        for key in sorted(left.keys() | right.keys()):
            path = f"{prefix}.{key}"
            if key not in left or key not in right:
                differences.append({"field": path, "baseline": left.get(key), "candidate": right.get(key),
                                    "reason": "missing_field"})
            else:
                differences.extend(field_differences(left[key], right[key], path))
        return differences
    if left != right:
        return [{"field": prefix, "baseline": left, "candidate": right, "reason": "identity_mismatch"}]
    return []


ARBITRATION_METRICS = frozenset({
    "request_superseded_count", "request_cleanup_cancelled_count", "source_metadata_available_count",
    "source_metadata_unavailable_count", "recovery_unavailable_count", "request_queue_wait_ms",
    "detection_execution_ms", "source_capture_to_consumption_ms",
})


OBSERVATION_METRICS = frozenset({"los_publication_count", "observation_suppressed_count", "off_notification_count"})
OBSERVATION_FIELDS = OBSERVATION_METRICS | {"los_valid_count", "published_float_box"}


def load_run(path):
    """Validate integrity before reading any value into a formal set."""
    run = Path(path).resolve(strict=True)
    identity = read_json(run / "identity.json")
    completion = read_json(run / "completion.json")
    metrics = read_json(run / "metrics.json")
    if identity.get('mode') != 'realtime':
        raise ValueError(f'{run}/identity.json:case_id/mode: expected realtime')
    common.validate_record(identity, 'identity')
    version = identity['schema_version']
    modern = version == 2
    if (not modern and identity.get("case_id") != CASE_ID) or identity.get("mode") != "realtime":
        raise ValueError(f"{run}/identity.json:case_id/mode: expected {CASE_ID}/realtime")
    if not modern and identity.get("duration_us") != 20_000_000:
        raise ValueError(f"{run}/identity.json:duration_us: expected 20000000")
    clock = identity.get("clock")
    if clock is None:
        clock = read_json(run / "input-clock.json")
    for field in ("m0_ns", "e0_us"):
        if (not isinstance(clock, dict) or not isinstance(clock.get(field), int)
                or isinstance(clock[field], bool) or clock[field] < 0):
            raise ValueError(f"{run}/identity.json:clock.{field}: nonnegative integer required")
    validate_group(identity.get("comparison_group"))
    code = identity.get("code", {})
    for field in ("source_sha256", "binary_sha256"):
        if not isinstance(code.get(field), str) or not SHA256_PATTERN.fullmatch(code[field]):
            raise ValueError(f"{run}/identity.json:code.{field}: required SHA-256; commit alone is insufficient")
    if (completion.get("schema_version") != version or completion.get("qualified") is not True
            or completion.get("normal_end") is not True or completion.get("reasons")):
        raise ValueError(f"{run}/completion.json: unqualified or incomplete run: {completion.get('reasons')}")
    files = completion.get("files")
    if not isinstance(files, dict):
        raise ValueError(f"{run}/completion.json:files: required checksum object")
    missing = REQUIRED_FILES - files.keys()
    if identity.get("clock") is None and "input-clock.json" not in files:
        missing.add("input-clock.json")
    if missing:
        raise ValueError(f"{run}/completion.json:files: missing {', '.join(sorted(missing))}")
    if not any(name == "events.bin" or name.endswith(".dtd") for name in files):
        raise ValueError(f"{run}/completion.json:files: raw events.bin or .dtd evidence required")
    for relative, expected in files.items():
        file = (run / relative).resolve(strict=True)
        if not file.is_relative_to(run) or Path(relative).is_absolute():
            raise ValueError(f"{run}/completion.json:files.{relative}: path escapes run directory")
        if not isinstance(expected, str) or not SHA256_PATTERN.fullmatch(expected):
            raise ValueError(f"{run}/completion.json:files.{relative}: invalid SHA-256")
        actual = sha256(file)
        if expected != actual:
            raise ValueError(f"{run}/{relative}: checksum mismatch: expected {expected}, actual {actual}")
    if metrics.get("schema_version") != version or metrics.get("metric_version") != 1:
        raise ValueError(f"{run}/metrics.json: schema_version/metric_version mismatch")
    if metrics.get("record_complete") is not True or metrics.get("issues"):
        raise ValueError(f"{run}/metrics.json: incomplete diagnostic evidence")
    bounds = identity['input']['analysis_windows'] if modern else {
        'full':{'start_us':0,'end_us':20_000_000}, 'core':{'start_us':13_500_000,'end_us':17_500_000}}
    if modern:
        data = identity['input']
        if data['end_us'] - data['start_us'] != identity['duration_us'] or bounds.get('full') != {
                'start_us':0,'end_us':identity['duration_us']}:
            raise ValueError(f'{run}/identity.json: input duration/window mismatch')
        if any(type(data[k]) is not int or data[k] <= 0 for k in ('frame_count','width','height')):
            raise ValueError(f'{run}/identity.json: invalid input dimensions/count')
        config = identity['comparison_group']['test_driver']['configuration']
        if config.get('schema_version') != 3 or config.get('case_id') != identity['case_id'] or config.get('analysis_windows') != bounds:
            raise ValueError(f'{run}/identity.json: comparison conditions differ from input')
    if set(metrics.get("windows", {})) != set(bounds):
        raise ValueError(f"{run}/metrics.json:windows: full and core required")
    for window_name, descriptors in metrics["windows"].items():
        if not descriptors:
            raise ValueError(f"{run}/metrics.json:windows.{window_name}: no metrics")
        for name, descriptor in descriptors.items():
            fields = {"metric_version", "unit", "window", "sample_count", "availability", "reason"}
            if not isinstance(descriptor, dict) or fields - descriptor.keys():
                raise ValueError(f"{run}/metrics.json:{window_name}.{name}: incomplete descriptor")
            extension = (name in ARBITRATION_METRICS and descriptor["metric_version"] == 2) or name in OBSERVATION_FIELDS
            historical_absence = (extension and descriptor["availability"] == "unavailable"
                                  and descriptor["reason"] == "historical_field_absent")
            unknown_source_time = (extension and name == "source_capture_to_consumption_ms"
                                   and descriptor["availability"] == "incomplete"
                                   and descriptor["reason"] == "unknown_sample")
            if descriptor["availability"] not in ("available", "no_event", "not_applicable") \
                    and not historical_absence and not unknown_source_time:
                raise ValueError(f"{run}/metrics.json:{window_name}.{name}: {descriptor['availability']} ({descriptor['reason']})")
            expected_bounds = (bounds[window_name]['start_us'], bounds[window_name]['end_us'])
            if not 0 <= expected_bounds[0] < expected_bounds[1] <= identity['duration_us']:
                raise ValueError(f'{run}/identity.json: invalid analysis window')
            window = descriptor.get("window", {})
            if (window.get("start_us"), window.get("end_us")) != expected_bounds:
                raise ValueError(f"{run}/metrics.json:{window_name}.{name}.window: expected {expected_bounds}")
            expected_version = 3 if name in OBSERVATION_METRICS else 2 if name in ARBITRATION_METRICS else 1
            if descriptor["metric_version"] != expected_version:
                raise ValueError(f"{run}/metrics.json:{window_name}.{name}.metric_version: expected {expected_version}")
            if (not isinstance(descriptor["sample_count"], int) or isinstance(descriptor["sample_count"], bool)
                    or descriptor["sample_count"] < 0):
                raise ValueError(f"{run}/metrics.json:{window_name}.{name}.sample_count: nonnegative integer required")
    return {"path": str(run), "identity": identity, "completion": completion, "metrics": metrics,
            "checksums": {**files, "completion.json": sha256(run / "completion.json")}}


def validate_set(paths):
    paths = list(paths)
    if len(paths) < 3:
        raise ValueError("runs: at least three independent qualified 20-second runs required")
    runs = [load_run(path) for path in paths]
    if len({run["path"] for run in runs}) != len(runs):
        raise ValueError("runs: duplicate run directory")
    # A copied run is not a new independent process.
    if len({run["checksums"]["identity.json"] for run in runs}) != len(runs):
        raise ValueError("runs: duplicated identity.json; independent process identities required")
    first = runs[0]["identity"]["comparison_group"]
    first_identity = runs[0]["identity"]
    for run in runs[1:]:
        if any(run['identity'][key] != first_identity[key] for key in ('schema_version','case_id','duration_us')):
            raise ValueError('runs: mixed record versions, case identities or durations')
        differences = field_differences(first, run["identity"]["comparison_group"])
        if differences:
            raise ValueError(f"{run['path']}: comparison group mismatch: " +
                             ", ".join(difference["field"] for difference in differences))
        for field in ("source_sha256", "binary_sha256", "project_libraries"):
            if runs[0]["identity"]["code"].get(field, {}) != run["identity"]["code"].get(field, {}):
                raise ValueError(f"{run['path']}: code.{field}: different builds within one run set")
    return runs


def freeze(paths, output):
    """Create a new immutable manifest; there is deliberately no update mode."""
    destination = Path(output)
    if destination.exists():
        raise FileExistsError(f"baseline output already exists: {destination}")
    runs = validate_set(paths)
    manifest = {
        **{key:runs[0]["identity"][key] for key in ("schema_version","case_id","mode","duration_us")},
        "comparison_group": runs[0]["identity"]["comparison_group"],
        "runs": [{"path": os.path.relpath(run["path"], destination.resolve()),
                  "checksums": run["checksums"], "code": run["identity"]["code"]} for run in runs],
    }
    manifest["manifest_sha256"] = canonical_digest(manifest)
    destination.mkdir(parents=True, exist_ok=False)
    with (destination / "baseline.json").open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2, ensure_ascii=False, allow_nan=False)
        stream.write("\n")
    return manifest


def load_baseline(path):
    source = Path(path)
    if source.is_dir():
        source = source / "baseline.json"
    manifest = read_json(source)
    recorded = manifest.get("manifest_sha256")
    content = {key: value for key, value in manifest.items() if key != "manifest_sha256"}
    if recorded != canonical_digest(content):
        raise ValueError(f"{source}: manifest_sha256: frozen manifest altered")
    common.validate_record(manifest, 'baseline')
    if (manifest.get("schema_version") == 1 and manifest.get("case_id") != CASE_ID) or manifest.get("mode") != "realtime":
        raise ValueError(f"{source}: schema/case/mode mismatch")
    refs = manifest.get("runs", [])
    runs = validate_set([source.parent / item["path"] for item in refs])
    for reference, run in zip(refs, runs):
        if any(manifest[key] != run['identity'][key] for key in ('schema_version','case_id','duration_us')):
            raise ValueError(f'{source}: frozen case/version/range differs from runs')
        if reference["checksums"] != run["checksums"]:
            differences = field_differences(reference["checksums"], run["checksums"], "checksums")
            raise ValueError(f"{run['path']}: frozen run modified: " + ", ".join(d["field"] for d in differences))
    differences = field_differences(manifest["comparison_group"], runs[0]["identity"]["comparison_group"])
    if differences:
        raise ValueError(f"{source}: frozen comparison_group no longer matches runs")
    return manifest, runs


def main(argv=None):
    print('Use run_suite.py CONFIG.yaml with operation: freeze; baseline.py is an internal component.', file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())

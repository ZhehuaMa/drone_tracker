"""从验收原始 CSV/JSON 计算指标；不把未知同步位置当作零，不改写原件。"""

import argparse
import csv
import json
import math
from pathlib import Path


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)] if values else None


def rows(path):
    with path.open() as stream:
        return [{key: int(value) for key, value in row.items()} for row in csv.DictReader(stream)]


def summarize(directory):
    result = json.loads((directory / "result.json").read_text())
    timing = rows(directory / "timing.csv")
    statuses = rows(directory / "timing.csv.status.csv")
    windows = []
    for round_data in result["rounds"]:
        if "window_begin_us" not in round_data:
            continue
        begin, end = round_data["window_begin_us"], round_data["window_end_us"]
        duration = (end - begin) / 1000000
        samples = [row for row in timing if begin <= row["monotonic_us"] < end and row["consumed"]]
        states = [row for row in statuses if begin <= row["monotonic_us"] < end]
        known = [max(0, row["latest_frame_us"] - row["synced_frame_us"])
                 for row in states if row["sync_known"]]
        resources = [row for row in result["resources"] if begin <= row["monotonic_us"] < end]
        resource_summary = {}
        for name in ("node", "worker"):
            available = [row for row in resources if row.get(name)]
            resource_summary[name] = {"samples": len(available)}
            if available:
                first, last = available[0], available[-1]
                seconds = (last["monotonic_us"] - first["monotonic_us"]) / 1000000
                same_process = first[name]["pid"] == last[name]["pid"]
                resource_summary[name].update({
                    "cpu_percent_one_core": 100 * (last[name]["cpu_seconds"] - first[name]["cpu_seconds"]) / seconds
                    if seconds > 0 and same_process else None,
                    "rss_first_kib": first[name]["rss_kib"], "rss_last_kib": last[name]["rss_kib"],
                    "rss_max_kib": max(row[name]["rss_kib"] for row in available),
                    "fds_first": first[name]["fds"], "fds_last": last[name]["fds"],
                    "fds_max": max(row[name]["fds"] for row in available),
                })
        temperatures = [value for row in resources for value in row["temperature_millicelsius"].values()]
        io = [row["partition_io"] for row in resources if row.get("partition_io")]
        windows.append({
            "duration_seconds": duration, "consumed_frames": len(samples),
            "tracking_p95_us": percentile([row["update_us"] for row in samples], 0.95),
            "consumption_fps": len(samples) / duration,
            "sync_known_samples": len(known), "sync_unknown_samples": len(states) - len(known),
            "sync_lag_p99_us": percentile(known, 0.99), "sync_lag_max_us": max(known) if known else None,
            "sync_duration_max_us": max((row["sync_duration_us"] for row in states), default=None),
            "recording_dropped_total_max": max((row["dropped"] for row in states), default=None),
            "recording_failed_samples": sum(bool(row["failed"]) for row in states),
            "resources": resource_summary,
            "temperature_max_millicelsius": max(temperatures) if temperatures else None,
            "partition_written_bytes_delta": io[-1]["written_bytes"] - io[0]["written_bytes"] if len(io) > 1 else None,
            "power_samples": sum(bool(row["power_microwatts"]) for row in resources),
        })
    return {"label": result["label"], "sync_ms": result["sync_ms"], "exit_code": result["exit_code"],
            "error": result.get("error"), "windows": windows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    results = [summarize(path.parent) for path in sorted(args.root.glob("*/result.json"))
               if (path.parent / "timing.csv").exists()]
    print(json.dumps({"percentile_method": "nearest rank", "runs": results},
                     ensure_ascii=False, indent=2))



if __name__ == "__main__":
    main()

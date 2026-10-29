#!/usr/bin/env python3
"""Reconstruct full-window tail calls from existing, unmodified CPU retest data."""
import argparse
import bisect
import csv
import json
import math
from pathlib import Path


def analyze(root):
    result = {"root": str(root.resolve()), "pairs": []}
    for number in range(1, 11):
        directory = root / "evidence" / f"pair{number}"
        case = json.loads((directory / "case.json").read_text())
        pair = {"number": number, "modes": {}}
        for window in case["rounds"]:
            mode = window["mode"]
            sample_path, = directory.glob(f"*-{mode}/calls.csv")
            with sample_path.open() as source:
                samples = [{key: int(value) for key, value in row.items()}
                           for row in csv.DictReader(source)]
            samples = [row for row in samples
                       if window["measurement_start_ns"] <= row["start_ns"]
                       < window["measurement_end_ns"]]
            assert samples, "Missing full-window samples"
            durations = sorted(row["duration_ns"] for row in samples)
            p99 = durations[math.ceil(len(durations) * 0.99) - 1]
            pair["modes"][mode] = {
                "samples": len(samples), "p99_ms": p99 / 1e6,
                "consumed_fps": sum(row["consumed"] for row in samples) * 1e9
                / (window["measurement_end_ns"] - window["measurement_start_ns"]),
            }
            if mode != "basic":
                continue
            events_path, = directory.glob("export-*/events.jsonl")
            starts, updates = [], {}
            with events_path.open() as source:
                for line in source:
                    event = json.loads(line)
                    if event["producer_id"] != 0:
                        continue
                    key = (event["round_id"], event["update_id"])
                    if event["type"] == "调用开始":
                        starts.append((event["mono_ns"], key))
                        updates[key] = {"steps": 0, "init_ns": 0, "step_ns": 0}
                    elif key in updates:
                        if event["type"] == "初始化结束":
                            updates[key]["init_ns"] += event["duration_ns"]
                        elif event["type"] == "补跟踪步骤":
                            updates[key]["steps"] += 1
                            updates[key]["step_ns"] += event["duration_ns"]
            starts.sort()
            times = [item[0] for item in starts]
            tails = []
            for row in samples:
                if row["duration_ns"] < p99:
                    continue
                index = bisect.bisect_left(times, row["start_ns"])
                assert index < len(starts)
                stamp, key = starts[index]
                assert stamp < row["start_ns"] + row["duration_ns"], "Unmatched call"
                work = updates[key]
                tails.append(dict(row, update_id=key[1], **work,
                                  init_and_steps_fraction=(work["init_ns"] + work["step_ns"])
                                  / row["duration_ns"]))
            pair["modes"][mode]["tail_calls"] = tails
        pair["p99_delta_ms"] = pair["modes"]["basic"]["p99_ms"] - pair["modes"]["off"]["p99_ms"]
        pair["fps_decline_percent"] = 100 * (
            1 - pair["modes"]["basic"]["consumed_fps"] / pair["modes"]["off"]["consumed_fps"])
        result["pairs"].append(pair)
    tails = [call for pair in result["pairs"] for call in pair["modes"]["basic"]["tail_calls"]]
    result["tail_summary"] = {
        "count": len(tails), "minimum_steps": min(row["steps"] for row in tails),
        "maximum_steps": max(row["steps"] for row in tails),
        "minimum_init_and_steps_fraction": min(row["init_and_steps_fraction"] for row in tails),
        "maximum_call_ms": max(row["duration_ns"] for row in tails) / 1e6,
    }
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = [analyze(root) for root in args.roots]
    with args.output.open("x") as destination:
        json.dump(output, destination, indent=2)
        destination.write("\n")
    for item in output:
        print(item["root"], item["tail_summary"])

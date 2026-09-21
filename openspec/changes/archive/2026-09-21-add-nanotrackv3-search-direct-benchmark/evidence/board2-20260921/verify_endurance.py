"""核对持久化负载记录与独立普通路径，输出可重算的验收摘要。"""

import csv
import hashlib
import io
import json
import math
from pathlib import Path
import tarfile


def read_json(archive, name):
    with archive.extractfile(name) as stream:
        return json.load(stream)


def read_rows(archive, name):
    with archive.extractfile(name) as stream:
        return list(csv.DictReader(io.TextIOWrapper(stream)))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def result_key(row):
    return row["operation"], row["frame_index"]


def compare_rows(rows, reference):
    maximum_box = 0.0
    maximum_score = 0.0
    for row in rows:
        expected = reference[result_key(row)]
        for field in ("pts", "pts_valid", "status", "bbox_valid", "bbox_kind"):
            require(row[field] == expected[field], f"{field} 不一致：{row}")
        require(row["status"] == "success" and not row["error"], "存在失败帧")
        for field in ("x", "y", "width", "height", "score"):
            if not expected[field]:
                require(not row[field], f"{field} 不应有值")
                continue
            value = float(row[field])
            require(math.isfinite(value), f"{field} 非有限值")
            error = abs(value - float(expected[field]))
            require(error <= (1e-6 if field == "score" else 1e-4), f"{field} 超限")
            if field == "score":
                maximum_score = max(maximum_score, error)
            else:
                maximum_box = max(maximum_box, error)
    return maximum_box, maximum_score


def main():
    root = Path(__file__).resolve().parent
    old_path = root.parent / "board2-20260920/records.tar.gz"
    new_path = root / "records.tar.gz"
    with tarfile.open(old_path) as old, tarfile.open(new_path) as new:
        baseline = "pack/runs/perf-baseline-total-1/"
        reference_rows = read_rows(old, baseline + "frames.csv")
        reference = {result_key(row): row for row in reference_rows}
        require(len(reference) == 301, "参照未覆盖初始化及 300 次更新")
        compare_rows(reference_rows, reference)
        baseline_identity = read_json(old, baseline + "manifest.json")["identity"]
        manifest = read_json(new, "runs/endurance/manifest.json")
        for key in ("board", "machine", "kernel", "hostname", "code_commit",
                    "code_snapshot_sha256", "video_sha256", "model_0_sha256",
                    "model_1_sha256", "model_2_sha256", "rknn_library_sha256"):
            require(manifest["identity"][key] == baseline_identity[key], f"身份不符：{key}")
        require(manifest["status"] == "interrupted" and manifest["reason"] == "signal",
                "应保留受控中止状态")
        summary = read_json(new, "runs/endurance/summary.json")
        rows = read_rows(new, "runs/endurance/frames.csv")
        maximum_box, maximum_score = compare_rows(rows, reference)
        updates = sum(row["phase"] == "measure" and row["operation"] == "track" for row in rows)
        require(updates == summary["valid_updates"], "正式更新计数不符")
        require(summary["failed_frames"] == 0, "存在失败帧")
        sequences = {}
        for row in rows:
            sequences.setdefault(row["repeat_id"], []).append(int(row["frame_index"]))
        for indices in sequences.values():
            require(indices == list(range(len(indices))), "轮次中有缺帧、重复或错序")
        spans = read_rows(new, "runs/endurance/spans.csv")
        span_seconds = (max(int(row["end_ns"]) for row in spans)
                        - min(int(row["start_ns"]) for row in spans)) / 1e9
        require(span_seconds >= 1800, "实际帧处理时间跨度不足 30 分钟")
        result = read_json(new, "logs/endurance-result.json")
        require(result["exit_code"] == 130, "不是授权的信号退出")
        cycles = [name for name in old.getnames()
                  if name.startswith("pack/runs/cycles/run-") and name.endswith("/frames.csv")]
        require(len(cycles) >= 20, "重复启停证据不足")
        for name in cycles:
            compare_rows(read_rows(old, name), reference)
        resources = read_rows(new, "logs/endurance-resources.csv")
        # 25 个 FD 对应模型、录像均已打开的稳定负载阶段；原始启动和收尾样本不丢弃。
        active = [row for row in resources if int(row["open_fds"]) == 25]
        ranges = {}
        for field in ("open_fds", "vmrss_kb", "vmhwm_kb", "threads", "cpu_temp_millic"):
            values = [int(row[field].split()[0]) for row in active]
            ranges[field] = [min(values), max(values)]
        report = {
            "status": "passed",
            "termination": "user_authorized_SIGINT",
            "benchmark_status": manifest["status"],
            "exit_code": result["exit_code"],
            "monitor_elapsed_seconds": result["elapsed_seconds"],
            "frame_span_seconds": span_seconds,
            "frames_compared": len(rows),
            "valid_updates": updates,
            "measured_initializations": summary["initializations"],
            "complete_measured_repeats": sum(
                len(indices) == 301 for key, indices in sequences.items() if key != "0"),
            "max_bbox_error": maximum_box,
            "max_score_error": maximum_score,
            "prior_cycles_rechecked": len(cycles),
            "resource_samples": len(resources),
            "active_resource_samples": len(active),
            "active_resource_ranges": ranges,
            "first_resource_sample": resources[0],
            "last_resource_sample": resources[-1],
            "device_memory_gap": result["device_memory_gap"],
            "records_sha256": hashlib.sha256(new_path.read_bytes()).hexdigest(),
        }
        print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

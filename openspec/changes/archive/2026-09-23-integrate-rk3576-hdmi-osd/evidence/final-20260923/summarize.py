"""从已结束的验收日志汇总性能；不把显示提交频率当作物理扫描频率。"""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parent
summary = {}
for mode in ("load-off", "endurance", "display-failure"):
    result = json.loads((root / f"{mode}-result.json").read_text())
    samples = json.loads((root / f"{mode}-resources.json").read_text())
    last_round = result["rounds"][-1]
    start = last_round["begin"]
    active = [sample for sample in samples if sample["t"] >= start["t"]]
    last = active[-1]
    transitions = []
    previous = None
    for sample in active:
        if sample["fds"] != previous:
            transitions.append({"seconds": sample["t"] - start["t"], "fds": sample["fds"]})
            previous = sample["fds"]
    idle_points = [item["idle"] for item in result["rounds"]]
    entry = {
        "pid": result["pid"],
        "exit_code": result["exit_code"],
        "display_failure": result["display_failure"],
        "rounds": len(result["rounds"]),
        "last_round_seconds": last_round["seconds"],
        "messages": last_round["messages"],
        "output_hz": last_round["messages"] / last_round["seconds"],
        "cpu_percent_one_core": 100 * (last["cpu_seconds"] - start["cpu_seconds"]) / (last["t"] - start["t"]),
        "rss_range_kib": [min(s["rss_kb"] for s in active), max(s["rss_kb"] for s in active)],
        "threads_active": sorted({s["threads"] for s in active}),
        "threads_idle": [s["threads"] for s in idle_points],
        "fds_idle": [s["fds"] for s in idle_points],
        "fd_transitions": transitions,
        "stop_seconds": [item["stop_observed_seconds"] for item in result["rounds"]],
        "exit_seconds": result["exit_seconds"],
    }
    log = (root / f"{mode}.log").read_text()
    counters = re.findall(r"\[INFO\] \[(\d+\.\d+)\].*OSD submitted=(\d+) replaced=(\d+) displayed=(\d+) commits=(\d+) rejected=(\d+)", log)
    # 取末尾约 600 秒连续区间，避开前 20 轮启停。
    if counters:
        end = counters[-1]
        selected = [c for c in counters if float(c[0]) >= float(end[0]) - min(600, last_round["seconds"] - 2)]
        first = selected[0]
        seconds = float(end[0]) - float(first[0])
        entry["last_counters"] = dict(zip(("submitted", "replaced", "displayed", "commits", "rejected"), map(int, end[1:])))
        if seconds > 0:
            entry["counter_window_seconds"] = seconds
            entry["new_display_hz"] = (int(end[3]) - int(first[3])) / seconds
            entry["completed_commit_hz"] = (int(end[4]) - int(first[4])) / seconds
    timing = re.findall(r"OSD submit_calls=(\d+) submit_mean_us=([\d.]+) submit_max_us=([\d.]+)", log)
    if timing:
        entry["submit_calls"], entry["submit_mean_us"], entry["submit_max_us"] = map(float, timing[-1])
    entry["unmeasured"] = ["物理屏幕扫描频率", "相机曝光到屏幕的端到端延迟", "显示冻结像素出现的精确延迟", "NPU 内部设备内存"]
    summary[mode] = entry
(root / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2))
print(json.dumps(summary, ensure_ascii=False, indent=2))

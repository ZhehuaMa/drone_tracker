"""仅验证录像副本的截断前缀，不修改原件或生成转换后视频。"""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "first-ts-session"
STRICT = "--strict-decode" in sys.argv
OUTPUT = ROOT / ("truncation-strict-evidence" if STRICT else "truncation-evidence")
OUTPUT.mkdir(exist_ok=True)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(case):
    name, offset, kind = case
    original = SOURCE / name
    with tempfile.NamedTemporaryFile(suffix=".ts", dir=OUTPUT) as copy:
        with original.open("rb") as source:
            remaining = offset
            while remaining:
                data = source.read(min(remaining, 1024 * 1024))
                if not data:
                    break
                copy.write(data)
                remaining -= len(data)
        copy.flush()
        # framecrc 只输出解码帧摘要，用于核验完整解码帧的最后时间。
        command = [
            "ffmpeg", "-hide_banner", "-v", "warning", "-threads", "1",
        ]
        if STRICT:
            command += ["-err_detect", "explode", "-fflags", "+discardcorrupt"]
        result = subprocess.run(command + [
            "-copyts", "-i", copy.name, "-map", "0:v:0", "-an",
            "-fps_mode", "passthrough", "-f", "framecrc", "-"
        ], capture_output=True, text=True, timeout=60)
    rows = [line for line in result.stdout.splitlines() if line and not line.startswith("#")]
    timebase = next((line for line in result.stdout.splitlines() if line.startswith("#tb")), None)
    log = OUTPUT / f"{name}-{offset}.stderr.txt"
    log.write_text(result.stderr)
    return {"source": name, "offset": offset, "boundary": kind,
            "strict_decode": STRICT,
            "exit_code": result.returncode, "decoded_frames": len(rows),
            "timebase": timebase, "last_decoded_frame": rows[-1] if rows else None,
            "decoder_diagnostics": log.name}


cases = {}
for video in sorted(SOURCE.glob("*.ts")):
    records = [json.loads(line) for line in video.with_suffix(".frames.jsonl").read_text().splitlines()]
    indices = sorted(set([0, 1, len(records) - 1] + [i * (len(records) - 1) // 12 for i in range(13)]))
    boundaries = [(records[i]["video_end"], "frame_or_segment") for i in indices]
    for line in (SOURCE / "commits.jsonl").read_text().splitlines():
        commit = json.loads(line)
        if commit["segment"] == int(video.stem):
            boundaries.append((commit["video_end"], "sync_commit"))
    for end, kind in boundaries:
        for delta in (-189, -1, 0):
            offset = end + delta
            if 0 < offset <= video.stat().st_size:
                cases[(video.name, offset)] = (video.name, offset, kind)

originals = {p.name: digest(p) for p in SOURCE.glob("*.ts")}
(OUTPUT / "source-sha256.json").write_text(json.dumps(originals, indent=2))
with (OUTPUT / "results.jsonl").open("x") as report:
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for result in pool.map(check, cases.values()):
            report.write(json.dumps(result) + "\n")
            report.flush()
assert originals == {p.name: digest(p) for p in SOURCE.glob("*.ts")}, "Original evidence changed"
print(f"Checked {len(cases)} offsets; originals unchanged", flush=True)

"""验证独立恢复检查器的损坏元数据识别、重复检查及证据不变性。"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def hashes(root):
    return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in root.glob("session-*/*") if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inspector", type=Path)
    parser.add_argument("new_directory", type=Path)
    args = parser.parse_args()
    root = args.new_directory.resolve()
    root.mkdir()  # 测试目录必须全新，不清理旧证据。
    interrupted = root / "session-interrupted"
    interrupted.mkdir()
    (interrupted / "session.json").write_text('{"test":true}\n')
    (interrupted / "events.jsonl").write_text("")
    # 这里只测试 TS 包结构。合成包不作为解码证据。
    packet = bytes([0x47]) + bytes(187)
    (interrupted / "000001.ts").write_bytes(packet * 3 + packet[:57])
    (interrupted / "000001.frames.jsonl").write_text('{"sequence":1}\n{"sequence":')
    (interrupted / "commits.jsonl").write_text(
        '{"segment":1,"video_end":752,"last_frame_us":1000}\n{"segment":')
    damaged = root / "session-damaged"
    damaged.mkdir()
    (damaged / "session.json").write_text('{"unterminated":')
    (damaged / "complete.json").write_text('{"outcome":')
    (damaged / "events.jsonl").write_text('{"bad":\n')
    (damaged / "commits.jsonl").write_text('{"segment":"invalid","video_end":1,"last_frame_us":1}\n')
    before = hashes(root)
    reports = []
    for index in range(2):
        report = root / f"inspection-{index}.jsonl"
        subprocess.run([str(args.inspector.resolve()), str(root), str(report)], check=True)
        rows = {row["session"]: row for row in map(json.loads, report.read_text().splitlines())}
        entry = rows["session-interrupted"]
        assert not entry["normal_marker_present"]
        assert entry["commit_tail_damaged"] and entry["complete_commit_records"] == 1
        assert entry["last_parseable_commit"]["video_end"] == 752
        assert not entry["last_parseable_commit"]["video_boundary_exists"]
        assert not entry["decoded"] and not entry["prior_sync_success_verified"]
        video = next(item for item in entry["files"] if item["name"] == "000001.ts")
        frames = next(item for item in entry["files"] if item["name"] == "000001.frames.jsonl")
        assert video["ts_packet_prefix_bytes"] == 564
        assert frames["tail_damaged"] and frames["complete_records"] == 1
        entry = rows["session-damaged"]
        assert not entry["manifest_valid_object"] and not entry["completion_valid_object"]
        assert entry["event_tail_damaged"] and entry["last_parseable_commit"] is None
        reports.append(rows)
    assert reports[0] == reports[1]
    collision = root / "inspection-0.jsonl"
    report_hash = hashlib.sha256(collision.read_bytes()).hexdigest()
    duplicate = subprocess.run([str(args.inspector.resolve()), str(root), str(collision)], check=False)
    assert duplicate.returncode != 0
    assert hashlib.sha256(collision.read_bytes()).hexdigest() == report_hash
    assert hashes(root) == before
    (root / "original-sha256.json").write_text(json.dumps(before, indent=2))
    print("Damaged metadata, commit boundary, repeat inspection and evidence preservation passed", flush=True)


if __name__ == "__main__":
    main()

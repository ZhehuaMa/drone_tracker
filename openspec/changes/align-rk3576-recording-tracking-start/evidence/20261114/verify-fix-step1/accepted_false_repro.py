"""Exact startup membership regression; sources and recordings are synthetic fixtures."""
import json
import pathlib
import subprocess
import sys
import tempfile


def main():
    program = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="startup-inspect-") as temporary:
        root = pathlib.Path(temporary)
        session = root / "session-test"
        session.mkdir()
        identity = dict(stream_id=71, tracking_round=2, request_generation=9,
                        capture_id=4294967313, timestamp_us=123, sequence=17)
        source = dict(identity, accepted=True)
        startup = dict(stream_id=71, tracking_round=2, request_generation=9,
                       outcome="accepted", first_yolo=source, first_initialization=None)
        report_id = 0

        def write(name, value):
            (session / name).write_text(json.dumps(value) + "\n")

        def inspect(verify=False):
            nonlocal report_id
            report_id += 1
            output = root / (str(report_id) + ".jsonl")
            subprocess.run([program, str(root), str(output)] + (["--verify-video"] if verify else []),
                           check=True, timeout=190)
            result = json.loads(output.read_text())
            assert "inspection_error" not in result, result
            return result["startup_coverage"]

        write("session.json", {key: identity[key] for key in
                              ("stream_id", "tracking_round", "request_generation")})
        write("camera.json", {})
        assert inspect()["first_yolo"] == "unknown"
        write("camera.json", dict(startup=dict(startup, first_yolo=dict(capture_id=17))))
        assert inspect()["first_yolo"] == "unknown"
        write("camera.json", dict(startup=dict(startup, first_yolo=dict(source, accepted=False))))
        assert inspect()["first_yolo"] == "failed"
        write("camera.json", dict(startup=startup))
        assert inspect()["first_yolo"] == "accepted"
        write("complete.json", dict(video_success=True))
        assert inspect()["first_yolo"] == "failed"  # Accepted but unwritten.
        write("000000.frames.jsonl", identity)
        result = inspect()
        assert result == dict(first_yolo="metadata-matched", first_initialization="not-applicable"), result
        write("000000.frames.jsonl", dict(identity, request_generation=8))
        assert inspect()["first_yolo"] == "failed"
        write("000000.frames.jsonl", dict(identity, capture_id=17))
        assert inspect()["first_yolo"] == "failed"  # Same wrapping sequence is insufficient.
        (session / "000000.frames.jsonl").write_text('{"capture_id":')
        assert inspect()["first_yolo"] == "failed"
        write("000000.frames.jsonl", identity)
        write("complete.json", dict(video_success=False))
        assert inspect()["first_yolo"] == "failed"
        write("complete.json", dict(video_success=True))
        startup["outcome"] = "unavailable"
        write("camera.json", dict(startup=startup))
        assert inspect()["first_yolo"] == "failed"
        startup["outcome"] = "accepted"
        write("camera.json", dict(startup=startup))
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "color=size=32x32:rate=1",
                        "-frames:v", "1", "-c:v", "libx264", "-f", "mpegts", str(session / "000000.ts")],
                       check=True, timeout=30)
        assert inspect(True)["first_yolo"] == "verified-written"
    print("Startup inspection identity, failure, compatibility and decoder checks passed")


if __name__ == "__main__":
    main()

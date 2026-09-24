"""通过测试专用动态库验证首帧前收尾与真实同步阻塞，不修改正式程序。"""

import argparse
import json
import os
from pathlib import Path
import time

from production_acceptance import Run, rclpy, worker_pid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--config", type=Path, default=Path("/opt/drone_tracker/config/camera.yaml"))
    parser.add_argument("--recordings", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--shim", type=Path, required=True)
    args = parser.parse_args()
    assert all(value.is_absolute() for value in vars(args).values())
    args.output.mkdir(exist_ok=True)
    root = Path("/home/hzhy/drone-recording-validation-20260923")
    repeat = root / "repeat-timestamps"
    block = root / "block-video-sync"
    assert not repeat.exists() and not block.exists(), "已有故障标记，拒绝覆盖"
    os.environ["LD_PRELOAD"] = str(args.shim)
    rclpy.init()
    try:
        for mode in ("warmup-cancel", "warmup-failure", "blocked-sync"):
            marker = block if mode == "blocked-sync" else repeat
            marker.touch(exist_ok=False)
            run = None
            error = None
            before = set(args.recordings.glob("session-*"))
            try:
                run = Run(args, mode, 1000)
                if mode == "blocked-sync":
                    row = run.start()
                    boxes = run.box_count
                    run.wait(lambda: any(text.startswith("Camera recording failed:")
                                         for _, text in run.events), timeout=12)
                    run.spin(1)
                    assert run.box_count > boxes, "同步阻塞后跟踪未继续"
                    row["boxes_during_fault"] = run.box_count - boxes
                    run.stop(row)
                else:
                    run.wait(lambda: run.trigger.get_subscription_count() > 0)
                    started = time.monotonic()
                    run.command("ON")
                    run.wait(lambda: worker_pid(run.process.pid) is not None)
                    if mode == "warmup-cancel":
                        run.spin(0.35)
                        run.stop({"on_us": int(started * 1000000), "expected_zero_frames": True})
                    else:
                        run.wait(lambda: any(t >= started and text == "Drone Tracker Stopped"
                                             for t, text in run.events), timeout=8)
                        run.spin(0.5)
                    assert run.box_count == 0, "预热观察帧进入跟踪输出"
                    sessions = set(args.recordings.glob("session-*")) - before
                    assert len(sessions) == 1
                    session = sessions.pop()
                    assert not list(session.glob("*.ts")), "零帧结果创建了占位视频"
                    complete = json.loads((session / "complete.json").read_text())
                    expected = "camera_warmup_failed" if mode == "warmup-failure" else "cancelled_before_first_frame"
                    assert complete["outcome"] == expected, complete
                    assert complete["frames"] == 0 and not complete["video_success"]
                    run.result["zero_frame_completion"] = complete
                marker.unlink()
                run.window(3)  # 同一节点在移除故障后必须能开始新一轮。
            except Exception as caught:
                error = caught
            finally:
                marker.unlink(missing_ok=True)
                if run is not None:
                    run.close(error)
            if error:
                raise error
            print(mode + " completed", flush=True)
    finally:
        repeat.unlink(missing_ok=True)
        block.unlink(missing_ok=True)
        rclpy.shutdown()


if __name__ == "__main__":
    main()

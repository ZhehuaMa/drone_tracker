"""核对固定原片的两次回放，并抽取首、中、尾帧对照实际输入及模型输出。"""

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

import cv2
import numpy as np
from rknnlite.api import RKNNLite

from compare_outputs import adapt, compare


def main():
    root = Path(sys.argv[1])
    model = sys.argv[2]
    video = root / "V_DRONE_051.mp4"
    runs = [
        [line for line in (root / f"video-run-{i}.log").read_text().splitlines() if line.startswith("frame=")]
        for i in (1, 2)
    ]
    assert len(runs[0]) == 309 and runs[0] == runs[1]
    assert [int(re.match(r"frame=(\d+)", line)[1]) for line in runs[0]] == list(range(1, 310))
    capture = cv2.VideoCapture(str(video))
    report = {
        "source": "https://github.com/DroneDetectionThesis/Drone-detection-dataset/blob/"
        "442d0c6750708cc221636f7a1fd31cbd8cb0aeb8/Data/Video_V/V_DRONE_051.mp4",
        "sha256": hashlib.sha256(video.read_bytes()).hexdigest(),
        "width": capture.get(cv2.CAP_PROP_FRAME_WIDTH),
        "height": capture.get(cv2.CAP_PROP_FRAME_HEIGHT),
        "fps": capture.get(cv2.CAP_PROP_FPS),
        "frames": len(runs[0]),
        "repeat_equal": True,
        "hits": sum("hit=1" in line for line in runs[0]),
        "samples": [],
    }
    for number in (1, 105, 309):
        capture.set(cv2.CAP_PROP_POS_FRAMES, number - 1)
        ok, frame = capture.read()
        assert ok
        prefix = root / f"video-frame-{number}"
        png = prefix.with_suffix(".png")
        assert cv2.imwrite(str(png), frame)
        command = [str(root / "rk3576_yolo_probe"), "--model", model]
        subprocess.run(command + ["--mode", "region", "--image", str(png), "--dump", str(prefix)],
                       check=True, stdout=subprocess.DEVNULL)
        detected = subprocess.check_output(command + ["--mode", "detect", "--target", "2", "--image", str(png)],
                                           text=True)
        line = next(line for line in detected.splitlines() if line.startswith("frame="))
        assert line.split(" ", 1)[1] == runs[0][number - 1].split(" ", 1)[1]
        pixels = np.fromfile(str(prefix) + ".input.u8", np.uint8).reshape(1, 640, 640, 3)
        runtime = RKNNLite(verbose=False)
        assert runtime.load_rknn(model) == 0 and runtime.init_runtime() == 0
        try:
            raw = runtime.inference(inputs=[pixels], data_format=["nhwc"])
        finally:
            runtime.release()
        raw_checks = []
        for index, reference in enumerate(raw):
            actual = np.fromfile(str(prefix) + f".output{index}.f32", np.float32).reshape(reference.shape)
            raw_checks.append(compare(actual, reference, 1e-5))
        actual = np.fromfile(str(prefix) + ".prediction.f32", np.float32).reshape(1, 8, 8400)
        converted = adapt(raw)
        adapter_check = compare(actual, converted)
        assert all(item["finite"] and item["failed_elements"] == 0 for item in raw_checks + [adapter_check])
        # 独立按单一目标通道筛选。全图有合格候选时直接核对视频最终框。
        boxes, scores = [], []
        for index in range(8400):
            cx, cy, width, height = converted[0, :4, index]
            score = converted[0, 6, index]
            if score >= 0.5 and width / 640 <= 0.8 and height / 512 <= 0.8:
                boxes.append([cx - width / 2, cy - 64 - height / 2, width, height])
                scores.append(float(score))
        final_reference = None
        if boxes:
            kept = np.asarray(cv2.dnn.NMSBoxes(boxes, scores, 0.5, 0.5)).reshape(-1)
            best = max(kept, key=lambda index: scores[index])
            match = re.search(r"class=2 score=([\d.e+-]+) box=\[([\d.e+-]+) x ([\d.e+-]+) from \(([\d.e+-]+), ([\d.e+-]+)\)\]", line)
            assert match is not None
            score, width, height, x, y = map(float, match.groups())
            assert abs(score - scores[best]) < 1e-5
            assert np.allclose([x, y, width, height], boxes[best], atol=1e-3, rtol=1e-5)
            final_reference = {"score": scores[best], "box": boxes[best]}
        report["samples"].append({"frame": number, "png_sha256": hashlib.sha256(png.read_bytes()).hexdigest(),
                                  "raw": raw_checks, "adapter": adapter_check, "detected": line,
                                  "full_frame_reference": final_reference})
    capture.release()
    assert any(sample["full_frame_reference"] is not None for sample in report["samples"])
    (root / "video-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS", "frames", report["frames"], "hits", report["hits"], "repeat_equal", report["repeat_equal"])


if __name__ == "__main__":
    main()

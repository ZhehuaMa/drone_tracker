"""固定四类别验收模型的数值对照；参数为板上输出目录、RKNN、原 ONNX、六输出 ONNX。"""

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import onnxruntime as ort
from rknnlite.api import RKNNLite


def compare(actual, reference, atol=1e-4, rtol=1e-5):
    """统计逐元素容差，保持失败可见，不扩大容差。"""
    delta = np.abs(actual - reference)
    return {
        "finite": bool(np.isfinite(actual).all() and np.isfinite(reference).all()),
        "max_abs": float(delta.max()),
        "failed_elements": int(np.count_nonzero(delta > atol + rtol * np.abs(reference))),
    }


def adapt(outputs):
    """双精度独立参考，按尺度及行优先顺序计算全部候选。"""
    result = []
    for index, (size, stride) in enumerate([(80, 8), (40, 16), (20, 32)]):
        logits = outputs[2 * index].astype(np.float64).reshape(1, 4, 16, size, size)
        weights = np.exp(logits - logits.max(axis=2, keepdims=True))
        distance = (weights * np.arange(16).reshape(1, 1, 16, 1, 1)).sum(axis=2) / weights.sum(axis=2)
        yy, xx = np.mgrid[:size, :size]
        center = np.stack([xx + 0.5, yy + 0.5])[None]
        low, high = center - distance[:, :2], center + distance[:, 2:]
        boxes = np.concatenate([(low + high) * 0.5 * stride, (high - low) * stride], axis=1)
        probability = 1 / (1 + np.exp(-outputs[2 * index + 1].astype(np.float64)))
        result.append(np.concatenate([boxes, probability], axis=1).reshape(1, 8, -1))
    return np.concatenate(result, axis=2)


def main():
    root, model, original_path, six_path = sys.argv[1:]
    root = Path(root)
    runtime = RKNNLite(verbose=False)
    assert runtime.load_rknn(model) == 0
    assert runtime.init_runtime() == 0
    report = {}
    try:
        for name in ["constant-0", "constant-114", "constant-255", "image"]:
            pixels = np.fromfile(root / f"{name}.input.u8", np.uint8).reshape(1, 640, 640, 3)
            outputs = runtime.inference(inputs=[pixels], data_format=["nhwc"])
            actual = [
                np.fromfile(root / f"{name}.output{i}.f32", np.float32).reshape(value.shape)
                for i, value in enumerate(outputs)
            ]
            prediction = np.fromfile(root / f"{name}.prediction.f32", np.float32).reshape(1, 8, 8400)
            report[name] = {
                "raw": [compare(a, b, 1e-5) for a, b in zip(actual, outputs)],
                "adapter": compare(prediction, adapt(outputs)),
            }
    finally:
        runtime.release()
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    original = ort.InferenceSession(original_path, options, providers=["CPUExecutionProvider"])
    six = ort.InferenceSession(six_path, options, providers=["CPUExecutionProvider"])
    pixels = np.fromfile(root / "image.input.u8", np.uint8).reshape(1, 640, 640, 3)
    network = pixels.transpose(0, 3, 1, 2).astype(np.float32) / 255
    raw = six.run(None, {six.get_inputs()[0].name: network})
    merged = original.run(None, {original.get_inputs()[0].name: network})[0]
    for index, value in enumerate(raw):
        value.astype(np.float32).tofile(root / f"onnx.output{index}.f32")
    subprocess.run([str(root / "adapter_dump"), str(root / "onnx")], check=True)
    cpp = np.fromfile(root / "onnx.cpp.f32", np.float32).reshape(merged.shape)
    report["onnx_cpp"] = compare(cpp, merged)
    report["onnx_reference"] = compare(adapt(raw), merged)
    quantized = np.fromfile(root / "image.prediction.f32", np.float32).reshape(merged.shape)
    report["quantization_observation"] = {
        "boxes_max_abs": float(np.max(np.abs(quantized[:, :4] - merged[:, :4]))),
        "probability_max_abs": float(np.max(np.abs(quantized[:, 4:] - merged[:, 4:]))),
    }
    (root / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    checks = [report["onnx_cpp"], report["onnx_reference"]]
    for name in ["constant-0", "constant-114", "constant-255", "image"]:
        checks.extend(report[name]["raw"] + [report[name]["adapter"]])
    assert all(check["finite"] and check["failed_elements"] == 0 for check in checks), report
    print("PASS: 原始输出、独立适配参考与实际 C++ ONNX 对照均满足既定容差")


if __name__ == "__main__":
    main()

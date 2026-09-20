"""对照 C++ region 导出的六输出；运行前容差依据见 validation.md。"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("onnx", type=Path)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    prefix = str(args.prefix)
    data = Path(prefix + ".input.u8").read_bytes()
    pixels = np.frombuffer(data, dtype=np.uint8).reshape(1, 640, 640, 3)
    assert np.unique(pixels).size > 1, "必须使用非均匀输入"
    assert np.array_equal(pixels[..., 0], pixels[..., 1])
    assert np.array_equal(pixels[..., 1], pixels[..., 2])
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    session = ort.InferenceSession(str(args.onnx), options, providers=["CPUExecutionProvider"])
    expected_shapes = [(1, c, side, side) for side in (80, 40, 20) for c in (64, 1)]
    outputs = session.get_outputs()
    assert len(outputs) == 6
    assert [tuple(out.shape) for out in outputs] == expected_shapes
    reference = session.run(None, {
        session.get_inputs()[0].name: np.ascontiguousarray(pixels.transpose(0, 3, 1, 2), dtype=np.float32) / 255
    })
    results = []
    for i, expected in enumerate(reference):
        actual = np.fromfile(prefix + f".output{i}.f32", dtype=np.float32).reshape(expected.shape)
        error = np.abs(actual.astype(np.float64) - expected.astype(np.float64))
        relative = error / np.maximum(np.abs(expected), 1e-3)
        finite = bool(np.isfinite(actual).all() and np.isfinite(expected).all())
        passed = finite and bool(np.all(error <= 0.5 + 0.02 * np.abs(expected))) and float(error.mean()) <= 0.1
        results.append(dict(index=i, name=outputs[i].name, shape=list(expected.shape),
                            max_abs=float(error.max()), mean_abs=float(error.mean()),
                            max_relative=float(relative.max()), mean_relative=float(relative.mean()), passed=passed))
    report = dict(onnx_sha256=hashlib.sha256(args.onnx.read_bytes()).hexdigest(),
                  input_sha256=hashlib.sha256(data).hexdigest(), input_min=int(pixels.min()),
                  input_max=int(pixels.max()), input_unique=int(np.unique(pixels).size),
                  atol=0.5, rtol=0.02, mean_abs_limit=0.1, relative_denominator_floor=1e-3,
                  outputs=results, passed=all(row["passed"] for row in results))
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

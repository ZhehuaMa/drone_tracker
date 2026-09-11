"""独立 RKNNLite 同输入参考：先固定重复误差和容差，再读取 C++ 结果。"""

import hashlib
import json
from pathlib import Path
import sys

import numpy as np
from rknnlite.api import RKNNLite


def read(path):
    return np.fromfile(path, dtype=np.float32)


def difference(actual, expected, atol, rtol):
    actual = np.asarray(actual).reshape(-1)
    expected = np.asarray(expected).reshape(-1)
    if actual.shape != expected.shape:
        raise ValueError(f'形状不匹配：{actual.shape} / {expected.shape}')
    finite = bool(np.isfinite(actual).all() and np.isfinite(expected).all())
    delta = np.abs(actual.astype(np.float64) - expected.astype(np.float64))
    return {'finite': finite, 'elements': actual.size, 'max_abs': float(delta.max()),
            'failed_elements': int(np.count_nonzero(delta > atol + rtol * np.abs(expected))),
            'passed': finite and bool(np.all(delta <= atol + rtol * np.abs(expected)))}


def main():
    output = Path(sys.argv[1])
    paths = sys.argv[2:5]
    crops = [read(output / f'{name}.f32').reshape(1, 3, side, side)
             for name, side in [('template', 127), ('search', 255)]]
    images = [np.ascontiguousarray(crop.transpose(0, 2, 3, 1)) for crop in crops]
    runtimes = []
    try:
        for path in paths:
            runtime = RKNNLite(verbose=False)
            runtimes.append(runtime)
            if runtime.load_rknn(path) != 0 or runtime.init_runtime() != 0:
                raise RuntimeError(f'参考模型加载失败：{path}')
        references = []
        for _ in range(5):
            z = runtimes[0].inference(inputs=[images[0]], data_format=['nhwc'])[0]
            x = runtimes[1].inference(inputs=[images[1]], data_format=['nhwc'])[0]
            features = [np.ascontiguousarray(value.transpose(0, 2, 3, 1)) for value in [z, x]]
            head = runtimes[2].inference(inputs=features, data_format=['nhwc', 'nhwc'])
            references.append([z, x, *head])
        # 先测重复误差，若不满足预先规定的噪声上限则停止，不根据后端差异放宽容差。
        repeat = [difference(references[i][j], references[0][j], 0, 0)
                  for i in range(1, 5) for j in range(4)]
        maximum = max(item['max_abs'] for item in repeat)
        if not all(item['finite'] for item in repeat) or maximum > 1e-5:
            raise RuntimeError(f'参考重复误差超过预定上限：{maximum}')
        tolerance = {'reference_repeats': 5, 'repeat_max_abs': maximum, 'atol': 1e-5, 'rtol': 1e-5,
                     'rule': '|actual-reference| <= atol + rtol*|reference|'}
        (output / 'tolerance.json').write_text(json.dumps(tolerance, indent=2) + '\n')
        # 从此处才读取后端执行结果。独立参考不调用 C++ 的模型会话或重排函数。
        z, x, cls, loc = references[0]
        expected = {'template-input0': images[0], 'search-input0': images[1],
                    'template-output0': z, 'search-output0': x,
                    'head-input0': z.transpose(0, 2, 3, 1), 'head-input1': x.transpose(0, 2, 3, 1),
                    'head-output0': cls, 'head-output1': loc, 'classification': cls, 'location': loc}
        comparisons = {f'{iteration}-{name}': difference(read(output / f'{iteration}-{name}.f32'), value, 1e-5, 1e-5)
                       for iteration in range(5) for name, value in expected.items()}
        backend_repeat = {f'{iteration}-{name}': difference(read(output / f'{iteration}-{name}.f32'),
                          read(output / f'0-{name}.f32'), 0, 0)
                          for iteration in range(1, 5) for name in expected}
        report = {'tolerance': tolerance, 'comparisons': comparisons,
                  'backend_repeat_max_abs': max(item['max_abs'] for item in backend_repeat.values()),
                  'hashes': {str(path): hashlib.sha256(Path(path).read_bytes()).hexdigest()
                             for path in [*paths, output / 'template.f32', output / 'search.f32']},
                  'fractional_pixels': [int(np.count_nonzero(crop != np.round(crop))) for crop in crops],
                  'passed': all(item['passed'] for item in comparisons.values())}
        (output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({'passed': report['passed'], 'comparisons': len(comparisons),
                          'max_abs': max(item['max_abs'] for item in comparisons.values()),
                          'repeat_max_abs': maximum, 'backend_repeat_max_abs': report['backend_repeat_max_abs'],
                          'fractional_pixels': report['fractional_pixels']}))
        return 0 if report['passed'] else 1
    finally:
        for runtime in reversed(runtimes):
            runtime.release()


if __name__ == '__main__':
    sys.exit(main())

"""在开发板比较提取前后 YOLO 的同输入结果，保存可复核摘要。"""

import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    output = Path(sys.argv[1]).resolve()
    baseline = Path('/home/firefly/drone_tracker_test/yolo_acceptance_20260910')
    model = Path('/home/firefly/drone_tracker_test/models/yolov8n-model_zoo-int8_fp16-multi_cls.rknn')
    image = baseline / 'video-frame-1.png'
    programs = {'old': baseline / 'rk3576_yolo_probe', 'new': output / 'rk3576_yolo_probe'}
    report = {'hashes': {}, 'cases': {}, 'raw_equal': {}, 'video': {}}
    for path in [*programs.values(), model, image, baseline / 'V_DRONE_051.mp4']:
        report['hashes'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    cases = {
        'image': ['--mode', 'detect', '--target', '2', '--image', str(image)],
        'missing-model': ['--mode', 'query'],
        'missing-image': ['--mode', 'detect', '--target', '2', '--image', str(output / 'absent.png')],
        'invalid-target': ['--mode', 'detect', '--target', '4', '--image', str(image)],
        'invalid-roi': ['--mode', 'detect', '--target', '2', '--image', str(image), '--roi', '0', '0', '-1', '1'],
        'wrong-model': ['--mode', 'query'],
    }
    wrong_model = Path('/home/firefly/benchmarks/nanotrackv3_evaluation_runs/nanotrackv3-official-20260910/models/components/int8/rknn_int8/head.rknn')
    for case, args in cases.items():
        entries = {}
        for version, program in programs.items():
            selected = output / 'absent.rknn' if case == 'missing-model' else wrong_model if case == 'wrong-model' else model
            result = subprocess.run([str(program), '--model', str(selected), *args], capture_output=True, text=True, timeout=60)
            (output / f'{version}-{case}.log').write_text(result.stdout + result.stderr)
            entries[version] = {'exit': result.returncode, 'frames': [s for s in result.stdout.splitlines() if s.startswith('frame=')]}
        entries['passed'] = entries['old'] == entries['new'] and entries['new']['exit'] == (0 if case == 'image' else 1)
        report['cases'][case] = entries
    for version, program in programs.items():
        prefix = output / f'{version}-raw'
        result = subprocess.run([str(program), '--model', str(model), '--mode', 'region', '--image', str(image), '--dump', str(prefix)], capture_output=True, text=True, timeout=60)
        (output / f'{version}-raw.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'{version} 原始输出导出失败')
    for suffix in ['input.u8', *[f'output{i}.f32' for i in range(6)], 'prediction.f32']:
        report['raw_equal'][suffix] = (output / f'old-raw.{suffix}').read_bytes() == (output / f'new-raw.{suffix}').read_bytes()
    frames = {v: [s for s in (output / f'{v}-video.log').read_text().splitlines() if s.startswith('frame=')] for v in programs}
    report['video'] = {'old_frames': len(frames['old']), 'new_frames': len(frames['new']), 'equal': frames['old'] == frames['new'], 'hits': sum('hit=1' in s for s in frames['new'])}
    report['passed'] = all(c['passed'] for c in report['cases'].values()) and all(report['raw_equal'].values()) and report['video']['equal'] and len(frames['new']) == 309
    (output / 'yolo-regression.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())

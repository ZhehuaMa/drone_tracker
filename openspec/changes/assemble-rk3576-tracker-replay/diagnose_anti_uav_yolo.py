"""用既有 YOLO probe 检查四个可见帧的全图类别分数，不修改验收阈值。"""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import struct
import math


def main():
    phase = Path(sys.argv[1])
    output = phase / 'yolo-diagnostic'
    output.mkdir()
    probe = Path('/home/firefly/drone_tracker_test/yolo_acceptance_20260910/rk3576_yolo_probe')
    model = '/home/firefly/drone_tracker_test/models/yolov8n-model_zoo-int8_fp16-multi_cls.rknn'
    result = {'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest(), 'frames': {}}
    for index in (0, 100, 360, 420):
            image = output / f'frame-{index}.png'
            subprocess.run(['ffmpeg', '-nostdin', '-n', '-v', 'error', '-i', '/home/firefly/RGB.mp4',
                            '-vf', f'select=eq(n\\,{index})', '-frames:v', '1', str(image)], check=True)
            prefix = output / f'frame-{index}'
            command = [str(probe), '--model', model, '--mode', 'region', '--image', str(image), '--dump', str(prefix)]
            with (output / f'frame-{index}.log').open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
            prediction = struct.unpack('<67200f', Path(str(prefix) + '.prediction.f32').read_bytes())
            assert all(math.isfinite(v) for v in prediction)
            result['frames'][index] = {'command': command, 'max_probability_per_class': [
                max(prediction[k * 8400:(k + 1) * 8400]) for k in range(4, 8)]}
    (output / 'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()

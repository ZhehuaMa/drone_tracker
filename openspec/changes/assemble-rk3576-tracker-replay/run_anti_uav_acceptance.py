"""补测真实户外离开返回场景及 FP16 无目标矩阵，标注不传给跟踪器。"""

import hashlib
from pathlib import Path
import sys

from board_acceptance import configuration, run_one, write_json


def main():
    root = Path(sys.argv[1]).resolve()
    phase = root / 'anti-uav-20260914'
    phase.mkdir()
    write_json(phase / 'identity.json', {
        str(p): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (Path(__file__), root / 'board_acceptance.py',
                  root / 'rk3576_tracker_replay', Path('/home/firefly/RGB.mp4'),
                  Path('/home/firefly/RGB_label.json'))
    })
    for precision, scene in [('fp16', 'anti-uav'), ('int8', 'anti-uav'), ('fp16', 'bird')]:
        for repeat, dcf in enumerate((1, 0, 1), 1):
            name = f'{precision}-{scene}-dcf{dcf}-{repeat:03d}'
            edits = None
            if scene == 'anti-uav':
                edits = {
                    '/home/firefly/drone_tracker_test/yolo_acceptance_20260910/V_DRONE_051.mp4': '/home/firefly/RGB.mp4',
                    'fps: 30.0': 'fps: 20.0',
                    'max_frames: 0': 'max_frames: 500',
                    'cx: 320.0': 'cx: 960.0',
                    'cy: 256.0': 'cy: 540.0',
                    '# 固定回归配置：640×512、30 FPS 的 V_DRONE_051 原录像。': '# 户外回归：1920×1080、20 FPS，原录像前 500 帧；主点取图像中心。',
                }
            config = configuration(root, phase, name, precision,
                                   'drone' if scene == 'anti-uav' else scene, dcf, edits)
            result = run_one(root, phase, name, config)
            assert result['exit_code'] == 0
            assert result['end_reason'] == ('frame_limit' if scene == 'anti-uav' else 'eof')
    write_json(phase / 'complete.json', {'execution_complete': True, 'quality_evaluated': False})


if __name__ == '__main__':
    main()

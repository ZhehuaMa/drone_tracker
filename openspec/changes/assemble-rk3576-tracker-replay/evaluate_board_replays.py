"""离线评估完整回放；标注只用于评估，不参与跟踪初始化或修正。

用法：python3 evaluate_board_replays.py ROOT
ROOT 为板上独立验收目录。逐帧框误差另存 CSV，缺失框计入全帧 IoU=0，
中心误差仅统计有效预测，避免用任意坐标代表未捕获目标。
"""

import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys


def overlap(prediction, truth):
    cx, cy, w, h = prediction
    tx, ty, tw, th = truth
    intersection = max(0, min(cx + w / 2, tx + tw / 2) - max(cx - w / 2, tx - tw / 2)) * max(
        0, min(cy + h / 2, ty + th / 2) - max(cy - h / 2, ty - th / 2))
    return intersection / (w * h + tw * th - intersection), math.hypot(cx - tx, cy - ty)


def mean(values):
    return statistics.mean(values) if values else None


def main():
    root = Path(sys.argv[1]).resolve()
    reports = sorted((root / 'runs').glob('*/run.json')) + sorted((root / 'continued-matrix').glob('*/run.json'))
    labels_root = Path('/home/firefly/Desktop/drone_yolo_dataset/labels')
    annotations = {}
    hashes = {}
    for path in labels_root.glob('*/V_DRONE_051_frame_*.txt'):
        index = int(path.stem.split('_')[-1]) - 1
        assert index not in annotations
        entries = [list(map(float, line.split())) for line in path.read_text().splitlines() if line.split() and line.split()[0] == '2']
        assert len(entries) == 1
        annotations[index] = [v * scale for v, scale in zip(entries[0][1:], (640, 512, 640, 512))]
        hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    assert len(annotations) == 309
    result = {'notes': [
        '数据集标注从 1 计数，回放从 0 计数；按原帧号匹配。',
        '全帧平均 IoU 将无有效框记为 0；中心误差只统计有效框，并同时报告覆盖率。',
        'AirSim 无逐帧真值，只报告状态，不能把状态变化当作正确恢复的证明。',
        'FP16 仅已有 DCF 开启首轮；不补跑用户暂缓的 7.2，比较范围因此受限。',
        '无已批准的质量或部署性能门槛，不据此宣称达标。'], 'annotation_sha256': hashes, 'runs': {}}
    output = root / 'quality-per-frame.csv'
    with output.open('w') as stream:
        writer = csv.writer(stream)
        writer.writerow(['run', 'frame_index', 'box', 'valid', 'iou', 'center_error_px'])
        for path in reports:
            report = json.loads(path.read_text())
            rows = list(csv.DictReader((path.parent / 'frames.csv').open()))
            video = Path(report['configuration']['input']['video']).name
            summary = {k: report[k] for k in ('sha256', 'state_counts', 'state_transitions', 'end_reason', 'timings')}
            summary['frames'] = len(rows)
            summary['tracking_fraction'] = sum(row['tracking'] == '1' for row in rows) / len(rows)
            summary['first_capture_video_seconds'] = next((int(row['frame_index']) / 30 for row in rows if row['state'] == '1'), None)
            summary['captures_video_seconds'] = [int(t['frame_index']) / 30 for t in report['state_transitions'] if t['state'] == 1]
            summary['lost_video_seconds'] = [int(t['frame_index']) / 30 for t in report['state_transitions'] if t['state'] == 3]
            if video == 'V_DRONE_051.mp4':
                assert len(rows) == 309
                summary['boxes'] = {}
                for prefix in ('display', 'realtime', 'nano'):
                    all_iou, valid_iou, errors = [], [], []
                    for row in rows:
                        index = int(row['frame_index'])
                        valid = row[prefix + '_valid'] == '1'
                        iou, error = 0, None
                        if valid:
                            if prefix == 'nano':
                                x, y, w, h = [float(row['nano_' + k]) for k in ('x', 'y', 'width', 'height')]
                                box = [x + w / 2, y + h / 2, w, h]
                            else:
                                box = [float(row[prefix + '_' + k]) for k in ('cx', 'cy', 'width', 'height')]
                            iou, error = overlap(box, annotations[index])
                            valid_iou.append(iou)
                            errors.append(error)
                        all_iou.append(iou)
                        writer.writerow([path.parent.name, index, prefix, int(valid), iou, error])
                    summary['boxes'][prefix] = {'valid_frames': len(errors), 'annotated_frames': len(rows),
                        'mean_iou_all_frames': mean(all_iou), 'mean_iou_valid_frames': mean(valid_iou),
                        'mean_center_error_valid_px': mean(errors), 'zero_iou_valid_frames': sum(v == 0 for v in valid_iou)}
            result['runs'][path.parent.name] = summary
    (root / 'quality-summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({name: {'frames': r['frames'], 'capture': r['first_capture_video_seconds'], 'boxes': r.get('boxes')}
                      for name, r in result['runs'].items()}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

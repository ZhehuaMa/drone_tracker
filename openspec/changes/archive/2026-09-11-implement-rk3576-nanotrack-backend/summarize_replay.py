"""统计灰度回放；标注按文件中的原帧号匹配，明确报告覆盖率。"""

import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys


def main():
    output = Path(sys.argv[1])
    name = sys.argv[2] if len(sys.argv) > 2 else 'int8'
    rows = list(csv.DictReader((output / f'replay-{name}.csv').open()))
    labels = Path('/home/firefly/Desktop/drone_yolo_dataset/labels')
    comparisons = []
    label_hashes = {}
    for row in rows[1:]:
        # probe 的首帧为 0；数据集的原帧号从 1 开始。
        index = int(row['frame']) + 1
        matches = list(labels.glob(f'*/V_DRONE_051_frame_{index:06d}.txt'))
        if len(matches) != 1 or row['success'] != '1':
            continue
        path = matches[0]
        targets = [list(map(float, line.split())) for line in path.read_text().splitlines() if line.split() and line.split()[0] == '2']
        if len(targets) != 1:
            continue
        _, cx, cy, w, h = targets[0]
        truth = [(cx - w / 2) * 640, (cy - h / 2) * 512, w * 640, h * 512]
        predicted = [float(row[key]) for key in ('x', 'y', 'w', 'h')]
        x, y, pw, ph = predicted
        tx, ty, tw, th = truth
        intersection = max(0, min(x + pw, tx + tw) - max(x, tx)) * max(0, min(y + ph, ty + th) - max(y, ty))
        iou = intersection / (pw * ph + tw * th - intersection)
        center_error = math.hypot(x + pw / 2 - tx - tw / 2, y + ph / 2 - ty - th / 2)
        comparisons.append({'frame': int(row['frame']), 'iou': iou, 'center_error_px': center_error})
        label_hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    times = sorted(float(row['elapsed_ms']) for row in rows[1:])
    confidence = [float(row['confidence']) for row in rows[1:] if row['success'] == '1']
    summary = {
        'frames': len(rows), 'search_frames': len(rows) - 1,
        'successes': sum(row['success'] == '1' for row in rows[1:]),
        'elapsed_mean_ms': statistics.mean(times),
        'elapsed_p95_ms': times[math.ceil(len(times) * 0.95) - 1],
        'confidence_mean': statistics.mean(confidence) if confidence else None,
        'annotation_matches': len(comparisons),
        'mean_iou': statistics.mean(v['iou'] for v in comparisons) if comparisons else None,
        'mean_center_error_px': statistics.mean(v['center_error_px'] for v in comparisons) if comparisons else None,
        'zero_overlap_frames': sum(v['iou'] == 0 for v in comparisons),
        'last_matched_frame': comparisons[-1] if comparisons else None,
        'label_hashes': label_hashes,
        'per_frame': comparisons,
    }
    (output / f'replay-{name}-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k not in ('per_frame', 'label_hashes')}, indent=2))


if __name__ == '__main__':
    main()

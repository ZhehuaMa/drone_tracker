"""按原始零起点帧号评估户外回放；缺失预测的可见帧 IoU 记零。"""

import csv
import json
from pathlib import Path
import statistics
import sys

from evaluate_board_replays import overlap


def main():
    phase = Path(sys.argv[1])
    labels = json.loads(Path(sys.argv[2]).read_text())
    results = {}
    for path in sorted(phase.glob('*/run.json')):
        report = json.loads(path.read_text())
        rows = list(csv.DictReader((path.parent / 'frames.csv').open()))
        assert [int(r['frame_index']) for r in rows] == list(range(len(rows)))
        assert all(r['consumed'] == '1' for r in rows)
        item = {k: report[k] for k in ('state_counts', 'state_transitions', 'timings', 'sha256', 'end_reason')}
        item['frames'] = len(rows)
        item['wall_seconds'] = report['elapsed_wall_ms'] / 1000
        if 'anti-uav' in path.parent.name:
            assert len(rows) == 500
            assert all(int(r['timestamp_us']) == 1000000 + i * 50000 for i, r in enumerate(rows))
            item['segments'] = {}
            for name, start, end in [('before', 0, 214), ('absent', 214, 339), ('returned', 339, 500)]:
                ious, errors = [], []
                valid_count = 0
                for i in range(start, end):
                    row = rows[i]
                    valid = row['display_valid'] == '1'
                    valid_count += valid
                    if not labels['exist'][i]:
                        continue
                    iou = 0
                    if valid:
                        x, y, w, h = labels['gt_rect'][i]
                        pred = [float(row['display_' + k]) for k in ('cx', 'cy', 'width', 'height')]
                        iou, error = overlap(pred, [x + w / 2, y + h / 2, w, h])
                        errors.append(error)
                    ious.append(iou)
                item['segments'][name] = {
                    'frames': end - start, 'valid_display_frames': valid_count,
                    'mean_iou_visible_frames': statistics.mean(ious) if ious else None,
                    'mean_center_error_valid_px': statistics.mean(errors) if errors else None,
                }
        results[path.parent.name] = item
    (phase / 'quality-summary.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(results, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

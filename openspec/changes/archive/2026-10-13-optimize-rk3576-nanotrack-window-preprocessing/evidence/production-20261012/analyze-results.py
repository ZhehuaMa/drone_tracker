import csv
import hashlib
import json
import math
import pathlib
import statistics

root = pathlib.Path(__file__).parent / 'board-results'
frames = list(csv.DictReader((root / 'frames.csv').open()))
intervals = list(csv.DictReader((root / 'intervals.csv').open()))
batches = {}
for row in frames:
    key = tuple(int(row[name]) for name in ('warmup', 'pair', 'order', 'legacy'))
    batches.setdefault(key, []).append(row)
assert len(batches) == 24 and len(frames) == 600
for key, rows in batches.items():
    assert len(rows) == 25
    warmup, pair, order, legacy = key
    assert warmup == (pair < 2) and legacy == ((pair + order) % 2 == 0)
    assert [int(row['frame']) for row in rows] == list(range(25))
    assert all(row['success'] == '1' and row['initialized'] == '1' for row in rows)
    for role, count in [('template', 1), ('search', 24), ('head', 24)]:
        assert sum(int(row[role + '_calls']) for row in rows) == count
    assert len({row['batch_ns'] for row in rows}) == 1
for pair in range(12):
    a = next(rows for key, rows in batches.items() if key[1] == pair and key[3] == 1)
    b = next(rows for key, rows in batches.items() if key[1] == pair and key[3] == 0)
    for left, right in zip(a, b):
        for field in ('frame', 'timestamp_us', 'success', 'initialized', 'x', 'y', 'w', 'h', 'score'):
            assert left[field] == right[field]
spans = {}
for row in intervals:
    key = tuple(int(row[name]) for name in ('warmup', 'pair', 'order', 'legacy', 'frame'))
    spans.setdefault(key, {})[int(row['id'])] = row
for group in spans.values():
    for row in group.values():
        assert row['complete'] == '1' and int(row['end_ns']) >= int(row['start_ns'])
        if int(row['parent']):
            parent = group[int(row['parent'])]
            assert int(parent['start_ns']) <= int(row['start_ns']) <= int(row['end_ns']) <= int(parent['end_ns'])
for key, group in spans.items():
    calls = {'template_backbone': 0, 'search_backbone': 0, 'head': 0}
    for row in group.values():
        if row['stage'] != 'run':
            continue
        ancestors = []
        parent = int(row['parent'])
        while parent:
            ancestors.append(group[parent]['stage'])
            parent = int(group[parent]['parent'])
        roles = [stage for stage in ancestors if stage in calls]
        assert len(roles) <= 1
        if roles:
            calls[roles[0]] += 1
        else:
            assert 'inference' in ancestors
            calls['search_backbone'] += 1
    assert calls == ({'template_backbone': 1, 'search_backbone': 0, 'head': 0} if key[-1] == 0
                     else {'template_backbone': 0, 'search_backbone': 1, 'head': 1})
def distribution(values):
    values = sorted(values)
    return {'median_ms': statistics.median(values) / 1e6, 'p95_ms': values[math.ceil(.95 * len(values)) - 1] / 1e6}
summary = {}
for legacy, label in ((1, 'old'), (0, 'new')):
    selected = [rows for key, rows in batches.items() if key[0] == 0 and key[3] == legacy]
    summary[label] = {'recovery': distribution([int(rows[0]['batch_ns']) for rows in selected])}
    for field in ('preprocess_ns', 'image_prepare_ns', 'elapsed_ns'):
        summary[label][field] = distribution([sum(int(row[field]) for row in rows) for rows in selected])
    summary[label]['init'] = distribution([int(rows[0]['elapsed_ns']) for rows in selected])
    summary[label]['updates'] = distribution([sum(int(row['elapsed_ns']) for row in rows[1:]) for rows in selected])
summary['ratio'] = summary['new']['preprocess_ns']['median_ms'] / summary['old']['preprocess_ns']['median_ms']
summary['passed'] = summary['ratio'] <= .2 and summary['new']['recovery']['median_ms'] < summary['old']['recovery']['median_ms'] and summary['new']['recovery']['p95_ms'] <= summary['old']['recovery']['p95_ms']
windows = list(csv.DictReader((root / 'other-windows.csv').open()))
assert len(windows) == 8 * 24 and all(row['exact'] == '1' for row in windows)
summary['windows'] = {}
for size in sorted({int(row['side']) for row in windows}):
    summary['windows'][size] = {}
    for legacy, label in ((1, 'old'), (0, 'new')):
        rows = [row for row in windows if row['warmup'] == '0' and int(row['side']) == size and int(row['legacy']) == legacy]
        assert len(rows) == 10
        summary['windows'][size][label] = {role: distribution([int(row[role + '_ns']) for row in rows]) for role in ('template', 'search')}
summary['frames'] = len(frames)
summary['batches'] = len(batches)
summary['intervals'] = len(intervals)
summary['evidence_hashes'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.iterdir()) if p.is_file()}
(root.parent / 'analysis.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(summary, ensure_ascii=False, indent=2))

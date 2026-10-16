"""Summarize the guarded 20-round/610-second private acceptance evidence."""
import json
import math
import re
import statistics
import sys
from pathlib import Path


root = Path(sys.argv[1])
rounds = json.loads((root / 'rounds.json').read_text())
run = json.loads((root / 'endurance.json').read_text())
resources = json.loads((root / 'endurance-resources.json').read_text())
events = [json.loads(line) for line in (root / 'checkpoints.jsonl').read_text().splitlines()]
restore = json.loads((root / 'restore.json').read_text())
text = (root / 'node.log').read_text()
lo, hi = run['live_wall'] + 10, run['end_wall']
windows = []
failure_samples = []
for block in re.split(r'(?=\[INFO\].*OSD timing round=)', text):
    if 'OSD timing round=' not in block:
        continue
    head = block.splitlines()[0]
    stamp = re.search(r'\[([0-9]+\.[0-9]+)\]', head)
    duration = re.search(r'window_us=(\d+)', head)
    if not stamp or not duration:
        continue
    end, seconds = float(stamp[1]), int(duration[1]) / 1e6
    for match in re.finditer(r'^  (new_video|repeat) result=failure count=([1-9]\d*)', block, re.M):
        failure_samples.append(dict(wall=end, kind=match[1], count=int(match[2])))
    line = re.search(r'^  new_video result=success[^\n]*', block, re.M)
    if end - seconds >= lo and end <= hi and line and 'samples=none' not in line[0]:
        windows.append((seconds, head, line[0]))

result = dict(run=run, rounds=len(rounds),
              each_round_has_results=all(row['new_boxes'] > 0 for row in rounds),
              timing_seconds=sum(seconds for seconds, _, _ in windows),
              failure_samples=failure_samples,
              runtime_display_disabled='OSD disabled after display failure' in text,
              restore=restore)
result['failures_during_continuous_load'] = sum(row['count'] for row in failure_samples
    if run['live_wall'] <= row['wall'] <= run['end_wall'])
result['started_pids'] = [event['pid'] for event in events if event['event'] == 'node_started']
result['same_process'] = result['started_pids'] == [run['pid']]
result['hdmi_fps'] = sum(int(re.search(r' displayed=(\d+)', head)[1])
                         for _, head, _ in windows) / result['timing_seconds']
result['stable_window_counters'] = {field: sum(int(re.search(r' ' + field + r'=(\d+)', head)[1])
    for _, head, _ in windows) for field in ('submitted', 'replaced', 'displayed', 'rejected', 'commits')}
for stage in ('render_total', 'submit_to_display', 'pending_wait', 'new_frame_interval'):
    total = count = 0
    buckets = [0] * 28
    maximum = 0
    for _, _, line in windows:
        fields = dict(part.split('=', 1) for part in re.search(stage + r'\{([^}]+)\}', line)[1].split(','))
        n = int(fields['count'])
        if not n:
            continue
        count += n
        total += n * float(fields['mean'])
        maximum = max(maximum, int(fields['max']))
        for index, bucket in enumerate(fields['buckets'].split(':')):
            buckets[index] += int(bucket)
    cumulative = 0
    upper = None
    for index, bucket in enumerate(buckets):
        cumulative += bucket
        if count and cumulative >= math.ceil(count * .95):
            upper = 2 ** index / 1000
            break
    result[stage] = dict(count=count, mean_ms=total / count / 1000 if count else None,
                         max_ms=maximum / 1000, p95_upper_ms=upper)

stable = [row for row in resources if row['mono'] >= run['live'] + 10]
result['tracking_fps'] = (stable[-1]['boxes'] - stable[0]['boxes']) / (stable[-1]['mono'] - stable[0]['mono'])
result['cpu_percent'] = (stable[-1]['cpu_ticks'] - stable[0]['cpu_ticks']) / (stable[-1]['mono'] - stable[0]['mono'])
for field in ('rss_kb', 'threads', 'fds'):
    result[field + '_range'] = [min(row[field] for row in stable), max(row[field] for row in stable)]
result['rss_minute_medians_kb'] = [statistics.median(row['rss_kb'] for row in resources
    if minute * 60 <= row['mono'] - run['live'] < (minute + 1) * 60)
    for minute in range(10)]
result['max_temperature_c'] = max(max(event['temps'].values()) for event in events
                                  if event['event'] == 'checkpoint') / 1000
result['guard_completed'] = events[-1]['event'] == 'completed' and not any(
    event['event'] in ('failed', 'thermal_abort', 'thermal_monitor_failed', 'signal_abort')
    or event.get('errors') for event in events)
result['passed'] = all((len(rounds) == 20, run['rounds_same_process'] == 20,
    result['same_process'],
    result['each_round_has_results'], run['duration_sec'] >= 610,
    result['guard_completed'], restore['services_restored'],
    restore['config_and_release_unchanged'], restore['node_exit_code'] == 0,
    not result['runtime_display_disabled'], result['hdmi_fps'] >= 25,
    result['failures_during_continuous_load'] == 0,
    result['render_total']['mean_ms'] <= 40,
    result['render_total']['p95_upper_ms'] <= 65.536,
    result['submit_to_display']['mean_ms'] <= 60))
print(json.dumps(result, indent=2))
raise SystemExit(0 if result['passed'] else 1)

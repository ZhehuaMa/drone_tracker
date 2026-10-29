#!/usr/bin/env python3
"""Independently recompute the predeclared live-camera CPU 6/7 comparison."""
import argparse
import bisect
from collections import Counter, defaultdict
import copy
import csv
import json
import math
from pathlib import Path
import re
import statistics


ORDER = (6, 7, 7, 6, 7, 6, 6, 7)


def load(path):
    return json.loads(path.read_text())


def quantile(values, probability):
    ordered = sorted(values)
    return ordered[math.ceil(len(ordered) * probability) - 1] if ordered else None


def csv_rows(path):
    with path.open() as source:
        return [{key: int(value) for key, value in row.items()} for row in csv.DictReader(source)]


def normalized_config(config):
    value = copy.deepcopy(config)
    params = value['drone_tracker_node']['ros__parameters']
    params['save_path'] = '<isolated bag directory>'
    params['recording']['directory'] = '<isolated recording directory>'
    params['diagnostics']['socket'] = '<isolated socket>'
    return value


def resources(case, log):
    roles = {}
    for pid, tid, role in re.findall(r'CPU affinity pid=(\d+) tid=(\d+) role=(\w+)', log):
        roles[(int(pid), int(tid))] = role
    by_task = defaultdict(list)
    cores = []
    start, end = case['measurement_start_ns'], case['measurement_end_ns']
    temperatures = defaultdict(list)
    for sample in case['resources']:
        stamp = sample['mono_ns']
        if not start <= stamp < end:
            continue
        for actor, process in sample['processes'].items():
            for tid, task in process.get('tasks', {}).items():
                stat = task.get('stat')
                if not isinstance(stat, str):
                    continue
                fields = stat.rsplit(') ', 1)[1].split()
                sched = task.get('schedstat')
                counters = list(map(int, sched.split())) if isinstance(sched, str) else None
                by_task[(process['pid'], int(tid), fields[19])].append({
                    'stamp': stamp, 'actor': actor, 'name': task['name'],
                    'affinity': task['affinity'], 'scheduler': task['scheduler'],
                    'last_cpu': int(fields[36]), 'nice': int(fields[16]), 'sched': counters,
                })
        cpu_stat = sample.get('cpu_stat')
        if isinstance(cpu_stat, str):
            values = {}
            for line in cpu_stat.splitlines():
                fields = line.split()
                if re.fullmatch(r'cpu[0-9]+', fields[0]):
                    # Guest counters are already included in user/nice.
                    ticks = list(map(int, fields[1:9]))
                    values[fields[0]] = (sum(ticks), ticks[3] + ticks[4])
            cores.append(values)
        for name, value in sample.get('sensors', {}).items():
            if isinstance(value, str) and value.strip().isdigit():
                temperatures[name].append(int(value))
    task_results = []
    for (pid, tid, _), samples in by_task.items():
        first, last = samples[0], samples[-1]
        elapsed = last['stamp'] - first['stamp']
        item = {'pid': pid, 'tid': tid, 'name': first['name'], 'actor': first['actor'],
                'role': roles.get((pid, tid), 'collector' if first['actor'] == 'collector' else 'unmapped'),
                'samples': len(samples), 'masks': [list(value) for value in sorted({tuple(s['affinity']) for s in samples})],
                'schedulers': sorted({s['scheduler'] for s in samples}),
                'nice_values': sorted({s['nice'] for s in samples}),
                'last_cpu_counts': dict(Counter(str(s['last_cpu']) for s in samples))}
        if elapsed > 0 and first['sched'] and last['sched']:
            item['sample_seconds'] = elapsed / 1e9
            item['cpu_percent_one_core'] = 100 * (last['sched'][0] - first['sched'][0]) / elapsed
            item['runnable_wait_ms_per_second'] = 1000 * (last['sched'][1] - first['sched'][1]) / elapsed
        task_results.append(item)
    busy = {}
    if len(cores) > 1:
        for cpu, (last_total, last_idle) in cores[-1].items():
            first_total, first_idle = cores[0][cpu]
            if last_total > first_total:
                busy[cpu] = 100 * (1 - (last_idle - first_idle) / (last_total - first_total))
    return {'tasks': task_results, 'cpu_busy_percent_excluding_idle_and_iowait': busy,
            'sensor_ranges': {name: {'min': min(values), 'max': max(values)} for name, values in temperatures.items()}}


def analyze_one(root, number, cpu, prefix):
    run_id = f'{prefix}run{number:02d}-cpu{cpu}'
    local = root / run_id
    raw = local / 'raw' / run_id
    if not (raw / 'case.json').exists():
        return {'run_id': run_id, 'cpu': cpu, 'available': False}
    case = load(raw / 'case.json')
    summary = load(raw / 'metrics.json')
    cleanup = load(local / 'cleanup.json')
    window = load(local / 'window.json')
    integrity = load(local / 'integrity.json')
    result = {'run_id': run_id, 'cpu': cpu, 'available': True,
              'execution_passed': case.get('execution_passed', False),
              'protected_unchanged': case.get('protected_unchanged', False),
              'full_workload': summary['full_workload_observed'],
              'cleanup_released': cleanup.get('released', False),
              'integrity_mismatches': integrity['mismatches'],
              'whole_seconds': case['elapsed_seconds'],
              'outer_window_seconds': window['elapsed_seconds'],
              'window_status': window['final'], 'timeout': window['timeout'],
              'programs': case['programs'], 'normalized_config': normalized_config(case['actual_config'])}
    if 'measurement_start_ns' not in case or 'measurement_end_ns' not in case:
        result['error'] = case.get('error', 'Missing measurement window')
        result['valid'] = False
        return result
    start, end = case['measurement_start_ns'], case['measurement_end_ns']
    seconds = (end - start) / 1e9
    calls = [r for r in csv_rows(raw / 'sampling/calls.csv') if start <= r['start_ns'] < end]
    submissions = [r for r in csv_rows(raw / 'sampling/submissions.csv') if start <= r['start_ns'] < end]
    result.update(window_seconds=seconds, calls=len(calls), consumed=sum(row['consumed'] for row in calls),
                  consumed_fps=sum(row['consumed'] for row in calls) / seconds,
                  call_p50_ms=quantile([row['duration_ns'] for row in calls], .50) / 1e6,
                  call_p95_ms=quantile([row['duration_ns'] for row in calls], .95) / 1e6,
                  call_p99_ms=quantile([row['duration_ns'] for row in calls], .99) / 1e6,
                  call_max_ms=max(row['duration_ns'] for row in calls) / 1e6,
                  submit_p99_us=quantile([row['duration_ns'] for row in submissions], .99) / 1e3)
    assert math.isclose(result['consumed_fps'], summary['calls']['consumed_fps'], abs_tol=1e-10)
    assert math.isclose(result['call_p99_ms'] * 1e6, summary['calls']['p99_ns'], abs_tol=.5)
    assert math.isclose(result['submit_p99_us'] * 1000, summary['submissions']['p99_ns'], abs_tol=.5)
    events = [json.loads(line) for line in (raw / 'diagnostic-export/events.jsonl').open()]
    inside = [event for event in events if start <= event['mono_ns'] < end]
    health = [event for event in inside if event['type'] == '健康']
    result['diagnostic_health'] = {
        'sampled_records': len(health),
        'degraded_records': sum(not bool(event['flags'] & 128) for event in health),
        'maximum_sampled_unsynced_age_ms': max((event['values'][4] for event in health), default=0) / 1e6,
    }
    published = [stamp for stamp in case.get('synthetic_input_publish_ns', []) if start <= stamp < end]
    result['input_hz_per_topic'] = len(published) / seconds
    result['sample_overflows'] = sum(int(value) for value in summary['sampling_status'].values())
    node_starts = sorted((event['mono_ns'], (event['round_id'], event['update_id']))
                         for event in events if event['producer_id'] == 0 and event['type'] == '调用开始')
    stamps = [item[0] for item in node_starts]
    selected = set()
    for row in calls:
        index = bisect.bisect_left(stamps, row['start_ns'])
        assert index < len(stamps) and stamps[index] < row['start_ns'] + row['duration_ns']
        selected.add(node_starts[index][1])
    work = [event for event in events if event['producer_id'] == 0
            and (event['round_id'], event['update_id']) in selected]
    counts = Counter(event['type'] for event in work)
    captures = [event for event in inside if event['type'] == '采集' and event['reason'] in ('无', '队列满')]
    result['work'] = {'selected_calls_with_event_identity': len(selected),
                      'initializations': counts['初始化开始'], 'catchup_steps': counts['补跟踪步骤'],
                      'tracking_output_calls': sum(event['values'][1] != 0 for event in work if event['type'] == '输出'),
                      'detect_completions_in_window': sum(event['type'] == '检测结束' for event in inside),
                      'successful_detections_in_window': sum(event['type'] == '检测结束' and bool(event['flags'] & 512) for event in inside)}
    boxes = [event['box'] for event in work if event['type'] == '输出' and event['flags'] & 16]
    result['tracked_box_summary'] = {
        'count': len(boxes),
        'mean_xywh': [statistics.fmean(box[axis] for box in boxes) for axis in range(4)] if boxes else None,
        'stddev_xywh': [statistics.pstdev(box[axis] for box in boxes) for axis in range(4)] if boxes else None,
        'note': 'Observed tracker boxes describe scene consistency; they are not independent target ground truth.',
    }
    result['camera'] = {'qualified_capture_events': len(captures), 'capture_fps': len(captures) / seconds,
                        'queue_full': sum(event['reason'] == '队列满' for event in captures),
                        'driver_gap_frames': sum(event['values'][0] for event in inside if event['type'] == '采集' and event['reason'] == '驱动序号缺口'),
                        'latest_skipped': sum(event['type'] == '节点跳过' for event in inside)}
    result['osd_whole_run'] = summary['osd_full_run']
    result['recordings_whole_run'] = summary['recordings']
    result['recording_counter_maxima_whole_run'] = summary.get('recording_counter_maxima')
    result['bags_whole_run'] = summary['bags']
    result['bag_log'] = summary['bag_log']
    result['diagnostic_gaps'] = summary['events']['gap_events']
    result['resources'] = resources(case, (raw / 'tracker.log').read_text(errors='replace'))
    result['valid'] = (summary['full_workload_valid_before_host_cleanup_check']
                       and cleanup.get('released') and not integrity['mismatches']
                       and not window['timeout'] and window['elapsed_seconds'] <= 150
                       and window['final'].get('Result') == 'success'
                       and window['final'].get('ExecMainStatus') == '0')
    return result


def compare(root, prefix):
    runs = [analyze_one(root, number, cpu, prefix) for number, cpu in enumerate(ORDER, 1)]
    available = [run for run in runs if run['available']]
    if available:
        same_programs = all(run['programs'] == available[0]['programs'] for run in available)
        same_config = all(run['normalized_config'] == available[0]['normalized_config'] for run in available)
    else:
        same_programs = same_config = False
    pairs = []
    for index in range(0, len(runs), 2):
        first, second = runs[index:index + 2]
        if not (first.get('valid') and second.get('valid')):
            pairs.append({'pair': index // 2 + 1, 'valid': False,
                          'runs': [first['run_id'], second['run_id']]})
            continue
        a, b = (first, second) if first['cpu'] == 6 else (second, first)
        pairs.append({'pair': index // 2 + 1, 'valid': True, 'runs': [first['run_id'], second['run_id']],
                      'cpu7_minus_cpu6_p99_ms': b['call_p99_ms'] - a['call_p99_ms'],
                      'cpu7_minus_cpu6_fps_percent': 100 * (b['consumed_fps'] / a['consumed_fps'] - 1),
                      'cpu7_minus_cpu6_submit_p99_us': b['submit_p99_us'] - a['submit_p99_us']})
    aggregates = {}
    for cpu in (6, 7):
        valid = [run for run in runs if run.get('valid') and run['cpu'] == cpu]
        aggregates[str(cpu)] = {'planned': 4, 'valid_runs': len(valid),
                                'run_ids': [run['run_id'] for run in valid]}
        for field in ('consumed_fps', 'call_p50_ms', 'call_p95_ms', 'call_p99_ms', 'submit_p99_us'):
            values = [run[field] for run in valid]
            aggregates[str(cpu)][field] = {'values': values,
                                          'median': statistics.median(values) if values else None}
    return {'same_programs': same_programs, 'same_normalized_configuration': same_config,
            'all_planned_runs_valid': len(available) == 8 and all(run.get('valid') for run in runs),
            'prefix': prefix, 'order': list(ORDER), 'runs': runs, 'pairs': pairs, 'aggregates': aggregates,
            'note': 'Four live-camera runs per core; per-run metrics and workload variation remain material. No task 7.3 acceptance inference.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--prefix', default='', help='New batch prefix, such as batch02-')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = compare(args.root, args.prefix)
    with args.output.open('x') as destination:
        json.dump(result, destination, indent=2)
        destination.write('\n')
    print(json.dumps({'valid_runs': {key: value['valid_runs'] for key, value in result['aggregates'].items()},
                      'all_planned_runs_valid': result['all_planned_runs_valid'],
                      'pairs': result['pairs']}, indent=2))

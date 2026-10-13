#!/usr/bin/env python3
"""从独立采样数组和只读导出的事件计算验收指标，保存全部慢样本。"""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path


def rows(path):
    with path.open() as source:
        return [{key: int(value) for key, value in record.items()} for record in csv.DictReader(source)]


def quantile(values, probability=0.99):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[math.ceil(probability * len(ordered)) - 1]


def events(directory):
    result = []
    for path in sorted(directory.glob('export-*/events.jsonl')):
        result.extend(json.loads(line) for line in path.read_text().splitlines())
    return result


def metric(directory, name, window):
    calls = [sample for sample in rows(directory / name / 'calls.csv')
             if window['measurement_start_ns'] <= sample['start_ns'] < window['measurement_end_ns']]
    submits = [sample for sample in rows(directory / name / 'submissions.csv')
               if window['measurement_start_ns'] <= sample['start_ns'] < window['measurement_end_ns']]
    durations = [sample['duration_ns'] for sample in calls]
    submit_durations = [sample['duration_ns'] for sample in submits]
    seconds = (window['measurement_end_ns'] - window['measurement_start_ns']) / 1e9
    call_p99 = quantile(durations)
    submit_p99 = quantile(submit_durations)
    slow = {'calls': [sample for sample in calls if sample['duration_ns'] >= call_p99] if call_p99 else [],
            'submissions': [sample for sample in submits if sample['duration_ns'] >= submit_p99] if submit_p99 else []}
    (directory / (name + '-slow-samples.json')).write_text(json.dumps(slow, indent=2))
    status = dict(line.split('=') for line in (directory / name / 'sampling-status.txt').read_text().splitlines())
    return {'mode': window['mode'], 'window_seconds': seconds, 'call_samples': len(calls),
            'consumed': sum(sample['consumed'] for sample in calls),
            'consumed_fps': sum(sample['consumed'] for sample in calls) / seconds,
            'call_p99_ms': call_p99 / 1e6 if call_p99 is not None else None,
            'call_max_ms': max(durations) / 1e6 if durations else None,
            'submit_samples': len(submits), 'submit_p99_us': submit_p99 / 1000 if submit_p99 is not None else None,
            'submit_max_us': max(submit_durations) / 1000 if submit_durations else None,
            'sample_overflow': sum(int(value) for value in status.values()),
            'submit_by_producer': dict(Counter(sample['producer'] for sample in submits))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    args = parser.parse_args()
    summary = {'pairs': [], 'cases': {}}
    for name in tuple('pair' + str(index) for index in range(1, 11)) + ('detail', 'lifecycle', 'business-kill', 'worker-kill', 'full', 'write', 'sync',
                 'services', 'write-after-data', 'sync-after-data'):
        directory = args.evidence / name
        if not directory.exists() or not (directory / 'case.json').exists():
            summary['cases'][name] = {'executed': False}
            if name.startswith('pair'):
                summary['pairs'].append({'name': name, 'passed': False, 'failure': '缺少完整单组记录'})
            continue
        case = json.loads((directory / 'case.json').read_text())
        observed = events(directory)
        types = Counter(event['type'] for event in observed)
        drops = max([event['values'][0] for event in observed if event['type'] == '诊断缺口'] or [0])
        health_drops = max([event['values'][3] for event in observed if event['type'] == '健康'] or [0])
        timelines = list(directory.glob('export-*/timeline.txt'))
        complete = bool(timelines) and all('缺少完成标志' not in path.read_text() for path in timelines)
        item = {'executed': True, 'execution_passed': case.get('execution_passed', False),
                'elapsed_seconds': (case['finish_ns'] - case['start_ns']) / 1e9,
                'events': len(observed), 'event_types': dict(types), 'max_gap_counter': drops,
                'max_health_drops': health_drops, 'complete_segments': complete,
                'received_states': dict(Counter(event['state'] for event in case['states'])),
                'received_boxes': len(case['boxes'])}
        summary['cases'][name] = item
        if name.startswith('pair') or name == 'detail':
            if len(case['rounds']) != 2:
                summary['pairs'].append({'name': name, 'passed': False,
                    'failure': case.get('error', '缺少两个完整测量窗口')})
                continue
            metrics = []
            for index, window in enumerate(case['rounds']):
                metrics.append(metric(directory, str(index) + '-' + window['mode'], window))
            item['metrics'] = metrics
            if name.startswith('pair') and len(metrics) == 2:
                off = next(value for value in metrics if value['mode'] == 'off')
                basic = next(value for value in metrics if value['mode'] == 'basic')
                if off['call_p99_ms'] is None or basic['call_p99_ms'] is None or basic['submit_p99_us'] is None or not off['consumed_fps']:
                    summary['pairs'].append({'name': name, 'off': off, 'basic': basic,
                        'passed': False, 'failure': '有效样本不足'})
                    continue
                delta = basic['call_p99_ms'] - off['call_p99_ms']
                decline = (1 - basic['consumed_fps'] / off['consumed_fps']) * 100
                external_path = args.evidence / (name + '-window.json')
                external = json.loads(external_path.read_text()) if external_path.exists() else {}
                systemd_state = external.get('systemd_state', {})
                passed = (systemd_state.get('Result') == 'success' and systemd_state.get('ExecMainStatus') == '0' and
                          external.get('resources_recovered', False) and not external.get('timeout', True) and
                          external.get('elapsed_seconds', 151) <= 150 and case.get('execution_passed', False) and
                          not case.get('cleanup_errors') and basic['submit_p99_us'] <= 20 and delta <= 0.2 and decline <= 1 and
                          bool(observed) and not drops and not health_drops and complete and not basic['sample_overflow'] and not off['sample_overflow'] and
                          off['call_samples'] >= 100 and basic['call_samples'] >= 100)
                summary['pairs'].append({'name': name, 'off': off, 'basic': basic,
                    'call_p99_increment_ms': delta, 'consumption_decline_percent': decline, 'passed': passed})
        timing = defaultdict(lambda: {'events': 0, 'count': 0, 'failures': 0, 'duration_ns': 0, 'unobserved': 0})
        for event in observed:
            if event['type'] == '阶段耗时':
                value = timing[str(event['values'][0])]
                value['events'] += 1
                value['count'] += event['counts'][0]
                value['failures'] += event['counts'][1]
                value['duration_ns'] += event['duration_ns']
                value['unobserved'] += not bool(event['flags'] & 2048)
        if timing:
            item['timing_by_stage'] = dict(timing)
    summary['passed_pairs'] = sum(pair['passed'] for pair in summary['pairs'])
    summary['basic_passed'] = len(summary['pairs']) == 10 and summary['passed_pairs'] >= 8
    (args.evidence / 'metrics.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Summarize retained workload identities and measured rates without nominal-rate substitution."""
import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics


def quantile(values, fraction):
    values = sorted(values)
    if not values:
        return None
    position = (len(values) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    return values[lower] + (values[upper] - values[lower]) * (position - lower)


def cpu(first, last, seconds, ticks):
    return (last['cpu_ticks'] - first['cpu_ticks']) / ticks / seconds * 100


def summarize(directory):
    document = json.loads((directory / 'workload.json').read_text())
    rounds = document['rounds']
    seconds = sum(item['off_monotonic'] - item['start_monotonic'] for item in rounds)
    active = [sample for sample in document['samples'] if any(
        item['start_monotonic'] + .5 <= sample['monotonic_ns'] / 1e9 <= item['off_monotonic'] - .5
        for item in rounds)]
    calls = list(csv.DictReader((directory / 'measurements/calls.csv').open()))
    selected = [row for row in calls if row['consumed'] == '1' and any(
        item['start_monotonic'] <= int(row['start_ns']) / 1e9 <= item['off_monotonic']
        for item in rounds)]
    latency = [int(row['duration_ns']) / 1000 for row in selected]
    cpu_values = []
    disk_values = []
    worker_cpu = []
    collector_cpu = []
    weights = []
    for first, last in zip(active, active[1:]):
        elapsed = (last['monotonic_ns'] - first['monotonic_ns']) / 1e9
        if elapsed > 1.5:
            continue
        weights.append(elapsed)
        cpu_values.append(cpu(first, last, elapsed, document['clock_ticks']))
        collector_cpu.append(cpu(first['collector'], last['collector'], elapsed, document['clock_ticks']))
        before = {child['pid']: child for child in first['children']}
        after = {child['pid']: child for child in last['children']}
        worker_cpu.append(sum(cpu(before[pid], after[pid], elapsed, document['clock_ticks'])
                              for pid in before.keys() & after.keys()))
        disk_values.append((last['filesystem_io'][6] - first['filesystem_io'][6]) * 512 / elapsed)
    input_rates = {}
    for identifier, data in document['inputs'].items():
        span = (data['last_ns'] - data['first_ns']) / 1e9 if data['count'] > 1 else 0
        input_rates[identifier] = dict(requested_hz=data['requested_hz'], count=data['count'],
                                      measured_hz=(data['count'] - 1) / span if span else None,
                                      mean_serialized_bytes=data['serialized_bytes'] / data['count'] if data['count'] else 0)
    summaries = [json.loads(path.read_text()) for path in sorted((directory / 'bags').glob('*/session.json'))]
    totals = {key: sum(topic[key] for summary in summaries for topic in summary['topics'].values())
              for key in ('offered', 'accepted', 'written', 'policy_skipped', 'byte_limited', 'oversized',
                          'queue_dropped', 'accepted_but_unwritten', 'timestamp_fallback', 'middleware_lost', 'incompatible_qos')}
    displayed = 0
    display_seconds = 0
    histogram = [0] * 28
    display_total = 0
    display_count = 0
    display_max = 0
    tracking_window = False
    for line in (directory / 'tracker.log').read_text().splitlines():
        if 'OSD timing ' in line:
            fields = dict(re.findall(r'(\w+)=([\d.]+)', line))
            tracking_window = int(fields.get('tracking_displayed', 0)) > 0 and int(fields.get('preview_displayed', 0)) == 0
            if tracking_window:
                displayed += int(fields['tracking_displayed'])
                display_seconds += int(fields['window_us']) / 1e6
        elif tracking_window and 'new_video result=success' in line:
            match = re.search(r'submit_to_display\{count=(\d+),mean=([\d.]+),max=(\d+),.*?buckets=([\d:]+)', line)
            if match:
                count, mean, maximum, buckets = match.groups()
                display_count += int(count)
                display_total += int(count) * float(mean)
                display_max = max(display_max, int(maximum))
                histogram = [left + right for left, right in zip(histogram, map(int, buckets.split(':')))]

    def display_quantile(fraction):
        rank = math.ceil(display_count * fraction)
        cumulative = 0
        for index, count in enumerate(histogram):
            cumulative += count
            if cumulative >= rank:
                return 2 ** index
        return None

    rss = [sample['rss_kib'] for sample in active]
    quarter = max(1, len(rss) // 4)
    def weighted_mean(values):
        return sum(value * weight for value, weight in zip(values, weights)) / sum(weights) if weights else None

    return dict(profile=document['profile'], domain=document['domain'], node_sha256=document['binary_sha256'],
                config_sha256=document['config_sha256'], exit_status=document['exit_status'],
                errors=document['errors'], duration_seconds=document['duration_seconds'],
                active_seconds=seconds, rounds=len(rounds), resource_samples=len(document['samples']),
                consumed=len(selected), consumed_fps=len(selected) / seconds,
                displayed_fps=displayed / display_seconds if display_seconds else None,
                update_us=dict(p50=quantile(latency, .5), p95=quantile(latency, .95), p99=quantile(latency, .99),
                               maximum=max(latency) if latency else None),
                submit_to_display_us=dict(mean=display_total / display_count if display_count else None,
                                          p95_upper=display_quantile(.95), p99_upper=display_quantile(.99), maximum=display_max),
                node_cpu_percent=weighted_mean(cpu_values),
                video_worker_cpu_percent=weighted_mean(worker_cpu),
                diagnostic_cpu_percent=weighted_mean(collector_cpu),
                rss_mib=dict(median=statistics.median(rss) / 1024 if rss else None,
                             peak=max(rss) / 1024 if rss else None,
                             first_quarter_median=statistics.median(rss[:quarter]) / 1024 if rss else None,
                             last_quarter_median=statistics.median(rss[-quarter:]) / 1024 if rss else None),
                fds=dict(minimum=min(s['fds'] for s in active), maximum=max(s['fds'] for s in active)),
                threads=dict(minimum=min(s['threads'] for s in active), maximum=max(s['threads'] for s in active)),
                root_filesystem_write_mib_per_second=weighted_mean(disk_values) / 1048576 if disk_values else None,
                process_io_available=False, inputs=input_rates, session_totals=totals,
                session_phases=[item['phase'] for item in summaries],
                peak_messages=max((item['peak_messages'] for item in summaries), default=0),
                peak_payload_bytes=max((item['peak_bytes'] for item in summaries), default=0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = [summarize(path) for path in args.directories]
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    for row in result:
        print(row['profile'], 'consumed_fps', round(row['consumed_fps'], 3),
              'node_cpu%', round(row['node_cpu_percent'], 2), 'rss_mib', row['rss_mib']['median'])


if __name__ == '__main__':
    main()

"""汇总两组新实例资源负载；不以主机内存推断 NPU 设备内存。

用法：python3 summarize_resources.py continued-resources
保留每轮峰值与趋势，而不是只用全局平均值掩盖持续增长。
"""

import json
from pathlib import Path
import statistics
import sys


def slope(values):
    center = (len(values) - 1) / 2
    average = statistics.mean(values)
    denominator = sum((i - center) ** 2 for i in range(len(values)))
    return sum((i - center) * (value - average) for i, value in enumerate(values)) / denominator if denominator else 0


def main():
    phase = Path(sys.argv[1])
    records = [json.loads(line) for line in (phase / 'journal.jsonl').read_text().splitlines()]
    result = {'device_memory_measured': False, 'groups': {}}
    for precision in ('fp16', 'int8'):
        rows = [row for row in records if row['name'].startswith(precision + '-')]
        if not rows:
            continue
        peaks = [row['peak']['rss_kib'] for row in rows]
        window = min(10, len(rows))
        summary = {
            'rounds': len(rows), 'reported_active_seconds': sum(row['reported_run_seconds'] for row in rows),
            'completed_eof_rounds': sum(row['exit_code'] == 0 and row['end_reason'] == 'eof' for row in rows),
            'completed_eof_active_seconds': sum(row['reported_run_seconds'] for row in rows if row['exit_code'] == 0 and row['end_reason'] == 'eof'),
            'interrupted_runs': [row['name'] for row in rows if row['end_reason'] == 'interrupted'],
            'unique_pids': len(set(row['pid'] for row in rows)),
            'rss_peak_min_kib': min(peaks), 'rss_peak_max_kib': max(peaks),
            'rss_peak_first10_mean_kib': statistics.mean(peaks[:window]),
            'rss_peak_last10_mean_kib': statistics.mean(peaks[-window:]),
            'rss_peak_linear_slope_kib_per_round': slope(peaks),
            'thread_peak_values': sorted(set(row['peak']['threads'] for row in rows)),
            'fd_peak_values': sorted(set(row['peak']['fds'] for row in rows)),
            'mem_available_before_first_kib': rows[0]['mem_available_before_kib'],
            'mem_available_after_last_kib': rows[-1]['mem_available_after_kib'],
            'all_exit_zero_eof': all(row['exit_code'] == 0 and row['end_reason'] == 'eof' for row in rows),
            'all_reaped_no_replay_processes': all(not row['pid_exists_after_wait'] and not row['remaining_replay_pids'] for row in rows),
            'timeouts': sum(row['timeout'] for row in rows), 'forced_kills': sum(row['forced_kill'] for row in rows),
            'runs': [{'name': row['name'], 'pid': row['pid'], 'peak': row['peak'],
                      'seconds': row['reported_run_seconds']} for row in rows],
        }
        result['groups'][precision] = summary
    (phase / 'resource-summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({key: {k: v for k, v in value.items() if k != 'runs'}
                      for key, value in result['groups'].items()}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

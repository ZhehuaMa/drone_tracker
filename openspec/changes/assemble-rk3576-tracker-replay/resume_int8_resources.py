"""独立补测 INT8 资源负载；历史暂停时间和历史轮次均不计入本轮。

在开发板运行：python3 resume_int8_resources.py ROOT
ROOT 为已有验收目录。输出到新目录，拒绝覆盖已有结果。
复用 board_acceptance.py 的全新进程、资源采样和退出检查。
"""

import datetime
import hashlib
import json
from pathlib import Path
import sys

from board_acceptance import configuration, matching_processes, run_one, write_json


def main():
    root = Path(sys.argv[1]).resolve()
    executable = root / 'rk3576_tracker_replay'
    assert not matching_processes(executable), '已有回放进程，不能并发验收'
    program_sha = hashlib.sha256(executable.read_bytes()).hexdigest()
    assert program_sha == '7e787d84e6474a6a7d05f42f8eb1f3a8a35587e50da552d231ecc510b10605ef'
    previous = [json.loads(line) for line in (root / 'continued-resources/journal.jsonl').read_text().splitlines()]
    reference = next(row['sha256'] for row in previous if row['name'].startswith('int8-') and row['exit_code'] == 0)
    phase = root / 'int8-resources-20260914'
    phase.mkdir()
    write_json(phase / 'identity.json', {
        'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'program_sha256': program_sha,
        'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'shared_runner_sha256': hashlib.sha256((root / 'board_acceptance.py').read_bytes()).hexdigest(),
        'python': sys.version, 'sampling_seconds': 0.25,
        'historical_rounds_counted': 0, 'device_memory_measured': False,
    })
    active_seconds = 0.0
    rounds = 0
    while active_seconds < 1800 or rounds < 20:
        rounds += 1
        name = f'int8-resource-{rounds:03d}'
        config = configuration(root, phase, name, 'int8')
        result = run_one(root, phase, name, config)
        assert result['exit_code'] == 0 and result['end_reason'] == 'eof', result
        for key, value in reference.items():
            if key != 'configuration':
                assert result['sha256'][key] == value, (key, result['sha256'], reference)
        active_seconds += result['reported_run_seconds']
        write_json(phase / 'progress.json', {
            'rounds': rounds, 'cumulative_reported_run_seconds': active_seconds,
            'complete': active_seconds >= 1800 and rounds >= 20,
        })
    write_json(phase / 'complete.json', {'complete': True, 'rounds': rounds, 'active_seconds': active_seconds})


if __name__ == '__main__':
    main()

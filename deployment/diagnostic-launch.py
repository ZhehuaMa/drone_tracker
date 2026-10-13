#!/usr/bin/env python3
"""核对配置和 CPU 允许范围，使用普通调度启动独立日志进程。"""
import os
from pathlib import Path
import sys
import yaml


def parse(path):
    value = yaml.safe_load(Path(path).read_text(encoding='utf-8'))
    if not isinstance(value, dict) or value.get('version') != 1 or value.get('protocol_version') != 1:
        raise ValueError('诊断配置或协议版本不兼容')
    if value.get('sync_ms') not in (100, 200):
        raise ValueError('sync_ms 必须为 100 或 200')
    for name in ('segment_bytes', 'total_bytes', 'minimum_free_bytes'):
        if type(value.get(name)) is not int or value[name] < 0:
            raise ValueError(f'{name} 必须为非负整数')
    if value['segment_bytes'] < 1048576 or value['total_bytes'] < value['segment_bytes']:
        raise ValueError('分段至少 1 MiB，且不得超过总量上限')
    for name in ('socket', 'directory'):
        if not isinstance(value.get(name), str) or not Path(value[name]).is_absolute() or '\n' in value[name]:
            raise ValueError(f'{name} 必须为绝对路径')
    if value['socket'] != '/run/drone_tracker/diagnostics.sock' or value['directory'] != '/var/lib/drone_tracker/diagnostics':
        raise ValueError('诊断路径与生产安装约定不同')
    return value


def main():
    try:
        value = parse(sys.argv[1])
        requested = {int(cpu) for cpu in sys.argv[2].split(',')}
        os.sched_setaffinity(0, requested)
        actual = os.sched_getaffinity(0)
        if actual != requested:
            raise ValueError(f'CPU 读回不一致：请求 {sorted(requested)}，实际 {sorted(actual)}')
        os.sched_setscheduler(0, os.SCHED_OTHER, os.sched_param(0))
        if os.sched_getscheduler(0) != os.SCHED_OTHER:
            raise ValueError('日志进程未使用普通调度')
        print(f'诊断 CPU 请求={sorted(requested)} 读回={sorted(actual)} 调度=SCHED_OTHER', flush=True)
        Path('/run/drone_tracker').mkdir(mode=0o700, exist_ok=True)
        Path(value['directory']).mkdir(mode=0o700, parents=True, exist_ok=True)
        binary = '/opt/drone_tracker/current/lib/drone_tracker/rk3576_diagnostic_worker'
        arguments = [binary, '--socket', value['socket'], '--directory', value['directory']]
        for key in ('sync_ms', 'segment_bytes', 'total_bytes', 'minimum_free_bytes'):
            arguments += ['--' + key.replace('_', '-'), str(value[key])]
        os.execv(binary, arguments + sys.argv[3:])
    except (OSError, ValueError, KeyError, IndexError, yaml.YAMLError) as error:
        print(f'诊断不可用：{error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

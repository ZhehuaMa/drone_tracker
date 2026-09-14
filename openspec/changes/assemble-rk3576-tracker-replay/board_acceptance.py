"""板上验收驱动：每次子进程只读取一份完整配置，不复用跟踪实例。

用法：python3 board_acceptance.py ROOT matrix|resources|failures
ROOT 是含工具和固定配置的独立验收目录。原始模型及录像不会被修改。
资源采样是主机进程指标，不代表 NPU 设备内存；阶段不得并发运行。
"""

import csv
import datetime
import hashlib
import json
from pathlib import Path
import signal
import subprocess
import sys
import time


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def memory_available():
    return next(int(line.split()[1]) for line in Path('/proc/meminfo').read_text().splitlines()
                if line.startswith('MemAvailable:'))


def process_sample(pid):
    proc = Path('/proc') / str(pid)
    try:
        status = dict(line.split(':', 1) for line in (proc / 'status').read_text().splitlines() if ':' in line)
        try:
            descriptors = len(list((proc / 'fd').iterdir()))
        except PermissionError:
            # 退出时 fd 目录可能先于进程消失而不可读；缺测显式留空，不冒充 0。
            descriptors = None
        return {
            'rss_kib': int(status.get('VmRSS', '0 kB').split()[0]),
            'threads': int(status['Threads']),
            'fds': descriptors,
        }
    except (FileNotFoundError, ProcessLookupError):
        return None


def matching_processes(executable):
    matches = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if (proc / 'exe').resolve() == executable.resolve():
                matches.append(int(proc.name))
        except (OSError, RuntimeError):
            pass
    return matches


def configuration(root, phase, name, precision, scene='drone', dcf=1, edits=None):
    source = 'replay-fp16-dcf1-001.yaml' if scene == 'drone' else f'replay-fp16-{scene}-dcf1-001.yaml'
    content = (root / source).read_text()
    if precision == 'int8':
        content = content.replace('/components/fp16/', '/components/int8/rknn_int8/')
    content = content.replace('enable_dcf: 1', f'enable_dcf: {dcf}')
    lines = content.splitlines()
    for i, line in enumerate(lines):
        if line.strip().startswith('directory:'):
            lines[i] = f'  directory: "{phase / name}"'
    content = '\n'.join(lines) + '\n'
    for old, new in (edits or {}).items():
        assert old in content, old
        content = content.replace(old, new)
    path = phase / f'{name}.yaml'
    path.write_text(content)
    return path


def run_one(root, phase, name, config, interrupt_frame=None, timeout=300):
    executable = root / 'rk3576_tracker_replay'
    output = phase / name
    record = {'name': name, 'configuration': str(config), 'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'mem_available_before_kib': memory_available(), 'timeout': False, 'forced_kill': False}
    started = time.monotonic()
    peaks = {'rss_kib': 0, 'threads': 0, 'fds': 0}
    fd_samples = 0
    fd_unavailable = 0
    sent_at = None
    with (phase / f'{name}-console.log').open('w') as console, (phase / f'{name}-resources.csv').open('w') as samples:
        writer = csv.DictWriter(samples, fieldnames=['elapsed_s', 'pid', 'rss_kib', 'threads', 'fds'])
        writer.writeheader()
        child = subprocess.Popen([str(executable), str(config)], stdout=console, stderr=subprocess.STDOUT, start_new_session=True)
        record['pid'] = child.pid
        while child.poll() is None:
            elapsed = time.monotonic() - started
            sample = process_sample(child.pid)
            if sample:
                writer.writerow({'elapsed_s': elapsed, 'pid': child.pid, **sample})
                samples.flush()
                fd_samples += sample['fds'] is not None
                fd_unavailable += sample['fds'] is None
                for key, value in sample.items():
                    if value is not None:
                        peaks[key] = max(peaks[key], value)
            csv_path = output / 'frames.csv'
            if interrupt_frame is not None and sent_at is None and csv_path.exists():
                with csv_path.open() as frames:
                    frame_count = sum(1 for _ in frames) - 1
                if frame_count >= interrupt_frame:
                    child.send_signal(signal.SIGINT)
                    sent_at = time.monotonic()
                    record['sigint_after_rows'] = frame_count
            if elapsed > timeout and sent_at is None:
                record['timeout'] = True
                child.send_signal(signal.SIGINT)
                sent_at = time.monotonic()
            if sent_at is not None and time.monotonic() - sent_at > 30:
                child.kill()
                record['forced_kill'] = True
            time.sleep(0.25)
        record['exit_code'] = child.wait()
    record['process_wall_seconds'] = time.monotonic() - started
    record['peak'] = peaks
    record['fd_readable_samples'] = fd_samples
    record['fd_unavailable_samples'] = fd_unavailable
    record['pid_exists_after_wait'] = Path(f'/proc/{child.pid}').exists()
    record['remaining_replay_pids'] = matching_processes(executable)
    record['mem_available_after_kib'] = memory_available()
    report_path = output / 'run.json'
    if report_path.exists():
        report = json.loads(report_path.read_text())
        record['end_reason'] = report['end_reason']
        record['input'] = report['input']
        record['state_counts'] = report['state_counts']
        record['state_transitions'] = report['state_transitions']
        record['timings'] = report['timings']
        record['sha256'] = report['sha256']
        record['reported_run_seconds'] = report['elapsed_wall_ms'] / 1000
    with (phase / 'journal.jsonl').open('a') as journal:
        journal.write(json.dumps(record, ensure_ascii=False) + '\n')
    print(json.dumps({key: record.get(key) for key in ('name', 'exit_code', 'end_reason', 'process_wall_seconds', 'peak')}, ensure_ascii=False), flush=True)
    assert not record['timeout'] and not record['forced_kill'], record
    assert not record['pid_exists_after_wait'] and not record['remaining_replay_pids'], record
    if record['process_wall_seconds'] > 1 and record['exit_code'] == 0:
        assert fd_samples > 0, record
    return record


def main():
    root = Path(sys.argv[1]).resolve()
    mode = sys.argv[2]
    assert mode in ('matrix', 'resources', 'failures')
    phase = root / ('continued-' + mode)
    phase.mkdir(exist_ok=(mode == 'matrix'))
    identity_path = phase / ('identity.json' if not (phase / 'identity.json').exists() else f'identity-resume-{time.time_ns()}.json')
    write_json(identity_path, {
        'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'program_sha256': hashlib.sha256((root / 'rk3576_tracker_replay').read_bytes()).hexdigest(),
        'python': sys.version, 'mode': mode,
        'sampling_seconds': 0.25, 'device_memory_measured': False,
    })
    if mode == 'matrix':
        completed = {json.loads(line)['name'] for line in (phase / 'journal.jsonl').read_text().splitlines()} if (phase / 'journal.jsonl').exists() else set()
        for scene in ('drone', 'bird', 'airsim'):
            for repeat, dcf in enumerate((1, 0, 1), 1):
                name = f'int8-{scene}-dcf{dcf}-{repeat:03d}'
                if name in completed:
                    continue
                if (phase / name).exists():
                    retired = root / f'sampling-incomplete-{name}-{time.time_ns()}'
                    retired.mkdir()
                    for partial in phase.glob(name + '*'):
                        partial.rename(retired / partial.name)
                config = configuration(root, phase, name, 'int8', scene, dcf)
                result = run_one(root, phase, name, config)
                assert result['exit_code'] == 0 and result['end_reason'] == 'eof', result
    elif mode == 'resources':
        for precision in ('fp16', 'int8'):
            active_seconds = 0
            rounds = 0
            group_start = time.monotonic()
            while active_seconds < 1800 or rounds < 20:
                rounds += 1
                name = f'{precision}-resource-{rounds:03d}'
                config = configuration(root, phase, name, precision)
                result = run_one(root, phase, name, config)
                assert result['exit_code'] == 0 and result['end_reason'] == 'eof', result
                active_seconds += result['reported_run_seconds']
                write_json(phase / f'{precision}-progress.json', {
                    'precision': precision, 'rounds': rounds,
                    'cumulative_reported_run_seconds': active_seconds,
                    'group_wall_seconds': time.monotonic() - group_start,
                    'complete': active_seconds >= 1800 and rounds >= 20,
                })
    else:
        original = root.parent / 'yolo_acceptance_20260910' / 'V_DRONE_051.mp4'
        faststart = phase / 'source-faststart.mp4'
        subprocess.run(['ffmpeg', '-nostdin', '-n', '-v', 'error', '-i', str(original), '-c', 'copy',
                        '-movflags', '+faststart', str(faststart)], check=True)
        data = faststart.read_bytes()
        truncated = phase / 'truncated.mp4'
        truncated.write_bytes(data[:len(data) // 2])
        corrupt = phase / 'corrupt.mp4'
        corrupt.write_bytes(b'not a video\n')
        cases = [
            ('eof', {}, None, 0, 'eof'),
            ('sigint', {}, 30, 130, 'interrupted'),
            ('missing', {}, None, 1, 'initialization_error'),
            ('class', {'yolo_target_class_id: 2': 'yolo_target_class_id: 4'}, None, 1, 'initialization_error'),
            ('corrupt', {}, None, 1, 'initialization_error'),
            ('truncated', {}, None, 1, 'decode_error'),
        ]
        # 缺失模型与损坏输入仅改变测试配置/派生副本，不操作真实驱动或原文件。
        for precision in ('fp16', 'int8'):
            for case, edits, interrupt, expected_code, expected_reason in cases:
                if case == 'missing':
                    edits = {'/home/firefly/drone_tracker_test/models/yolov8n-model_zoo-int8_fp16-multi_cls.rknn': str(phase / 'missing.rknn')}
                if case in ('corrupt', 'truncated'):
                    edits = {str(original): str(corrupt if case == 'corrupt' else truncated)}
                name = f'{precision}-{case}'
                config = configuration(root, phase, name, precision, edits=edits)
                result = run_one(root, phase, name, config, interrupt)
                assert result['exit_code'] == expected_code and result['end_reason'] == expected_reason, result
    write_json(phase / 'complete.json', {'mode': mode, 'complete': True})


if __name__ == '__main__':
    main()

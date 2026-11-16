"""Summarize retained raw board evidence without changing the originals."""
import collections
import json
import pathlib
import re
import statistics
import sys


def main():
    root = pathlib.Path(sys.argv[1])
    results = json.loads(root.joinpath('results.json').read_text())
    cameras = [json.loads(path.read_text()) for path in root.glob('recordings/session-*/camera.json')]
    completions = [json.loads(path.read_text()) for path in root.glob('recordings/session-*/complete.json')]
    inspection = [json.loads(line) for line in root.joinpath('inspection-readback.jsonl').read_text().splitlines()]
    latency = [camera['startup']['first_eligible_mono_us'] - camera['startup']['on_mono_us'] for camera in cameras]
    log = root.joinpath('node.log').read_text()
    versions = set(re.findall(r'OSD control event=tracking round=[1-9]\d* phase=0 version=(\d+)', log))
    responses = []
    for version in versions:
        values = [int(value) for value in re.findall(
            r'OSD display[^\n]*control_version=' + version + r' control_to_display_us=(\d+)', log)]
        if values:
            responses.append(min(values))
    off_commands = []
    displays = [(int(round_id), int(stamp)) for round_id, stamp in re.findall(
        r'OSD display round=(\d+)[^\n]*hud="TRK OFF"[^\n]*monotonic_us=(\d+)', log)]
    for item in results['rounds']:
        stamp = item['off']['monotonic_ns'] // 1000
        matches = [display_stamp for round_id, display_stamp in displays
                   if round_id == item['round'] and display_stamp >= stamp]
        if matches:
            off_commands.append(min(matches) - stamp)
    rate_samples = {}
    for round_id, elapsed, consumed, displayed in re.findall(
            r'Round performance round=(\d+) elapsed_mono_us=(\d+) consumed=(\d+) hdmi_displayed=(\d+)', log):
        if int(round_id):
            rate_samples[int(round_id)] = dict(consumed_hz=int(consumed) * 1e6 / int(elapsed),
                                              displayed_hz=int(displayed) * 1e6 / int(elapsed))

    def memory(status):
        match = re.search(r'VmRSS:\s*(\d+)', status)
        return int(match.group(1)) if match else None

    summary = dict(
        rounds=len(results['rounds']), elapsed_seconds=results['elapsed_seconds'],
        combined_on_seconds=results['combined_on_seconds'], node_exit_status=results['node_exit_status'],
        startup_latency_us=dict(min=min(latency), median=statistics.median(latency), max=max(latency)),
        first_yolo=dict(collections.Counter(row.get('startup_coverage', {}).get('first_yolo') for row in inspection)),
        first_initialization=dict(collections.Counter(
            row.get('startup_coverage', {}).get('first_initialization') for row in inspection)),
        completion_outcomes=dict(collections.Counter(row['outcome'] for row in completions)),
        total_frames=sum(row['frames'] for row in completions),
        recorder_drops=sum(row['dropped'] for row in completions),
        off_display_us=dict(samples=len(responses), min=min(responses), max=max(responses)),
        off_command_to_display_us=dict(samples=len(off_commands), min=min(off_commands), max=max(off_commands)),
        rates=rate_samples,
        resources_before=dict(descriptors=results['before']['descriptors'], threads=results['before']['threads'],
                              rss_kib=memory(results['before']['status'])),
        resources_after=dict(descriptors=results['after']['descriptors'], threads=results['after']['threads'],
                             rss_kib=memory(results['after']['status'])),
        settled_round_resources=[dict(round=row['round'], descriptors=row['resources']['descriptors'],
                                      threads=row['resources']['threads'], rss_kib=memory(row['resources']['status']))
                                 for row in results['rounds']])
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()

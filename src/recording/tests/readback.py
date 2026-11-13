#!/usr/bin/env python3
"""Read every split file and reconcile preserved source stamps with session counters."""
import argparse
from collections import Counter
import json
from pathlib import Path

import rosbag2_py
from rclpy.serialization import deserialize_message
from rosidl_runtime_py.utilities import get_message


def stamp_ns(stamp):
    return stamp.sec * 1_000_000_000 + stamp.nanosec


def read_session(directory):
    summary_file = directory / 'session.json'
    summary = json.loads(summary_file.read_text()) if summary_file.exists() else None
    reader = rosbag2_py.SequentialReader()
    reader.open(rosbag2_py.StorageOptions(uri=str(directory), storage_id='sqlite3'),
                rosbag2_py.ConverterOptions('', ''))
    types = {topic.name: get_message(topic.type) for topic in reader.get_all_topics_and_types()}
    counts = Counter()
    trigger_values = []
    image_stamps = []
    pair_stamps = {'/fpv/target_bbox': [], '/fpv/target_los': []}
    final_valid = {}
    errors = []
    while reader.has_next():
        topic, data, timestamp = reader.read_next()
        message = deserialize_message(data, types[topic])
        counts[topic] += 1
        stamp = message.header.stamp if hasattr(message, 'header') else None
        if topic == '/fpv/tracker_trigger':
            stamp = message.stamp
            trigger_values.append(message.frame_id)
        if stamp is not None:
            valid = stamp.sec >= 0 and stamp.nanosec < 1_000_000_000 and stamp_ns(stamp) != 0
            if valid and stamp_ns(stamp) != timestamp:
                errors.append(f'{topic}: source timestamp changed')
        if topic in pair_stamps:
            pair_stamps[topic].append(timestamp)
            final_valid[topic] = bool(message.header.frame_id)
        if topic == '/fpv/tracker_image':
            image_stamps.append(timestamp)
            if message.encoding != 'bgr8' or len(message.data) != message.step * message.height:
                errors.append('invalid image payload')
        if topic in ('/fpv/control_command', '/fpv/control_command_raw'):
            if list(message.rotor_thrusts) != [1., 2., 3., 4.]:
                errors.append('control vector changed')
        if topic == '/fpv/low_level_feedback' and list(message.motor_speeds) != [100, 200, 300, 400]:
            errors.append('feedback vector changed')
    if pair_stamps['/fpv/target_bbox'] != pair_stamps['/fpv/target_los']:
        errors.append('observation pair timestamps/counts differ')
    if summary is not None:
        for topic, counters in summary['topics'].items():
            offered = (counters['accepted'] + counters['policy_skipped'] + counters['byte_limited']
                       + counters['oversized'] + counters['queue_dropped'])
            if counters['offered'] != offered:
                errors.append(f'{topic}: offered counters do not reconcile')
            if counters['written'] != counts[topic]:
                errors.append(f'{topic}: readback/written counts differ')
            if counters['accepted_but_unwritten'] != counters['accepted'] - counters['written']:
                errors.append(f'{topic}: accepted-but-unwritten does not reconcile')
        if summary['phase'] == 'complete' and any(value['queue_dropped'] or value['oversized']
                or value['accepted_but_unwritten'] for value in summary['topics'].values()):
            errors.append('complete session contains unexpected gaps')
        if summary['configuration']['catalog']:
            image_names = {value['name'] for value in summary['configuration']['catalog']
                           if value['id'] == 'tracker_image'}
            if not image_stamps and image_names.intersection(types):
                errors.append('unobserved image topic metadata exists')
    return dict(path=str(directory), counts=dict(counts), triggers=trigger_values,
                image_stamps=image_stamps, final_pair_valid=final_valid,
                phase=summary['phase'] if summary else 'legacy_no_summary',
                files=len(list(directory.glob('*.db3'))), errors=errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    results = [read_session(path) for path in sorted(args.directory.glob('drone_rosbag_*'))]
    args.output.write_text(json.dumps(results, indent=2) + '\n')
    if any(result['errors'] for result in results):
        raise SystemExit('bag readback failed; see output errors')
    print(f'Read back {len(results)} complete multi-file bags')


if __name__ == '__main__':
    main()

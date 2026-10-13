#!/usr/bin/env python3
"""Select the exact source frames, decode luminance, and freeze source evidence."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sqlite3
import struct
import subprocess
import sys
import shutil
import common


def select_frames(rows, start=common.START_US, end=common.END_US):
    previous = -1
    epoch = None
    chosen = []
    for index, source in enumerate(rows):
        pts = source['pts_us']
        if not isinstance(pts, int) or pts <= previous:
            raise ValueError(f'frame {index}: pts_us repeated or backwards')
        previous = pts
        zero = source['timestamp_us'] - pts
        if epoch is None:
            epoch = zero
        if zero != epoch:
            raise ValueError(f'frame {index}: timestamp_us - pts_us does not share source epoch')
        if start <= pts < end:
            chosen.append({'schema_version': 1, 'fixture_frame_id': len(chosen),
                           'source_frame_id': index, 'driver_sequence': source['sequence'],
                           'source_timestamp_us': source['timestamp_us'], 'pts_us': pts,
                           'relative_us': pts - start})
    return chosen, epoch


class CdrImu:
    """Decode the fixed ROS 2 sensor_msgs/Imu CDR layout; keep every source byte."""
    def __init__(self, data):
        if data[:2] not in (b'\x00\x01', b'\x00\x00'):
            raise ValueError('gimbal: unsupported CDR serialization')
        self.data = data[4:]
        self.offset = 0
        self.endian = '<' if data[1] else '>'

    def read(self, fmt, alignment):
        self.offset += (-self.offset) % alignment
        size = struct.calcsize(fmt)
        result = struct.unpack_from(self.endian + fmt, self.data, self.offset)
        self.offset += size
        return result

    def decode(self):
        sec, nsec = self.read('iI', 4)
        length, = self.read('I', 4)
        frame_id = self.data[self.offset:self.offset + length - 1].decode('utf-8')
        self.offset += length
        q = self.read('4d', 8)
        numeric = {'orientation': list(q), 'orientation_covariance': list(self.read('9d', 8)),
                   'angular_velocity': list(self.read('3d', 8)),
                   'angular_velocity_covariance': list(self.read('9d', 8)),
                   'linear_acceleration': list(self.read('3d', 8)),
                   'linear_acceleration_covariance': list(self.read('9d', 8))}
        if not 0 <= nsec < 1_000_000_000 or not all(math.isfinite(v) for v in q) or sum(v*v for v in q) == 0:
            raise ValueError('gimbal: invalid header.stamp or orientation')
        return sec * 1_000_000 + nsec // 1000, frame_id, numeric


def extract_attitude(bag_files, epoch, start=common.START_US, end=common.END_US):
    records = []
    for file in bag_files:
        with sqlite3.connect(f'file:{file}?mode=ro', uri=True) as db:
            topic = db.execute("SELECT id,type,serialization_format FROM topics WHERE name='/fpv/gimbal'").fetchone()
            if not topic or topic[1:] != ('sensor_msgs/msg/Imu', 'cdr'):
                raise ValueError(f'{file}: /fpv/gimbal sensor_msgs/msg/Imu cdr required')
            for timestamp, data in db.execute('SELECT timestamp,data FROM messages WHERE topic_id=? ORDER BY timestamp,id', (topic[0],)):
                stamp, frame_id, fields = CdrImu(data).decode()
                records.append({'schema_version': 1, 'source_header_us': stamp,
                                'bag_storage_ns': timestamp, 'source_bag': file.name,
                                'source_cdr_sha256': hashlib.sha256(data).hexdigest(),
                                'frame_id': frame_id, 'relative_us': stamp - epoch - start, **fields})
    previous = None
    initial = None
    selected = []
    for row in records:
        stamp = row['source_header_us']
        if previous is not None and stamp <= previous:
            raise ValueError(f'gimbal header.stamp {stamp}: repeated or backwards')
        previous = stamp
        if row['relative_us'] < 0:
            initial = row
        elif row['relative_us'] < end - start:
            selected.append(row)
    if initial is None:
        raise ValueError('gimbal: initial attitude before boundary missing; cannot synthesize it')
    if not selected:
        raise ValueError('gimbal: no interval attitude; source epoch cannot be confirmed')
    return [initial] + selected


# These non-configurable literals are unchanged in the original source and imported implementation.
# The executable checks them before construction; changes require a new comparison group.
NANO_CONSTANTS = {'score_size':15,'min_wh':5,'template_size':127,'instance_size':255,
                  'window_influence':0.455,'track_lr':0.348,'track_penalty_k':0.138,'stride':16,
                  'context_ratio':0.5}

def freeze_algorithm(session):
    identity = session['identity']
    params, models = {}, {}
    for row in identity.splitlines():
        if ' path=' in row:
            match = re.fullmatch(r'(\S+) path=(.+) sha256=([0-9a-f]{64})', row)
            if not match:
                raise ValueError('session.identity: malformed model identity')
            key, path, digest = match.groups()
            models[key] = {'path': path, 'sha256': digest}
        elif '=' in row:
            key, text = row.split('=', 1)
            if key.startswith(('tracker.', 'camera.', 'mv_camera.')) or key in ('use_history_timestamp', 'use_sim_time'):
                if text in ('true', 'false'):
                    value = text == 'true'
                elif re.fullmatch(r'-?\d+', text):
                    value = int(text)
                elif re.fullmatch(r'-?\d+\.\d+', text):
                    value = float(text)
                else:
                    value = text
                params[key] = value
    required = ('tracker.yolo_engine', 'tracker.nano_template_backbone', 'tracker.nano_search_backbone', 'tracker.nano_head')
    for key in required:
        if key not in models:
            raise ValueError(f'session.identity.{key}: model fingerprint missing')
    for key in ('tracker.yolo_interval', 'tracker.lost_max_count', 'tracker.frame_buffer_size', 'camera.fx', 'camera.fy'):
        if key not in params:
            raise ValueError(f'session.identity.{key}: effective parameter missing')
    return {'schema_version': 1, 'parameters': params, 'models': models, 'nano_constants':NANO_CONSTANTS,
            'nano_contract_source':'ac96d0c:src/hybrid_tracker/nano_tracker.h; unchanged literal values'}


def raw_frames(path, width, height):
    command = ['ffmpeg', '-v', 'error', '-i', str(path), '-map', '0:v:0', '-vf', 'extractplanes=y',
               '-vsync', '0', '-f', 'rawvideo', '-pix_fmt', 'gray', '-']
    # gray FFV1 already is a luminance plane; extractplanes=y supports it as well.
    process = subprocess.Popen(command, stdout=subprocess.PIPE)
    try:
        size = width * height
        while True:
            pixels = bytearray()
            while len(pixels) < size:
                part = process.stdout.read(size - len(pixels))
                if not part:
                    break
                pixels.extend(part)
            if not pixels:
                break
            if len(pixels) != size:
                raise ValueError(f'{path}: partial decoded frame')
            yield bytes(pixels)
        if process.wait() != 0:
            raise ValueError(f'{path}: FFmpeg decode failed')
    finally:
        process.stdout.close()
        if process.poll() is None:
            process.terminate()
        process.wait()


def prepare(root, output):
    source = root / common.SOURCE
    for name, digest in common.SOURCE_HASHES.items():
        common.verify_hash(source / name, digest)
    rows = common.lines(source / '000001.frames.jsonl') + common.lines(source / '000002.frames.jsonl')
    chosen, epoch = select_frames(rows)
    expected = (len(chosen), chosen[0]['source_frame_id'], chosen[-1]['source_frame_id'],
                chosen[0]['relative_us'], chosen[-1]['relative_us'])
    if expected != (1200, 3302, 4501, 15721, 19992663):
        raise ValueError(f'fixed frame selection mismatch: {expected}')
    bag_files = sorted((root / 'test-01-20260929-1513').glob('drone_rosbag_*/*.db3'))
    if not bag_files:
        raise ValueError('source bag: missing .db3')
    attitude = extract_attitude(bag_files, epoch)
    algorithm = freeze_algorithm(common.load(source / 'session.json'))
    output.mkdir(parents=True, exist_ok=False)
    clip = output / 'clip.mkv'
    command = ['ffmpeg', '-v', 'error', '-f', 'rawvideo', '-pix_fmt', 'gray', '-s', '1920x1200',
               '-r', '60', '-i', '-', '-an', '-c:v', 'ffv1', '-level', '3', '-threads', '8', '-slices', '24', '-coder', '0', '-context', '0', '-slicecrc', '0', '-pix_fmt', 'gray', str(clip)]
    encoder = subprocess.Popen(command, stdin=subprocess.PIPE)
    try:
        selected = {row['source_frame_id']: row for row in chosen}
        count = 0
        for index, pixels in enumerate(raw_frames(source / 'test-01-20260929-1513.mp4', 1920, 1200)):
            if index in selected:
                selected[index]['pixel_sha256'] = hashlib.sha256(pixels).hexdigest()
                encoder.stdin.write(pixels)
                count += 1
        encoder.stdin.close()
        if encoder.wait() != 0 or count != 1200:
            raise ValueError(f'clip.mkv: encoder failure or missing frames ({count})')
    finally:
        if encoder.poll() is None:
            encoder.kill()
        encoder.wait()
    common.write_lines(output / 'frames.jsonl', chosen)
    common.write_lines(output / 'attitude.jsonl', attitude)
    # JSON is a strict YAML subset, also readable by yaml-cpp without PyYAML.
    common.write(output / 'algorithm.yaml', algorithm)
    sources = [{'path': str((source / name).relative_to(root)), 'sha256': digest}
               for name, digest in common.SOURCE_HASHES.items()]
    sources.append({'path': str((source / 'session.json').relative_to(root)), 'sha256': common.sha256(source / 'session.json')})
    for file in bag_files:
        sources.append({'path': str(file.relative_to(root)), 'sha256': common.sha256(file)})
    fixture = {'schema_version': 1, 'case_id': common.CASE_ID, 'frame_count': 1200,
               'width': 1920, 'height': 1200, 'pixel_format': 'gray8', 'duration_us': 20_000_000,
               'start_us': common.START_US, 'end_us': common.END_US, 'source_epoch_us': epoch,
               'time_mapping': 'source_header_us - source_epoch_us - start_us', 'sources': sources,
               'files': {name: common.sha256(output / name) for name in ('clip.mkv', 'frames.jsonl', 'attitude.jsonl', 'algorithm.yaml')},
               'tool_sha256': common.sha256(Path(__file__)),
               'encoding':{'codec':'ffv1','level':3,'threads':8,'slices':24,'coder':0,'context':0,'slicecrc':0},
               'ffmpeg_version': subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0]}
    common.validate_record(fixture, 'fixture')
    common.write(output / 'fixture.json', fixture)
    from verify_fixture import verify
    verify(output, root)
    return fixture


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.input_root, args.output), indent=2))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)

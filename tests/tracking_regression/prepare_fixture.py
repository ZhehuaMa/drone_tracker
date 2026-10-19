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
        if type(pts) is not int or pts < 0 or pts <= previous:
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
        if row['relative_us'] <= 0:
            initial = row
        elif row['relative_us'] < end - start:
            selected.append(row)
    if initial is None:
        raise ValueError('gimbal: initial attitude before boundary missing; cannot synthesize it')
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


def probe_source(video, rows):
    """Prove frame correspondence and a tail boundary without using nominal frame rate."""
    from configuration import seconds_us
    probe = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_frames', '-show_streams',
        '-show_entries', 'frame=best_effort_timestamp,best_effort_timestamp_time,pkt_duration_time,width,height:stream=time_base,start_time,duration,width,height',
        '-of', 'json', str(video)], text=True))
    streams, frames = probe.get('streams', []), probe.get('frames', [])
    if len(streams) != 1 or not frames or len(frames) != len(rows):
        raise ValueError('source.video: decoded frame count does not match frame_logs')
    stream = streams[0]
    def us(value, field):
        from decimal import Decimal
        try:
            return seconds_us(Decimal(value), field)
        except (TypeError, ValueError, KeyError) as error:
            raise ValueError(field + ': trustworthy source timing missing') from error
    pts = [us(frame.get('best_effort_timestamp_time'), f'source.video.frames[{index}].pts')
           for index, frame in enumerate(frames)]
    shift = rows[0]['pts_us'] - pts[0]
    recorded_ticks = any('pts_90k' in row for row in rows)
    if recorded_ticks and stream.get('time_base') != '1/90000':
        raise ValueError('source.video: recorded pts_90k requires time_base 1/90000')
    for index, (row, frame) in enumerate(zip(rows, frames)):
        if recorded_ticks:
            # Recording uses av_rescale_q: integer microseconds are rounded to
            # the nearest 90 kHz tick. Prove that exact conversion and the
            # encoded tick, rather than treating quantization as missing time.
            tick = row.get('pts_90k')
            matches = (type(tick) is int and type(frame.get('best_effort_timestamp')) is int
                       and tick == frame['best_effort_timestamp']
                       and tick == (row['pts_us'] * 9 + 50) // 100)
        else:
            matches = abs(row['pts_us'] - pts[index] - shift) <= 1
        if not matches:
            raise ValueError(f'source.frame_logs[{index}].pts_us: does not match video timing')
        if frame.get('width') != stream['width'] or frame.get('height') != stream['height']:
            raise ValueError(f'source.video.frames[{index}]: changing dimensions')
    start = us(stream.get('start_time'), 'source.video.start_time') + shift
    duration = frames[-1].get('pkt_duration_time')
    if duration is not None and us(duration, 'source.video.last_duration') > 0:
        end = pts[-1] + shift + us(duration, 'source.video.last_duration')
    elif stream.get('duration') is not None:
        end = start + us(stream['duration'], 'source.video.duration')
    else:
        raise ValueError('source.video: trustworthy tail duration missing')
    if start < 0 or start > rows[0]['pts_us'] or end <= rows[-1]['pts_us']:
        raise ValueError('source.video: trustworthy timeline boundaries missing')
    return {'source_start_us': start, 'source_end_us': end,
            'width': stream['width'], 'height': stream['height'], 'source_frame_count': len(rows)}


def resolve_selection(clip, metadata, rows):
    requested = clip.get('frame_selection_interval')
    start = requested['start_us'] if requested else metadata['source_start_us']
    end = requested['end_us'] if requested else metadata['source_end_us']
    if end == 'end':
        end = metadata['source_end_us']
    if start < metadata['source_start_us'] or end > metadata['source_end_us'] or start >= end:
        raise ValueError('frame_selection_interval: outside verified source timeline')
    chosen, epoch = select_frames(rows, start, end)
    if not chosen:
        raise ValueError('frame_selection_interval: empty selected frame set')
    windows = {'full': {'start_us': 0, 'end_us': end - start}, **clip['analysis_windows']}
    for name, window in windows.items():
        if not 0 <= window['start_us'] < window['end_us'] <= end - start:
            raise ValueError(f'analysis_windows.{name}: outside selected duration')
    return start, end, chosen, epoch, windows


def prepare(clip, output):
    """Build a new variable-length fixture; no source is ever opened for writing."""
    source = clip['source']
    files = [Path(source['video']), *(Path(item) for item in source['frame_logs']),
             *(Path(item) for item in source['attitude_bags']), Path(source['session'])]
    sources = [{'path': str(file.resolve()), 'sha256': common.sha256(file)} for file in files]
    rows = [row for file in source['frame_logs'] for row in common.lines(file)]
    metadata = probe_source(source['video'], rows)
    start, end, chosen, epoch, windows = resolve_selection(clip, metadata, rows)
    attitude = extract_attitude([Path(item) for item in source['attitude_bags']], epoch, start, end)
    algorithm = freeze_algorithm(common.load(source['session']))
    for name in ('camera.fx', 'camera.fy'):
        if not isinstance(algorithm['parameters'][name], (int, float)) or algorithm['parameters'][name] <= 0:
            raise ValueError(f'algorithm.parameters.{name}: positive focal length required')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    width, height = metadata['width'], metadata['height']
    encoder = subprocess.Popen(['ffmpeg', '-v', 'error', '-f', 'rawvideo', '-pix_fmt', 'gray',
        '-s', f'{width}x{height}', '-r', '60', '-i', '-', '-an', '-c:v', 'ffv1', '-level', '3',
        '-threads', '8', '-pix_fmt', 'gray', str(output / 'clip.mkv')], stdin=subprocess.PIPE)
    try:
        selected = {row['source_frame_id']: row for row in chosen}
        decoded_count = 0
        for index, pixels in enumerate(raw_frames(source['video'], width, height)):
            decoded_count += 1
            if index in selected:
                selected[index]['pixel_sha256'] = hashlib.sha256(pixels).hexdigest()
                encoder.stdin.write(pixels)
        encoder.stdin.close()
        if encoder.wait() != 0 or decoded_count != len(rows) or any('pixel_sha256' not in row for row in chosen):
            raise ValueError('source.video: decode/encoder failure or frame count mismatch')
        common.write_lines(output / 'frames.jsonl', chosen)
        common.write_lines(output / 'attitude.jsonl', attitude)
        common.write(output / 'algorithm.yaml', algorithm)
        for item in sources:
            common.verify_hash(item['path'], item['sha256'])
        fixture = {'schema_version': 2, 'case_id': clip['case_id'], **metadata,
                   'frame_count': len(chosen), 'pixel_format': 'gray8', 'duration_us': end - start,
                   'start_us': start, 'end_us': end, 'source_epoch_us': epoch, 'sources': sources,
                   'selection_mode': 'interval' if 'frame_selection_interval' in clip else 'whole',
                   'selection_requested': clip.get('frame_selection_interval'), 'analysis_windows': windows,
                   'time_mapping': 'source_header_us - source_epoch_us - start_us',
                   'files': {name: common.sha256(output / name) for name in
                             ('clip.mkv', 'frames.jsonl', 'attitude.jsonl', 'algorithm.yaml')},
                   'tool_sha256': common.sha256(Path(__file__)),
                   'ffmpeg_version': subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0]}
        common.validate_record(fixture, 'fixture')
        common.write(output / 'fixture.json', fixture)
        from verify_fixture import verify
        verify(output, Path('/'))
        return fixture
    except BaseException:
        # A failed directory remains diagnostic evidence, never a runnable fixture.
        (output / 'fixture.json').unlink(missing_ok=True)
        raise
    finally:
        if encoder.poll() is None:
            encoder.kill()
        encoder.wait()


if __name__ == '__main__':
    sys.exit('Use run_suite.py CONFIG.yaml with operation: prepare; prepare_fixture.py is an internal component.')

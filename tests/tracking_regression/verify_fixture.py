#!/usr/bin/env python3
"""Reject altered fixed input before a model can be loaded."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import common
from prepare_fixture import raw_frames


def verify(directory, source_root=None, *, verify_pixels=True):
    fixture = common.load(directory / 'fixture.json')
    common.validate_record(fixture, 'fixture')
    if (fixture['start_us'], fixture['end_us'], fixture['pixel_format']) != (55_000_000,75_000_000,'gray8'):
        raise ValueError('fixture: fixed interval or pixel format mismatch')
    sources = {row['path']:row['sha256'] for row in fixture['sources']}
    for name,digest in common.SOURCE_HASHES.items():
        if sources.get(common.SOURCE+'/'+name) != digest:
            raise ValueError(f'fixture.sources.{name}: fixed source identity mismatch')
    probe = json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0',
        '-show_entries','stream=codec_name,width,height,pix_fmt','-of','json',str(directory/'clip.mkv')],text=True))
    streams = probe['streams']
    if len(streams) != 1 or streams[0] != {'codec_name':'ffv1','width':1920,'height':1200,'pix_fmt':'gray'}:
        raise ValueError('clip.mkv: FFV1 gray 1920x1200 required')
    if (fixture['width'], fixture['height'], fixture['frame_count'], fixture['duration_us']) != (1920, 1200, 1200, 20_000_000):
        raise ValueError('fixture: fixed dimensions/count/duration mismatch')
    for name, digest in fixture['files'].items():
        common.verify_hash(directory / name, digest)
    for required in ('clip.mkv', 'frames.jsonl', 'attitude.jsonl', 'algorithm.yaml'):
        if required not in fixture['files']:
            raise ValueError(f'fixture.files.{required}: missing')
    if source_root is not None:
        for source in fixture['sources']:
            common.verify_hash(source_root / source['path'], source['sha256'])
    frames = common.lines(directory / 'frames.jsonl')
    previous = -1
    for index, row in enumerate(frames):
        common.validate_record(row, 'frame')
        if row['fixture_frame_id'] != index or row['source_frame_id'] != 3302 + index:
            raise ValueError(f'frame {index}: frame identity mismatch')
        if not 0 <= row['relative_us'] < 20_000_000 or row['relative_us'] <= previous:
            raise ValueError(f'frame {index}: relative_us repeated/backwards/outside interval')
        if row['pts_us'] - fixture['start_us'] != row['relative_us']:
            raise ValueError(f'frame {index}: pts_us mapping mismatch')
        previous = row['relative_us']
    if len(frames) != 1200 or (frames[0]['relative_us'], frames[-1]['relative_us']) != (15721, 19992663):
        raise ValueError('frames.jsonl: fixed frame count or boundary mismatch')
    if verify_pixels:
        count = 0
        for index, pixels in enumerate(raw_frames(directory / 'clip.mkv', 1920, 1200)):
            if index >= len(frames) or hashlib.sha256(pixels).hexdigest() != frames[index]['pixel_sha256']:
                raise ValueError(f'clip.mkv frame {index}: luminance pixel checksum mismatch')
            count += 1
        if count != len(frames):
            raise ValueError(f'clip.mkv: decoded count {count} != {len(frames)}')
    attitudes = common.lines(directory / 'attitude.jsonl')
    if not attitudes or attitudes[0]['relative_us'] >= 0:
        raise ValueError('attitude.jsonl: initial attitude before boundary missing')
    previous = None
    for index, row in enumerate(attitudes):
        common.validate_record(row, 'attitude')
        if previous is not None and row['relative_us'] <= previous:
            raise ValueError(f'attitude {index}: repeated/backwards relative_us')
        if row['relative_us'] != row['source_header_us'] - fixture['source_epoch_us'] - fixture['start_us']:
            raise ValueError(f'attitude {index}: header.stamp mapping mismatch')
        if row['relative_us'] >= 20_000_000 or (index and row['relative_us'] < 0):
            raise ValueError(f'attitude {index}: outside selected interval')
        q=row['orientation']
        if len(q)!=4 or not all(math.isfinite(x) for x in q) or sum(x*x for x in q)==0:
            raise ValueError(f'attitude {index}: orientation invalid')
        for key in ('orientation_covariance','angular_velocity','angular_velocity_covariance',
                    'linear_acceleration','linear_acceleration_covariance'):
            if not all(math.isfinite(x) for x in row[key]):
                raise ValueError(f'attitude {index}: {key} invalid')
        previous = row['relative_us']
    algorithm=common.load(directory/'algorithm.yaml')
    from prepare_fixture import NANO_CONSTANTS
    if algorithm.get('nano_constants') != NANO_CONSTANTS:
        raise ValueError('algorithm.nano_constants: frozen source algorithm contract mismatch')
    return fixture


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('--source-root', type=Path)
    args = parser.parse_args()
    try:
        verify(args.fixture, args.source_root)
        print('Fixture files, frame identities, timing and decoded pixels verified.')
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)

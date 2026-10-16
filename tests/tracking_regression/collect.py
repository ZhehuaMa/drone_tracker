#!/usr/bin/env python3
"""Read complete native diagnostic records and retain every original numeric field."""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import shutil
import struct
import sys
import zlib
import common
import metrics

HEADER = struct.Struct('<IHHIII')
EVENT = struct.Struct('<HHHHIIQQQQQQIIQQQQQQQQ4f4f4f7Q4I')
COMMIT = struct.Struct('<10Q')
assert HEADER.size == 20 and EVENT.size == 256 and COMMIT.size == 80
FIELDS = ['version','byte_length','type_id','reason_id','flags','producer_id','run_id','round_id',
          'producer_seq','mono_ns','capture_id','frame_round_id','driver_sequence','frame_flags',
          'update_id','tracker_frame_id','request_id','source_update_id','source_tracker_frame_id',
          'frame_time_us','prediction_us','duration_ns']


def decode_event(data):
    values = EVENT.unpack(data)
    row = dict(zip(FIELDS, values[:22]))
    if row['version'] != 1 or row['byte_length'] != 256 or row['producer_id'] >= 4:
        raise ValueError('event: unsupported version, byte_length or producer_id')
    if row['type_id'] >= len(metrics.TYPE_NAMES) or row['reason_id'] >= len(metrics.REASON_NAMES):
        raise ValueError('event: unknown type_id or reason_id')
    row.update(type=metrics.TYPE_NAMES[row['type_id']], reason=metrics.REASON_NAMES[row['reason_id']],
               box=list(values[22:26]), other_box=list(values[26:30]), score=values[30], iou=values[31],
               threshold=values[32], highest_score=values[33], values=list(values[34:41]), counts=list(values[41:45]))
    row['error_code_known'] = row['type'] == 'InitializeEnd' and bool(row['flags'] & 4096)
    row['error_domain'] = row['values'][1] if row['error_code_known'] else None
    row['error_code'] = row['values'][2] if row['error_code_known'] else None
    return row


def scan(file):
    events, commits, errors = [], [], []
    complete, end_boundary, offset = False, 0, 0
    with file.open('rb') as stream:
        while True:
            raw = stream.read(HEADER.size)
            if not raw:
                break
            if len(raw) != HEADER.size:
                errors.append(f'{file}: partial record header at {offset}')
                break
            magic, version, kind, length, crc, endian = HEADER.unpack(raw)
            if magic != 0x44544731 or version != 1 or endian != 0x01020304 or length > 1048576:
                errors.append(f'{file}: unsupported header at {offset}')
                break
            data = stream.read(length)
            zero = HEADER.pack(magic,version,kind,length,0,endian)
            if len(data) != length or zlib.crc32(data) ^ zlib.crc32(zero) != crc:
                errors.append(f'{file}: CRC/partial payload at {offset}')
                break
            if kind == 1 and length and length % 256 == 0:
                try:
                    events.extend(decode_event(data[i:i+256]) for i in range(0,length,256))
                except ValueError as error:
                    errors.append(f'{file}:{offset}: {error}')
                    break
            elif kind == 2 and length == 80:
                commit = COMMIT.unpack(data)
                if commit[0] != offset:
                    errors.append(f'{file}: invalid commit boundary at {offset}')
                    break
                commits.append(commit)
                if any(commit[6:]):
                    errors.append(f'{file}: committed producer drops {list(commit[6:])}')
            elif kind == 3 and length == 4:
                flag, = struct.unpack('<I',data)
                complete = flag == 1
                end_boundary = offset + HEADER.size + length
            else:
                errors.append(f'{file}: invalid kind/length at {offset}')
                break
            offset += HEADER.size + length
    if not complete or not commits or commits[-1][0] < end_boundary:
        errors.append(f'{file}: normal end and synchronized final commit required')
    return events, errors


def pairs(events, start, end, field='update_id', skip_detail=False):
    starts, ends = Counter(), Counter()
    for event in events:
        if event['type'] not in (start,end):
            continue
        if skip_detail and event['type']==end and (event['values'][0] or event['flags'] & 4096):
            continue
        key = (event['run_id'],event['round_id'],event[field])
        (starts if event['type']==start else ends)[key] += 1
    return [f'{start}/{end}: unpaired identity {key}, starts={starts[key]}, ends={ends[key]}'
            for key in starts.keys()|ends.keys() if starts[key]!=1 or ends[key]!=1]


def qualify(events, inputs, clock, source, config, preparation=None):
    reasons = []
    preparation = preparation or {}
    if not preparation.get('ready') or preparation.get('input_mode') != 'predecode_all':
        reasons.append('preparation: complete verified cache missing')
    if preparation.get('verified_frame_count') != 1200 or preparation.get('cached_pixel_bytes') != 2764800000:
        reasons.append('preparation: verified cached frame count or bytes mismatch')
    end = preparation.get('end_mono_ns', 0)
    if not preparation.get('begin_mono_ns', 0) or end < preparation.get('begin_mono_ns', 0) or not end:
        reasons.append('preparation: completion timing missing')
    if not clock.get('m0_ns') or end >= clock.get('m0_ns', 0):
        reasons.append('preparation: playback clock established before ready')
    if preparation.get('reason'):
        reasons.append('preparation: ' + preparation['reason'])
    for name in ('process_rss_kib', 'process_peak_rss_kib', 'system_total_kib', 'system_available_kib'):
        if not isinstance(preparation.get(name), int) or preparation[name] <= 0:
            reasons.append('preparation: memory observation missing ' + name)
    identities = set()
    producer_sequences = defaultdict(list)
    runs = set()
    for event in events:
        key=(event['run_id'],event['producer_id'],event['producer_seq'])
        if event['producer_seq']:
            if key in identities:
                reasons.append(f'duplicate event identity {key}')
            identities.add(key)
        runs.add(event['run_id'])
        if event['producer_seq']:
            producer_sequences[(event['run_id'],event['producer_id'])].append(event['producer_seq'])
        if event['type'] in ('Gap','AbnormalEnd'):
            reasons.append(f'{event["type"]}: {key}')
        if event['type']=='Health' and (event['values'][3] or event['values'][6] or event['values'][4]>500000000):
            reasons.append(f'health drops/error {key}')
        if event['type']=='PublishEnd' and (event['reason_id']!=0 or not event['flags']&128):
            reasons.append(f'publication failed {key}')
        if event['type']=='DetectStage' and event['reason_id']!=22 and not event['flags']&2048:
            reasons.append(f'inference observation capability missing {key}')
    if len(runs)!=1:
        reasons.append(f'exactly one diagnostic run required, found {len(runs)}')
    for producer,sequence in producer_sequences.items():
        sequence.sort()
        if sequence != list(range(1, max(sequence)+1)):
            reasons.append(f'producer sequence gap: {producer}')
    for start,end,field,detail in [('UpdateStart','UpdateEnd','update_id',False),
        ('PublishStart','PublishEnd','update_id',False),('DetectStart','DetectEnd','request_id',False),
        ('InitializeStart','InitializeEnd','update_id',True)]:
        reasons.extend(pairs(events,start,end,field,detail))
    for start,end in [('RunStart','RunEnd'),('RoundStart','RoundEnd')]:
        if sum(e['type']==start for e in events)!=1 or sum(e['type']==end for e in events)!=1:
            reasons.append(f'{start}/{end}: exactly one normal lifecycle required')
    accepted={(e['run_id'],e['request_id']) for e in events if e['type']=='RequestDecision' and e['flags']&128}
    terminal={(e['run_id'],e['request_id']) for e in events if e['type'] in ('DetectEnd','RequestCancel')}
    for identity in accepted-terminal:
        reasons.append(f'accepted request has no terminal outcome {identity}')
    # A superseded request never executes; its successor must be an accepted identity.
    superseded = [e for e in events if e['type']=='RequestCancel' and e['reason']=='Superseded']
    executed = {(e['run_id'], e['request_id']) for e in events if e['type']=='DetectStart'}
    seen = set()
    for event in superseded:
        key = (event['run_id'], event['request_id'])
        successor = (event['run_id'], event['values'][0])
        if key in seen or key in executed or successor not in accepted or key == successor:
            reasons.append(f'invalid supersession chain {key} -> {successor}')
        seen.add(key)
    if len(inputs)!=1200 or [row['fixture_frame_id'] for row in inputs]!=list(range(1200)):
        reasons.append('input: fixture identity set incomplete')
    by_time={row['frame_time_us']:row['fixture_frame_id'] for row in inputs if row['supplied']}
    if len(by_time)!=sum(row['supplied'] for row in inputs):
        reasons.append('input: ambiguous exact frame timestamps')
    destinations=defaultdict(list)
    for event in events:
        if event['frame_time_us'] in by_time:
            event['fixture_frame_id']=by_time[event['frame_time_us']]
        if event['type'] in ('FrameSelect','FrameSkip'):
            if event.get('fixture_frame_id') is None:
                reasons.append(f'{event["type"]}: unknown fixture timestamp {event["frame_time_us"]}')
            else:
                destinations[event['fixture_frame_id']].append('selected' if event['type']=='FrameSelect' else 'skipped')
    for row in inputs:
        key=row['fixture_frame_id']
        if not row['supplied']:
            row['destination']='not_supplied'
            reasons.append(f'frame {key}: not supplied')
        elif not row['enqueued']:
            row['destination']='rejected'
        elif row.get('residual') and not destinations[key]:
            row['destination']='residual'
        elif len(destinations[key])==1:
            row['destination']=destinations[key][0]
        else:
            row['destination']='unknown'
            reasons.append(f'frame {key}: terminal destination count {len(destinations[key])}')
        if row['supplied'] and (row['supplied_mono_ns']-row['planned_mono_ns'])//1000 > config['max_supply_lateness_us']:
            reasons.append(f'frame {key}: supply lateness budget exceeded')
        if row['planned_mono_ns'] and row['planned_mono_ns']!=clock['m0_ns']+row['relative_us']*1000:
            reasons.append(f'frame {key}: source time mapping invalid')
    if not source.get('normal_eof'):
        reasons.append('source: normal EOF missing')
    if source.get('reason'):
        reasons.append('source: '+source['reason'])
    return sorted(set(reasons))


def csv_file(path,rows,fields):
    with path.open('x',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=fields,extrasaction='ignore')
        writer.writeheader()
        for row in rows:
            writer.writerow({key:json.dumps(value) if isinstance(value,(dict,list)) else value for key,value in row.items()})


def collect(directory, extra_reasons=()):
    reasons=list(extra_reasons)
    raw=sorted((directory/'raw').glob('**/*.dtd'))
    events=[]
    if not raw:
        reasons.append('diagnostics: raw binary segments missing')
    with (directory/'events.bin').open('xb') as combined:
        for file in raw:
            values,errors=scan(file)
            events.extend(values)
            reasons.extend(errors)
            combined.write(file.read_bytes())
    events.sort(key=lambda e:(e['mono_ns'],e['producer_id'],e['producer_seq']))
    inputs=common.lines(directory/'input.jsonl') if (directory/'input.jsonl').exists() else []
    clock=common.load(directory/'input-clock.json') if (directory/'input-clock.json').exists() else {'m0_ns':0,'e0_us':0}
    config=common.load(directory/'run-config.json')
    source=common.load(directory/'source-completion.json') if (directory/'source-completion.json').exists() else {}
    preparation=common.load(directory/'preparation.json') if (directory/'preparation.json').exists() else {}
    identity=common.load(directory/'identity-preflight.json')
    identity['clock']=clock
    common.write(directory/'identity.json',identity)
    try:
        common.validate_record(identity,'identity')
        common.validate_record(preparation,'preparation')
        reasons.extend(qualify(events,inputs,clock,source,config,preparation))
    except (ValueError,KeyError) as error:
        reasons.append(str(error))
    common.write_lines(directory/'events.jsonl',events)
    csv_file(directory/'frames.csv',inputs,['fixture_frame_id','relative_us','planned_mono_ns','supplied_mono_ns',
             'frame_time_us','supplied','enqueued','residual','destination'])
    publications=[e for e in events if e['type']=='PublishEnd']
    outputs={(e['run_id'],e['round_id'],e['update_id']):e for e in events if e['type']=='Output'}
    messages=[]
    for event in publications:
        output=outputs.get((event['run_id'],event['round_id'],event['update_id']))
        if output is None:
            reasons.append(f'publication: output missing for update {event["update_id"]}')
        messages.append({**event,'published_box':output['other_box'] if output else None,
                         'tracking':output['values'][1] if output else None,
                         'subscriber_received':None})
    csv_file(directory/'messages.csv',messages,['run_id','round_id','update_id','fixture_frame_id','mono_ns',
             'frame_time_us','prediction_us','reason','flags','published_box','tracking','subscriber_received'])
    if (directory/'submissions.csv').exists():
        with (directory/'submissions.csv').open() as stream:
            submissions=list(csv.DictReader(stream))
        durations=[int(row['duration_ns'])/1000 for row in submissions]
        common.write(directory/'observation-overhead.json',{'unit':'us','mode':'basic',
            'submission':metrics.statistics(durations),'sample_count':len(durations),
            'interpretation':'Measured submit cost includes clock sampling and notification; this is not off/basic total overhead acceptance.'})
        if not durations: reasons.append('observation measurement missing submissions')
        if (directory/'sampling-status.txt').exists():
            for row in (directory/'sampling-status.txt').read_text().splitlines():
                if int(row.split('=')[1]):reasons.append('observation measurement overflow: '+row)
    else: reasons.append('observation measurement missing')
    computed=metrics.compute(events,inputs,clock,complete=not reasons)
    if not computed.get('record_complete') or computed.get('issues'):
        reasons.extend('metrics: '+str(issue) for issue in computed.get('issues',[]))
        if not computed.get('record_complete'):
            reasons.append('metrics: record incomplete')
        computed=metrics.compute(events,inputs,clock,complete=False)
    common.write(directory/'metrics.json',computed)
    files={str(file.relative_to(directory)):common.sha256(file) for file in directory.rglob('*')
           if file.is_file() and file.name!='completion.json'}
    completion={'schema_version':1,'normal_end':bool(source.get('normal_eof')),
                'qualified':not reasons,'reasons':sorted(set(reasons)),'files':files}
    common.write(directory/'collection.json',completion)
    return completion


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run',type=Path)
    args=parser.parse_args()
    try:
        result=collect(args.run)
        print(json.dumps(result,indent=2))
        sys.exit(0 if result['qualified'] else 1)
    except (OSError,ValueError,KeyError) as error:
        print(error,file=sys.stderr);sys.exit(1)

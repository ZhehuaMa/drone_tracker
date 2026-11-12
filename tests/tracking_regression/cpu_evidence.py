"""Validate actual managed placement without inferring opaque library thread roles."""
from collections import Counter
import csv
from pathlib import Path
import common

REQUIRED_TYPES = {'UpdateStart':7, 'UpdateEnd':8, 'InitializeStart':21, 'InitializeEnd':22,
                  'CatchupStep':23, 'DetectStart':14, 'DetectStage':15, 'DetectEnd':16,
                  'PublishStart':26, 'PublishEnd':27}


def qualify(config, metadata, points, snapshots, clock, events, supervision):
    errors = []
    try:
        common.validate(metadata, common.load(Path(__file__).parent/'schema/cpu_observation.schema.json'))
        if metadata['capacity_per_writer'] != config['cpu_observation']['capacity_per_writer']:
            errors.append('cpu_observation: capacity differs from frozen configuration')
        writers = {row['writer']:row for row in metadata['writers']}
        # Schema 1 predates the appended Dispatcher slot. The diagnostic format,
        # rather than the CPU schema number, identifies the producer layout.
        formats = {event['version'] for event in events}
        if formats not in ({1}, {2}, {3}, {4}):
            return errors + ['cpu_observation: missing/mixed/unsupported diagnostic format']
        slots = set(range(4 if formats == {1} else 5))
        if len(metadata['writers']) != len(slots) or set(writers) != slots:
            return errors + ['cpu_observation: missing/duplicate/unknown writer slots for diagnostic format']
        if any(point['writer'] not in slots for point in points):
            errors.append('cpu_observation: unknown point writer')
        for writer_id, row in writers.items():
            selected = [p for p in points if p['writer'] == writer_id]
            if row['overflow'] or row['count'] > metadata['capacity_per_writer']:
                errors.append(f'cpu_observation.writer[{writer_id}]: lost/over-capacity evidence')
            if len(selected) != row['count']:
                errors.append(f'cpu_observation.writer[{writer_id}]: point count mismatch')
            if selected and (not row['valid'] or row['tid'] <= 0 or row['pid'] <= 0 or row['registered_ns'] <= 0):
                errors.append(f'cpu_observation.writer[{writer_id}]: invalid writer registration')
            if any(p['pid'] != row['pid'] or p['tid'] != row['tid'] or p['cpu'] not in row['allowed_cpus']
                   for p in selected):
                errors.append(f'cpu_observation.writer[{writer_id}]: actual execution/identity mismatch')
        policy = config.get('cpu_affinity', {})
        pinned = policy.get('mode') == 'role-pinned'
        expected = {0:policy.get('tracking_cpu'), 2:policy.get('yolo_cpu'), 3:policy.get('control_cpu')}
        for writer_id in (0, 2, 3):
            row = writers[writer_id]
            if (not row['valid'] or row['overflow'] or not row['count'] or row['tid'] <= 0 or row['pid'] <= 0
                    or row['registered_ns'] <= 0 or row['registered_ns'] >= clock['m0_ns']):
                errors.append(f'cpu_observation.writer[{writer_id}]: missing/lost/pre-clock registration invalid')
            if pinned and row['allowed_cpus'] != [expected[writer_id]]:
                errors.append(f'cpu_observation.writer[{writer_id}]: requested/readback mask mismatch')
            selected = [p for p in points if p['writer'] == writer_id]
            if pinned and any(p['cpu'] != expected[writer_id] for p in selected):
                errors.append(f'cpu_observation.writer[{writer_id}]: actual execution/identity mismatch')
        if not any(e['type']=='UpdateStart' for e in events) or not any(e['type']=='DetectStart' for e in events):
            errors.append('cpu_observation: actual update/detection evidence missing')
        for name, point_id in REQUIRED_TYPES.items():
            required = Counter((e['round_id'],e['update_id'],e['request_id'],e.get('frame_time_us',0)) for e in events if e['type'] == name)
            actual = Counter((p['round'],p['update_id'],p['request_id'],p.get('frame_time_us',0)) for p in points if p['point'] == point_id)
            if required != actual:
                errors.append('cpu_observation: exact event-point identity/count mismatch '+name)
        # Every actual stage has a preprocessing boundary; completed inference/postprocessing have symmetric boundaries.
        stages = [p for p in points if p['point'] == 15]
        stage_ids = Counter((p['round'],p['update_id'],p['request_id'],p['detail']) for p in stages)
        for start, end in ((100,101),(102,103),(104,105)):
            left=Counter((p['round'],p['update_id'],p['request_id'],p['detail']) for p in points if p['point']==start)
            right=Counter((p['round'],p['update_id'],p['request_id'],p['detail']) for p in points if p['point']==end)
            if left != right or (start == 100 and left != stage_ids) or (stages and not left):
                errors.append(f'cpu_observation: YOLO boundary missing {start}/{end}')
        for phase in ('model-ready','playback-ended'):
            if not any(row['phase']==phase and (phase != 'model-ready' or 0 < row.get('mono_ns',0) < clock['m0_ns']) for row in snapshots):
                errors.append('cpu_observation: external boundary snapshot missing '+phase)
        if pinned:
            if supervision.get('allowed_cpus') != sorted(policy['supervision_cpus']) or supervision.get('mono_ns',0)>=clock['m0_ns']:
                errors.append('cpu_observation: supervision placement missing/pre-clock mismatch')
            logger = [thread for snap in snapshots for thread in snap['threads'] if thread['process_role']=='logger' and not thread.get('exited_during_read')]
            if not logger or any(t['allowed_cpus'] != [policy['logger_cpu']] for t in logger):
                errors.append('cpu_observation: logger mask mismatch/missing')
        for snap in snapshots:
            for thread in snap['threads']:
                if thread.get('exited_during_read'):
                    continue
                # A thread may legally retain its initial range during preparation.
                # Use sample start, since a /proc scan can straddle role registration.
                observed = thread.get('observed_mono_ns', snap.get('mono_ns', 0))
                if (isinstance(observed, bool) or not isinstance(observed, int)
                        or observed <= 0 or observed > snap.get('mono_ns', 0)):
                    errors.append('cpu_observation: invalid thread snapshot timestamp')
                    continue
                for writer_id in (0,2,3):
                    writer = writers[writer_id]
                    if (thread['pid'] == writer['pid'] and thread['tid'] == writer['tid']
                            and observed >= writer['registered_ns']
                            and thread['allowed_cpus'] != writer['allowed_cpus']):
                        errors.append('cpu_observation: managed mask changed during playback')
        # Exit races stay as evidence. They cannot stand in for required managed observations.
    except (KeyError, TypeError, ValueError) as error:
        errors.append('cpu_observation: malformed evidence '+str(error))
    return errors


def collect(directory, config, clock, events):
    if 'cpu_observation' not in config:
        return []
    try:
        metadata = common.load(directory/'cpu-observation.json')
        with (directory/'cpu-points.csv').open() as stream:
            points = [{key:int(value) for key,value in row.items()} for row in csv.DictReader(stream)]
        snapshots = common.lines(directory/'thread-snapshots.jsonl')
        supervision = common.load(directory/'supervision-scheduling.json')
        errors = qualify(config, metadata, points, snapshots, clock, events, supervision)
        from metrics import statistics
        summary = {'schema_version':1, 'qualified':not errors, 'reasons':errors,
                   'sample_count':len(points), 'cost_ns':statistics([p['cost_ns'] for p in points]),
                   'execution_cpus':{str(writer):dict(Counter(p['cpu'] for p in points if p['writer']==writer)) for writer in (0,2,3)},
                   'managed_threads':[{**writer, 'role':{0:'tracking',2:'yolo',3:'control'}[writer['writer']]}
                                      for writer in metadata['writers'] if writer['writer'] in (0,2,3)],
                   'unknown_threads':[{**thread, 'phase':snap['phase'],
                                       'observed_mono_ns':thread.get('observed_mono_ns',snap['mono_ns'])}
                                      for snap in snapshots for thread in snap['threads']
                                      if thread['attribution']=='unknown_library_or_middleware' and not any(
                                          thread['pid']==writer['pid'] and thread['tid']==writer['tid']
                                          and thread.get('observed_mono_ns',snap['mono_ns'])>=writer['registered_ns']
                                          for writer in metadata['writers'])]}

        common.write(directory/'cpu-placement.json', summary)
        return errors
    except (OSError, ValueError, KeyError, TypeError) as error:
        return ['cpu_observation: necessary evidence missing/malformed '+str(error)]

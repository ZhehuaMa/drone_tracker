"""Read one immutable suite request and reject configuration errors before execution."""
import copy
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re

import yaml
import common


class StrictLoader(yaml.SafeLoader):
    """Keep decimal spellings exact and prohibit ambiguous duplicate mappings."""


def _mapping(loader, node):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node)
        if not isinstance(key, str):
            raise ValueError(f'line {key_node.start_mark.line + 1}: mapping keys must be strings')
        if key in result:
            raise ValueError(f'line {key_node.start_mark.line + 1}.{key}: duplicate key')
        result[key] = loader.construct_object(value_node)
    return result


StrictLoader.add_constructor('tag:yaml.org,2002:map', _mapping)


def _decimal(loader, node):
    text = node.value.lower().replace('_', '')
    special = {'.nan':'NaN', '.inf':'Infinity', '+.inf':'Infinity', '-.inf':'-Infinity'}
    try:
        return Decimal(special.get(text,text))
    except InvalidOperation as error:
        raise ValueError(f'line {node.start_mark.line+1}: unsupported decimal {node.value!r}') from error


StrictLoader.add_constructor('tag:yaml.org,2002:float', _decimal)
# PyYAML's YAML-1.1 resolver treats JSON's `1e-6` as text. Accept the JSON numeric
# subset without converting through binary float or changing quoted strings.
StrictLoader.add_implicit_resolver('tag:yaml.org,2002:float', re.compile(
    r'^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$'), list('+-0123456789.'))


def fields(value, allowed, required, field):
    if not isinstance(value, dict):
        raise ValueError(f'{field}: expected object')
    for key in value:
        if key not in allowed:
            raise ValueError(f'{field}.{key}: unsupported field')
    for key in required:
        if key not in value:
            raise ValueError(f'{field}.{key}: missing required field')


def identifier(value, field):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', value):
        raise ValueError(f'{field}: safe letters, digits, hyphens and underscores required')
    return value


def seconds_us(value, field, positive=False):
    if isinstance(value, bool) or not isinstance(value, (int, float, Decimal)):
        raise ValueError(f'{field}: numeric seconds required')
    try:
        number = Decimal(str(value)) * 1_000_000
    except InvalidOperation as error:
        raise ValueError(f'{field}: invalid decimal seconds') from error
    if not number.is_finite() or number < 0 or (positive and number <= 0):
        raise ValueError(f'{field}: finite {"positive" if positive else "nonnegative"} seconds required')
    if number != number.to_integral_value() or number > 2**63 - 1:
        raise ValueError(f'{field}: must be exactly representable as integer microseconds')
    return int(number)


def interval(value, field, allow_end=False):
    fields(value, {'start_seconds', 'end_seconds'}, {'start_seconds', 'end_seconds'}, field)
    start = seconds_us(value['start_seconds'], field + '.start_seconds')
    last = value['end_seconds']
    end = 'end' if allow_end and last == 'end' else seconds_us(last, field + '.end_seconds')
    if end != 'end' and end <= start:
        raise ValueError(f'{field}.end_seconds: must exceed start_seconds')
    return {'start_us': start, 'end_us': end}


def path_value(value, base, field):
    if not isinstance(value, str) or not value or value.startswith('TODO'):
        raise ValueError(f'{field}: verified file path required (replace TODO placeholder)')
    path = Path(value).expanduser()
    return str((base / path).resolve())


def path_list(value, base, field):
    if not isinstance(value, list) or not value:
        raise ValueError(f'{field}: nonempty path list required')
    result = [path_value(item, base, f'{field}[{index}]') for index, item in enumerate(value)]
    if len(set(result)) != len(result):
        raise ValueError(f'{field}: duplicate path')
    return result


def load_config(path):
    location = Path(path).resolve(strict=True)
    raw = location.read_text(encoding='utf-8')
    try:
        data = yaml.load(raw, Loader=StrictLoader)
    except (yaml.YAMLError, InvalidOperation) as error:
        raise ValueError(f'{location}: invalid safe YAML: {error}') from error
    fields(data, {'schema_version', 'suite_id', 'operation', 'output_root', 'rounds',
                  'deadlines', 'clips', 'prepare', 'run', 'freeze', 'compare'},
           {'schema_version', 'suite_id', 'operation', 'output_root', 'clips'}, '$')
    if type(data['schema_version']) is not int or data['schema_version'] != 1:
        raise ValueError('$.schema_version: expected 1')
    identifier(data['suite_id'], '$.suite_id')
    if data['operation'] not in ('prepare', 'run', 'freeze', 'compare'):
        raise ValueError('$.operation: expected prepare, run, freeze or compare')
    if data['operation'] not in data:
        raise ValueError(f'$.{data["operation"]}: missing operation section')
    visiting = set()
    def safe_values(value, field='$'):
        container = isinstance(value,(dict,list))
        if container and id(value) in visiting:
            raise ValueError(field+': cyclic YAML alias')
        if container: visiting.add(id(value))
        if isinstance(value, dict):
            for key, child in value.items(): safe_values(child, field+'.'+key)
        elif isinstance(value, list):
            for index, child in enumerate(value): safe_values(child, f'{field}[{index}]')
        elif isinstance(value, (float, Decimal)):
            if not Decimal(str(value)).is_finite(): raise ValueError(field+': non-finite number')
        elif value is not None and not isinstance(value, (str,int,bool)):
            raise ValueError(field+': unsupported YAML value')
        if container: visiting.remove(id(value))
    safe_values(data)
    base = location.parent
    parsed = copy.deepcopy(data)
    parsed['output_root'] = path_value(data['output_root'], base, '$.output_root')
    clips = data['clips']
    if not isinstance(clips, list) or not clips:
        raise ValueError('$.clips: nonempty list required')
    seen, source_paths = set(), []
    parsed['clips'] = []
    for index, clip in enumerate(clips):
        at = f'$.clips[{index}]'
        fields(clip, {'case_id', 'source', 'frame_selection_interval', 'analysis_windows', 'fixture'},
               {'case_id', 'source'}, at)
        case = identifier(clip['case_id'], at + '.case_id')
        if case in seen:
            raise ValueError(at + '.case_id: duplicate identity')
        seen.add(case)
        source = clip['source']
        fields(source, {'video', 'frame_logs', 'attitude_bags', 'session'},
               {'video', 'frame_logs', 'attitude_bags', 'session'}, at + '.source')
        resolved = {key: path_list(value, base, at + '.source.' + key) if key in ('frame_logs', 'attitude_bags')
                    else path_value(value, base, at + '.source.' + key) for key, value in source.items()}
        source_paths.extend([resolved['video'], resolved['session'], *resolved['frame_logs'], *resolved['attitude_bags']])
        current = {'case_id': case, 'source': resolved}
        current['fixture'] = path_value(clip['fixture'], base, at + '.fixture') if 'fixture' in clip else str(
            Path(parsed['output_root']) / 'fixtures' / case)
        if 'frame_selection_interval' in clip:
            current['frame_selection_interval'] = interval(clip['frame_selection_interval'], at + '.frame_selection_interval', True)
        windows = clip.get('analysis_windows', {})
        if not isinstance(windows, dict):
            raise ValueError(at + '.analysis_windows: expected object')
        current['analysis_windows'] = {}
        for name, window in windows.items():
            identifier(name, at + '.analysis_windows')
            if name == 'full':
                raise ValueError(at + '.analysis_windows.full: reserved window')
            current['analysis_windows'][name] = interval(window, at + '.analysis_windows.' + name)
        selection = current.get('frame_selection_interval')
        if selection and selection['end_us'] != 'end':
            duration = selection['end_us'] - selection['start_us']
            for name, window in current['analysis_windows'].items():
                if window['end_us'] > duration:
                    raise ValueError(at+'.analysis_windows.'+name+': outside selected duration')
        parsed['clips'].append(current)
    if 'prepare' in data:
        fields(data['prepare'], set(), set(), '$.prepare')
    if data['operation'] in ('prepare', 'run') or 'deadlines' in data:
        budgets = data.get('deadlines')
        fields(budgets, {'preparation_seconds', 'cleanup_seconds'},
               {'preparation_seconds', 'cleanup_seconds'}, '$.deadlines')
        parsed['deadlines'] = {name: seconds_us(value, '$.deadlines.' + name, True) / 1_000_000
                               for name, value in budgets.items()}
    if 'rounds' in data and (type(data['rounds']) is not int or data['rounds'] < 3):
        raise ValueError('$.rounds: at least three predeclared rounds required')
    if 'run' in data:
        run = data['run']
        fields(run, {'cohort_id', 'source_repo', 'binary', 'worker', 'settings'},
               {'cohort_id', 'source_repo', 'binary', 'worker', 'settings'}, '$.run')
        identifier(run['cohort_id'], '$.run.cohort_id')
        parsed['run'] = {**run, **{name: path_value(run[name], base, '$.run.' + name)
                                     for name in ('source_repo', 'binary', 'worker')}}
        settings = copy.deepcopy(run['settings'])
        if not isinstance(settings, dict) or 'schema_version' in settings:
            raise ValueError('$.run.settings: object without schema_version required')
        # Public settings follow the current runtime contract. Historical v2
        # records keep their original, read-only schema unchanged.
        settings_schema = common.load(Path(__file__).parent / 'schema/run_config.v3.schema.json')
        runtime_fields = {'schema_version', 'case_id', 'deadlines', 'analysis_windows'}
        settings_schema['required'] = [name for name in settings_schema['required'] if name not in runtime_fields]
        settings_schema['properties'] = {name: value for name, value in settings_schema['properties'].items()
                                         if name not in runtime_fields}
        common.validate(settings, settings_schema, '$.run.settings')
        from affinity import validate_settings
        validate_settings(settings, '$.run.settings')
        if 'model_paths' in settings:
            from identity import MODEL_KEYS
            fields(settings['model_paths'], set(MODEL_KEYS), set(), '$.run.settings.model_paths')
            settings['model_paths'] = {key: path_value(value, base, '$.run.settings.model_paths.' + key)
                                       for key, value in settings['model_paths'].items()}
        parsed['run']['settings'] = settings
    if data['operation'] == 'run' and 'rounds' not in data:
        raise ValueError('$.rounds: required for run')
    for operation in ('freeze', 'compare'):
        if operation not in data:
            continue
        section = data[operation]
        id_key = 'baseline_id' if operation == 'freeze' else 'comparison_id'
        fields(section, {id_key, 'cases'}, {id_key, 'cases'}, '$.' + operation)
        identifier(section[id_key], '$.' + operation + '.' + id_key)
        if not isinstance(section['cases'], dict):
            raise ValueError('$.' + operation + '.cases: expected case_id mapping')
        parsed[operation] = {id_key: section[id_key], 'cases': {}}
        for case, item in section['cases'].items():
            identifier(case, '$.' + operation + '.cases')
            at = '$.' + operation + '.cases.' + case
            allowed = {'runs'} if operation == 'freeze' else {'baseline', 'candidates', 'thresholds', 'affinity_experiment'}
            fields(item, allowed, {'runs'} if operation == 'freeze' else {'baseline', 'candidates'}, at)
            resolved = {key: path_list(value, base, at + '.' + key) if key in ('runs', 'candidates')
                        else path_value(value, base, at + '.' + key) for key, value in item.items()}
            if operation == 'freeze' and len(resolved['runs']) < 3:
                raise ValueError(at + '.runs: at least three runs required')
            if 'affinity_experiment' in resolved:
                from affinity_experiment import load_manifest
                load_manifest(resolved['affinity_experiment'])
            parsed[operation]['cases'][case] = resolved
    target = operation_directory(parsed)
    protected = [Path(location), *(Path(item) for item in source_paths)]
    if 'run' in parsed:
        protected.extend(Path(parsed['run'][key]) for key in ('binary','worker'))
        protected.extend(Path(item) for item in parsed['run']['settings'].get('model_paths',{}).values())
    for operation in ('freeze', 'compare'):
        if data['operation'] != operation:
            continue
        for item in parsed.get(operation, {}).get('cases', {}).values():
            for value in item.values():
                protected.extend(Path(entry) for entry in (value if isinstance(value, list) else [value]))
    if data['operation'] != 'prepare':
        protected.extend(Path(clip['fixture']) for clip in parsed['clips'])
    for other in protected:
        if target == other or target in other.parents or other in target.parents:
            raise ValueError(f'$.output_root: operation output overlaps input {other}')
    if len({clip['fixture'] for clip in parsed['clips']}) != len(clips):
        raise ValueError('$.clips.fixture: duplicate fixture directory')
    if data['operation'] == 'prepare':
        for clip in parsed['clips']:
            if Path(clip['fixture']) != target / clip['case_id']:
                raise ValueError('$.clips.fixture: prepare uses output_root/fixtures/case_id')
    # Paths, decimal values and all operation sections are now validated. No output exists yet.
    normalized = json.loads(json.dumps(parsed, default=lambda value: str(value) if isinstance(value, Decimal) else value))
    return {'raw': raw, 'parsed': normalized, 'path': str(location),
            'raw_sha256': hashlib.sha256(raw.encode()).hexdigest(),
            'parsed_sha256': hashlib.sha256(json.dumps(normalized, sort_keys=True, allow_nan=False).encode()).hexdigest()}


def operation_directory(config):
    root = Path(config['output_root'])
    operation = config['operation']
    if operation == 'prepare':
        return root / 'fixtures'
    if operation == 'run':
        return root / 'runs' / config['run']['cohort_id']
    if operation == 'freeze':
        return root / 'baselines' / config['freeze']['baseline_id']
    return root / 'comparisons' / config['compare']['comparison_id']


def snapshot(record, directory):
    (directory / 'config-source.yaml').write_text(record['raw'], encoding='utf-8')
    common.write(directory / 'config-resolved.json', record['parsed'])
    common.write(directory / 'config-snapshot.json', {key: record[key] for key in
                 ('path', 'raw_sha256', 'parsed_sha256')})

"""A frozen CPU treatment permits exact leaves, never broad identity exclusions."""
import copy
from pathlib import Path
import re
import common
import baseline

AFFINITY_LEAVES = {'version','mode','allowed_cpus','control_cpu','tracking_cpu','yolo_cpu','logger_cpu','supervision_cpus'}
AUDITED_DRIVER_FILES = {'main.cpp','realtime_source.cpp','realtime_source.h'}


def allowed_path(path):
    for prefix in ('comparison_group.cpu_policy.regression.',
                   'comparison_group.test_driver.configuration.cpu_affinity.'):
        if path.startswith(prefix) and path[len(prefix):] in AFFINITY_LEAVES:
            return True
    if path == 'comparison_group.cpu_policy.private_policy_sha256':
        return True
    prefix = 'comparison_group.test_driver.sources.'
    return path.startswith(prefix) and path[len(prefix):] in AUDITED_DRIVER_FILES


def normalized_group(group):
    result = copy.deepcopy(group)
    sources = result['test_driver']['sources']
    if not isinstance(sources,list) or any(not isinstance(row,(list,tuple)) or len(row)!=2 for row in sources):
        raise ValueError('affinity_experiment: driver sources must be an exact file/hash list')
    mapped = dict(sources)
    if len(mapped)!=len(sources):
        raise ValueError('affinity_experiment: duplicate driver source')
    result['test_driver']['sources'] = mapped
    return result


def load_manifest(path):
    source = Path(path)
    record = common.load(source)
    common.validate(record, common.load(Path(__file__).parent/'schema/affinity_experiment.schema.json'))
    seen = set()
    for index, row in enumerate(record['allowed_differences']):
        field = row['field']
        if field in seen or not allowed_path(field):
            raise ValueError(f'affinity_experiment.allowed_differences[{index}].field: unsupported or duplicate leaf {field}')
        if row['baseline'] == row['candidate']:
            raise ValueError('affinity_experiment: treatment leaf must actually differ')
        seen.add(field)
    if record['reference']['algorithm_commit'] != '4871442dda37d3d64087fda0b7756692c8f7e473':
        raise ValueError('affinity_experiment: fixed reference algorithm commit required')
    for side in ('reference','candidate'):
        for key in ('source_sha256','binary_sha256','source_patch_sha256'):
            if not re.fullmatch('[0-9a-f]{64}', record[side][key]):
                raise ValueError('affinity_experiment: invalid '+side+'.'+key)
    if record['reference']['binary_sha256'] == record['candidate']['binary_sha256']:
        raise ValueError('affinity_experiment: two distinct binaries required')
    if not record['build']['compiler'] or not record['build']['options']:
        raise ValueError('affinity_experiment: compiler/options required')
    if not re.fullmatch('[0-9a-f]{64}',record['observation_patch_sha256']):
        raise ValueError('affinity_experiment: common observation patch hash required')
    return {'path':str(source.resolve()),'sha256':common.sha256(source),'content':record}


def compare_groups(manifest, reference, candidate):
    differences = baseline.field_differences(normalized_group(reference), normalized_group(candidate))
    expected = {row['field']:row for row in manifest['allowed_differences']}
    errors, accepted = [], []
    found = set()
    for row in differences:
        field = row['field']; found.add(field)
        wanted = expected.get(field)
        if not wanted or row.get('baseline') != wanted['baseline'] or row.get('candidate') != wanted['candidate']:
            errors.append({**row,'reason':'undeclared_or_wrong_treatment_value'})
        else:
            accepted.append({**row,'reason':'declared_cpu_treatment','audit':wanted['audit']})
    for field in expected.keys()-found:
        errors.append({'field':field,'reason':'declared_treatment_not_observed'})
    return errors, accepted


def verify_code(manifest, runs, side):
    errors=[]
    expected=manifest[side]
    for index,run in enumerate(runs):
        for key in ('source_sha256','binary_sha256','project_libraries'):
            if run['identity']['code'].get(key) != expected[key]:
                errors.append({'field':f'{side}.runs[{index}].code.{key}','reason':'frozen_treatment_build_mismatch'})
        observation=run['identity']['comparison_group']['observation'].get('cpu_observation')
        if observation != manifest['cpu_observation']:
            errors.append({'field':f'{side}.runs[{index}].observation','reason':'frozen_common_observation_mismatch'})
    return errors

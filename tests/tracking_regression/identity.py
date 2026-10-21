"""Separate immutable content conditions from file locations used to run a cohort."""
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import common

MODEL_KEYS = ('tracker.yolo_engine', 'tracker.nano_template_backbone',
              'tracker.nano_search_backbone', 'tracker.nano_head')
IDENTITY_VERSION = 2


def workload_processes(repo, proc_root=Path('/proc'), controller_pid=None):
    """Separate kernel scheduling observations from concurrent userspace work.

    PF_KTHREAD in the kernel's process flags identifies kernel threads. Their
    changing names are observations, not application workload identities. Only
    this controller's ancestry and known executables/scripts in its private
    test tree are excluded; unrelated Python and shell workloads remain visible.
    """
    processes = {}
    test_root = Path(repo).resolve().parent
    control_names = {'run_suite.py', 'run_batch.py', 'attempt.py', 'run_one.sh', 'restore_guard.py',
                     'rk3576_tracking_regression', 'rk3576_diagnostic_worker'}
    for directory in proc_root.glob('[0-9]*'):
        try:
            raw = (directory / 'stat').read_text()
            tail = raw[raw.rfind(')') + 2:].split()
            arguments = (directory / 'cmdline').read_bytes().split(b'\0')
            processes[int(directory.name)] = {
                'pid': int(directory.name), 'ppid': int(tail[1]),
                'name': raw[raw.find('(') + 1:raw.rfind(')')],
                'kernel_thread': bool(int(tail[6]) & 0x00200000),
                'arguments': [value.decode(errors='replace') for value in arguments if value],
            }
        except (OSError, ValueError, IndexError):
            # A process can exit between enumeration and reading its metadata.
            continue
    ancestors = set()
    pid = os.getpid() if controller_pid is None else controller_pid
    while pid > 1 and pid in processes and pid not in ancestors:
        ancestors.add(pid)
        pid = processes[pid]['ppid']
    for pid, row in processes.items():
        private_control = any(Path(arg).is_absolute() and Path(arg).is_relative_to(test_root)
                              and Path(arg).name in control_names for arg in row['arguments'])
        row['control_process'] = pid in ancestors or private_control
    fixed = sorted({row['name'] for row in processes.values()
                    if not row['kernel_thread'] and not row['control_process']})
    return fixed, sorted(processes.values(), key=lambda row: row['pid'])


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                    ensure_ascii=False, allow_nan=False).encode()).hexdigest()


def supplier_cpu_condition(config, allowed, cpu_root=Path('/sys/devices/system/cpu')):
    """Reject unavailable source placement before preparation or model loading."""
    if 'supplier_cpu' not in config:
        return {'mode': 'inherit'}
    cpu = config['supplier_cpu']
    if type(cpu) is not int or cpu < 0 or cpu not in allowed:
        raise ValueError('supplier_cpu: nonnegative CPU in controller allowed set required')
    directory = cpu_root / ('cpu' + str(cpu))
    if not directory.is_dir() or ((directory / 'online').exists()
                                  and (directory / 'online').read_text().strip() != '1'):
        raise ValueError('supplier_cpu: requested CPU is not online')
    return {'mode': 'single_cpu', 'cpu': cpu}


def semantic_inputs(fixture, config):
    """Keep every algorithm value and input identity, excluding only known locators."""
    manifest = common.load(fixture / 'fixture.json')
    algorithm = common.load(fixture / 'algorithm.yaml')
    if set(algorithm['models']) != set(MODEL_KEYS):
        raise ValueError('algorithm.models: four frozen model identities required')
    mapping = config.get('model_paths', {})
    if not isinstance(mapping, dict) or set(mapping) - set(MODEL_KEYS):
        raise ValueError('model_paths: only frozen model parameter names may be mapped')
    semantic_algorithm = copy.deepcopy(algorithm)
    locations = {}
    for key in MODEL_KEYS:
        item = algorithm['models'][key]
        if algorithm['parameters'].get(key) != item['path']:
            raise ValueError(f'algorithm.parameters.{key}: differs from recorded model locator')
        path = Path(mapping.get(key, item['path']))
        if not path.is_absolute():
            raise ValueError(f'model_paths.{key}: absolute path required')
        common.verify_hash(path, item['sha256'])
        locations[key] = {'recorded_path': item['path'], 'run_path': str(path),
                          'resolved_path': str(path.resolve()), 'sha256': item['sha256']}
        del semantic_algorithm['parameters'][key]
        del semantic_algorithm['models'][key]['path']
    # The semantic algorithm hash replaces its raw file hash. Every other manifest
    # field remains fixed, including encoded clip, per-frame pixels/times and tool identity.
    semantic_fixture = copy.deepcopy(manifest)
    semantic_fixture['files']['algorithm.yaml'] = digest(semantic_algorithm)
    for source in semantic_fixture['sources']:
        source.pop('path')
    semantic_fixture['sources'].sort(key=digest)
    run_configuration = copy.deepcopy(config)
    run_configuration.pop('model_paths', None)
    # The request's spelling and input/output paths are provenance. Only this case
    # and its resolved numeric range participate in comparison conditions.
    semantic_fixture.pop('selection_requested', None)
    semantic_fixture.pop('selection_mode', None)
    semantic_fixture.pop('tool_sha256', None)
    group = {'identity_version': IDENTITY_VERSION,
             'fixture': digest(semantic_fixture), 'attitude': manifest['files']['attitude.jsonl'],
             'models': {key: algorithm['models'][key]['sha256'] for key in MODEL_KEYS},
             'algorithm': digest(semantic_algorithm),
             'observations': {'protocol_version': 2,
                 'publish_prediction': semantic_algorithm['parameters'].get('publish_prediction', False),
                 'rate_filter_tau': semantic_algorithm['parameters'].get('los.rate_filter_tau', 0.12),
                 'image_qos': 'best_effort/depth1', 'bag_topics': 'gimbal/odom/bbox/los/mavros_imu'}}
    provenance = {'fixture_manifest_sha256': common.sha256(fixture / 'fixture.json'),
                  'algorithm_file_sha256': common.sha256(fixture / 'algorithm.yaml'),
                  'fixture_directory': str(fixture.resolve()), 'models': locations,
                  'source_locations': manifest['sources']}
    return group, provenance, run_configuration


def resolved_dependencies(binaries, env, project_library_names=()):
    """Ask the ELF loader for actual direct/transitive resolution under the run environment.

    Content by SONAME (the dynamic loader's library name) belongs to the comparison
    group. Resolved paths and code-owned libraries remain provenance/code identity.
    No tested application is started, and unresolved libraries are fatal.
    """
    content, provenance, project = {}, {}, {}
    for role, binary in binaries.items():
        result = subprocess.run(['ldd', str(binary)], env=dict(env, LC_ALL='C'),
                                text=True, capture_output=True, check=False)
        if result.returncode or 'not found' in result.stdout:
            raise ValueError(f'dependencies.{role}: loader resolution failed: '
                             + (result.stdout + result.stderr).strip())
        paths = {}
        for row in result.stdout.splitlines():
            row = row.strip()
            if not row or row.startswith('linux-vdso.'):
                continue
            match = re.fullmatch(r'(\S+) => (/.+) \(0x[0-9a-fA-F]+\)', row)
            if match:
                name, located = match.groups()
            else:
                match = re.fullmatch(r'(/.+) \(0x[0-9a-fA-F]+\)', row)
                if not match:
                    raise ValueError(f'dependencies.{role}: unrecognized loader record: {row}')
                located = match[1]
                name = Path(located).name
            if name in paths:
                raise ValueError(f'dependencies.{role}.{name}: duplicate loader identity')
            actual = Path(located).resolve(strict=True)
            paths[name] = {'path': located, 'resolved_path': str(actual), 'sha256': common.sha256(actual)}
        if not paths:
            raise ValueError(f'dependencies.{role}: resolved library identities missing')
        provenance[role] = paths
        content[role] = {name: row['sha256'] for name, row in paths.items()
                         if name not in project_library_names}
        project[role] = {name: row for name, row in paths.items() if name in project_library_names}
    return content, provenance, project


def middleware_identity(rmw_loader, env):
    """Resolve ROS's effective dlopen middleware in an isolated, non-node probe."""
    script = r'''
import ctypes,json,pathlib,sys

def mapped():
    paths=set()
    for row in pathlib.Path('/proc/self/maps').read_text().splitlines():
        columns=row.split(None,5)
        if len(columns)==6 and columns[5].startswith('/') and '.so' in columns[5]:
            paths.add(columns[5])
    return paths
before=mapped()
loader=ctypes.CDLL(sys.argv[1])
get=loader.rmw_get_implementation_identifier
get.argtypes=[];get.restype=ctypes.c_char_p
value=get()
if not value:raise ValueError('effective ROS middleware unavailable')
print(json.dumps({'effective':value.decode(), 'libraries':sorted(mapped()-before)}))
'''
    result = subprocess.run([os.sys.executable, '-c', script, str(rmw_loader)], env=env,
                            text=True, capture_output=True, check=False)
    if result.returncode:
        raise ValueError('dependencies.middleware: probe failed: ' + result.stderr.strip())
    observed = json.loads(result.stdout)
    content, locations = {}, {}
    for located in observed['libraries']:
        path = Path(located).resolve(strict=True)
        metadata = subprocess.check_output(['readelf', '-d', str(path)], env=dict(env, LC_ALL='C'), text=True)
        match = re.search(r'\(SONAME\).*\[([^\]]+)\]', metadata)
        if not match:
            raise ValueError('dependencies.middleware: library SONAME missing: ' + str(path))
        name = match[1]
        if name in content and content[name] != common.sha256(path):
            raise ValueError('dependencies.middleware: ambiguous SONAME: ' + name)
        content[name] = common.sha256(path)
        locations[name] = {'path': located, 'resolved_path': str(path), 'sha256': content[name]}
    if 'lib' + observed['effective'] + '.so' not in content:
        raise ValueError('dependencies.middleware: selected implementation library not resolved')
    provenance = {'requested': env.get('RMW_IMPLEMENTATION') or 'default',
                  'effective': observed['effective'], 'libraries': locations}
    return {'effective': observed['effective'], 'libraries': content}, provenance

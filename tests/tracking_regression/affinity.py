"""Private regression policy, pre-clock bounds and external thread observations."""
import os
import functools
from pathlib import Path
import time
import common

ROLE_KEYS = ('control_cpu', 'tracking_cpu', 'yolo_cpu', 'logger_cpu')


def validate_settings(config, field='$'):
    if config.get('cpu_observation', {}).get('capacity_per_writer', 1) > 4194304:
        raise ValueError(field + '.cpu_observation.capacity_per_writer: exceeds preallocated maximum 4194304')
    policy = config.get('cpu_affinity')
    if not policy:
        return
    if policy['mode'] == 'inherit':
        if set(policy) != {'version', 'mode'}:
            raise ValueError(field + '.cpu_affinity: inherit accepts only version and mode')
        return
    for key in ('allowed_cpus', 'supervision_cpus', *ROLE_KEYS):
        if key not in policy:
            raise ValueError(field + '.cpu_affinity.' + key + ': missing required field')
    allowed = policy['allowed_cpus']
    for key in ('allowed_cpus', 'supervision_cpus'):
        if len(set(policy[key])) != len(policy[key]):
            raise ValueError(field + '.cpu_affinity.' + key + ': duplicate CPU')
    if 'supplier_cpu' not in config:
        raise ValueError(field + '.supplier_cpu: required for role-pinned mode')
    for key in ROLE_KEYS:
        if policy[key] not in allowed:
            raise ValueError(field + '.cpu_affinity.' + key + ': outside allowed_cpus')
    if not set(policy['supervision_cpus']).issubset(allowed):
        raise ValueError(field + '.cpu_affinity.supervision_cpus: outside allowed_cpus')
    if config['supplier_cpu'] not in allowed:
        raise ValueError(field + '.supplier_cpu: outside cpu_affinity.allowed_cpus')
    if len(set([config['supplier_cpu'], *(policy[key] for key in ROLE_KEYS)])) != 5:
        raise ValueError(field + '.cpu_affinity: managed roles must use distinct CPUs')
    if set(policy['supervision_cpus']) & {config['supplier_cpu'], policy['tracking_cpu'], policy['yolo_cpu'], policy['logger_cpu']}:
        raise ValueError(field + '.cpu_affinity.supervision_cpus: overlaps managed worker CPU')
    if 'cpu_observation' not in config:
        raise ValueError(field + '.cpu_observation: required for role-pinned mode')


def cpu_list(text):
    result = set()
    for item in text.strip().split(','):
        if item:
            bounds = item.split('-')
            result.update(range(int(bounds[0]), int(bounds[-1]) + 1))
    return sorted(result)


def effective_cpuset(proc=Path('/proc'), root=Path('/sys/fs/cgroup')):
    """Resolve this process's cgroup rather than assuming the root cgroup applies."""
    for row in (proc / 'self/cgroup').read_text().splitlines():
        _, controllers, relative = row.split(':', 2)
        base = root / relative.lstrip('/') if not controllers else root / 'cpuset' / relative.lstrip('/')
        boundary = root if not controllers else root / 'cpuset'
        if controllers and 'cpuset' not in controllers.split(','):
            continue
        # cgroup v2 omits cpuset files when a parent has not enabled the controller for children.
        # The nearest ancestor that exposes an effective set remains the kernel's inherited limit.
        for directory in (base, *base.parents):
            if directory != boundary and boundary not in directory.parents:
                break
            names = ('cpuset.cpus.effective',) if not controllers else ('cpuset.effective_cpus','cpuset.cpus')
            for name in names:
                path = directory / name
                if path.exists() and path.read_text().strip():
                    return {'cgroup':relative, 'path':str(path), 'cpus':cpu_list(path.read_text())}
            if directory == boundary:
                break
    raise ValueError('cpu_affinity: cannot establish effective kernel cpuset')


def preflight(config, environment=None, allowed=None, cpu_root=Path('/sys/devices/system/cpu'), effective=None):
    validate_settings(config)
    policy = config.get('cpu_affinity')
    if policy is None:
        return None
    environment = os.environ if environment is None else environment
    if environment.get('DRONE_TRACKER_CPU_CONFIG'):
        raise ValueError('cpu_affinity: external DRONE_TRACKER_CPU_CONFIG conflicts with explicit regression policy')
    initial = sorted(os.sched_getaffinity(0)) if allowed is None else allowed
    effective = effective_cpuset() if effective is None else effective
    wanted = policy.get('allowed_cpus', initial)
    for cpu in wanted:
        directory = cpu_root / ('cpu' + str(cpu))
        if cpu not in initial or cpu not in effective['cpus']:
            raise ValueError('cpu_affinity.allowed_cpus: outside initial allowed/effective cgroup set')
        if not directory.is_dir() or ((directory / 'online').exists() and (directory / 'online').read_text().strip() != '1'):
            raise ValueError('cpu_affinity.allowed_cpus: requested CPU not online')
    return {'initial_allowed': initial, 'effective_cpuset': effective, 'online_cpus': cpu_list((cpu_root/'online').read_text()),
            'requested': policy, 'checked_mono_ns': time.monotonic_ns()}


def private_policy(config, output, environment):
    proof = preflight(config, environment)
    if proof is None:
        return dict(environment), None
    common.write(output/'affinity-preflight.json', proof)
    p = config['cpu_affinity']
    policy = {'version':1, 'mode':p['mode'], 'allowed_cpus':p.get('allowed_cpus', proof['initial_allowed'])}
    if p['mode'] == 'role-pinned':
        policy['roles'] = {'control':p['control_cpu'], 'tracking':p['tracking_cpu'], 'yolo':p['yolo_cpu'],
                           'display':p['control_cpu'], 'capture':config['supplier_cpu'], 'recording':p['logger_cpu']}
    path = output/'regression-cpu-policy.json'
    common.write(path, policy)
    return dict(environment, DRONE_TRACKER_CPU_CONFIG=str(path)), proof


def restrict_self(cpus):
    os.sched_setaffinity(0, cpus)
    if sorted(os.sched_getaffinity(0)) != sorted(cpus):
        raise ValueError('cpu_affinity: supervision readback mismatch')


def snapshot(processes, phase, proc=Path('/proc')):
    rows = []
    for role, pid in processes.items():
        for directory in sorted((proc / str(pid) / 'task').glob('[0-9]*')):
            row = {'process_role':role, 'pid':pid, 'tid':int(directory.name),
                   'attribution':'unknown_library_or_middleware', 'observed_mono_ns':time.monotonic_ns()}
            try:
                status = dict(line.split(':', 1) for line in (directory/'status').read_text().splitlines() if ':' in line)
                text = (directory/'stat').read_text(); tail = text[text.rfind(')')+2:].split()
                row.update(name=status['Name'].strip(), allowed_cpus=cpu_list(status['Cpus_allowed_list']),
                           last_cpu=int(tail[36]), policy=int(tail[38]), priority=int(tail[15]),
                           rss_pages=int(tail[21]), process_rss_kib=int(status.get('VmRSS','0 kB').split()[0]),
                           process_peak_rss_kib=int(status.get('VmHWM','0 kB').split()[0]))
                if role == 'logger':
                    row['attribution'] = 'logger'
                elif directory.name == str(pid):
                    row['attribution'] = 'control'
                elif row['name'] == 'regression-src':
                    row['attribution'] = 'supplier'
            except (OSError, ValueError, KeyError, IndexError) as error:
                row.update(exited_during_read=True, error=str(error))
            rows.append(row)
    return {'schema_version':1, 'mono_ns':time.monotonic_ns(), 'phase':phase, 'threads':rows,
            'temperatures':{str(p):p.read_text().strip() for p in Path('/sys/class/thermal').glob('thermal_zone*/temp')},
            'frequencies':{str(p):p.read_text().strip() for p in Path('/sys/devices/system/cpu/cpufreq').glob('policy*/scaling_cur_freq')},
            'memory':{line.split(':')[0]:int(line.split()[1]) for line in Path('/proc/meminfo').read_text().splitlines()
                      if line.startswith(('MemTotal:', 'MemAvailable:', 'MemFree:'))}}


def restore_mask_after(function):
    """Keep a caller reusable after successful or failed supervised pinned attempts."""
    @functools.wraps(function)
    def wrapped(request, *args, **kwargs):
        pinned = request.get('configuration', {}).get('cpu_affinity', {}).get('mode') == 'role-pinned'
        if not pinned:
            return function(request, *args, **kwargs)
        initial = os.sched_getaffinity(0)
        try:
            return function(request, *args, **kwargs)
        finally:
            os.sched_setaffinity(0, initial)
    return wrapped

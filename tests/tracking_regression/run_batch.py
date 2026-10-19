#!/usr/bin/env python3
"""Bound complete independent real-model runs without modifying formal services."""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import signal
import shutil
import subprocess
import sys
import time
import common
import identity as run_identity
from collect import collect
from verify_fixture import verify

SYSTEM_LIBRARY_SHA = 'd31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8'
GROUP_FIELDS=('identity_version','fixture','attitude','models','algorithm','board','runtime','driver','system_library',
              'dependencies','cpu_policy','workload','observation','test_driver','metric_schema','metric_calculator')


def command(args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT).strip()


def tree_identity(repo):
    files=[]
    # Exported trees and Git checkouts are both represented by actual build inputs.
    for root in ('src','tests','parameters','launch','dockerfiles'):
        for file in sorted((repo/root).rglob('*')):
            if file.is_file() and '__pycache__' not in file.parts and file.suffix not in ('.pyc','.rknn','.mp4','.mkv'):
                files.append((str(file.relative_to(repo)), common.sha256(file)))
    for name in ('CMakeLists.txt','package.xml'):
        files.append((name, common.sha256(repo/name)))
    if not files:
        raise ValueError('code.source_sha256: source inputs missing')
    encoded=json.dumps(files,separators=(',',':')).encode()
    identity={'source_sha256':hashlib.sha256(encoded).hexdigest(),'source_files':files}
    try:
        identity['commit']=command(['git','-C',str(repo),'rev-parse','HEAD'])
        diff=subprocess.check_output(['git','-C',str(repo),'diff','--binary','HEAD','--'])
        identity['dirty_diff_sha256']=hashlib.sha256(diff).hexdigest()
        identity['dependency_commit']='e555f7a335660b6efd89bd5c4f757522834a6b03'
        identity['merge_committed']=not Path(command(['git','-C',str(repo),'rev-parse','--git-path','MERGE_HEAD'])).exists()
    except subprocess.CalledProcessError:
        identity['commit']=None
        if (repo/'source-export.json').exists():
            exported=common.load(repo/'source-export.json')
            if exported['source_sha256']!=identity['source_sha256']:
                raise ValueError('exported source snapshot differs from actual source tree')
            identity.update({k:v for k,v in exported.items() if k!='source_files'})
    return identity


def production_identity(fixture,config,repo,binary,worker):
    if platform.machine()!='aarch64':
        raise ValueError('board.architecture: aarch64 production board required')
    model=Path('/proc/device-tree/model').read_text().rstrip('\0\n')
    if model!='HZ-RK3576_WuKong Board':
        raise ValueError(f'board.model: expected HZ-RK3576_WuKong Board, got {model}')
    library=Path('/usr/lib/librknnrt.so')
    common.verify_hash(library,SYSTEM_LIBRARY_SHA)
    driver=Path('/sys/kernel/debug/rknpu/version').read_text().strip()
    if '0.9.8' not in driver:
        raise ValueError(f'driver: expected 0.9.8, got {driver}')
    # The frozen library hash pins Runtime 2.3.2 without launching rknn_server.
    algorithm=common.load(fixture/'algorithm.yaml')
    input_group,input_locations,semantic_config=run_identity.semantic_inputs(fixture,config)
    text=(repo/'src/hybrid_tracker/nano_tracker.h').read_text()
    for key,value in algorithm['nano_constants'].items():
        if key=='context_ratio':
            if '(w + h) * 0.5f' not in (repo/'src/hybrid_tracker/nano_tracker.cpp').read_text():
                raise ValueError('algorithm.nano_constants.context_ratio: implementation changed')
            continue
        match=re.search(r'const (?:int|float) '+key+r'\s*=\s*([0-9.]+)f?;',text)
        if not match or float(match[1])!=value:
            raise ValueError(f'algorithm.nano_constants.{key}: implementation differs from frozen source')
    code=tree_identity(repo)
    code['binary_sha256']=common.sha256(binary)
    cpu_path=os.environ.get('DRONE_TRACKER_CPU_CONFIG')
    cpu={'mode':'inherit','allowed':sorted(os.sched_getaffinity(0))}
    if cpu_path:
        cpu={'file_sha256':common.sha256(cpu_path),'text':Path(cpu_path).read_text(),
             'allowed':sorted(os.sched_getaffinity(0))}
    cpu['supplier'] = run_identity.supplier_cpu_condition(config, cpu['allowed'])
    isolated = Path('/sys/devices/system/cpu/isolated')
    cpu['isolated_cpus'] = isolated.read_text().strip() if isolated.exists() else ''
    governors={str(path):path.read_text().strip() for path in sorted(Path('/sys/devices/system/cpu/cpufreq').glob('policy*/scaling_governor'))}
    cpu['governors']=governors
    services={name:subprocess.run(['systemctl','is-active',name],capture_output=True,text=True).stdout.strip()
              for name in ('drone-tracker.service','drone-tracker-diagnostics.service')}
    # Record fixed workload identity separately from variable temperature/frequency measurements.
    fixed_processes, process_observations = run_identity.workload_processes(repo)
    workload={'formal_services':services,'declared':config.get('workload','isolated regression'),
              'processes':process_observations}
    workload_group={'formal_services':services,'declared':workload['declared'],
                    'process_identity_version':2,'fixed_processes':fixed_processes}
    if config.get('expected_workload_processes') is not None and workload_group['fixed_processes']!=config['expected_workload_processes']:
        raise ValueError('workload.fixed_processes: concurrent workload differs from frozen configuration')
    if services['drone-tracker.service']=='active':
        raise ValueError('workload.formal_services: formal tracker active; arrange authorized maintenance externally')
    # Resolve using the same environment inherited by both isolated children. Paths
    # are provenance only; all external direct/transitive library contents are compared.
    project_stems=('drone_tracker','drone_tracker_ros','hybrid_tracker','platform_rk3576',
                   'model_common','preprocess_geometry','cpu_affinity','drone_osd_core','drone_diagnostics')
    project_names={file.name for file in binary.parent.glob('*.so*') if file.is_file()
                   and any(file.name.startswith('lib'+stem+'.so') for stem in project_stems)}
    libraries,library_locations,project_libraries=run_identity.resolved_dependencies(
        {'node':binary,'worker':worker},os.environ,project_names)
    for name in ('librclcpp.so','libopencv_core.so','libavcodec.so',
                 'libavformat.so','libavutil.so','librknnrt.so'):
        if not any(actual==name or actual.startswith(name+'.') for actual in libraries['node']):
            raise ValueError('dependencies.node.'+name+': required actual dependency missing')
    common.verify_hash(Path(library_locations['node']['librknnrt.so']['resolved_path']),SYSTEM_LIBRARY_SHA)
    if Path(library_locations['node']['librknnrt.so']['resolved_path'])!=library.resolve():
        raise ValueError('dependencies.node.librknnrt.so: system Runtime must be loaded')
    code['project_libraries']={role:{name:row['sha256'] for name,row in files.items()}
                               for role,files in project_libraries.items()}
    middleware,middleware_locations=run_identity.middleware_identity(
        library_locations['node']['librmw_implementation.so']['resolved_path'],dict(os.environ))
    dependencies={'identity_version':run_identity.IDENTITY_VERSION,'ros':os.environ.get('ROS_DISTRO',''),
                  'ffmpeg':command(['ffmpeg','-version']).splitlines()[0],
                  'ffmpeg_binary_sha256':common.sha256(shutil.which('ffmpeg')),
                  'ffprobe_binary_sha256':common.sha256(shutil.which('ffprobe')),
                  'libraries':libraries,'middleware':middleware}
    if dependencies['ros']!='humble':
        raise ValueError('dependencies.ros: source /opt/ros/humble/setup.bash before running')
    drivers=[(str(file.relative_to(repo/'tests/tracking_regression')),common.sha256(file))
             for file in sorted((repo/'tests/tracking_regression').glob('*')) if file.is_file() and file.suffix in ('.py','.cpp','.h')]
    group={**input_group,
           'board':{'model':model,'machine_id':Path('/etc/machine-id').read_text().strip(),
                    'serial':Path('/proc/device-tree/serial-number').read_text().rstrip('\0\n'),
                    'kernel':platform.release()},'runtime':'2.3.2','driver':'0.9.8',
           'system_library':{'path':'/usr/lib/librknnrt.so','sha256':SYSTEM_LIBRARY_SHA},
           'dependencies':dependencies,'cpu_policy':cpu,'workload':workload_group,
           'observation':{'mode':'basic','inference_observed_contract':1,'osd':False,'images':False,'recording':False,'submission_measurement':True,
                          'worker_sha256':common.sha256(worker)},
           'test_driver':{'sources':drivers,'configuration':semantic_config,
               'decoder':{'threads':8,'codec':'ffv1','pixels':'gray','input_mode':'predecode_all',
                          'pixel_verification':'SHA-256 actual retained Gray8 frames before model loading',
                          'implementation':'native libavformat/libavcodec FFV1 GRAY8 stride copy',
                          'thread_type':'slice','packet_drain':'send/receive EAGAIN and EOF drain'}},'metric_schema':1,
           'metric_calculator':common.sha256(repo/'tests/tracking_regression/metrics.py')}
    manifest = common.load(fixture / 'fixture.json')
    modern = manifest['schema_version'] == 2
    group['metric_schema'] = manifest['schema_version']
    return {'schema_version':2 if modern else 1,'case_id':manifest['case_id'],'mode':'realtime','comparison_group':group,
            'code':code,'duration_us':manifest['duration_us'],
            **({'input': {key: manifest[key] for key in ('frame_count','width','height','start_us','end_us','analysis_windows')}} if modern else {}),'workload_observation':workload,
            'dependency_locations':library_locations,'middleware_locations':middleware_locations,
            'input_locations':input_locations,
            'loadavg':Path('/proc/loadavg').read_text().strip(),
            'temperatures':{str(path):path.read_text().strip() for path in Path('/sys/class/thermal').glob('thermal_zone*/temp')}}


def enable_subreaper():
    if sys.platform!='linux':
        raise ValueError('deadline: Linux child subreaper required')
    if ctypes.CDLL(None,use_errno=True).prctl(36,1,0,0,0)!=0:
        raise OSError(ctypes.get_errno(),'cannot enable child subreaper')


def descendants():
    """Track parent ancestry, including children adopted after a test leader exits."""
    parents={}
    for path in Path('/proc').glob('[0-9]*/stat'):
        try:
            text=path.read_text();tail=text[text.rfind(')')+2:].split()
            parents[int(path.parent.name)]=int(tail[1])
        except (OSError,ValueError,IndexError):
            continue
    found=set();front=[os.getpid()]
    while front:
        parent=front.pop()
        for pid,ppid in parents.items():
            if ppid==parent and pid not in found:
                found.add(pid);front.append(pid)
    return found


def stop_children(processes,stop_time,deadline):
    for process in processes:
        if process.poll() is None:
            try: os.killpg(process.pid,signal.SIGINT)
            except ProcessLookupError: pass
    while time.monotonic()<min(stop_time+2,deadline-1) and descendants():
        time.sleep(.02)
        for process in processes: process.poll()
    for pid in descendants():
        try: os.kill(pid,signal.SIGKILL)
        except ProcessLookupError: pass
    while time.monotonic()<deadline-0.1:
        try:
            pid,_=os.waitpid(-1,os.WNOHANG)
            if pid==0:
                if not descendants(): break
                time.sleep(.01)
        except ChildProcessError:
            break
    return not descendants()


def worker_round(request):
    """Run inside the externally bounded child, including validation and collection."""
    output=Path(request['output']);fixture=Path(request['fixture']);repo=Path(request['repo'])
    config=request['configuration'] if 'configuration' in request else common.load(Path(request['config']))
    common.validate_record(config,'run_config')
    verified=verify(fixture,verify_pixels=False)
    common.write(output/'preflight-fixture.json',{'schema_version':1,'files_verified':True,'pixels_verified':False,'pixel_verification':'native cached frames before model loading',
                 'fixture_sha256':common.sha256(fixture/'fixture.json'),'files':verified['files']})
    identity=production_identity(fixture,config,repo,Path(request['binary']),Path(request['worker']))
    common.write(output/'identity-preflight.json',identity)
    common.write(output/'run-config.json',config)
    socket=str(output/'diagnostics.sock')
    if len(socket.encode())>=108:
        raise ValueError('diagnostic socket: absolute output path exceeds Unix socket limit')
    env=dict(os.environ,ROS_DOMAIN_ID=str(config['ros_domain_id']),ROS_LOCALHOST_ONLY='1',
             DRONE_TRACKER_ACCEPTANCE_DIRECTORY=str(output))
    worker_log=(output/'worker.log').open('xb')
    node_log=(output/'node.log').open('xb')
    common.write(output/'node-request.json', {'schema_version':1, 'fixture':str(fixture),
        'config':str(output/'run-config.json'), 'output':str(output), 'diagnostic_socket':socket})
    worker=subprocess.Popen([request['worker'],'--socket',socket,'--directory',str(output/'raw'),
                             '--minimum-free-bytes','0'],stdout=worker_log,stderr=subprocess.STDOUT,env=env,start_new_session=True)
    try:
        until=time.monotonic()+2
        while not Path(socket).is_socket() and time.monotonic()<until and worker.poll() is None:
            time.sleep(.01)
        if not Path(socket).is_socket():
            raise ValueError('diagnostic worker socket not ready')
        node=subprocess.Popen([request['binary'],str(output/'node-request.json')],
                              stdout=node_log,stderr=subprocess.STDOUT,env=env,start_new_session=True)
        node_code=node.wait()
        worker.send_signal(signal.SIGINT)
        worker_code=worker.wait(timeout=3)
        common.write(output/'process-exit.json',{'node':node_code,'worker':worker_code})
        errors=[]
        if node_code:errors.append(f'node exit {node_code}')
        if worker_code:errors.append(f'worker exit {worker_code}')
        result=collect(output,errors)
        return 0 if result['qualified'] else 1
    finally:
        if worker.poll() is None:
            worker.kill();worker.wait()
        worker_log.close();node_log.close()


def deadline_plan(request):
    budgets = request.get('deadlines', {'preparation_seconds':90, 'cleanup_seconds':10})
    duration = request.get('duration_us', 20_000_000)
    total = budgets['preparation_seconds'] + math.ceil(duration / 1_000_000) + budgets['cleanup_seconds']
    return {**budgets, 'duration_us':duration, 'total_seconds':total,
            'stop_after_seconds':total - budgets['cleanup_seconds']}


def bounded_attempt(request):
    enable_subreaper()
    output=Path(request['output'])
    if not output.exists():output.mkdir(parents=True,exist_ok=False)
    common.write(output/'attempt.json',request)
    plan = deadline_plan(request)
    common.write(output/'deadline-plan.json',plan)
    total = plan['total_seconds']
    begin=time.monotonic();deadline=begin+total-min(.5,plan['cleanup_seconds']/4)
    log=(output/'controller.log').open('xb')
    process=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--internal',str(output/'attempt.json')],
                             stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    timeout=False
    preparation_timeout = False
    while process.poll() is None and time.monotonic()<begin+plan['stop_after_seconds']:
        if not (output/'input-clock.json').exists() and time.monotonic() >= begin+plan['preparation_seconds']:
            preparation_timeout = True
            break
        time.sleep(.01)
    if process.poll() is None: timeout=True
    recovered=stop_children([process],time.monotonic(),deadline)
    log.close()
    elapsed=time.monotonic()-begin
    deadline_record={'limit_seconds':total,'stop_after_seconds':plan['stop_after_seconds'],
                'preparation_seconds':plan['preparation_seconds'],'cleanup_seconds':plan['cleanup_seconds'],
                'preparation_timed_out':preparation_timeout,'elapsed_seconds':elapsed,
                'timed_out':timeout,'resources_recovered':recovered,'exit_code':process.returncode}
    common.write(output/'deadline.json',deadline_record)
    result=common.load(output/'collection.json') if (output/'collection.json').exists() else {
        'schema_version':2 if request.get('configuration',{}).get('schema_version') == 3 else 1,'qualified':False,'normal_end':False,
        'reasons':['preflight_or_collection_failed']}
    if preparation_timeout: result['reasons'].append('preparation_deadline')
    if timeout: result['reasons'].append('external_deadline')
    if not recovered: result['reasons'].append('test_descendants_not_recovered')
    if elapsed>total: result['reasons'].append('external_deadline_exceeded')
    result['qualified']=result['qualified'] and not result['reasons']
    # Raw evidence hashes were computed inside the externally bounded collector.
    # Finalization hashes only the two small controller records; it never rereads large logs.
    result['files']=result.get('files',{})
    for name in ('collection.json','deadline.json'):
        if (output/name).exists(): result['files'][name]=common.sha256(output/name)
    if time.monotonic()>=deadline:
        result['qualified']=False;result['reasons'].append('deadline_during_finalization')
    common.write(output/'completion.json',result)
    elapsed=time.monotonic()-begin
    if elapsed>total:
        raise TimeoutError('external controller finalization exceeded configured total deadline')
    return common.load(output/'completion.json')['qualified'] and recovered and not timeout and elapsed<=total


def supervise(request, total_seconds=None):
    """An outer process bounds the controller itself, including evidence finalization."""
    enable_subreaper()
    output=Path(request['output'])
    plan = deadline_plan(request)
    total_seconds = plan['total_seconds'] if total_seconds is None else total_seconds
    start=time.monotonic()
    output.mkdir(parents=True,exist_ok=False)
    common.write(output/'supervised-request.json',request)
    common.write(output/'deadline-plan-supervisor.json',plan)
    log=(output/'supervisor.log').open('xb')
    controller=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--controller',
        str(output/'supervised-request.json')],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    preparation_timeout = False
    while controller.poll() is None and time.monotonic()<start+total_seconds-min(.5,total_seconds/10):
        if not (output/'input-clock.json').exists() and time.monotonic() >= start+plan['preparation_seconds']:
            preparation_timeout = True
            break
        time.sleep(.01)
    timed_out=controller.poll() is None
    cleanup_deadline=start+total_seconds-0.1
    if timed_out or descendants():
        for pid in descendants():
            try:os.kill(pid,signal.SIGKILL)
            except ProcessLookupError:pass
    while time.monotonic()<cleanup_deadline:
        controller.poll()
        try:
            pid,_=os.waitpid(-1,os.WNOHANG)
            if pid==0:
                if not descendants():break
                time.sleep(.005)
        except ChildProcessError:break
    recovered=not descendants()
    log.close()
    completion_path=output/'completion.json'
    result=common.load(completion_path) if completion_path.exists() else {
        'schema_version':2 if request.get('configuration',{}).get('schema_version') == 3 else 1,'qualified':False,'normal_end':False,
        'reasons':['external_controller_deadline_or_failure'],'files':{}}
    if completion_path.exists():
        common.write(output/'controller-completion.json',result)
    deadline_path=output/'deadline.json'
    if deadline_path.exists():
        common.write(output/'controller-deadline.json',common.load(deadline_path))
        deadline_path.unlink()
    elapsed=time.monotonic()-start
    deadline_record={'limit_seconds':total_seconds,'stop_after_seconds':min(plan['stop_after_seconds'],total_seconds),
        'preparation_seconds':plan['preparation_seconds'],'cleanup_seconds':plan['cleanup_seconds'],
        'elapsed_seconds':elapsed,'timed_out':timed_out,'resources_recovered':recovered,
        'preparation_timed_out':preparation_timeout,
        'controller_exit_code':controller.returncode,'scope':'includes controller finalization'}
    common.write(deadline_path,deadline_record)
    if preparation_timeout:result['reasons'].append('preparation_deadline')
    if timed_out:result['reasons'].append('external_controller_deadline')
    if controller.returncode!=0:result['reasons'].append('controller_abnormal_exit')
    if not recovered:result['reasons'].append('test_descendants_not_recovered')
    if elapsed>total_seconds:result['reasons'].append('external_deadline_exceeded')
    result['qualified']=result['qualified'] and not result['reasons']
    for name in ('attempt.json','supervised-request.json','controller.log','supervisor.log',
                 'controller-completion.json','controller-deadline.json','deadline.json','deadline-plan-supervisor.json'):
        if (output/name).exists():result['files'][name]=common.sha256(output/name)
    temporary=output/'completion-supervised.json'
    common.write(temporary,result)
    os.replace(temporary,completion_path)
    return result['qualified'] and time.monotonic()-start<=total_seconds


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--internal',type=Path)
    parser.add_argument('--controller',type=Path)
    parser.add_argument('--fixture',type=Path);parser.add_argument('--config',type=Path)
    parser.add_argument('--binary',type=Path);parser.add_argument('--worker',type=Path)
    parser.add_argument('--repo',type=Path);parser.add_argument('--output',type=Path)
    parser.add_argument('--rounds',type=int,default=3)
    args=parser.parse_args()
    if args.internal:
        return worker_round(common.load(args.internal))
    if args.controller:
        return 0 if bounded_attempt(common.load(args.controller)) else 1
    raise ValueError('Use run_suite.py CONFIG.yaml; run_batch.py is an internal controller.')

if __name__=='__main__':
    try: sys.exit(main())
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        print(error,file=sys.stderr);sys.exit(1)

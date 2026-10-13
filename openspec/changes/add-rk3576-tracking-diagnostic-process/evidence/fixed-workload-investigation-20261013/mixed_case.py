#!/usr/bin/env python3
"""固定工作量归因入口；由外层systemd控制组约束所有后代。"""
import os,sys,subprocess,time,json,signal,threading
from pathlib import Path
ROOT=Path(os.environ['DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT']); E=ROOT/'evidence'/sys.argv[1];E.mkdir()
BIN=ROOT/'drone_tracker/lib/drone_tracker'; VIDEO=Path('/home/hzhy/drone-performance-investigation-20261013/fixed-balloon.avi')
PAIRS={1:[('mixed',0,0,80)],2:[('mixed',0,0,80)]}
SEEDS={1:20261013,2:20261014}
CORES={1:['4-7'],2:['4-7']}
number=int(sys.argv[1][4:]); processes=[]; records=[]; resources=[];done=threading.Event()
def thermal(pid,worker):
 while not done.is_set():
  item={'mono_ns':time.monotonic_ns(),'business':pid,'worker':worker}
  for name,root in [('cpu','/sys/devices/system/cpu/cpufreq'),('thermal','/sys/class/thermal'),('devfreq','/sys/class/devfreq')]:
   item[name]={}
   for p in Path(root).glob('policy*/scaling_cur_freq' if name=='cpu' else ('thermal_zone*/temp' if name=='thermal' else '*/cur_freq')):
    try:item[name][str(p)]=p.read_text().strip()
    except OSError:pass
  for label,p in [('business',pid),('worker',worker)]:
   if p:
    try:item[label+'_schedstat']=(Path('/proc')/str(p)/'schedstat').read_text();item[label+'_stat']=(Path('/proc')/str(p)/'stat').read_text()
    except OSError:pass
  resources.append(item); done.wait(1)
def launch(command,path):
 f=path.open('w');p=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT);processes.append((p,f));return p
signal.signal(signal.SIGTERM,lambda *_: (_ for _ in ()).throw(RuntimeError('external cutoff')))
os.environ['DRONE_MIXED_SEED']=str(SEEDS[number])
started=time.monotonic_ns()
try:
 for idx,(mode,observer,audit,cycles) in enumerate(PAIRS[number]):
  out=E/(str(idx)+'-'+mode);out.mkdir();socket=str(E/'worker.sock');worker=None
  if mode in ('basic','mixed'):
   worker=launch(['taskset','-c',str(CORES[number][idx]),str(BIN/'rk3576_diagnostic_worker'),'--socket',socket,'--directory',str(out/'diagnostics'),'--minimum-free-bytes','0'],out/'worker.log')
   deadline=time.monotonic()+3
   while not Path(socket).exists():
    if worker.poll() is not None or time.monotonic()>=deadline:raise RuntimeError('worker not ready')
    time.sleep(.01)
  if worker:
   def cpuset(pid):
    group=(Path('/proc')/str(pid)/'cgroup').read_text().strip().split(':')[-1]; base=Path('/sys/fs/cgroup')/group.lstrip('/')
    while base!=Path('/sys/fs'):
     if (base/'cpuset.cpus.effective').exists():return {'path':str(base/'cpuset.cpus.effective'),'value':(base/'cpuset.cpus.effective').read_text()}
     base=base.parent
   (out/'worker-affinity.json').write_text(json.dumps({'pid':worker.pid,'requested_cpu':CORES[number][idx],'cgroup':(Path('/proc')/str(worker.pid)/'cgroup').read_text(),'effective_cpuset':cpuset(worker.pid),'tasks':{t.name:{'status':(t/'status').read_text(),'affinity':sorted(os.sched_getaffinity(int(t.name))),'scheduler':os.sched_getscheduler(int(t.name))} for t in (Path('/proc')/str(worker.pid)/'task').iterdir()}},indent=2))
  p=launch([str(BIN/'fixed_workload_probe'),mode,socket,str(VIDEO),str(out),str(cycles),str(observer),str(audit)],out/'business.log')
  done.clear();t=threading.Thread(target=thermal,args=(p.pid,worker.pid if worker else None));t.start()
  begin=time.monotonic_ns(); rc=p.wait(timeout=125); done.set();t.join(2)
  records.append({'worker_cpu':CORES[number][idx],'mode':mode,'observer':observer,'audit':audit,'cycles':cycles,'returncode':rc,'elapsed_ns':time.monotonic_ns()-begin,'directory':str(out)})
  if worker:
   worker.send_signal(signal.SIGTERM);wrc=worker.wait(timeout=3); records[-1]['worker_returncode']=wrc
   if wrc:raise RuntimeError('worker nonzero')
   for run in (out/'diagnostics').iterdir():
    if run.is_dir():
     with (out/'inspect.log').open('w') as f:
      result=subprocess.run([str(BIN/'rk3576_diagnostic_inspect'),str(run),'--output',str(out/('export-'+run.name))],stdout=f,stderr=subprocess.STDOUT,timeout=10)
     records[-1]['inspect_returncode']=result.returncode
  if rc:raise RuntimeError('business nonzero')
finally:
 done.set()
 for p,f in processes:
  if p.poll() is None:
   p.terminate()
   try:p.wait(timeout=2)
   except subprocess.TimeoutExpired:p.kill();p.wait(timeout=2)
  f.close()
 (E/'case.json').write_text(json.dumps({'start_ns':started,'finish_ns':time.monotonic_ns(),'records':records},indent=2))
 (E/'resources.json').write_text(json.dumps(resources,indent=2))

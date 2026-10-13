"""生产板固定存储对照；原件与 tmpfs 副本在 finally 有限保存。"""
import os,sys,subprocess,time,json,signal,threading,hashlib,shutil
from pathlib import Path
ROOT=Path(os.environ['DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT']); E=ROOT/'evidence'/sys.argv[1];E.mkdir()
BIN=ROOT/'drone_tracker/lib/drone_tracker'; VIDEO=Path('/home/hzhy/drone-performance-investigation-20261013/fixed-balloon.avi')
number=int(sys.argv[1][4:]); cycles=4 if number==1 else 64
SEEDS={1:20261012,2:20261013,3:20261014}; audit=number==1
TMP=Path('/tmp/drone-fixed-storage-20261013'); CONTROL=TMP/sys.argv[1];CONTROL.mkdir()
processes=[]; resources=[]; done=threading.Event(); record={};started=time.monotonic_ns()
def launch(command,path):
 f=path.open('w');p=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT);processes.append((p,f));return p
def thermal(pid,worker):
 while not done.is_set():
  item={'mono_ns':time.monotonic_ns(),'business':pid,'worker':worker}
  for name,root in [('cpu','/sys/devices/system/cpu/cpufreq'),('thermal','/sys/class/thermal'),('devfreq','/sys/class/devfreq')]:
   item[name]={}
   for p in Path(root).glob('policy*/scaling_cur_freq' if name=='cpu' else ('thermal_zone*/temp' if name=='thermal' else '*/cur_freq')):
    try:item[name][str(p)]=p.read_text().strip()
    except OSError:pass
  for label,pidv in [('business',pid),('worker',worker)]:
   try:
    task=Path('/proc')/str(pidv);item[label+'_schedstat']=(task/'schedstat').read_text();item[label+'_stat']=(task/'stat').read_text();item[label+'_tasks']={t.name:{'affinity':sorted(os.sched_getaffinity(int(t.name))),'scheduler':os.sched_getscheduler(int(t.name)),'status':(t/'status').read_text()} for t in (task/'task').iterdir()};item[label+'_fd_count']=len(list((task/'fd').iterdir()))
   except OSError:pass
  item['meminfo']=Path('/proc/meminfo').read_text();resources.append(item);done.wait(1)
signal.signal(signal.SIGTERM,lambda *_: (_ for _ in ()).throw(RuntimeError('external cutoff')))
out=E/'0-mixed';out.mkdir(); socket=str(CONTROL/'worker.sock')
os.environ['DRONE_MIXED_SEED']=str(SEEDS[number]);os.environ['DRONE_STORAGE_CONTROL']=str(CONTROL)
try:
 worker=launch(['taskset','-c','4-7',str(BIN/'rk3576_diagnostic_worker'),'--socket',socket,'--directory',str(out/'emmc'),'--test-switch-control',str(CONTROL),'--test-tmpfs-directory',str(CONTROL/'tmpfs'),'--sync-ms','100','--minimum-free-bytes','8388608','--total-bytes','67108864'],out/'worker.log')
 deadline=time.monotonic()+3
 while not Path(socket).exists():
  if worker.poll() is not None or time.monotonic()>=deadline:raise RuntimeError('worker not ready')
  time.sleep(.01)
 p=launch([str(BIN/'fixed_workload_probe'),'mixed',socket,str(VIDEO),str(out),str(cycles),'0',str(int(audit))],out/'business.log')
 t=threading.Thread(target=thermal,args=(p.pid,worker.pid));t.start()
 begin=time.monotonic_ns(); rc=p.wait(timeout=125);done.set();t.join(2)
 record={'seed':SEEDS[number],'cycles':cycles,'audit':audit,'returncode':rc,'business_pid':p.pid,'worker_pid':worker.pid,'probe_elapsed_ns':time.monotonic_ns()-begin}
 worker.send_signal(signal.SIGTERM);wrc=worker.wait(timeout=3);record['worker_returncode']=wrc
 if rc or wrc:raise RuntimeError('probe/worker nonzero')
finally:
 done.set()
 for p,f in processes:
  if p.poll() is None:
   p.terminate()
   try:p.wait(timeout=2)
   except subprocess.TimeoutExpired:p.kill();p.wait(timeout=2)
  f.close()
 if 't' in globals():t.join(2)
 originals=[]
 for path in CONTROL.rglob('*'):
  if path.is_file() and not path.name.endswith(('.sock','.sock.lock')):
   destination=out/'tmpfs-originals'/path.relative_to(CONTROL);destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,destination)
   digest=hashlib.sha256(path.read_bytes()).hexdigest();assert digest==hashlib.sha256(destination.read_bytes()).hexdigest();originals.append({'path':str(path.relative_to(CONTROL)),'bytes':path.stat().st_size,'sha256':digest})
 (out/'tmpfs-copy-verification.json').write_text(json.dumps(originals,indent=2))
 (E/'case.json').write_text(json.dumps({'start_ns':started,'finish_ns':time.monotonic_ns(),'record':record},indent=2));(E/'resources.json').write_text(json.dumps(resources,indent=2))

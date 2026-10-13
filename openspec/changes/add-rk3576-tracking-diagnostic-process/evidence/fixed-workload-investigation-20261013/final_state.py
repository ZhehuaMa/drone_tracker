#!/usr/bin/env python3
"""恢复后独立核对保护指纹、HDMI客户端及本轮所有进程。"""
import json,os,hashlib,subprocess,time
from pathlib import Path
root=Path(os.environ['DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT']);e=root/'evidence'
paths=[Path('/usr/lib/librknnrt.so')]+sorted(p for folder in ['config','models'] for p in (Path('/opt/drone_tracker')/folder).rglob('*') if p.is_file())
identity={'current':os.path.realpath('/opt/drone_tracker/current'),'files':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
state=subprocess.check_output(['systemctl','show','drone-tracker.service','--property=ActiveState,SubState,MainPID,UnitFileState,ControlGroup'],text=True)
fields=dict(x.split('=',1) for x in state.splitlines());formal=int(fields.get('MainPID',0));drm=Path('/sys/kernel/debug/dri/0/clients').read_text();clients=[]
for row in drm.splitlines()[1:]:
 parts=row.split()
 if len(parts)<2:continue
 try:
  pid=int(parts[1]);status=(Path('/proc')/str(pid)/'status').read_text();tgid=int(next(x.split(':',1)[1] for x in status.splitlines() if x.startswith('Tgid:')));clients.append({'listed_tid':pid,'tgid':tgid,'formal':tgid==formal})
 except (OSError,ValueError,StopIteration):clients.append({'row':row,'unknown':True})
remaining=[]
for p in Path('/proc').glob('[0-9]*'):
 if int(p.name)==os.getpid():continue
 try:
  args=(p/'cmdline').read_bytes().split(b'\0'); argv=[x.decode(errors='replace') for x in args if x]
  if argv and (argv[0].startswith('/home/hzhy/drone-fixed-workload-20261013/') or (len(argv)>1 and argv[0].endswith('python3') and argv[1].startswith('/home/hzhy/drone-fixed-workload-20261013/'))):remaining.append({'pid':int(p.name),'argv':argv})
 except (OSError,ProcessLookupError):pass
before=json.loads((e/'protected-before.json').read_text())
result={'mono_ns':time.monotonic_ns(),'formal':fields,'identity':identity,'protected_sha_equal':identity==before,'drm_text':drm,'drm_clients':clients,'formal_hdmi_found':any(x.get('formal') for x in clients),'remaining':remaining}
(e/'final-state.json').write_text(json.dumps(result,ensure_ascii=False,indent=2));print(json.dumps({k:v for k,v in result.items() if k not in ['identity','drm_text']},ensure_ascii=False,indent=2))

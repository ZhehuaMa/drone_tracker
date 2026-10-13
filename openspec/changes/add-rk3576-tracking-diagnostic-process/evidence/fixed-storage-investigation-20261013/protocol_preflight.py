import os,subprocess,time,signal,json
from pathlib import Path
r=Path('/home/hzhy/drone-fixed-storage-20261013');b=r/'drone_tracker/lib/drone_tracker';e=r/'protocol-preflight';e.mkdir();tmp=Path('/tmp/drone-fixed-storage-20261013/protocol');tmp.mkdir();control=tmp/'control';control.mkdir();sock=str(control/'worker.sock');started=time.monotonic_ns();processes=[]
try:
 with (e/'worker.log').open('w') as log:
  w=subprocess.Popen(['taskset','-c','4-7',str(b/'rk3576_diagnostic_worker'),'--socket',sock,'--directory',str(e/'emmc'),'--test-switch-control',str(control),'--test-tmpfs-directory',str(tmp/'tmpfs'),'--minimum-free-bytes','8388608'],stdout=log,stderr=log);processes.append(w)
  until=time.monotonic()+3
  while not Path(sock).exists():
   if w.poll() is not None or time.monotonic()>until:raise RuntimeError('worker not ready')
   time.sleep(.01)
  c=subprocess.run(['taskset','-c','0-3',str(b/'storage_switch_test'),sock,str(control),'4'],capture_output=True,text=True,timeout=8);(e/'client.log').write_text(c.stdout+c.stderr);assert c.returncode==0,c.stderr
  w.send_signal(signal.SIGTERM);assert w.wait(timeout=3)==0
 subprocess.run(['cp','-a',str(tmp),str(e/'tmpfs-originals')],check=True,timeout=3)
 exports=[]
 for f in e.rglob('*.dtd'):
  dest=e/('export-'+str(len(exports)));p=subprocess.run([str(b/'rk3576_diagnostic_inspect'),str(f),'--output',str(dest)],capture_output=True,text=True,timeout=3);exports.append({'file':str(f),'rc':p.returncode});assert p.returncode==0
 (e/'result.json').write_text(json.dumps({'elapsed_seconds':(time.monotonic_ns()-started)/1e9,'exports':exports},indent=2))
finally:
 for p in processes:
  if p.poll() is None:p.kill();p.wait(timeout=2)

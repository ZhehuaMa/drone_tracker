"""复制全部实验内存原件到 eMMC，核对后才允许精确卸载。"""
import hashlib,json,shutil,subprocess
from pathlib import Path
r=Path('/home/hzhy/drone-fixed-storage-20261013');tmp=Path('/tmp/drone-fixed-storage-20261013');destination=r/'tmpfs-final-originals';destination.mkdir();files=[]
for p in tmp.rglob('*'):
 if p.is_file():
  relative=p.relative_to(tmp);q=destination/relative;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q);a=hashlib.sha256(p.read_bytes()).hexdigest();assert a==hashlib.sha256(q.read_bytes()).hexdigest();files.append({'path':str(relative),'bytes':p.stat().st_size,'sha256':a})
(r/'tmpfs-final-copy-verification.json').write_text(json.dumps(files,indent=2));print('durable copies verified',len(files))
subprocess.run(['umount',str(tmp)],check=True,timeout=5);tmp.rmdir()
(r/'tmpfs-cleanup.json').write_text(json.dumps({'exact_path':str(tmp),'unmounted':True,'files_verified':len(files)},indent=2))

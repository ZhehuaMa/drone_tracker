from pathlib import Path
import shutil
root=Path.cwd(); target=root/'.docker-artifacts/fixed-workload-investigation-20261013/source'
target.mkdir(exist_ok=False)
for name in ['src','parameters','launch','cmake','deployment','models','tests']:
 p=root/name
 if p.exists(): shutil.copytree(p,target/name,symlinks=True,ignore=shutil.ignore_patterns('__pycache__'))
for name in ['CMakeLists.txt','package.xml','build.sh']:
 shutil.copy2(root/name,target/name)
art=target/'.docker-artifacts';art.mkdir()
for name in ['build','install']: shutil.copytree(root/'.docker-artifacts'/name,art/name,symlinks=True)

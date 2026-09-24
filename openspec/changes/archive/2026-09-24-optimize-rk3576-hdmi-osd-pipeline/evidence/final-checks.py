"""性能运行完成后串行执行故障与退出验证，不与测量争用设备。"""
import json,os,subprocess,time,hashlib
from pathlib import Path
R=Path('/home/hzhy/hdmi-osd-optimization-20260924');E=R/'evidence'
while not ((E/'implementation-histogram.sha256').exists() and (E/'comparison-analysis.json').exists() and (E/'comparison-analysis.json').stat().st_mtime > (E/'implementation-histogram.sha256').stat().st_mtime):time.sleep(5)
# next-comparison.sh 先恢复正式服务，之后才写分析文件。
assert subprocess.call(['systemctl','is-active','--quiet','drone-tracker.service'])==0
original=Path('/opt/drone_tracker/config/camera.yaml').read_bytes();release=os.path.realpath('/opt/drone_tracker/current')
for mode in ['fault-commit','fault-timeout']:
 with (E/(mode+'-console.log')).open('w') as log:
  subprocess.run(['python3','acceptance.py',mode],cwd=R,stdout=log,stderr=subprocess.STDOUT,check=True)
try:
 subprocess.run(['systemctl','stop','drone-tracker.service'],check=True)
 with (E/'pipeline-probe-final.log').open('w') as log:
  subprocess.run(['stdbuf','-oL',str(R/'osd_pipeline_probe')],env=dict(os.environ,LD_PRELOAD=str(R/'audit_rga.so')),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
 config=E/'comparison-2-cached-overlay.yaml'
 args=[str(R/'install/lib/drone_tracker/drone_tracker_node'),'--ros-args','--params-file',str(config),'-p','osd.drm_device:=/dev/dri/osd-missing-device']
 env=dict(os.environ,ROS_DOMAIN_ID='197',ROS_LOCALHOST_ONLY='1',ROS_LOG_DIR=str(E/'open-failure-ros'))
 result=subprocess.run(args,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
 text=result.stdout.decode(errors='replace');(E/'drm-open-failure.log').write_text(text)
 assert result.returncode!=0 and 'osd-missing-device' in text
 (E/'drm-open-failure.json').write_text(json.dumps({'command':args,'exit_code':result.returncode,'expected_startup_rejection':True},indent=2))
finally:
 assert original==Path('/opt/drone_tracker/config/camera.yaml').read_bytes() and release==os.path.realpath('/opt/drone_tracker/current')
 subprocess.run(['systemctl','start','drone-tracker.service'],check=True)
 (E/'final-restore.json').write_text(json.dumps({'service_active':subprocess.call(['systemctl','is-active','--quiet','drone-tracker.service'])==0,'config_and_release_unchanged':True,'runtime_sha256':hashlib.sha256(Path('/usr/lib/librknnrt.so').read_bytes()).hexdigest()},indent=2))
print('PASS final faults, probe, open failure, restore',flush=True)

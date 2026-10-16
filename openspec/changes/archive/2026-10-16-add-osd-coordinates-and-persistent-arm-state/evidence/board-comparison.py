"""Private fixed-replay acceptance with durable diagnostics and service recovery."""
import os, sys, json, hashlib, signal, subprocess, time, re
from pathlib import Path
import yaml
import rclpy
from std_msgs.msg import Header
from sensor_msgs.msg import Imu, NavSatFix
from mavros_msgs.msg import State
from nav_msgs.msg import Odometry
from vision_msgs.msg import Detection2D
from acceptance_safety import Diagnostics, cleanup_and_restore, service_states, stop_owned_process
R=Path('/home/hzhy/osd-gps-armed-status-20261016'); E=R/'evidence'; E.mkdir(exist_ok=True)
mode=sys.argv[1] if len(sys.argv)>1 else 'smoke'
assert mode in ['smoke','comparison','endurance','fault-commit','fault-timeout','rounds']
original=Path('/opt/drone_tracker/config/camera.yaml').read_bytes()
release=os.path.realpath('/opt/drone_tracker/current')
active=subprocess.run(['systemctl','is-active','--quiet','drone-tracker.service']).returncode==0
os.environ.update(ROS_DOMAIN_ID='197',ROS_LOCALHOST_ONLY='1',RCUTILS_COLORIZED_OUTPUT='0')
rclpy.init(); n=rclpy.create_node('hdmi_optimization_acceptance')
trigger=n.create_publisher(Header,'/fpv/tracker_trigger',10)
imu=n.create_publisher(Imu,'/fpv/gimbal',10); odom=n.create_publisher(Odometry,'/fpv/odom',10)
flight=n.create_publisher(State,'/mavros/state',10)
position=n.create_publisher(NavSatFix,'/mavros/global_position/global',10)
boxes=[]
sub=n.create_subscription(Detection2D,'/fpv/target_bbox',lambda msg: boxes.append((time.monotonic(),msg.header.frame_id)),100)
p=None
diagnostics=Diagnostics(E/(mode+'-checkpoints.jsonl'))
initial_services=service_states()

def settings():
 return {'governors':{str(f):f.read_text().strip() for f in Path('/sys/devices/system/cpu/cpufreq').glob('policy*/scaling_governor')},'affinity':sorted(os.sched_getaffinity(0)),'nice':os.getpriority(os.PRIO_PROCESS,0)}
def spin(seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  diagnostics.check(resources if p is not None and p.poll() is None else None)
  if p is not None and p.poll() is not None: raise RuntimeError('node exited '+str(p.returncode))
  flight.publish(State(connected=True,armed=True,mode='OFFBOARD'))
  fix=NavSatFix();fix.status.status=0;fix.latitude=-90.;fix.longitude=-180.;position.publish(fix)
  stamp=n.get_clock().now().to_msg(); i=Imu();i.header.stamp=stamp;i.orientation.w=1.;imu.publish(i)
  o=Odometry();o.header.stamp=stamp;o.pose.pose.orientation.w=1.;odom.publish(o)
  rclpy.spin_once(n,timeout_sec=.02)
def command(value):
 h=Header();h.frame_id=value;h.stamp=n.get_clock().now().to_msg();trigger.publish(h)
def stop():
 global p
 stop_owned_process(p,diagnostics)
def resources():
 path=Path('/proc')/str(p.pid); status=dict(s.split(':',1) for s in (path/'status').read_text().splitlines() if ':' in s)
 return {'cpu_ticks':sum(int(x) for x in (path/'stat').read_text().split()[13:15]),'mono':time.monotonic(),'rss_kb':int(status['VmRSS'].split()[0]),'threads':int(status['Threads']),
         'fds':len(list((path/'fd').iterdir())), 'affinity':status['Cpus_allowed_list'].strip(),
         'scheduling':subprocess.check_output(['ps','-L','-p',str(p.pid),'-o','tid=,cls=,rtprio=,ni=']).decode(),
         'thread_affinities':sorted(set(next(line.split(':')[1].strip() for line in f.read_text().splitlines() if line.startswith('Cpus_allowed_list:')) for f in (path/'task').glob('*/status'))),
         'temps':{str(f):f.read_text().strip() for f in Path('/sys/class/thermal').glob('thermal_zone*/temp')}}
params=yaml.safe_load(original)
a=params['drone_tracker_node']['ros__parameters'];a['video_source']='/home/hzhy/hdmi-hud-acceptance-20260924/fixed-drone-1920x1200-650s-60fps.avi'
a['tracker']['yolo_engine']='/opt/drone_tracker/models/yolo.rknn';a['tracker']['yolo_target_class_id']=2
a['camera'].update(fx=1875.,fy=1875.,cx=960.,cy=600.)
a['recording']['enabled']=False;a['visualized_result']=False;a['need_save']=False
paths=[Path(a['video_source']),Path('/usr/lib/librknnrt.so')]+[Path(a['tracker'][key]) for key in ['yolo_engine','nano_template_backbone','nano_search_backbone','nano_head']]
def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024), b''):h.update(chunk)
 return h.hexdigest()
identity={'kernel':os.uname().release,'hostname':os.uname().nodename,'board':Path('/proc/device-tree/model').read_text().rstrip('\0'),
          'sha256':{str(f):digest(f) for f in paths},
          'before':settings(),'original_release':release,'service_active':active,'source_note':'V_DRONE_051 remux copy; repeated and converted to 60fps MJPEG; original 640x512 -> 1500x1200 plus 210px horizontal margins; 1920x1200, simulated 1875/1875/960/600 intrinsics'}
identity['initial_services']=initial_services
(E/(mode+'-identity.json')).write_text(json.dumps(identity,indent=2))
diagnostics.record('identity',identity=identity)
cases=[('cached-overlay',True,15)] if mode=='smoke' else ([(path,path!='off',75) for i in range(3) for path in ['off','legacy','cached-overlay']])
if mode=='endurance': cases=[('cached-overlay',True,610)]
if mode.startswith('fault-'): cases=[('cached-overlay',True,15)]
if mode=='rounds': cases=[('cached-overlay',True,2)] * 20
diagnostics.install_signal_handlers()
try:
 diagnostics.check(force=True)
 subprocess.run(['systemctl','stop','drone-tracker.service'],check=True)
 for index,(path,enabled,seconds) in enumerate(cases):
  label=mode+'-'+str(index)+'-'+path; logpath=E/(label+'.log');config=E/(label+'.yaml')
  a['osd'].update(enabled=enabled,timing_enabled=True,render_path=path if enabled else 'legacy')
  config.write_text(yaml.safe_dump(params,sort_keys=False))
  env=dict(os.environ,ROS_LOG_DIR=str(E/label),DRONE_TRACKER_CPU_CONFIG='/opt/drone_tracker/config/cpu-affinity.yaml',LD_LIBRARY_PATH='/opt/drone_tracker/current/lib/bundled-ros:'+os.environ.get('LD_LIBRARY_PATH',''))
  if mode.startswith('fault-'): env.update(LD_PRELOAD=str(R/'audit_rga.so'),OSD_TEST_FAULT=mode.split('-')[1])
  executable=R/'install/lib/drone_tracker/drone_tracker_node'
  args=['taskset','-c','4-7',str(executable),'--ros-args','--params-file',str(config)]
  f=logpath.open('w'); p=subprocess.Popen(args,stdout=f,stderr=subprocess.STDOUT,env=env,start_new_session=True);f.close()
  diagnostics.record('node_started',label=label,pid=p.pid,command=args)
  result={'command':args,'settings_before':settings(),'resources':[],'rounds':[]}; boxes.clear()
  print('START',label,'pid',p.pid,flush=True)
  deadline=time.monotonic()+35
  while trigger.get_subscription_count()==0:
   if time.monotonic()>deadline:raise RuntimeError('trigger discovery timeout')
   spin(.1)
  spin(.3);command('ON');live=None
  while not boxes:
   if time.monotonic()>deadline:raise RuntimeError('first tracking result timeout')
   spin(.05)
  live=time.monotonic();result['live']=live;result['live_wall']=time.time()
  while time.monotonic()-live<seconds:
   spin(.2)
   if not result['resources'] or time.monotonic()-result['resources'][-1]['mono']>=1:
    result['resources'].append(resources())
  result['end']=time.monotonic();result['end_wall']=time.time();command('OFF');spin(.6)
  stop(); result['exit_code']=p.returncode;p=None
  result['boxes']=boxes.copy();result['settings_after']=settings()
  text=logpath.read_text(errors='replace')
  result['tracking_states']={state:len(re.findall('Tracker State: '+state,text)) for state in ['Detecting','TargetFound','Tracking','TargetLost']}
  result['valid_boxes']=sum(bool(x[1]) for x in boxes)
  result['display_failure']='OSD disabled after display failure' in text
  if mode.startswith('fault-'):
   assert result['display_failure'], 'display fault not reported'
   assert sum(t > live + 10 for t, _ in boxes)>10, 'tracking did not survive display fault'
  (E/(label+'.json')).write_text(json.dumps(result,indent=2))
  if result['exit_code']!=0:raise RuntimeError('unclean exit')
  print('DONE',label,'messages',len(boxes),'valid',result['valid_boxes'],flush=True)
except BaseException as error:
 diagnostics.record('failed',error=str(error),acceptance_completed=False)
 raise
finally:
 def restore():
  if active:subprocess.run(['systemctl','start','drone-tracker.service'],check=True,timeout=60)
 def verify_restore():
  evidence={'settings':settings(),'services':service_states(),
            'config_and_release_unchanged':Path('/opt/drone_tracker/config/camera.yaml').read_bytes()==original and os.path.realpath('/opt/drone_tracker/current')==release}
  evidence['service_active']=evidence['services']['drone-tracker']=='active'
  evidence['services_restored']=evidence['services']==initial_services
  (E/(mode+'-restore.json')).write_text(json.dumps(evidence,indent=2))
  assert evidence['config_and_release_unchanged'], 'production config/release changed'
  assert evidence['services_restored'], 'service states differ from baseline'
  return evidence
 try:cleanup_and_restore(p,diagnostics,restore,verify_restore)
 finally:
  try:n.destroy_node()
  finally:rclpy.shutdown()
diagnostics.record('completed',acceptance_completed=True)

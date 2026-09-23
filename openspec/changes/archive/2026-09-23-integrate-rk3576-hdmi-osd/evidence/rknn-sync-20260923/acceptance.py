"""生产板合成视频运行记录；不判断 HDMI 像素或模型捕获质量。"""
import os,pathlib,time,json,signal,subprocess,sys
os.environ['ROS_DOMAIN_ID']='193';os.environ['ROS_LOCALHOST_ONLY']='1'
import rclpy
from rclpy.qos import qos_profile_sensor_data
from std_msgs.msg import Header
from sensor_msgs.msg import Imu
from nav_msgs.msg import Odometry
from vision_msgs.msg import Detection2D
from mavros_msgs.msg import StatusText
R=pathlib.Path('/home/hzhy/drone-tracker-rknn-sync-20260923');mode=sys.argv[1];duration=float(sys.argv[2]) if len(sys.argv)>2 else 610
rclpy.init();n=rclpy.create_node('osd_acceptance');events=[];boxes=[];p=None
trigger=n.create_publisher(Header,'/fpv/tracker_trigger',qos_profile_sensor_data)
imu=n.create_publisher(Imu,'/fpv/gimbal',qos_profile_sensor_data);odom=n.create_publisher(Odometry,'/fpv/odom',qos_profile_sensor_data)
subs=[n.create_subscription(StatusText,'/mavros/statustext/send',lambda m:events.append((time.monotonic(),m.text)),10),n.create_subscription(Detection2D,'/fpv/target_bbox',lambda m:boxes.append((time.monotonic(),m.header.stamp.sec*10**9+m.header.stamp.nanosec,m.header.frame_id)),10)]
def spin(sec):
 end=time.monotonic()+sec
 while time.monotonic()<end:
  if p is not None and p.poll() is not None:raise RuntimeError('node exited '+str(p.returncode))
  i=Imu();i.orientation.w=1.;i.header.stamp=n.get_clock().now().to_msg();imu.publish(i)
  o=Odometry();o.pose.pose.orientation.w=1.;o.header.stamp=i.header.stamp;odom.publish(o)
  rclpy.spin_once(n,timeout_sec=.02)
def wait(fn,timeout=30):
 end=time.monotonic()+timeout
 while not fn():
  if time.monotonic()>end:raise RuntimeError('wait timeout')
  spin(.05)
def command(s):
 h=Header();h.frame_id=s;h.stamp=n.get_clock().now().to_msg();trigger.publish(h)
def resource():
 q=pathlib.Path('/proc')/str(p.pid);d=dict(l.split(':',1) for l in (q/'status').read_text().splitlines() if ':' in l);a=(q/'stat').read_text().split()
 return dict(t=time.monotonic(),rss_kb=int(d['VmRSS'].split()[0]),threads=int(d['Threads']),fds=len(list((q/'fd').iterdir())),cpu_seconds=(int(a[13])+int(a[14]))/os.sysconf('SC_CLK_TCK'),boxes=len(boxes))
result={'mode':mode,'input':'synthetic-640x480-20fps.avi','quality_evaluation':False,'rounds':[]};log=R/(mode+'.log');samples=[]
try:
 env=os.environ.copy()
 if mode=='display-failure':env['LD_PRELOAD']=str(pathlib.Path('/home/hzhy/drone-tracker-osd-20260923/inject_display_failure.so'))
 if mode=='audit':env['LD_PRELOAD']=str(pathlib.Path('/home/hzhy/drone-tracker-osd-20260923/audit_display.so'))
 f=log.open('w');p=subprocess.Popen([str(R/'drone_tracker_node'),'--ros-args','--params-file','/home/hzhy/drone-tracker-osd-20260923/synthetic.yaml','-p','osd.enabled:='+str(mode!='load-off').lower()],stdout=f,stderr=subprocess.STDOUT,env=env);f.close()
 wait(lambda:trigger.get_subscription_count()>0);spin(1);result['initial']=resource();result['pid']=p.pid
 for j in range(21 if mode=='endurance' else (20 if mode=='rounds' else 1)):
  boxes.clear();events.clear();before=time.time_ns();command('ON');wait(lambda:len(boxes)>0);begin=time.monotonic();initial=resource()
  while time.monotonic()-begin<(2 if mode=='rounds' or (mode=='endurance' and j<20) else duration):
   spin(.2);samples.append(resource())
  count=len(events);off=time.monotonic();command('OFF');wait(lambda:any(t>=off and text=='Drone Tracker Stopped' for t,text in events));stop=time.monotonic()-off;spin(.5)
  old=len(boxes);spin(.5);assert old==len(boxes),'output after stop'
  assert all(x[1]>=before for x in boxes),'old round timestamp'
  result['rounds'].append({'index':j+1,'seconds':off-begin,'messages':len(boxes),'valid_boxes':sum(bool(x[2]) for x in boxes),'stop_observed_seconds':stop,'begin':initial,'idle':resource()})
  print(mode,j+1,result['rounds'][-1],flush=True)
 result['final']=resource();closing=p;started=time.monotonic();p=None;closing.send_signal(signal.SIGINT);closing.wait(timeout=20);result['display_failure']='OSD disabled after display failure' in log.read_text();result['exit_code']=closing.returncode;result['exit_seconds']=time.monotonic()-started;assert closing.returncode==0
except Exception as e:
 result['error']=repr(e)
 if p is not None and p.poll() is None:
  p.send_signal(signal.SIGINT)
  try:p.wait(timeout=20)
  except subprocess.TimeoutExpired:result['exit_timeout']=True
 raise
finally:
 (R/(mode+'-result.json')).write_text(json.dumps(result,indent=2));(R/(mode+'-resources.json')).write_text(json.dumps(samples));n.destroy_node();rclpy.shutdown()

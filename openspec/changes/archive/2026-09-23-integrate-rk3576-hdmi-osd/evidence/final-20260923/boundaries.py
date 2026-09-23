"""生产板合成视频运行记录；不判断 HDMI 像素或模型捕获质量。"""
import os,pathlib,time,json,signal,subprocess,sys
os.environ['ROS_DOMAIN_ID']='193';os.environ['ROS_LOCALHOST_ONLY']='1'
import rclpy
from rclpy.qos import qos_profile_sensor_data
from std_msgs.msg import Header
from sensor_msgs.msg import Imu, BatteryState
from nav_msgs.msg import Odometry
from vision_msgs.msg import Detection2D
from mavros_msgs.msg import StatusText
R=pathlib.Path('/home/hzhy/drone-tracker-osd-final-20260923');mode=sys.argv[1];duration=float(sys.argv[2]) if len(sys.argv)>2 else 610
rclpy.init();n=rclpy.create_node('osd_acceptance');events=[];boxes=[];p=None
trigger=n.create_publisher(Header,'/fpv/tracker_trigger',qos_profile_sensor_data)
imu=n.create_publisher(Imu,'/fpv/gimbal',qos_profile_sensor_data);odom=n.create_publisher(Odometry,'/fpv/odom',qos_profile_sensor_data)
subs=[n.create_subscription(StatusText,'/mavros/statustext/send',lambda m:events.append((time.monotonic(),m.text)),10),n.create_subscription(Detection2D,'/fpv/target_bbox',lambda m:boxes.append((time.monotonic(),m.header.stamp.sec*10**9+m.header.stamp.nanosec,m.header.frame_id)),10)]
battery=n.create_publisher(BatteryState,'/mavros/battery',10)
osd_imu=n.create_publisher(Imu,'/mavros/imu/data',qos_profile_sensor_data)
voltage=24.0; send_battery=True; send_attitude=True
def spin(sec):
 end=time.monotonic()+sec
 while time.monotonic()<end:
  if p is not None and p.poll() is not None:raise RuntimeError('node exited '+str(p.returncode))
  i=Imu();i.orientation.w=1.;i.header.stamp=n.get_clock().now().to_msg();imu.publish(i)
  o=Odometry();o.pose.pose.orientation.w=1.;o.header.stamp=i.header.stamp;odom.publish(o)
  if send_attitude:osd_imu.publish(i)
  if send_battery:
   b=BatteryState();b.voltage=voltage;b.current=1.;b.capacity=13.;b.charge=10.;b.percentage=.8;b.present=True;battery.publish(b)
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
hold=float(os.environ.get('OSD_OBSERVE_HOLD','15'))
assert 1 <= hold <= 300
result={'mode':mode,'events':[]};log=R/(mode+'.log');video=R/'boundary.mov';hidden=R/'boundary.hidden'
try:
 f=log.open('w');p=subprocess.Popen([str(R/'drone_tracker_node'),'--ros-args','--params-file','/home/hzhy/drone-tracker-osd-20260923/synthetic.yaml','-p','video_source:='+str(video)],stdout=f,stderr=subprocess.STDOUT);f.close()
 wait(lambda:trigger.get_subscription_count()>0);spin(1)
 for j in range(2):
  print('START EOF ROUND',j+1,flush=True)
  events.clear();boxes.clear();command('ON');wait(lambda:any(text=='Drone Tracker Stopped' for t,text in events));assert boxes
  result['events'].append({'case':'EOF','frames':len(boxes),'resource':resource()});print('EOF FREEZE: PLAYBACK ENDED',flush=True);spin(hold)
 assert log.read_text().count('Input ended: EOF')==2
 video.rename(hidden);events.clear();command('ON');wait(lambda:'Round failed:' in log.read_text());wait(lambda:any(text=='Drone Tracker Stopped' for t,text in events));result['events'].append({'case':'missing input','resource':resource()});print('INPUT FAILURE: FROZEN ERROR',flush=True);spin(hold);hidden.rename(video)
 events.clear();boxes.clear();command('ON');wait(lambda:bool(boxes));spin(1);command('OFF');wait(lambda:any(text=='Drone Tracker Stopped' for t,text in events));result['events'].append({'case':'recovered new round','frames':len(boxes),'resource':resource()})
 print('OFF FREEZE: BATTERY 23V; ATTITUDE EXPIRES',flush=True);voltage=23.;send_attitude=False;spin(hold)
 print('OFF FREEZE: BATTERY EXPIRES',flush=True);send_battery=False;spin(hold)
 result['events'].append({'case':'telemetry visual phases completed','resource':resource()})
 closing=p;p=None;closing.send_signal(signal.SIGINT);closing.wait(timeout=20);result['exit_code']=closing.returncode;assert closing.returncode==0
except Exception as e:
 result['error']=repr(e)
 if p is not None and p.poll() is None:p.send_signal(signal.SIGINT);p.wait(timeout=20)
 raise
finally:
 if hidden.exists():hidden.rename(video)
 (R/(mode+'-result.json')).write_text(json.dumps(result,indent=2));n.destroy_node();rclpy.shutdown()

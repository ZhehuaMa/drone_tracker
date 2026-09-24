"""独立短测记录线程调度系统调用，不用于性能统计。"""
import os,sys,json,signal,subprocess,time
from pathlib import Path
import rclpy
from std_msgs.msg import Header
R=Path('/home/hzhy/hdmi-osd-optimization-20260924');E=R/'evidence'
os.environ.update(ROS_DOMAIN_ID='198',ROS_LOCALHOST_ONLY='1')
rclpy.init();node=rclpy.create_node('osd_scheduling_audit');pub=node.create_publisher(Header,'/fpv/tracker_trigger',10)
active=subprocess.call(['systemctl','is-active','--quiet','drone-tracker.service'])==0
p=None
try:
 subprocess.run(['systemctl','stop','drone-tracker.service'],check=True)
 for index,path in [(0,'off'),(1,'legacy'),(2,'cached-overlay')]:
  config=E/f'comparison-{index}-{path}.yaml';base=E/('scheduling-'+path)
  args=['strace','-f','-e','trace=setpriority,sched_setaffinity,sched_setscheduler,sched_setparam','-o',str(base)+'.strace',str(R/'install/lib/drone_tracker/drone_tracker_node'),'--ros-args','--params-file',str(config)]
  with open(str(base)+'.log','w') as log:p=subprocess.Popen(args,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  deadline=time.monotonic()+30
  while pub.get_subscription_count()==0:
   assert p.poll() is None and time.monotonic()<deadline
   rclpy.spin_once(node,timeout_sec=.1)
  deadline=time.monotonic()+1
  while time.monotonic()<deadline:rclpy.spin_once(node,timeout_sec=.1)
  h=Header();h.frame_id='ON';pub.publish(h)
  deadline=time.monotonic()+5
  while time.monotonic()<deadline:rclpy.spin_once(node,timeout_sec=.1)
  children=subprocess.check_output(['pgrep','-P',str(p.pid)]).decode().split()
  snapshot={pid:subprocess.check_output(['ps','-L','-p',pid,'-o','tid,cls,rtprio,ni']).decode() for pid in children}
  os.killpg(p.pid,signal.SIGINT);p.wait(timeout=30)
  assert p.returncode==0
  Path(str(base)+'.json').write_text(json.dumps({'command':args,'threads':snapshot,'exit_code':p.returncode},indent=2));p=None
  deadline=time.monotonic()+1
  while time.monotonic()<deadline:rclpy.spin_once(node,timeout_sec=.1)
finally:
 if p is not None and p.poll() is None:os.killpg(p.pid,signal.SIGINT);p.wait(timeout=30)
 if active:subprocess.run(['systemctl','start','drone-tracker.service'],check=True)
 node.destroy_node();rclpy.shutdown()

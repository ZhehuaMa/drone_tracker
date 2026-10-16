"""Capture actual DRM planes from an isolated new tracker process."""
import hashlib,json,os,signal,subprocess,time
from pathlib import Path
import yaml,rclpy
from mavros_msgs.msg import State
from nav_msgs.msg import Odometry
from sensor_msgs.msg import BatteryState,Imu,NavSatFix
from std_msgs.msg import Bool,Header
from vision_msgs.msg import Detection2D
ROOT=Path('/home/hzhy/osd-gps-armed-status-20261016');OUT=ROOT/'visual';OUT.mkdir(exist_ok=True)
original=Path('/opt/drone_tracker/config/camera.yaml').read_bytes();release=Path('/opt/drone_tracker/current').resolve()
active=subprocess.run(['systemctl','is-active','--quiet','drone-tracker.service']).returncode==0
os.environ.update(ROS_DOMAIN_ID='197',ROS_LOCALHOST_ONLY='1',RCUTILS_COLORIZED_OUTPUT='0')
rclpy.init();node=rclpy.create_node('coordinates_arm_display_acceptance')
trigger=node.create_publisher(Header,'/fpv/tracker_trigger',10)
flight=node.create_publisher(State,'/mavros/state',10)
position=node.create_publisher(NavSatFix,'/mavros/global_position/global',10)
battery=node.create_publisher(BatteryState,'/mavros/battery',10)
failsafe_pub=node.create_publisher(Bool,'/fpv/failsafe',10)
imu=node.create_publisher(Imu,'/fpv/gimbal',10)
odom=node.create_publisher(Odometry,'/fpv/odom',10)
boxes=[];sub=node.create_subscription(Detection2D,'/fpv/target_bbox',lambda msg:boxes.append((time.monotonic(),bool(msg.header.frame_id))),100)
p=None;events=[];send=True;connected=True;armed=True;valid_position=True;failure=False

def tick(seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if p is not None and p.poll() is not None:raise RuntimeError('tracker exited '+str(p.returncode))
        stamp=node.get_clock().now().to_msg()
        if send:
            flight.publish(State(connected=connected,armed=armed,mode='OFFBOARD'))
            fix=NavSatFix();fix.status.status=0 if valid_position else -1;fix.latitude=-90.;fix.longitude=-180.;position.publish(fix)
            battery.publish(BatteryState(present=True,voltage=22.2,current=4.3,percentage=.72,capacity=10.,charge=9.59))
            failsafe_pub.publish(Bool(data=failure))
        i=Imu();i.header.stamp=stamp;i.orientation.w=1.;imu.publish(i)
        o=Odometry();o.header.stamp=stamp;o.pose.pose.orientation.w=1.;odom.publish(o)
        rclpy.spin_once(node,timeout_sec=.04)

def command(value):
    message=Header(frame_id=value);message.stamp=node.get_clock().now().to_msg();trigger.publish(message)

def capture(name,width,path):
    result=subprocess.run(['/tmp/osd-gps-armed-capture',str(OUT/name)],capture_output=True,text=True,check=True)
    event=dict(name=name,width=width,height=480 if width==720 else 720,render_path=path,wall=time.time(),mono=time.monotonic(),send=send,connected=connected,armed=armed,valid_position=valid_position,failsafe=failure,boxes=len(boxes),capture=result.stdout)
    events.append(event);(OUT/'events.json').write_text(json.dumps(events,indent=2));print('CAPTURE',name,flush=True)

def stop():
    global p
    if p is not None and p.poll() is None:p.send_signal(signal.SIGINT);p.wait(timeout=30)
    if p is not None and p.returncode!=0:raise RuntimeError('unclean tracker exit '+str(p.returncode))
    p=None

try:
    subprocess.run(['systemctl','stop','drone-tracker.service'],check=True)
    for width,path in [(720,'cached-overlay'),(1280,'legacy')]:
        params=yaml.safe_load(original);cfg=params['drone_tracker_node']['ros__parameters']
        cfg['video_source']='/home/hzhy/hdmi-hud-acceptance-20260924/fixed-drone-1920x1200-650s-60fps.avi'
        cfg['tracker']['yolo_engine']='/opt/drone_tracker/models/yolo.rknn';cfg['tracker']['yolo_target_class_id']=2
        cfg['camera'].update(fx=1875.,fy=1875.,cx=960.,cy=600.)
        cfg['recording']['enabled']=False;cfg['need_save']=False;cfg['visualized_result']=False
        cfg['osd'].update(enabled=True,width=width,height=480 if width==720 else 720,render_path=path,timing_enabled=True)
        config=OUT/f'visual-{width}.yaml';config.write_text(yaml.safe_dump(params,sort_keys=False))
        log=(OUT/f'node-{width}.log').open('w')
        p=subprocess.Popen([str(ROOT/'install/lib/drone_tracker/drone_tracker_node'),'--ros-args','--params-file',str(config)],stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,LD_LIBRARY_PATH='/opt/drone_tracker/current/lib/bundled-ros:'+os.environ.get('LD_LIBRARY_PATH',''),ROS_LOG_DIR=str(OUT/f'ros-{width}')));log.close()
        send=True;armed=True;connected=True;valid_position=True;failure=False
        deadline=time.monotonic()+30
        while trigger.get_subscription_count()==0:
            if time.monotonic()>deadline:raise RuntimeError('discovery timeout')
            tick(.1)
        tick(.5);before=len(boxes);command('ON')
        while len(boxes)==before:
            if time.monotonic()>deadline:raise RuntimeError('tracking timeout')
            tick(.1)
        tick(3.2);capture(f'{width}-armed-over3s',width,path)
        if width==720:tick(72);capture(f'{width}-armed-over75s',width,path)
        failure=True;command('OFF');tick(.4);capture(f'{width}-armed-trigger-frozen-warning',width,path)
        tick(2.1);capture(f'{width}-armed-trigger-expired',width,path)
        armed=False;tick(.4);capture(f'{width}-disarmed-frozen',width,path)
        connected=False;tick(.4);capture(f'{width}-disconnected',width,path)
        connected=True;send=False;tick(2.3);capture(f'{width}-stale',width,path)
        send=True;armed=True;valid_position=False;tick(.4);capture(f'{width}-invalid-position',width,path)
        valid_position=True;tick(.4);capture(f'{width}-recovered',width,path)
        command('MISSION');tick(.3);capture(f'{width}-mission',width,path)
        command('OFF');tick(.5);stop();time.sleep(.5)
finally:
    try:stop()
    finally:
        assert Path('/opt/drone_tracker/config/camera.yaml').read_bytes()==original
        assert Path('/opt/drone_tracker/current').resolve()==release
        if active:subprocess.run(['systemctl','start','drone-tracker.service'],check=True)
        (OUT/'restore.json').write_text(json.dumps(dict(config_unchanged=True,release_unchanged=True,service_active=subprocess.run(['systemctl','is-active','--quiet','drone-tracker.service']).returncode==0),indent=2))
        node.destroy_node();rclpy.shutdown()

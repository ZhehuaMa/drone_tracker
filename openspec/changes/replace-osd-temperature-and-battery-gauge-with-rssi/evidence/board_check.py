#!/usr/bin/env python3
"""Bounded isolated-domain candidate check; output is injected telemetry evidence."""
import json
import os
from pathlib import Path
import signal
import subprocess
import time

import cv2
import numpy as np
import rclpy
from mavros_msgs.msg import RCIn
from sensor_msgs.msg import Imu
from std_msgs.msg import Header, Int8
from rclpy.qos import QoSProfile, ReliabilityPolicy

work = Path(__file__).resolve().parent
node_process = None
rclpy.init()
node = rclpy.create_node('rssi_acceptance_source')
rc = node.create_publisher(RCIn, '/rssi_acceptance/rc', QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT))
trigger = node.create_publisher(Header, '/fpv/tracker_trigger', 10)
imu = node.create_publisher(Imu, '/mavros/imu/data', QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT))
states = []
node.create_subscription(Int8, '/fpv/tracker_state', lambda msg: states.append(msg.data), 10)

def pump(seconds, percent=None, event=None):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if node_process.poll() is not None:
            raise RuntimeError('candidate exited: ' + str(node_process.returncode))
        if percent is not None:
            message = RCIn(); message.rssi = percent; rc.publish(message)
        attitude = Imu(); attitude.orientation.w = 1.0; imu.publish(attitude)
        if event:
            control = Header(); control.frame_id = event; trigger.publish(control)
        rclpy.spin_once(node, timeout_sec=0.02)
        time.sleep(0.03)

def capture(label, expected):
    planes = []
    for plane in (58, 103):
        raw = subprocess.check_output(['sudo', '-n', 'kmsgrab', '--plane', str(plane)], timeout=10)
        (work / f'{label}-plane-{plane}.bgra').write_bytes(raw)
        assert len(raw) == 720 * 480 * 4, len(raw)
        planes.append(np.frombuffer(raw, dtype=np.uint8).reshape(480, 720, 4).copy())
    video, overlay = planes
    alpha = overlay[:, :, 3:4].astype(np.float32) / 255
    combined = (overlay[:, :, :3] * alpha + video[:, :, :3] * (1-alpha)).astype(np.uint8)
    cv2.imwrite(str(work / f'{label}.png'), combined)
    cv2.imwrite(str(work / f'{label}-video.png'), video)
    row = combined[335, 21:169, :3]
    yellow = np.all(row == np.array([60, 200, 255], np.uint8), axis=1)
    count = int(yellow.sum())
    assert count == expected, (label, count, expected)
    return {'phase': label, 'yellow_pixels_at_bar_midline': count}

try:
    log = (work / 'candidate.log').open('w')
    node_process = subprocess.Popen([str(work / 'drone_tracker_node'), '--ros-args', '--params-file', str(work / 'camera.yaml'), '-r', '/mavros/rc/in:=/rssi_acceptance/rc'], stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    (work / 'candidate.pid').write_text(str(node_process.pid))
    pump(7, 85)
    assert rc.get_subscription_count() == 1, rc.get_subscription_count()
    results = [capture('preview-85', 125)]
    pump(3.0)
    results.append(capture('preview-expired', 0))
    pump(1, 9)
    results.append(capture('preview-recovered-9', 13))
    pump(0.5, 85, 'ON')
    pump(5, 85)
    assert states, 'no tracker state after ON'
    pump(0.5, 85, 'OFF')
    pump(1, 85)
    results.append(capture('preview-after-off-85', 125))
    os.killpg(node_process.pid, signal.SIGINT)
    node_process.wait(timeout=25)
    assert node_process.returncode == 0, node_process.returncode
    data = {'injected_rssi_only': True, 'board': 'HZ-RK3576_WuKong Board (hzhy)', 'ros_domain': os.environ.get('ROS_DOMAIN_ID'), 'captures': results, 'tracker_states': states, 'candidate_exit': node_process.returncode}
    (work / 'result.json').write_text(json.dumps(data, indent=2) + '\n')
    print(json.dumps(data))
finally:
    if node_process and node_process.poll() is None:
        os.killpg(node_process.pid, signal.SIGINT)
        try:
            node_process.wait(timeout=25)
        except subprocess.TimeoutExpired:
            os.killpg(node_process.pid, signal.SIGKILL)
            node_process.wait(timeout=5)
    node.destroy_node()
    rclpy.shutdown()

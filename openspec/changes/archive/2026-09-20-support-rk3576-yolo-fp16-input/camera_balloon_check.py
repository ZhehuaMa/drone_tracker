"""开发板 2 有气球后的有界复测；复用已验收的 ON/OFF 与退出清理函数。"""
import importlib.util
import json
from pathlib import Path

import cv2
import numpy as np
from rosidl_runtime_py.convert import message_to_ordereddict


spec = importlib.util.spec_from_file_location(
    "acceptance", "/home/firefly/ros-camera-final-20260915/ros_acceptance.py"
)
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)
a.R = Path.cwd()
original_image = a.on_image


def on_image(message):
    original_image(message)
    path = a.R / "tracking.png"
    if a.states and a.states[-1] == 2 and not path.exists():
        pixels = np.frombuffer(bytes(message.data), dtype=np.uint8).reshape(message.height, message.step)
        pixels = pixels[:, :message.width * 3].reshape(message.height, message.width, 3)
        assert cv2.imwrite(str(path), pixels)


a.on_image = on_image
valid_boxes = []
subscription = a.n.create_subscription(
    a.Detection2D, "/fpv/target_bbox",
    lambda message: valid_boxes.append(message_to_ordereddict(message)) if message.header.frame_id else None,
    10,
)
result = {}
try:
    result["initial"] = a.start(
        "fp16-balloon-camera", "fp16", "software_trigger", True,
        ["tracker.yolo_engine:=/home/firefly/Desktop/models/balloon_best_zoo6_fp16.rknn",
         "tracker.yolo_target_class_id:=0"],
    )
    expected = {
        "/fpv/gimbal": "sensor_msgs/msg/Imu", "/fpv/odom": "nav_msgs/msg/Odometry",
        "/fpv/tracker_trigger": "std_msgs/msg/Header", "/fpv/target_bbox": "vision_msgs/msg/Detection2D",
        "/fpv/tracker_image": "sensor_msgs/msg/Image", "/fpv/tracker_state": "std_msgs/msg/Int8",
    }
    types = dict(a.n.get_topic_names_and_types())
    assert all(value in types.get(key, []) for key, value in expected.items())
    result["topics"] = {key: types[key] for key in expected}
    result["rounds"] = [a.round_run(seconds=10), a.round_run(seconds=5)]
    result["exit"] = a.close()
    result["tracking_seen"] = all(2 in row["states"] and row["valid_boxes"] > 0 for row in result["rounds"])
    result["valid_box_count"] = len(valid_boxes)
    result["first_valid_boxes"] = valid_boxes[:5]
    print(json.dumps(result, indent=2))
finally:
    if a.active is not None:
        result["cleanup_exit"] = a.close()
    (a.R / "camera-balloon-results.json").write_text(json.dumps(result, indent=2) + "\n")
    a.n.destroy_node()
    a.rclpy.shutdown()

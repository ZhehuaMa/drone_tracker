"""指定生产板的非断电录像验收；不修改正式配置、历史会话或系统库。"""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time

os.environ["ROS_DOMAIN_ID"] = "194"
os.environ["ROS_LOCALHOST_ONLY"] = "1"

import rclpy
from rclpy.qos import qos_profile_sensor_data
from mavros_msgs.msg import StatusText
from nav_msgs.msg import Odometry
from sensor_msgs.msg import Imu
from std_msgs.msg import Header, Int8
from vision_msgs.msg import Detection2D


def process_sample(pid):
    """只读取本次启动进程的资源；进程退出时返回空值。"""
    root = Path("/proc") / str(pid)
    try:
        status = dict(line.split(":", 1) for line in (root / "status").read_text().splitlines() if ":" in line)
        # comm 字段允许空格；从右括号后解析，避免 CPU 字段错位。
        stat = (root / "stat").read_text().rsplit(")", 1)[1].split()
        try:
            io = dict(line.split(":", 1) for line in (root / "io").read_text().splitlines() if ":" in line)
        except FileNotFoundError:
            # 当前生产内核未提供进程 I/O 统计，不能因此丢弃其他资源数据。
            io = {}
        return {
            "pid": pid, "rss_kib": int(status.get("VmRSS", "0 kB").split()[0]),
            "threads": int(status["Threads"]), "fds": len(list((root / "fd").iterdir())),
            "cpu_seconds": (int(stat[11]) + int(stat[12])) / os.sysconf("SC_CLK_TCK"),
            "write_bytes": int(io["write_bytes"]) if "write_bytes" in io else None,
            "process_io_available": bool(io),
        }
    except (FileNotFoundError, ProcessLookupError):
        return None


def worker_pid(parent):
    # 生产内核没有 /proc/PID/task/TID/children，使用 PPid 与精确程序名同时确认。
    matches = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            status = (entry / "status").read_text()
            ppid = next(int(line.split()[1]) for line in status.splitlines() if line.startswith("PPid:"))
            if ppid == parent and Path(os.readlink(entry / "exe")).name == "rk3576_recording_worker":
                matches.append(int(entry.name))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    if len(matches) > 1:
        raise RuntimeError("同一节点出现多个录像工作进程")
    return matches[0] if matches else None


def send_kill(pid):
    descriptor = os.pidfd_open(pid)
    try:
        signal.pidfd_send_signal(descriptor, signal.SIGKILL)
    finally:
        os.close(descriptor)


class Run:
    def __init__(self, args, label, sync_ms, osd=True, images=False):
        self.args = args
        self.label = label
        self.output = args.output / label
        self.output.mkdir()  # 排他保留每次运行证据，不覆盖旧结果。
        with (self.output / "camera-topology.txt").open("x") as topology:
            subprocess.run(["v4l2-ctl", "--list-devices"], stdout=topology, stderr=subprocess.STDOUT, check=True)
            subprocess.run(["media-ctl", "-d", "/dev/media2", "-p"],
                           stdout=topology, stderr=subprocess.STDOUT, check=True)
        self.node = rclpy.create_node("recording_acceptance")
        self.events = []
        self.box_count = 0
        self.last_box_us = None
        self.valid_count = 0
        self.states = []
        self.resources = []
        self.rounds = []
        self.process = None
        self.next_resource = 0.0
        mount_device = subprocess.check_output(
            ["findmnt", "-T", str(args.output), "-no", "SOURCE"], text=True).strip()
        self.storage_stat = Path("/sys/class/block") / Path(mount_device).name / "stat"
        self.trigger = self.node.create_publisher(Header, "/fpv/tracker_trigger", qos_profile_sensor_data)
        self.imu = self.node.create_publisher(Imu, "/fpv/gimbal", qos_profile_sensor_data)
        self.odom = self.node.create_publisher(Odometry, "/fpv/odom", qos_profile_sensor_data)
        self.subscriptions = [
            self.node.create_subscription(StatusText, "/mavros/statustext/send",
                                          lambda msg: self.events.append((time.monotonic(), msg.text)), 100),
            self.node.create_subscription(Detection2D, "/fpv/target_bbox", self.on_box, 100),
            self.node.create_subscription(Int8, "/fpv/tracker_state",
                                          lambda msg: self.states.append((time.monotonic(), msg.data)), 100),
        ]
        env = os.environ.copy()
        env["DRONE_RECORDING_TIMING_FILE"] = str(self.output / "timing.csv")
        command = [str(args.executable), "--ros-args", "--params-file", str(args.config),
                   "-p", "recording.enabled:=" + str(bool(sync_ms)).lower(),
                   "-p", "recording.directory:=" + str(args.recordings),
                   "-p", "recording.sync_interval_ms:=" + str(sync_ms or 1000),
                   "-p", "recording.segment_seconds:=60", "-p", "need_save:=false",
                   "-p", "osd.enabled:=" + str(osd).lower(),
                   "-p", "visualized_result:=" + str(images).lower(),
                   "-p", "mv_camera.media_device:=/dev/media2"]
        self.result = {"label": label, "command": command, "sync_ms": sync_ms,
                       "osd": osd, "images": images, "rounds": self.rounds,
                       "input": "SC233HGS /dev/video22 1920x1200 NV12 60fps",
                       "simulated_attitude": True, "quality_evaluation": False}
        self.result["storage_device"] = mount_device
        self.result["block_io_scope"] = "整个验收目录所在分区，包含其他进程写入；不是本进程专属写入量"
        with (self.output / "node.log").open("x") as log:
            self.process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        self.result["pid"] = self.process.pid

    def on_box(self, message):
        self.box_count += 1
        self.valid_count += bool(message.header.frame_id)
        self.last_box_us = time.monotonic_ns() // 1000

    def resource(self):
        child = worker_pid(self.process.pid)
        temperatures = {}
        for sensor in Path("/sys/class/thermal").glob("thermal_zone*/temp"):
            try:
                temperatures[sensor.parent.name] = int(sensor.read_text())
            except (OSError, ValueError):
                continue
        power = {}
        for sensor in Path("/sys/class/hwmon").glob("hwmon*/power*_input"):
            try:
                power[str(sensor)] = int(sensor.read_text())
            except (OSError, ValueError):
                continue
        try:
            block_fields = [int(value) for value in self.storage_stat.read_text().split()]
            block_io = {"write_operations": block_fields[4], "written_bytes": block_fields[6] * 512,
                        "write_time_ms": block_fields[7], "io_time_ms": block_fields[9]}
        except (OSError, ValueError, IndexError):
            block_io = None
        self.resources.append({"monotonic_us": time.monotonic_ns() // 1000,
                               "node": process_sample(self.process.pid),
                               "worker": process_sample(child) if child else None,
                               "temperature_millicelsius": temperatures, "power_microwatts": power,
                               "partition_io": block_io,
                               "boxes": self.box_count, "valid_boxes": self.valid_count})

    def spin(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"节点提前退出：{self.process.returncode}")
            stamp = self.node.get_clock().now().to_msg()
            imu = Imu()
            imu.header.stamp = stamp
            imu.orientation.w = 1.0
            self.imu.publish(imu)
            odometry = Odometry()
            odometry.header.stamp = stamp
            odometry.pose.pose.orientation.w = 1.0
            self.odom.publish(odometry)
            rclpy.spin_once(self.node, timeout_sec=0.02)
            if time.monotonic() >= self.next_resource:
                self.resource()
                self.next_resource = time.monotonic() + 1.0

    def wait(self, predicate, timeout=30):
        deadline = time.monotonic() + timeout
        while not predicate():
            if time.monotonic() > deadline:
                raise RuntimeError("等待节点状态超时")
            self.spin(0.04)

    def command(self, value):
        message = Header()
        message.stamp = self.node.get_clock().now().to_msg()
        message.frame_id = value
        self.trigger.publish(message)

    def start(self):
        self.wait(lambda: self.trigger.get_subscription_count() > 0)
        before = self.box_count
        on_us = time.monotonic_ns() // 1000
        self.command("ON")
        self.wait(lambda: self.box_count > before)
        return {"on_us": on_us, "first_box_us": self.last_box_us, "before_boxes": before}

    def stop(self, row):
        off = time.monotonic()
        self.command("OFF")
        self.wait(lambda: any(t >= off and text == "Drone Tracker Stopped" for t, text in self.events))
        row["off_us"] = int(off * 1000000)
        row["stop_observed_us"] = int((time.monotonic() - off) * 1000000)
        self.spin(1.0)
        before = self.box_count
        self.spin(0.5)
        if self.box_count != before:
            raise RuntimeError("OFF 排空之后仍有跟踪输出")
        if worker_pid(self.process.pid) is not None:
            raise RuntimeError("正常 OFF 后录像工作进程未回收")
        row["boxes_total_after_stop"] = self.box_count
        self.rounds.append(row)

    def window(self, seconds):
        row = self.start()
        self.spin(5.0)
        row["window_begin_us"] = time.monotonic_ns() // 1000
        row["window_boxes_begin"] = self.box_count
        self.spin(seconds)
        row["window_end_us"] = row["window_begin_us"] + int(seconds * 1000000)
        row["window_boxes_end"] = self.box_count
        self.stop(row)

    def close(self, error=None, killed=False):
        if error:
            self.result["error"] = str(error)
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGINT)
            try:
                self.process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                send_kill(self.process.pid)
                self.process.wait(timeout=10)
                self.result["shutdown_timeout"] = True
        self.result["exit_code"] = self.process.returncode
        self.result["parent_kill_expected"] = killed
        self.result["events"] = self.events
        self.result["states"] = self.states
        self.result["resources"] = self.resources
        (self.output / "result.json").write_text(json.dumps(self.result, ensure_ascii=False, indent=2))
        self.node.destroy_node()
        if error or (not killed and self.process.returncode != 0):
            raise RuntimeError(f"验收运行失败：{self.label}: {error or self.process.returncode}")


def execute(args, label, period, operation, **options):
    run = Run(args, label, period, **options)
    error = None
    try:
        operation(run)
    except Exception as caught:
        error = caught
    run.close(error, killed=args.mode == "parent-kill")
    print(label + " completed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["smoke", "acceptance", "kill", "parent-kill"])
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--config", type=Path, default=Path("/opt/drone_tracker/config/camera.yaml"))
    parser.add_argument("--recordings", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not all(path.is_absolute() for path in (args.executable, args.config, args.recordings, args.output)):
        parser.error("所有路径必须是绝对路径")
    args.output.mkdir(exist_ok=True)
    rclpy.init()
    try:
        if args.mode == "smoke":
            execute(args, "smoke-osd", 1000, lambda run: run.window(70))
        elif args.mode == "acceptance":
            def acceptance(run):
                run.window(300)
                row = run.start()
                run.command("ON")  # 重复 ON 不应创建第二个录像进程。
                run.spin(2)
                run.stop(row)
            execute(args, "five-minute", 1000, acceptance)
        elif args.mode == "kill":
            def kill_worker(run):
                row = run.start()
                run.spin(2)
                child = worker_pid(run.process.pid)
                if child is None:
                    raise RuntimeError("故障注入前未找到本轮录像进程")
                killed_at = time.monotonic()
                before = run.box_count
                send_kill(child)
                run.spin(2)
                if run.box_count <= before:
                    raise RuntimeError("录像死亡后跟踪没有继续输出")
                if not any(t >= killed_at and text.startswith("Camera recording failed:")
                           for t, text in run.events):
                    raise RuntimeError("录像进程强杀未报告故障")
                row.update({"worker_pid": child, "post_kill_boxes": run.box_count - before})
                run.stop(row)
                row = run.start()
                run.spin(2)
                run.stop(row)
            execute(args, "worker-kill", 1000, kill_worker)
        else:
            def kill_parent(run):
                row = run.start()
                run.spin(2)
                child = worker_pid(run.process.pid)
                if child is None:
                    raise RuntimeError("父进程故障注入前无录像工作进程")
                send_kill(run.process.pid)
                run.process.wait(timeout=10)
                deadline = time.monotonic() + 5
                while Path(f"/proc/{child}").exists() and time.monotonic() < deadline:
                    time.sleep(0.05)
                row["worker_pid"] = child
                row["worker_reclaimed"] = not Path(f"/proc/{child}").exists()
                run.rounds.append(row)
                if not row["worker_reclaimed"]:
                    raise RuntimeError("父进程退出后录像子进程未回收")
            execute(args, "parent-kill", 1000, kill_parent)
    finally:
        rclpy.shutdown()


if __name__ == "__main__":
    main()

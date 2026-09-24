#!/usr/bin/env python3
"""停止桌面，并在 OSD 启用时确认显示设备已无客户端。"""

import os
from pathlib import Path
import stat
import subprocess
import sys
import time

import yaml


def systemctl(*arguments):
    result = subprocess.run(
        ["systemctl", *arguments], check=True, text=True, capture_output=True
    )
    return result.stdout.strip()


def stop_desktop():
    # display-manager 是通用别名；显式检查 lightdm 也覆盖别名被禁用的生产板。
    for service in ("display-manager.service", "lightdm.service"):
        load_state = systemctl("show", service, "--property=LoadState", "--value")
        if load_state == "not-found":
            continue
        if load_state not in ("loaded", "masked"):
            raise RuntimeError(f"无法确认桌面服务状态: {service}: {load_state}")
        systemctl("stop", service)
        state = systemctl("show", service, "--property=ActiveState", "--value")
        if state not in ("inactive", "failed"):
            raise RuntimeError(f"桌面未停止: {service}: {state}")


def wait_for_display(device):
    info = Path(device).stat()
    if not stat.S_ISCHR(info.st_mode) or os.major(info.st_rdev) != 226:
        raise RuntimeError(f"不是 DRM 显示设备: {device}")
    # DRM 是 Linux 显示设备接口；debugfs 的 clients 列表反映设备打开者。
    clients_path = Path(f"/sys/kernel/debug/dri/{os.minor(info.st_rdev)}/clients")
    deadline = time.monotonic() + 5
    while True:
        lines = [line.split() for line in clients_path.read_text().splitlines() if line.strip()]
        if not lines or lines[0][:4] != ["command", "pid", "dev", "master"]:
            raise RuntimeError(f"无法解析显示设备占用信息: {clients_path}")
        if len(lines) == 1:
            return
        if time.monotonic() >= deadline:
            raise RuntimeError(f"显示设备仍被占用: {device}: {lines[1:]}")
        time.sleep(0.1)


def prepare(config):
    if os.geteuid() != 0:
        raise RuntimeError("关闭桌面需要 root，请使用 sudo 或 systemd 服务启动")
    params = yaml.safe_load(Path(config).read_text())["drone_tracker_node"]["ros__parameters"]
    osd = params.get("osd", {})
    stop_desktop()
    if osd.get("enabled", False):
        wait_for_display(osd.get("drm_device", "/dev/dri/card0"))
    print("桌面已停止，显示启动前置检查通过。", flush=True)


if __name__ == "__main__":
    try:
        prepare(sys.argv[1] if len(sys.argv) == 2 else "/opt/drone_tracker/config/camera.yaml")
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.CalledProcessError, yaml.YAMLError) as error:
        print(f"拒绝启动 drone_tracker: {error}", file=sys.stderr)
        sys.exit(1)

#!/usr/bin/env python3
"""统一 CPU 策略校验入口；只输出 taskset 可用的允许核心列表。"""
import argparse
import pathlib
import sys
import yaml

ROLES = ("control", "tracking", "yolo", "display", "capture", "recording")

def parse(path):
    source = pathlib.Path(path)
    if not source.is_absolute() or not source.is_file():
        raise ValueError(f"CPU 策略必须是存在的绝对路径: {path}")
    data = yaml.safe_load(source.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or type(data.get("version")) is not int or data["version"] != 1:
        raise ValueError("CPU 策略 version 必须为 1")
    mode = data.get("mode")
    if mode not in ("inherit", "role-pinned"):
        raise ValueError(f"CPU 策略 mode 不支持: {mode}")
    allowed = data.get("allowed_cpus")
    if not isinstance(allowed, list) or not allowed:
        raise ValueError("CPU 策略 allowed_cpus 必须是非空列表")
    if any(type(cpu) is not int or cpu < 0 for cpu in allowed) or len(set(allowed)) != len(allowed):
        raise ValueError("CPU 策略 allowed_cpus 含无效或重复核心")
    for cpu in allowed:
        directory = pathlib.Path(f"/sys/devices/system/cpu/cpu{cpu}")
        online = directory / "online"
        if not directory.is_dir() or (online.exists() and online.read_text().strip() != "1"):
            raise ValueError(f"CPU 策略核心不存在或离线: {cpu}")
    if mode == "role-pinned":
        roles = data.get("roles")
        if not isinstance(roles, dict):
            raise ValueError("CPU 策略缺少 roles")
        for role in ROLES:
            core = roles.get(role)
            if type(core) is not int or core not in allowed:
                raise ValueError(f"CPU 策略职责 {role} 缺失或不在允许范围")
    return allowed

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path")
    args = parser.parse_args()
    try:
        print(",".join(map(str, parse(args.path))))
    except (OSError, yaml.YAMLError, ValueError) as error:
        print(f"CPU 策略无效: {error}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

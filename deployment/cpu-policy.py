#!/usr/bin/env python3
"""Validate the shared CPU policy and print its taskset-compatible allowed set."""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import yaml

ROLES = ("control", "tracking", "yolo", "display", "capture", "recording")
CPU_SETSIZE = 1024


def validate_program(node, policy_path, enabled):
    """Reject older binaries that cannot honor the selected explicit topic role."""
    policy = yaml.safe_load(pathlib.Path(policy_path).read_text(encoding="utf-8"))
    if not enabled or policy["mode"] != "role-pinned":
        return
    result = subprocess.run([node, "--diagnostic-protocol-version"],
                            env=dict(os.environ, DRONE_TRACKER_RELEASE_CAPABILITIES="1"),
                            capture_output=True, text=True, timeout=10)
    try:
        capabilities = json.loads(result.stdout)
    except json.JSONDecodeError:
        capabilities = None
    if (result.returncode or not isinstance(capabilities, dict)
            or type(capabilities.get("topic_recording_affinity")) is not int
            or capabilities["topic_recording_affinity"] != 1):
        raise ValueError("Selected drone_tracker_node cannot confirm roles.topic_recording support; "
                         "rebuild the revised source and select its matching --install-root")


def recorder_enabled(camera_path):
    """Use the node's false default when a selected configuration omits need_save."""
    data = yaml.safe_load(pathlib.Path(camera_path).read_text(encoding="utf-8"))
    try:
        params = data["drone_tracker_node"]["ros__parameters"]
        enabled = params.get("need_save", False)
    except (KeyError, TypeError, AttributeError) as error:
        raise ValueError("Camera configuration must contain drone_tracker_node.ros__parameters") from error
    if type(enabled) is not bool:
        raise ValueError("Camera setting need_save must be Boolean")
    return enabled


def parse(path, check_available=True, require_topic_recording=False):
    source = pathlib.Path(path)
    if not source.is_absolute() or not source.is_file():
        raise ValueError(f"CPU policy must be an existing absolute path: {path}")
    data = yaml.safe_load(source.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or type(data.get("version")) is not int or data["version"] != 1:
        raise ValueError("CPU policy version must be 1")
    mode = data.get("mode")
    if mode not in ("inherit", "role-pinned"):
        raise ValueError(f"Unsupported CPU policy mode: {mode}")
    allowed = data.get("allowed_cpus")
    if not isinstance(allowed, list) or not allowed:
        raise ValueError("CPU policy allowed_cpus must be a nonempty list")
    if (any(type(cpu) is not int or cpu < 0 or cpu >= CPU_SETSIZE for cpu in allowed)
            or len(set(allowed)) != len(allowed)):
        raise ValueError("CPU policy allowed_cpus contains invalid or duplicate cores")
    if mode == "role-pinned":
        roles = data.get("roles")
        if not isinstance(roles, dict):
            raise ValueError("CPU policy is missing roles")
        for role in ROLES + ("ros_transport", "topic_recording"):
            if role not in ROLES and role not in roles:
                continue
            core = roles.get(role)
            if type(core) is not int or core not in allowed:
                raise ValueError(f"CPU policy roles.{role} must be an integer in allowed_cpus")
        if require_topic_recording and "topic_recording" not in roles:
            raise ValueError("Recorder-enabled role-pinned releases require roles.topic_recording; "
                             "add it and its core to allowed_cpus, or explicitly select mode: inherit")
    if check_available:
        for cpu in allowed:
            directory = pathlib.Path(f"/sys/devices/system/cpu/cpu{cpu}")
            online = directory / "online"
            if not directory.is_dir() or (online.exists() and online.read_text().strip() != "1"):
                field = " roles.topic_recording" if mode == "role-pinned" and roles.get("topic_recording") == cpu else ""
                raise ValueError(f"CPU {cpu}{field} is unavailable or offline on this target; "
                                 "select an available policy core")
    return allowed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path")
    parser.add_argument("--offline", action="store_true", help="Validate structure without build-host topology")
    parser.add_argument("--camera-config", help="Require the topic role for recorder-enabled role-pinned releases")
    parser.add_argument("--node", help="Also verify the selected node implements the required topic role")
    args = parser.parse_args()
    try:
        enabled = recorder_enabled(args.camera_config) if args.camera_config else False
        allowed = parse(args.path, not args.offline, enabled)
        if args.node:
            validate_program(args.node, args.path, enabled)
        print(",".join(map(str, allowed)))
    except (OSError, subprocess.SubprocessError, yaml.YAMLError, ValueError) as error:
        print(f"Invalid CPU policy: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

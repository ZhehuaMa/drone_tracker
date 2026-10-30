#!/usr/bin/env python3
"""Run two-process ROS discovery/latency checks in an isolated domain."""
import argparse
import json
import os
import pathlib
import select
import subprocess
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program")
    parser.add_argument("--samples", type=int, default=100)
    parser.add_argument("--period-ms", type=int, default=10)
    parser.add_argument("--control", type=int)
    parser.add_argument("--transport", type=int)
    parser.add_argument("--contention", action="store_true")
    parser.add_argument("--negative-checks", action="store_true")
    parser.add_argument("--node-initialization", action="store_true")
    args = parser.parse_args()
    program = [args.program]
    if pathlib.Path(args.program).read_bytes()[:20][18:20] == b"\xb7\x00" and os.uname().machine != "aarch64":
        emulator = os.environ.get("DRONE_QEMU_AARCH64", "/opt/qemu-arm64/bin/qemu-aarch64")
        program = [emulator, "-L", os.environ.get("QEMU_LD_PREFIX", "/opt/arm64-sysroot"), args.program]
    available = sorted(os.sched_getaffinity(0))
    control = args.control if args.control is not None else available[0]
    transport = args.transport if args.transport is not None else available[-1]
    for core in (control, transport):
        online = pathlib.Path(f"/sys/devices/system/cpu/cpu{core}/online")
        if not online.parent.is_dir() or (online.exists() and online.read_text().strip() != "1"):
            raise RuntimeError("requested CPU unavailable")
    with tempfile.TemporaryDirectory(prefix="ros-transport-probe-") as directory:
        policy = pathlib.Path(directory) / "cpu.yaml"
        roles = {role: control for role in ("control", "tracking", "yolo", "capture", "recording")}
        roles.update(display=transport, ros_transport=transport)
        policy.write_text("version: 1\nmode: role-pinned\nallowed_cpus: ["
                          + ", ".join(map(str, sorted({control, transport}))) + "]\nroles:\n"
                          + "".join(f"  {role}: {core}\n" for role, core in roles.items()))
        env = dict(os.environ, DRONE_TRACKER_CPU_CONFIG=str(policy), ROS_LOCALHOST_ONLY="1",
                   ROS_DOMAIN_ID=os.environ.get("ROS_DOMAIN_ID", "183"),
                   RMW_IMPLEMENTATION="rmw_fastrtps_cpp")
        if args.node_initialization:
            return subprocess.run(program + ["--affinity-initialization"], env=env).returncode
        load = None
        if args.contention:
            duration = args.samples * args.period_ms / 1000 * 2 + 20
            code = ("import os,time;os.sched_setaffinity(0,{" + str(control) + "});"
                    "end=time.monotonic()+" + str(duration) + "\n"
                    "while time.monotonic()<end:\n"
                    " start=time.monotonic()\n"
                    " while time.monotonic()-start<0.0075:pass\n"
                    " time.sleep(0.0025)\n")
            load = subprocess.Popen([sys.executable, "-c", code])
            print(json.dumps({"event": "contention", "pid": load.pid, "cpu": control,
                              "duty_cycle": 0.75, "nice": os.getpriority(os.PRIO_PROCESS, load.pid),
                              "maximum_duration_s": duration}), flush=True)
        try:
            cases = [(None, None)]
            if args.negative_checks:
                cases += [(0, None), (None, 1)]
            for maximum, skip in cases:
                common = program + ["--samples", str(args.samples), "--period-ms", str(args.period_ms)]
                receiver_command = common + ([] if maximum is None else ["--max-latency-ms", str(maximum)])
                receiver = subprocess.Popen(receiver_command, env=env, stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, text=True)
                sender = None
                try:
                    if not select.select([receiver.stdout], [], [], 15)[0]:
                        raise RuntimeError("receiver did not start")
                    ready = receiver.stdout.readline()
                    print(ready, end="", flush=True)
                    if '"event":"ready"' not in ready:
                        raise RuntimeError("invalid receiver readiness")
                    sender = subprocess.Popen(common + ["--publisher"]
                                              + ([] if skip is None else ["--skip-sequence", str(skip)]),
                                              env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                    timeout = args.samples * args.period_ms / 1000 * 2 + 35
                    out, err = receiver.communicate(timeout=timeout)
                    print(out, end="", flush=True)
                    print(err, end="", file=sys.stderr, flush=True)
                    expected_failure = maximum is not None or skip is not None
                    if (receiver.returncode != 0) != expected_failure:
                        if sender.poll() is None:
                            sender.terminate()
                        sender_out, sender_err = sender.communicate(timeout=5)
                        print(sender_out, end="", flush=True)
                        print(sender_err, end="", file=sys.stderr, flush=True)
                        raise RuntimeError(f"receiver verdict mismatch: {receiver.returncode}")
                    if expected_failure:
                        if sender.poll() is None:
                            sender.terminate()
                    sender_out, sender_err = sender.communicate(timeout=15)
                    print(sender_out, end="", flush=True)
                    print(sender_err, end="", file=sys.stderr, flush=True)
                    if not expected_failure and sender.returncode:
                        raise RuntimeError(f"publisher failed: {sender.returncode}")
                    print(json.dumps({"event": "case_passed", "threshold_failure": maximum is not None,
                                      "loss_failure": skip is not None}), flush=True)
                finally:
                    for process in (receiver, sender):
                        if process is not None and process.poll() is None:
                            process.terminate()
                            try:
                                process.wait(timeout=5)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait()
        finally:
            if load is not None:
                load.terminate()
                load.wait(timeout=5)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)

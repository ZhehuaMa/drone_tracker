import pathlib
import subprocess
import sys
import tempfile
import yaml

root = pathlib.Path(sys.argv[1])
helper = root / "deployment/cpu-policy.py"
deployment = root / "deployment"
online = [p for p in pathlib.Path("/sys/devices/system/cpu").glob("cpu[0-9]*")
          if p.is_dir() and (not (p / "online").exists() or (p / "online").read_text().strip() == "1")]
cpus = sorted(int(p.name[3:]) for p in online)
assert cpus

def check(data, valid):
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory) / "policy.yaml"
        path.write_text(yaml.safe_dump(data))
        result = subprocess.run([sys.executable, str(helper), str(path)], capture_output=True, text=True)
        assert (result.returncode == 0) == valid, result.stderr
        if valid:
            assert result.stdout.strip() == ",".join(str(x) for x in data["allowed_cpus"])

base = {
    "version": 1, "mode": "role-pinned", "allowed_cpus": cpus[:2],
    "roles": {role: cpus[0] for role in ("control", "tracking", "yolo", "display", "capture", "recording")},
}
check(base, True)
for field, value in (("version", 2), ("mode", "invalid"), ("allowed_cpus", []),
                     ("allowed_cpus", [cpus[0], cpus[0]]), ("allowed_cpus", [-1])):
    changed = dict(base)
    changed[field] = value
    check(changed, False)
changed = dict(base)
changed["roles"] = dict(base["roles"])
del changed["roles"]["display"]
check(changed, False)
changed = dict(base)
changed["mode"] = "inherit"
del changed["roles"]
check(changed, True)
template = yaml.safe_load((deployment / "cpu-affinity.yaml").read_text())
assert template == {
    "version": 1, "mode": "role-pinned", "allowed_cpus": [4, 5, 6, 7],
    "roles": {"control": 4, "tracking": 4, "yolo": 5, "display": 6, "capture": 7, "recording": 7},
}
service = (deployment / "drone-tracker.service").read_text()
runner = (deployment / "run-camera").read_text()
assert "ExecStart=/opt/drone_tracker/bin/run-camera" in service
assert "CPUAffinity=" not in service
assert "cpu-policy.py" in runner and "DRONE_TRACKER_CPU_CONFIG" in runner
assert "taskset -c \"$cpus\"" in runner
assert "taskset -c 4-7" not in runner
packager = (deployment / "package.sh").read_text()
installer = (deployment / "deploy.sh").read_text()
assert "--cpu-policy" in packager and "cpu_policy_sha256" in packager
assert "config/cpu-affinity.yaml" in installer and "cpu-policy.py" in installer

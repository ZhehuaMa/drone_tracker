#!/usr/bin/env bash
set -eo pipefail
source /opt/ros/humble/setup.bash
source /opt/drone_tracker/current/share/drone_tracker/local_setup.bash
set -u
work=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$work"
export LD_LIBRARY_PATH="/opt/drone_tracker/current/lib/bundled-ros${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export ROS_DOMAIN_ID=119 ROS_LOCALHOST_ONLY=1 ROS_LOG_DIR="$work/ros-log"
fingerprints() {
    readlink -f /opt/drone_tracker/current
    sha256sum /opt/drone_tracker/current/lib/drone_tracker/drone_tracker_node \
        /opt/drone_tracker/config/camera.yaml /opt/drone_tracker/config/cpu-affinity.yaml \
        /usr/lib/librknnrt.so
    find /opt/drone_tracker/models -type f -exec sha256sum {} \; | sort
}
restore() {
    result=$?
    trap - EXIT
    if [[ -f candidate.pid ]]; then
        candidate_pid=$(cat candidate.pid)
        if kill -0 "$candidate_pid" 2>/dev/null; then
            kill -INT -- "-$candidate_pid" || true
            for attempt in {1..25}; do
                kill -0 "$candidate_pid" 2>/dev/null || break
                sleep 1
            done
            kill -KILL -- "-$candidate_pid" 2>/dev/null || true
        fi
    fi
    sudo -n systemctl start drone-tracker.service
    for attempt in {1..20}; do
        [[ $(systemctl show drone-tracker.service -p SubState --value) == running ]] && break
        sleep 1
    done
    systemctl show drone-tracker.service -p ActiveState -p SubState -p MainPID > restoration.txt
    systemctl is-active drone-tracker.service >> restoration.txt
    fingerprints > after-fingerprints.txt
    diff -u before-fingerprints.txt after-fingerprints.txt >> restoration.txt
    systemctl show drone-interceptor.service -p ActiveState -p SubState >> restoration.txt
    if [[ $(systemctl show drone-tracker.service -p ActiveState --value) != active || $(systemctl show drone-tracker.service -p SubState --value) != running ]]; then
        exit 97
    fi
    exit "$result"
}
fingerprints > before-fingerprints.txt
systemctl show drone-tracker.service -p ActiveState -p SubState -p MainPID > before-service.txt
[[ $(systemctl show drone-tracker.service -p ActiveState --value) == active ]]
[[ $(systemctl show lightdm -p ActiveState --value) == inactive ]]
python3 - <<'PY'
from pathlib import Path
import yaml
config = yaml.safe_load(Path('/opt/drone_tracker/config/camera.yaml').read_text())
params = config['drone_tracker_node']['ros__parameters']
params['need_save'] = False
params['recording']['enabled'] = False
params['osd']['timing_enabled'] = False
Path('camera.yaml').write_text(yaml.safe_dump(config, sort_keys=False))
PY
ldd -r ./drone_tracker_node > candidate-ldd.txt 2>&1
! grep -E 'not found|undefined symbol' candidate-ldd.txt
trap restore EXIT
sudo -n systemctl stop drone-tracker.service
timeout --signal=INT --kill-after=30 75 python3 board_check.py

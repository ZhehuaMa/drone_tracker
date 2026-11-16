#!/usr/bin/env bash
set -eo pipefail
source /opt/ros/humble/setup.bash
set -u
root=${DRONE_RECORDING_VALIDATION_ROOT:-/var/lib/drone_tracker/validation-align-start-balloon-20261116}
cd "$root"
export ROS_DOMAIN_ID=186 ROS_LOCALHOST_ONLY=1
restore_services() {
    systemctl start drone-tracker-diagnostics.service drone-tracker.service drone-interceptor.service
    systemctl is-active drone-tracker.service drone-tracker-diagnostics.service drone-interceptor.service > restored-service-states.txt
}
trap restore_services EXIT
systemctl stop drone-interceptor.service drone-tracker.service drone-tracker-diagnostics.service
python3 release/prepare-display.py camera.yaml
set +e
timeout --signal=INT --kill-after=15 160 python3 board_rounds.py "$root" > controller.log 2>&1
result=$?
set -e
printf '%s\n' "$result" > controller.exit
exit "$result"

#!/usr/bin/env bash
set -eo pipefail
cd /tmp/nanotrack-window-20261012
source /opt/ros/humble/setup.bash
set -u
restore() {
    local test_status=$?
    trap - EXIT INT TERM
    systemctl start drone-tracker.service
    systemctl show drone-tracker.service -p ActiveState -p SubState -p MainPID > service-restored.txt
    if ! systemctl is-active --quiet drone-tracker.service; then
        cat service-restored.txt
        exit 90
    fi
    if ! grep -q '^SubState=running$' service-restored.txt; then
        cat service-restored.txt
        exit 91
    fi
    systemctl stop nanotrack-window-restore.timer || true
    exit "$test_status"
}
trap restore EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# USB 中断或 shell 被强制结束时，板端定时器仍会启动原正式服务。
systemd-run --unit=nanotrack-window-restore --on-active=10m /bin/systemctl start drone-tracker.service
systemctl stop drone-tracker.service
systemctl show drone-tracker.service -p ActiveState -p SubState -p MainPID > service-stopped.txt
if systemctl is-active --quiet drone-tracker.service; then
    exit 92
fi
ps -eo pid,comm,args > concurrent-processes.txt
timeout 300 build/nanotrack_window_recovery --measure experiment/config.yaml

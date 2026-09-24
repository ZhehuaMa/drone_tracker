#!/usr/bin/env bash
# 在指定生产板运行隔离验收；第二参数必须是尚不存在的证据目录名。
set -eo pipefail
mode=${1:?需要指定 smoke、acceptance、kill、parent-kill 或 lifecycle}
label=${2:?需要指定新的证据目录名}
case "$mode" in smoke|acceptance|kill|parent-kill|lifecycle) ;; *) exit 2 ;; esac
case "$label" in *[!a-zA-Z0-9_-]*|'') exit 2 ;; esac
source /opt/ros/humble/setup.bash
validation_root=/home/hzhy/drone-recording-validation-20260923
source "$validation_root/validation-install/share/drone_tracker/local_setup.bash"
output="$validation_root/$label"
mkdir "$output"
display_was_active=$(systemctl is-active display-manager || true)
restore_display() {
    if [[ "$display_was_active" == active ]]; then
        systemctl start display-manager
        systemctl is-active display-manager
    fi
}
trap restore_display EXIT
systemctl stop display-manager
sha256sum "$validation_root/validation-install/lib/drone_tracker/recording_validation_node" \
    "$validation_root/validation-install/lib/drone_tracker/rk3576_recording_worker" \
    /usr/lib/librknnrt.so /opt/drone_tracker/models/yolo.rknn \
    /opt/drone_tracker/models/fp16/template_backbone.rknn \
    /opt/drone_tracker/models/fp16/search_backbone.rknn \
    /opt/drone_tracker/models/fp16/head.rknn > "$output/identities.txt"
if [[ "$mode" == lifecycle ]]; then
    python3 "$validation_root/implementation/openspec/changes/add-rk3576-camera-recording/production_lifecycle_faults.py" \
        --executable "$validation_root/validation-install/lib/drone_tracker/recording_validation_node" \
        --recordings "$validation_root/$label-recordings" --output "$output" \
        --shim "$validation_root/inject_recording_faults.so"
else
    python3 "$validation_root/implementation/openspec/changes/add-rk3576-camera-recording/production_acceptance.py" \
    "$mode" --executable "$validation_root/validation-install/lib/drone_tracker/recording_validation_node" \
    --recordings "$validation_root/$label-recordings" --output "$output"
fi

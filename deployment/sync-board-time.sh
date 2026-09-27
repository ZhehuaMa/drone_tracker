#!/usr/bin/env bash
set -euo pipefail

# 在开发机运行：./deployment/sync-board-time.sh [ADB_SERIAL]
# 只接受 HZ-RK3576_WuKong Board；从开发机复制当前 UTC 时间到系统时钟和 RTC。

fail() { printf '校时失败：%s\n' "$*" >&2; exit 1; }

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    printf '用法：%s [ADB_SERIAL]\n' "$0"
    exit 0
fi
(( $# <= 1 )) || fail '最多指定一个 ADB 设备序列号'
command -v adb >/dev/null || fail '开发机未安装 adb'

if (( $# == 1 )); then
    serial=$1
else
    devices=$(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }')
    [[ -n $devices ]] || fail '没有已连接的 ADB 设备'
    [[ $devices != *$'\n'* ]] || fail '连接了多台设备，请指定 ADB 序列号'
    serial=$devices
fi
adb_cmd=(adb -s "$serial")

model=$("${adb_cmd[@]}" shell 'cat /proc/device-tree/model' | tr -d '\000\015')
[[ $model == 'HZ-RK3576_WuKong Board' ]] || fail "设备 $serial 的型号不符合预期：$model"
uid=$("${adb_cmd[@]}" shell 'id -u' | tr -d '\015')
[[ $uid == 0 ]] || fail 'ADB shell 必须以 root 身份运行，才能设置时钟和 RTC'
"${adb_cmd[@]}" shell 'test -e /dev/rtc0 && command -v hwclock >/dev/null && command -v systemctl >/dev/null' \
    || fail '板卡缺少 RTC、hwclock 或 systemctl'

printf '开发机时间：%s\n' "$(date '+%Y-%m-%d %H:%M:%S %Z %z')"
printf '目标板卡：%s (%s)\n' "$model" "$serial"

# 只恢复脚本开始时正在运行的服务。即使校时中途失败，也执行恢复。
services=(drone-interceptor.service drone-tracker.service)
active_services=()
for service in "${services[@]}"; do
    state=$("${adb_cmd[@]}" shell "systemctl is-active $service" 2>&1 | tr -d '\015') || true
    case $state in
        active|activating|reloading) active_services+=("$service") ;;
        inactive|failed) ;;
        *) fail "无法确认 $service 状态：$state" ;;
    esac
done

restore_services() {
    local result=$?
    local index service
    trap - EXIT INT TERM
    for ((index=${#active_services[@]}-1; index>=0; index--)); do
        service=${active_services[index]}
        if "${adb_cmd[@]}" shell "systemctl start $service && systemctl is-active --quiet $service"; then
            printf '已恢复服务：%s\n' "$service"
        else
            printf '恢复服务失败：%s\n' "$service" >&2
            result=1
        fi
    done
    exit "$result"
}
trap restore_services EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

for service in "${active_services[@]}"; do
    "${adb_cmd[@]}" shell "systemctl stop $service && ! systemctl is-active --quiet $service" \
        || fail "无法停止 $service"
    printf '已停止服务：%s\n' "$service"
done

# 尽量临近远端设置时取开发机时间，减少 ADB 传输造成的误差。
host_epoch=$(date -u +%s)
[[ $host_epoch =~ ^[0-9]+$ ]] || fail '无法读取开发机 UTC 时间'
"${adb_cmd[@]}" shell "date -u -s '@$host_epoch' >/dev/null && hwclock --systohc --utc" \
    || fail '写入系统时钟或 RTC 失败'

host_before=$(date -u +%s)
board_epoch=$("${adb_cmd[@]}" shell 'date -u +%s' | tr -d '\015')
host_after=$(date -u +%s)
rtc_before=$("${adb_cmd[@]}" shell 'date -u +%s' | tr -d '\015')
rtc_epoch=$("${adb_cmd[@]}" shell 'cat /sys/class/rtc/rtc0/since_epoch' | tr -d '\015')
rtc_after=$("${adb_cmd[@]}" shell 'date -u +%s' | tr -d '\015')
[[ $board_epoch =~ ^[0-9]+$ && $rtc_before =~ ^[0-9]+$ && $rtc_epoch =~ ^[0-9]+$ && $rtc_after =~ ^[0-9]+$ ]] \
    || fail '无法读回板卡时钟或 RTC'
(( board_epoch >= host_before - 2 && board_epoch <= host_after + 2 )) \
    || fail '板卡系统时间与开发机相差超过 2 秒'
(( rtc_epoch >= rtc_before - 2 && rtc_epoch <= rtc_after + 2 )) \
    || fail 'RTC 与板卡系统时间相差超过 2 秒'

printf '板卡系统时间：'
"${adb_cmd[@]}" shell 'date -Is'
printf '板卡 RTC 时间：'
"${adb_cmd[@]}" shell 'hwclock --show --utc'
printf '系统时钟和 RTC 校时成功。\n'

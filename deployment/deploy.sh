#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
fail() { echo "部署失败: $*" >&2; exit 1; }
mode=install
start=0
for arg in "$@"; do
    case "$arg" in
        --check) mode=check ;;
        --start) start=1 ;;
        --help|-h) echo "用法: sudo $0 [--check | --start]"; exit 0 ;;
        *) fail "未知参数: $arg" ;;
    esac
done
[[ $mode != check || $start == 0 ]] || fail '--check 与 --start 不能同时使用'
bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=/opt/drone_tracker
service=drone-tracker.service
[[ $(uname -m) == aarch64 ]] || fail '仅支持 ARM64 生产板'
[[ $(tr -d '\0' </proc/device-tree/model) == 'HZ-RK3576_WuKong Board' ]] || fail '不是 HZ-RK3576_WuKong Board'
[[ -r /opt/ros/humble/setup.bash ]] || fail '缺少 ROS 2 Humble'
for command in sha256sum systemctl systemd-analyze taskset flock ldd readelf tar python3; do
    command -v "$command" >/dev/null || fail "缺少命令 $command"
done
[[ -d /run/systemd/system ]] || fail '系统未使用 systemd 启动'
cd "$bundle"
sha256sum --check --quiet SHA256SUMS || fail '部署包校验失败'
runtime_hash=d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8
[[ $(sha256sum /usr/lib/librknnrt.so | cut -d' ' -f1) == "$runtime_hash" ]] || fail '系统 RKNN Runtime 与生产基线不同'
taskset -c 4-7 true || fail 'CPU 4–7 不可用'
while IFS= read -r device; do
    [[ -c $device ]] || fail "配置设备不存在: $device；请先核对相机拓扑，不要直接修改配置"
done < DEVICES
if [[ -d models ]]; then
    sha256sum --check --quiet MODELS.sha256 || fail '包内模型校验失败'
else
    (cd "$root" && sha256sum --check --quiet "$bundle/MODELS.sha256") || fail '目标模型缺失或不一致'
fi
# ROS 环境脚本允许读取未定义变量，加载时临时关闭 nounset。
set +u
source /opt/ros/humble/setup.bash
source "$bundle/payload/share/drone_tracker/local_setup.bash"
set -u
export LD_LIBRARY_PATH="$bundle/payload/lib/bundled-ros${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python3 -c 'import yaml' || fail '缺少 ROS 2 已有依赖 PyYAML，无法读取显示配置'
for program in drone_tracker_node rk3576_recording_worker rk3576_recording_inspect; do
    binary="$bundle/payload/lib/drone_tracker/$program"
    [[ -x $binary ]] || fail "程序不可执行: $program"
    readelf -h "$binary" | grep -q 'Machine:.*AArch64' || fail "程序不是 ARM64: $program"
    links=$(ldd -r "$binary" 2>&1) || fail "动态库检查失败: $program: $links"
    if grep -Eq 'not found|undefined symbol|version .* not found' <<<"$links"; then
        fail "动态库不匹配: $program: $links"
    fi
done
# verify 检查 ExecStart；新板未安装该路径时改用已有 /usr/bin/true 进行语法检查。
verify_dir=$(mktemp -d)
trap 'rm -rf -- "$verify_dir"' EXIT
sed 's|^ExecStart=.*|ExecStart=/usr/bin/true|' drone-tracker.service > "$verify_dir/$service"
systemd-analyze verify "$verify_dir/$service" || fail '服务文件检查失败'
echo '预检通过：包、模型、配置设备、CPU、ARM64 程序和系统动态库均可用。'
[[ $mode != check ]] || exit 0
[[ $EUID == 0 ]] || fail '安装需要 sudo；仅检查可使用 --check'
exec 9>/run/lock/drone-tracker-deploy.lock
flock -n 9 || fail '已有部署正在执行'
[[ ! -e $root/current || -L $root/current ]] || fail 'current 不是符号链接，拒绝覆盖'
[[ ! -d /etc/systemd/system/$service.d ]] || fail '存在服务覆盖配置，请先核对 CPU 和启动设置'
[[ ! -L /etc/systemd/system/$service ]] || fail '服务文件是符号链接或已 masked，请先核对'
was_active=0
systemctl is-active --quiet "$service" && was_active=1
stamp=$(date -u +%Y%m%dT%H%M%SZ)-$$
release="$root/releases/bundle-$stamp"
backup="$root/backups/$stamp"
mkdir -p "$backup" "$release"
systemctl is-enabled "$service" > "$backup/service-enabled.txt" 2>/dev/null || true
echo "$was_active" > "$backup/service-active.txt"
[[ ! -L $root/current ]] || readlink "$root/current" > "$backup/previous-release.txt"
items=()
for item in config bin models current; do
    if [[ -e $root/$item || -L $root/$item ]]; then items+=("$item"); fi
done
if ((${#items[@]})); then tar -cpf "$backup/installation.tar" -C "$root" "${items[@]}"; fi
if [[ -f /etc/systemd/system/$service ]]; then
    cp -a "/etc/systemd/system/$service" "$backup/$service"
fi
cp -a payload/. "$release/"
cp SOURCE.json SHA256SUMS MODELS.sha256 "$backup/"
# 从这里起可能改变正在运行的服务；任何失败均给出明确恢复入口。
trap 'echo "安装中断。旧版本与配置备份保留在: $backup；按 README 回退。" >&2' ERR
if ((was_active)); then systemctl stop "$service"; fi
mkdir -p "$root/config" "$root/bin" "$root/models" /var/lib/drone_tracker/recordings
if [[ -d models ]]; then cp -a models/. "$root/models/"; fi
install -m 644 config/camera.yaml "$root/config/camera.yaml"
install -m 755 run-camera "$root/bin/run-camera"
install -m 644 prepare-display.py "$root/bin/prepare-display.py"
install -m 644 drone-tracker.service "/etc/systemd/system/$service"
ln -s "$release" "$root/.current-$stamp"
mv -Tf "$root/.current-$stamp" "$root/current"
cmp config/camera.yaml "$root/config/camera.yaml"
(cd "$root" && sha256sum --check --quiet "$bundle/MODELS.sha256")
systemctl daemon-reload
systemctl enable "$service"
systemctl is-enabled --quiet "$service"
if ((start || was_active)); then
    systemctl restart "$service"
    systemctl is-active --quiet "$service"
fi
trap - ERR
echo "部署完成，已启用上电启动。配置与包内生产快照一致。备份: $backup"
echo "状态: systemctl status $service；日志: journalctl -u $service -f"

#!/usr/bin/env bash
# 恢复匹配的入口、配置、程序链接和服务；从不删除诊断原件或发布目录。
set -euo pipefail
root=/opt/drone_tracker
backup=${1:?用法: sudo rollback.sh /opt/drone_tracker/backups/备份名}
[[ $EUID == 0 && $backup == "$root/backups/"* && -f $backup/installation.tar ]] || { echo '备份无效或需要 sudo' >&2; exit 1; }
exec 9>/run/lock/drone-tracker-deploy.lock
flock -n 9 || { echo '已有部署正在执行' >&2; exit 1; }
systemctl stop drone-tracker.service || true
systemctl stop drone-tracker-diagnostics.service || true
# 将新配置移入备份边界，不删除任何历史模型或诊断文件。
stamp=$(date -u +%Y%m%dT%H%M%SZ)-$$
mkdir -p "$root/backups/rollback-$stamp"
for item in config bin current; do
    if [[ -e $root/$item || -L $root/$item ]]; then mv "$root/$item" "$root/backups/rollback-$stamp/"; fi
done
tar -xpf "$backup/installation.tar" -C "$root"
for service in drone-tracker.service drone-tracker-diagnostics.service; do
    if [[ -f $backup/$service ]]; then
        install -m 644 "$backup/$service" "/etc/systemd/system/$service"
    else
        systemctl disable "$service" || true
        rm -f "/etc/systemd/system/$service"
    fi
done
systemctl daemon-reload
if [[ $(cat "$backup/service-enabled.txt") == enabled ]]; then systemctl enable drone-tracker.service; else systemctl disable drone-tracker.service || true; fi
if [[ -f $backup/drone-tracker-diagnostics.service ]]; then
    if [[ $(cat "$backup/diagnostic-enabled.txt") == enabled ]]; then systemctl enable drone-tracker-diagnostics.service; else systemctl disable drone-tracker-diagnostics.service || true; fi
    if [[ $(cat "$backup/diagnostic-active.txt") == 1 ]]; then systemctl start drone-tracker-diagnostics.service; fi
fi
if [[ $(cat "$backup/service-active.txt") == 1 ]]; then systemctl start drone-tracker.service; fi
echo '回退完成，所有诊断日志保留在 /var/lib/drone_tracker/diagnostics；新格式日志使用保存的匹配检查器。'

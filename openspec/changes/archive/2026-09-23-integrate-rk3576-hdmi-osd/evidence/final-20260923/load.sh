#!/usr/bin/env bash
# 生产板复验：保留所有日志；有显示客户端残留时不恢复桌面。
set -eo pipefail
source /opt/ros/humble/setup.bash
source /home/hzhy/drone-tracker-osd-20260923/install-on/share/drone_tracker/local_setup.bash
cd /home/hzhy/drone-tracker-osd-final-20260923
restore_desktop() {
    cat /sys/kernel/debug/dri/0/clients > clients-load-after.txt
    if [ "$(wc -l < clients-load-after.txt)" -eq 1 ]; then
        systemctl start lightdm
        systemctl is-active lightdm > desktop-load-restored.txt
    else
        echo '仍有显示客户端，未恢复桌面' > desktop-load-restore-blocked.txt
    fi
}
systemctl stop lightdm
trap restore_desktop EXIT
systemctl is-active lightdm > desktop-load-stopped.txt || true
cat /sys/kernel/debug/dri/0/clients > clients-load-before.txt
test "$(wc -l < clients-load-before.txt)" -eq 1
sha256sum drone_tracker_node /usr/lib/librknnrt.so > identities-load.txt
ldd -r drone_tracker_node > ldd-final.txt
dmesg > kernel-load-before.txt
python3 acceptance.py load-off 60 > load-off-driver.log 2>&1
python3 acceptance.py endurance 610 > endurance-driver.log 2>&1
python3 - <<'PY'
import json
from pathlib import Path
for mode in ('load-off', 'endurance'):
    result = json.loads(Path(mode + '-result.json').read_text())
    assert result['exit_code'] == 0 and 'error' not in result
    assert not result['display_failure']
result = json.loads(Path('endurance-result.json').read_text())
assert len(result['rounds']) == 21 and result['rounds'][-1]['seconds'] >= 600
PY
python3 acceptance.py display-failure 10 > display-failure-driver.log 2>&1
dmesg > kernel-load-after.txt
echo 'PASS' > load-pass.txt

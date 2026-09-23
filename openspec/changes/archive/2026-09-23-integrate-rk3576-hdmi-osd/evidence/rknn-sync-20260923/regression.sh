#!/usr/bin/env bash
set -eo pipefail
source /opt/ros/humble/setup.bash
source /home/hzhy/drone-tracker-osd-20260923/install-on/share/drone_tracker/local_setup.bash
cd /home/hzhy/drone-tracker-rknn-sync-20260923
restore_desktop() {
    cat /sys/kernel/debug/dri/0/clients > clients-after.txt
    if [ "$(wc -l < clients-after.txt)" -eq 1 ]; then
        systemctl start lightdm
        systemctl is-active lightdm > desktop-restored.txt
    else
        printf 'Display clients remain; desktop restoration not attempted\n' > desktop-restore-blocked.txt
    fi
}
trap restore_desktop EXIT
[ "$(systemctl is-active lightdm || true)" = inactive ]
cat /sys/kernel/debug/dri/0/clients > clients-pre-regression.txt
[ "$(wc -l < clients-pre-regression.txt)" -eq 1 ]
sha256sum drone_tracker_node /usr/lib/librknnrt.so > final-binary-identities.txt
ldd drone_tracker_node > final-ldd.txt
dmesg > kernel-before.txt
cp load-off.log load-off-initial-config.log
cp load-off-result.json load-off-initial-config-result.json
python3 acceptance.py load-off 40 > load-off-driver.log 2>&1
python3 acceptance.py hdmi-smoke 30 > hdmi-smoke-driver.log 2>&1
python3 - <<'PY'
import json
r=json.load(open('hdmi-smoke-result.json'))
assert r['exit_code']==0 and not r['display_failure'] and 'error' not in r
PY
python3 acceptance.py endurance 610 > endurance-driver.log 2>&1
python3 - <<'PY'
import json
r=json.load(open('endurance-result.json'))
assert r['exit_code']==0 and not r['display_failure'] and 'error' not in r
assert len(r['rounds'])==21 and r['rounds'][-1]['seconds']>=600
PY
python3 acceptance.py display-failure 10 > display-failure-driver.log 2>&1
dmesg > kernel-after.txt
printf 'PASS regression finished\n' > regression-pass.txt

set -e
source /opt/ros/humble/setup.bash
cd /home/hzhy/hdmi-osd-optimization-20260924
while test ! -f evidence/endurance-restore.json; do sleep 5; done
test "$(systemctl is-active drone-tracker.service)" = active
mkdir -p evidence/initial-comparison
find evidence -maxdepth 1 -name 'comparison-*' -exec mv -t evidence/initial-comparison {} +
tar -xzf install-histogram.tar.gz -C install
source install/share/drone_tracker/local_setup.bash
sha256sum install/lib/drone_tracker/drone_tracker_node > evidence/implementation-histogram.sha256
python3 acceptance.py comparison > evidence/comparison-console.log 2>&1
python3 analyze.py evidence > evidence/comparison-analysis.json

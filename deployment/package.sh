#!/usr/bin/env bash
# 在生产源板执行；读取当前安装，不启动程序或修改服务。
set -euo pipefail
export LC_ALL=C
usage() {
    echo "用法: $0 输出目录 [--without-models] [--cpu-policy /绝对路径/策略.yaml] [--release /绝对路径/安装目录]"
}
[[ $# -ge 1 ]] || { usage; exit 2; }
output_argument=$1
shift
include_models=1
cpu_policy=
release_source=
while (($#)); do
    case "$1" in
        --without-models) include_models=0; shift ;;
        --cpu-policy) [[ $# -ge 2 ]] || { usage; exit 2; }; cpu_policy=$2; shift 2 ;;
        --release) [[ $# -ge 2 ]] || { usage; exit 2; }; release_source=$2; shift 2 ;;
        *) usage; exit 2 ;;
    esac
done
if [[ -n $release_source ]]; then
    [[ $release_source == /* && -d $release_source ]] || { echo '显式安装目录必须是存在的绝对路径' >&2; exit 2; }
    release_source=$(cd -- "$release_source" && pwd -P)
fi
if [[ -n $cpu_policy ]]; then
    [[ $cpu_policy == /* && -f $cpu_policy ]] || { echo '显式策略必须是存在的绝对路径' >&2; exit 2; }
else
    cpu_policy=/opt/drone_tracker/config/cpu-affinity.yaml
    [[ -f $cpu_policy ]] || { echo '旧源安装缺少 CPU 策略；须显式提供 --cpu-policy /绝对路径/策略.yaml' >&2; exit 2; }
fi
[[ $(uname -m) == aarch64 ]] || { echo '必须在 ARM64 生产源板打包' >&2; exit 1; }
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mkdir -p -- "$output_argument"
output=$(cd -- "$output_argument" && pwd)
stage=$(mktemp -d "$output/.package-XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
name="drone-tracker-$(date -u +%Y%m%dT%H%M%SZ)-${stage##*-}"
bundle="$stage/$name"
mkdir -p "$bundle/payload/lib/drone_tracker" "$bundle/config"
cp "$script_dir"/{deploy.sh,run-camera,prepare-display.py,cpu-policy.py,drone-tracker.service,README.md} "$bundle/"
python3 "$script_dir/cpu-policy.py" "$cpu_policy" >/dev/null

# 使用源板 ROS 环境解析共享库，不依赖调用者的交互 shell。
set +u
source /opt/ros/humble/setup.bash
set -u
# PyYAML 是 ROS 2 launch 的已有依赖；只解析数据，不执行配置内容。
python3 - "$bundle" "$include_models" "$cpu_policy" "$release_source" <<'PY'
import hashlib
import json
import pathlib
import re
import subprocess
import shutil
import sys
import yaml

bundle = pathlib.Path(sys.argv[1])
include_models = sys.argv[2] == '1'
policy_source = pathlib.Path(sys.argv[3])
root = pathlib.Path('/opt/drone_tracker')
release = pathlib.Path(sys.argv[4]) if sys.argv[4] else (root / 'current').resolve(strict=True)
tracked = {}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def copy(source, target):
    before = digest(source)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    if digest(target) != before:
        raise RuntimeError(f'复制期间文件变化: {source}')
    tracked[str(source)] = before

copy(root / 'config/camera.yaml', bundle / 'config/camera.yaml')
copy(policy_source, bundle / 'config/cpu-affinity.yaml')
params = yaml.safe_load((bundle / 'config/camera.yaml').read_text())['drone_tracker_node']['ros__parameters']
for name in ('drone_tracker_node', 'rk3576_recording_worker', 'rk3576_recording_inspect'):
    copy(release / 'lib/drone_tracker' / name, bundle / 'payload/lib/drone_tracker' / name)
# ldd 会列出传递依赖；显式加入运行时加载的消息类型支持库。
ros_lib = pathlib.Path('/opt/ros/humble/lib')
private_lib = bundle / 'payload/lib/bundled-ros'
private_lib.mkdir(parents=True)
seeds = [ros_lib / 'libcv_bridge.so']
seeds.extend(p for p in sorted(ros_lib.glob('libvision_msgs*.so'))
             if 'generator_py' not in p.name)
if len(seeds) < 2:
    raise RuntimeError('源板缺少 vision_msgs 类型支持库')
pending = list((bundle / 'payload/lib/drone_tracker').iterdir()) + seeds
seen = set()
def include_library(name, source):
    target = private_lib / name
    if target.exists():
        if digest(target) != digest(source):
            raise RuntimeError(f'同名库内容冲突: {name}')
    else:
        copy(source, target)
    pending.append(source)

for source in seeds:
    include_library(source.name, source)
while pending:
    source = pending.pop()
    if source in seen:
        continue
    seen.add(source)
    result = subprocess.run(['ldd', str(source)], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode or 'not found' in result.stdout:
        raise RuntimeError(f'源板依赖检查失败: {source}\n{result.stdout}')
    for line in result.stdout.splitlines():
        match = re.match(r'\s*(\S+) => (/\S+) \(', line)
        if not match:
            continue
        name, resolved = match.groups()
        dependency = pathlib.Path(resolved)
        if dependency.is_relative_to(ros_lib):
            include_library(name, dependency)

# ROS 包发现和环境文件保留；排除构建元数据、默认参数和模型占位文件。
for subdir in ('share/ament_index', 'share/drone_tracker'):
    for source in sorted((release / subdir).rglob('*')):
        relative = source.relative_to(release)
        if any(part in ('cmake', 'parameters', 'models', '__pycache__') for part in relative.parts):
            continue
        if source.is_file():
            copy(source, bundle / 'payload' / relative)

model_hashes = []
tracker = params['tracker']
for key in ('yolo_engine', 'nano_backbone', 'nano_template_backbone', 'nano_search_backbone', 'nano_head'):
    value = tracker.get(key)
    if not value:
        continue
    source = pathlib.Path(value)
    relative = source.relative_to(root / 'models')
    if '..' in relative.parts or any(c.isspace() for c in str(relative)):
        raise RuntimeError(f'不支持的模型路径: {source}')
    checksum = digest(source)
    tracked[str(source)] = checksum
    model_hashes.append(f'{checksum}  models/{relative}\n')
    if include_models:
        copy(source, bundle / 'models' / relative)
(bundle / 'MODELS.sha256').write_text(''.join(model_hashes))
devices = [params['video_source'], params['mv_camera']['media_device']]
if params.get('osd', {}).get('enabled'):
    devices.append(params['osd']['drm_device'])
for device in devices:
    if not str(device).startswith('/dev/') or '\n' in device:
        raise RuntimeError(f'不支持的设备路径: {device}')
(bundle / 'DEVICES').write_text(''.join(f'{d}\n' for d in devices))
recording_dir = params.get('recording', {}).get('directory', '/var/lib/drone_tracker/recordings')
if recording_dir != '/var/lib/drone_tracker/recordings':
    raise RuntimeError('需核对非标准录像目录后扩展部署脚本')
if not sys.argv[4] and (root / 'current').resolve(strict=True) != release:
    raise RuntimeError('打包期间 current 版本发生变化')
for source, expected in tracked.items():
    if digest(pathlib.Path(source)) != expected:
        raise RuntimeError(f'打包期间源文件变化: {source}')
(bundle / 'SOURCE.json').write_text(json.dumps({
    'board': pathlib.Path('/proc/device-tree/model').read_text().rstrip('\0'),
    'release': str(release), 'models_included': include_models,
    'cpu_policy_source': str(policy_source), 'cpu_policy_sha256': digest(policy_source),
    'files': tracked,
}, ensure_ascii=False, indent=2) + '\n')
PY
chmod 755 "$bundle/deploy.sh" "$bundle/run-camera"
(
    cd "$bundle"
    find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
    sha256sum --check --quiet SHA256SUMS
)
tar -czf "$output/$name.tar.gz" -C "$stage" "$name"
(cd "$output" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
echo "部署包: $output/$name.tar.gz"

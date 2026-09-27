#!/usr/bin/env bash
# 从本机 ARM64 构建产物生成生产部署包；不访问生产板。
set -euo pipefail

usage() {
    cat <<'EOF'
用法: deployment/package.sh 输出目录 --model-root 本地模型目录 [选项]

默认相机配置: parameters/rk3576_sc233hgs_production_params.yaml
默认安装目录: .docker-artifacts/install/drone_tracker
默认 CPU 策略: deployment/cpu-affinity.yaml

选项:
  --camera-config 绝对路径   使用指定相机配置，逐字节纳入包
  --install-root 绝对路径    使用指定 ARM64 安装目录
  --cpu-policy 绝对路径      使用指定 CPU 策略
  --without-models          省略模型文件，仍校验并记录模型 SHA-256
  --help                    显示帮助
EOF
}

fail() { printf '打包失败: %s\n' "$*" >&2; exit 2; }
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
image=drone-tracker-rk3576:humble-arm64-rknn2.3.2
camera_config=$repo_dir/parameters/rk3576_sc233hgs_production_params.yaml
install_root=$repo_dir/.docker-artifacts/install/drone_tracker
cpu_policy=$repo_dir/deployment/cpu-affinity.yaml
model_root=
without_models=0
output=
while (($#)); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --without-models) without_models=1; shift ;;
        --model-root|--camera-config|--install-root|--cpu-policy)
            (($# >= 2)) || fail "$1 缺少路径"
            option=$1; value=$2; shift 2
            [[ $value == /* ]] || fail "$option 必须是绝对路径"
            case "$option" in
                --model-root) model_root=$value ;;
                --camera-config) camera_config=$value ;;
                --install-root) install_root=$value ;;
                --cpu-policy) cpu_policy=$value ;;
            esac ;;
        --*) fail "未知选项: $1" ;;
        *) [[ -z $output ]] || fail "多余的位置参数: $1"; output=$1; shift ;;
    esac
done
[[ -n $output ]] || fail '缺少输出目录'
[[ -n $model_root ]] || fail '必须指定 --model-root 本地模型目录'
[[ -d $model_root ]] || fail "模型目录不存在: $model_root"
[[ -d $install_root ]] || fail "安装目录不存在: $install_root"
[[ -f $camera_config ]] || fail "相机配置不存在: $camera_config"
[[ -f $cpu_policy ]] || fail "CPU 策略不存在: $cpu_policy"
model_root=$(cd -- "$model_root" && pwd -P)
install_root=$(cd -- "$install_root" && pwd -P)
camera_config=$(cd -- "$(dirname -- "$camera_config")" && pwd -P)/$(basename -- "$camera_config")
cpu_policy=$(cd -- "$(dirname -- "$cpu_policy")" && pwd -P)/$(basename -- "$cpu_policy")
command -v docker >/dev/null || fail '未找到 Docker'
docker info >/dev/null || fail 'Docker 未启动'
image_id=$(docker image inspect "$image" --format '{{.Id}}') || fail "镜像不存在: $image"
mkdir -p -- "$output"
output=$(cd -- "$output" && pwd -P)
git_commit=$(git -C "$repo_dir" rev-parse HEAD)
if [[ -n $(git -C "$repo_dir" status --porcelain --untracked-files=normal) ]]; then
    git_dirty=true
else
    git_dirty=false
fi
docker run --rm --network none --platform linux/arm64 \
    --mount "type=bind,src=$repo_dir/deployment,dst=/packager,readonly" \
    --mount "type=bind,src=$camera_config,dst=/inputs/camera.yaml,readonly" \
    --mount "type=bind,src=$cpu_policy,dst=/inputs/cpu-affinity.yaml,readonly" \
    --mount "type=bind,src=$install_root,dst=/inputs/install,readonly" \
    --mount "type=bind,src=$model_root,dst=/inputs/models,readonly" \
    --mount "type=bind,src=$output,dst=/output" \
    --env "PACKAGE_CAMERA_SOURCE=$camera_config" \
    --env "PACKAGE_POLICY_SOURCE=$cpu_policy" \
    --env "PACKAGE_INSTALL_SOURCE=$install_root" \
    --env "PACKAGE_MODEL_SOURCE=$model_root" \
    --env "PACKAGE_OUTPUT_SOURCE=$output" \
    --env "PACKAGE_GIT_COMMIT=$git_commit" \
    --env "PACKAGE_GIT_DIRTY=$git_dirty" \
    --env "PACKAGE_IMAGE=$image" \
    --env "PACKAGE_IMAGE_ID=$image_id" \
    --env "PACKAGE_WITHOUT_MODELS=$without_models" \
    "$image" bash -lc 'source /opt/ros/humble/setup.bash; exec python3 /packager/package_core.py'

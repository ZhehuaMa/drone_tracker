#!/usr/bin/env bash
# 构建 RK3576 正式发布/日常编译共用的 Docker 镜像。
# 镜像固定使用 RKNN Runtime 2.3.2（与生产开发板系统库位级一致），
# 以及与生产板一致的 ROS 2 Humble、GCC 11.4.0、CMake 3.22.1 工具链。
set -euo pipefail

usage() {
  cat <<'EOF'
用法：./build-image.sh [--tag <镜像名:标签>] [--help]

默认镜像标签：drone-tracker-rk3576:humble-arm64-rknn2.3.2

构建前会先执行 scripts/download-firefly-media-debs.sh 补齐 vendor/ 下的
librga、mpp 依赖包（若已存在且哈希匹配则跳过下载）。
EOF
}

image_tag="drone-tracker-rk3576:humble-arm64-rknn2.3.2"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag) image_tag="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) printf '未知参数：%s\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
done

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
vendor_dir="$script_dir/vendor/firefly-jammy-arm64"
checksum_file="$script_dir/checksums/firefly-media-jammy-arm64.sha256"

need_download=false
if [[ ! -f "$vendor_dir/librga-dev_2.2.0-1_arm64.deb" ]]; then
  need_download=true
elif ! (cd "$vendor_dir" && sha256sum --check "$checksum_file" >/dev/null 2>&1); then
  need_download=true
fi
if "$need_download"; then
  "$script_dir/scripts/download-firefly-media-debs.sh"
fi

command -v docker >/dev/null || { printf '未找到 docker 命令。\n' >&2; exit 1; }
docker info >/dev/null

docker build --platform linux/arm64 \
  -f "$script_dir/Dockerfile.rk3576-humble-arm64" \
  -t "$image_tag" \
  "$script_dir"

printf '镜像构建完成：%s\n' "$image_tag"

#!/usr/bin/env bash
# 在现有 ARM64 镜像中构建 RK3576 版本，不下载或安装依赖。
set -euo pipefail

usage() {
  cat <<'EOF'
用法：./build.sh [--clean] [--test] [--help]

默认：增量编译 RK3576 Release 版本。
  --clean  将旧 build、install、log 目录移入时间戳备份后全新编译。
  --test   编译测试程序，并在构建成功后运行 CTest。
  --help   显示帮助。

需要启动 Docker，并提前用 dockerfiles/build-image.sh 构建镜像
drone-tracker-rk3576:humble-arm64-rknn2.3.2（RKNN Runtime 2.3.2，
与生产开发板系统库位级一致）。
容器关闭网络，不安装依赖；退出后自动删除。
产物保存在仓库的 .docker-artifacts/install/，日志在 .docker-artifacts/log/。
本镜像也用于正式发布产物的编译；容器构建通过仍不代表 RK3576 实机
相机、模型或持续负载验证通过。
EOF
}

clean=false
build_tests=OFF
for argument in "$@"; do
  case "$argument" in
    --clean) clean=true ;;
    --test) build_tests=ON ;;
    --help|-h) usage; exit 0 ;;
    *) printf '未知参数：%s\n' "$argument" >&2; usage >&2; exit 2 ;;
  esac
done

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
artifacts_dir="$repo_dir/.docker-artifacts"
build_image=drone-tracker-rk3576:humble-arm64-rknn2.3.2
# 固定名称也用于阻止本脚本并发使用同一套构建目录。
container_name=drone-tracker-repository-build

command -v docker >/dev/null || { printf '未找到 docker 命令。\n' >&2; exit 1; }
docker info >/dev/null
docker image inspect "$build_image" >/dev/null 2>&1 \
  || { printf '未找到镜像 %s，请先执行 dockerfiles/build-image.sh 构建。\n' "$build_image" >&2; exit 1; }
if docker container inspect "$container_name" >/dev/null 2>&1; then
  printf '容器 %s 已存在，请先确认上一轮构建已结束并清理该容器。\n' "$container_name" >&2
  exit 1
fi

if "$clean"; then
  mkdir -p "$artifacts_dir"
  backup_dir="$(mktemp -d "$artifacts_dir/backup-$(date +%Y%m%d-%H%M%S)-XXXXXX")"
  for directory in build install log; do
    if [[ -d "$artifacts_dir/$directory" ]]; then
      mv "$artifacts_dir/$directory" "$backup_dir/"
    fi
  done
  printf '旧产物备份：%s\n' "$backup_dir"
fi
mkdir -p "$artifacts_dir"/{build,install,log}

docker run --rm --network none --platform linux/arm64 \
  --name "$container_name" \
  --mount "type=bind,src=$repo_dir,dst=/work/ros2_ws/src/drone_tracker" \
  --mount "type=bind,src=$artifacts_dir/build,dst=/work/ros2_ws/build" \
  --mount "type=bind,src=$artifacts_dir/install,dst=/work/ros2_ws/install" \
  --mount "type=bind,src=$artifacts_dir/log,dst=/work/ros2_ws/log" \
  --workdir /work/ros2_ws \
  --env "DRONE_BUILD_TESTS=$build_tests" \
  "$build_image" bash -lc '
    set -eo pipefail
    source /opt/ros/humble/setup.bash
    colcon build --packages-select drone_tracker --cmake-args \
      -DCMAKE_BUILD_TYPE=Release \
      -DDRONE_TRACKER_PLATFORM=rk3576 \
      -DDRONE_TRACKER_BUILD_TESTS="$DRONE_BUILD_TESTS" \
      -DDRONE_TRACKER_BUILD_OSD=ON \
      -DDRONE_TRACKER_BUILD_RECORDING=ON \
      2>&1 | tee log/build-console.log

    if [[ "$DRONE_BUILD_TESTS" == ON ]]; then
      export LD_LIBRARY_PATH=/opt/rknn-runtime/lib:${LD_LIBRARY_PATH:-}
      source install/setup.bash
      ctest --test-dir build/drone_tracker --output-on-failure \
        2>&1 | tee log/test-console.log
    fi
  '

printf '构建成功，安装目录：%s/install\n' "$artifacts_dir"

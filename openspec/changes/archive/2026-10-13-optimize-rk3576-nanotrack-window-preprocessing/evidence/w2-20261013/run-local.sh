#!/usr/bin/env bash
# 从仓库根目录运行；复用已有 ARM64 镜像与独立诊断缓存，不安装依赖。
set -euo pipefail
repo_root="$(pwd)"
git_metadata="$(git rev-parse --path-format=absolute --git-common-dir)"
docker run --rm --network none --platform linux/arm64 --name drone-tracker-w2-build \
  --mount "type=bind,src=$repo_root,dst=/work/source" \
  --mount "type=bind,src=$repo_root/.docker-artifacts/standalone,dst=/work/build" \
  --mount "type=bind,src=$git_metadata,dst=$git_metadata,readonly" \
  --workdir /work/source drone-tracker-rk3576:humble-arm64-rknn2.3.2 bash -lc '
    set -e
    source /opt/ros/humble/setup.bash
    for tool in clang-tidy clangd; do
      command -v "$tool" || printf "%s：ARM64 容器未提供；仓库未配置检查命令\n" "$tool"
    done
    cmake -S tests/nanotracev3_performance_benchmarks/window_preprocess \
      -B /work/build -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON
    cmake --build /work/build -j4 --target nanotrack_window_recovery nanotrack_window_tests \
      nanotrack_window_performance_tests recovery_regression_tests baseline_nanotrack_preprocess_tests
    LD_LIBRARY_PATH=/opt/rknn-runtime/lib:${LD_LIBRARY_PATH:-} ctest --test-dir /work/build -V \
      -R "^(nanotrack_window_.*|recovery_regression_tests|baseline_nanotrack_preprocess_tests)$"
  '

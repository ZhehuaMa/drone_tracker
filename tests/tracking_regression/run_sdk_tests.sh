#!/usr/bin/env bash
# Run the complete optional RK3576 build/tests with private test-fixture CLIs.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
image=drone-tracker-rk3576:humble-arm64-rknn2.3.2
regex=
case "${1:-}" in
  "") ;;
  --regression-only) regex=tracking_regression ;;
  *) printf 'Usage: bash tests/tracking_regression/run_sdk_tests.sh [--regression-only]\n' >&2; exit 2 ;;
esac
[[ $# -le 1 ]] || { printf 'Only one optional argument is supported.\n' >&2; exit 2; }
tools="$repo/.docker-artifacts/tracking-regression/test-tools/ffmpeg-4.4.2/bin"
[[ -x "$tools/ffmpeg" && -x "$tools/ffprobe" ]] || {
  printf 'Prepare private tools first: bash tests/tracking_regression/prepare_test_tools.sh\n' >&2
  exit 1
}
mkdir -p "$repo/.docker-artifacts/"{build,install,log}
# Managed worktrees contain an absolute .git pointer. Mount its referenced metadata
# read-only, so code provenance generation works without copying or rewriting Git.
git_dir=$(git -C "$repo" rev-parse --path-format=absolute --git-common-dir)
container_tools=/work/ros2_ws/src/drone_tracker/.docker-artifacts/tracking-regression/test-tools/ffmpeg-4.4.2/bin
docker run --rm --network none --platform linux/arm64 \
  --name drone-tracker-tracking-regression-build \
  --mount "type=bind,src=$repo,dst=/work/ros2_ws/src/drone_tracker" \
  --mount "type=bind,src=$repo/.docker-artifacts/build,dst=/work/ros2_ws/build" \
  --mount "type=bind,src=$repo/.docker-artifacts/install,dst=/work/ros2_ws/install" \
  --mount "type=bind,src=$repo/.docker-artifacts/log,dst=/work/ros2_ws/log" \
  --mount "type=bind,src=$git_dir,dst=$git_dir,readonly" \
  --env "TRACKING_TEST_TOOLS=$container_tools" \
  --env "TRACKING_CTEST_REGEX=$regex" \
  --env GIT_CONFIG_COUNT=1 --env GIT_CONFIG_KEY_0=safe.directory \
  --env GIT_CONFIG_VALUE_0=/work/ros2_ws/src/drone_tracker \
  --workdir /work/ros2_ws "$image" bash -lc '
    set -eo pipefail
    source /opt/ros/humble/setup.bash
    export PATH="$TRACKING_TEST_TOOLS:$PATH"
    colcon build --packages-select drone_tracker --cmake-args \
      -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_PLATFORM=rk3576 \
      -DDRONE_TRACKER_BUILD_TESTS=ON -DDRONE_TRACKER_BUILD_OSD=ON \
      -DDRONE_TRACKER_BUILD_RECORDING=ON \
      -DTRACKING_REGRESSION_FFMPEG="$TRACKING_TEST_TOOLS/ffmpeg" \
      -DTRACKING_REGRESSION_FFPROBE="$TRACKING_TEST_TOOLS/ffprobe" \
      2>&1 | tee log/tracking-regression-build.log
    source install/setup.bash
    export LD_LIBRARY_PATH=/opt/rknn-runtime/lib:${LD_LIBRARY_PATH:-}
    arguments=()
    if [[ -n "$TRACKING_CTEST_REGEX" ]]; then
      arguments+=(--tests-regex "$TRACKING_CTEST_REGEX")
    fi
    ctest --test-dir build/drone_tracker --output-on-failure "${arguments[@]}" \
      2>&1 | tee log/tracking-regression-tests.log
  '

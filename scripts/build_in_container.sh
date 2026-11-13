#!/usr/bin/env bash
# Container-side build runner. Native compiler processes retain native ROS paths.
set -eo pipefail
source /opt/ros/humble/setup.bash
mkdir -p "$HOME" /work/ros2_ws/log/ros
export ROS_LOG_DIR=/work/ros2_ws/log/ros
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
export QEMU_CPU=cortex-a72
# Randomize every ROS test invocation; localhost isolation also prevents network discovery.
export ROS_DOMAIN_ID=$(( $(od -An -N2 -tu2 /dev/urandom) % 200 + 1 ))
export ROS_LOCALHOST_ONLY=1
printf 'ROS_DOMAIN_ID=%s\n' "$ROS_DOMAIN_ID" > /work/ros2_ws/log/ros-domain.txt
export CMAKE_BUILD_PARALLEL_LEVEL="$DRONE_BUILD_JOBS"

cmake_arguments=(-DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_PLATFORM=rk3576
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  "-DDRONE_TRACKER_BUILD_TESTS=$DRONE_BUILD_TESTS" -DDRONE_TRACKER_BUILD_OSD=ON
  -DDRONE_TRACKER_BUILD_RECORDING=ON
  -Dquadrotor_msgs_DIR=/work/ros2_ws/install/quadrotor_msgs/share/quadrotor_msgs/cmake)
if [[ "$DRONE_BUILD_BACKEND" == amd64 ]]; then
  test -f /opt/toolchains/aarch64-humble.cmake || { printf 'The amd64 image lacks the ARM64 toolchain. Rebuild dockerfiles/build-image-amd64.sh.\n' >&2; exit 1; }
  export DRONE_QEMU_AARCH64="${DRONE_QEMU_AARCH64:-/opt/qemu-arm64/bin/qemu-aarch64}"
  test -x "$DRONE_QEMU_AARCH64" || { printf 'Selected ARM emulator is missing: %s. Prepare and rebuild the amd64 image.\n' "$DRONE_QEMU_AARCH64" >&2; exit 1; }
  export QEMU_LD_PREFIX=/opt/arm64-sysroot
  cmake_arguments+=(-DCMAKE_TOOLCHAIN_FILE=/opt/toolchains/aarch64-humble.cmake)
fi

if [[ "$DRONE_BUILD_TESTS" == ON ]]; then
  if [[ "$DRONE_BUILD_BACKEND" == arm64 && -x "$DRONE_PRIVATE_TOOLS/ffmpeg" && -x "$DRONE_PRIVATE_TOOLS/ffprobe" ]]; then
    fixture_ffmpeg="$DRONE_PRIVATE_TOOLS/ffmpeg"
    fixture_ffprobe="$DRONE_PRIVATE_TOOLS/ffprobe"
  else
    fixture_ffmpeg="$(command -v ffmpeg || true)"
    fixture_ffprobe="$(command -v ffprobe || true)"
  fi
  for tool in "$fixture_ffmpeg" "$fixture_ffprobe"; do
    if [[ -z "$tool" ]] || ! "$tool" -version >/dev/null 2>&1; then
      printf 'FFmpeg fixture tools are missing or unusable. Before building run:\n  %s\n' "$DRONE_PREPARE_TOOLS_COMMAND" >&2
      exit 1
    fi
  done
  cmake_arguments+=("-DTRACKING_REGRESSION_FFMPEG=$fixture_ffmpeg" "-DTRACKING_REGRESSION_FFPROBE=$fixture_ffprobe")
  if [[ "$DRONE_BUILD_BACKEND" == amd64 ]]; then
    test -x /opt/arm64-sysroot/usr/bin/python3 || { printf 'The amd64 image lacks target Python. Rebuild the image before --test.\n' >&2; exit 1; }
    cmake_arguments+=("-DTRACKING_REGRESSION_PYTHON_LAUNCHER=$DRONE_SOURCE_DIR/tests/tracking_regression/target_python.sh")
  fi
fi

printf 'backend=%s\ntarget=arm64\nimage=%s\n' "$DRONE_BUILD_BACKEND" "$DRONE_BUILD_IMAGE_ID" > log/build-environment.txt
colcon --log-base log/colcon build --base-paths "$DRONE_QUADROTOR_SOURCE" "$DRONE_SOURCE_DIR" \
  --packages-up-to drone_tracker \
  --cmake-args "${cmake_arguments[@]}" 2>&1 | tee log/build-console.log

binary=install/drone_tracker/lib/drone_tracker/drone_tracker_node
file "$binary" | tee log/output-architecture.log
readelf -h "$binary" | grep -q 'Machine:.*AArch64' || { printf 'Refusing non-ARM64 deployment output.\n' >&2; exit 1; }

if [[ "$DRONE_BUILD_TESTS" == ON ]]; then
  if [[ "$DRONE_BUILD_BACKEND" == amd64 ]]; then
    # QEMU cannot enforce guest RLIMIT_AS. Retain the real OS-limit assertion
    # in a separate native executable built from the same source test harness.
    cmake -S "$DRONE_SOURCE_DIR/tests/tracking_regression/native_resource_limit" \
      -B build/native-resource-limit -DCMAKE_BUILD_TYPE=Release \
      "-DTRACKING_REGRESSION_FFMPEG=$fixture_ffmpeg" 2>&1 | tee log/native-resource-configure.log
    cmake --build build/native-resource-limit -j"$DRONE_BUILD_JOBS" 2>&1 | tee log/native-resource-build.log
    ctest --test-dir build/native-resource-limit --output-on-failure 2>&1 | tee log/native-resource-test.log
    export AMENT_PREFIX_PATH=/work/ros2_ws/install/drone_tracker:/opt/arm64-sysroot/opt/ros/humble
    export QEMU_SET_ENV=LD_LIBRARY_PATH=/work/ros2_ws/install/quadrotor_msgs/lib:/opt/arm64-sysroot/opt/ros/humble/lib:/opt/arm64-sysroot/opt/rknn-runtime/lib:/opt/arm64-sysroot/usr/lib/aarch64-linux-gnu
  else
    source install/setup.bash
    export LD_LIBRARY_PATH=/opt/rknn-runtime/lib:${LD_LIBRARY_PATH:-}
    if [[ "$DRONE_HOST_ARCH" != aarch64 && "$DRONE_HOST_ARCH" != arm64 ]]; then
      printf 'ARM64 fallback is emulated on %s. Its real OS resource-limit assertion remains enabled; ignored guest RLIMIT_AS is a failed test, not native OS coverage.\n' "$DRONE_HOST_ARCH"
    fi
  fi
  arguments=()
  [[ -z "$DRONE_TEST_REGEX" ]] || arguments+=(--tests-regex "$DRONE_TEST_REGEX")
  ctest --test-dir build/drone_tracker --output-on-failure "${arguments[@]}" 2>&1 | tee log/test-console.log
fi

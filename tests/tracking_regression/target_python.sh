#!/usr/bin/env bash
# Only this process and its children use target Python paths and ARM libraries.
set -euo pipefail
root=/opt/arm64-sysroot
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export QEMU_LD_PREFIX="$root"
export QEMU_CPU=cortex-a72
export QEMU_SET_ENV="LD_LIBRARY_PATH=$root/opt/ros/humble/lib:$root/opt/rknn-runtime/lib:$root/usr/lib/aarch64-linux-gnu"
export PYTHONHOME="$root/usr"
export PYTHONPATH="$root/opt/ros/humble/local/lib/python3.10/dist-packages:$root/opt/ros/humble/lib/python3.10/site-packages"
export AMENT_PREFIX_PATH="/work/ros2_ws/install/drone_tracker:$root/opt/ros/humble"
export PATH="$script_dir/target_tools:$PATH"
export PYTHONDONTWRITEBYTECODE=1
exec "${DRONE_QEMU_AARCH64:-/opt/qemu-arm64/bin/qemu-aarch64}" -L "$root" "$root/usr/bin/python3" "$@"

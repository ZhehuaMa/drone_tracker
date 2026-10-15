#!/usr/bin/env bash
# Build native tools and preserve the existing ARM64 image as an SDK source.
set -euo pipefail

image_tag="drone-tracker-rk3576:humble-amd64-rknn2.3.2"
sdk_image="drone-tracker-rk3576:humble-arm64-rknn2.3.2"
native_base="ros:humble-ros-base-jammy"
build_network="default"
native_deps_preinstalled=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag) image_tag="${2:?--tag requires an image tag}"; shift 2 ;;
    --sdk-image) sdk_image="${2:?--sdk-image requires an ARM64 image}"; shift 2 ;;
    --base-image) native_base="${2:?--base-image requires a native base image}"; shift 2 ;;
    --network) build_network="${2:?--network requires a Docker network mode}"; shift 2 ;;
    --offline-native-deps) native_deps_preinstalled=1; shift ;;
    --help|-h)
      printf 'Usage: %s [--tag IMAGE] [--sdk-image ARM64_IMAGE] [--base-image AMD64_IMAGE] [--network MODE] [--offline-native-deps]\n' "$0"
      exit 0 ;;
    *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
  esac
done
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
python3 "$script_dir/scripts/verify-patched-qemu.py" "$script_dir/vendor/qemu-user" \
  "$script_dir/patches/qemu-8.2.2-close-truncated-rights.patch"
sdk_arch="$(docker image inspect "$sdk_image" --format '{{.Architecture}}')"
[[ "$sdk_arch" == arm64 ]] || { printf 'SDK image must be arm64, got %s\n' "$sdk_arch" >&2; exit 1; }
docker build --platform linux/amd64 --network "$build_network" \
  --build-arg "ARM64_SDK_IMAGE=$sdk_image" \
  --build-arg "NATIVE_BASE_IMAGE=$native_base" \
  --build-arg "NATIVE_DEPS_PREINSTALLED=$native_deps_preinstalled" \
  -f "$script_dir/Dockerfile.rk3576-humble-amd64" \
  -t "$image_tag" "$script_dir"
docker image inspect "$image_tag" --format '{{.Id}} {{.Os}}/{{.Architecture}}'

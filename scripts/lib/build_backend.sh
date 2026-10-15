#!/usr/bin/env bash
# Shared host-side policy for the build and test entry points.

drone_build_usage() {
  cat <<'EOF'
Usage: ./build.sh [--backend amd64|arm64] [--image IMAGE] [--test]
                  [--regression-only] [--clean] [--artifacts-dir ROOT] [--jobs N]

Default: linux/amd64 compiler image, RK3576 Release, ARM64 deployment output.
  --backend arm64   Explicitly use the existing ARM64 compiler image.
  --image IMAGE     Override the image; its architecture must match --backend.
  --test            Build and run the complete existing CTest suite.
  --regression-only Build tests, then run the tracking_regression CTest subset.
  --clean           Back up only this backend's build/install/log directories.
  --artifacts-dir   Artifact root (default: REPOSITORY/.docker-artifacts).
  --jobs N          Parallel compiler jobs (default: 6).
  --help            Show this help.

Caches: ROOT/backends/BACKEND/{build,install,log}; legacy caches stay untouched.
Changing image or toolchain requires --clean; previous output is backed up.
Sources and Git metadata are read-only; routine containers have no network.
Missing ARM64 fixture tools: bash tests/tracking_regression/prepare_test_tools.sh --backend arm64

Production packaging must explicitly select the new install directory:
  bash deployment/package.sh /absolute/output-directory \
    --install-root /absolute/artifact-root/backends/BACKEND/install/drone_tracker \
    --model-root /absolute/model-directory
The legacy packaging default is not redirected to a different build silently.
Container tests do not establish NPU, camera, HDMI, timing, or flight acceptance.
EOF
}

drone_build_defaults() {
  DRONE_REPO_DIR="$(cd -- "$1" && pwd -P)"
  DRONE_BACKEND=amd64
  DRONE_IMAGE=
  DRONE_ARTIFACT_ROOT="$DRONE_REPO_DIR/.docker-artifacts"
  DRONE_CLEAN=false
  DRONE_BUILD_TESTS=OFF
  DRONE_TEST_REGEX=
  DRONE_BUILD_JOBS=6
}

drone_parse_build_options() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --backend|--image|--artifacts-dir|--jobs)
        [[ $# -ge 2 && -n "$2" && "$2" != --* ]] || { printf '%s requires a value.\n' "$1" >&2; return 2; }
        case "$1" in
          --backend) DRONE_BACKEND=$2 ;;
          --image) DRONE_IMAGE=$2 ;;
          --artifacts-dir) DRONE_ARTIFACT_ROOT=$2 ;;
          --jobs) DRONE_BUILD_JOBS=$2 ;;
        esac
        shift 2 ;;
      --clean) DRONE_CLEAN=true; shift ;;
      --test) DRONE_BUILD_TESTS=ON; shift ;;
      --regression-only) DRONE_BUILD_TESTS=ON; DRONE_TEST_REGEX=tracking_regression; shift ;;
      --help|-h) drone_build_usage; exit 0 ;;
      *) printf 'Unknown argument: %s\n' "$1" >&2; return 2 ;;
    esac
  done
  [[ "$DRONE_BACKEND" == amd64 || "$DRONE_BACKEND" == arm64 ]] || { printf 'Unsupported backend: %s (use amd64 or arm64).\n' "$DRONE_BACKEND" >&2; return 2; }
  [[ "$DRONE_BUILD_JOBS" =~ ^[1-9][0-9]*$ ]] || { printf '--jobs must be a positive integer.\n' >&2; return 2; }
  if [[ -z "$DRONE_IMAGE" ]]; then
    DRONE_IMAGE="drone-tracker-rk3576:humble-$DRONE_BACKEND-rknn2.3.2"
  fi
  mkdir -p "$DRONE_ARTIFACT_ROOT"
  DRONE_ARTIFACT_ROOT="$(cd -- "$DRONE_ARTIFACT_ROOT" && pwd -P)"
  DRONE_BACKEND_DIR="$DRONE_ARTIFACT_ROOT/backends/$DRONE_BACKEND"
}

drone_check_build_image() {
  command -v docker >/dev/null || { printf 'Docker is required.\n' >&2; return 1; }
  docker info >/dev/null
  local image_platform
  image_platform="$(docker image inspect "$DRONE_IMAGE" --format '{{.Os}}/{{.Architecture}}' 2>/dev/null)" || {
    printf 'Image %s is missing. Prepare dockerfiles/build-image%s.sh first.\n' "$DRONE_IMAGE" "$([[ "$DRONE_BACKEND" == amd64 ]] && printf '%s' '-amd64')" >&2
    return 1
  }
  [[ "$image_platform" == "linux/$DRONE_BACKEND" ]] || {
    printf 'Image %s is %s; --backend %s requires linux/%s. No fallback attempted.\n' "$DRONE_IMAGE" "$image_platform" "$DRONE_BACKEND" "$DRONE_BACKEND" >&2
    return 1
  }
  DRONE_IMAGE_ID="$(docker image inspect "$DRONE_IMAGE" --format '{{.Id}}')"
}

drone_run_repository_build() {
  local cache_key container_name lock_dir git_dir common_dir metadata backup_dir directory prepare_command expected_environment environment_file
  cache_key="$(printf '%s' "$DRONE_BACKEND_DIR" | cksum | cut -d ' ' -f 1)"
  container_name="drone-tracker-$DRONE_BACKEND-$cache_key"
  mkdir -p "$DRONE_ARTIFACT_ROOT/locks" "$DRONE_BACKEND_DIR"
  lock_dir="$DRONE_ARTIFACT_ROOT/locks/$DRONE_BACKEND-$cache_key"
  if ! mkdir "$lock_dir" 2>/dev/null; then
    printf 'Cache is locked: %s (owner PID: %s). Check the active build before removing a stale lock.\n' "$lock_dir" "$(cat "$lock_dir/pid" 2>/dev/null || printf unknown)" >&2
    return 1
  fi
  printf '%s\n' "$$" > "$lock_dir/pid"
  DRONE_LOCK_DIR=$lock_dir
  trap 'rm -f -- "$DRONE_LOCK_DIR/pid"; rmdir -- "$DRONE_LOCK_DIR"' EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  if docker container inspect "$container_name" >/dev/null 2>&1; then
    printf 'Build container %s already exists; inspect it before retrying.\n' "$container_name" >&2
    return 1
  fi
  environment_file="$DRONE_BACKEND_DIR/build-environment.txt"
  expected_environment="$(printf 'backend=%s\ntarget=arm64\nimage=%s\ntoolchain=%s\n' \
    "$DRONE_BACKEND" "$DRONE_IMAGE_ID" \
    "$(sha256sum "$DRONE_REPO_DIR/dockerfiles/toolchains/aarch64-humble.cmake" | cut -d ' ' -f 1)")"
  if ! "$DRONE_CLEAN"; then
    if [[ -f "$environment_file" ]]; then
      if [[ "$(cat "$environment_file")" != "$expected_environment" ]]; then
        printf 'Cached build uses a different image or toolchain: %s. Rerun with --clean to back it up before configuring.\n' "$environment_file" >&2
        return 1
      fi
    elif [[ -d "$DRONE_BACKEND_DIR/build" && -n "$(ls -A "$DRONE_BACKEND_DIR/build")" ]]; then
      printf 'Cached build has no verified image/toolchain identity: %s. Rerun with --clean to preserve it and configure afresh.\n' "$DRONE_BACKEND_DIR/build" >&2
      return 1
    fi
  fi
  if "$DRONE_CLEAN"; then
    mkdir -p "$DRONE_ARTIFACT_ROOT/backups/$DRONE_BACKEND"
    backup_dir="$(mktemp -d "$DRONE_ARTIFACT_ROOT/backups/$DRONE_BACKEND/$(date +%Y%m%d-%H%M%S)-XXXXXX")"
    for directory in build install log build-environment.txt; do
      [[ ! -e "$DRONE_BACKEND_DIR/$directory" ]] || mv -- "$DRONE_BACKEND_DIR/$directory" "$backup_dir/"
    done
    printf 'Previous %s artifacts preserved in %s\n' "$DRONE_BACKEND" "$backup_dir"
  fi
  mkdir -p "$DRONE_BACKEND_DIR"/{build,install,log}
  printf '%s\n' "$expected_environment" > "$environment_file"
  local mounts=(--mount "type=bind,src=$DRONE_REPO_DIR,dst=$DRONE_REPO_DIR,readonly"
    --mount "type=bind,src=$DRONE_BACKEND_DIR/build,dst=/work/ros2_ws/build"
    --mount "type=bind,src=$DRONE_BACKEND_DIR/install,dst=/work/ros2_ws/install"
    --mount "type=bind,src=$DRONE_BACKEND_DIR/log,dst=/work/ros2_ws/log")
  git_dir="$(git -C "$DRONE_REPO_DIR" rev-parse --absolute-git-dir)"
  common_dir="$(git -C "$DRONE_REPO_DIR" rev-parse --path-format=absolute --git-common-dir)"
  for metadata in "$git_dir" "$common_dir"; do
    if [[ "$metadata" != "$DRONE_REPO_DIR/"* ]]; then
      mounts+=(--mount "type=bind,src=$metadata,dst=$metadata,readonly")
    fi
    [[ "$git_dir" != "$common_dir" ]] || break
  done
  local private_tools="$DRONE_ARTIFACT_ROOT/tracking-regression/test-tools/$DRONE_BACKEND/ffmpeg-4.4.2/bin"
  if [[ "$DRONE_BACKEND" == arm64 && ! -x "$private_tools/ffmpeg" ]]; then
    private_tools="$DRONE_REPO_DIR/.docker-artifacts/tracking-regression/test-tools/ffmpeg-4.4.2/bin"
  fi
  if [[ -d "$private_tools" && "$private_tools" != "$DRONE_REPO_DIR/"* ]]; then
    mounts+=(--mount "type=bind,src=$private_tools,dst=$private_tools,readonly")
  fi
  prepare_command="$(printf '%q ' bash "$DRONE_REPO_DIR/tests/tracking_regression/prepare_test_tools.sh" \
    --backend "$DRONE_BACKEND" --image "$DRONE_IMAGE_ID" --artifacts-dir "$DRONE_ARTIFACT_ROOT")"
  printf 'Backend: %s; target: ARM64; image: %s\nInstall: %s/install/drone_tracker\nLogs: %s/log\n' "$DRONE_BACKEND" "$DRONE_IMAGE_ID" "$DRONE_BACKEND_DIR" "$DRONE_BACKEND_DIR"
  docker run --rm --network none --platform "linux/$DRONE_BACKEND" \
    --name "$container_name" --user "$(id -u):$(id -g)" "${mounts[@]}" \
    --workdir /work/ros2_ws --env HOME=/tmp/drone-build-home \
    --env "DRONE_SOURCE_DIR=$DRONE_REPO_DIR" --env "DRONE_BUILD_BACKEND=$DRONE_BACKEND" \
    --env "DRONE_BUILD_TESTS=$DRONE_BUILD_TESTS" --env "DRONE_TEST_REGEX=$DRONE_TEST_REGEX" \
    --env "DRONE_BUILD_JOBS=$DRONE_BUILD_JOBS" --env "DRONE_PRIVATE_TOOLS=$private_tools" \
    --env "DRONE_PREPARE_TOOLS_COMMAND=$prepare_command" \
    --env "DRONE_BUILD_IMAGE_ID=$DRONE_IMAGE_ID" --env PYTHONDONTWRITEBYTECODE=1 \
    --env "DRONE_HOST_ARCH=$(uname -m)" \
    --env GIT_CONFIG_COUNT=1 --env GIT_CONFIG_KEY_0=safe.directory --env "GIT_CONFIG_VALUE_0=$DRONE_REPO_DIR" \
    "$DRONE_IMAGE_ID" bash -c '
      set -e
      cp -- "$DRONE_SOURCE_DIR/scripts/build_in_container.sh" /tmp/drone-build-runner.sh
      cp -- /tmp/drone-build-runner.sh /work/ros2_ws/log/build-runner.sh
      exec bash /tmp/drone-build-runner.sh
    '
  printf 'Build succeeded. ARM64 install: %s/install/drone_tracker\n' "$DRONE_BACKEND_DIR"
  printf 'For packaging pass --install-root %s/install/drone_tracker explicitly.\n' "$DRONE_BACKEND_DIR"
}

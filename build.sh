#!/usr/bin/env bash
# Native x86_64 compilers produce ARM64 deployment binaries by default.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$repo_dir/scripts/lib/build_backend.sh"
drone_build_defaults "$repo_dir"
drone_parse_build_options "$@"
drone_check_build_image
drone_run_repository_build

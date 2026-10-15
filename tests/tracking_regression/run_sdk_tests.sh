#!/usr/bin/env bash
# Keep the established SDK-test command on the same backend/cache policy as build.sh.
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
exec bash "$repo/build.sh" --test "$@"

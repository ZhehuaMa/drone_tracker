#!/usr/bin/env bash
# Explicit environment preparation; routine build/test entry points stay offline.
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
docker_dir="$(dirname -- "$script_dir")"
source_url=https://archive.ubuntu.com/ubuntu/pool/main/q/qemu/qemu_8.2.2+ds.orig.tar.xz
source_sha=5e692dd20950a819ff6550cd3974e00319a93f47c0a15f808c58df1a9e4d2478
archive=
work_dir=
output_dir="$docker_dir/vendor/qemu-user"
python_tools=
jobs=6
while [[ $# -gt 0 ]]; do
  case "$1" in
    --source-archive) archive="${2:?--source-archive requires a path}"; shift 2 ;;
    --work-dir) work_dir="${2:?--work-dir requires a path}"; shift 2 ;;
    --output-dir) output_dir="${2:?--output-dir requires a path}"; shift 2 ;;
    --python-tools) python_tools="${2:?--python-tools requires a directory}"; shift 2 ;;
    --jobs) jobs="${2:?--jobs requires a positive integer}"; shift 2 ;;
    --help|-h)
      cat <<'HELP'
Usage: prepare-patched-qemu.sh --python-tools DIR [--source-archive PATH]
       [--work-dir EMPTY_DIR] [--output-dir DIR] [--jobs N]
Builds a static native x86_64 ARM emulator from checksum-pinned official source.
The external Python helper directory must contain meson==1.2.3,
ninja==1.11.1.1 and tomli==2.0.1. Install these explicitly during setup.
No host package installation occurs. Missing native static dependencies fail early.
Default output: dockerfiles/vendor/qemu-user (ignored generated binary/manifest).
Provide --source-archive for an entirely offline preparation run.
HELP
      exit 0 ;;
    *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
  esac
done
[[ "$(uname -s)/$(uname -m)" == Linux/x86_64 ]] || { printf 'Preparation requires a native Linux x86_64 host.\n' >&2; exit 1; }
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { printf '--jobs requires a positive integer.\n' >&2; exit 2; }
[[ -d "$python_tools" ]] || { printf 'Prepare an external Python helper directory and pass --python-tools DIR; see dockerfiles/README.amd64.md.\n' >&2; exit 1; }
python_tools="$(cd -- "$python_tools" && pwd -P)"
export PYTHONPATH="$python_tools" PYTHONDONTWRITEBYTECODE=1
python3 - <<'PY'
from importlib.metadata import version
for name, required in {'meson': '1.2.3', 'ninja': '1.11.1.1', 'tomli': '2.0.1'}.items():
    actual = version(name)
    if actual != required:
        raise SystemExit(f'{name} must be {required}, got {actual}')
PY
for command in cc c++ ar make pkg-config patch tar sha256sum readelf file; do
  command -v "$command" >/dev/null || { printf 'Missing native build tool: %s. See documented setup dependencies.\n' "$command" >&2; exit 1; }
done
if [[ -z "$work_dir" ]]; then work_dir="$(mktemp -d /tmp/nickluo-qemu-build-XXXXXX)"; fi
mkdir -p "$work_dir"
work_dir="$(cd -- "$work_dir" && pwd -P)"
[[ -z "$(ls -A "$work_dir")" ]] || { printf 'Work directory must be empty: %s\n' "$work_dir" >&2; exit 1; }
export XDG_CACHE_HOME="$work_dir/cache"
# Prove static GLib dependencies are linkable before a lengthy source build.
printf '#include <glib.h>\nint main(void) { g_free(g_malloc(1)); return 0; }\n' > "$work_dir/static-check.c"
if ! cc -static $(pkg-config --cflags glib-2.0 gthread-2.0) "$work_dir/static-check.c" \
  $(pkg-config --static --libs glib-2.0 gthread-2.0) -o "$work_dir/static-check"; then
  printf 'Static GLib link failed. Prepare build-essential, pkg-config, libglib2.0-dev, libpcre3-dev/libpcre2-dev and zlib1g-dev explicitly; no host packages were changed.\n' >&2
  exit 1
fi
if [[ -z "$archive" ]]; then
  archive="$work_dir/qemu_8.2.2+ds.orig.tar.xz"
  curl --fail --location --retry 3 "$source_url" --output "$archive"
else
  archive="$(realpath -- "$archive")"
fi
printf '%s  %s\n' "$source_sha" "$archive" | sha256sum -c -
patch_file="$docker_dir/patches/qemu-8.2.2-close-truncated-rights.patch"
test -f "$patch_file" || { printf 'Required tracked emulator patch is missing: %s\n' "$patch_file" >&2; exit 1; }
printf '%s  %s\n' a2838c7f61ec52a29a59e5622a2d69a488a7f5a58d761d0276188702a42ad4e9 "$patch_file" | sha256sum -c -
tar -xJf "$archive" -C "$work_dir"
source_dir="$work_dir/qemu-8.2.2"
patch --directory "$source_dir" --strip 1 --fuzz 0 < "$patch_file"
mkdir "$work_dir/build"
cd "$work_dir/build"
export PATH="$python_tools/ninja/data/bin:$python_tools/bin:/usr/bin:/bin"
export PIP_CACHE_DIR="$work_dir/pip-cache"
"$source_dir/configure" --target-list=aarch64-linux-user --static \
  --disable-system --disable-tools --disable-docs --disable-install-blobs \
  --disable-guest-agent --disable-plugins --disable-gcrypt --disable-nettle --disable-slirp \
  --disable-fdt --disable-capstone --disable-download --python="$(command -v python3)" \
  2>&1 | tee "$work_dir/configure.log"
"$python_tools/ninja/data/bin/ninja" -j "$jobs" qemu-aarch64 2>&1 | tee "$work_dir/build.log"
binary="$work_dir/build/qemu-aarch64"
file "$binary"
if readelf -l "$binary" | grep -q INTERP; then printf 'Refusing a dynamically linked emulator.\n' >&2; exit 1; fi
"$binary" --version
mkdir -p "$output_dir/bin"
output_dir="$(cd -- "$output_dir" && pwd -P)"
cp "$binary" "$output_dir/bin/qemu-aarch64"
chmod 755 "$output_dir/bin/qemu-aarch64"
python3 - "$output_dir" "$source_url" "$source_sha" "$patch_file" "$work_dir" <<'PY'
from hashlib import sha256
from importlib.metadata import version
import json
from pathlib import Path
import subprocess
import sys
out, url, source_hash, patch, work = sys.argv[1:]
output = Path(out)
def command(*arguments):
    return subprocess.check_output(arguments, text=True).strip()
manifest = {
    'qemu_version': '8.2.2', 'source_url': url, 'source_sha256': source_hash,
    'patch_sha256': sha256(Path(patch).read_bytes()).hexdigest(),
    'binary_sha256': sha256((output / 'bin/qemu-aarch64').read_bytes()).hexdigest(),
    'upstream_comparison_package_url': 'https://security.ubuntu.com/ubuntu/pool/universe/q/qemu/qemu-user-static_8.2.2+ds-0ubuntu1.16_amd64.deb',
    'upstream_comparison_package_sha256': 'a8f4e6121b7d1710885ba00fdb1e5a8256fb396f12ade2da9ed8a3693ad6d1bd',
    'compiler': command('cc', '--version'), 'pkg_config': command('pkg-config', '--version'),
    'glib_version': command('pkg-config', '--modversion', 'glib-2.0'),
    'glib_static_libraries': command('pkg-config', '--static', '--libs', 'glib-2.0', 'gthread-2.0'),
    'python_helpers': {name: version(name) for name in ('meson', 'ninja', 'tomli')},
    'required_host_packages': ['build-essential', 'pkg-config', 'libglib2.0-dev', 'libpcre3-dev', 'libpcre2-dev', 'zlib1g-dev', 'python3', 'python3-pip', 'python3-venv', 'patch', 'xz-utils'],
    'external_build_directory': work,
    'configure_options': ['--target-list=aarch64-linux-user', '--static', '--disable-system', '--disable-tools', '--disable-docs', '--disable-install-blobs', '--disable-guest-agent', '--disable-plugins', '--disable-gcrypt', '--disable-nettle', '--disable-slirp', '--disable-fdt', '--disable-capstone', '--disable-download'],
}
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(output / 'SHA256SUMS').write_text(manifest['binary_sha256'] + '  bin/qemu-aarch64\n')
PY
printf 'Prepared %s\nBuild evidence: %s\n' "$output_dir" "$work_dir"

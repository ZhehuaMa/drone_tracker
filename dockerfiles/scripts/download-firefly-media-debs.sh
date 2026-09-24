#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
dockerfiles_dir="$(cd -- "${script_dir}/.." && pwd)"
vendor_dir="${dockerfiles_dir}/vendor/firefly-jammy-arm64"
checksum_file="${dockerfiles_dir}/checksums/firefly-media-jammy-arm64.sha256"
base_url="http://wiki.t-firefly.com/firefly-ubuntu-repo"

mkdir -p "${vendor_dir}"

download() {
  local repo_path="$1"
  local filename="$2"

  curl --fail --location --retry 5 --retry-all-errors \
    "${base_url}/${repo_path}/${filename}" \
    --output "${vendor_dir}/${filename}"
}

download "pool/main/libr/librga" "librga-dev_2.2.0-1_arm64.deb"
download "pool/main/libr/librga" "librga2_2.2.0-1_arm64.deb"
download "pool/main/m/mpp" "librockchip-mpp-dev_1.5.0-1_arm64.deb"
download "pool/main/m/mpp" "librockchip-mpp1_1.5.0-1_arm64.deb"

cd "${vendor_dir}"
if command -v sha256sum >/dev/null 2>&1; then
  sha256sum --check "${checksum_file}"
else
  shasum -a 256 --check "${checksum_file}"
fi

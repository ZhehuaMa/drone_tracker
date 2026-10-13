#!/usr/bin/env bash
# Build private ARM64 FFmpeg CLIs without changing the SDK image or system libraries.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
image=drone-tracker-rk3576:humble-arm64-rknn2.3.2
cache="$repo/.docker-artifacts/tracking-regression/test-tools"
output="$cache/ffmpeg-4.4.2"
archive="$cache/ffmpeg-4.4.2.tar.xz"
expected=af419a7f88adbc56c758ab19b4c708afbcae15ef09606b82b855291f6a6faa93
mkdir -p "$cache"
exec > >(tee "$cache/prepare-test-tools.log") 2>&1
if [[ -x "$output/bin/ffmpeg" && -x "$output/bin/ffprobe" ]]; then
  printf 'Private tools already exist: %s\n' "$output/bin"
  exit 0
fi
[[ ! -e "$output" ]] || { printf 'Incomplete output exists; choose a new tool directory: %s\n' "$output" >&2; exit 1; }
if [[ ! -f "$archive" ]]; then
  curl --fail --location --output "$archive.download" https://ffmpeg.org/releases/ffmpeg-4.4.2.tar.xz
  mv "$archive.download" "$archive"
fi
actual=$(shasum -a 256 "$archive" | cut -d ' ' -f 1)
[[ "$actual" == "$expected" ]] || { printf 'FFmpeg source checksum mismatch: %s\n' "$archive" >&2; exit 1; }
stage=$(mktemp -d "$cache/build-XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
mkdir "$stage/source" "$stage/install"
tar -xJf "$archive" --strip-components=1 -C "$stage/source"
docker run --rm --network none --platform linux/arm64 \
  --mount "type=bind,src=$stage/source,dst=/work/ffmpeg" \
  --mount "type=bind,src=$stage/install,dst=/tooling/private-ffmpeg" \
  --workdir /work/ffmpeg "$image" bash -lc '
    set -eo pipefail
    ./configure --prefix=/tooling/private-ffmpeg --disable-autodetect --disable-everything \
      --enable-ffmpeg --enable-ffprobe --disable-ffplay --enable-protocol=file,pipe \
      --enable-demuxer=matroska,mov --enable-muxer=matroska,rawvideo \
      --enable-decoder=ffv1,h264,rawvideo --enable-encoder=ffv1,rawvideo \
      --enable-filter=extractplanes,color,format,scale --enable-indev=lavfi \
      --disable-doc --disable-debug --disable-shared --enable-static
    make -j4
    make install
    /tooling/private-ffmpeg/bin/ffmpeg -version
    /tooling/private-ffmpeg/bin/ffprobe -version
  '
printf '%s  ffmpeg-4.4.2.tar.xz\n' "$expected" > "$stage/install/source-sha256.txt"
mv "$stage/install" "$output"
printf 'Private ARM64 tools ready: %s\n' "$output/bin"

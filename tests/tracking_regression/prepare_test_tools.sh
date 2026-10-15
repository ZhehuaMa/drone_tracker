#!/usr/bin/env bash
# Explicit setup: amd64 validates image tools; ARM64 prepares private pinned CLIs.
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$repo/scripts/lib/build_backend.sh"
drone_build_defaults "$repo"
drone_parse_build_options "$@"
drone_check_build_image
cache="$DRONE_ARTIFACT_ROOT/tracking-regression/test-tools"
mkdir -p "$cache/$DRONE_BACKEND"
exec > >(tee "$cache/$DRONE_BACKEND/prepare-test-tools.log") 2>&1

if [[ "$DRONE_BACKEND" == amd64 ]]; then
  docker run --rm --network none --platform linux/amd64 "$DRONE_IMAGE_ID" bash -lc '
    set -e
    command -v ffmpeg
    command -v ffprobe
    ffmpeg -version
    ffprobe -version
  '
  printf 'Native fixture tools ready in image %s; no download or private build is needed.\n' "$DRONE_IMAGE_ID"
  exit 0
fi

output="$cache/arm64/ffmpeg-4.4.2"
legacy="$repo/.docker-artifacts/tracking-regression/test-tools/ffmpeg-4.4.2"
for candidate in "$output" "$legacy"; do
  if [[ -x "$candidate/bin/ffmpeg" && -x "$candidate/bin/ffprobe" ]]; then
    docker run --rm --network none --platform linux/arm64 \
      --mount "type=bind,src=$candidate,dst=/tooling/private-ffmpeg,readonly" \
      "$DRONE_IMAGE_ID" bash -lc '/tooling/private-ffmpeg/bin/ffmpeg -version && /tooling/private-ffmpeg/bin/ffprobe -version'
    printf 'Verified private ARM64 fixture tools: %s/bin\n' "$candidate"
    exit 0
  fi
done
[[ ! -e "$output" ]] || { printf 'Incomplete tools exist: %s. Preserve or remove that directory deliberately before preparing again.\n' "$output" >&2; exit 1; }
archive="$cache/ffmpeg-4.4.2.tar.xz"
expected=af419a7f88adbc56c758ab19b4c708afbcae15ef09606b82b855291f6a6faa93
if [[ ! -f "$archive" ]]; then
  curl --fail --location --output "$archive.download" https://ffmpeg.org/releases/ffmpeg-4.4.2.tar.xz
  mv "$archive.download" "$archive"
fi
actual="$(shasum -a 256 "$archive" | cut -d ' ' -f 1)"
[[ "$actual" == "$expected" ]] || { printf 'FFmpeg source checksum mismatch: %s\n' "$archive" >&2; exit 1; }
stage="$(mktemp -d "$cache/arm64/build-XXXXXX")"
trap 'rm -rf -- "$stage"' EXIT
mkdir "$stage/source" "$stage/install"
tar -xJf "$archive" --strip-components=1 -C "$stage/source"
docker run --rm --network none --platform linux/arm64 --user "$(id -u):$(id -g)" \
  --mount "type=bind,src=$stage/source,dst=/work/ffmpeg" \
  --mount "type=bind,src=$stage/install,dst=/tooling/private-ffmpeg" \
  --env "DRONE_TOOL_JOBS=$DRONE_BUILD_JOBS" --workdir /work/ffmpeg "$DRONE_IMAGE_ID" bash -lc '
    set -eo pipefail
    ./configure --prefix=/tooling/private-ffmpeg --disable-autodetect --disable-everything \
      --enable-ffmpeg --enable-ffprobe --disable-ffplay --enable-protocol=file,pipe \
      --enable-demuxer=matroska,mov --enable-muxer=matroska,rawvideo \
      --enable-decoder=ffv1,h264,rawvideo --enable-encoder=ffv1,rawvideo \
      --enable-filter=extractplanes,color,format,scale --enable-indev=lavfi \
      --disable-doc --disable-debug --disable-shared --enable-static
    make -j"$DRONE_TOOL_JOBS"
    make install
    /tooling/private-ffmpeg/bin/ffmpeg -version
    /tooling/private-ffmpeg/bin/ffprobe -version
  '
printf '%s  ffmpeg-4.4.2.tar.xz\n' "$expected" > "$stage/install/source-sha256.txt"
mv "$stage/install" "$output"
printf 'Private ARM64 fixture tools ready: %s/bin\n' "$output"

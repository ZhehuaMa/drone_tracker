#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(git rev-parse --show-toplevel)
output_dir=${1:?Provide an absolute output directory}
mkdir -p "$output_dir"
docker run --rm --platform linux/amd64 --network none \
    -v "$repo_dir:/src:ro" -v "$output_dir:/out" \
    drone-tracker-rk3576:humble-amd64-rknn2.3.2 bash -lc '
        export TZ=UTC
        cmake -S /src/src/osd -B /out/build -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=/usr/bin/python3
        cmake --build /out/build -j 8
        ctest --test-dir /out/build --output-on-failure
        /out/build/osd_hud_style_tests /out
    '

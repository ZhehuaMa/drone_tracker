# NanoTrackV3 FP16 benchmark

`nanotrackv3_baseline` runs the public NanoTracker and RK3576 model backend on a local
MJPEG file. MJPEG stores each frame as an independent JPEG image. Decode yields the
original eight-bit luminance without a color round trip. The program does not start
ROS, load YOLO, or run DCF/center Kalman filtering. It does not convert or train models.

The executable name is historical. Its current default `loadNanoTrack` backend uses
shared device memory for the search-backbone-to-head feature transfer. Template
features and final head outputs still pass through Float32 host storage. FP16 means
16-bit floating model precision; Float32 means 32-bit floating application data.
The search edge must also meet the native FP16 NC1HWC2 layout contract. NC1HWC2 groups
channels for device storage. Unsupported native layout, import or binding fails rather
than falling back to a CPU feature transfer.

Older baseline records measured the former Float32 host-transfer backend. Their lack
of `benchmark_variant` described that old implementation; it is not proof that a newly
built executable uses the same path. Preserve and inspect source snapshots, executable
hashes, actual model descriptions and recorded spans before making any comparison.
The historical comparison reader interprets an absent variant as the old CPU path and
can reject current default-backend records. A fresh CPU-versus-shared-memory experiment
requires an explicitly identified CPU reference; running both current executable names
alone does not establish that comparison. See the [search-direct guide](../search_direct/README.md).

## Build

```bash
./build.sh --test
```

The default x86_64 image cross-compiles ARM64 output. The installed program is
`.docker-artifacts/backends/amd64/install/drone_tracker/lib/drone_tracker/nanotrackv3_baseline`.
`--backend arm64` explicitly selects the ARM64-image fallback and separate cache.
See the [image guide](../../../dockerfiles/README.amd64.md).

A native ARM64 SDK with OpenCV, FFmpeg, JPEG, OpenSSL and RKNN can build independently:

```bash
cmake -S tests/nanotracev3_performance_benchmarks/baseline -B build-baseline \
  -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON \
  -DRKNN_INCLUDE_DIR=/opt/rknn-runtime/include \
  -DRKNN_LIBRARY=/opt/rknn-runtime/lib/librknnrt.so
cmake --build build-baseline -j4
LD_LIBRARY_PATH=/opt/rknn-runtime/lib ctest --test-dir build-baseline --output-on-failure
```

In the x86_64 image add `-DCMAKE_TOOLCHAIN_FILE=/opt/toolchains/aarch64-humble.cmake`
and select SDK paths below `/opt/arm64-sysroot`. Run ARM tests through the selected
QEMU launcher. Keep generated output outside read-only source mounts. Each build embeds
Git commit/diff identity; inaccessible Git metadata fails the build. Keep source/Git
state stable during compilation. Use the board's system Runtime 2.3.2 and driver 0.9.8
for measurements; never overwrite its Runtime with an SDK library.

## Inputs and precision evidence

1. Provide a local regular MJPEG file and first-frame box `[x,y,width,height]` in source
   pixels. The top-left is x/y, width/height are positive, and partial out-of-bounds
   boxes retain the production crop/padding rules.
2. Provide template backbone, search backbone and head FP16 models. Backbones extract
   features; the head compares them. Retain original export evidence, not filename
   assertions about precision.
3. Create a per-model evidence index pointing to the original export record:

```yaml
%YAML:1.0
---
role: "template_backbone"
model_sha256: "REPLACE_WITH_MODEL_SHA256"
precision: "FP16"
status: "success"
source_record: "original-export-record.json"
source_record_sha256: "REPLACE_WITH_RECORD_SHA256"
```

The index summarizes existing evidence; it does not probe internal operator precision.
Missing/conflicting export records, hash mismatches, INT8 or mixed-precision exports
cannot establish this FP16 benchmark. Actual interfaces must be floating. AFFINE
metadata on floating tensors is not independently a rejection reason.

4. Create an OpenCV YAML manifest with `schema_version: 1` and a `models` sequence.
   Each of `template_backbone`, `search_backbone`, `head` requires `role`, `sha256`,
   `precision: FP16`, `export_record` and `export_record_sha256`.
5. Copy [config.example.yaml](config.example.yaml), fill actual paths, board identity
   and initial box. Relative paths resolve against their containing config/evidence
   file. No shell/environment/tilde expansion occurs. Preserve `%YAML:1.0` headers.

## Run and outputs

```bash
./nanotrackv3_baseline config.yaml
```

Output must be new and its parent must exist. Defaults initialize and warm up for 30
updates, then process three rounds from the first frame. Each round rebuilds its template
but reuses model sessions. `max_updates: 0` reads to EOF. Insufficient warmup fails.
Frames run sequentially without playback-rate sleeps or score-based resets/skips.
Initialization has no prediction score. Slow/failed frames and warmup remain recorded.

Use a second configuration with `detailed_observation: 0` and a new output directory
for total-only timing. Keep models, input, warmup and rounds identical. Compare boxes
within `1e-4` pixels per component and scores within `1e-6`, and report observation
cost without subtracting it from raw measurements.

| File | Contents |
|---|---|
| manifest.json | Configuration, input/model/program/source hashes, actual attributes, Runtime/driver, board/environment and final state. Export evidence is copied separately. |
| code_snapshot.txt | Build-time commit, differences and additional source identity. |
| frames.csv | Round/phase/operation/frame, PTS, box, score, total duration and error. PTS means presentation timestamp; absent PTS has `pts_valid=0`. Initial box is `bbox_kind=input`, updates are prediction. |
| spans.csv | Interval/parent IDs, model role, integer monotonic nanoseconds and completion. `residual_ns` subtracts direct child intervals. |
| environment.csv | Temperature, CPU/NPU frequencies and policies sampled outside timing; inaccessible values retain reasons. |
| summary.json / report.md | Statistics computed from stored CSV. The current generated Markdown report is Chinese. |
| diagnostics.log | Preparation/runtime diagnostics; retain SDK console output separately. |

Blank CSV cells mean no executed/valid value, not zero. JSON stores original 64-bit times
and nanosecond statistics as decimal strings. Floating results retain round-trip precision.
Failed frames do not enter successful-update distributions.

`decode_to_result` starts at actual JPEG decode and ends when init/update returns,
including the first frame decoded during reader construction. Model loading, demuxing,
record writes and environment samples lie outside it. Do not add nested intervals twice;
`other_ns` subtracts mutually exclusive top-level intervals. P50/P90/P95/P99 use sorted
`ceil(p*N)`; population standard deviation divides by N. Empty samples are null.
Processing rate is successful updates divided by summed latency: 20 ms plus 30 ms gives
40 Hz. It excludes file I/O and playback waiting.

Normal EOF/frame-limit completion with successful formal updates and complete output
returns 0 and state complete. Configuration error returns 2; runtime/write error 1;
handled SIGINT/SIGTERM returns 130 and interrupted. SIGKILL, fatal device failures or
power loss may leave running/incomplete output. An intact row is not a complete run.

`baseline_verify_records DIRECTORY [SECOND_DIRECTORY]` independently recomputes CSV
statistics and compares optional per-frame outputs. It validates the historical variant's
expected stage set. In particular, an unlabelled historical CPU record requires output
retrieval for every model and no binding, while search_direct requires shared binding and
no search-output retrieval. Apply the current-backend compatibility limitation above.
SDK spans include host scheduling, waiting and conversion; they are not pure NPU time.
Latency or numerical equivalence does not establish tracking accuracy.

## Retention

`.docker-artifacts` and `/tmp` are disposable and may be cleared routinely. Preserve
required raw results, identities, export evidence and input locators in an explicitly
retained external location. Historical paths do not guarantee surviving files.

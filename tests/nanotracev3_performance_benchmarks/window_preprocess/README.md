# RK3576 NanoTrack local-window verification

This tool checks whether local-window preparation preserves the former model input,
and compares recovery time using identical inputs. Recovery initializes from an older
detection and tracks its following history frames in order. It uses the real
NanoTracker/NanoDcfTracker. Disabling DCF, a grayscale correlation filter, still retains
the center Kalman filter that smooths position using timestamps.

## Reference and input identity

`whole_frame_preprocessor.h/.cpp` derives from commit
`ea9cd822b9717b2759ece8e251bb977c3f88449b`'s RK3576 preprocessor, as recorded in
[reference_source.txt](reference_source.txt). Only class, namespace and result-type
references change. It retains whole-image Float32 conversion, padding, crop, floating
resize, full-image mean and storage leases. Float32 means 32-bit floating point; a lease
prevents input storage being overwritten while held. Production does not link this
reference and it is not an installed runtime fallback.

The designated input is `test-01-20260929-1513`, SHA-256
`11d45885dc32859c5aba16541ecc83f769ad47ba9ae66b54908b19d805d4c152`.
Original reconstruction source is `09006db9ea11bd4fa137efc47ecb117660e0fad2`.
The 25 frames correspond to zero-based video indices 4146-4170, selecting the latest
recorded frame not later than each processing time. This reconstructs neither original
uncompressed camera pixels nor the complete historical workload. Its results cannot
be presented as a stage decomposition of the original approximately 388 ms incident.

Large images/models stay outside Git. Read the existing input package without replacing
its images through a fresh decode. The box is fixed at
`[756.689,461.019,39.712,36.7552]`. Verify supplied model fingerprints against real files.
Example timestamps and pixel hashes are placeholders; obtain them from the original
package, rather than inventing timestamps from nominal frame rate.

## Configure and inspect

Copy [config.example.yaml](config.example.yaml),
[input-manifest.example.yaml](input-manifest.example.yaml) and
[environment-notes.example.txt](environment-notes.example.txt) into an isolated directory.
Config-relative paths resolve there. Frame paths resolve against the input manifest
and must not contain `..`. Decimal-string microseconds avoid OpenCV YAML integer loss.

The manifest requires 25 entries with `order`, `video_index`, `timestamp_us`, `path`
and `pixel_sha256`. Hash decoded Gray8 pixels from top to bottom, exactly 1920 bytes
per row, excluding padding. Every image must be 1920x1200 single-channel eight-bit
grayscale. No implicit color conversion occurs. First order/index is 0/4146.
Missing, altered, wrong-sized or out-of-order data fails before recovery.

```bash
nanotrack_window_recovery --check-inputs experiment/config.yaml
```

This reads only config/images, with no models, output directory or board-device access.

## Build and software checks

Use `./build.sh --test` for ARM64 output from the default x86_64 compiler image.
`--backend arm64` selects the explicit ARM64-image fallback. The
[image guide](../../../dockerfiles/README.amd64.md) covers SDK paths and test launchers.
Managed-worktree containers need read-only access to external Git metadata for real
source identity; do not substitute another checkout or rewrite the identity.

Independent native ARM64 SDK build:

```bash
cmake -S tests/nanotracev3_performance_benchmarks/window_preprocess \
  -B build/window-preprocess -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON
cmake --build build/window-preprocess -j4
ctest --test-dir build/window-preprocess --output-on-failure
```

The x86_64 image requires the cross toolchain and RKNN paths under
`/opt/arm64-sysroot/opt/rknn-runtime`, with ARM tests run through the selected QEMU
launcher. Keep generated output outside read-only source mounts.

`baseline_nanotrack_preprocess_tests` checks exact old-path input equality and a separate
double-precision four-neighbor interpolation reference within `5e-4`. Coverage includes
odd/even sizes, fractional centers, every edge/corner, fully external windows, one-pixel
size, scaling, random/pattern inputs and non-contiguous rows. It also covers stale
workspace, mean changes, 114 fallback, source release, busy storage and destruction.
`nanotrack_window_tests` checks one initialization plus 24 updates and their outputs
and model-call counts. `recovery_regression_tests` checks actual HybridTracker recovery
with controlled models, including failed initialization and partial catch-up failure.
These verify process rules, not real-model numerical acceptance.

`nanotrack_window_performance_tests` exercises the production `performanceVerdict` with
fixed statistics; `nanotrack_window_performance_exit_tests` checks its child-process
exit/status behavior. Boundary cases preserve strict/non-strict comparisons for the
20% preprocessing ratio, decreased recovery median and non-increasing recovery P95.
These checks generate no hardware performance evidence.

## Production-board comparison

Use the identified HZ-RK3576_WuKong Board, Ubuntu 22.04.5, kernel `6.1.141-rt36`,
system RKNN Runtime 2.3.2 and driver 0.9.8. Do not replace `/usr/lib/librknnrt.so`.
The tool reads the environment and does not alter services, governors or cameras.
If a maintenance window authorizes stopping services, an external controller must
restore their original states on success or failure. An independent board restoration
timer protects against lost access. Record board, services and concurrent processes
in the environment notes. Driver-version reads may need existing debugfs permission.

```bash
source /opt/ros/humble/setup.bash
build/window-preprocess/nanotrack_window_recovery --measure experiment/config.yaml
```

The full build puts this non-installed tool below
`.docker-artifacts/backends/amd64/build/drone_tracker/tests/nanotracev3_performance_benchmarks/window_preprocess/`.
Provide it and required project libraries in an isolated experiment directory, without
switching the formal node release.

Before formal warmup the tool verifies identities and exactly compares the new and old
inputs along a real recovery trajectory. Loading, decoding, frame copying, per-batch
reset, verification and output writing remain outside timing. Each path warms up at
least twice, then measures at least ten paired recoveries in alternating order. Config
fixes CPU placement and OpenCV threads. Each batch performs one initialization and
24 timestamp-ordered updates; any update failure or output difference fails the case.

| File | Evidence |
|---|---|
| exact-inputs.txt | Elementwise comparison of all 25 tensors, counts and window sizes outside timing. |
| other-windows.csv | Independent preparation comparisons at sizes 1/32/77/154/255/511/1024/2048, two warmups and at least ten pairs, without inference. |
| identity.txt / code-snapshot.txt | Code, executable, model, library, input/config identities and copied requests. |
| frames.csv | Warmup/formal flag, path order, per-frame outcome, box/score, stages and model counts. Count actual RKNN Run intervals by ancestry. The current direct-search path lacks a SearchBackbone wrapper; a Run directly under Inference is search. Template/head retain their model ancestors. Mocks without Run use model-stage events. Repeated batch_ns contributes once per batch. |
| intervals.csv | Raw parent/child intervals; ImagePrepare is inside Preprocess and must not be added again. |
| environment-snapshots.txt | CPU/NPU frequencies, temperature, governors, counters and load before/after each batch. |
| report.txt / status.txt | Formal statistics and verdict; warmup remains stored but excluded from statistics. |

Even-sized medians average the middle two samples; P95 selects sorted `ceil(0.95*N)`.
Acceptance requires exact per-frame comparison, 1/24/24 template/search/head calls,
new preparation median <=20% of the former path, lower overall recovery median and
non-increasing recovery P95. An unmet limit returns 1; success returns 0. Partial
failure retains completed batches and reasons without a passing verdict.

Other window results stay separate from the designated recovery and keep its template/
search output sizes and initial box. A single successful input does not establish all
window cases or predict the optimized duration of the historical incident. See the
[archived validation](../../../openspec/changes/archive/2026-10-13-optimize-rk3576-nanotrack-window-preprocessing/validation.md)
for that measured implementation and its limitations.

`.docker-artifacts` and `/tmp` are disposable and may be cleared routinely. Retain
required input packages, raw samples, identities, manifests and reports externally
before cleanup. Historical storage paths do not guarantee current availability.

# NanoTrackV3 search-feature shared-memory benchmark

[Project overview](../../../README.md) · [Documentation index](../../../docs/README.md)

`nanotrackv3_search_direct` runs public NanoTracker on local MJPEG luminance frames.
The template backbone extracts features from 127x127 input; the search backbone uses
255x255 input; the head compares the two. Search features pass through one shared
FP16 device buffer, without application-side CPU reads, copies or conversion. Template
features and final head outputs retain Float32 host transfers. FP16 and Float32 mean
16-bit and 32-bit floating point respectively.

The original benchmark compared this path with a CPU-transfer `nanotrackv3_baseline`.
The production loader and the current baseline executable now also use shared-memory
search transfer. Therefore these two current executable names do not establish a fresh
CPU-versus-shared-memory experiment. The explicit `loadNanoTrackCpuReference` helper
exists in the source but is not the baseline executable's default. Historical measured
comparisons apply to their recorded source/binary identities. Existing comparison tools
also interpret an absent variant as the older CPU path; that inference is insufficient
for new default-loader output. See the [baseline guide](../baseline/README.md).

## Build and run

Use `./build.sh --test` for default x86_64 cross-compilation to ARM64. Select
`--backend arm64` explicitly for the preserved ARM64 compiler image. Installed tools
include `nanotrackv3_search_direct` and `nanotrackv3_benchmark_compare` under
`.docker-artifacts/backends/amd64/install/drone_tracker/lib/drone_tracker/`.
See the [image guide](../../../dockerfiles/README.amd64.md).

Independent native ARM64 SDK build:

```bash
cmake -S tests/nanotracev3_performance_benchmarks/search_direct -B build-search-direct \
  -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON \
  -DRKNN_INCLUDE_DIR=/opt/rknn-runtime/include \
  -DRKNN_LIBRARY=/opt/rknn-runtime/lib/librknnrt.so
cmake --build build-search-direct -j4
LD_LIBRARY_PATH=/opt/rknn-runtime/lib ctest --test-dir build-search-direct --output-on-failure
```

The x86_64 image instead needs its cross toolchain and target paths beneath
`/opt/arm64-sysroot`; use the selected QEMU launcher for ARM checks. Build output belongs
outside read-only source mounts. Git identity is embedded before build; Git query failure
fails compilation. The standalone build includes baseline targets. Use the production
board's system Runtime 2.3.2 and driver 0.9.8, never an SDK replacement.

Copy [config.example.yaml](config.example.yaml) and supply the same input/box, three
FP16 models, export evidence, warmup, update limits, rounds and board identity described
in the baseline guide. Run:

```bash
./nanotrackv3_search_direct search-direct.yaml
```

Output must be new and its parent must exist. Exit codes are complete=0, runtime/write
failure=1, configuration error=2 and handled SIGINT/SIGTERM=130. The tool runs no ROS,
YOLO, DCF or Kalman filtering, and performs no model conversion or training.

## Native contract and per-frame ordering

NC1HWC2 groups logical channels for device storage; `[1,12,16,16,8]` represents 96
channels. Both search output and head search input must have rank 5, FP16 type,
NC1HWC2 layout, identical native dimensions, effective row stride, safe capacity and
quantization metadata. Native groups must cover logical channels. Output `w_stride=0`
means logical width, so zero and explicit matching width are equivalent. SDK write-only
`h_stride` is not an admission criterion. Logical element count alone is insufficient.
Contract, import or binding failure reports expected/actual attributes and fails.

Binding (`rknn_set_io_mem`) selects device storage; it neither runs the model nor copies
features. Ordinary submission (`rknn_inputs_set`) supplies host data. Each search follows:

1. Prepare Float32 search image input and submit it to the search model.
2. Rebind search output to shared buffer S after that ordinary submission.
3. Run search synchronously and confirm its current output is complete in S.
4. Submit only template features to the head through its ordinary input operation.
5. Rebind the head search input to S after template submission, with pass-through.
6. Run the head and retrieve its two Float32 outputs for public postprocessing.
7. Permit the next search to overwrite S only after head reading finishes.

Ordinary submissions invalidate prior bindings. Historical board-2 validation on
2026-09-20 established the post-submission ordering across 720 independent processes;
that result is scoped to its models and implementation. Search allocates with
`rknn_create_mem`; the head imports the same descriptor with `rknn_create_mem_from_fd`.
Release the consumer import before the producer allocation, then destroy sessions.
Unknown device-stop state or shared-memory release failure follows the fatal resource
safety policy instead of freeing potentially active storage.

## Records, numerical checks and historical comparison

Files follow baseline format: manifest, frames, spans, environment, summary, report,
diagnostics and source snapshot. Additional identity includes `benchmark_variant=search_direct`,
`search_handoff=native_fp16_shared`, `bind_policy=rebind_after_each_inputs_set`, completion
evidence, shared buffer bytes/descriptor, native attributes and head role indices.
Summary/report retain that identity and model/Runtime/driver descriptions. Multiline
summary descriptions are string arrays; the manifest retains its original string form.
The generated human report currently uses Chinese.

`io_mem_bind` spans belong under search_backbone/head and remain included in parent and
end-to-end timing. Search has no ordinary outputs_get/output_copy/outputs_release/normalize
stages. Absent execution has no sample. Older unlabelled CPU records retain their
historical interpretation; do not relabel newly generated output to force compatibility.

`baseline_verify_records DIRECTORY [SECOND_DIRECTORY]` independently recomputes stored
statistics and optional per-frame differences. Box tolerance is `1e-4` pixels per component;
score tolerance is `1e-6`. It checks variant-specific stage contracts, so current baseline
compatibility must be established before treating it as a CPU reference.

The board-only numerical probe compares raw head outputs from the benchmark ordering,
a separately verified ordering and independent ordinary search/head reference sessions:

```bash
./nanotrackv3_search_direct_probe \
  --template-model models/template_backbone.rknn \
  --search-model models/search_backbone.rknn \
  --head-model models/head.rknn --iterations 240 --output probe.json
```

Template seeds 11/29 and search seeds 101/202/303 cover first use, A/B/A/C reuse and
four-iteration template changes. Mismatch, nonfinite output or insufficient reference
variation fails. Raw tensor equality is stronger than equality of final boxes, but
still covers only the measured inputs/models.

`nanotrackv3_benchmark_compare` retains its historical four-group interface: explicit
`--baseline DIRECTORY` and `--direct DIRECTORY` arguments, with detailed and total-only
observation for both sides and at least `--min-rounds` (default 3) runs per group.
`--report FILE` writes the report. Each option accepts exactly one directory.
The tool checks board/system/host, CPU mask, input/model/library hashes, actual precision,
code/compiler/options/dependencies, time base, Runtime/driver, initial box, warmup,
limits, rounds and environment sampling. Missing/different conditions prevent its
single-variable speed conclusion. It retains all slow/low-score frames and raw binding
costs without post-hoc thresholds. Its current baseline-identification limitation means
successful record parsing alone does not prove a valid new host-transfer comparison.

## Evidence boundaries and retention

Software doubles do not establish NPU acceptance. Numerical equivalence does not prove
absence of all device-internal movement; latency does not establish tracking accuracy.
Historical board results are in the archived
[benchmark validation](../../../openspec/changes/archive/2026-09-21-add-nanotrackv3-search-direct-benchmark/validation.md).
`.docker-artifacts` and `/tmp` may be cleared routinely. Preserve required raw results,
source/binary identities and manifests in an explicitly retained external location.
Historical paths are provenance, not guaranteed current storage.

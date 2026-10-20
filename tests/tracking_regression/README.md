# Configurable RK3576 tracking regression

Run one operation from one file:

```bash
python3 tests/tracking_regression/run_suite.py CONFIG.yaml
```

The file supplies `operation: prepare|run|freeze|compare`, all case inputs, paths, round counts and comparison settings. Extra command-line overrides are rejected. The former `prepare_fixture.py`, `run_batch.py`, `baseline.py` and `compare.py` business interfaces now point to this entry. `verify_fixture.py` remains a read-only inspection tool. Build scripts, SDK tests and the independent sequential `rk3576_tracker_replay CONFIG.yaml` keep their existing interfaces.

A **fixture** is an immutable input directory containing a lossless luminance clip, original frame mapping, recorded gimbal attitude and frozen algorithm/model identities. A **qualified run** has complete input, diagnostic and process-cleanup evidence. Qualification does not establish tracking accuracy or acceptable performance. Explicit versioned thresholds are needed for a performance pass. There are no human target labels.

## Configuration and preparation

See [configs/example.yaml](configs/example.yaml) for complete operation sections. It contains one `[55,75)` case and one whole-video case. Paths resolve relative to the configuration file, including model mappings. Every operation saves the original text, resolved JSON and both SHA-256 fingerprints before executing its cases. External edits after loading do not change the saved request. Duplicate keys, unknown fields, duplicate case identities, unsafe identifiers, null intervals, boolean numbers, non-finite values, excessive sub-microsecond precision and overlapping input/output directories are rejected before execution.

Each `clips` item requires:

| Field | Source and meaning |
|---|---|
| `case_id` | Stable letters/digits/hyphens/underscores; identifies this case across cohorts |
| `source.video` | Original video, including overlay pixels when the specified video has overlays |
| `source.frame_logs` | Explicit ordered JSONL files containing every source frame's integer `pts_us`, `timestamp_us` and driver `sequence` |
| `source.attitude_bags` | Explicit ordered SQLite ROS bags with `/fpv/gimbal` `sensor_msgs/msg/Imu` CDR messages |
| `source.session` | Original `session.json` whose `identity` records effective algorithm parameters and model hashes |
| `frame_selection_interval` | Optional source-time selection; both `start_seconds` and `end_seconds` are mandatory when present |
| `analysis_windows` | Optional named statistics ranges relative to the selection start; they do not alter supplied frames |
| `fixture` | Optional existing fixture locator for run; prepare uses `output_root/fixtures/case_id` |

An interval is **not a timeout**. `{start_seconds: 55, end_seconds: 75}` selects integer frame times satisfying `55 <= pts < 75`. `{start_seconds: 165, end_seconds: end}` selects from 165 seconds through the verified recording end, including the final decoded frame. Omitting the entire field selects all source frames. Null, an empty object, a missing endpoint, an unknown marker, an empty selection or a range beyond the source timeline fails. Decimal seconds must convert exactly to integer microseconds.

Preparation checks one-to-one source-video/log frame order and timing, constant timestamp epoch, dimensions and a trustworthy final-frame duration or stream end. Nominal FPS is never used to invent a tail. The latest actual attitude at or before the selection start is required; later attitudes retain their due times. Missing frame times, initial attitude, valid camera parameters or model fingerprints fails preparation. No simulated attitude or different recording is substituted. Related sidecars for an overlay video must be proven to match its frames and timing; similar filenames are insufficient.

Set `operation: prepare`, `prepare: {}` and positive `deadlines.preparation_seconds`/`cleanup_seconds`. Each case is prepared in a bounded process. FFmpeg extracts the source Y plane directly and FFV1 preserves those Gray8 pixels without scaling or a color round trip. File hashes are checked again after decoding. A failed preparation retains diagnostics but removes the runnable fixture marker. Source files are never overwritten.

The retained [reference template](configs/wlcb-reference.yaml) preserves `wlcb-1513-loss-recovery`: source `[55,75)`, 1920×1200, 1,200 frames, original IDs 3302–4501 and first/last offsets 15,721/19,992,663 microseconds. Its `core` window remains `[13.5,17.5)`. General cases use their own metadata.

## Runs, resources and deadlines

Set `operation: run`, `rounds: 3` or more, and a `run` section:

```yaml
run:
  cohort_id: reference-a
  source_repo: /external/source
  binary: /external/install/lib/drone_tracker/rk3576_tracking_regression
  worker: /external/install/lib/drone_tracker/rk3576_diagnostic_worker
  settings:
    input_mode: predecode_all
    max_supply_lateness_us: 50000
    queue_capacity: 32
    clock_jump_tolerance_us: 5000
    ros_domain_id: 83
deadlines:
  preparation_seconds: 90
  cleanup_seconds: 10
```

`max_supply_lateness_us` bounds lateness of the independent input supplier. Freeze it before reference runs. `queue_capacity` bounds queued frame references, not the full prepared input cache. Optional `workload` and `expected_workload_processes` declare existing workload conditions. Optional `model_paths` maps frozen `tracker.yolo_engine`, `tracker.nano_template_backbone`, `tracker.nano_search_backbone` and `tracker.nano_head` to matching model content at new locations. Unknown names or changed content are rejected. Configuration paths do not override recorded algorithm values.

Each round starts a new node/worker process, diagnostic socket and run directory. The node receives one generated request. It checks the fixture certificate and file hashes, then decodes and verifies all cached pixels before loading any model. Only after models are ready does it establish the playback clock. Original frame intervals and the first-frame offset remain intact. The supplier runs independently of slow tracking; the production tracker still drains queued frames and handles the newest one. Queued frames retain the verified, immutable cached pixels in real RK3576 host frames. No full-image allocation or pixel copy occurs during timed supply. The complete cache remains held until stop; each frame/read view independently retains the pixels it needs after cleanup. Stop wakes waits, joins the supplier, releases caches/models and reaps the isolated process tree before another round starts.

The entire selected clip is cached without a fixed memory ceiling or streaming fallback. Cache bytes equal the actual retained matrices; 1,200 frames at 1920×1200 occupy 2,764,800,000 pixel bytes, plus model/decoder/frame-metadata memory. Larger inputs may exhaust board memory. Allocation, decode, pixel, process-kill or cleanup failures retain incomplete evidence and never shorten input automatically. `preparation.json` records verified frames, cache bytes, preparation timing and process/system memory.

Let P be preparation seconds, D be the resolved playback duration and C be cleanup seconds. The recorded total deadline is **P + ceil(D) + C**. Preparation includes preflight, full-cache decoding and model loading. It must finish within P. Forced cleanup starts by total minus C; process stopping and completion-record finalization are inside the total deadline. With P=90 and C=10, D=20 gives 120 seconds and D=180 gives 280 seconds. A 45-second preparation followed by twenty seconds of playback can succeed. Selecting `[55,75)` does not impose a twenty-second process timeout.

Run cases in their declared order and rounds serially. A normal failed round is retained and later planned rounds continue after cleanup. Unconfirmed cleanup stops the suite and explicitly lists unexecuted rounds/cases. There are no automatic retries or fastest-round selections.

## Freeze and compare

The example file contains explicit `freeze.cases` and `compare.cases` maps keyed by `case_id`. Each freeze case names `runs: [PATH1, PATH2, PATH3]`; `freeze.baseline_id` selects a new immutable output directory. Each compare case names `baseline`, `candidates: [PATH1, PATH2, PATH3]` and optionally `thresholds`. `compare.comparison_id` selects a new comparison directory. No glob or most-recent-run discovery is used.

A complete workflow uses separate saved request files or deliberately changes `operation` between invocations:

1. `prepare`: create all fixtures.
2. `run` with `cohort_id: reference-a`: execute every case three times.
3. `freeze` with `baseline_id: baseline-a`: freeze each case's three explicit reference paths.
4. Build and run the candidate with `cohort_id: candidate-a`; keep input/model/platform/observation/window/deadline conditions identical.
5. `compare` with `comparison_id: candidate-a`: compare each frozen case against its three explicit candidate paths.

Existing operation destinations are rejected. A comparison cannot overwrite references. Each operation produces `suite-report.json`, `suite-report.csv` and a Chinese `report.md`. Comparisons additionally produce per-case JSON/CSV/Chinese reports. Missing, extra, incomplete and incomparable cases remain visible. No thresholds means “未设置判定规则”; operation success and performance pass are separate fields. Metrics and percentages are never averaged across recordings.

```text
OUTPUT_ROOT/
  fixtures/CASE/{fixture.json,clip.mkv,frames.jsonl,attitude.jsonl,algorithm.yaml}
  runs/COHORT/CASE/run-NNN/{identity.json,preparation.json,input.jsonl,events.bin,metrics.json,completion.json,...}
  baselines/BASELINE/CASE/baseline.json
  comparisons/COMPARISON/CASE/{comparison.json,comparison.csv,report.md}
```

The [four-case overlay configuration](configs/wlcb-overlay-four-cases.yaml) declares `wlcb-1605-whole`, `wlcb-104206-020-035`, `wlcb-104206-100-130`, and `wlcb-104206-165-end`. Its source locations identify the copied recordings and verified companion files. Their checksums, exact encoded frame ticks and recorded attitudes are documented in the change's resumed board report. Relocate these paths when using another data copy and choose an external output root. Test-02 uses all frames; test-03's `[20,35)`, `[100,130)` and `[165,end)` are three independent cases. Four cases × three rounds = twelve runs per cohort and twenty-four across reference/candidate. Each case has its own reference paths, candidate paths and baseline; the three ranges cannot be joined with inherited state. Large media stays outside Git.

For recordings with `pts_90k`, preparation proves that each encoded 90 kHz timestamp equals the log and the recorder's exact rounding from microseconds. A 90 kHz tick is about 11.11 microseconds, so comparing rounded decimal seconds to the original microseconds with a one-microsecond tolerance would reject valid recordings. Missing or altered ticks and an incompatible stream time base remain errors. The replay clock still uses the original microsecond timestamps.

## Identity, migration and platform

Suite configuration is version 1; generated per-round settings are version 3. New fixture, preparation, identity, completion, metrics container, baseline and comparison records are version 2. Original diagnostic events and metric formula versions remain unchanged. Historical version-1 records and run configuration version 2 retain fixed-reference validation and read-only inspection. Unknown versions and mixed-version/case cohorts are rejected. Prepare/rerun the reference through the new entry to establish a new group; old evidence is never rewritten.

Comparison conditions include this case's pixels, timing, resolved numeric selection, attitudes, algorithm/model content, windows, preparation/deadline settings, board, Runtime/driver, actual dependencies, CPU policy, userspace workload and observation mode. Source/model locations and the original interval spelling remain provenance. Unrelated case order/list edits and output/config paths do not change this case's semantic conditions. Execution order remains recorded context and can affect thermal observations. ELF dependencies are hashed by SONAME (the dynamic loader's library name); project-built libraries belong to code identity. The effective ROS middleware is measured in a separate non-node probe. Concurrent unrelated userspace processes remain workload conditions; private suite/control processes and kernel threads are recorded separately.

Real-model/performance acceptance requires the same specific `HZ-RK3576_WuKong Board`, its system RKNN Runtime 2.3.2 and RKNPU 0.9.8. The tool never changes Runtime, production services or CPU policy. Arrange any required maintenance separately. The historical three-reference/three-repeat fixed-case acceptance in [acceptance.md](acceptance.md) is evidence for the earlier fixed tool, not acceptance of this configurable feature.

## Build and portable checks

Dependencies are Python 3 with `python3-yaml`, FFmpeg/ffprobe, ROS 2 Humble and the existing platform SDK. Both native and target Python must import YAML; missing dependencies fail before testing. Normal builds do not download/install packages.

```bash
./build.sh --test
./build.sh --test --artifacts-dir /external/validation
bash tests/tracking_regression/run_sdk_tests.sh --regression-only
```

The default x86_64 image cross-compiles ARM64 targets and runs portable process/configuration/fixture tests natively. The SDK coordinator executes every discovered Python ID exactly once, with the real ARM middleware case on target Python, within the original 30-second aggregate deadline. Source tests retain the original 90-second deadline and native OS memory-limit check. No backend is selected automatically after failure. `--backend arm64` explicitly selects the preserved ARM64-image fallback; prepare its private FFmpeg tools first with `prepare_test_tools.sh --backend arm64`. Source/Git mounts are read-only and ordinary containers are offline. See [the image/build guide](../../dockerfiles/README.amd64.md) for separate caches and artifact packaging. QEMU and host results do not establish NPU, camera, HDMI or production timing acceptance.

See [metrics.md](metrics.md) for formulas, [diagnostic_contract.md](diagnostic_contract.md) for event integrity, and the active OpenSpec change for implementation/acceptance evidence.

The optional `run.settings.supplier_cpu` selects one online CPU for the test's
frame supplier. For example, `supplier_cpu: 4` changes only that thread's allowed
CPU set. Omit the field to inherit the caller's set. The board preflight rejects
a CPU outside the controller's allowed set. Thread setup and readback finish
before the replay clock starts; failure prevents playback. The requested CPU
belongs to the comparison identity, so changing it requires fresh references.

`source-scheduling.json` records the supplier thread ID, inherited and actual
CPU sets, scheduling policy, priority, and setup time. `supply-timing.csv` separates
scheduled wakeup, actual wakeup, frame-copy time, queue acquisition, and attitude
callback time. It also records the actual CPU, thread CPU time, page faults, and
context switches during copying. These diagnostics preserve the original supply
timestamp: a frame counts as supplied only after its owned pixel copy is ready
and the queue lock has been acquired. The lateness threshold remains 50,000
microseconds in the external clip configurations.

CPU masks need interpretation alongside the board's isolation configuration.
When Linux boots with `isolcpus=4-7`, an inherited mask of `0-7` does not guarantee
normal scheduler load balancing onto CPUs 4–7. Explicit supplier placement can
keep replay input independent of compute contention. It does not change the
production policy, CPU governors, model execution, or other tracker threads.
Artificial contention tests are diagnostic groups; formal references and
candidates run without that added load.

## Consume completed YOLO output before requesting the next frame

The `consume-yolo-result-before-request` change uses three stages at each normal
interaction: take one completed result into a local snapshot; attempt the current
request (or keep the existing Tracking interval skip); then arbitrate the saved
result. Arbitration includes history lookup, initialization and catch-up, which
replays cached frames after the detection's source frame. This order lets the
worker process the new request while catch-up runs. Tracking captures its current
NanoTrack observation before taking the result snapshot.

A completion arriving after that one read stays in the detector's result slot.
It can still reject the current request with `UnreadResult`. There is no second
read in the same update. An accepted pending frame can still be replaced by a
newer frame; the order does not guarantee inference on fixture frame 389.

For a before/after comparison, bind the reference source to commit
`7fbb0a2e006f12654a7db0f6d9d152244ec07e0d` and export the actual candidate source.
Record both source fingerprints and both real executable hashes before the first
round, and require both pairs to differ. Running two cohorts with one executable
only measures repeatability. Keep fixtures, model content, algorithm settings,
tools, board, CPU policy, supplier CPU 4, preparation deadline 3600 seconds,
cleanup deadline 10 seconds and supply lateness limit 50000 microseconds fixed.
Use new reference, candidate, baseline and comparison directories. Run only the
three test-03 intervals, three rounds per side; do not prepare or replay test-02.
The entry point remains `python3 tests/tracking_regression/run_suite.py CONFIG.yaml`.

Report every round, including zero-valid-box rounds, then each case's median,
range, candidate-minus-reference difference and relative change. If the reference
value is zero, relative change is undefined; preserve the raw values instead of
inventing a percentage. Show valid boxes / total publications alongside update
and detection time, longest invalid output, publication gaps, initialization,
catch-up, rejection/replacement counts and memory. For the middle interval, list
each round's initialization outcome and the request/execution identities around
fixture frames 388/389. Do not average across recordings. A qualified run proves
that evidence and cleanup are complete; a faster update or more valid boxes does
not establish tracking accuracy. Without explicit performance thresholds, retain
`not_set` as the verdict.

## Optional regression CPU placement

`configs/affinity-example.yaml` enables a private CPU policy through the same single suite file.
The supplier uses CPU 4, Tracking uses 5, YOLO uses 6, and the separate diagnostic logger uses 7.
The node main thread and its single-threaded ROS executor use CPU 1. External supervisors use CPUs 0–3.
An affinity mask is a thread's permitted CPU set. Each managed entry reads its actual mask back.
This example assumes these CPUs are online and available in the process's initial mask and effective cgroup.
A cgroup is an operating-system resource boundary; the test never expands beyond its permitted CPUs.
The test writes its generated policy only in the new run directory. It leaves the production policy unchanged.
An explicit policy rejects `DRONE_TRACKER_CPU_CONFIG` from the environment instead of accepting an override.

To retain the old supplier-only behavior, omit `cpu_affinity`. To declare the unbound experimental reference,
use `cpu_affinity: {version: 1, mode: inherit}` and keep `supplier_cpu: 4`.
For symmetric work-point evidence, both sides use the identical `cpu_observation` object and observation patch.
The pinned mode requires observation. Omit both new objects to disable the new policy and observation.
No realtime priority, governor, NPU assignment, model, topic or tracking parameter is changed.

The pinned startup sequence is explicit:

1. Validate online CPUs, initial process mask and effective cgroup. Preserve this creation mask for the node.
2. Decode and hash every retained frame. Release the FFmpeg decoder and its preparation threads.
3. Create and bind the owned supplier, then park it on a condition variable. No frame, attitude or clock is emitted.
4. Bind the main thread and construct models. YOLO confirms its role at worker entry.
5. Save the external model-ready thread snapshot and acknowledge it. Confirm supervisor and logger placement.
6. Create Tracking and read its mask. `start()` reuses the parked supplier, establishes `m0/e0`, then releases input.
7. At EOF, capture the end snapshot, stop and join all workers, and export bounded CPU arrays.

Preparation failure, model-construction failure and cancellation all stop and join a parked supplier.
Repeated `stop()` is safe. Without the policy, supplier creation keeps its original `start()` timing.

## Explicit affinity experiments

Ordinary comparisons still require identical CPU policies. An experiment must explicitly provide
`compare.cases.<case_id>.affinity_experiment`, a frozen JSON manifest (schema 1).
`configs/affinity-experiment-example.json` shows the structure; replace its illustrative hashes with actual build evidence.
The manifest fixes both source and binary hashes, project libraries, compiler/options and common observation patch.
It permits only exact CPU-policy leaves or individually audited `main.cpp`, `realtime_source.cpp`, and
`realtime_source.h` driver hashes. Wildcards and whole-object exclusions are rejected.
The report retains both original comparison groups and lists every accepted and rejected difference.
Models, algorithm, input, budget, queue, dependencies, measurement rules and integrity gates remain exact.
Each side must still have three independent qualified runs from one build.

The reference is labelled **4871442 algorithm baseline + common observation patch**. Its control, tracking,
YOLO and logger remain unbound, with the initial CPU 0–7 range; only supplier uses 4.
The candidate is a distinct build with the pinned policy. A single binary with two settings is insufficient.
Freeze the treatment manifest before the first timed run. Keep all failed, slow and zero-box planned rounds.
Use alternating reference/candidate order per clip. This change requires three fresh rounds per side for each
of test-03's `[20,35)`, `[100,130)`, and `[165,end)` clips. Test-02 remains explicitly skipped by the user.
A successful placement check does not establish a performance improvement. Without declared thresholds,
the result is `not_set` (未设置判定规则). No ground-truth annotation means no accuracy conclusion.

The shared source-support fix is required in both affinity-experiment builds. Preserve its exact patch
separately from the common observation patch. Label the reference “4871442 algorithm baseline + common
observation patch + common source-support fix”; the reference still has no managed role binding.
Deep-copy production APIs and callers retain their original behavior. Timing and integrity limits
remain unchanged, and earlier failed or diagnostic runs never replace the final frozen comparison.

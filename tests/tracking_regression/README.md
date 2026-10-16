# Fixed RK3576 tracking regression

This test replays `wlcb-1513-loss-recovery`, the half-open `[55,75)` second interval from `test-01-20260929-1513`. It preserves 1,200 decoded luminance frames and their original timing. The first frame is due 15.721 ms after the interval boundary. It uses the production `DroneTrackerNode`, real RKNN models, and the production strategy that drains queued frames and processes only the latest frame. The existing sequential `rk3576_tracker_replay CONFIG.yaml` entry remains independent and unchanged.

Production-board tool acceptance completed with three qualified reference runs and three independent candidate repeats using the same build. All 1,200 frames were verified and supplied in each run. The baseline is frozen and Chinese/JSON/CSV reports are available. This establishes repeatability and comparison-tool behavior, not an optimization benefit. See [acceptance.md](acceptance.md) for evidence, retained failures and restored production state. The synchronized behavior contract is [the main specification](../../openspec/specs/rk3576-tracking-regression/spec.md); the [archived change and verification report](../../openspec/changes/archive/2026-10-13-add-rk3576-tracking-regression-comparison/verification-report.md) retain the implementation decisions and independent checks.

A **qualified run** has complete, healthy evidence and identical fixed inputs. Qualification does not mean acceptable performance or correct target tracking. Performance passes only when an explicit, versioned threshold file defines the rule. There are no human target annotations, so these tools do not report accuracy. Model scores and overlap between model boxes are not evidence of correctness.

## Fixed input preparation

Set `INPUT_ROOT` to the directory containing `test-01-20260929-1513`. Use an external `RESULT_ROOT` for large inputs and outputs. Never modify the source recording or submit videos, models, or run logs to Git.

```bash
python3 tests/tracking_regression/prepare_fixture.py \
  --input-root "$INPUT_ROOT" --output "$RESULT_ROOT/fixtures/wlcb-1513-loss-recovery"
python3 tests/tracking_regression/verify_fixture.py \
  "$RESULT_ROOT/fixtures/wlcb-1513-loss-recovery" --source-root "$INPUT_ROOT"
```

Preparation verifies the frozen source video and both original frame logs. It decodes the original Y (luminance) plane directly, without a color round trip. FFV1, a lossless video codec, stores those pixels in `clip.mkv`. Every decoded frame has its own SHA-256 checksum. Playback uses integer microseconds from `frames.jsonl`, not the container frame rate.

The source ROS bag is a SQLite recording of messages. Preparation reads `/fpv/gimbal` as `sensor_msgs/msg/Imu` using its original `header.stamp`. It preserves all numeric message content, storage timestamps and serialized-source fingerprints. The frame log's constant `timestamp_us - pts_us` establishes the shared epoch. The latest attitude before 55 seconds is required. No current production configuration or synthesized attitude is substituted. `session.json` freezes effective algorithm parameters and model content fingerprints.

An existing output directory is rejected. The resulting directory can move between machines: its checksums identify content, while `--source-root` locates retained originals for a full provenance check. At run time the copied fixture is rechecked before model loading, including dimensions, identities, monotonic times and every decoded pixel. Source hashes remain recorded after migration.

```text
fixtures/wlcb-1513-loss-recovery/
  fixture.json clip.mkv frames.jsonl attitude.jsonl algorithm.yaml
runs/GROUP/RUN/
  identity.json preparation.json input-clock.json input.jsonl events.bin events.jsonl
  frames.csv messages.csv metrics.json completion.json
baselines/BASELINE/baseline.json
comparisons/COMPARISON/report.md comparison.json comparison.csv
```

## Real-time run configuration

The ordinary build/test command now defaults to the native x86_64 compiler image. It still produces ARM64 tracker binaries. No package installation or download occurs during normal builds or tests:

```bash
./build.sh --test
# Equivalent established helper:
bash tests/tracking_regression/run_sdk_tests.sh
# Rebuild, then select the four regression CTests:
bash tests/tracking_regression/run_sdk_tests.sh --regression-only
```

The default image `drone-tracker-rk3576:humble-amd64-rknn2.3.2` provides native `ffmpeg` and `ffprobe`. Both tools are checked before compilation and passed to CMake explicitly. Synthetic video encoding runs natively while tracker programs run as genuine ARM64 executables under QEMU. `bash tests/tracking_regression/prepare_test_tools.sh` validates those image tools without downloading anything.

The previous ARM64 compiler image remains an explicit fallback:

```bash
bash tests/tracking_regression/prepare_test_tools.sh --backend arm64
./build.sh --backend arm64 --test
```

For ARM64, preparation reuses complete private tools from the selected artifact root or the preserved historical tool directory. If missing, this explicit setup command downloads official FFmpeg 4.4.2 source from `https://ffmpeg.org/releases/ffmpeg-4.4.2.tar.xz`, verifies SHA-256 `af419a7f88adbc56c758ab19b4c708afbcae15ef09606b82b855291f6a6faa93`, and builds private ARM64 tools without replacing system libraries. The ordinary test command fails before compilation with this preparation command when the tools are absent or unusable.

Both entry points accept `--backend amd64|arm64`, an architecture-checked `--image IMAGE`, `--artifacts-dir ROOT`, `--jobs N`, and `--clean`. Build/install/log directories are `ROOT/backends/BACKEND/{build,install,log}`. The root defaults to `.docker-artifacts`; the old top-level `build`, `install`, and `log` directories remain untouched. `--clean` backs up only the selected backend. A lock prevents concurrent commands from mutating that cache. The source and Git metadata are mounted read-only, and build/test containers have no network access. Logs are `log/build-console.log` and `log/test-console.log` below the selected backend.

The amd64 workflow keeps native Python for code generation and pure script checks. The regression Python CTest uses `target_python.sh` to load genuine ARM64 ROS Python extensions with the target loader and target-only library paths. Its architecture-aware `ldd` helper inspects ARM executables with that same target loader. Emulated subprocesses explicitly select `QEMU_CPU=cortex-a72`, matching the production board's A72 CPU rather than the emulator's maximum-feature model. Native compiler subprocesses retain their normal library environment. No test deadline or acceptance assertion is widened. A failed build or test is returned to the caller; it never silently reruns on the other backend.

The SDK Python coordinator runs 52 portable cases on native Python and the actual ARM ROS middleware identity case on genuine ARM64 Python. Every discovered test ID must run exactly once, with no skipped case. Both groups share the existing 30-second CTest deadline. ARM Python children explicitly reenter the selected emulator. QEMU 8.2 does not implement the child-subreaper syscall; process-supervision cases exercise the native Linux kernel.

The amd64 test command additionally builds and runs `tracking_regression_native_resource_limit` with the actual native kernel's address-space limit. Its ARM source suite separately verifies a real, deterministic cache-allocation exception with the original rejection/evidence/no-clock assertions. The ARM64-image fallback retains `tracking_regression_os_resource_limit` inside its own CTest suite. On a true ARM host that is a normal OS-limit check. On this x86 host it runs under QEMU, which can ignore guest `RLIMIT_AS`; that assertion remains enabled and fails honestly rather than being omitted or reported as native coverage. `CMAKE_CROSSCOMPILING=false` inside an emulated ARM image does not establish native execution.

For a test build outside the source directory, use `./build.sh --test --artifacts-dir /tmp/tracker-validation`. To package that result, explicitly pass `deployment/package.sh /tmp/tracker-release --install-root /tmp/tracker-validation/backends/amd64/install/drone_tracker --model-root /absolute/models`; the deployment packager's legacy default is intentionally not redirected. See [the build guide](../../dockerfiles/README.amd64.md) for image preparation, provenance, and device-validation limits.

The internal real-time executable is `rk3576_tracking_regression`. Use `run_batch.py` to verify the immutable files, frame map and attitudes and create its per-run certificate. The executable rechecks certificate-bound file hashes, decodes all frames into memory and verifies each actual cached pixel hash before constructing the models and rejects existing input outputs. This test-only source does not add H.264 or FFV1 support to the production video factory. OSD (on-screen display), image visualization, recording, historical prediction timestamps and simulated time are disabled. Box publication remains enabled. Observation uses the fixed `basic` diagnostic mode.

Each run starts a new process with empty detection request/result slots, frame history and tracking state. The source independently schedules original timestamps with a monotonic clock, which is unaffected by system clock adjustment. Attitudes are delivered to the production callback only when due. The system clock is mapped to the same origin for frame and prediction timestamps. A system-clock jump invalidates a run.

Choose CPU affinity outside the batch tool and freeze it before the first baseline. The tool records the inherited mask and does not expand it. On the measured board the adb shell inherited Cortex-A53 cores 0–3. An all-core example is `unset DRONE_TRACKER_CPU_CONFIG`, then `taskset -c 0-7 python3 tests/tracking_regression/run_batch.py ...`. If a role-pinned CPU configuration is intentionally enabled, record it and verify actual node/decoder thread masks as well as the controller mask. The all-core example describes the policy used for the qualified full-predecode cohorts. The earlier streaming all-core attempt failed qualification and remains separate historical evidence.

Use an explicit versioned run configuration. `max_supply_lateness_us` is the maximum permitted input-supply delay, in microseconds. Set it before the first official baseline and freeze it. Never widen it after observing candidate results. Queue capacity and the `predecode_all` input mode also belong to the comparison group. Configuration version 2 requires this input mode and rejects the former `prefetch_frames` field.

Each attempt follows this sequence:

1. Verify frozen file hashes, frame identities/times and attitudes.
2. Decode all 1,200 FFV1 frames with eight native slice threads. Copy their Gray8 plane with its row stride and verify each retained frame against its recorded SHA-256. This single decode performs both preparation and pixel verification.
3. Record `preparation.json`, release codec resources and construct the real models.
4. Establish the playback clock and supply independent pixel copies at the original frame deadlines for 20 seconds. The supplier never decodes during this measured window.

The retained pixel cache occupies exactly 2,764,800,000 bytes (about 2.575 GiB). There is no 256 MiB ceiling or replacement fixed memory ceiling. Models, decoder resources and queued copies need additional memory. `preparation.json` records readiness, verified count, cache bytes, predecode duration, process resident/peak memory and OS-visible total/available memory. Allocation failure, decode/count error or pixel mismatch rejects the attempt before models and playback. An interrupted preparation cannot qualify.

```json
{"schema_version":2,"input_mode":"predecode_all","max_supply_lateness_us":50000,"queue_capacity":32,
 "clock_jump_tolerance_us":5000,"ros_domain_id":83}
```

The 50 ms supply budget above is the frozen acceptance setting for this case. Verify that the chosen production-board workload can supply frames within the frozen budget. Supply delay, a full queue, and consumer skips are separate observations.

## Reference, freeze, candidate, compare

Run these commands on the same specific HZ-RK3576_WuKong production board. Use system RKNN Runtime 2.3.2 and RKNPU driver 0.9.8. Keep model files outside the repository; their paths must resolve to the recorded content fingerprints. The batch controller uses an isolated ROS communication domain, a unique diagnostic socket and new output directories. It never stops formal services or changes CPU policy, deployed releases or Runtime. If a maintenance window is needed, an operator must establish a restoration guard and restore the original services after the test.

```bash
python3 tests/tracking_regression/run_batch.py \
  --fixture "$RESULT_ROOT/fixtures/wlcb-1513-loss-recovery" \
  --config "$RUN_CONFIG" --binary "$INSTALL/lib/drone_tracker/rk3576_tracking_regression" \
  --worker "$INSTALL/lib/drone_tracker/rk3576_diagnostic_worker" \
  --repo "$SOURCE_REPO" --output "$RESULT_ROOT/runs/group-a/reference" --rounds 3
python3 tests/tracking_regression/baseline.py \
  --output "$RESULT_ROOT/baselines/reference-a" "$RESULT_ROOT"/runs/group-a/reference/run-*
# Rebuild the candidate from its recorded source; keep group conditions identical.
python3 tests/tracking_regression/run_batch.py \
  --fixture "$RESULT_ROOT/fixtures/wlcb-1513-loss-recovery" \
  --config "$RUN_CONFIG" --binary "$CANDIDATE_INSTALL/lib/drone_tracker/rk3576_tracking_regression" \
  --worker "$CANDIDATE_INSTALL/lib/drone_tracker/rk3576_diagnostic_worker" \
  --repo "$SOURCE_REPO" --output "$RESULT_ROOT/runs/group-a/candidate" --rounds 3
python3 tests/tracking_regression/compare.py "$RESULT_ROOT/baselines/reference-a" \
  --output "$RESULT_ROOT/comparisons/candidate-a" "$RESULT_ROOT"/runs/group-a/candidate/run-*
```

Predeclare at least three rounds. Every attempt is saved, including failures and slow qualified runs. An eligibility failure may be retried with an explicit reason; do not select only fast attempts. Both reference and candidate require at least three qualified runs. Changing models, algorithm parameters, board, workload, supply budget or metric calculation requires a new comparison group. Code and executable versions may differ.

The external controller counts preflight, full predecode/pixel verification, model loading, playback and shutdown within a single 120-second limit. At 110 seconds it begins stopping the isolated processes, and kills remaining descendants before the total deadline. A timeout retains an incomplete completion record. Full predecode and model loading are preparation, separate from individual tracker update duration. The 20-second measurement origin is established only after the entire cache and models are ready. A slow decode may consume preparation time; it never shifts frame timestamps or slows measured playback.

Baseline freezing is explicit and immutable. Candidate generation and comparison cannot rewrite it. Reports include each round, median and range, candidate-minus-reference differences and zero-base explanations. Optional `--thresholds FILE` applies fixed, versioned rules. With no rules the report states that no performance decision rules were set. The historical 388 ms observation is background only and cannot become this baseline.

See [diagnostic_contract.md](diagnostic_contract.md) for event identities and health gates, [metrics.md](metrics.md) for formulas and availability, and [acceptance.md](acceptance.md) for executed evidence and limits.

## Content identity and relocated dependencies

The identity format records raw file hashes and paths as provenance, meaning evidence of where files came from. Its comparison conditions use semantic fixture/algorithm digests: all pixels, frame times, attitude, effective algorithm values, model content, encoding and preparation conditions remain fixed. Only known source/model locator fields are omitted. The raw fixture and algorithm files remain immutable and retain their original checksums.

A model can move without rewriting the fixture. Add a `model_paths` object to the run configuration, mapping any of `tracker.yolo_engine`, `tracker.nano_template_backbone`, `tracker.nano_search_backbone`, and `tracker.nano_head` to an absolute runtime path. For example:

```json
"model_paths": {
  "tracker.yolo_engine": "/external/models/balloon_best_zoo6_int8_fp16_cls.rknn"
}
```

Unmapped models use their recorded paths. Both the controller and the C++ entry verify the actual selected model file against the frozen content hash before model construction. Unknown mapping keys, relative paths or different model bytes are rejected. Mapping paths are provenance, not algorithm changes. Other configuration values remain comparison conditions.

For each actual node/worker executable, `ldd` resolves the direct and transitive ELF library graph under the same inherited environment used for execution. ELF is Linux's executable/library format; a SONAME is the library name used by its dynamic loader. Every resolved external library is hashed by SONAME, including bundled ROS, OpenCV, FFmpeg and the system RKNN Runtime. Paths are saved separately. Missing or unresolved libraries refuse measurement. Known project-built library targets, if installed beside the package executable, belong to code identity instead of the environment comparison group; bundled third-party libraries are still external conditions.

ROS can load its middleware plugin after initial ELF resolution. A separate process queries the resolved `librmw_implementation.so` for the effective implementation and records newly loaded shared-library content. The requested `RMW_IMPLEMENTATION` selector and resolved paths are provenance; the effective implementation and content are comparison conditions. This probe does not start a ROS node or load tracking models. It covers middleware selection, not every optional plugin an application could load later. No exhaustive runtime profiling is claimed.

Identity version 2 distinguishes these repaired conditions from earlier raw-file/library identities. The freeze/comparison tools reject missing or legacy identity versions and missing actual node/worker dependency fingerprints. Previous failed qualification evidence is preserved as originally measured and is not rewritten to the new format. These changes require a new comparison group for future measurements.

Concurrent workload identity uses userspace process names and formal service states. Kernel threads are identified from the kernel's `PF_KTHREAD` process flag and retained as separate observations, because their worker names can change without an application workload change. Controller ancestry and known scripts/executables in the private test tree are excluded from the workload group. Unrelated Python, shell and other userspace processes remain conditions; adding one makes the groups differ. Full process metadata remains in `workload_observation` for review. This is a process-name workload check, not exhaustive CPU or I/O profiling.

## Pending replacement and historical comparisons

The current detector admits the newest valid input into its one unstarted slot, while
leaving the executing request and unread-result protection intact. Each replacement
emits a supersession link. The tracker retains value-only source boxes, times and round
identity with every outstanding request, independently of image-history retention.
Raw `SourceContext` records separate source comparison availability from recovery pixels.
See the interpretation-2 payload contract in `diagnostic_contract.md` and the metric-2
extensions in `metrics.md`. Historical logs keep their old metric meanings; fields they
do not contain remain unavailable. Compare both full/core windows and retain rejected
attempts, the original supply budget and 120-second total deadline.

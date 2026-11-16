# Verification step 1 — align-rk3576-recording-tracking-start

Date: 2026-11-16 (UTC+08:00). Repository: `/home/mzh/nickluo/drone_tracker`.
Branch: `fix/align-rk3576-recording-tracking-start`.
Source revision: `2ebc49668af48e9db1dc01e2370d87a246f82567`, with the November 16 acceptance artifacts and task completion uncommitted.

This fresh verification follows the repository-local `openspec-verify-change` skill. The CLI resolves the existing local OpenSpec root and the `spec-driven` schema. All concrete context files were read: proposal, design, tasks, and three delta specifications. CLI task completion was checked against source, fixtures, retained software tests, and production evidence. Planning completion alone was not used as acceptance evidence.

## Summary scorecard

| Dimension | Result |
| --- | --- |
| Completeness | **33/33 tasks complete.** All **15 ADDED requirements** have implementation and evidence. No MODIFIED, REMOVED, or RENAMED requirements. Task Completion and Spec Coverage passed. |
| Correctness | Requirement Implementation Mapping passed for **15/15 requirements**. Scenario Coverage passed for **42/42 scenarios**, using the fixtures and evidence mapped below. Fresh readback confirms exact first-YOLO and successful initialization sources in **20/20 production sessions**. |
| Coherence | Design Adherence passed for all **seven decisions**. Code Pattern Consistency was reviewed against the implementation diff from `f08399533b9f548666b52ef9e16694e4c1ce0835`, its neighboring code, and owning guides. No material divergence found. |

**Issue counts: CRITICAL 0 / WARNING 0 / SUGGESTION 0.**

All six defined verification checks ran. No check is marked Not verified or Not applicable. The execution limits below distinguish reused test evidence, fresh checks, and out-of-scope hardware properties.

## Issues and repair disposition

- CRITICAL: none.
- WARNING: none.
- SUGGESTION: none.

The November 14 blocker was successful real-target initialization on the production board. The new live-balloon evidence supplies that event and exact saved-frame membership in every round. It resolves that blocker without changing the historical no-target results. The earlier inspector fixes also pass the fresh regression suite.

No Apply repair was needed in this step because verification found no CRITICAL or WARNING issue. No implementation file, task checkbox, acceptance criterion, or specification was changed by this verification step.

## Requirement and scenario coverage

Paths below are repository-relative. Scenario counts total 42. “Verified-written” means that the exact source identity matches saved frame metadata and the corresponding segment decodes with the expected frame count. It does not assert prior power-loss durability.

| ADDED requirement | Scenarios reviewed | Implementation and test/evidence mapping |
| --- | --- | --- |
| Frames expose optional immutable recording receipts | 2: shared consumers; later recording status | `src/frame/frame.h:79`, `src/frame/recording_receipt.h:16`, and const receipt storage in `src/platform/rk3576/host_frame.cpp:31`. `recordingReceipts()` in `src/platform/rk3576/tests/nanotrack_preprocess_test.cpp:226` checks immutable request values, unchanged original frames, shared pixels and original timestamps. |
| Recording identities remain usable without diagnostics | 2: diagnostics off; driver sequence wrap | Request identity includes stream, round and generation. The capture id is 64-bit and populated independently of diagnostic emission in `src/platform/rk3576/rk3576_camera_capture_base.cpp:398`. Frame and inspector fixtures distinguish different capture ids sharing sequence 17. The production acceptance configuration disables diagnostics. |
| Optional receipts preserve existing backend compatibility | 3: preprocessing; unaffected backend; retained view | The default accessor returns unavailable. `withRecordingReceipt()` retains the concrete `HostFrame` and shares reference-counted pixels. The preprocessing fixture checks concrete compatibility, successful model preprocessing, and surviving read views after frame references are released. Public metadata contains no hardware SDK types. |
| Recording reports acceptance for the exact current candidate | 3: OFF after staging; full queue; mid-visit attachment | `src/platform/rk3576/recording/shared_slots.h:143` resets and publishes each candidate's disposition. `SessionDispatcher` pins one receiver for the whole visit. Capture attaches the receipt only after deferred commit at `src/platform/rk3576/ar0234_camera_frame_source.cpp:197`. `dispatcherBoundary()` in `recording/session_test.cpp:79` controls all three orderings. |
| Recording status identifies the current request synchronously | 2: previous completion; unreclaimed worker | `src/platform/rk3576/sc233hgs_camera_frame_source.cpp:50` registers a fresh request before asynchronous preparation. `recordingRequestStatus()` scopes actual status to that identity. `liveCameraControl()` checks synchronous pending and replacement tokens; existing lifecycle fixtures check unreclaimed-worker refusal. |
| Accepted startup frames enter ordinary video immediately | 2: no target; preparation excluded | Recorder input publication remains independent of detector output. The dispatcher has no pre-acceptance pixel history. Session fixtures and the retained November 14 no-target sessions establish ordinary persistence without initialization. November 16 sessions also contain accepted frames before later successful initialization. |
| Startup coordination preserves existing resource and failure limits | 2: continuous rejection; preparation failure | `RecordingSlots` retains four slots and the 64 MiB raw limit. `Session::start()` retains its three-second budget; close and warmup remain separate. Existing `session_test.cpp` lifecycle checks cover preparation/close/synchronization timeouts, unreclaimed resources, independent warmup, and continuously rejected qualified inputs reaching progress failure. |
| Startup coverage evidence distinguishes acceptance from saved video | 4: historical source; accepted but unwritten; diagnostics off; no initialization | The detector hook at `src/hybrid_tracker/yolo_detector.cpp:288` observes actual execution. The successful hook at `hybrid_tracker.cpp:284` uses `hist.frame`. Tracker fixtures compare successful diagnostic initialization events and prove overwritten submissions are absent from executed inputs. Inspector regressions cover accepted-but-unwritten and failed sessions. No-target sessions retain explicit absent initialization. |
| Coverage metadata remains bounded and compatible | 3: omissions; historical metadata; written verification | `src/frame/startup_collector.h` retains at most two sources. `recording/metadata.h` serializes separate observation-point counters through bounded session-end metadata. `inspect.cpp` streams bounded sidecars and matches session/request/frame keys. Fresh tests cover missing fields, partial sources, rejected sources, old request generations, sequence wrap, unwritten frames, failed video and general-decoder readback. |
| Initial algorithm inputs have matching recording acceptance | 3: frames 8884–8891; old queued frame; task replacement | `admitStartupFrame()` at `src/drone_tracker_node.cpp:440` checks accepted disposition, request identity, capture id and original timestamp. It runs at dispatch and consumption. `recordingStartupAdmission()` at `src/tests/ros_video_test.cpp:1350` imposes the eight-frame preparation boundary and confirms first execution at capture 8892 or later. Dispatcher and detector replacement fixtures independently check the replacement paths. |
| Startup filtering ends on successful tracker initialization | 4: negative detection; failed initialization; historical success; later recovery | Node `processFrames()` releases the filter only after a successful current-round initialization observation. ROS fixtures retain filtering through negative detections and failed templates, then allow rejected receipts after successful initialization. Only round setup enables the filter; the existing target-loss and recovery state machine does not re-enable it. |
| Startup filtering preserves preview and video independence | 3: delayed preparation; no target; rejected paired candidate | Starting presentation continues before first admission. Withheld frames never become tasks or publish algorithm observations/debug images. After processing begins, display remains paired with its consumed frame. Continuous-camera fixtures check preview, telemetry, slow inference and paired output; no-target production video demonstrates recording independence. |
| Recording failure releases startup filtering for the current round | 3: preparation timeout; unreclaimed old worker; failure before initialization | Only a matching current-request terminal status releases admission in `admitStartupFrame()`. Failure permanently disables filtering for that round. The ROS fallback fixture combines this with session failure/refusal tests. Receipt and stale-generation fixtures prevent old status or old accepted frames from authorizing the new request. |
| OFF invalidates startup eligibility immediately | 3: OFF during preparation; rapid restart; display response | OFF closes control intent, discards pending work, advances the presentation version, freezes evidence and stops acceptance without waiting for inference or storage. `recordingStartupCancellation()` checks continued preview and generation replacement. In-flight inference and collector fixtures check late completion. November 16 has 20 command-to-OFF-display samples, 5.137–27.516 ms, below 500 ms. |
| Startup acceptance is verified by exact saved-frame membership | 3: normal completion; no initialization; production evidence | Fresh exact-key assertions and freshly compiled inspector/FFmpeg readback verify **20/20 first executed YOLO sources and 20/20 successful historical initialization sources**. Fifteen rounds use different first-YOLO and initialization capture ids. Earlier no-target sessions explicitly report initialization not applicable. The actual identified production run meets all round/workload/deadline and identity requirements. |

## Design adherence and compatibility

1. Admission remains in node orchestration. Every initial candidate is checked at dispatch and consumption until successful initialization. Later loss does not reactivate startup filtering.
2. Receipt metadata preserves the existing RK3576 concrete host frame and reference-counted pixels. No additional pixel copy or hardware type enters the shared frame interface.
3. Final recording publication determines acceptance. A visit retains its original receiver through commit and receipt retrieval. Driver lifetime and final capture checks precede publication.
4. ON synchronously establishes a new stream/round/request identity. Preparation and terminal status are scoped to that request. The existing preparation, close and health budgets remain authoritative.
5. Preview, telemetry, OFF and failure fallback use existing control and display boundaries. Public tracker states, ROS contracts, CPU roles and production `visualized_result=false` remain unchanged. Recording-disabled, AR0234, file and OSD-disabled inputs retain their existing lifecycles.
6. Hooks report values and do not decide model execution or tracking arbitration. Detector execution copies its hook under the detector mutex. Callbacks retain only the bounded collector. Node control takes its mutex before the collector mutex; callbacks take only the collector mutex. OFF freezes collection, and inference cleanup precedes round replacement. File writing remains on the recording path.
7. Existing controlled fixtures cover preparation, receiver attachment, publication, replacement and cancellation. Production evidence is retained outside disposable caches and is distinguished from native and emulated software checks.

The owning platform/recording guides explain acceptance versus persistence, historical initialization, immediate video, failure fallback and exact readback. Their concrete examples match the implementation. No model, preprocessing calculation, inference runtime, embedded external dependency, public state value, parameter default or hardware dependency boundary changed in this change's implementation diff.

## Independent evidence and software identity checks

Retained root: `/home/mzh/nickluo/validation-retained/align-recording-start-20261116`.

Fresh SHA-256 assertions checked all **259 entries** in its existing `SHA256.json`; there were zero mismatches. The previous release's **33-file source manifest** was compared with this checkout. Only `inspect.cpp` and `inspect_test.py` differ, exactly as disclosed in the acceptance report. The node, worker, portable receipt, capture, collector and shared-memory layout sources match that manifest.

The current inspector and test fingerprints match the November 14 corrected sources:

- `inspect.cpp`: `bf5e72503761bc22385e4e3a5f1f31cb571b4329e401c85a692bb8ad79622a1b`.
- `inspect_test.py`: `7214faafa393d89953022273a8c6c2973a617d8071d4e53070b078611afa9419`.
- Fresh native inspector: `321bdeb1e05487ac4a38c97a658434c8aad890e385c6bdecac63cdeed06fbbcc`, also matching the inspector used by job 1.

The retained release and current offline inspector are preserved together with executable/model/configuration/CPU-policy identities. A repackaged runtime release is not claimed. The inspector-only changes do not change the parent/worker shared-memory contract. Fresh video readback used the current inspector rather than the release's older inspector.

Independent assertions also checked all 20 original sessions. Each has a successful completion, no startup fallback, and exactly one sidecar match for each source. All six source keys match: `stream_id`, `tracking_round`, `request_generation`, `capture_id`, `timestamp_us`, and `sequence`. Session and startup request keys also match. Rounds cover exactly 1 through 20. The raw controller reports success, node exit 0, 64.21871815 seconds between ON/OFF commands and 75.467285799 seconds total, satisfying the required 60-second workload and 180-second bound.

The retained board evidence identifies `HZ-RK3576_WuKong Board`, Ubuntu 22.04.5 LTS ARM64, kernel `6.1.141-rt36`, GCC 11.4.0, SC233HGS NV12 capture at 1920×1200/60 Hz, and actual camera/media/NPU/display topology. Its system RKNN Runtime is 2.3.2 with the pinned checksum `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`; the driver is 0.9.8. All four existing model hashes, executable hashes, configuration and CPU policy are recorded in [validation-20261116.md](validation-20261116.md). Resource, display, processing, startup and OFF measurements are retained with raw inputs. No host result is used as a replacement for board execution.

## Fresh commands and results

Fresh compact evidence is under [evidence/20261116/verify-fix-step1](evidence/20261116/verify-fix-step1/). Exact entry points/statuses are in `commands.json`; identity assertions, source hashes, native regression output and a separate readback report are retained there.

| Check | Result |
| --- | --- |
| `openspec list --json`, `status --change align-rk3576-recording-tracking-start --json`, and `instructions apply --change align-rk3576-recording-tracking-start --json` | Exit 0; local root, concrete artifacts, 33/33 tasks, no unavailable tracking files. |
| `docker image inspect drone-tracker-rk3576:humble-amd64-rknn2.3.2` | Exit 0; image `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`. |
| Fresh native compile: `g++ -std=c++17 -Wall -Wextra -Wpedantic -I/src/src -I/src/src/hybrid_tracker $(pkg-config --cflags opencv4) /src/src/platform/rk3576/recording/inspect.cpp -lyaml-cpp -o /out/recording-inspect-step1` | Exit 0; no compiler warnings. |
| `python3 /src/src/platform/rk3576/recording/inspect_test.py /out/recording-inspect-step1` | Exit 0; all identity, failure, compatibility and decoder regressions passed. |
| `/out/recording-inspect-step1 /recordings /out/inspection-readback.jsonl --verify-video` | Exit 0; 20 sessions, zero inspection errors; both sources 20/20 `verified-written`. |
| Conda base Python SHA-256 and exact-key assertions on retained originals | Exit 0; 259 retained hashes, 33 release-source files, all 40 exact source memberships checked. |
| `openspec validate align-rk3576-recording-tracking-start --strict` | Exit 0. |
| `git diff --check` | Exit 0. |

Docker execution was offline with read-only source and original-recording mounts. Generated binaries and reports used a separate writable output mount. Native compilation and FFmpeg ran on this x86_64 host; hardware timing comes from the retained production run. General host assertions used `/home/mzh/miniconda3/bin/python`.

## Existing integration evidence and execution limits

The retained supported integration run, `./build.sh --test --quadrotor-msgs /home/mzh/nickluo/quadrotor_msgs`, exited 0 with **73/73 CTests**, plus the separate native resource-limit test. Its actual log and statuses are under `evidence/20261114/verify-fix-step1/`. This evidence was inspected rather than rerun because no implementation changed after that successful run. It used the selected amd64 cross-compiler backend with QEMU for ARM64 execution; no automatic backend switch occurred.

No board workload or board command was repeated in this verification step. Current production acceptance had just completed and its raw hashes and saved bytes were independently verified. Job 1's restoration evidence shows `drone-tracker.service`, `drone-tracker-diagnostics.service` and `drone-interceptor.service` active, with installed release/configuration/CPU-policy/system Runtime unchanged. Verification made no service or deployment changes.

No C++ files were edited in this step. `clang-tidy` and `clangd` are unavailable on the host; no lint or language-server pass is claimed. Matching Jetson hardware/SDK execution, flight behavior, long-duration stability, physical panel latency and real power-loss durability were not executed. Those properties are outside this change's bounded acceptance criteria. The six skill-defined specification checks have no skipped items.

## Final assessment

**All defined verification checks passed. CRITICAL 0 / WARNING 0 / SUGGESTION 0.** The change satisfies this verification step's completion condition and is ready for archival review. This report does not perform synchronization, archival, commit or persistent deployment. Only this report and its separate verification evidence were added by the step.

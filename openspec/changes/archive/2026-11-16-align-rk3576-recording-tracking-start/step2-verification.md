# Verification step 2 — align-rk3576-recording-tracking-start

Date: 2026-11-14. Worktree: `/home/mzh/nickluo/worktrees/drone_tracker-align-recording-start`.
Branch: `fix/align-rk3576-recording-tracking-start`. HEAD: `f08399533b9f548666b52ef9e16694e4c1ce0835`, with the existing uncommitted implementation and step 1 inspector fixes.

This is an independent full verification using the repo-local `openspec-verify-change` skill. The proposal, design, all three delta specifications, and tasks were read from the CLI's concrete context paths. Implementation, fixtures, existing integration results, retained production identities and source-to-video evidence were reviewed. The schema is `spec-driven`; the CLI reports 32/33 tasks complete. Its planning-complete status does not establish implementation completion.

## Scorecard

| Dimension | Result |
| --- | --- |
| Completeness | 32/33 tasks complete. All 15 ADDED requirements have implementation. One CRITICAL remains: task 6.3. No MODIFIED, REMOVED or RENAMED requirements. |
| Correctness | All 15 requirements and 42 scenarios mapped to implementation and test/evidence paths. The two previous inspector warnings independently pass fresh regressions. Successful real-target initialization membership on the required board remains **Not verified** because that event never occurred in the available scene. This is the task 6.3 issue, counted once. |
| Coherence | All seven design decisions followed in source review. Code Pattern Consistency checked against the full identified implementation and documentation diff; no significant deviation found. |

**Issue counts: CRITICAL 1, WARNING 0, SUGGESTION 0.**

## Issues

### C1 — CRITICAL: complete task 6.3 with a real target

Reference: `tasks.md:53`; `specs/rk3576-continuous-hdmi-stream/spec.md:91` and its normal-completed-round and production-evidence scenarios.

The retained production workload meets the 20-round, 60-second combined-workload and bounded-run conditions. Fresh readback with the corrected inspector confirms all 20 first executed YOLO sources in their exact session metadata and readable video. Each initialization source is explicitly absent. A scene without a target balloon is expected to yield no detection; this is not an algorithm defect. It cannot establish successful historical initialization and its saved-frame membership on the production board.

Recommendation: place a real target in the identified production camera scene. Run the remaining bounded acceptance with the matching release and approved existing models. Verify the exact first successful historical initialization source against that session's frame metadata and decoded video. Retain source, executable, model, configuration, runtime and board identities with commands and exit statuses. Mark task 6.3 complete only after that evidence exists.

WARNING: none. SUGGESTION: none.

## Requirement and scenario coverage

Counts in the second column account for all 42 scenarios. Paths are repository-relative. Source/fixture coverage does not imply that the remaining real-target board event was executed.

| Requirement | Scenarios | Independently reviewed evidence |
| --- | --- | --- |
| Optional immutable frame receipts | 2: shared consumers; later status change | `src/frame/frame.h:79`, `recording_receipt.h:16`, concrete const receipt storage in `src/platform/rk3576/host_frame.cpp:31`; `recordingReceipts()` verifies unchanged original and shared pixels. |
| Identity without diagnostics | 2: diagnostics off; driver sequence wrap | Portable request and 64-bit capture identity; capture populates the value independently of diagnostics. Frame and inspector fixtures distinguish capture ids sharing sequence 17; production configuration has diagnostics off. |
| Existing backend compatibility | 3: preprocessing; unaffected backend; retained view | `withRecordingReceipt()` preserves `HostFrame` and reference-counted `cv::Mat`; default accessor is unavailable. `nanotrack_preprocess_test.cpp:226` checks concrete preprocessing and surviving read views. |
| Exact candidate acceptance | 3: OFF after staging; queue rejection; mid-visit attachment | `shared_slots.h:143` through final publish; `session.h:72` pins one receiver; capture attaches only after deferred commit at `ar0234_camera_frame_source.cpp:197`. `dispatcherBoundary()` controls all three boundaries. |
| Synchronous request status | 2: previous completion; unreclaimed worker | `sc233hgs_camera_frame_source.cpp:50` synchronously establishes a generation; status matches the active token. `liveCameraControl()` tests pending and replacement identity; existing session fixtures test unreclaimed resources. |
| Immediate ordinary startup video | 2: no target; preparation excluded | Accepted NV12 inputs use the existing encoder queue independently of tracking. Dispatcher retains no preview history. Session fixtures and 20 retained no-target production sessions cover ordinary persistence. |
| Existing resource and failure limits | 2: continuous rejection; failed preparation | Four raw slots and 64 MiB limit remain. `Session::start()` retains three-second preparation; stop retains its separate budget; existing progress monitor remains active. Session fixtures exercise timeout, refusal, warmup separation and rejection. |
| Startup source evidence | 4: historical source; unwritten acceptance; diagnostics off; absent initialization | Execution hook at `yolo_detector.cpp:288`, successful initialization hook at `hybrid_tracker.cpp:284`, bounded `StartupCollector`. Tracker fixtures compare real execution rather than overwritten submissions and successful diagnostic initialization events. Inspector regression covers failure and absence. |
| Bounded compatible coverage metadata | 3: omissions; historical metadata; written verification | Fixed-size evidence serialized by `metadata.h:47` through the existing bounded session summary. Inspector streams capped sidecars, matches all request/frame fields and optionally decodes. Fresh tests independently cover missing keys, malformed partial sources, rejected sources, wrong requests, wrapping sequences, unwritten and failed recordings. |
| Accepted initial algorithm inputs | 3: frames 8884–8891; old queued frame; replacement | `admitStartupFrame()` checks acceptance, current token, capture id and timestamp at dispatch and consumption. `recordingStartupAdmission()` imposes the exact preparation barrier and confirms first executed capture is at least 8892; existing latest-task fixtures cover replacement. |
| Release only after initialization | 4: miss; failed initialization; historical success; later recovery | `processFrames()` releases after current-round observed success. ROS fixture retains filtering through misses and failed templates, then consumes rejected receipts after successful initialization. Only round setup enables the filter; tracker loss/recovery does not reactivate it. |
| Preview and video independence | 3: preparation delay; no target; rejected paired candidate | Starting preview remains active before admission. After processing starts, rejected frames do not create tracking tasks or receive previous-image boxes. Continuous fixtures check preview, telemetry and paired presentation; retained video confirms no-target persistence. |
| Current-request terminal fallback | 3: timeout; old worker; failure before initialization | Node admits current live frames only for matching terminal request status and disables filtering for the remainder of that round. Session failure fixtures plus ROS fallback fixture cover these paths; stale token cannot authorize fallback. |
| Immediate OFF invalidation | 3: preparation OFF; restart; display response | Control clears pending work and presentation version, freezes evidence and closes acceptance. `recordingStartupCancellation()` covers preview continuation, 500 ms response and stale generation. In-flight detector and frozen-collector fixtures cover late callbacks. |
| Exact saved membership | 3: normal completion; no initialization; production evidence | Inspector matches stream, round, request generation, capture id, timestamp and sequence; acceptance and decoder readback are separate. Synthetic success fixture and fresh readback establish software behavior and 20 production YOLO sources. Real-target successful initialization membership remains **Not verified** (C1). |

## Design and pattern consistency

1. The node applies per-frame admission at dispatch and consumption until successful initialization. Model values and algorithm arbitration are unchanged.
2. Receipt-bearing frames retain the existing concrete RK3576 host type and share original pixels.
3. Receipts describe final publication through the receiver pinned for the frame visit. Driver-buffer return and final capture checks still precede commit.
4. ON synchronously establishes request identity. Status is scoped to that request, including preparation failure and unreclaimed workers.
5. Preview, OFF cancellation and fallback use existing control/presentation boundaries. Public tracker-state values, ROS contracts, CPU roles and production debug-image defaults remain unchanged.
6. Observation hooks retain only bounded collector state. Hooks take its mutex; node control takes its own mutex first. Frozen collectors reject late callbacks. Inference cleanup precedes round replacement. Metadata writing remains on the recording worker path.
7. Controlled fixtures cover ordering and failure cases; supported build evidence and dated production evidence are retained separately. Proposed behavior and synthetic results are not used as hardware acceptance.

Owning platform/recording guides explain receipts, startup delay, immediate video, fallback and inspector readback. The root README and architecture guide are consistent with the implementation. Public types introduce no hardware SDK dependency. Embedded external dependencies were not modified.

## Fresh verification commands and results

- `openspec list --json`, `openspec status --change align-rk3576-recording-tracking-start --json`, and `openspec instructions apply --change align-rk3576-recording-tracking-start --json`: exit 0; local root and concrete context confirmed.
- Native inspector compile and regressions in offline image `drone-tracker-rk3576:humble-amd64-rknn2.3.2`: exit 0. Compile command: `g++ -std=c++17 -Wall -Wextra -Wpedantic -I/src/src -I/src/src/hybrid_tracker $(pkg-config --cflags opencv4) /src/src/platform/rk3576/recording/inspect.cpp -lyaml-cpp -o /out/recording-inspect-step2`. Test command: `python3 /src/src/platform/rk3576/recording/inspect_test.py /out/recording-inspect-step2`. No compiler warning was reported. The real FFmpeg fixture passed.
- Corrected inspector against the read-only retained production recording tree, with `--verify-video`: exit 0. Result: 20 sessions, zero inspection errors, first-YOLO **20 verified-written**, first-initialization **20 not-applicable**. Separate report: `evidence/20261114/verify-fix-step2/inspection-readback.jsonl`.
- `openspec validate align-rk3576-recording-tracking-start --strict`: exit 0.
- `git diff --check`: exit 0.

The existing image identity was rechecked: `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`. Native tests ran on the x86_64 host in that image. Conda base Python `/home/mzh/miniconda3/bin/python` aggregated the fresh readback. Inspector source SHA-256 is `bf5e72503761bc22385e4e3a5f1f31cb571b4329e401c85a692bb8ad79622a1b`; test SHA-256 is `7214faafa393d89953022273a8c6c2973a617d8071d4e53070b078611afa9419`. Both exactly match the retained step 1 corrected sources.

The supported integration command `./build.sh --test --quadrotor-msgs /home/mzh/nickluo/quadrotor_msgs` was not repeated because no source changed after its step 1 pass. Its retained log independently confirms exit 0, 73/73 CTests and the separate native resource-limit check. ARM64 tests run through the selected image's QEMU launcher; they are not board timing evidence. No automatic backend switch occurred.

No board command, service mutation or deployment was performed in step 2. Previously retained board evidence remains scoped to its actual HZ-RK3576_WuKong Board, Ubuntu 22.04.5 ARM64/kernel 6.1.141-rt36, approved Runtime 2.3.2 checksum and driver 0.9.8, SC233HGS live no-target camera, and the four model hashes recorded in `validation.md`. That report also retains exact executable/configuration identities, 64.339980447 seconds combined ON workload, 3,743 frames and zero recorder drops. The fresh inspector does not imply a new runtime release or deployment.

An initial unprivileged Docker identity read was denied by the socket; the authorized escalation succeeded. The first host read of the new root-owned exclusive report failed with permission denied; making that new report readable succeeded, and aggregation then exited 0. These housekeeping failures do not represent passing checks. Original recording evidence remained read-only.

## Same-change Apply disposition

After verification found only C1, the repo-local `.agents/skills/openspec-apply-change/SKILL.md` was loaded for the same exact change. Progress remains **32/33**. The remaining task requires an event unavailable in the no-target scene. The skill says, “Only mark a task `- [x]` when its specified behavior is fully implemented, not when it is partially done or deferred,” and directs pausing on blockers. Therefore no task checkbox, requirement or acceptance criterion was changed. No code fix was necessary in this step, and no fresh verification is inferred from this disposition.

## Checks not verified and final assessment

- Applicable production successful real-target initialization and exact saved-frame membership: not executed because the available scene has no target (C1).
- Lint/language-server checks: `clang-tidy` and `clangd` were unavailable in the checked host/image environment; no pass is claimed.
- Matching Jetson hardware/SDK execution, flight behavior, long-duration stability and real power-loss retention were not executed. These are outside this bounded acceptance run and are not additional invented archive prerequisites.

**1 critical issue found. Fix before archiving.** The real-target membership check remains not verified for the stated reason. No warnings or suggestions remain after independent confirmation of the two step 1 inspector fixes. The change is **incomplete**; it is not ready for synchronization or archival. Only this report and its separate readback evidence were added in step 2. No commit, synchronization or archival was performed.

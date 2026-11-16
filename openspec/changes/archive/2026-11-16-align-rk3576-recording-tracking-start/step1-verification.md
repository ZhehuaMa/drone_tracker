# Verification step 1 — align-rk3576-recording-tracking-start

Date: 2026-11-14. Repository: `/home/mzh/nickluo/worktrees/drone_tracker-align-recording-start`.
Branch: `fix/align-rk3576-recording-tracking-start`; base revision: `f08399533b9f548666b52ef9e16694e4c1ce0835`, plus the existing uncommitted implementation and the two inspector fixes described below.

The repo-local `openspec-verify-change` skill was followed first. After findings were identified, the same agent loaded the repo-local `openspec-apply-change` skill and fixed the two actionable inspector defects. The parent process owns any later independent verification. Passing tests below do not mean a fresh full OpenSpec verification was performed after the fixes.

## Initial verification scorecard

| Dimension | Result before fixes |
| --- | --- |
| Completeness | 32/33 tasks complete. All 15 ADDED requirements have implementation. No MODIFIED, REMOVED, or RENAMED requirements. One CRITICAL: task 6.3 remains incomplete. |
| Correctness | All 15 requirements and 42 scenarios inspected. Two WARNING findings concern inspector interpretation of incomplete or rejected source evidence. Successful real-target initialization membership on the production board is not verified; this is the same task 6.3 blocker, counted once. |
| Coherence | All seven design decisions have corresponding implementation. Code pattern consistency reviewed across the identified implementation diff; no significant pattern finding. |

**Initial issue counts: CRITICAL 1, WARNING 2, SUGGESTION 0.**

## Issues and disposition

### C1 — CRITICAL: production initialization acceptance remains incomplete

Reference: `tasks.md:53`, task 6.3; continuous-HDMI delta `spec.md:91`, especially the normal-completed-round and production-evidence scenarios.

The retained production run contains 20 ON/OFF rounds, 64.339980447 seconds of combined ON workload, 3,743 saved frames, no recorder drops, and 20 exact first-YOLO sources verified through metadata and FFmpeg readback. All initialization sources are explicitly absent. The camera scene contains no target balloon, so missing YOLO detections are expected; this is not an inference defect. Successful real-target initialization and its saved-frame membership therefore remain unexecuted on the required board.

Recommendation: provide a real target in the production camera scene and execute the remaining bounded acceptance using the matching programs and existing models. Confirm exact historical initialization-source membership and video readability, retain identities and command statuses, then mark task 6.3 complete. Do not mark it complete from synthetic fixtures or from no-target rounds.

Disposition: remains blocked by the available scene. No checkbox, requirement, or acceptance criterion was weakened. No new board work was performed in this step.

### W1 — WARNING: missing source keys were mistaken for explicit absence

Reference: `src/platform/rk3576/recording/inspect.cpp:147`; camera-recording delta `spec.md:77`.

If startup metadata contained a request generation but omitted `first_yolo` or `first_initialization`, default YAML null nodes made the inspector report `not-applicable`. A missing field means unknown coverage. An explicitly present JSON null means the event did not occur. These are different evidence states.

Recommendation implemented: check that each source key exists before interpreting null. Missing keys retain `unknown`. Regression checks independently omit each source key.

Reproduction against retained pre-fix executable: new regression invocation exited **1**, with `AssertionError: first_yolo`. After the fix, the complete inspector regression passed.

### W2 — WARNING: rejected source evidence was labeled accepted

Reference: `src/platform/rk3576/recording/inspect.cpp:155`; camera-recording delta `spec.md:57`.

A complete source object containing `accepted:false` was initially labeled `accepted` when no completion marker or matching frame record existed. The label contradicted the source's own disposition.

Recommendation implemented: interpret the boolean value and report `failed` coverage for an explicitly unaccepted source. Add a regression before writing any completion marker.

This was detected while examining the same inspector validation path after W1. A separate reproduction skipped W1 assertions and ran against the retained pre-fix executable. It exited **1** at the `accepted:false` assertion. After the fix, the complete inspector regression passed.

## Requirement and scenario mapping

Paths below are repository-relative. Each row accounts for every scenario of the named requirement; counts total 42.

| Requirement | Scenarios inspected | Implementation and evidence |
| --- | --- | --- |
| Generic frame: optional immutable receipts | Shared consumers; status changes after publication (2) | `src/frame/frame.h:79`, `src/frame/recording_receipt.h:16`, `src/platform/rk3576/host_frame.cpp:31`. Const stored receipt, value accessor and shared pixel storage. `recordingReceipts()` in `src/platform/rk3576/tests/nanotrack_preprocess_test.cpp:226` checks immutable copies, original frame, unchanged timestamp and storage. |
| Generic frame: identities without diagnostics | Diagnostics off; driver sequence wrap (2) | Request stream/round/generation and 64-bit capture id in `recording_receipt.h:9`; capture id populated in `rk3576_camera_capture_base.cpp`. `recordingReceipts()` and inspector wrap regression cover equal driver sequence with distinct capture ids. Final production configuration had diagnostics off. |
| Generic frame: backend compatibility | RK3576 preprocessing; unaffected backend; retained read view (3) | `HostFrame` remains the concrete type; `withRecordingReceipt()` at `host_frame.cpp:49` shares `cv::Mat` storage. `recordingReceipts()` runs template preprocessing and reads retained pixels after frame owners release. Default accessor returns unavailable. Existing platform/video regressions exercise unaffected inputs. |
| Recording: exact candidate acceptance | Staged OFF; queue rejection; attachment during visit (3) | `RecordingSlots::stage/publish/cancel` in `shared_slots.h:143`; `SessionDispatcher` pins the receiver throughout the visit in `session.h:72`. Capture attaches the receipt only after deferred commit in `ar0234_camera_frame_source.cpp:197`. `dispatcherBoundary()` in `session_test.cpp:79` directly controls all three boundaries. |
| Recording: synchronous request identity | Previous completion; unreclaimed worker (2) | `requestRecording()` synchronously allocates the token in `sc233hgs_camera_frame_source.cpp:50`; `recordingRequestStatus()` at line 81 matches the active request. `liveCameraControl()` at `session_test.cpp:157` checks initial and replacement tokens. Session lifecycle fixtures cover refusal while reclamation remains outstanding. |
| Recording: immediate ordinary video | No target; preparation frames excluded (2) | Acceptance feeds the unchanged encoder queue independently of detection. `SessionDispatcher` has no retained image history. Session fixtures and retained production no-target recordings establish ordinary accepted-video flow. |
| Recording: resource/failure limits | Continuous qualified rejection; preparation failure (2) | `shared_slots.h` retains four raw slots and the 64 MiB cap. `Session::start/stop` retain separate three-second budgets; its monitor keeps independent progress limits. `session_test.cpp:254` covers bounded failure paths, and its lines 309–332 cover warmup separation and rejection without progress. Native board session/storage fixture exit files each contain 0. |
| Recording: startup coverage evidence | Historical initialization; accepted but unwritten; diagnostics off; no initialization (4) | Actual-execution observer in `yolo_detector.cpp:288`; successful historical-source observer in `hybrid_tracker.cpp:284`; bounded collector in `frame/startup_collector.h`. `testOverwrite()` at `diagnostic_tracker_test.cpp:739` records executed frames 1 and 3 rather than superseded 2. `testInitFailureAndLowIou()` at line 470 compares successful initialization events and historical source identities. `inspect_test.py` covers unwritten/failed sessions and explicit absence. W2 was found in this evidence interpretation. |
| Recording: bounded compatible metadata | Preparation omissions versus drops; historical recordings; written coverage (3) | `startupSummary()` in `metadata.h:47` writes fixed source records and separate counters. `Session::stop()` uses the existing bounded summary. `inspect.cpp:133` streams bounded sidecars and matches session plus capture identities; `decodedFrames()` at line 86 provides optional readback. `dispatcherBoundary()` checks distinct counters and preparation epochs; inspector tests cover old/partial data, wrong requests, wrap, truncation and real decoder output. W1 was found in this compatibility path. |
| Continuous preview: accepted initial inputs | Preparation 8884–8891; queued old frame; latest replacement (3) | `admitStartupFrame()` in `drone_tracker_node.cpp:440` checks token, capture id and timestamp; dispatch and consumption both call it at lines 491 and 557. `recordingStartupAdmission()` at `ros_video_test.cpp:1350` reproduces the eight-frame preparation barrier. Existing latest-task and detector replacement fixtures cover replacement semantics. |
| Continuous preview: release on initialization | Negative detection; failed initialization; historical success; later recovery (4) | `processFrames()` at `drone_tracker_node.cpp:1411` releases only after successful observed initialization for the current display version. `recordingStartupAdmission()` tests negative and failed initialization, then successful historical initialization and continued processing of rejected receipts afterward. Source review confirms no reactivation on target loss; the only enabling assignment is new-round setup. Tracker loss/recovery fixtures preserve algorithm behavior. |
| Continuous preview: preview/video independence | Preparation delay; video without target; rejected paired candidate (3) | Dispatch submits fresh preview before pairing starts and omits rejected candidates after pairing. No algorithm task means no observations/debug image for that candidate. `recordingStartupAdmission()` checks preview progress while the preparation barrier is held and absence of detector output. Existing continuous lifecycle/telemetry and same-frame display fixtures remain applicable. Retained production video and HDMI rates cover the no-target workload. |
| Continuous preview: terminal fallback | Preparation timeout; old worker refusal; worker failure before initialization (3) | `admitStartupFrame()` accepts current-request terminal status and disables filtering for the rest of the round. It does not authorize a stale token. Session failure fixtures cover timeout/refusal, and `recordingStartupAdmission()` exercises explicit fallback with current live frames. |
| Continuous preview: immediate OFF invalidation | OFF during preparation; rapid ON/OFF/ON; 500 ms display (3) | `stopWithTrigger()` under the control mutex clears pending work, changes presentation version, freezes collector and requests recorder OFF. `recordingStartupCancellation()` at `ros_video_test.cpp:1405` checks preparation cancellation, preview progress, response budget and stale generation rejection. `testOffWithExecutingCurrentRequest()` at `diagnostic_tracker_test.cpp:1126` covers in-flight inference cleanup; collector fixture rejects late callbacks. |
| Continuous preview: exact saved membership | Normal completed round; no initialization; production evidence (3) | Inspector matches stream, round, request generation, capture id, timestamp and sequence, requires acceptance and verifies decoding. Synthetic source/decoder fixtures cover normal membership. Retained real-board evidence and fresh inspector readback cover first-YOLO and explicit no-initialization outcomes in 20 sessions. Real-target initialization acceptance remains not verified (C1). |

## Design and pattern coherence

The seven design choices map to (1) node admission and successful-initialization release, (2) const metadata on the existing concrete host frame, (3) final publication outcome through a pinned receiver, (4) synchronous request generation and current-request terminal status, (5) preview and OFF handling under the control mutex, (6) two bounded value-only source observations and worker-owned metadata writes, and (7) controlled fixtures plus retained bounded target evidence.

Callbacks retain the collector, not raw node/session pointers. Hooks take only its mutex; control acquires its own mutex before the collector. End-of-round inference cleanup precedes replacing observer state. Public types contain portable values and keep SDK dependencies inside platform code. No change to model values, ROS contracts, public tracker-state codes, CPU roles, recording limits, or production debug-image defaults was found in the reviewed diff. This is source/fixture evidence; no matching Jetson hardware run is claimed.

## Fixes made by this step

- `src/platform/rk3576/recording/inspect.cpp`: preserve unknown for absent source keys; reject `accepted:false` coverage claims.
- `src/platform/rk3576/recording/inspect_test.py`: independently cover absent first-YOLO, absent first-initialization, and explicit unaccepted source without a completion marker.
- This report and its adjacent retained step evidence.

The main specs, planning requirements and task checkboxes were not modified. The earlier runtime release and board acceptance evidence retain their recorded identities. This step changes the inspector source after that release; it does not claim a new board deployment or a newly packaged release.

## Commands and validation

`openspec status --change align-rk3576-recording-tracking-start --json`, `openspec instructions apply --change align-rk3576-recording-tracking-start --json` and `openspec list --json` all exited 0. The CLI identified the local OpenSpec root, the spec-driven schema, every proposal/design/spec/task context file, and progress 32/33. All six planning files were read.

| Command/check | Result |
| --- | --- |
| Missing-source regression against retained original inspector in the selected offline image | Exit 1, expected reproduction of W1. |
| Independent accepted-false regression against retained original inspector in the selected offline image | Exit 1, expected reproduction of W2. |
| Native `g++ -std=c++17 -Wall -Wextra -Wpedantic` compile of corrected `inspect.cpp`, with existing OpenCV headers and `-lyaml-cpp` | Exit 0; no compiler warning reported. |
| Native `python3 inspect_test.py recording-inspect-step1` in selected offline image | Exit 0; compatibility, identity, failure and actual FFmpeg fixture checks passed. |
| Corrected inspector against read-only retained final board recordings, new report, `--verify-video` | Exit 0; 20 sessions, zero inspection errors, first-YOLO 20 verified-written, first-initialization 20 not-applicable. Originals unchanged. |
| `./build.sh --test --quadrotor-msgs /home/mzh/nickluo/quadrotor_msgs` | Exit 0; 73/73 CTests passed in 201.30 seconds; the separate native resource-limit CTest also passed. |
| `openspec validate align-rk3576-recording-tracking-start --strict` | Exit 0. |
| `git diff --check` | Exit 0. |

Native checks used x86_64 GCC in `drone-tracker-rk3576:humble-amd64-rknn2.3.2`. The supported build uses the same amd64 backend to produce ARM64 output and explicitly runs portable ARM64 tests through QEMU. This step did not select the ARM64 fallback. Native and emulated checks do not establish target-board timing. The board/model/runtime/input identities for the previously retained camera run remain in `validation.md` and retained inventories; no new board command was executed.

`clang-tidy` and `clangd` are absent on the host and in the selected image. Lint and language-server checks are not verified. C++ compiler checks, integration tests and whitespace checks are reported separately. Python editing used `/home/mzh/miniconda3/bin/python` from conda base; build-image tests used their configured native interpreter.

Raw step evidence is under `evidence/20261114/verify-fix-step1/`. The prior explicitly retained root remains `/home/mzh/nickluo/validation-retained/align-recording-start-20261114`.

## Assessment

Initial verification found **1 CRITICAL, 2 WARNING, 0 SUGGESTION**. Both warnings were fixed by this agent and exercised by the listed regression checks. They have not been independently reverified as a fresh complete OpenSpec verification in this step. The task 6.3 production initialization acceptance blocker remains. Applicable successful real-target initialization membership is not verified, and lint/language-server checks are unavailable. The change is **incomplete** and is not ready for archive.

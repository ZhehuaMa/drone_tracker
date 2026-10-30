# HDMI OSD implementation and validation record

> Latest acceptance as of 2026-09-23 is in [remaining-task acceptance](final-validation.md), including product exclusivity checks, user observation of actual output, and final node validation. RKNN synchronization repair is in [RKNN synchronization validation](rknn-sync-validation.md). This report preserves earlier failure and acceptance history. Progress and blockers below describe those earlier stages, not the final result.

Change: `integrate-rk3576-hdmi-osd`. Acceptance target was production `HZ-RK3576_WuKong Board`, not development board 1 or 2. Updated 2026-09-23. This historical stage was **28/32**, with 4.6, 6.2, 6.3, and 6.4 outstanding. They stayed unchecked and this stage did not synchronize or archive the change as accepted.

Subsequent isolated-test evidence and diagnoses are in [isolation-diagnosis.md](isolation-diagnosis.md). Initial statements that the cause was unconfirmed remain records of that stage and do not override later findings.

## Implemented code

- `src/osd/presentation.h`: complete presentation packet, telemetry snapshots owning strings/values, and injectable display interface.
- `src/osd/presenter.*`: separate thread, single-slot whole-packet replacement, round isolation, freezing, expiry, failure cleanup, and counters.
- `src/osd/geometry.*`: final video rectangle from actual input dimensions, with the same mapping/clipping for boxes.
- `src/osd/layout.*` and `status_glyphs.h`: separate information/transparent-target layers, state colors, lock decorations, unknown values, and fixed Chinese screen hints.
- `src/osd/ros_telemetry.*`: standard-message subscriptions, monotonic expiry, home/barometric references, unit conversions, and warnings.
- `src/osd/low_level_feedback.*`: separate optional-message dependency with compile-time battery-level checks.
- `src/platform/rk3576/osd/`: owned DMA buffers, RGA grayscale/color operations, two-plane atomic DRM submission, and prior-state restoration.
- `src/drone_tracker_node.*`: `display_box` submission only for consumed results and display lifecycle notifications on existing control paths. This change added neither a frame-source consumer nor ROS resubscription to local results.

## Simulation and ROS automation evidence

The local agreed image was `roc-rk3576-ros2-build:humble-arm64`, invoked through `./build.sh --test`, without image modification/rebuild. The default OSD-disabled complete suite passed **34/34**. Historical logs were `.docker-artifacts/log/build-console.log` and `test-console.log`.

| Test | Coverage |
|---|---|
| `osd_presenter_tests` | A/B/C whole-packet replacement, immutable source frames, retained views, no reuse during hardware access, stopping before/during submission, new-round isolation, controlled monotonic expiry, EOF, input/display failure, repeated shutdown |
| `osd_geometry_tests` | 1920×1200 → `(0,14,720,450)`, center `(360,239)`, other aspect ratios, edge/outside/invalid boxes |
| `osd_layout_tests` | Four tracking states, clearing empty boxes, frozen images without targets, separate warning/freeze areas, new telemetry, unknown attitude, fixed Chinese hints, PNG snapshots |
| `osd_telemetry_tests` | Quaternion normalization/ENU heading, nonfinite/unknown values, GPS units, 3000 mAh example, unknown cell count, arm-edge home capture, airborne pressure reference, warning priorities, independent expiry |
| `ros_osd_telemetry_tests` | Real ROS publication to mock display, remapping, QoS, updates after OFF, independent expiry groups, radar cannot overwrite local tracking state |
| `ros_video_tests` | OSD/debug-image combinations, capture timestamps, state publication only on change, business continues after display failure, initialization rollback, existing restart/EOF/input-failure coverage |
| `ros_initialization_tests` | Explicit error for enabling unbuilt OSD; built OSD uses a missing display device to verify failed initialization |

Layout snapshots were `.docker-artifacts/build/drone_tracker/src/osd/osd-status-*.png`. Manual inspection of returned fault snapshots found readable Chinese text without overlapping freeze hints/warnings. This was a CPU-canvas check, not HDMI-screen acceptance. `.docker-artifacts/` is disposable; historical paths do not guarantee the files still exist.

Calendar time does not determine expiry. Tests inject monotonic time; layout receives system time separately. Message-provided timestamps were not used as receipt freshness.

No runnable clang-tidy/clangd entry was configured and the container lacked those tools. C++ builds, CTest, and `git diff --check` ran. Jetson hardware/SDK builds did not run; `jetson_config_tests` checks configuration policy only.

## Production hardware prerequisites and probes

See [environment-check.md](environment-check.md). SSH target was `firefly-production`, hostname `hzhy`, kernel `6.1.141-rt36`.

- libdrm pkg-config: 2.4.124. librga pkg-config: 2.1.0; runtime API: `1.10.5_[9]`.
- `HDMI-A-1`, 720×480 at actual 60 Hz, with atomic mode and an alpha-capable plane.
- System `/usr/lib/librknnrt.so` SHA-256: `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`; driver 0.9.8. Runtime was not overwritten.
- Board standalone build: `/tmp/drone-osd-20260923/build`. Public tests passed **3/3**.
- Initial `osd_hdmi_probe` used padded 1920×1200 grayscale gradients, red/blue blocks, and moving boxes, opening the backend twice. Both runs reported `submitted=60,replaced=0,displayed=60,refreshes=93` and exit 0. Initial two-plane TEST_ONLY, real atomic submissions, completion-event waits, restoration, and reopen succeeded.
- Later probe source added `FRAME N` within video and matching target label `N` for visual pairing. Initial probe counts are not visual acceptance for that later version.

`displayed` counts successfully delivered new packets. `refreshes` counts successful display submissions, not physical scans. Repeated refresh of one frame does not increase new-frame count.

**Actual-output evidence was still missing at this stage:** Mac AVFoundation exposed no readable video device. HDMI grayscale neutrality, red/blue channels, moving-box pairing, and freeze hints had not been observed. Successful submission is not pixel acceptance; task 4.6 remained incomplete.

## Complete board build environment

The board initially lacked `vision_msgs` and `cv_bridge`. Added `ros-humble-vision-msgs` 4.1.1, `ros-humble-cv-bridge` 3.2.1, and dependencies `libboost-python1.74.0`/`python3-opencv`; existing packages were not upgraded. Download proxy `127.0.0.1:18080` was not running. Those apt commands temporarily used DIRECT without changing proxy configuration.

Existing board `quadrotor_msgs` 1.0.0 was under `/home/hzhy/workspace/fpv_ws/install/quadrotor_msgs`. LowLevelFeedback defined BAT_INVALID=0, BAT_GOOD=1, BAT_LOW=2, BAT_CRITICAL=3. Loading only `/opt/ros/humble` did not discover it; explicitly selecting its actual CMake path succeeded. An unloaded environment is not evidence that a package is absent.

The complete-node source snapshot was `/tmp/drone-osd-node-20260923/src/drone_tracker`. Its build copied existing repository `.docker-artifacts/rknn/include/rknn_api.h` into a private temporary SDK directory and still linked the board system library. That header was not a newly installed/upgraded system SDK.

The snapshot lacked Git history. An initial all-target build failed in unrelated NanoTrack benchmark code-identity collection; subsequent builds targeted this node and related checks. Complete default ARM64-container regression still covered existing benchmarks. The board's targeted build must not be described as all targets passing.

## Board builds and diagnostics performed

A targeted HDMI/`quadrotor_msgs` build succeeded. Two initial CTest failures came from missing installation-prefix and optional-message library paths. After installation and setting `AMENT_PREFIX_PATH`/`LD_LIBRARY_PATH`, related checks passed **8/8**. Rebuilding with the optional adapter disabled passed ROS telemetry integration **1/1**. Final source synchronization exposed reused stale objects, so affected source was forcibly recompiled. After the odd-grayscale-size fix, final OSD-enabled/optional-message-disabled checks passed **8/8** in 14.94 seconds; see `rebuilt-final-test.log`. Final-source optional-message-enabled confirmation passed **1/1** in 0.78 seconds; see `final-test-lowlevel-on-confirmed.log`. It published actual BAT_CRITICAL, asserted level 3, and checked radar did not overwrite local state. Only the test target was rebuilt for that confirmation; the installed final node retained the disabled optional adapter.

The first real-model run used existing INT8 NanoTrack. The backend explicitly rejected its native feature layout: required FP16 NC1HWC2, observed INT8 NC1HWC2. Subsequent runs used approved FP16 models from the same batch. No contract, training, or model conversion changed.

Synthetic input was FFmpeg `testsrc2`, 640×480, 20 frames/second, 660-second MJPEG AVI. Intrinsics `(800,810,320,240)` were software-test examples, not calibration. No target was captured; valid boxes were zero. This says nothing about tracking quality or actual moving-target pairing.

The preliminary process completed 20 ON/OFF rounds with output stopping/restarting normally. A display submission encountered `ENOSPC`; display disabled itself while tracking continued. It did not pass healthy sustained display. A later 30-second system-call-traced reproduction did not repeat the error and observed a completion-event read after every submission. That did not resolve the first fault. Historical records were `.docker-artifacts/hdmi-osd-20260923/` and the board acceptance directory.

Odd dimensions exposed RGA Y400's even-width/even-height constraint. A 641×479 grayscale frame initially produced an explicit RGA parameter error. The fix copied odd grayscale into equal-channel BGR only in the display thread, then scaled with RGA. It retained dimensions, stride, and coordinates without cropping edges or mutating source pixels. Two real-board EOF rounds then consumed 90 frames each, reaching `submitted=180,displayed=180` without size errors. Moving the input away explicitly failed startup; restoring it allowed another round to consume 2 frames, reaching `submitted=182,displayed=182`, then exit normally. These prove submission/lifecycle paths, not HDMI pixel observation.

## Rollback installations and identities

Board root was `/home/hzhy/drone-tracker-osd-20260923/`. `install-off/` held the default-disabled build; `install-on/` held final HDMI enabled with optional adapter disabled. Both used `lib/drone_tracker/drone_tracker_node`. Installation ran only root-package installation rules; unrelated unbuilt benchmarks were not claimed as installed.

Both copies passed `ldd -r` without missing libraries or unresolved symbols. Final enabled output did not depend on `quadrotor_msgs` shared libraries. Production service executables were not switched and system RKNN Runtime was unchanged.

| File | SHA-256 |
|---|---|
| OSD-enabled node | `6846910feddde3cb6da12b01d753e3c7363d8a1ba728fd67ddffae68ebadbb02` |
| OSD-disabled rollback node | `a90ca1bb0d55e1c928d24557b4a94b0d3954d1aa450b902b5b7154a3821ec014` |
| Synthetic-video configuration `synthetic.yaml` | `0a25315b9ab1a81d403dc5431343fc780d1b92c75bf0ce42480d7e7a64464fde` |
| 640×480, 20 FPS, 660-second synthetic video | `3e344dd4afab97a1b0b3b9c43ad47f8e480c8f4d58ca8e0b7d39f9417de6c1f9` |
| 641×479, 90-frame boundary video | `7d9c2e0961678812126fca978b74da2125449a9e90b61f4d57573aff34ae0a2b` |
| System RKNN Runtime, unchanged before/after deployment | `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8` |
| YOLO | `4d8a22bc8da9fbbc43f190b7ea90c27d9980f77e3e27064ec32cf6566c2680f3` |
| FP16 Template Backbone | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| FP16 Search Backbone | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| FP16 Head | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

Models were existing approved RKNN files, without training/conversion or Git addition. Historical scripts, logs, and resource samples were under local `.docker-artifacts/hdmi-osd-20260923/`. Board build logs were `/tmp/drone-osd-node-20260923/`, with final source under its `src/drone_tracker/`. These temporary directories are not durable deployment dependencies or guaranteed evidence storage.

## Incomplete production acceptance and actual blockers at this stage

On 2026-09-23, the user explicitly postponed actual HDMI observation for task 4.6. This deferred visual acceptance without making other production checks pass.

| Check | Actual result | Conclusion |
|---|---|---|
| Final node EOF/missing input/recovery | Two EOF rounds of 90 frames, recovery round of 2, matching display-count increases, exit 0 | Automated path passed; screen freeze hints unobserved |
| Same-process 20 restarts | All 20 produced output and stopped; observed stop duration 0.255–0.340 seconds | Mid-run display fault prevents healthy multi-round acceptance |
| Final-version display fault | `OSD paired plane commit: No space left on device`, DRM atomic `ENOSPC` | Reproduced; cause unconfirmed then. This is not a disk-space diagnosis |
| Business after failure | Tracking/ROS continued after display disabled; normal stop exited | Establishes isolation, not sustained display |
| Intended EIO injection | Tool scheduled failure at submission 100, but real ENOSPC occurred first | Intended injection did not complete |
| At least 10 minutes of load | Sustained phase followed 20 rounds, but display was already failed; SIGINT ended it | Incomplete; tracking-only remainder is not display load |
| DRM audit | At 100 submissions, 99 completion events had been read; RKNN fatal wait occurred before the next hundred-submission log | Does not establish an event-queue leak as the ENOSPC cause |
| RKNN diagnostic-period failure | `search_backbone.rknn: RKNN wait failed; device access cannot be excluded` | Existing protection terminated the process. Causality with OSD was unproven and model backend unchanged |
| OSD on/off performance comparison | Final comparison blocked by the above | Incomplete |

Across 20 round-idle points, RSS went from 126996 to 127712 KiB, threads from 17 to 16, and descriptors from 49 to 39. The last two decreases reflected resource cleanup after display failure, not healthy long-term leak freedom. Stop duration measured receipt of OFF through the observer's stop message, not screen freeze latency.

`endurance-result.json` contained `RuntimeError('node exited 0')` because the invalid long run was deliberately stopped, not because the node crashed. `audit-result.json` contained `KeyError('VmRSS')` when sampling an already exited process; the true termination cause was the fatal RKNN wait in `audit.log`. Neither failed record was deleted or relabeled passed.

All acceptance processes exited. Fatal RKNN termination bypassed destruction and left 720×480 display mode temporarily active. Using preflight `modetest.txt`, restoration first verified framebuffer 213 remained 1280×720, then restored connector 177, CRTC 75, 1280×720@60 and that framebuffer. `drm-postflight-restored.txt` records restoration. These IDs describe this cleanup only and were not hardcoded in product selection. This run stopped no desktop service and changed neither frequencies nor CPU affinity.

Unmeasured scope remained actual HDMI grayscale/color, moving-box pairing, 500 ms screen-freeze deadline, end-to-end display delay, submit-duration distribution, healthy 10-minute resource trends, and OSD on/off performance. Synthetic no-target input established neither target quality nor camera calibration. Tasks 4.6 and 6.2–6.4 stayed incomplete at this stage.

Deployment and two rollback methods are in [RK3576_HDMI_OSD.md](../../../../docs/RK3576_HDMI_OSD.md). The historical next steps were to diagnose DRM ENOSPC/RKNN waits and rerun healthy restarts/load; actual visual acceptance was separately deferred by the user. Later outcomes are in the reports linked at the top.

## Historical task/evidence index

| Task | Implementation/evidence | Boundary at this stage |
|---|---|---|
| 1.1–1.3 | `presentation.h`, `presenter.*`, `osd_presenter_tests` | Public display control verified |
| 1.4–1.5 | `src/osd/tests/presenter_test.cpp` | Blockable backend and controlled clock, not hardware latency |
| 2.1 | `geometry.*`, `osd_geometry_tests` | Includes specified coordinate examples |
| 2.2–2.4 | `layout.*`, `status_glyphs.h`, `osd_layout_tests`, PNGs | CPU canvas checked; HDMI separate |
| 3.1–3.2, 3.4 | `ros_telemetry.*`, `osd_telemetry_tests` | Standard-message contracts verified |
| 3.3 | `low_level_feedback.*`, actual LowLevelFeedback definition | Enabled build/real-message checks recorded separately; disabled build has no package dependency |
| 3.5 | `src/tests/ros_osd_telemetry_test.cpp` | Standard telemetry to mock display passed |
| 4.1 | `environment-check.md` | Production capability query complete |
| 4.2–4.5 | `src/platform/rk3576/osd/`, board probe | Real submit/complete/reopen evidence; incomplete pixel acceptance |
| 4.6 | Numbered-frame `osd_hdmi_probe` | User deferred actual-output observation; incomplete |
| 5.1–5.3 | `drone_tracker_node.*`, two RK3576 parameter files | Existing consumer/control paths, default disabled |
| 5.4 | Root/OSD CMake targets | Disabled default discovers no display/custom-message SDK |
| 5.5 | `ros_video_tests`, `ros_initialization_tests` | Business compatibility checked with controlled models |
| 5.6 | Full ARM64 regression, targeted board build | Final enabled OSD 8/8; both optional-message settings checked |
| 6.1 | Independent installations, fingerprints | Two copies, full identities, dynamic dependencies recorded |
| 6.2–6.4 | Real models, 20 rounds, sustained load | Real-model paths partly passed; ENOSPC/RKNN wait blocked acceptance |
| 6.5 | `docs/RK3576_HDMI_OSD.md` | Dependencies, parameters, deployment, two rollback routes documented |
| 6.6 | This report and `tasks.md` | Evidence-based completion state; no archival at this stage |

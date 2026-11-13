# Implementation validation — 2026-11-12

The earlier implementation evidence below identifies job 1's executable
`819a9cff13f0671312166592813d4bcc054de7533100f7ab0e431f5398d48239`.
The later verification/fix addendum at the end records the revised source and
its separate validation. Earlier results are preserved under their original
identity and are not relabeled as results for a changed executable.

## Reproducible baseline

The baseline recorder was built successfully with `./build.sh --artifacts-dir
/home/mzh/nickluo/validation-retained/session-recording-20261112/baseline-build
--jobs 8` (exit 0). The starting revision is
`c47088f01c991d14f3e0b40980956cd3d3736847`; its executable source is identical to
`e17c43e` (the intervening diff adds this change's planning artifacts only).
The build uses native x86_64 compilation to produce ARM64 output. It is not a
board execution result. Image identity is
`sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`.

The explicitly retained evidence root is
`/home/mzh/nickluo/validation-retained/session-recording-20261112/`.
`baseline-build/backends/amd64/` retains the installed baseline executable,
configuration, build logs, and image/toolchain manifest. `baseline/` retains
the board's selected camera configuration and complete Git source archives:

| Input | Revision | Archive SHA-256 |
|---|---|---|
| tracker | `e17c43e` | `1561dbfb8ac5bd0e45e45e8dc9925ed6a612fc37d8ea1973d2ed1ebe676c71a8` |
| apm_bridge | `31b00e8d458e30323755eeb3506553e2b2111fbe` | `02b52068a98a67bd0b1397c0b08400f63adb88d01b699b5641e347f9ec92d99d` |
| drone_interceptor | `73c605c54ad106de3029c614bcc44eb9017ce8cc` | `edc7cec69a47369ba8d2b5024a01428bda89a14014a67a0237fab8f2ed4d2483` |
| quadrotor_msgs | `bc8bf6cbf30f30593bf978d878ad448af5f9cc20` | `81798fe5c107fb5e739dbd8ecf2f8daf07967616de6aefaa20eeca8142012230` |

All three sibling repositories were clean. The message sources can therefore
be reproduced without copied definitions. The baseline binary was built with
the OSD feedback adapter disabled and does not require quadrotor_msgs; the new
recorder will require that real generated package. Model identities and scene
identities must still be frozen for production comparisons. No historical board
release is asserted to match this baseline: its current release lacks a
SOURCE.json file.

Baseline executable SHA-256:
`c496225d17a45e181944ff0a388b847a7c63c6214882ce75f6ef8541a5c0e0df`.
Selected production camera configuration SHA-256:
`bd988d875d03cb88376ddb08fb1da019db818c515567ca13e780b604c70457b3`.

## Board preflight

Read-only SSH inspection identifies `firefly-production` as hostname `hzhy`,
`HZ-RK3576_WuKong Board`, Ubuntu 22.04.5 ARM64, kernel
`6.1.141-rt36`, GCC 11.4.0, 3.8 GiB RAM, no swap. The root filesystem is 57 GiB
with approximately 11 GiB free at inspection. Tracker, diagnostics, bridge and
interceptor services were running; none was changed during preflight.

System Runtime resolves to `/usr/lib/librknnrt.so`, reports 2.3.2
(`429f97ae6b@2025-04-09T09:09:27`), and has SHA-256
`d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`.
This matches the SDK recipe. RKNPU reports driver 0.9.8. NPU access uses DRM
`card1`/`renderD129`; `/dev/rknpu` is absent. Display `card0`, camera
`video22`/`media2`, permissions, V4L2 device listing, and media topology were
captured in `baseline/board-preflight.txt`. These are preflight observations,
not capture or recording acceptance results.

## Final implementation identity and scope

All 41 tracked tasks are implemented and verified by the evidence below. The
source base remains `c47088f01c991d14f3e0b40980956cd3d3736847` with uncommitted
changes for this change. No commit, OpenSpec archive, production deployment or
rollback was performed. The retained source snapshot and checksum manifest
identify the actual edited files independently of that base revision.

The final ARM64 executable SHA-256 is
`819a9cff13f0671312166592813d4bcc054de7533100f7ab0e431f5398d48239`.
The previous executable is
`c496225d17a45e181944ff0a388b847a7c63c6214882ce75f6ef8541a5c0e0df`.
Both are retained outside build caches. The expanded recorder uses one typed
`RecordingManager`, a dedicated callback executor and one asynchronous bag writer.
The executor schedules recording callbacks separately from tracker control.
Jetson retains its existing recorder.

The final package is
`packages/drone-tracker-20261112T122821Z-pb5juhke.tar.gz` beneath the retained root.
Its selected production camera YAML is byte-identical to the repository template,
SHA-256 `3c2e90046d06f95fd24f0fa76fab8e7587cca64cbc3344561443aa0ee3e377de`.
The selected CPU policy SHA-256 is
`09928cce2b8a7d03fd87b31dd0bd9b416043599653168feaa7494c7d085443f6`.
It retains allowed CPUs 4–7 and assigns ROS transport/recording callbacks to CPU 6,
control/tracking to CPU 4, YOLO to CPU 5, display to CPU 6 and capture/recording
helpers to CPU 7. The input fixture uses CPUs 0–3 and does not change those roles.

The package contains 100 validated ARM64 ROS libraries. They include all seven
non-Python `quadrotor_msgs` generated/type-support libraries and the catalog's
package-discovery resources. Type support is generated code that lets ROS encode
and decode a message. The final node links
`libquadrotor_msgs__rosidl_typesupport_cpp.so` while
`DRONE_TRACKER_OSD_LOW_LEVEL_FEEDBACK=OFF`. Actual dependency collection and
`ldd -r` checks passed in the ARM64 packaging environment. Package checksums,
`ldd -r` on the board and the node's protocol-version probe also passed. Only the
existing tracker and diagnostic services are packaged; there is no bag-recorder
service. The system RKNN Runtime is excluded from the package.

Existing board models were copied into the retained `models/` directory without
conversion or training:

| Role and board file | SHA-256 |
|---|---|
| YOLO: `balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| Template Backbone: `fp16/template_backbone.rknn` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| Search Backbone: `fp16/search_backbone.rknn` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| Head: `fp16/head.rknn` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

## Commands, platforms and exit statuses

The final clean build used:

```bash
./build.sh --clean --test \
  --quadrotor-msgs /home/mzh/nickluo/quadrotor_msgs \
  --artifacts-dir /home/mzh/nickluo/validation-retained/session-recording-20261112/expanded-build \
  --jobs 8
```

It exited 0. All 69 CTests passed in 194.27 s, plus the separate native
operating-system memory-limit check. The random test domain was 70 and discovery
was restricted to localhost. `final-build-evidence/` retains the build/test logs,
runner, compile database, image/toolchain manifest and domain identity outside
backend caches. Intermediate 69/69 runs are preserved in `run-history/`; they are
not substituted for this final clean result.

The compiler runs natively on the x86_64 development host and produces ARM64
programs. Most target tests execute under QEMU, which emulates ARM instructions.
Full recording transport and writer integration tests additionally build and run
natively against native ROS libraries. They build the real shared messages
separately; ARM64 Python extensions are never loaded into native Python. These
native/emulated results do not replace the production runs below.

The final package command exited 0:

```bash
bash deployment/package.sh /home/mzh/nickluo/validation-retained/session-recording-20261112/packages \
  --install-root /home/mzh/nickluo/validation-retained/session-recording-20261112/expanded-build/backends/amd64/install/drone_tracker \
  --message-install /home/mzh/nickluo/validation-retained/session-recording-20261112/expanded-build/backends/amd64/install/quadrotor_msgs \
  --model-root /home/mzh/nickluo/validation-retained/session-recording-20261112/models \
  --camera-config /home/mzh/.codex/worktrees/ac88/drone_tracker/parameters/rk3576_sc233hgs_production_params.yaml
```

Six host build-entry tests passed using `/home/mzh/miniconda3/bin/python`.
Six package tests passed using `/usr/bin/python3`; conda base lacks PyYAML.
ROS generators and board tools use matching ROS Humble Python environments.
`clang-tidy`, `clangd` and `clang-format` are unavailable on the checked host/image.
The clean build exported a compile database and enabled `-Wall -Wextra
-Wpedantic` for the new recorder. Existing toolchain syntax and camera
`__int128` warnings remain outside this change.

## Native and emulated requirement checks

| Check | Verified behavior |
|---|---|
| `recording_catalog_policy_tests` | All generated catalog structures; vectors and original timestamps in shared commands/feedback; valid old stamps and reason-specific zero/negative/malformed fallbacks; immediate/irregular/sparse selection, clock rollback, very small rates, protected transitions and image token accounting |
| `recording_transport_tests` (native) | All default entries plus bench; generated-message equality after deserialization; source timestamps; startup overrides and ROS remapping; radar publisher started after ON; retained snapshot and oversized-cache rejection; identical real ON occurrences; old-handle exclusion; incompatible delivery diagnostics |
| `recording_writer_tests` (native) | Blocked in-flight 20 MiB payload; image per-entry rejection; exactly 240 ordinary plus 16 reserved slots; protected OFF and pair admission; eight-control-message fairness; multi-file SQLite readback; quota/free-space injection; open/write/close/summary faults; opening completion cannot erase callback failure; existing evidence retained |
| `ros_video_tests` | Real ROS video/session readback; OFF/EOF concurrency; sampled published images and absent image metadata when disabled; invalid recording-only names, rates and parameter types preserve healthy tracking; runtime settings require restart; pair admission, queue counters and writer-failure isolation |
| `ros_continuous_camera_tests` | Bags enabled during rapid ON/OFF changes and blocked inference; no preview bag; exactly two established rounds; original OFF/MISSION/DISMISS events and final invalid pair; late-result exclusion; continued preview and safe final cleanup |
| Package/build tests | Missing/wrong-architecture message libraries fail; selected configuration is preserved; dependency input is explicit; cache/backend protections remain; existing matched rollback and service behavior remains |

DDS is the ROS message transport. QoS means delivery reliability, retained-state
behavior and queue depth. Incompatible-QoS tests confirm that coverage errors are
visible rather than describing a quiet configured topic as successfully captured.
Bag admission records a bbox/LOS pair before either independent ROS publication
call. Bbox is the target rectangle; LOS is the camera-to-target direction.
Inspection confirms that a publication exception cannot erase the admitted pair.
Atomic admission does not promise two-message disk transactions; interrupted
writing is tested and reported as failed/incomplete.

## Production workload and service boundaries

Actual runs use `HZ-RK3576_WuKong Board` through `firefly-production`. They retain
SC233HGS capture at 1920×1200/60 FPS, HDMI at 720×480/60 Hz, the four models above,
basic diagnostics and independent original-camera video. The scene is a live,
unannotated bench scene. All measured rounds stayed Detecting. This establishes
recorder behavior for that declared workload; it does not establish target-capture
quality, NanoTrack tracking throughput, calibrated angular accuracy or flight
readiness.

The same `board_workload.py` revision drives the selected-policy profiles,
SHA-256 `837d63367dafe244daee2c5ab30eb50af5eeedc45f803c2a206c2067b5d6df24`.
It requests body IMU/gimbal at 200 Hz, odometry at 100 Hz, selected telemetry and
commands at 50 Hz, radar at 20 Hz, GPS at 5 Hz and state/event inputs at 1 Hz.
Actual rates and serialized sizes are recorded independently in every
`workload.json`. Requested rates are not substituted for measured rates. The
fixture's measured body-IMU rates were 145.19/144.61/137.49 Hz for previous,
disabled and expanded profiles. These variations limit attribution of small
performance differences; the report makes no zero-overhead or isolated causal
speed claim.

Each profile uses a fresh random localhost-only ROS domain. It writes a temporary
camera YAML and uses the exact packaged CPU policy. The original tracker, bridge
and interceptor services are stopped inside an exit-trapped SSH wrapper and
restored afterward. No production release link, camera YAML or CPU policy is
modified. A fresh temporary diagnostic collector uses the same basic mode, CPU 6,
100 ms synchronization, 64 MiB segments, 1 GiB quota and 5 GiB free-space guard.
Its temporary socket/directory avoid modifying or deleting the old diagnostic
store. Full reproduction commands are retained in
`run-packaged-policy-board.sh` and the profile console logs.

| Profile directory | Domain | Node exit | Total / active seconds | Result |
|---|---:|---:|---:|---|
| `previous-policy6` | 115 | 0 | 21.696 / 15.152 | Preserved five-topic recorder; complete readback |
| `disabled-policy6` | 21 | 0 | 21.581 / 15.041 | No bag directory; camera/video/diagnostics healthy |
| `expanded-policy6` | 18 | 0 | 21.696 / 15.150 | Complete expanded bag; no detected gaps |
| `images-policy6` | 60 | 0 | 21.920 / 15.382 | Complete image-enabled bag; no detected gaps |
| `lifecycle-policy6` | 51 | 0 | 83.705 / 65.681 | Twenty complete ON/OFF bags and continued final OFF preview |

All individual workloads are below 180 s. All complete multi-file bag readback
commands exited 0. The lifecycle contains at least 60 s of active workload.
Every original service was restored active, and the final audit found no
validation processes left running. The system Runtime checksum and driver version
remain unchanged. `final-service-and-runtime-audit.txt` retains those checks.

## Measured comparison

CPU percentages use one fully occupied CPU as 100%; the node can use multiple
CPUs. RSS is resident process memory, the RAM currently mapped into that process.
It is separate from retained bag payload, middleware memory, kernel page cache
and unmeasured device allocations. Update latency measures `tracker.update()`;
it is not total capture-to-display latency. HDMI figures count newly displayed
tracking frames. Display latency is submission-to-display completion and does not
measure light reaching the screen. Its p95/p99 bounds come from merged
power-of-two histogram buckets.

| Measure | Previous | Disabled | Expanded | Image-enabled |
|---|---:|---:|---:|---:|
| Consumed FPS | 59.992 | 60.036 | 59.935 | 59.875 |
| New tracking HDMI FPS | 59.900 | 59.897 | 59.673 | 28.981 |
| Node CPU % | 144.67 | 119.53 | 175.98 | 263.93 |
| Video-worker CPU % | 9.00 | 8.60 | 9.28 | 14.54 |
| Diagnostic CPU % | 2.40 | 2.39 | 2.41 | 3.52 |
| Median / peak RSS MiB | 215.64 / 216.93 | 210.04 / 213.65 | 230.83 / 232.38 | 291.90 / 291.90 |
| Active node threads | 20 | 19 | 21 | 21 |
| Active node file descriptors | 54 | 53 | 54 | 54 |
| Update p50 / p95 / p99 µs | 24.77 / 37.61 / 48.51 | 25.60 / 39.95 / 45.88 | 26.13 / 38.60 / 48.55 | 27.37 / 58.47 / 290.86 |
| Mean display-submission latency ms | 17.99 | 16.43 | 18.85 | 41.93 |
| Display p95 / p99 upper bounds ms | 32.77 / 32.77 | 32.77 / 32.77 | 32.77 / 32.77 | 65.54 / 131.07 |
| Observed root-device writes MiB/s | 1.62 | 1.91 | 1.72 | 1.69 |
| Final bag-file growth / active second MiB/s | 0.193 | 0 | 0.170 | 13.031 |

Relative to the previous recorder, expanded recording used 31.31 additional CPU
percentage points (+21.64%) and 15.19 MiB more median RSS (+7.04%). Consumption
changed by −0.057 FPS (−0.095%) and new HDMI frames by −0.227 FPS (−0.378%).
Relative to disabled recording, CPU increased 56.46 percentage points (+47.23%)
and median RSS increased 20.79 MiB (+9.90%). Consumption changed by −0.102 FPS
(−0.169%) and new HDMI frames by −0.224 FPS (−0.374%). These are measured differences
under the stated fixture, including its observed input-rate variation.

The kernel omits `/proc/PID/io` and `/proc/PID/task/PID/children`. Process I/O is
therefore explicitly unavailable. Resource sampling associates video-worker PPids
and reads the actual root eMMC partition's sector counters. Root-device traffic
includes other writes and buffered flush timing. Final logical bag-file growth
is shown separately; it must not be mistaken for physically synchronized disk
throughput or durability. `packaged-policy-comparison.json` retains the full
metrics, actual input rates/sizes and resource ranges.

The image workload has a material separate cost: new HDMI frames fall to about
29 FPS while consumption remains about 60 FPS. Production image publication
remains disabled. Thirty original BGR8 images were admitted and read back with
unchanged capture headers. From 920 image offers, 890 were deliberately frequency
sampled; byte-limited, oversized, queue-loss and unwritten counts were all zero.
The default 2 Hz and 16 MiB/s/16 MiB burst policy was retained. Total logical bag
size was 210,181,992 bytes. Peak accounted payload was 6,948,060 bytes with 137
message slots, below all global/per-entry bounds; images did not consume the
reserved control capacity or cause admission loss.

## Completeness, lifecycle resources and retained failures

| Selected-policy run | Offered | Accepted = written | Policy skipped | Queue / middleware / unwritten loss | Mode snapshots |
|---|---:|---:|---:|---|---:|
| Expanded | 13,797 | 9,423 | 4,374 | 0 / 0 / 0 | 1 |
| Image-enabled | 14,421 | 9,224 | 5,197 | 0 / 0 / 0 | 1 |
| Twenty-round lifecycle | 59,968 | 40,916 | 19,052 | 0 / 0 / 0 | 20 |

The normal expanded queue peaked at 44 messages / 12,054 accounted bytes. The
lifecycle peaked at 42 messages / 11,695 bytes. Readback reconciled every topic's
`offered = accepted + policy_skipped + byte_limited + oversized + queue_dropped`
and `accepted_but_unwritten = accepted − written`. Valid source timestamps and
all shared command/feedback vectors matched their original fields. Every final
OFF pair was invalid as required, and initiating ON/OFF events remained present.
All 30 non-image default entries were observed. Image-free bags contain no image
metadata. The native tests separately cover the optional bench entry.

Lifecycle active thread count remained 21. File descriptors settled at 54 after
the first round's transient maximum of 57. Per-round median RSS ranged from
227.11 to 233.22 MiB without monotonic accumulation. Final OFF released each bag
writer and video session while capture/preview continued. The final service exit
then released capture/display and restored camera controls. Per-round resource
values are retained in `lifecycle-round-resources.json`.

The lifecycle's fixed diagnostic timing-sample array overflowed by 1,358 timing
samples. This is acceptance instrumentation, not the diagnostic or bag recorder.
Its call-timing array did not overflow. Actual diagnostic counters and all twenty
bag summaries report zero lost/unwritten messages. The separate 15 s performance
runs did not overflow their timing arrays. No complete lifecycle submission-latency
distribution is claimed from the capped array.

Earlier attempts remain explicitly excluded from final healthy acceptance:

- The first sample attempted unavailable process-I/O files. The harness now
  records their absence rather than inventing zero I/O.
- `previous02` lacked the video helper beside the temporary executable.
- `previous03` reached the existing production diagnostic store's exhausted
  1 GiB quota. That approximately 1.1 GiB store was retained unchanged.
- The old installed CPU policy omits `ros_transport` and falls back to CPU 4.
  Normal image-disabled runs remained healthy, but its image-enabled stress
  run (`images`) reported 2,868 supported middleware-loss indications and
  correctly closed incomplete. It had zero admission queue drops and wrote all
  7,554 accepted items, including 33 images. The loss is not hidden or reclassified
  as deliberate sampling. The selected packaged CPU-6 policy passed the same
  declared image workload without those indications. Placement is a plausible
  cause; these measurements do not establish a general zero-loss guarantee.

## Requirement-to-evidence closure

| Task | Completion evidence |
|---|---|
| 1.1 | Preserved e17c43e source/binary/configuration, sibling archives and checksums outside caches |
| 1.2 | Fixed 31-entry application catalog plus optional bench; generated catalog/policy test |
| 1.3 | Real target message build input, package dependency and final ELF linkage with feedback adapter OFF; Jetson target has no manager linkage |
| 1.4 | Generated-type compilation, stable catalog IDs/default uniqueness, command/feedback vectors and timestamp tests |
| 1.5 | Recording/build guides describe the exact catalog and real message dependency |
| 2.1 | Immutable effective configuration, all RK3576 templates and native startup configuration tests |
| 2.2 | Duplicate/invalid/type failures isolate recording; node runtime guards reject bag updates with restart explanation |
| 2.3 | Typed H/S/R extraction, malformed fallback reasons and source-time payload readback |
| 2.4 | Injected monotonic selection, joint pair/OFF rules, protected events and admission-charged image bucket |
| 2.5 | Irregular/sparse/rollback/zero/malformed/old-stamp/protected-transition/very-small-rate tests |
| 2.6 | Parameter and session guides include 200-to-100-Hz, zero-Hz, reception cost and clock examples |
| 3.1 | Dynamic-capacity accounting and blocked in-flight 20 MiB test retain charge until release |
| 3.2 | Per-entry/image limits, 240+16 slots, reserved OFF pair and fairness tests |
| 3.3 | Versioned reconcilable counters; complete/incomplete/failed/storage-limited results; actual board reconciliation |
| 3.4 | Slow writer, one-slot pair rejection, image pressure, reserved OFF, fairness and interrupted-pair cases |
| 3.5 | Guide distinguishes retained payload, middleware, serialization and whole-process memory |
| 4.1 | One manager owns ordinary subscriptions; business gimbal history remains unchanged; full typed transport |
| 4.2 | Non-auto callback group/dedicated executor uses existing transport role and works in all enabled input modes |
| 4.3 | Idle AIM snapshot, bounded oversized-cache rejection and snapshot counters |
| 4.4 | All typed sources, overrides/remapping, late radar, incompatible delivery and disabled/idle checks |
| 4.5 | Recording, architecture and agent guidance identify sole writer/executor/direct interfaces |
| 5.1 | Duplicate-content real ON occurrences remain distinct; deferred ON is forwarded once; programmatic starts add no fake ROS Header |
| 5.2 | Constructed pair admitted before independent ROS publications; joint sampling/admission and unsampled final OFF |
| 5.3 | Old handle cannot enter replacement session; OFF/EOF/failure sealing and mission events remain correct |
| 5.4 | Executor cancellation/join, transport-bound destruction, writer finalization and original callback/writer failure reasons |
| 5.5 | Real ROS two-session/rapid/blocking/EOF/failure tests and twenty readable production bags |
| 5.6 | Session/deployment guides give operating intervals and normal/abrupt stop limits |
| 6.1 | Real SQLite multi-file split/readback retains one session and original timestamps |
| 6.2 | Writer-side open/active/split guards, injected quotas/free space, exclusive ownership and non-following old symlink accounting |
| 6.3 | Bounded session.json contains identities, catalog/limits, boundaries, snapshots, peaks and counters; summary failures are visible |
| 6.4 | Multi-file readback, preserved evidence, injected quota/free/open/write/close/summary failures and partial pair failure |
| 6.5 | Deployment/session guides distinguish file splitting, total quotas, shared free space and evidence retention |
| 7.1 | Actual 100-library ARM64 package, shared-message resources, target-loader and real board checks |
| 7.2 | Exact selected YAML/policy fingerprints; bridge/interceptor names; independent video settings and matched existing rollback tests |
| 7.3 | Missing/wrong-architecture rejection, package configuration tests and unchanged service/rollback contracts |
| 7.4 | Build/parameter/deployment/architecture/HDMI/agent guides agree on sampled ON images and default disabled publication |
| 8.1 | Final supported clean build: 69/69 plus native OS-limit check, exit 0; native/emulated boundaries recorded |
| 8.2 | Twenty ON/OFF rounds, 65.681 active seconds, bounded resources and final OFF preview in 83.705 s |
| 8.3 | Matched selected-policy previous/disabled/expanded measurements with actual rates, CPU/RSS/latency/disk/counters/readback |
| 8.4 | Separate gap-free image run, original headers, explicit rate/byte counters and full readable bag below 180 s |
| 8.5 | This retained requirement review, complete source/config/model identities and strict OpenSpec validation |

Jetson hardware/SDK execution, target acquisition quality, externally frozen or
annotated scene replay, calibrated LOS accuracy, power-loss durability and
per-process disk-I/O attribution were not performed or are unavailable. They are
not silently substituted by native, emulated or unrelated historical results.
Strict OpenSpec validation and `git diff --check` are recorded in the final
closure commands. No required task acceptance remains open for the declared
session-recording workload.

## Verification/fix step 1 addendum — 2026-11-12

The independent review in `verification-step-1.md` found a control-event race
and two groups of scenario-test gaps. ON/OFF recording forwarding now uses the
same control lock as the round decision. Original callback-entry receipt time
is preserved. The session-opening race test gates the continuous worker before
`RecordingManager::open()`, delivers OFF while that worker holds the lock, and
reads back the resulting ON, OFF and final invalid observation pair.

The old ordering has a precise failing interleaving: the worker acquires the
control lock before opening, OFF reaches the inactive manager and clears its
pending ON, the worker opens the round, and OFF later acquires the control lock
and seals it. Neither original trigger entered that bag. The new ordering
prevents OFF forwarding until the gated opening completes. The regression also
checks the real bag contents rather than inferring correctness from a lock.

Added manager tests exercise pair sampling followed by OFF, zero/negative/
malformed timestamp fallback with original payload readback, protected Boolean
transitions, separate image byte/oversized/queue outcomes, and token charging
only on successful admission. Added writer tests exercise an active free-space
limit, a split-time limit, accepted-but-unwritten accounting, old incomplete
files and summaries, and symlink exclusion without filling a production disk.

Final revised build, package and board results are recorded in the final
evidence subsection below. The original issue counts remain 0 CRITICAL,
3 WARNING and 1 SUGGESTION; only a fresh verification step can establish
final counts.

The same boundary review also found ignored finite-input ON commands and failed
starts leaving stale pending recording commands. The final fix takes one
run-state snapshot for the control decision, explicitly disallows deferral for
ignored ON, and clears commands canceled by failure or service termination.
Gated finite-input close/restart, failed recording-open, thread-start failure,
tracking failure and camera-failure tests cover those paths. The complete
state/event table is retained in `verification-step-1.md`.

The intermediate executable `71bd8c578b3cbe9caa9a8bc9ce252c4357855a919f0d4358ca161f3b405c831c`
passed 69/69 tests and the board profile/lifecycle matrix before that companion
fix. Its package and board outputs are retained directly under
`verify-fix-step-1/`. They remain intermediate evidence and are not relabeled
as acceptance of the final companion fix. Final evidence is recorded separately.


### Final verification/fix step 1 evidence

The revised executable is **`8fbad8fc5ccce24b20877ea0a68769200febf99ada89f0aafd6875f5b4c2ad37`**. Its source base remains
`c47088f01c991d14f3e0b40980956cd3d3736847` plus the retained source patch/snapshot.
The final package is `drone-tracker-20261112T140616Z-cksaad9e.tar.gz`, SHA-256
`9979f27b20bca3cd6f4038382fdc9097fe524fcdf5bacd3645bf4c9018b93532`.
All final step-1 evidence is beneath
`/home/mzh/nickluo/validation-retained/session-recording-20261112/verify-fix-step-1/final/`.
No production release was deployed. All four original services are active again,
no validation processes remain, and system Runtime/camera/CPU-policy checksums
match preflight. The four model identities and selected source configuration /
CPU policy remain the pinned identities listed above and were rechecked in
`board-preflight-and-loader.txt`. The production scene remained Detecting-only.

The final supported command was the same `./build.sh --test --quadrotor-msgs
/home/mzh/nickluo/quadrotor_msgs --artifacts-dir
/home/mzh/nickluo/validation-retained/session-recording-20261112/expanded-build
--jobs 8`. It exited **0**, passed **69/69 CTests in 195.82 s**, and passed the
native OS memory-limit check. Its random localhost-only ROS domain was **126**.
`build-evidence/` retains logs and the compile database outside caches. Native ROS
policy/transport/storage results and ARM64 emulation retain their separate scope;
Jetson hardware execution remains unavailable. Lint tools remain unavailable as
recorded above. The package command used the existing selected camera template,
real matching message installation and models, with output under `final/packages`;
it exited 0. Actual target `ldd -r` and protocol probes passed.

#### Complete boundary and policy regression coverage

- `runWithTrigger()` and `stopWithTrigger()` share the round-control lock with
  authoritative forwarding and preserve original callback-entry receipt time.
  A single run-state snapshot prevents finite-input cleanup from changing the
  meaning of a command between forwarding and the start decision.
- `observeTrigger(..., defer_on)` cannot cache an ignored finite-input ON for a
  future programmatic round. `cancelPendingStart()` clears starts canceled by
  ROS/programmatic stop, failed recording/round start, camera failure or exit.
  Active real triggers remain distinct occurrences; programmatic calls fabricate
  no ROS Header. The actual `round_start_failure` and `camera_failure` boundaries
  remain visible.
- Shutdown freezes control intent before sealing/callback-executor teardown.
  The gated prepared-worker test verifies that a canceled round cannot open a bag
  during that gap. Recording synchronization hooks are inactive when no manager
  exists, preserving disabled-recording resource behavior.
- Real node tests gate OFF against session opening, finite-input close against an
  ignored ON, and preparation against service exit. They read original triggers
  and final paired observations. Rapid continuous cases verify both retained and
  programmatically canceled deferred ON. Failed open/thread start and existing
  EOF/tracking/camera/MISSION/DISMISS cases preserve their actual boundaries.
- Native manager tests cover paired omission and final OFF bypass; three invalid
  timestamp reasons with unchanged payloads; brief protected events; distinct
  image byte/oversized/queue outcomes; and token charging only after admission.
- Native writer tests cover active/split free-space protection, unwritten counts,
  existing incomplete/summary files, and symlink exclusion without filling disk.

These checks close the re-opened task groups 2.5, 5.1, 5.5 and 6.4. The unchanged
requirements retain the full 41-task mapping above. The initial step-1 report
found **0 CRITICAL / 3 WARNING / 1 SUGGESTION**. All three warnings were fixed;
those original counts are not overwritten by an applied fix. A fresh agent must
provide the next verification counts. The formatting suggestion was not expanded
into an unrelated rewrite.

#### Matched production profiles for the revised executable

The final retry uses the unchanged fixture and selected policy, with a fresh short
output root `/var/lib/drone_tracker/recording-vf1b-20261112`. The earlier long
socket path and camera-fault attempts remain retained below. `run-board-validation-retry.sh`
records reproduction commands; `board-raw/` contains actual configurations, message
rates/sizes, measurements, logs and bags. All individual runs stayed below 180 s.

| Profile | Domain | Total / active s | Rounds | Node exit | Consumed / displayed FPS | Node CPU % | Median RSS MiB |
|---|---:|---:|---:|---:|---:|---:|---:|
| previous | 107 | 22.031 / 15.482 | 1 | 0 | 60.005 / 59.918 | 145.911 | 215.141 |
| disabled | 3 | 21.582 / 15.040 | 1 | 0 | 60.039 / 59.893 | 122.570 | 210.027 |
| expanded | 65 | 21.872 / 15.331 | 1 | 0 | 60.009 / 59.529 | 178.394 | 228.512 |
| images | 198 | 22.164 / 15.619 | 1 | 0 | 59.865 / 28.750 | 265.573 | 291.934 |
| lifecycle | 151 | 80.685 / 62.643 | 20 | 0 | 59.735 / 58.932 | 180.125 | 229.328 |

| Profile | Update p50 / p95 / p99 / maximum, us | Display mean / p95 upper / p99 upper, ms | Root eMMC write MiB/s | Actual body-IMU Hz |
|---|---|---|---:|---:|
| previous | 24.889 / 36.401 / 43.602 / 62.264 | 19.037 / 32.768 / 32.768 | 2.443 | 143.357 |
| disabled | 24.565 / 39.906 / 44.592 / 52.833 | 15.710 / 32.768 / 32.768 | 2.346 | 144.178 |
| expanded | 25.669 / 40.434 / 49.692 / 4407.026 | 22.286 / 32.768 / 32.768 | 2.519 | 137.225 |
| images | 25.783 / 54.427 / 299.281 / 2238.664 | 42.876 / 131.072 / 131.072 | 2.326 | 123.882 |
| lifecycle | 26.705 / 43.103 / 53.669 / 76.386 | 21.876 / 32.768 / 32.768 | 3.247 | 115.171 |

Expanded versus previous recording measured **+32.48 CPU percentage
points (22.26%)** and
**+13.37 MiB median RSS (6.22%)**.
Actual rate differences and the live unannotated scene limit causal attribution;
no zero-overhead claim is made. Disk figures are shared-filesystem observations,
not per-process attribution or proof of durability. Image publication retains its
separate material cost of approximately 28.75 displayed FPS and 265.57% node CPU.
Normal production publication remains disabled. All detailed metrics, input
serialized sizes, limits and absolute values are in `comparison.json`.

Legacy logs/readback report 7,644 accepted/written messages, zero drops and a
reported 2,960-byte peak under the old accounting. The helper's zero legacy
`session_totals` fields are unavailable-new-summary placeholders, not measured
zero traffic. Old and new byte estimators are not treated as equivalent.
Expanded recording reports 9,473 accepted/written and 4,492 policy omissions;
image recording reports 9,356 accepted/written, including 31 original images,
and 5,214 policy omissions. Both report zero queue, middleware, oversized or
accepted-but-unwritten loss. Their peaks are 44 slots / 12,566 accounted bytes
and 154 slots / 6,954,433 bytes. Complete readback preserved source headers,
payloads, pair boundaries and image metadata rules.

#### Lifecycle result and the stricter diagnostic gate

**The lifecycle harness returned 1. Its diagnostic-lag gate did not pass.**
The tracker itself exited 0 after 20 ON/OFF rounds, 62.643 active seconds and
80.685 total seconds. The exact warning was `error=0`,
`unsynced_age_ns=612498243`, and `dropped=0,0,0,0`. This was a real transient
612.5 ms disk-synchronization backlog, not timing-sample overflow. It recovered
to roughly 20–71 ms in the later recorded samples. Diagnostic settings were not
changed, the warning was not removed, and the harness error remains unmodified
in `board-raw/lifecycle/workload.json`.

A separate forensic readback exited 0 for all **20 complete bags**: 38,941
accepted messages were written, 18,143 were deliberately sampled, and queue,
byte-limit, oversized, middleware and unwritten loss counts were zero.
`rk3576_diagnostic_inspect` exited 0 with a CRC-valid 15,617,040-byte diagnostic
prefix, 732 commits, a normal-completion marker, final producer commit sequences
48,118 / 4,734 / 2,152 / 122 / 4,723, and **zero cumulative gaps for all five
producers**. Thus the diagnostic backlog drained without detected data loss.
All timing-sampling overflow counters were zero in this revised run.

Active file descriptors stayed at 54 and threads at 21. Per-round median RSS
ranged from 228.30 to 232.95 MiB without monotonic growth; first/last medians
were 230.45 / 229.78 MiB. Fully preview-only windows after final OFF displayed
126 new frames. `lifecycle-resource-checks.json` retains those checks.

The change's production-acceptance requirement and design decision 12 require
matched diagnostics, measured performance, bounded lifecycle resources, complete
readback and no unexpected recorder loss. They do **not** set a 500 ms diagnostic
synchronization limit. The data therefore satisfy those explicit recording and
lifecycle criteria while failing the harness's stricter diagnostic gate. This
assessment does not claim that the independent diagnostic latency target passed.
`requirement-assessment.json`, the raw warning, normal diagnostic close and full
readback preserve this distinction for fresh independent verification.

#### Retained failed and superseded checks

- The initial gated race assertion incorrectly counted ordinary invalid Detecting
  observations as final OFF pairs. Its failed run is retained. The corrected
  assertion checks the final common bbox/LOS reference and payload.
- Intermediate `71bd8c...` results and the later `pre-shutdown/` 69/69 result
  retain their own binary identities. Two superseded suites were intentionally
  interrupted with exit 130 and are not final acceptance evidence.
- `pre-hook-guard/` retains the disabled-recorder synchronization-hook failure
  and its package identity. The final source guards those test hooks.
- The first final board attempt could not start diagnostics because its socket
  name was 110 bytes, beyond the worker's 108-byte address capacity. No tracker
  ran in that attempt. `failed-long-path/` and its script/log preserve it.
- The next disabled-recording run (domain 62) correctly exited 1 on the camera's
  fail-fast timestamp validation at driver sequence 360. `failed-disabled/`
  retains its raw inputs/log/data. No camera timestamp filter or other production
  setting was relaxed. The single unchanged bounded retry above passed that
  disabled profile. This does not establish general camera reliability.

The Detecting-only scene, unavailable Jetson hardware, unmeasured device memory,
per-process disk attribution, tracking-quality and power-loss limitations above
remain unchanged. Strict OpenSpec and diff checks are recorded in closure logs.

## CPU 2 installation and automatic affinity — 2026-11-13

This addendum validates the dedicated `topic_recording` role added in tasks
9–11. The standard package selects CPU 2 for the recorder's ROS callback
executor and every session's bag writer. Camera-video recording remains on CPU
7. Installation and rollback were exercised in isolated filesystem fixtures;
the production board ran a staged candidate and was not upgraded or rolled back.

### Candidate and retained evidence

The retained evidence directory is
`/home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/apply/`
(abbreviated **E** below). It is outside disposable build caches. The worktree
remains at `c47088f01c991d14f3e0b40980956cd3d3736847` with uncommitted changes.
`E/source-snapshot.tar.gz`, `source-manifest.json`, and `source-diff.patch`
retain the implementation and pre-addendum source identities. The parent
`implementation-before.json` and dirty-diff snapshots distinguish this work
from the implementation already present before the CPU-role update.

| Item | Identity |
|---|---|
| Candidate node SHA-256 | `bc999a725aa6fd2050ba4111c9b10eaa59359b2721adf09cc547607fa8e57216` |
| Standard policy SHA-256 | `95604fdb423e9dfd7d8d4b427a2a1f39fb112825875b208e09e9dab6fad5cfbc` |
| Camera template SHA-256 | `3c2e90046d06f95fd24f0fa76fab8e7587cca64cbc3344561443aa0ee3e377de` |
| Package | `E/packages/drone-tracker-20261113T085605Z-s040p4s6.tar.gz` |
| Package SHA-256 | `bcf75374562f72f395b94d110e0aea23d17f7af2696a4481c43633fd2d53df7f` |
| Compiler image | `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421` |
| Packaging image | `sha256:620524fe768a8c7664a24c40504c86dfea35538d53b2da63b6dbf4cc4849293d` |

`E/package/drone-tracker-20261113T085605Z-s040p4s6/SOURCE.json` records every
program, bundled ROS library, real generated `quadrotor_msgs` library, model,
and selected configuration checksum. The package excludes model payloads and
checks the existing board models against `MODELS.sha256`. The message package
was rebuilt from `/home/mzh/nickluo/quadrotor_msgs`; ARM64 recorder linkage and
native test-message linkage use their respective generated libraries.

The exact packaged policy was used on the board, without widening its allowed
set or changing role assignments: allowed CPUs `[2, 4, 5, 6, 7]`, topic recording
2, control/tracking 4, YOLO 5, ROS transport/display 6, and capture/camera
recording 7. `board-analysis.json` checks that each case's copied policy is
byte-identical to the package policy. Parsing the policy alone is not treated
as worker-binding evidence.

### Build, package, and installation checks

All commands ran from the selected worktree unless a different path is shown.
Each completed command below returned **0**.

```bash
./build.sh --test \
  --artifacts-dir /home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/apply/build \
  --quadrotor-msgs /home/mzh/nickluo/quadrotor_msgs

./deployment/package.sh \
  /home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/apply/packages \
  --install-root /home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/apply/build/backends/amd64/install/drone_tracker \
  --message-install /home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/apply/build/backends/amd64/install/quadrotor_msgs \
  --model-root /home/mzh/nickluo/validation-retained/session-recording-20261112/models \
  --without-models
```

The default **amd64 cross compiler** produced ARM64 output; no compiler backend
switch occurred. `build-console.log` records **71/71 CTests passed** in 198.37
seconds and the separate mandatory native memory-limit check **1/1 passed**.
The ROS recording/affinity suites use the existing native test build with real
native message generation. ARM64 targets were also compiled; other cross-test
targets use the configured emulator where applicable. These host checks are
separate from physical-board acceptance below.

Focused host commands were `/usr/bin/python3 deployment/tests/test_cpu_policy.py
"$PWD"` and `/usr/bin/python3 -m unittest discover -s deployment/tests -p
'<test-file>'`, selecting `test_package.py` and `test_install_policy.py`
separately. The latter suites passed 6 tests each. System Python supplied
PyYAML, which the preferred conda-base interpreter lacks. Final logs are
`cpu-policy-test.log`, `package-test.log`, and `install-policy-test.log`.
Initial isolated-fixture runs failed because the fixture selected a Python
without PyYAML and retained a production diagnostics path. The fixture now
uses the test interpreter and redirects that configuration consistently.
Those initial failures are not acceptance passes.

The isolated install tests execute copies of the production install/rollback
scripts with redirected paths and hardware/service stubs. They check fresh
installation, upgrade over an older installed configuration, matching rollback,
exact selected policy bytes, missing-role rejection before mutation, an
unusable target CPU before mutation, explicit inherited mode, custom CPU
selection, and recorder-disabled legacy input. Offline validation also runs
with mocked absent build-host CPU directories. These fixtures do not establish
actual production upgrade or rollback execution. During upgrade, the installer
replaces camera configuration with the selected package configuration. The
rollback fixture then checks restoration of the old camera configuration and
old policy; it does not exercise a camera-retention option.

On the physical board, `bash ./deploy.sh --check` from the staged release
returned 0 (`board-preflight-check.log`). It checked actual CPU availability,
ABI, dependencies, devices, models, and Runtime without installing. Existing
systemd warnings about unrelated bootanim/rkaiq units remain in that log.
The SDK toolchain's existing CMake developer warnings remain in the build log.

### Physical-board acceptance

`E/run-board.sh` completed with exit 0. It sourced ROS for the noninteractive
SSH session, staged the new package under `/var/lib/drone_tracker/trc2-1113`,
and used fresh localhost-only ROS domains 27, 56, and 165. Every tracker process
stayed below the 180-second bound. The fixture published the normal typed
catalog workload with images disabled. Camera mode retained camera-video
recording and HDMI; video replay disabled camera-video recording.

`board/preflight.txt` records the newly inspected `HZ-RK3576_WuKong Board`,
hostname `hzhy`, Ubuntu 22.04.5 ARM64, kernel `6.1.141-rt36`, GCC 11.4.0,
3.8 GiB RAM/no swap, storage, CPU frequencies, device permissions, and media
links. SC233HGS capture used `/dev/video22`, NV12 1920×1200 at 60 Hz. The
commands include `v4l2-ctl --list-devices`, `media-ctl -p`, media2/media3 topology,
and the selected node's format before capture. Display/NPU device access was
rechecked. System Runtime remained 2.3.2 with SHA-256
`d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8` and
RKNPU driver 0.9.8. The system library was not overwritten.

The existing four approved RKNN models and both available balloon clips were
hashed in `board/models-inputs.sha256`. The replay acceptance input was
`/home/hzhy/drone-diagnostic-acceptance-20261012/fixed-balloon.avi`.
All selected file hashes matched again after the runs. The live camera had no
target; every steady camera observation was therefore invalid. All 1,415
steady replay observation pairs were valid.

| Case | Rounds | Whole process, s | Steady consumed FPS | Native bbox/LOS pairs/s | Displayed FPS | Node CPU, % of one core | Median RSS, MiB |
|---|---:|---:|---:|---:|---:|---:|---:|
| Camera, recorder enabled | 2 | 43.794 | 60.015 | 60.015 | 60.036 | 192.60 | 232.22 |
| Balloon video, recorder enabled | 2 | 43.760 | 48.098 | 48.098 | 47.010 | 266.14 | 233.57 |
| Balloon video, recorder disabled | 2 | 23.785 | 47.950 | Not reanalyzed as native pairs | 45.753 | 194.62 | 226.30 |

Each enabled round ran for 18 seconds with 3 seconds excluded for warmup;
steady windows totaled about 29.42 seconds per mode. The disabled check used
8-second rounds and about 9.41 steady seconds. It proves absence of recorder
workers, subscriptions, and files; its shorter duration does not form a matched
performance comparison. CPU percentages can exceed 100% because the node uses
multiple cores. Instrumentation is also material: the typed publisher consumed
about 95% of one core and the read-only observer about 20–21% in enabled cases.

Whole-system mean busy percentages for CPUs 0 through 7 were, respectively:

- Camera: **44.31, 54.27, 73.98, 42.29, 13.17, 72.64, 31.23, 21.75**.
- Balloon video: **37.84, 50.93, 69.20, 38.23, 55.36, 64.09, 27.21, 56.81**.

`board-analysis.json` preserves full CPU, thermal, storage, input-rate, memory,
thread, and latency distributions. Native frame-selection-to-both-publications
p95 was **0.499 ms** for camera and **54.462 ms** for balloon video. This measures
application processing after frame selection. The camera timestamp age is
uncalibrated and is not exposure-to-output latency. The independent Python
receiver delivered only 29.26/36.34 pairs/s and accumulated roughly 1.67/2.13 s
p95 callback delay in this fixture. Native publication rate must not be read as
end-to-end DDS receipt rate. These receiver results are retained, not hidden.

Before-work native logs identify the executor and both recreated writer TIDs
for each enabled case, all with `role=topic_recording requested=2 actual=2`.
The observer then read their masks without changing them: 30 steady samples
for each executor and 15 for each writer. The camera controller and both encoder
processes read back CPU 7; all other managed roles retained their selected
cores. `harness.diff` retains the removal of the old observer's recorder-thread
retargeting. An AST check in `analyze_acceptance.py` confirms that its only
`sched_setaffinity` call selects the publisher process's own CPUs 0–3. It never
changes a tracker thread mask. No external correction was needed.

All **four bags** completed and were fully deserialized by the matching
`readback.py`. Camera wrote 21,813 accepted messages and intentionally sampled
10,085; video wrote 21,188 and sampled 10,454. Queue drops, oversized/byte-limit
losses, middleware loss, and accepted-but-unwritten counts were zero. Peak
queues were 101 messages / 29,174 accounted bytes and 71 / 20,856 bytes.
Source timestamps, message payload vectors, paired bbox/LOS boundaries, and
session counters reconciled. Both diagnostics exports closed normally with
zero gaps across all five producers, no degraded warnings, and complete timing
arrays. The disabled case created no topic executor, no bag writer, no bag,
and no recorder-only odom/control/control_raw subscriptions. Legitimate
non-recorder subscriptions are not counted as recorder failures.

`board/phase-exit-statuses.txt` records workload, bag validation, and diagnostic
export exit 0 for every case. `board/overall-exit-status.txt` records exit 0 for
the board command and service, protected-file, and current-link restoration.
All four originally active services were restored; the emergency restoration
timer was stopped and no staged test processes remained. `post-restore-check.txt`
confirms this separately. Production camera/policy configuration, current
executable, release link, and system Runtime hashes stayed unchanged. Existing
production diagnostic ENOSPC error 28 predates this work; its production logs
were preserved.

### Added task and scenario mapping

| Tasks / new scenarios | Implementation and actual evidence |
|---|---|
| 9.1; legacy direct-runtime policy, invalid explicit role, inherited mode | `cpu_affinity.*`, `cpu-policy.py`; passing `cpu_affinity_tests` and `deployment_cpu_policy_tests` cover separate legacy executor/writer fallbacks, strict values, inherited masks, and explicit routing. |
| 9.2–9.3; standard role, recreated writer, rejected executor/writer binding | `recording_manager.cpp`, `bag_session.h`; passing `recording_affinity_tests` checks before-work writer masks, executor readiness, two sessions, recorder-only rejection cleanup, and unchanged caller/ROS lifetime. CPU-affinity tests cover OS rejection without executing work. Board native logs/read-only samples cover both recreated writers on CPU 2. |
| 9.3; concurrent camera video, topic recording disabled | Camera controller/encoder CPU 7 plus both topic workers CPU 2 in the camera case; `board/disabled-balloon/workload.json` proves no topic workers, recorder-only endpoints, or bag artifacts. Existing ROS tests also exercise the disabled recording path. |
| 9.4, 10.4; ownership, migration and overrides | Updated `AGENTS.md`, deployment guide, session-recording guide, platform guide and refactoring plan distinguish topic work, camera video, ROS resource creation, legacy runtime compatibility, and new-package validation. |
| 10.1, 10.3; install standard policy, upgrade older policy, matching rollback | Passing isolated `deployment_install_policy_tests` checks exact selected policy bytes on fresh install and upgrade over an older installation, then restores the prior policy and camera configuration on rollback. Package fingerprint and target case copies agree. |
| 10.2–10.3; explicit custom core, inherited override, recorder-disabled legacy package | Shared helper plus passing CPU-policy/package/install suites; explicit input is preserved, including deliberate inherited mode and unused topic-role omission for disabled recording. |
| 10.2–10.3; missing topic role, build host lacks topology, target CPU 2 unavailable | Package and install checks reject missing role with a remedy; offline mock excludes topology inspection; target taskset-rejection fixture verifies failure before mutation. Real board `deploy.sh --check` passes the standard policy. |
| 11.1 | Retained default cross build, 71/71 CTests plus native memory check, newly generated package and successful target preflight, with source/program/dependency identities above. |
| 11.2 | New physical-board camera/replay rounds and full readback, native binding logs and read-only masks, all `board-analysis.json` checks true, protected state restored. |
| 11.3 | This addendum, retained source/package/raw logs/analysis, and successful strict OpenSpec/diff checks in `openspec-strict.log` and closure evidence. |

This addendum does not replace the earlier eight-core performance report or its
recorder-disabled comparison. Those used a different executable and an observer
that manually placed recorder threads. They motivated the CPU 2 choice; they
are not evidence of this candidate's automatic binding. The fresh rounds above
validate that binding and lifecycle behavior. They do not establish zero
recorder overhead, calibrated camera exposure latency, Jetson acceptance,
long-duration tracking quality, or production upgrade/rollback on real storage.
No further hardware-specific acceptance required by tasks 9–11 is left unrun.

## CPU 2 verification/fix step 1 — selected-binary capability, 2026-11-13

Fresh independent verification of all 30 changed requirements and 69 scenarios
found **0 CRITICAL / 1 WARNING / 1 SUGGESTION**. The full report is
[verification-cpu2-step1.md](verification-cpu2-step1.md). The warning concerns a
release constructed from a valid but older expanded-recorder installation:
the old executable understands diagnostic protocol 2 but cannot honor the new
topic CPU role. The current package entry point originally accepted it with
the new standard policy. Its recorded binary hash did not prevent that mismatch.

Evidence for this step is retained outside disposable caches at
`/home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/verify-fix/step1/`
(**V**). `old-node-package/` preserves the successful pre-fix reproduction with
node `8fbad8fc...` and the new policy. It is explicitly labeled as a reproduction
artifact, not a deployment candidate. Repeating the command after the fix fails
with exit **1**, before publishing an archive, and explains the rebuild and
matching `--install-root` remedy (`rejected-old-node-package.log`).

The node now answers a feature query through the existing safe
`--diagnostic-protocol-version` exit when
`DRONE_TRACKER_RELEASE_CAPABILITIES=1` is requested. Its response includes
`topic_recording_affinity: 1`. Ordinary output remains `2`; diagnostic protocol
version 2 is unchanged. Older protocol-2 nodes safely return their scalar version
and fail the capability check without initializing ROS or a camera. The shared
CPU-policy helper enforces this at packaging and installer preflight only when
the selected release enables recording in role-pinned mode. Intentional
inherited-mode and disabled-recorder compatibility remain supported.

Tasks 10.2, 10.3, and 11.1–11.3 were reopened before implementation and completed
again only after actual validation. The proposal, requirements and scenarios
were not narrowed. The dense-C++ readability suggestion remains optional.

### Fixed candidate and commands

| Item | Identity |
|---|---|
| Source base | `c47088f01c991d14f3e0b40980956cd3d3736847` plus retained dirty source |
| Node SHA-256 | `de6c8d79dfa5ef81244c68845f2604c73e7dd4be0aeb0289565fce811b6d20b0` |
| Package | `V/packages/drone-tracker-20261113T091721Z-n6mr05j8.tar.gz` |
| Archive SHA-256 | `717cdd728cfa178173878035c240cc2280640ca944c9c040445ea17964a010d5` |
| Standard CPU policy SHA-256 | `95604fdb423e9dfd7d8d4b427a2a1f39fb112825875b208e09e9dab6fad5cfbc` |
| Camera configuration SHA-256 | `3c2e90046d06f95fd24f0fa76fab8e7587cca64cbc3344561443aa0ee3e377de` |

The supported command was:

```bash
./build.sh --test \
  --artifacts-dir /home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/apply/build \
  --quadrotor-msgs /home/mzh/nickluo/quadrotor_msgs
```

It exited **0** with **72/72 CTests in 199.88 seconds**, plus the mandatory
native memory-limit **1/1** test. The default amd64 cross compiler produced
ARM64 output with the same compiler image identity recorded above; no backend
switch occurred. Native ROS tests use real native generated messages. ARM64
compilation/emulation and actual board execution remain separate evidence.
The new CTest proves the safe feature-query path. Focused CPU-policy tests,
**7 installation/rollback fixture tests**, and **6 package tests** also passed.
These fixtures cover missing roles and old binaries before mutation, exact
selected bytes, custom/inherited/disabled cases, actual-core rejection, fresh
installation, upgrade and matched rollback. PyYAML-dependent host tests use
system Python; general evidence processing uses conda base as documented above.

The package command used the explicit installation and matching message prefix
under the same `apply/build/backends/amd64/install/` directory, retained model
root `session-recording-20261112/models`, output `V/packages`, and
`--without-models`. Packaging and complete package checksum verification exited
**0**. `V/package/.../SOURCE.json` identifies all selected program, configuration,
dependency and model bytes. Actual target `deploy.sh --check` exited **0** with
the correct bundled-library environment, ordinary query `2`, and feature query
`{"diagnostic_protocol_version":2,"topic_recording_affinity":1}`.

`fixed-source-snapshot.tar.gz`, `fixed-source-manifest.json`, and
`fixed-source-diff.patch` retain the compiled implementation. Only task and
report closure edits follow that snapshot. `build-evidence/` retains the full
test logs and compile database. Exact commands/statuses are in
`command-statuses.json`, `board-command-statuses.json`, `run_fix_board.py`, and
their logs. C++ lint/language-server tools remain unavailable; no pass is claimed.

### Fresh staged production-board acceptance

The newly rechecked board was again `hzhy`, `HZ-RK3576_WuKong Board`, Ubuntu
22.04.5 ARM64, kernel `6.1.141-rt36`, GCC 11.4.0, and 3.8 GiB RAM. Before capture,
the wrapper retained actual devices, permissions, V4L2 formats and media links.
SC233HGS used `/dev/video22`, NV12 1920×1200/60 Hz. The four approved RKNN models
and `fixed-balloon.avi` input were hashed before the runs and matched afterward.
System Runtime remained 2.3.2 with the approved checksum
`d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`;
RKNPU driver remained 0.9.8. Raw identities are in `V/board/preflight.txt` and
`models-inputs.sha256`.

The revised package ran from `/var/lib/drone_tracker/tc2vf1` without a production
installation or rollback. Its exact CPU policy was used for all three modes.
Each enabled mode ran two 18-second ON rounds with 3-second warmup exclusions.
The disabled resource check used two 8-second rounds and is not a matched
performance comparison.

| Mode | Rounds | Process seconds | Accepted = written | Policy skips | Peak messages / accounted bytes |
|---|---:|---:|---:|---:|---:|
| Camera enabled | 2 | 43.791 | 22,039 | 10,445 | 85 / 24,052 |
| Balloon replay enabled | 2 | 43.762 | 21,271 | 10,515 | 73 / 21,856 |
| Balloon replay disabled | 2 | 23.775 | 0 | 0 | 0 / 0 |

Both enabled logs show the executor and each recreated writer independently
confirming `role=topic_recording requested=2 actual=2` before work. Read-only
observer samples confirm their masks afterward. The camera controller and both
encoder processes remained on CPU 7. Other managed roles retained their selected
cores. The harness still has no tracker-thread affinity-setting calls.

All **four bags** passed full typed deserialization, independent read-only SQLite
integrity/count checks, paired boundary checks, and counter reconciliation.
All **43,310** accepted messages were written; **20,960** offers were intentionally
sampled. Queue, oversized, byte-limit, supported middleware and unwritten loss
counts were zero. No debug-image messages or metadata were present. Both
diagnostic exports ended normally with zero producer gaps and complete timing
arrays. The disabled case created no topic workers, recorder-only endpoints or
bags. All tracker process durations were below 180 seconds.

Every check in `V/board-analysis.json` is true. All workload, readback, export,
restoration and orchestration commands exited **0**. `post-restore-check.log`
independently confirms all four original services active, unchanged protected
program/configuration/Runtime hashes and current link, no staged tracker process,
and an inactive restoration timer. Existing production diagnostic ENOSPC logs
were preserved. The earlier stricter diagnostic-lag failure remains disclosed
under its original executable identity.

Strict OpenSpec validation and `git diff --check` pass after closure. The revised
source, package, safe capability guard, and staged board behavior now have apply
evidence. These results do not establish zero overhead, exposure latency, target
accuracy, Jetson acceptance, production installation/rollback, or a fresh
verification verdict. The controller must launch a new independent verification
step after these fixes; the original counts remain **0 CRITICAL / 1 WARNING /
1 SUGGESTION**.

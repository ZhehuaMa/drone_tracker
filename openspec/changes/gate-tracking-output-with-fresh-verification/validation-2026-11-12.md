# Fresh-verification implementation and validation — 2026-11-12

## Status

Implementation and the specified protocol/timing validation are complete for this source snapshot. This is not an overall performance, accuracy, or flight-safety pass: the formal video profile admitted only 3/724 image outputs, and the live camera admitted 0/501. Performance criteria were not supplied and remain unset. This report covers the revised one-call detector scheduler, separate seed eligibility, and removal of no-result frame-count cleanup. Earlier dated reports retain their original meanings and do not validate this revision. A valid observation requires fresh verification; initialization and a remembered search region never grant output permission.

## Source and retention

- Worktree: `/home/mzh/.codex/worktrees/a6cf/drone_tracker`, branch `codex/tracking-fresh-verification`.
- Starting revision: `1474c7f1e4376c3c560758ece60835a5d7f6cc00`, with the user's existing planning edits preserved. Implementation remains uncommitted.
- Retained host evidence: `/home/mzh/nickluo/validation-retained/fresh-verification-20261112-a6cf/`.
- Retained board evidence: `/var/lib/drone_tracker/validation-fresh-20261112-a6cf/`.
- Final source/configuration/build manifest: `final-source-files.json` (461 non-Markdown files), SHA-256 `b0dcb4f7fb024ee3149a9fdf84ae612bc258cd92c6de33f8e2cbd6c182db83ae`. `final-working-tree.patch` and `extra-source/` retain the modified and new source.
- Final combined node SHA-256: `0509d58ed7193f5d49e9b053edc36739ffdd7671fd8cae0c2cecfb0e9f5cda66`. Corrected scheduler-only node: `508e2447631ac160f9d1262e66a5497a276a71f3f11f0efd452300f7dedf5713`. Earlier scheduler-only manifests describe only their historical snapshots.
- Compiler image: `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`. The x86_64 host cross-compiles ARM64 binaries; configured CPU tests run through the selected QEMU emulator. These checks do not replace native board measurements.
- Randomly generated `ROS_DOMAIN_ID=212` and `ROS_LOCALHOST_ONLY=1` isolate all test ROS traffic. Each non-interactive board command sources `/opt/ros/humble/setup.bash`. Existing services are stopped for each board experiment and their original active states are restored afterward.

The retained directories contain binaries, source patches/manifests, exact configurations, commands and logs, raw diagnostics, ROS bags, model/input identities, and crop input/output bytes. Required evidence is copied out of disposable build caches. Historical retention paths are not guarantees of future storage.

## Implemented policy

A request describes an admitted image. A stage job performs one model call. A plan retains one source image and its remaining whole-image/tile coverage. The single detector worker retains one active call, one shared latest-image mailbox, one fixed-image plan, one unread result, and one locally taken result. With both work types eligible, it serves two targeted checks then one discovery stage. Taking a result releases capacity before expensive arbitration or preparation.

Regional misses and unrelated positives remain partial. A unique same-image overlap can confirm; ambiguity closes adversely. A complete applicable plan can act adversely once. Newer agreement supersedes older discovery. Positive Searching plans pause for short seed disposition. `SeedAccepted` is sent before template preparation so a previously submitted later image can run during catch-up. Controls and results carry values, never image ownership or SDK bindings.

Seed eligibility is separate from confirmation eligibility. An undisposed D32 submitted during expired A can seed B at F42 while Searching, with original pixels and valid source/clock checks. Original A provenance stays unchanged. B starts at zero, replays each required image successfully, and needs a fresh later D41 matched to B's independent T41. The seed itself cannot verify B. An active replacement is protected.

The legacy no-result frame-count branch is removed. Missing replies across updates 15/16 or 60/61 do not abandon an unexpired provisional attempt. The elapsed candidate deadline, distinct usable adverse-plan budget, actual failures, confirmation expiry, and shutdown retain separate responsibilities. No detector-health timeout or restart policy was introduced.

Format-4 diagnostics distinguish jobs, plans, seed/confirmation eligibility, original/new attempts, terminal dispositions, capacity release, seed feedback, measured component time, and inactive frame cleanup. Historical whole-cascade metrics and watchdog events keep their old definitions. Missing evidence remains unavailable. Replay report version 3 identifies the revised policy without inventing ROS admission or subscriber delivery.

## Configured SDK checks

| Run | Outcome |
| --- | --- |
| First full run, `build-1.log` | ARM64 build succeeded; CTest exit 8, 64/68 passed. Legacy cascade/watchdog assertions, a session-publication barrier, and a format allow-list required updates. |
| Second full run, `build-2.log` | ARM64 build succeeded; CTest exit 8, 67/68 passed. The remaining standalone detector test needed explicit Searching seed disposition. |
| Focused shared checks, runs 4 and 5 | Build and CTest exit 0; 5/5 passed: validity, preprocessing, diagnostic tracker, recovery regression, and regression Python. |
| Scheduler-only full run, `build-6-scheduler-only.log` | `ROS_DOMAIN_ID=212 ./build.sh --test`, exit 0; 68/68 CTest checks passed in 193.83 s. |
| Pre-final combined full run, `build-7-pre-final.log` | ARM64 build and all 68 CTest checks passed. Later terminal-cancellation/rejection coverage is recorded separately. |
| Final full workflow, `build-12-final.log` | `ROS_DOMAIN_ID=212 ./build.sh --test`, exit 0; ARM64 installation succeeded and **68/68** CTest checks passed in **194.23 s**. |
| Final focused checks, `fresh-apply-focused-10.log` | Exit 0; 5/5 validity, geometry, diagnostic-tracker, Python-regression, and round checks passed in 18.68 s. |
| Standalone replay SDK run, `fresh-replay-sdk-final.log` | Exit 0; 3/3 checks including fixture setup passed in 4.54 s. Exact decoded-source image IDs and the asynchronous clock callable lifetime are covered. |
| Final native production-board checks, `native-final.log` | Exit 0; 5/5 executables passed: output validity, tracking geometry, observation output, target observation, and diagnostic tracker with its inspector. Raw per-command statuses are in `board/native-final/results.json`. |

Full run 8 failed an old retained-history assertion; run 11 failed a fixture that omitted the now-required evidence clock. The assertions/fixture were corrected and the final complete run above passed. Runs 7 and 9 passed before the last scheduler-control regression was added. The first combined pilot revealed repeated unchanged Searching controls cancelling discovery; the implementation now cancels on a phase/attempt transition, and a regression proves complete seven-stage progress under repeated controls. Pilot logs remain historical evidence.

After the final suite, a trailing empty line in one Python test was removed to satisfy `git diff --check`; executable source did not change. `final-tested-source-files.json` retains the exact tested manifest before that whitespace cleanup.

The image and host lack `clang-tidy` and `clangd`; `lint-tools.txt` records this gap. No repository-specific configured lint command was found. Compiler checks and applicable tests are retained. Full Jetson hardware/SDK validation is unavailable; shared configuration checks compile in the configured suite and do not establish Jetson physical acceptance.

## Identified production board

Current preflight identifies `HZ-RK3576_WuKong Board`, Ubuntu 22.04.5 ARM64, kernel `6.1.141-rt36`, GCC 11.4.0, approximately 3.8 GiB RAM, and 57 GiB root storage. `board-preflight.txt` records actual memory/storage, CPU policy, device permissions/topology, release link, and service states.

The system Runtime resolves to `/usr/lib/librknnrt.so`. SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8` matches the pinned RKNN Runtime 2.3.2 baseline. Driver readback is RKNPU `v0.9.8`. No Runtime library was replaced and no `rknn_server` was started.

| Deployed model | SHA-256 |
| --- | --- |
| YOLO | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| FP16 template Backbone | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| FP16 search Backbone | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| FP16 Head | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

The November 4 round-2 source has original TS checksum `fea7c0a0162e2f9d1e3b68c95d9b4a36532b007677134aec883e08b0b9f5557b`. The retained eight-second 1920×1200, 60 FPS MJPEG derivative has checksum `ac506822b26499863d1fc50af1324fbc29afd3fa76c82e67d03ce74a452f9ae4`. Recompression changed pixels; this is an identified derived workload, not a lossless comparison with the flight recording.

## Exact-pixel crop comparison

The formal comparison profile is confidence 0.50, NMS 0.45, and ROI relaxation 1.0. NMS removes overlapping duplicate detections. Existing model normalization, tensor layouts, and NanoTrack device-feature transfer remain unchanged. The initial search on source image 30 found a candidate scoring 0.519232 in `(591,0,739,666)`.

Independent NanoTrack replay then produced measurements on later exact images. Twelve later images compared: the current minimum-320 region, that exact prior successful region, and its 739×666 scale/aspect recentered at the genuine same-image measurement. Four initial images were negative for every crop. Across all twelve, positive-region counts were 1/12, 1/12, and 2/12 respectively. This supports evaluating the recentered hint, with no claim of repeatable quality or performance improvement.

All 36 regions retain source hashes, bounds, resize/padding, submitted UINT8 bytes, raw model outputs, and candidates. Independent host reconstruction reproduced 36/36 model-input buffers byte-for-byte. A 320 crop scales by 2.0 with no padding; a 739×666 crop resizes to 640×577 with 31/32 pixels of vertical padding. Base conda Python lacks OpenCV/NumPy, so this image-byte check used `/usr/bin/python3`; the exact interpreter/library versions are recorded.

`board/crop/comparison.json`, `comparison-broader.json`, and `crop/input-byte-checks.json` retain the results. The bounded comparison calls took 21.99 s and 38.89 s respectively. One raw-MJPEG container attempt was rejected by the target reader; remuxing the exact JPEG packets into AVI enabled the independent NanoTrack probe. Both attempts are retained. This changed the container, not the encoded image bytes.

The final hint is a bounded, attempt-specific region value. It retains producing job/source identity, recenters successful size/aspect at the current measurement, and resets on abandonment. It remains untrusted and supplies no confirmation evidence. Formal confidence/lifetime defaults were not changed.

## Final board comparison and measurement scope

Formal uses confidence 0.50 and 100 ms confirmation lifetime; the separate temporary profile uses 0.40 and 200 ms. Both retain NMS 0.45, ROI relaxation 1.0, candidate timeout 1000 ms, expiry interval 10 ms, and submission interval 1. Temporary settings do not become source defaults. Versioned performance thresholds were not supplied. One bounded run per program/profile is descriptive evidence, not a repeatable gain estimate.

The corrected scheduler-only control differs from the final combined binary only in targeted-region selection: it uses the existing current minimum-320 ROI instead of the recentered successful-region hint. `corrected-scheduler-only/build-control.py` copies the final source, replaces that block, compiles with the final CMake flags, replaces only that object in a copied archive, and links against all other unchanged final objects. `crop-only.diff`, compiler/link/archive manifests and binary hashes make the isolation reviewable. The repository source remains the combined policy. Models, input, CPU policy, scheduler/cancellation implementation and profile are fixed; output paths differ.

Earlier `scheduler-only/` and combined pilots predate the cancellation fix and are not the final isolated comparison. Their raw records, including the initial missing `AMENT_PREFIX_PATH` staging failure, are preserved. The successful runs include `/opt/drone_tracker/current` in that environment and use the existing bundled ROS libraries. The configured node CPU policy is `/opt/drone_tracker/config/cpu-affinity.yaml`.

All final node and inspector commands exited 0. `board_probe.py` retains the exact remote commands, environment, ON/OFF timing and 175-second process bound. Each measured run below is far shorter than the required 180 seconds. Raw identities/configurations, diagnostics, logs and resource samples are under the correspondingly named `board/run-*` directories. `final-metrics.json`, generated by retained `analyze_final.py` with frozen `analysis-modules/`, is the complete numeric record. Its protocol label requests inspection; it is not an automatic performance verdict.

| Program/profile | Observed wall window (s) | Consumed images | Valid / invalid / suppressed / absent | Valid bbox availability | Completed jobs | Peak sampled RSS (KiB) |
| --- | ---: | ---: | --- | ---: | ---: | ---: |
| Scheduler-only formal | 17.932349 | 702 | 0 / 702 / 0 / 0 | 0.000% | 236 | 221828 |
| Scheduler-only temporary | 17.927414 | 693 | 18 / 673 / 2 / 0 | 2.597% | 234 | 220980 |
| Combined formal | 17.937764 | 724 | 3 / 720 / 1 / 0 | 0.414% | 243 | 207944 |
| Combined temporary | 17.910709 | 691 | 76 / 610 / 5 / 0 | 10.999% | 232 | 217356 |
| Live camera formal | 11.903177 | 501 | 0 / 501 / 0 / 0 | 0.000% | 167 | 214220 |

The denominator is consumed `UpdateEnd` images. Trust-loss and OFF event pairs are excluded. “Valid” above means bounding-box admission; LOS (line-of-sight) remains invalid because these experiments do not supply the required pose, so they establish no angular accuracy. Formal video availability is very low, and the live camera produced no verified target. These results do not establish useful field tracking performance.

The first video round traverses the complete identified eight-second source. The second round deliberately ends after about four seconds to exercise ON/OFF isolation; its eight-second window is incomplete and cannot be treated as a complete source-window comparison. One-based decoded JPEG IDs 1–240 and 1–480 define the first four and complete eight source seconds at 60 FPS. The wall windows above include both rounds and pauses.

| Program/profile, first round | First 4 source seconds: valid / consumed | Complete 8 source seconds: valid / consumed |
| --- | ---: | ---: |
| Scheduler-only formal | 0/232 (0.000%) | 0/472 (0.000%) |
| Scheduler-only temporary | 9/220 (4.091%) | 9/460 (1.957%) |
| Combined formal | 0/240 (0.000%) | 0/480 (0.000%) |
| Combined temporary | 47/227 (20.705%) | 47/467 (10.064%) |

## Components, age, scheduler and recovery

All component values below are medians in milliseconds, with complete counts/ranges/95th percentiles in `final-metrics.json`. Backend execution includes the inference adapter and output access; isolated NPU device-cycle timing is unavailable. Catch-up is the observed interval from matching initialization-end to catch-up-end, not a zero-valued event payload. Capacity, seed-disposition and idle waits are measured separately. Event-observed request/take timestamps can invert across threads; inversions are retained explicitly rather than clamped to zero.

| Program/profile | Preprocessing | Backend execution | Shared postprocessing | Catch-up after initialization | Consumed result age | Usable confirmation gap | Remaining lifetime at valid admission |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Scheduler-only formal | 3.572 (n=236) | 36.616 (n=236) | 0.034 (n=236) | 150.575 (n=4) | 158.866 (n=234) | unavailable (0 samples) | unavailable (0 samples) |
| Scheduler-only temporary | 3.499 (n=234) | 36.021 (n=234) | 0.034 (n=234) | 128.930 (n=6) | 150.625 (n=232) | unavailable (0 samples) | 74.059 (n=18) |
| Combined formal | 3.544 (n=243) | 36.423 (n=243) | 0.034 (n=243) | 68.340 (n=2) | 200.574 (n=241) | unavailable (0 samples) | 24.019 (n=3) |
| Combined temporary | 3.549 (n=232) | 36.628 (n=232) | 0.034 (n=232) | 99.725 (n=8) | 108.744 (n=230) | 99.968 (n=9) | 91.143 (n=76) |
| Live camera formal | 4.246 (n=167) | 38.052 (n=167) | 0.034 (n=167) | unavailable (0 samples) | 298.113 (n=165) | unavailable (0 samples) | unavailable (0 samples) |

A usable-confirmation gap joins successive usable verification events for the same attempt; no such pair exists in the other runs. It does not fill an unverified gap with valid permission. The combined temporary run's gap ranges 49.888–841.658 ms (9 samples), and its valid-admission remaining lifetime ranges 7.402–141.329 ms (76 samples). Consumed detector result age includes stale rejected evidence and is not the age of valid output. Combined formal consumed-age median/p95/max is 200.574/350.623/592.075 ms; temporary is 108.744/350.607/643.519 ms.

`final-metrics.json` retains every job's region/plan coverage, parent verdict, terminal disposition, fallback/supersession/cancellation identity, service order, credit release, capacity/semantic waits and observed occupancy. There are no duplicate terminal stage dispositions, result-overwrite events or admitted-at/after-deadline observations in the final scans. Exact eligible T/T/D service, later-tile progress, bounded active/pending/plan/unread/local ownership, OFF wake-up and device/thread release are proved by controlled tests; wall-run dispatch prefixes alone do not establish simultaneous eligibility. Sampled RSS is not a long-duration leak proof. Distinct physical-pixel and NPU memory peaks are unavailable.

The combined formal run has one trust-loss episode and no completed recovery. Its expiry-to-revocation delay is 0.569041 ms, followed by 1.124778 ms to completion of the invalidation publication attempts. The combined temporary run has **five total trust-loss episodes: three completed and two incomplete**. Revocation-to-local-verification / revocation-to-first-valid-admission times are 858.212645/858.254851 ms, 91.704585/91.742513 ms, and 941.982400/942.020513 ms. Both topic attempts succeeded for these three admissions. The five expiry-to-revocation delays are 0.531609, 0.549942, 0.601559, 0.117235 and 0.574021 ms; subsequent invalidation-attempt delays are 0.814020, 1.259010, 0.737841, 0.552539 and 0.746441 ms.

Scheduler-only formal has no recovery episodes; temporary has two, both incomplete. Live camera has no verified-loss episode, so it supplies no live-scene recovery duration. Incomplete episodes remain incomplete; there is no imputed recovery duration. These measurements distinguish local verification, valid pair admission and publication attempts. They do not measure subscriber delivery or establish a worst-case deadline bound.

## Camera, recordings, paired bags and standalone replay

Current capture preflight reran `v4l2-ctl --list-devices` and `media-ctl -p` and verified permissions/topology: SC233HGS sensor, `/dev/video22`, `/dev/media2`, 1920×1200 at 60 FPS, DRM card0 and NPU card1. `camera-topology-preflight.txt` and the camera run retain these results. Capture preview remained running across ON/OFF. Both camera H264 recording sessions completed successfully (about 243 frames each); raw streams, checksums and paired bags are retained. The node reported 693 captured frames without sequence gaps and reclaimed resources at final shutdown. Camera output availability remains **0/501**, all invalid; successful capture/recording is not verified target acquisition.

`bag_summary.py` uses actual board ROS deserialization; `audit_pairs.py` independently checks raw message-header timestamps against diagnostics. `pair-audit.json` records 725 formal-video, 692 temporary-video and 503 camera bbox/LOS pairs. Every trust-loss pair (1 formal and 5 temporary) is present in both topic bags with empty frame IDs. There are zero paired timestamp mismatches or per-topic nonincreasing timestamps. The loss disposition precedes later recovery admission. In both finite-video runs, the first round's final post-EOF OFF pair is absent from its bag because the worker already sealed that bag at EOF; this is retained explicitly, not counted as a recorded OFF event. Both camera OFF pairs are recorded. Topic attempts and bag results are independent; paired application attempts do not imply atomic subscriber delivery.

The production-board standalone replay of 120 images exited 0 after unsetting `DRONE_TRACKER_CPU_CONFIG` and inheriting CPU affinity 4–7. Its portable executable rejects the node-specific CPU policy; the initial failed attempt is preserved separately. `board/standalone-output-final-inherit/` and `board-standalone-final-inherit.log` retain report version 3, actual model queries, command/status and output. Its `standalone-admission-v1` clock begins at immutable per-image admission after replay wait. It is synthetic evidence timing and is not combined with the node's `capture-monotonic-v1` age/availability measurements.

## Acceptance mapping and remaining limits

The proposal, design and four capability deltas were reviewed against the final implementation and the November 4-derived workload. Separate seed eligibility is exercised by the expired-A D32/new-B/T41/D41 tests, with immutable old provenance, original pixels, independent replay comparisons and rejection guards. Scheduler tests cover regional neutrality, complete/ambiguous adverse action, latest-mailbox replacement, first-miss fallback, supersession and capacity/ownership. Repeated unchanged Searching feedback no longer starves discovery. Candidate deadlines replace the removed no-result frame-count cleanup; former frame boundaries are tested without granting permission, resetting anchors or adding detector-health resets.

Task 3.12 maps to the isolated four-run comparison, source windows and component/scheduler/age metrics above. Tasks 8.1 and 8.2 map to the final complete ARM64 workflow plus explicit SDK/Jetson limits. Task 8.3 maps to identified-board native/video/camera/standalone runs, fingerprints, timing, bags, recording and restored resources. Task 8.4 maps to this retained report and its distinction between protocol/timing evidence and unmeasured quality. These tasks require measurement and honest reporting, not an unsupplied performance threshold.

At final inspection, `drone-tracker`, `diagnostics`, `drone-interceptor` and `apm-bridge` are active again. The release link remains `/opt/drone_tracker/releases/bundle-20261029T063135Z-33352`, the system Runtime checksum is unchanged, and driver remains v0.9.8; `final-board-state.txt` records this. No release, models, system Runtime or service definitions were replaced. Final binaries and raw acceptance evidence are retained outside build caches. `evidence-sha256.json` inventories the retained host files; it excludes itself and does not guarantee indefinite storage.

No physical Jetson build/run was performed because matching hardware and full SDKs were unavailable. No manual labels, calibrated appearance/motion rejection, angular accuracy, tracking accuracy or flight-safety evaluation was performed. The evidence uses one derived recording and one live scene. The formal profile's 3/724 and live camera's 0/501 availability are limitations, not an overall performance pass. Earlier reports keep their original comparison meanings. The controller and bridge are unchanged; old observations cannot be retracted, and predicted guidance may continue after invalid vision. Specification synchronization/archive and independent verify-fix remain separate follow-up workflows.

# Fresh verification validation — 2026-11-10

## Status and scope

Implementation and controlled protocol checks are complete. Task 8.3 remains incomplete: the real-model runs did not demonstrate restored acquisition after a verified loss, so successful recovery-verification and valid-admission durations are unavailable. An observed recovery episode remains incomplete at its measurement-window boundary. This report does not claim a performance pass, tracking accuracy, or flight acceptance.

The implementation follows `gate-tracking-output-with-fresh-verification`. Initialization stays provisional until a fresh later-image detection uniquely matches this attempt's measurement on the same image. Expiry and first usable adverse evidence revoke permission. A pending invalid pair and TargetLost attempt precede later recovery output. The two topics have independent publication outcomes.

## Source, build, and retention

This section and the board measurements below identify the job-1 implementation snapshot. Subsequent verification fixes are recorded in [verification-step-1.md](verification-step-1.md). The recorded board program hash and results do not establish target-board acceptance of those later fixes.

- Repository branch: `codex/tracking-fresh-verification`.
- Base revision: `91d75e1` (`docs(openspec): specify fresh verification for tracking output`), with the implementation working tree applied. No implementation commit existed during these checks.
- Final code/configuration/test manifest digest: `1db15e6287d9ac7a4f603e9f6f6b94ffc2d6a2d974238d79fc91d5e5d59cb3ca`. `source-files.json` records each file's SHA-256 checksum; documentation and task-checkbox updates are outside that code manifest.
- Final ROS node SHA-256: `8c58a84c05b317ccfc9b0df4d1f3b5fdcb570a2252f44404584f7a91c497fbf1`.
- Compiler image: `drone-tracker-rk3576:humble-amd64-rknn2.3.2`, image ID `025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`.
- Compiler host: x86_64. Output: ARM64/AArch64. Host execution used the workflow's QEMU emulator; hardware evidence below was collected natively on the identified board.
- Retained host evidence: `/home/mzh/nickluo/validation-retained/fresh-verification-20261110/`.
- Retained board evidence: `/var/lib/drone_tracker/validation-fresh-20261110-a6cf/`.

The retained host directory contains final program binaries, source/configuration fingerprints, input hashes, exact probe scripts, build/test logs, raw diagnostic records, exported records, ROS bags, decoded bag summaries, resource samples, and analysis JSON. Required evidence was copied out of `.docker-artifacts/` and `/tmp`. These paths describe the retained snapshot at this run, not a guarantee of permanent storage.

## Commands and outcomes

| Check | Command or retained command record | Outcome |
|---|---|---|
| Full configured SDK workflow | `./build.sh --test`; `fresh-verification-build-7.log` | ARM64 build exit 0; 68/68 CTest checks passed, 191.42 s. This preceded the final publication-race adjustment. |
| Final affected SDK checks | Existing `scripts/lib/build_backend.sh` entry functions with `DRONE_TEST_REGEX` selecting ROS video/continuous, shared Jetson configuration, validity, observation, diagnostic tracker, and recovery tests; `fresh-publication-final.log` | Build exit 0; 8/8 checks passed, 39.14 s, including fixture setup. |
| Standalone RK3576 SDK entry | CMake source `src/platform/rk3576`, toolchain `/opt/toolchains/aarch64-humble.cmake`, Runtime include/library under `/opt/arm64-sysroot`; `fresh-replay-sdk-build-2.log` | 39/40 passed. The QEMU missing-executable test reported child status 127 instead of the native `tool missing` error. |
| Native resolution of SDK exception | `bin/ar0234_camera_device_tests`; native board report | Exit 0 on the production board. No backend switch or assertion weakening was used. |
| Standalone focused final SDK checks | `fresh-replay-sdk-final.log`, then `fresh-replay-publication-final.log` after the shared admission boundary changed | 7/7 passed; final replay test 1/1 passed in 3.81 s. |
| Native board protocol tests | `board_probe.py ROOT native`, followed by fixture-only retries in `native-repeat` | Validity, geometry, paired observations, camera commands, assembly, replay, diagnostic tracker, ROS video, and continuous-camera tests all passed. Initial staging failures are retained separately. |
| Final native publication tests | `timeout --signal=INT --kill-after=5s 60s bin/ros_video_tests` | Exit 0 with the final publication implementation. |
| Real-model standalone replay | `timeout --signal=INT --kill-after=10s 175s python3 board_probe.py ROOT standalone`, suffix `-final` | Exit 0. Synthetic evidence timing; no ROS publisher exists in this runner. |
| Final real-model timed video | Same bounded probe, mode `video`, suffix `-final` | Node exit 0; diagnostic inspection exit 0. |
| Final production camera/preview | Same bounded probe, mode `camera`, suffix `-final` | Device preflights, node, and diagnostic inspection all exit 0. |
| Additional video protocol profile | Same probe, mode `video`, suffix `-protocol-500ms` | Exit 0; separately frozen 500 ms confirmation lifetime. It produced no verification. Source defaults remain 100 ms. |

The board probes used `ROS_DOMAIN_ID=188` and `ROS_LOCALHOST_ONLY=1`. Each non-interactive board command sourced `/opt/ros/humble/setup.bash`. Node dependency lookup used the existing release's bundled ROS libraries. `board_probe.py` records original service states, stops only the tracker and diagnostic services, reaps test processes, and restores those services in `finally`. All probe invocations remained within their 175-second bound. The deployed release link, system Runtime, model files, and service definitions were not replaced.

An attempted broad host-conda Python run lacked PyYAML, FFmpeg, and the SDK middleware-test selector. It is retained in `fresh-python-tests.log` and is not acceptance evidence. The configured SDK Python suite passed. Five focused validity-reporting tests also passed under the preferred conda interpreter.

## Production board and model identities

The selected `firefly-production` board identified itself as `HZ-RK3576_WuKong Board`, Ubuntu 22.04.5 LTS, ARM64, kernel `6.1.141-rt36`, GCC 11.4.0, approximately 3.8 GiB RAM, and 57.4 GiB root storage. The initial free root storage was approximately 13 GiB. The CPU boot configuration isolates CPUs 4–7; real-node probes selected the existing `/opt/drone_tracker/config/cpu-affinity.yaml` policy.

The system Runtime resolves through `/lib/librknnrt.so` to the approved system library. Its SHA-256 remained `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`, matching the pinned RKNN Runtime 2.3.2 baseline. RKNPU driver readback was `v0.9.8`. No `rknn_server` was started and no Runtime library was overwritten.

The selected camera was SC233HGS, entity `m00_b_sc233hgs 0-0030`, `/dev/media2`, `/dev/video22`, 1920×1200 at 60 FPS. `v4l2-ctl --list-devices`, `media-ctl -d /dev/media2 -p`, and `v4l2-ctl -d /dev/video22 --get-fmt-video` all returned 0 before the camera probe. Device permissions/topology are retained. DRM card0 belongs to the display subsystem; card1 belongs to `27700000.npu`. Camera/HDMI preview continued between ON/OFF rounds.

| Model | Deployed SHA-256 |
|---|---|
| YOLO `balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| FP16 template Backbone | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| FP16 search Backbone | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| FP16 Head | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

The production search-feature transfer remains the native FP16 shared-device-memory path. The work did not retrain, convert, or replace these models.

## Input and clock boundaries

The November 4 source is `/home/mzh/nickluo/test-data-2026-11-04/12-06-28/`. Round 2 source `session-3b279759c297707a6881520d89ce3377/000001.ts` has SHA-256 `fea7c0a0162e2f9d1e3b68c95d9b4a36532b007677134aec883e08b0b9f5557b`. It is 1920×1200, 60 FPS, approximately 47.45 s. An eight-second MJPEG derivative was produced with FFmpeg `-t 8 -an -c:v mjpeg -q:v 3 -threads 2`; its SHA-256 is `ac506822b26499863d1fc50af1324fbc29afd3fa76c82e67d03ce74a452f9ae4`. Recompression changes pixels, so this is an identified derived workload rather than a lossless same-condition comparison.

Round 1 source checksum is `198dc644350a7c1c723179ee8bb00e2026df2787156c6762acd728a711404b47`. Retained frame previews show the distant objects without manual labels. The earlier 100–130 ms drift bracket and 2.012 s invalid-box observation remain historical motivation; this run does not establish tracking accuracy or a cause of flight departure.

Standalone replay sampled immutable anchors after its per-image wait, using `standalone-admission-v1`. It processed 120 images at configured source FPS 60; source timestamps and execution anchors remained separate. Decoding/copying/waiting before admission were outside synthetic evidence age. Its timing does not establish physical-camera timing or rotation acceptance.

The paced ROS video source and real camera use `capture-monotonic-v1`, retaining age before tracker admission. Video uses its current execution-time reference, not the November recording's old wall-clock epoch. No recorded pose stream was injected into these isolated runs. LOS therefore remained invalid when rotation was unavailable, including the five eligible bbox samples. Physical angular accuracy and rotation acceptance remain unmeasured.

## Final observed availability and timing

Each row uses its complete diagnostic measurement window. The frame ratio denominator is consumed image updates. Trust-loss and OFF event pairs are excluded. Invalid, timestamp-suppressed, and absent output remain distinct.

| Final run | Window | Image samples | Valid bbox | Invalid | Suppressed | Absent | Valid-image ratio |
|---|---:|---:|---:|---:|---:|---:|---:|
| Default 100 ms, timed video | 17.925237360 s | 712 | 5 | 706 | 1 | 0 | 5/712 = 0.7022% |
| Default 100 ms, SC233HGS camera | 11.998143562 s | 500 | 0 | 500 | 0 | 0 | 0% |
| Separate 500 ms video profile | 17.906102798 s | 705 | 0 | 705 | 0 | 0 | 0% |

The initial default-profile runs had zero valid output (708 video and 501 camera images). They remain retained; they were not discarded or averaged into the final rows. These small runs do not establish a performance improvement or repeatability.

The final video run completed 49 detector requests and consumed 47 results. Eight completions were positive; two consumed results were fresh under 100 ms, and four stale positives seeded provisional attempts. Consumed eligible evidence age had minimum 58.982376 ms, median 284.154104 ms, P95 350.676154 ms, and maximum 350.813206 ms (47 samples). The camera had 32 eligible consumed-result age samples, minimum 345.414269 ms and P95 362.246124 ms; it had no positive detections. The 500 ms profile had 49 fresh consumed results but no qualifying verification.

The final 100 ms video run had one verified expiry event. Local revocation occurred 0.681401 ms after its confirmation deadline. The invalid pair/TargetLost attempt was disposed 0.938970 ms after revocation. These are observed local measurements, not hard delivery bounds or subscriber-receipt times. Five valid admissions were recorded, with no recorded admission at or beyond its deadline.

That recovery episode did not restore output within its window. `verification_ns`, `admission_ns`, `to_verification_ms`, and `to_admission_ms` are null/unavailable, and `complete=false`. There is one recovery sample and one incomplete episode. Other runs have no verified-loss episode; their missing durations are also unavailable, not zero. The controlled tests separately prove the specified 70 ms verification / 90 ms admission example and delayed-notification expiry behavior.

Peak sampled node RSS was 217,580 KiB for final video and 213,940 KiB for camera; peak thread counts were 16 and 18. Full resource samples and before/after round state are retained. These short observations and controlled ownership/destruction tests do not establish a long-duration leak bound or full NPU memory accounting.

## Paired publication and bag verification

Raw diagnostic scans and final exports completed without integrity errors. Final video had 714 bbox and 714 LOS payloads with matching `(round, observation sequence, stamp)` identities. The 711 ordinary publication attempts and three control attempts returned independently for both topics. The three control pairs comprise one trust-loss event and two OFF events. The final camera had matching pairs and two OFF events. Empty `frame_id` values were checked by decoding bag message bytes, not inferred from display boxes.

Video round 2's bag contains all 242 bbox and 242 LOS timestamp samples, including the trust-loss event; its timestamp-list hashes match the diagnostic payloads exactly. Five bbox records are valid and all LOS records are invalid because no pose input was provided. Round 1's bag has 471 paired samples. Its timestamp list exactly matches the diagnostic list with the final post-EOF OFF notification removed: the finite-video worker had already sealed that bag at EOF before the later OFF command. This difference is retained explicitly.

Camera bags contain 251 paired samples in each round. The 500 ms video profile's bags contain 467 and 239 paired samples. Every inspected bag has zero bbox/LOS timestamp mismatches and zero non-increasing timestamps within either topic. Pair construction and matching bag contents do not imply atomic transport or subscriber acknowledgment.

## Coverage mapped to tasks

| Tasks | Evidence |
|---|---|
| 1.1–1.3 | Named center-to-top-left conversion, shared ROI/source rectangle, fractional/non-square/clipping/minimum tests, updated algorithm example. |
| 2.1–2.9 | Shared validity values and checked nanosecond clocks; strict replay settings/parser/serialization; runtime startup guard; interval-one templates and configurable interval-four checks; immutable 5000/5140/5170/5240 ms and 80+30 ms examples. |
| 3.1–3.4 | Actual mock detector returns two surviving ROI candidates with stage/region provenance; diagnostic scheduling, pending replacement and unread-result tests remain. |
| 4.1–4.6 | Immediate provisional seed, stale seed, both D2 completion orders, retained same-image replay comparison, strict intermediate replay failure, missing/evicted pixels, metadata-only comparison, LOS/Kalman reset and attempt isolation tests. |
| 5.1–5.6 | First adverse revocation, stale neutrality, five-result provisional budget, nonblocking score/motion hooks, inactive jump authority, 15/16 and explicit 60/61 watchdog boundaries, cleanup after expiry without another loss. |
| 6.1–6.7 | Real node blocked-Nano/absent-input timer tests; controlled revocation/publication barriers; B expiry during A's delayed notification; bag/bbox failure independence; duplicate callbacks and OFF/ON; native final publication test exit 0. |
| 7.1–7.4 | Format-3 integer policy evidence, historical format handling, replay report version 2, separate availability/recovery milestones, mapping mismatch handling, unknown-format rejection, incomplete episodes and unset criteria tests. |
| 8.1–8.2 | Configured ARM64 build/test logs, standalone SDK/replay logs, native target tests, exact source/program/configuration/input identities. Shared Jetson configuration compilation passed; Jetson hardware/full SDK execution was unavailable and was not claimed. |
| 8.3 | Incomplete: actual deployed models exercised acquisition, expiry, invalid-pair recording, preview/ON/OFF and resource ownership. Successful actual-model recovery and its two restored-output durations were not observed. |
| 8.4 | This report distinguishes protocol evidence, availability/recovery measurements, historical inputs, unmeasured accuracy, and unchanged controller behavior. |

All availability/recovery performance criteria remain unset because no versioned acceptance threshold was supplied. The controller and bridge were unchanged. Already published observations cannot be retracted, and the unchanged controller may continue prediction-based guidance after invalid vision. The next required board evidence is a run with a qualifying later-image verification and restored recovery; no timeout extension or manual-label accuracy claim substitutes for it.

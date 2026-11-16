# Validation — 2026-11-14

Update, 2026-11-16: [real-target production validation](validation-20261116.md) completed the remaining task 6.3. The results below describe the earlier no-target run and remain unchanged as historical evidence.

## Result and remaining acceptance

The implementation and software checks pass. The final matching release completed 20 ON/OFF rounds on the production board. Exact sidecar matching and independent video decoding verified all 20 first executed YOLO inputs. The camera scene produced no target, so all 20 initialization sources are explicitly `not-applicable`.

Task **6.3 remains open** because successful tracker initialization has not been exercised with an actual target on the production board. Deterministic fixtures verify successful historical initialization, failed initialization, and admission release, but those fixtures do not replace that hardware evidence. No acceptance requirement was weakened. No specifications were synchronized or archived, and no commit was created.

## Source, build and release identities

- Worktree: `/home/mzh/nickluo/worktrees/drone_tracker-align-recording-start`.
- Branch: `fix/align-rk3576-recording-tracking-start`.
- Base revision: `f08399533b9f548666b52ef9e16694e4c1ce0835`, with the uncommitted changes identified by the retained source patch, changed-file snapshot and SHA-256 manifest.
- Build host: Linux `mzh-Dell-Pro-Max-Tower-T2`, x86_64, kernel `6.8.0-138-generic`.
- Selected backend: `amd64`; native GCC 11.4.0 cross-compiles ARM64 programs. The ARM64 fallback was not selected.
- Image: `drone-tracker-rk3576:humble-amd64-rknn2.3.2`, ID `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`.
- Cross compiler: `aarch64-linux-gnu-g++` 11.4.0; CMake 3.22.1. Portable ARM64 tests execute under the image's explicit QEMU launcher. This does not establish hardware timing.
- Final archive: `drone-tracker-20261114T074354Z-ggq1ko4h.tar.gz`, SHA-256 `60a317d5478f611f95c0cdbac213897d66da95a0acd49adc1af749cc1bfd852a`.

| Final program | SHA-256 |
| --- | --- |
| `drone_tracker_node` | `79488bc9ee59b40b3c222fb4d9c9dd93f413b4be90ebad5879e6dd3c72cbedbd` |
| `rk3576_recording_worker` | `7cad3036f1d90ef76bb7d9cfcca176532419328f29187fd813b8bfa5bed11235` |
| `rk3576_recording_inspect` | `6077464968769d885cdde186e7a3ea2b98068d7647b9bc040846d6c85eee3b15` |

The release contains matching programs, generated `quadrotor_msgs` dependencies, configuration, CPU policy, and model fingerprints. Packaging used `--without-models`; the board's existing matching models were used. The archive does not include or overwrite system RKNN Runtime.

## Board, models and input

The SSH alias `firefly-production` resolved to root at `192.168.77.218`. The board reported hostname `hzhy` and model `HZ-RK3576_WuKong Board`. It runs Ubuntu 22.04.5 LTS ARM64 with kernel `6.1.141-rt36` and GCC 11.4.0. Initial memory was 3.8 GiB, with no swap. The root ext4 filesystem had approximately 6.4 GiB available before these runs. Raw OS, memory, storage, mount, service and device inventories are retained.

System `/usr/lib/librknnrt.so` reported Runtime 2.3.2 (`429f97ae6b`, 2025-04-09). Its SHA-256 was `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`, matching the pinned SDK recipe. `/sys/kernel/debug/rknpu/version` reported driver 0.9.8. The library's checksum remained unchanged after validation.

Before capture, `v4l2-ctl --list-devices`, `media-ctl -p`, and the selected `media-ctl -d /dev/media2 -p` were executed. The selected SC233HGS camera uses `/dev/video22` at 1920×1200 NV12, 60 FPS. The topology, format readback and device permissions were recorded. HDMI uses `/dev/dri/card0`; the NPU render device is `/dev/dri/renderD129`.

| Existing model | SHA-256 |
| --- | --- |
| `balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| `fp16/template_backbone.rknn` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| `fp16/search_backbone.rknn` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| `fp16/head.rknn` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

Input was the board's live camera scene, without a target balloon. The configuration retained production `visualized_result=false`, OSD, camera recording, ROS bag recording, and the packaged CPU policy. Diagnostics were explicitly off to verify that startup evidence does not require diagnostics. Only recording/bag output paths were redirected to a fresh validation directory. Original camera capture timestamps remain unchanged in the evidence.

## Commands and results

The complete commands and status inventory are retained in `software/command-statuses.json`. Repository-relative entry points below were executed from this worktree.

| Check | Command or retained entry point | Actual exit status and result |
| --- | --- | --- |
| Final integration | `./build.sh --test --quadrotor-msgs /home/mzh/nickluo/quadrotor_msgs` | **0**; 73/73 CTests, 202.25 seconds; separate native resource-limit check also passed |
| Package | `bash deployment/package.sh .../.docker-artifacts/startup-release-final --install-root .../.docker-artifacts/backends/amd64/install/drone_tracker --model-root /home/mzh/nickluo/drone_tracker/models/balloon --without-models` | **0**; final archive and checksum retained |
| Final workload | `DRONE_RECORDING_VALIDATION_ROOT=/var/lib/drone_tracker/validation-align-start-final-20261114 bash run_board.sh` | **0**; controller and tracker both exited 0 |
| Native session fixtures | `timeout -k 5 120 ./rk3576_recording_session_tests "$PWD/session"` | **0**; 20 lifecycle rounds, cancellation, receiver pinning, queue rejection, request replacement, receipt handoff, unreclaimed worker, timeouts and progress checks |
| Native storage fixtures | `timeout -k 5 120 ./rk3576_recording_storage_tests "$PWD/storage-new"` | **0**; ordering, short writes, interrupted/failed writes, space failure, synchronization failures and zero-frame cases |
| Saved-frame readback | Final inspector source compiled with native GCC in the selected image, then `recording-inspect-native-final RECORDINGS NEW_REPORT.jsonl --verify-video` | **0**; exact source membership and FFmpeg readback for all 20 sessions |
| Metadata inspector regressions | `recording_startup_inspect_tests` and the retained native invocation of `inspect_test.py` | **0**; old/missing/truncated fields, wrong sessions, sequence wrap, accepted-but-unwritten frames and real decoder fixture |
| Artifact validation | `openspec validate align-rk3576-recording-tracking-start --strict` | **0** |
| Whitespace | `git diff --check` | **0** |

The board lacks an installed `ffmpeg` executable. Readback therefore used the same final inspector source compiled natively in the selected build image against copies of the original recordings. No original recording was modified. Its report is separate from queue-acceptance metadata.

Earlier failed attempts are preserved: two compile failures (exit 2), the OFF-preview regression timeout (CTest exit 8), and an incorrect fixture failure injection (CTest exit 8). A subsequent full pass preceded the final pass above. The native inspector initially rejected missing startup metadata; its compatibility handling was corrected and regression-tested. The first board wrapper stopped before service mutation because ROS setup does not support `set -u`; its corrected wrapper passed. Storage initially refused a pre-created test root (exit 1), then passed using a fresh path. These attempts are not represented as successful acceptance.

## Final production measurements

The final run lasted **74.99 seconds**, including **64.34 seconds** between its 20 ON/OFF pairs. It is below the 180-second per-load-run limit. The fixture commands each had an independent 120-second deadline.

| Measurement | Final result |
| --- | --- |
| Normal recording completions | 20/20 |
| First executed YOLO source | 20/20 `verified-written` |
| Successful initialization source | 20/20 `not-applicable`; none occurred |
| Saved video frames | 3,743 |
| Recorder drops | 0 |
| Request registration to first eligible dispatch | 96.6–114.8 ms; median 105.4 ms |
| OFF command to first completed OFF display | 20 measured commands, 8.1–27.1 ms |
| Processing rate from round counters | Approximately 50.4–56.8 Hz |
| HDMI display rate from round counters | Approximately 58.8–59.9 Hz |
| File descriptors | 40 before first round; 48 after every round |
| Threads | 18 before first round; 19 after every round |
| Resident memory | 116,856 KiB before first round; 222,876 KiB after final round; per-round observations 207,508–227,404 KiB |

Startup latency measures admission to the algorithm, not successful target acquisition. Display completion is the application's completed display event, not a measurement of light reaching the panel. OFF response uses the board's common monotonic clock for the published command and the first matching OFF display. Resource observations cover this bounded run and do not establish long-term stability.

Both the initial and final board runs restored the previously active `drone-tracker.service`, `drone-tracker-diagnostics.service`, and `drone-interceptor.service`. All three were independently checked active afterward. No previously active service was intentionally left stopped. The installed `/opt/drone_tracker/current` release remains `/opt/drone_tracker/releases/bundle-20261029T063135Z-33352`; validation used separate extracted releases. The system RKNN Runtime remained unchanged.

## Retained evidence and limits

Retained root: `/home/mzh/nickluo/validation-retained/align-recording-start-20261114`.

- `board/final-originals.tar.gz` and `board/final/`: original final recordings, sidecars, bags, command/result files, node/controller logs, configuration and identities.
- `board/final/inspection-readback.jsonl`: separate per-session exact-membership and readback report.
- `board/final/summary.json`: derived metrics produced by [summarize_board.py](evidence/20261114/summarize_board.py).
- `board/final-fixtures.tar.gz` and `board/final/fixtures/`: final fixture binaries, hashes, logs, exit files and fault-injection outputs.
- `board/run1-originals.tar.gz` and `board/run1/`: the earlier successful 20-round run, clearly separate from the final executable identity.
- `release/`: exact final archive, checksum, and executable hashes.
- `software/`: build/package/inspection logs, earlier failures, command statuses, actual tool versions and board inventories.
- `source/`: base revision, tracked patch, changed/new implementation files and SHA-256 manifest.

These are explicitly retained validation outputs, outside disposable `.docker-artifacts` caches. The repository report and reproducible board scripts remain under this change.

Unexecuted checks: successful initialization from a real target on the production board (task 6.3), matching Jetson hardware/SDK execution, long-duration stability, flight behavior and real power-loss retention. `clang-tidy` and `clangd` are unavailable in both the checked host/tool environment and selected build image; no lint or language-server pass is claimed. Compiler checks, fixtures, artifact validation and whitespace checks are reported separately.

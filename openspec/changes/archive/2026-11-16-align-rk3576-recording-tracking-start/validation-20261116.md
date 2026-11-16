# Production startup coverage — 2026-11-16

## Result

Task 6.3 passes. The identified production `HZ-RK3576_WuKong Board` completed 20 live-camera ON/OFF rounds with a balloon in view. Each round successfully initialized NanoTrack, the local appearance tracker. The matching recording inspector compared exact source identities with saved frame records and decoded the video. All 20 first executed YOLO inputs and all 20 first successful historical initialization sources are `verified-written`.

The workload lasted 75.467 seconds, including 64.219 seconds between ON/OFF commands. Its controller and tracker both exited 0. This single load run stayed below 180 seconds. OpenSpec now reports 33/33 tasks complete. This report completes the real-target acceptance left open in [the November 14 report](validation.md); it does not change the historical no-target results.

## Software and input identity

The checkout was on `fix/align-rk3576-recording-tracking-start` at revision `2ebc496`. No implementation files changed for this run. The retained November 14 release supplied the node and recording worker. Every file in its source manifest matches this checkout except `inspect.cpp` and `inspect_test.py`, which contain the previously verified inspector fixes. The current inspector was therefore compiled from this checkout for readback, and its regression tests passed. The recording protocol and exact frame identities are shared by these components. The release's original inspector remains retained for provenance; it was not used to establish coverage.

| Component | SHA-256 |
| --- | --- |
| Candidate node | `79488bc9ee59b40b3c222fb4d9c9dd93f413b4be90ebad5879e6dd3c72cbedbd` |
| Candidate recording worker | `7cad3036f1d90ef76bb7d9cfcca176532419328f29187fd813b8bfa5bed11235` |
| Retained release archive | `60a317d5478f611f95c0cdbac213897d66da95a0acd49adc1af749cc1bfd852a` |

The exact current inspector/source hashes are retained in `source/inspector.sha256`. The offline compilation image was `drone-tracker-rk3576:humble-amd64-rknn2.3.2`, image ID `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`.

The candidate used its retained production configuration with output paths redirected to `/var/lib/drone_tracker/validation-align-start-balloon-20261116`. Diagnostics were off. Camera recording, ROS bag recording and HDMI remained enabled; `visualized_result=false` and the 5 GiB free-space reserve remained unchanged. The selected CPU policy included `topic_recording: 2`. ROS domain 186 with localhost-only transport isolated test commands from production consumers. The installed production configuration was not edited.

The four existing models were unchanged:

| Model | SHA-256 |
| --- | --- |
| YOLO `balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| Template Backbone | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| Search Backbone | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| Head | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

## Board and device checks

SSH configuration resolved `firefly-production` to root at `192.168.77.218`. The board reported hostname `hzhy`, Ubuntu 22.04.5 LTS ARM64, kernel `6.1.141-rt36`, GCC 11.4.0, approximately 3.8 GiB RAM and no swap. The root filesystem is ext4 on `/dev/root`, with 60,586,528,768 total bytes. Actual memory, storage, mounts, toolchain, loaded-library resolution and device permissions are retained in `software/board-inventory.txt`.

The system Runtime was `/usr/lib/librknnrt.so`, version 2.3.2 (`429f97ae6b`, 2025-04-09), SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`. This matches the pinned SDK recipe. `/sys/kernel/debug/rknpu/version` reported driver 0.9.8. The Runtime was not overwritten, and its checksum was unchanged afterward.

Before capture, `v4l2-ctl --list-devices`, `media-ctl -p`, `media-ctl -d /dev/media2 -p`, and format readback were executed. The selected SC233HGS sensor feeds `/dev/video22` through `/dev/media2`, with 1920×1200 NV12 at 60 Hz. Root had access to the camera, media, display and NPU devices. HDMI used `/dev/dri/card0`; the NPU render device was `/dev/dri/renderD129`. The raw topology and access evidence is retained with the run.

Initially, free storage was below the unchanged 5 GiB recorder reserve. The two earlier November 14 validation directories were copied to retained host storage. All 1,133 regular files were checked against the board's SHA-256 manifest before those duplicate board copies were reclaimed. The candidate release was preserved. The apt download cache was also cleared. Free space before this run was 5,670,985,728 bytes and afterward was 5,588,942,848 bytes. Production recordings were not removed.

## Commands and measurements

The reproducible controller, service wrapper and summary script are under [evidence/20261116](evidence/20261116/). The wrapper sources `/opt/ros/humble/setup.bash`, stops conflicting services and restores their saved active state on exit. The controller enforces a 145-second budget; the outer wrapper enforces 160 seconds.

| Check | Command or entry point | Exit status |
| --- | --- | --- |
| Current inspector compilation and regressions | Offline pinned Docker image: native `g++` compilation of `src/platform/rk3576/recording/inspect.cpp`, then `inspect_test.py` | 0 |
| Production workload | `DRONE_RECORDING_VALIDATION_ROOT=/var/lib/drone_tracker/validation-align-start-balloon-20261116 bash run_board.sh` | 0; node 0 |
| Exact membership and video readback | `recording-inspect-current RECORDINGS NEW_REPORT.jsonl --verify-video` in the same offline image | 0 |
| Derived measurements | `summarize_board.py RETAINED_BOARD_RUN` | 0 |
| Restoration | Service active-state checks, release-link comparison and installed configuration/policy/Runtime checksum checks | 0 |
| Task state | `openspec instructions apply --change align-rk3576-recording-tracking-start --json` | 0; `all_done`, 33/33 |

The board has no installed FFmpeg executable. Video decoding therefore ran on unchanged copies of its original recordings in the pinned host image. Host readback verifies those saved bytes; hardware execution and timing come from the production board itself.

| Measurement | Result |
| --- | --- |
| Completed recordings | 20/20 |
| First executed YOLO source | 20/20 `verified-written` |
| First successful historical initialization source | 20/20 `verified-written` |
| Saved frames / recorder drops | 3,732 / 0 |
| Request registration to first eligible dispatch | 98.207–115.489 ms; median 107.690 ms |
| Request registration to first successful initialization | 158.366–3,096.576 ms |
| OFF command to completed OFF display | 20 samples; 5.137–27.516 ms |
| Processing / HDMI rates from round counters | 50.12–57.36 Hz / 57.26–59.65 Hz |
| File descriptors | 40 before first round; 54 after every round |
| Threads | 19 before first round; 20 after every round |
| Resident memory | 118,756 KiB before first round; 240,184 KiB after final round; settled round samples 222,256–240,184 KiB |

Timing uses the board's monotonic clock. Display completion is the application's completed display event; no optical measurement of panel response was performed. First initialization can use a later detector request than the first executed YOLO input. The [per-round source table](evidence/20261116/sources.csv) records both capture identities independently. The [inspection report](evidence/20261116/inspection-readback.jsonl) records the saved-video result for each session. The [summary](evidence/20261116/summary.json) includes individual rates and resource samples. These bounded resource observations do not establish long-duration stability.

## Retention and final state

Retained root: `/home/mzh/nickluo/validation-retained/align-recording-start-20261116`.

- `board/balloon-originals.tar.gz` and `board/balloon/`: original sessions, video, frame records, ROS bags, node/controller logs, trigger times, configuration, results and topology checks. The separate readback and derived summaries are in the extracted directory.
- `board/prior-20261114-board-originals.tar.gz`, `prior-board.sha256` and `prior-backup-verified.txt`: verified backup of the reclaimed duplicate validation evidence.
- `release/`: retained candidate archive and checksums.
- `software/`: command statuses, inspector binary and regression/readback logs, Docker image identity, current board inventories and restoration evidence.
- `source/`: source revision, source manifests and inspector source/binary hashes.

After the candidate stopped, `drone-tracker.service`, `drone-tracker-diagnostics.service` and `drone-interceptor.service` were all active again. The installed release remained `/opt/drone_tracker/releases/bundle-20261029T063135Z-33352`. Installed camera configuration, CPU policy and system Runtime fingerprints matched their initial values. No persistent deployment was made.

No acceptance blocker remains for task 6.3. The existing full software-test evidence was not rerun because this job changed only validation artifacts and the task checkbox. Jetson hardware/SDK execution, flight behavior, long-duration stability and real power-loss retention were not tested. No specification synchronization, archival or commit was performed.

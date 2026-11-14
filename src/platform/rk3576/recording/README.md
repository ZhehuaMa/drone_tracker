# RK3576 camera recording and encoder tools

[Project overview](../../../../README.md) · [Documentation index](../../../../docs/README.md)

This directory contains the production SC233HGS recording worker, session management, MPEG-TS storage, read-only inspector, and independent encoder probes. MPP is Rockchip's hardware video encoding interface. FFmpeg's libavformat packages encoded H.264 into MPEG-TS, a segmented video container. Production recording is integrated with the ROS node through the platform camera boundary.

## Build and enable

The repository's `./build.sh` enables `DRONE_TRACKER_BUILD_RECORDING=ON` and produces ARM64 output. Manual root CMake builds can select that option for RK3576; Jetson rejects it. The installed `rk3576_recording_worker` and `rk3576_recording_inspect` live beside `drone_tracker_node`. The worker must remain in that directory. MPP and container libraries are private recording-program dependencies, not shared algorithm dependencies.

For a standalone native ARM64 SDK build:

```bash
cmake -S src/platform/rk3576/recording -B build-recording -DCMAKE_BUILD_TYPE=Release
cmake --build build-recording -j4
```

Cross-compiling this standalone directory requires the toolchain and target paths in [the image guide](../../../../dockerfiles/README.amd64.md). It needs matching MPP, FFmpeg, OpenCV, threads, and YAML development dependencies.

Build support alone does not start recording. `recording.enabled` controls the SC233HGS original-camera video branch; `need_save` independently controls ROS bags, and `visualized_result` controls live debug images. The production SC233HGS template enables both recording and bag. AR0234/video templates disable recording. Enabling it for AR0234 or video reports independent recording unavailable on ON while healthy tracking can continue.

Example ROS parameters, keeping the remaining validated camera/models/OSD settings:

```yaml
drone_tracker_node:
  ros__parameters:
    recording:
      enabled: true
      directory: /var/lib/drone_tracker/recordings
      bitrate_bps: 12000000
      segment_seconds: 60
      sync_interval_ms: 1000
      max_total_bytes: 21474836480
      min_free_bytes: 5368709120
```

All recording parameters are startup-only. Directory must be absolute, writable by the runtime user, and on local ext4 as verified from mount information. Its parent must exist. Do not clear historical sessions in startup, install, or upgrade scripts. At 12 Mbps, video is roughly 5.4 GB/hour before content/container variation. Segment duration is 10..600 seconds; cutting waits for the next IDR, a frame that can start decoding without earlier images. Synchronization interval is 500, 1000, or 2000 ms and is not a maximum power-loss window.

The default total limit is 20 GiB, including incomplete sessions and sidecars, with at least 5 GiB free. Reaching either protection stops the current session and retains previous files. There is no circular overwrite or automatic deletion. Export, verify, and apply the operator's retention policy between sessions.

## Capture ownership and sessions

Video copies the full accepted NV12 camera frame, containing luminance and chroma, before hardware-buffer reuse. It does not use annotated ROS or HDMI output and adds no crop, resize, flip, or overlay. H.264 is lossy, so decoded pixels are not identical to the pre-encoding input.

`CameraNv12Sink` stages a candidate, then publishes or cancels it at the final capture boundary. Driver return, stop, and warmup-deadline checks complete before acceptance. Four shared slots transfer pixels to the worker; full slots drop the current recording frame instead of blocking healthy capture. Slot consumers finish copying before releasing a slot. The fixed session dispatcher retains one session through each candidate's complete stage/publish/cancel/disposition sequence.

For RK3576 camera input with `osd.enabled=true`, service startup opens capture and preview without preparing a recording worker or copying recording pixels. ON requests a session only when recording is enabled. A serial recording controller prepares resources while fresh unannotated preview and telemetry continue.
Initial tracking waits for a frame accepted by that exact recording request. Frames observed before
acceptance are not replayed into the session. This delay is recorder preparation latency, bounded by
the existing three-second preparation budget; there is no additional node readiness timer. OFF closes acceptance immediately and cancels candidates that have not passed final acceptance. Already accepted frames drain within the three-second parent-close budget; resource preparation has its separate three-second budget. An unreaped worker retains isolated storage and prevents another worker from being created. OFF leaves continuous capture/HDMI running.

With OSD disabled, original ON/start and OFF/stop capture lifetime remains. SC233HGS warmup precedes eligible frames. In continuous mode warmup belongs to the stream and can precede multiple recording sessions; ON does not repeat it. Each session records stream identity, tracking round, reception-boundary count snapshots, and its own end reason. `camera_running_at_end=true` means recording stopped while preview continued.

PTS, the video presentation timestamp, is zero for the session's first accepted frame. Later PTS values retain real capture-time gaps, including recording drops. They have not been validated as exposure times. The worker encodes H.264 Baseline Level 5.0 without B frames, requesting IDR approximately each input second. Its supported color contract is full range with the accepted default matrix/transfer metadata; unsupported marking is rejected.

## Files, health, and inspection

A new exclusive `session-…` directory contains:

| File | Purpose |
|---|---|
| `session.json` | Program, model, configuration, board, and actual input-format identity |
| `000001.ts` and later segments | Original-camera encoded video |
| `000001.frames.jsonl` and later sidecars | Capture identity, PTS, encoder time, and complete-frame end byte offsets |
| `events.jsonl` | Distinct driver gaps, tracker queue drops, and recording-branch drops |
| `commits.jsonl` | Synchronized video/sidecar commit boundaries |
| `camera.json` | Warmup, qualified-frame, and termination counts |
| Segment/session `complete.json` | Finished cleanup outcome; inspect `outcome` and `video_success` |

Missing completion markers mean unfinished output. `.pending` metadata remains as failure evidence. Zero-frame cancellation preserves its reason without creating a placeholder TS video. Normal process exit alone does not establish a successful video session.

A synchronization watermark reports the last successfully synchronized input position; before establishment it is unknown. Qualified input lag exceeding two seconds marks degradation with the default policy. Five seconds without synchronization progress or an actual synchronization timeout ends that recording session. Preparation, storage, and encoding failures report separately and leave healthy capture, tracking, and display running. Sudden power loss may remove unsynchronized data and does not guarantee a final marker.

Use the matching inspector to create a new report without changing originals:

```bash
rk3576_recording_inspect /var/lib/drone_tracker/recordings \
  /var/lib/drone_tracker/inspection-001.jsonl
```

It bounds line size, file count, memory, and report size while checking metadata, parseable commits, and complete TS packet prefixes. Packet structure is not decoding validation. A recovered commit record alone cannot prove that a synchronization call returned before power loss. General-decoder checks and actual power-loss experiments are separate evidence.

## Independent encoder probe

`rk3576_recording_probe` remains an experiment alongside production recording. Its arguments are exclusive output path, FPS, bitrate in bit/s, and frame count. For synthetic 1920×1200 NV12 input:

```bash
./build-recording/rk3576_recording_probe /persistent/new-output.h264 60 12000000 600
```

To use a verified SC233HGS topology, append capture device, media device, and sensor entity:

```bash
./build-recording/rk3576_recording_probe /persistent/new-camera.h264 60 12000000 600 \
  /dev/video22 /dev/media2 'm00_b_sc233hgs 0-0030'
```

Confirm actual wiring with `v4l2-ctl --list-devices` and `media-ctl -d /dev/media2 -p`. These node numbers are examples. Synthetic input measures maximum submission throughput without camera pacing. Probe output is raw H.264, not the production session's MPEG-TS/sidecar protocol. The probe's in-process threads do not establish production worker fault isolation. Use an external deadline and inspect frame/drop/decode/resource outcomes; resident memory excludes hardware-internal allocations.

For application rollback, close the active round and exit cleanly, then disable `recording.enabled` and restart. Retain recorded sessions and the matching inspector. Do not replace system RKNN Runtime. See [production integration evidence](../../../../openspec/changes/archive/2026-09-24-add-rk3576-camera-recording/final-validation.md) and [the platform guide](../../README.md). Historical measurements apply only to their recorded software and inputs. `.docker-artifacts` and `/tmp` may be cleared; copy required evidence to separately managed storage rather than assuming those paths remain available.

## Startup frame acceptance and evidence

For SC233HGS continuous preview with video recording enabled, each tracking candidate carries an
immutable recording receipt. This value records final queue acceptance, the stream, tracking round,
request generation, 64-bit capture id, driver sequence, and original microsecond capture timestamp.
The generation changes on each requested session. The capture id distinguishes a wrapping driver
sequence. These values remain available with diagnostics off. Acceptance means publication to the
bounded encoder queue; it does not mean encoding, writing, or synchronization has completed.

The startup filter checks each dispatched and consumed candidate until the first successful tracker
initialization. A negative YOLO result, a detected box whose initialization fails, or worker readiness
does not end filtering. Accepted frames enter ordinary video immediately even when no balloon is
present. Rejected candidates do not create tracking observations or debug images. Fresh preview
continues before algorithm processing begins. After processing begins, HDMI retains its existing
same-image result pairing; a withheld candidate never receives an older image's box.

For example, preparation frames 8884 through 8891 are withheld. Detection can execute on accepted
frame 8892 or a later accepted frame. A submitted detector task may be replaced before execution,
so submitted input is not execution evidence. If detection executes on frame 100 and its result
successfully initializes the tracker while processing frame 108, initialization evidence identifies
frame 100. After the first successful initialization, ordinary scheduling resumes. Later target loss
does not reactivate startup filtering. This change makes no promise that every steady-state or
recovery input is recorded.

OFF freezes the round's two source records before closing recording acceptance. Late callbacks
retain only their collector and cannot modify frozen or newer-round evidence. Terminal failure of
the current request releases startup filtering for the remainder of the round. Healthy tracking then
uses the current live input boundary, with coverage explicitly unavailable. Recording disabled,
AR0234, file inputs, OSD-disabled capture, and ROS bag policies retain their existing behavior.

New frame sidecars add `capture_id`, `stream_id`, `tracking_round`, `request_generation`, and
`segment_frame` (the zero-based ordinal within the segment). Existing identity and capture-time
fields remain unchanged. `camera.json.startup` contains at most `first_yolo` and
`first_initialization`, their accepted receipts, request identity, monotonic transition times, and
an outcome. Absent initialization has `initialization_outcome: not-applicable`.
`preparation_omitted` counts qualified capture visits before receiver attachment.
`dispatch_withheld` and `dispatch_rejected` count dispatcher candidates, while `pending_replaced`
counts superseded node tasks. These observation points overlap; do not sum them. Recorder drop
counters keep their existing meaning and do not include preparation omissions.

The matching inspector streams bounded sidecars and compares exact source identities, including
session/request keys. Normal completion or zero aggregate drops alone cannot verify coverage.
Its `startup_coverage` distinguishes `unknown`, `accepted`, `metadata-matched`, `verified-written`,
`failed`, and `not-applicable`. Older sessions with no startup fields remain `unknown`.
Use the optional general FFmpeg decoder readback to confirm readable frames in matched segments:

```bash
rk3576_recording_inspect /var/lib/drone_tracker/recordings \
  /var/lib/drone_tracker/startup-readback-001.jsonl --verify-video
```

The command creates a separate exclusive report and never edits the recording. Readback requires
installed `ffmpeg`, fails on decoding errors, and checks the decoded frame count against complete
sidecar records in each matched segment. Each decoder process has a 180-second deadline. Without
readback, exact membership is only `metadata-matched`. Retain originals, reports, executable/model
hashes, commands and exit statuses together. Parent, worker and inspector must come from the same
release because the shared-memory identity layout changed.

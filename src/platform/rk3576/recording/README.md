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

For RK3576 camera input with `osd.enabled=true`, service startup opens capture and preview without preparing a recording worker or copying recording pixels. ON requests a session only when recording is enabled. A serial recording controller prepares resources while tracking and preview continue. Frames observed before readiness are not replayed into the session. OFF closes acceptance immediately and cancels candidates that have not passed final acceptance. Already accepted frames drain within the three-second parent-close budget; resource preparation has its separate three-second budget. An unreaped worker retains isolated storage and prevents another worker from being created. OFF leaves continuous capture/HDMI running.

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

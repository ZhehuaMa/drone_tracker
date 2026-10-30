# Production-camera recording design

This archived design records the recording change's original scope and delivery decisions. Statements about the previous implementation, defaults, or outstanding checks refer to that change, not the current checkout. Later lifecycle and bag changes have their own specifications.

## Context

Production needs camera video without detection boxes, tracking boxes, or text for incident review and real-data collection. At the start of this change, camera input rejected recording. The goal was continuous recording from each round's first qualified post-warmup frame, with recording failures isolated from tracking. Warmup observes strictly increasing timestamps before image delivery; it does not establish stable exposure or image quality. See [proposal.md](proposal.md) for motivation and scope.

The prerequisite is the [startup-warmup change](../2026-09-23-fix-sc233hgs-startup-timestamp-warmup/design.md). Three raw-driver captures on the production board on 2026-09-23 all had initial repeated timestamps. Implementation therefore cannot assume every successful dequeue is immediately recordable. The camera change defines fixed warmup thresholds, tolerated startup repeats, and strict failures after warmup. This design defines how recording consumes qualified frames.

Observed integration boundaries:

- `Rk3576CameraCaptureBase::next()` copies luminance into an independent Gray8 frame before returning the driver buffer. SC233HGS chroma does not enter the public frame. Gray8 stores one eight-bit grayscale value per pixel; NV12 also has an interleaved chroma plane.
- `processFrames()` drains the queue to obtain the latest frame, so it cannot supply complete recording. `/fpv/tracker_image` already contains boxes and OSD has its own display pipeline. Neither is a recording source.
- `openRecording()` couples the old `need_save` option to ROS bag message files. New camera video needs an independent failure state and cannot use the old bag-write failure path that stops the whole round.
- The SC233HGS wrapper reuses AR0234 frame-source scheduling. Shared capture changes require AR0234 regression checks. This change's deltas must narrow the previous main-spec prohibitions against all recording and additional image queues.
- Read-only production inspection on 2026-09-23 found MPP service devices, libraries, and encoder tools. MPP is Rockchip's hardware media interface; file presence does not establish encoding performance. Root is on eMMC ext4, `/tmp` is volatile memory storage, and `/userdata` has insufficient capacity. Recheck at deployment; transient free space is not a hardware guarantee.

## Goals / Non-Goals

**Goals:**

- Prepare a session on camera ON, begin video at its first qualified frame, and stop on OFF. Continue recording without a detected target; OFF does not retain camera streaming in this original lifecycle.
- Branch from complete NV12 while preserving algorithm input pixels, timestamps, and queue consumption.
- Make encoding, writes, synchronization, memory, and closing observable; bound resource competition with tracking.
- Retain a complete prefix readable by general video tools and its correspondence to actual capture times.

**Non-Goals:**

- Sensor RAW, lossless video, additional image enhancement, or dynamic recording-interface switching.
- An offline conversion tool or expanded production replay decoding. Acceptance may use general decoders to inspect output.
- Power interruption, unplugging, forced reboot, collision, or destructive storage checks. Process termination is not evidence of real power failure.
- Circular overwrite, a low-battery subscription protocol, remote backups, or AR0234 recording.

## Decisions

### 1. Independent startup configuration and whole-camera rounds

Choose startup-only `recording.*`, disabled by default and explicitly enabled for production deployment. Keep RK3576 camera `need_save=false` in this change and do not automatically create ROS bags. Reject runtime recording-configuration changes. Video evidence and old message recording have different outputs and failure semantics; reinterpreting `need_save` would change video/Jetson behavior.

These initial settings are implementation configuration, not already measured device capabilities:

| Parameter | Default | Validation and effect |
|---|---|---|
| `recording.enabled` | false | Only RK3576 SC233HGS accepts true; other inputs explicitly reject it in this design |
| `recording.directory` | `/var/lib/drone_tracker/recordings` | Absolute path; enabling checks the actual persistent mount. Initial production supports local ext4 and rejects tmpfs, ramfs, and network filesystems |
| `recording.bitrate_bps` | 12000000 | Positive integer accepted by the encoder. Three field-image-quality scenarios and bitrate comparisons are outside this acceptance |
| `recording.segment_seconds` | 60 | 10–600 seconds; split at the first IDR after the interval |
| `recording.sync_interval_ms` | 1000 | Only 500, 1000, or 2000, for resource and lag comparisons |
| `recording.max_total_bytes` | 21474836480 | Recording and sidecar quota, default 20 GiB; positive |
| `recording.min_free_bytes` | 5368709120 | Filesystem free-space reserve, default 5 GiB; positive |

Use original dimensions, attempt every frame that passes camera warmup and normal time checks, encode H.264 into MPEG-TS, request IDR about once a second, and use no B-frames. IDR is a keyframe that starts decoding without previous images; B-frames introduce reordering waits. Initial scope excludes dynamic bitrate reduction and adaptive frame rate. Congestion drops current raw recording frames and records those drops; it does not change camera/algorithm rates. Warmup observation discards are not recording-congestion drops.

Invalid values, unsupported inputs, and volatile-storage configuration fail initialization explicitly in this design. A valid configuration encountering permissions, insufficient space, an unavailable encoder, or failed worker startup reports recording failure for that round but permits camera tracking. Configuration errors remain visible without making runtime recording resources a prerequisite for the core task. A later ON can retry; the current round does not repeatedly restart recording.

### 2. Branch complete NV12 within the driver-buffer lifetime

Add an optional nonblocking original-image sink to the shared capture base, enabled only for SC233HGS. Only a candidate passing metadata and time checks may be staged. Publish it to the worker only after final camera confirmation. Publication permits the worker to read and encode it. Copy valid Y/UV rows into preallocated recording slots before `VIDIOC_QBUF` returns the driver buffer. Validate row stride, chroma offset, valid length, and encoder alignment separately. Encoder padding is outside the visible image. Preserve driver color range/matrix metadata, explicitly mark unknown values, and do not stretch ranges automatically.

Tracking and recording share one warmup decision. Do not independently filter or cache observation frames for recording: both branches must use the same qualified identity. Recording before filtering would allow repeated timestamps into video. The sequence is:

1. Dequeue successfully and check safety.
2. Apply warmup or normal time checks.
3. Stage candidate pixels.
4. Return the driver buffer.
5. Recheck stop and the final warmup deadline.
6. Publish the qualified frame.

The encoder cannot read a staged slot before publication. Stop or failed final checks cancel it. Warmup discards only return the buffer and retain metadata; they copy no recording pixels. Fatal shared-boundary errors such as failed driver return still fail capture under its contract. Cancel the unpublished candidate, retain its terminal-disposition identity, and exclude it from encoding and video-frame records.

Reject a second camera open, another consumer of the tracking queue, and recapture from ROS/OSD. Recording must neither steal algorithm frames nor inherently lose every frame skipped by a slow tracker. The cost is one complete image copy: 1920×1200 NV12 is about 3.456 MB/frame, or about 207 MB/second at 60 fps. Actual memory-bus traffic is higher and must be measured.

Use four shared raw-frame slots with a total limit of 64 MiB; reject an oversized session. A full slot set drops the current new recording frame while preserving algorithm-queue rules. Convert recording-branch submission failures into recording faults at this boundary rather than propagating exceptions that stop capture. Disabled recording allocates and copies no NV12 recording buffer.

### 3. Independent worker process and explicit ownership

Start a new RK3576 recording program with `posix_spawn`; avoid complex library calls after fork in a process already running ROS/inference threads. Public configuration/status contains ordinary values, and MPP types remain platform-private. Preallocated shared slots, round identities, and nonblocking notifications transfer images.

The parent writes only free slots. The child copies a claimed slot into its own MPP input buffer, then acknowledges slot reuse. Retain MPP buffers until hardware has stopped accessing them. Driver-buffer ownership must not extend into encoding or disk writes. Cross-process synchronization must not let a process dying while holding a lock block capture forever.

Separate encoding and storage inside the child. Encoded-output buffering is limited to 8 MiB. Output congestion stops admission of raw frames. If encoded dependencies cannot be retained safely, fail the recording round rather than dropping arbitrary P-frames and claiming completeness. The parent detects worker exit, detaches submission, and reports failure.

Choose process isolation over exception-catching threads to isolate encoding-library crashes and blocked writes. This adds a process protocol and shared-memory management without changing the public tracking state machine. Signals cannot guarantee immediate exit from uninterruptible kernel I/O. The parent must neither wait indefinitely nor reuse resources still accessed by an old worker. Isolate at most one unreaped old worker per node; until it exits and is reaped, later rounds report recording failure without accumulating processes or buffers.

### 4. Hardware H.264 and independent MPEG-TS files

Use MPP encoding and libavformat MPEG-TS encapsulation. libavformat is FFmpeg's container library. Custom file I/O retains the underlying descriptor to control application-buffer flushes and data synchronization. Do not depend on an installed ffmpeg command or assume the standard FFmpeg encoder supports Rockchip hardware. Link MPP and the container library only into recording programs.

Reject ordinary MP4 because abnormal termination may leave required indexes missing. Fragmented MP4 can recover complete fragments but needs additional index/sync coordination. MJPEG is convenient per frame but usually needs more storage bandwidth at comparable quality. Each MPEG-TS segment has its own stream information and encoding parameters, without a previous-file dependency or external playlist. Start every file with IDR and repeat necessary decoding parameters. Original filenames always end in `.ts`.

Derive video PTS, the presentation timestamp, from capture time relative to the first qualified frame accepted for recording in that round. Its PTS is zero. Record stream start, warmup completion, and video origin separately; neither ON nor an observation frame supplies the video origin. Retain integer microseconds and conversion information. Do not invent constant timing from frame counts. Drops retain their gaps; long image absence creates no synthetic duplicates. Request IDR according to qualified capture time and verify actual output. Continued qualified input without expected IDR for more than two seconds stops recording as an encoding failure. Start that progress timer when the first qualified input reaches the recording-submission boundary; repeated rejection cannot reset it. Warmup starts no such timer. A split places the new IDR in the new file, and the old segment must not depend on the new one.

### 5. Synchronization period and actual progress

Use a default 1000 ms synchronization period. Flush container/application buffers at least every 200 ms so healthy writes do not retain a whole segment. `avio_flush` hands library-buffered bytes to the OS; `fdatasync` requests file-data and required-metadata synchronization. New files/directories and renames also require `fsync` on their relevant parent directories. Do not sync every frame, invoke global `sync`, discard kernel caches, or change global dirty-page settings.

Persistence sequence:

1. Exclusively create a unique round directory. Write and synchronize the session manifest and parent directory. Create the video segment/header and synchronize required directory changes only when the first accepted qualified frame arrives.
2. Write video continuously. Record file end offsets and original frame times only at complete encoded-frame boundaries.
3. At the synchronization deadline, make the container writer emit pending complete frames, flush its I/O buffer, then call video `fdatasync`. Register only complete frames actually written to the descriptor; encoder packets or container-function returns do not prove this. Check every return value and do not advance the confirmed position on failure.
4. Write and synchronize timestamp sidecars, then update and synchronize the commit record. A commit references only a successfully synchronized video boundary. Failed sidecars fail recording; they do not produce a complete-evidence claim.
5. Apply the same synchronization at normal close or split. Write the normal completion marker and claim completion only after that marker and directory changes synchronize successfully.

Use a monotonic clock for sync scheduling and operation timeouts so calendar correction cannot change them. Permit only one storage synchronization in flight. Do not create unbounded sync threads or silently lengthen the period to hide insufficient performance. Health includes `last_synced_frame_time_us`, the last successful synchronization's monotonic time, duration, and failure reason.

The parent computes persistence lag as latest qualified capture time minus the last synchronized frame time. Also track elapsed time since last successful synchronization. Include qualified frames rejected by recording, but exclude warmup observations. No synchronized frame means unknown, not zero. With defaults, warn after two seconds of lag. Continued qualified input without persistence progress for five seconds, or a single synchronization wait exceeding five seconds, stops recording submission and reports failure. The no-progress deadline starts at the first qualified frame reaching the recording boundary. Rejection or delayed first-segment creation cannot postpone it repeatedly. Warmup is not video-persistence stagnation; an actual file sync already started still has its timeout. Stopped capture cannot make an unchanged old lag a sign of continued health.

A one-second request period is not a one-second maximum-loss guarantee during power failure. Queues, encoding, synchronization latency, and device caches affect recovery. The normal-load candidate target is persistence-lag P99 no greater than two seconds with defaults. P99 is the value covering 99% of samples; report all maxima too.

### 6. Evidence files and space protection

A round with video contains `session.json`, numbered `.ts` files, per-segment `frames.jsonl`, and appended commits. JSONL stores one JSON record per line, so complete prefixes survive tail truncation. Every actual video-frame record contains round, driver sequence, driver timestamp/flags, mapped microsecond time, video PTS, segment number, and complete encoded-frame end offset. Separately record recording-queue drops, driver-sequence gaps, and tracking-queue drops.

Session evidence includes camera warmup thresholds, duration, observed/discarded/repeated counts, outcome, and first qualified identity. With zero frames, identity and video origin are unknown. Bounded warmup metadata does not enter video-frame records or increment PTS/frame counts. Reconcile camera dequeues, warmup discards, qualified frames, and terminal dispositions under the camera specification. Separately reconcile recording attempts, admissions, rejections, encoded frames, and tail dispositions. Driver-return failure, failed final deadline, or stop cancels a staged unpublished frame by capture identity; it is neither accepted nor encoded.

The manifest identifies program/configuration/models, actual camera format/color metadata, encoding settings, board, and time source. Compute file fingerprints before capture or reuse already obtained values; do not reread large models repeatedly in the capture path. Existing logs and status text report health, with at most one regular summary per second and prompt failures. Do not change the six business topics or tracking-state values.

Restart inspection recognizes incomplete sessions only within the configured recording directory. Preserve originals and write a separate report. Production inspection checks manifests, commits, and complete TS-packet prefixes; it neither transcodes nor rewrites video and adds no playback decoder. General acceptance tools independently establish decodability. Packet alignment alone is not decoding success. Inspection must run independently with rate limits rather than delay camera ON.

Quota accounting includes complete/incomplete sessions and sidecars. Before each new write, check requested bytes against quota and free space. Reserve capacity for tail records and account for external disk-usage changes. Hitting a protection threshold stops recording, preserves old files, and explains the cause. Soft protection cannot prevent other processes exhausting disk concurrently; actual ENOSPC remains a recording failure.

Preserving evidence rather than circular overwrite requires operators to export and clean between missions. Boot tasks must not empty recording directories, and installations/upgrades must not overwrite sessions. This retention rule concerns production recording originals; `.docker-artifacts/` and temporary validation outputs are disposable and need separate managed retention when required.

### 7. Start, stop and recovery order

Normal ON sequence in this original design:

1. Validate static parameters, create round identity, and prepare worker/buffers. Resource preparation waits at most three seconds; failure marks this round as not recording and still allows camera startup.
2. Successful stream start begins the camera's independent warmup deadline. Recording waits for qualified input. Only camera-qualified frames attempt recording and original algorithm-queue submission, whether or not a target is detected. The three-second recording-preparation budget does not consume the post-stream-start warmup budget; record both origins and durations.
3. OFF or camera failure first closes recording admission and stops capture, preserving the original camera-stop threshold.
4. Independently request recording drain, synchronization, and exit, waiting at most three seconds by default. Timeout revokes the session, retains incomplete status, requests worker termination, and monitors reaping asynchronously.
5. Models retain their original safe wait for in-flight accesses. Report recording close separately from model waits; three seconds does not bound the node's entire OFF operation.

Drain normal raw-queue tails. Count and explain tails discarded after timeout. New rounds use new buffers/identities; an old unreaped worker blocks new recording under decision 3 while tracking may restart. Parent-death notification and control-channel closure tell the worker to stop after abnormal parent exit; do not leave an unbounded independent capture process.

OFF during warmup preserves a zero-frame `cancelled_before_first_frame` session. Warmup failure preserves `camera_warmup_failed` and the original camera reason. Without actual video input, create neither `.ts` nor video-frame records or placeholder images. Session metadata may close normally, but cannot claim successful video. Failure remains failure, and cancellation is not timeout. Reclaim prepared resources within their separate budgets. If storage itself is unwritable, preserve failure at least in node logs; a session file in that directory cannot be guaranteed. Zero-frame sessions cannot all be labeled encoding failures because cancellation/camera failure are distinct causes.

### 8. Acceptance through non-destructive checks

Use three validation layers because the user explicitly excludes power interruption. Real power-loss checks are neither a completion prerequisite nor an unchecked implementation task.

- No-hardware checks: padded NV12 Y/UV content, round isolation, bounded buffers, sync-call ordering, failure positions, short writes/EINTR/EIO/ENOSPC, child death, and sync stalls. Inject failures at controlled boundaries without filling the system disk.
- Joint warmup checks: repeated-time observations never enter video; first accepted qualified input has PTS zero and requests IDR. Warmup OFF/failure and cross-round restart produce accurate zero-frame outcomes. Waiting for warmup does not trigger video stagnation; continuous qualified input that is rejected cannot postpone deadlines indefinitely.
- File-recovery checks: truncate copies near TS packets, encoded frames, synchronization, and splits at at least 100 offsets. General decoders verify complete prefixes and relate last decodable frames to truncation. Hardware scope is one recording-child termination and one abnormal parent exit; retain controlled repeats without adding more hardware failure runs. Keep the board powered and do not reboot.
- Production use: default 12 Mbps and 1000 ms sync, five continuous recording minutes with tracking and OSD. Check normal OFF, another ON, default 60-second splits, independent segment decoding, and frame accounting. Do not run further multi-period comparisons, twenty-round restarts, or thirty-minute load.

The user's 2026-09-24 completion scope is practical usability: five minutes of healthy tracking/display, no application recording drops, and decodable video reconciled against capture submissions. Default synchronization-lag P99 must be at most two seconds; report maxima unchanged. Remove the dedicated relative-baseline P95/throughput release gate without claiming performance is unaffected or long-term stability is established.

Count five minutes after the first qualified frame, excluding warmup. Keep warmup discards separate from recording drops and do not relabel normal-stage drops as warmup. Preserve preparation, warmup, first-frame, and unknown-sync evidence; unknown values are not zero. Use existing component evidence and small targeted checks for warmup cancellation/failure and blocked synchronization, without more repeated stress runs.

Report synchronization lag, CPU, memory, partition writes, and temperature available during the run. Without reliable equipment, explicitly leave power consumption unmeasured and add no separate measurement campaign. Fix known practical-use failures without lowering dimensions/frame rate or hiding failure. The three field-image-quality scenarios and bitrate comparison remain excluded; decodability is not image-quality acceptance.

## Risks / Trade-offs

- **Real eMMC power loss or media damage:** file/directory sync and streaming containers reduce risk. Real power-loss recovery is unverified; hardware damage can lose local data.
- **Encoding competes with NPU/RGA for bandwidth:** bounded buffers, hardware encoding, and a five-minute production joint check provide limited evidence, without a long-term performance promise.
- **Uninterruptible kernel I/O:** bound parent waits, isolate old resources, and prevent accumulating workers; immediate force-kill is not guaranteed.
- **Lossy compression harms small targets:** preserve original dimensions and do not call decoded video exact algorithm input. The user removed the three field-quality scenarios and bitrate comparison on 2026-09-23, so those properties are unverified and outside completion.
- **Shared capture changes affect AR0234:** the default path has no recording buffers; retain AR0234's recording rejection and its existing checks in this change.
- **Original replay cannot read new files:** document H.264/TS versus MJPEG boundaries; add no conversion or decoder extension here.

## Migration Plan

Prerequisite: finish `fix-sc233hgs-startup-timestamp-warmup` capture implementation and production acceptance before recording integration. Synchronize warmup before recording. Preserve overlapping ROS ON/OFF warmup text and scenarios so a later sync cannot erase them. Warmup must not depend on recording; its independent acceptance remains model/display-free.

1. Verify production MPP header/library ABI, meaning call compatibility, and actual encoding settings in an independent target. Use established build conventions. Missing SDKs require explicit recording dependencies, not an unauthorized image rebuild or system RKNN replacement.
2. Integrate the optional branch disabled by default. Update the three existing capability deltas above and preserve old `need_save` semantics. Check concurrent HDMI changes without overwriting them.
3. Complete simulation, ARM64 builds, non-power-loss recovery, and identified production-board acceptance. Prepare persistent storage with minimum permissions before deployment; do not make it world-writable.
4. Enable recording in a validated production configuration and retain exact frame rate, bitrate, synchronization period, and metrics. Roll back by setting `recording.enabled=false` and restarting; keep existing video files.

## Open Questions

The remaining measurements within this design are actual production MPP version, supported alignment/rate-control settings, and five-minute default recording results. If hardware cannot meet fixed format, full frame rate, or release thresholds, revise the design rather than letting implementation silently relax them. Default 12 Mbps does not establish manual small-target, fast-motion, or low-light quality; the user explicitly excluded that acceptance.

## References

- [Rockchip MPP](https://github.com/rockchip-linux/mpp): hardware-encoding interface and platform implementation. Use production inspection for the deployed version.
- [FFmpeg container documentation](https://www.ffmpeg.org/ffmpeg-formats.html): MPEG-TS, fragmented MP4, and container-buffer options.
- [Linux fsync/fdatasync documentation](https://www.man7.org/linux/man-pages/man2/fsync.2.html): file/directory synchronization and error semantics.

## Confirmed first-frame boundary — 2026-09-23

Recording begins at the first frame the camera finally confirms as valid. If a candidate passes its preliminary check at 1.999 seconds but copying and driver return finish at 2.001 seconds, existing camera rules still report warmup timeout. Cancel staged pixels and retain a zero-frame failed session. Recording must not remove or advance the original final camera check.

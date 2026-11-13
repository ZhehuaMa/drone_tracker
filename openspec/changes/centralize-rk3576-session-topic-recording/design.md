# Design

## Context

The current recording omits application messages needed to explain a flight session. The intended result is one recorder that saves the known bridge, tracker, and interceptor interfaces during ON/OFF rounds, while deliberately limiting selected telemetry and image samples. See [proposal.md](proposal.md) for the user-visible motivation.

The inspected baseline is `drone_tracker` rk3576 revision `e17c43e`, with interface evidence from `apm_bridge` `31b00e8`, `drone_interceptor` `73c605c`, and shared `quadrotor_msgs` `bc8bf6c`. These are source-review identities, not a hardware acceptance claim.

The later CPU-placement refinement selects CPU 2 for both topic-recorder threads in the standard installation. CPU affinity is the set of cores on which a thread may run; it does not reserve or isolate a core. The [2026-11-13 production measurements](../../../docs/reports/performance/RECORDER_CORE_AFFINITY_20261113.md) motivate that placement. They used temporary thread binding and are not acceptance evidence for the new automatic role or installer behavior.

At this baseline:

- `src/bag_session.h` owns a worker thread and a queue limited to 256 messages and 64 MiB of estimated payload. Its thread alone opens, writes, and closes the bag. Its current byte accounting stops charging an item before its write completes and generally uses `sizeof(Message)` for non-image data; neither accounts fully for retained dynamic payload.
- `src/drone_tracker_node.*` owns the session and submits recording work from several callbacks. The MAVROS body-IMU subscription and odometry callback are recording-only. The gimbal callback also updates the algorithm's camera-orientation history.
- `publishObservations()` offers bbox and LOS together before two separate ROS publish calls. `submitPair()` admits both messages or neither. OFF offers a final invalid pair before sealing. A pair already constructed for publication is evidence even when one subsequent ROS publish fails.
- The current bag topic list is gimbal, odometry, body IMU, bbox, and LOS. Its specifications exclude debug images for every value of `visualized_result`. This change explicitly supersedes that exclusion in the two affected deltas.
- `src/drone_tracker_main.cpp` runs tracker control in a single ROS executor. An executor is the scheduler that invokes ROS callbacks. CPU-affinity helpers distinguish ROS transport creation from control and provide an existing recording role.
- The bridge publishes retained `/fpv/gimbal_mode` strings. The retained-delivery setting, called `transient_local` in ROS, lets a late subscriber receive the publisher's last saved value. Its other sensor and event interfaces and the interceptor's configurable topic names define the fixed catalog.

## Goals / Non-Goals

**Goals:**

- Give one component ownership of all RK3576 recording configuration, subscriptions, policy, lifetime, and reporting.
- Keep source-time evidence and observation pairing correct across asynchronous recording and rapid session changes.
- Bound recording work before it can consume unlimited memory or wait for storage in an algorithm callback.
- Make the effective topic coverage and each kind of omission inspectable after a session.
- Install a standard policy that automatically binds both topic-recorder threads to CPU 2 without moving camera-video recording or general ROS communication work.

**Non-Goals:**

- No recording process, recorder service, continuous startup-to-shutdown recording, ordinary-message pre-roll, or post-OFF tail window.
- No runtime topic discovery, arbitrary message-type plugins, automatic deletion of old evidence, new ROS control topics, or changes to sibling repositories.
- No changes to tracking algorithms, state encodings, published message structures, source publication rates, camera acquisition, RKNN Runtime, or Jetson recording.
- No promise of zero overhead, exact algorithm-input replay after sampling, hard bounds on blocked filesystem calls, or a guaranteed power-loss retention interval.

## Decisions

### 1. One manager, one existing bag writer, and one recording executor

**Choice.** Add `src/recording/recording_manager.*` as an RK3576-only C++ component owned by `DroneTrackerNode`. It owns the catalog, typed recording subscriptions, sampling state, active session handle, counters, retained-mode cache, and `BagSession`. Extend the existing bag writer instead of creating another one.

Use a dedicated recording callback group on the existing ROS node. A callback group assigns subscriptions to a particular executor. Create it with automatic executor attachment disabled, and attach it explicitly to one recording `SingleThreadedExecutor` thread. The main tracker executor must not also execute this group. Sharing the node's ROS interfaces preserves the same namespace and launch remappings without introducing another parameter server or process.

Create subscription resources during controlled initialization, but start the recording executor only after node construction has succeeded. Start it for every enabled RK3576 input mode before tracking can establish a session, rather than hiding it behind the continuous-preview-only startup branch.

Create and destroy recording ROS resources through the existing ROS-transport affinity boundary. For an explicit `roles.topic_recording`, start both the dedicated recording executor and every session writer through that topic-recording role. The standard role-pinned deployment policy selects CPU 2. Keep ROS resource creation and its middleware helper inheritance on `RosTransport`; keep camera-video controllers and encoder processes on `Recording`. These remain CPU 6 and CPU 7 respectively in the standard policy. Do not implement production binding through thread-ID scans, creation-order guesses, or an external `taskset` adjustment after startup.

Each topic thread applies and reads back its selected mask before running callbacks or writer work. Its creator treats it as ready only after that confirmation. A topic-thread binding failure propagates through the existing recorder-unavailable or session-failure path, cleans up any created thread, and leaves healthy tracking usable. Global CPU-policy syntax errors retain their existing startup-failure behavior. No affinity change alters priorities, frequency settings, kernel isolation, other existing threads, or other processes.

**Why.** This consolidates ownership and moves recording callback work away from tracker control scheduling, while retaining the already implemented writer isolation.

**Rejected alternative.** Keeping old recording calls and adding a second collector for missing topics leaves two architectures. A separate recorder process adds communication and session-coordination work that is unnecessary for the accepted scope. Changing the shared `Recording` or `RosTransport` role to CPU 2 would also move camera-video or general ROS work; hardcoding CPU 2 in worker code would bypass explicit policy overrides.

**Consequence.** There is one bag writer per active session, not a legacy writer plus a new writer. The independent camera-video worker and diagnostic process remain separate. The same-process recorder still shares CPU, heap, and filesystem pressure with tracking.

The CPU 2 default is a deployment policy, not a hardcoded core in worker code. Add an optional version-1 `topic_recording` role to the shared CPU-policy representation and validators. When explicitly provided in role-pinned mode, it must be an integer in the allowed set and available on the target. A missing field in a legacy policy used directly at runtime preserves the two old bindings separately: recording callbacks use `RosTransport`, while the bag writer uses `Recording`. A single generic fallback core cannot represent that legacy split. No explicit policy, or explicit `mode: inherit`, continues to inherit affinity without promising CPU 2. Logs identify the actual requested role and readback, so these compatibility paths cannot be mistaken for the standard installed policy.

### 2. A typed startup registry owns every ordinary recording subscription

**Choice.** Add a typed catalog in `src/recording/topic_catalog.*`, using the complete table in [the new capability](specs/rk3576-session-topic-recording/spec.md). Each entry has a stable ID, compiled message type, name, timestamp extractor, delivery profile, sampling limits, and admission class. Typed callbacks use generated ROS message structures; they do not guess offsets in serialized bytes.

Move the existing recording-only odometry and body-IMU subscriptions into the manager. Remove RK3576 recording calls from the business gimbal callback while leaving its pose-history work intact. Add the manager's own gimbal recording subscription. Ordinary output topics, including tracker state and published debug images, use manager-owned subscriptions. The bbox/LOS pair and authoritative trigger are the only direct inputs described in decision 3.

**Why.** All ordinary recording subscriptions now follow one implementation path. Known types make header extraction and dynamic-payload accounting explicit and testable.

**Rejected alternative.** Generic subscriptions plus runtime type discovery add unnecessary configuration and timestamp-adapter complexity for this known application interface set. Sharing only the old gimbal callback would preserve the scattered architecture the user rejected.

**Consequence.** A recording subscription receives its own ROS delivery. For example, a best-effort gimbal recording can differ from the algorithm's own received samples. The manifest describes recorder observations, not an exact algorithm-consumption trace. Recording still pays message reception and typed decoding costs before applying sampling.

Message contracts used by the registry include:

| Type family | Relevant structure and ownership concern |
|---|---|
| `sensor_msgs/Imu` | Header, orientation, rates, acceleration, and fixed covariance arrays |
| `nav_msgs/Odometry`, `geometry_msgs/TwistStamped` | Header and pose/twist data; odometry also has dynamic frame strings |
| `vision_msgs/Detection2D` | Header, floating bbox, dynamic result array, and ID string |
| `sensor_msgs/Image` | Header, dimensions, encoding, row stride, and dynamic pixel vector |
| `mavros_msgs/State`, `ExtendedState`, `RCIn` | Headers and state fields; State has a mode string and RCIn has channel values |
| `BatteryState`, `NavSatFix`, `GPSRAW` | Headers and electrical/GPS fields; battery messages contain arrays and strings |
| `FluidPressure`, `Temperature` | Header, value, and variance |
| `quadrotor_msgs/ControlCommand` | Header, mode/arming fields, expected execution time, attitude/rates/thrust, and rotor-thrust vector |
| `quadrotor_msgs/LowLevelFeedback` | Header, battery/control fields, motor-speed vector, and thrust-coefficient vector |
| `mavros_msgs/StatusText` | Header, severity, and text; the current publisher populates its header |
| `std_msgs/Header` | The trigger itself has `.stamp` and `.frame_id`; it has no nested header |
| `Bool`, `Int8`, `String`, `Float64`, `Float32MultiArray` | No timestamp; the last type contains a layout and float vector |

Keep the actual definitions generated from the real message packages. Do not copy or modify those definitions in this repository.

### 3. Preserve paired observations and authoritative triggers through narrow interfaces

**Choice.** Keep `submitObservationPair(round, bbox, los)` as the tracker-to-manager integration. It selects or rejects a regular pair together, then uses the existing two-slot admission contract. The manager does not also subscribe to bbox or LOS. The final OFF pair bypasses frequency sampling and is eligible for reserved capacity.

The existing trigger callback forwards the original Header and its receipt time to the manager. The manager does not add a second trigger subscription. Assign a local trigger occurrence ID so forwarding and deferred session opening cannot enqueue the initiating ON twice, even when two real commands have identical header contents. Only the original trigger callback decides tracker behavior.

**Why.** ROS topics deliver separately. Reassembling bbox and LOS downstream cannot reproduce evidence of a pair constructed before a publication failure. Forwarding the original trigger avoids a second subscription racing session opening or sealing.

**Rejected alternative.** Independent subscriptions for all outputs can lose one observation member and miss the command that creates or closes the recording interval.

**Consequence.** Tracker integration contains no sampling, disk I/O, queue policy, or timestamp-selection logic. It supplies a pair and lifecycle events. Those two explicit sources are recorded in the catalog, preventing duplicate paths.

### 4. Keep ON/OFF sessions and make generation boundaries explicit

**Choice.** `need_save` remains the recording switch and `save_path` remains the parent recording directory. Create the enabled manager's subscriptions and executor at service initialization, but create a `BagSession` and writer thread only when a tracking round is established. Disabled recording creates neither recording subscriptions nor recording executor/writer threads.

Opening follows this order:

1. The control path assigns a round identity and provides the original pending ON, if the start came from ROS. Programmatic starts without a ROS Header are described in metadata; do not fabricate a topic message.
2. The manager creates a unique run/round session handle, resets session policies and counters, and queues the initiating ON and any known initial mode snapshot.
3. It publishes the active session handle to ordinary recording callbacks. The writer opens storage asynchronously and reports opening failure independently.
4. Recording callbacks capture the active handle and receipt times at callback entry, apply policy, and attempt admission against that same handle.

Closing follows this order:

1. The control path closes normal observation production under its existing synchronization.
2. It forwards OFF and offers the final invalid bbox/LOS pair before calling the manager's seal operation.
3. Sealing atomically prevents further admission to that handle. A callback already holding an old handle can only finish into that old session before sealing or be rejected; it cannot look up a replacement session during submission.
4. Cleanup drains accepted work, closes the writer, and publishes the terminal result. The next session waits for the previous writer's safe finalization under the existing round lifecycle.

EOF, round-ending input/tracking failures, and normal service termination use the same sealing/finalization contract, with their actual end reason. Repeated ON while active is an event, not another writer. Repeated OFF cannot create another final pair. MISSION and DISMISS do not open or close recording independently of the tracker's existing behavior.

**Why.** Explicit handles prevent late callbacks from entering a later session. Enqueueing boundary messages before sealing preserves the current control evidence.

**Rejected alternative.** Startup-to-shutdown recording was rejected by the user. A post-OFF grace window changes the interval and is unnecessary here.

**Consequence.** Only the retained-mode snapshot is recorded from cached idle state. Ordinary pre-ON and post-seal messages are not saved. An interceptor's stop response arriving after OFF sealing is outside the session. Callback entry defines recorder receipt; a delayed message with an older valid source stamp can still be recorded inside an active receipt interval.

Use existing phase names where possible: `disabled`, `opening`, `recording`, `complete`, `incomplete`, and `failed`, with explicit idle/closing management state and a `storage_limited` terminal result. Writer errors take precedence over policy-complete closure. A resource or configuration failure before a session is reported as recorder unavailable.

On final shutdown, stop admission, cancel and join the recording executor, release subscriptions through the ROS-transport boundary, finish accepted writer work, and only then release the manager. Final status reporting must not wait for a stopped executor or access a destroyed tracker. Existing disk-blocking shutdown limitations remain; this design does not introduce forced thread cancellation.

### 5. Resolve names and delivery profiles once

**Choice.** Declare startup parameters under `bag.*`. Use `bag.topics.<entry_id>.name` for resolved input/output names and `bag.topics.<entry_id>.max_hz` for ordinary rate policies. Use one `bag.observations.max_hz` parameter for the pair. Compiled types and the two direct-source entries cannot be changed into arbitrary subscriptions by configuration.

Resolve names using the existing node namespace and remapping interfaces. Freeze the complete result. Reject duplicate resolved names, invalid rates, invalid queue/storage limits, nonzero sampling of protected events, and independent bbox/LOS rate overrides. Report recording-only configuration failures without preventing healthy tracking. The parameter guard rejects runtime `bag.*` updates.

For direct bbox/LOS and trigger entries, use the actual resolved business endpoint names. A configured name must match that endpoint after remapping; it cannot silently relabel a directly supplied message as a different topic. Replace hardcoded pair topic names in writer admission with the checked catalog descriptors.

Share the selected topic-name values with the launch/deployment inputs for other modules. In particular, packaged interceptor YAML uses `/fpv/mission_start`; its bare constructor uses `/fpv/mission_launch`. Bridge launch remaps private control interfaces to `/fpv/*`. The recorder cannot infer another process's private configuration without discovery, so the configured profile and release report are the authority.

QoS means ROS delivery reliability, retained-state behavior, and queue depth. Configure it from the known interface contract:

- Body IMU: best effort, depth 100; gimbal: best effort, depth 10; debug image: best effort, depth 1.
- Odometry, RC, pressure, temperature, altitude, GPS, motor RPM, and control streams: best effort, depth 10, compatible with the known publishers while avoiding reliable recording backpressure on high-rate streams.
- Gimbal mode: reliable, transient-local, depth 1, matching its retained publisher.
- Remaining state/event and optional radar subscriptions: reliable, volatile, depth 10, matching their declared application interfaces.
- Bbox/LOS and trigger use direct forwarding rather than additional subscription delivery profiles. Existing business publishers and subscriptions retain their specified settings.

**Why.** A startup registry satisfies the fixed-interface requirement and can subscribe before a publisher starts.

**Rejected alternative.** A one-time snapshot of currently visible publishers could omit a known but late module. Runtime QoS adaptation would require graph monitoring outside the accepted scope.

**Consequence.** There is no discovery/rescan thread. Incompatible-delivery events and supported middleware loss notifications are recorded as coverage diagnostics, not hidden. Arbitrary new message types require a reviewed catalog update.

### 6. Select timestamps independently of policy timing

**Choice.** Each typed entry supplies the H, S, or R extractor in the new capability's table. Capture ROS system-clock receipt time and monotonic receipt time once at entry. The first is a bag-time fallback; the second governs sampling and elapsed metrics. Do not use writer execution time as message time.

For this production entry point, zero stamps, negative seconds, and nanoseconds outside `[0, 1000000000)` fall back to captured receipt time and increment a reason-specific counter. Valid old, future, repeated, or out-of-order source times remain unchanged; the recorder does not apply the algorithm's freshness filters. Payload headers are never rewritten. `expected_execution_time` remains a command field, not an indexing clock.

**Why.** The source time carries measurement meaning and is available in all stamped catalog structures. A monotonic clock avoids sampler bursts or stalls during system-clock corrections.

**Rejected alternative.** Using receipt time for all newly added topics loses source-time alignment. Parsing headers from assumed byte offsets risks corrupting known messages for no architectural benefit.

**Consequence.** Bag indexing can be out of order across sources. Readback checks original source timestamps rather than forcing a synthetic chronological sequence. Receipt-time fallback counts do not by themselves change a session to incomplete because the original payload remains available.

### 7. Use explicit rate policies, with protected events and image byte limits

**Choice.** The startup catalog defines concrete initial defaults: body IMU/gimbal 100 Hz, odometry 50 Hz, RC/pressure/temperature/altitude/motor RPM 25 Hz, debug images 2 Hz, and all other entries unsampled. Zero Hz means retain every received active-session message, not disable the topic. The pair defaults to unsampled.

For positive f, select the first eligible sample immediately. Thereafter select at monotonic intervals of at least `ceil(1e9 / f)` nanoseconds. Record the selection time when policy selects the sample, even if queue admission subsequently fails; do not retry faster under pressure. Never retain discarded samples for a catch-up burst. Reset policy state at each session.

Use a token bucket for image bytes: a bounded allowance replenished with elapsed time. Default replenishment and maximum allowance are both 16 MiB, meaning 16 MiB/s with at most 16 MiB immediately available. Charge accounted image payload only on successful admission. An image larger than the burst capacity is oversized and an unexpected gap, not an intentional sampled omission. Byte-budget rejections are intentional policy omissions. These outcomes have separate counters.

Protected trigger, arm/offboard, Boolean mission/failsafe, tracker/radar state, gimbal mode, and status-text entries cannot be rate/byte sampled. Control commands default to unsampled but permit an explicit recorded ordinary rate override. A regular observation-pair override applies to both members; the OFF pair always bypasses sampling.

**Why.** Rate caps control message work, while byte caps control large images. Protected events can change briefly and must not disappear simply because they arrive close together.

**Rejected alternative.** Automatically classifying event importance from observed frequency is unreliable. Keeping only one queued latest value for every topic can erase short transitions.

**Consequence.** These defaults are a reproducible starting policy, not production performance evidence. Sparse bursts on a capped topic can be filtered even if their long-term average is low. Sampling does not average signals or guarantee equivalent algorithm replay.

### 8. Keep retained work bounded, including variable payload and active writes

**Choice.** Extend `BagSession` to charge pending plus in-flight work against 256 slots and 64 MiB until buffers are released. Compute conservative retained-payload size from message storage, dynamic string/vector capacities, nested detection results, layouts, and shared-message ownership. Do not treat `sizeof(Message)` or serialized length alone as an exact whole-process memory measure.

Each ordinary entry has default limits of 32 retained messages and 8 MiB; images have four messages and 32 MiB. Reserve 16 slots and 1 MiB inside the global bounds for protected events and the OFF pair. A pair is one admission operation consuming two message slots. Explicit immutable message references keep selected payload alive until writing completes.

Use control and ordinary pending classes, preserving per-entry order. Service at most eight consecutive control messages before an available ordinary item, so neither class can indefinitely starve the other. A pair stays together as a logical work item, although a disk error can still interrupt its two writes. All queue locks protect only bounded memory operations, never file calls.

**Why.** A single image stream must not consume the capacity needed for stop/state evidence. Charging the writer's current item avoids freeing budget while its large payload is still retained.

**Rejected alternative.** An unlimited queue transfers disk stalls into memory growth. A simple fixed object-size estimate undercounts pixel arrays, channels, detections, and strings.

**Consequence.** Reserved capacity improves admission opportunity but does not guarantee lossless events under unlimited load. Any overflow or oversized payload is reported. ROS middleware caches, typed decoding, allocator overhead, and serialization scratch buffers are measured separately from the retained-work budget.

### 9. Cache the real retained mode and report bounded session evidence

**Choice.** Keep one latest gimbal-mode cache while recording is enabled. Bound it to 64 KiB and report an oversized mode value rather than allowing unbounded idle memory. At ON, offer a known snapshot before ordinary callback admission; use session-start indexing time because the String has no source stamp. Record its original receipt time and snapshot origin in the manifest. Live mode messages remain live events.

The idle cache is a separately bounded control-state resource. The 256-slot/64-MiB session-work budget covers admitted pending and in-flight messages; an admitted snapshot is charged there like any other message. Report the cache separately when measuring total recorder memory.

Write a versioned `session.json` summary in the newly owned bag directory. Its fixed catalog bounds the per-entry counters and metadata; do not accumulate every skipped payload or an unbounded list of event details. Record source/run/round identities, software and effective configuration fingerprints, boundaries, selected names/types, policy and QoS, snapshot origin, phase/reason, queue peaks, timestamp-fallback reasons, supported middleware loss, and per-entry counters. Count direct submissions and snapshots explicitly so accepted/written totals remain reconcilable. On writer failure, `accepted_but_unwritten = accepted - written` includes queued and in-flight work.

**Why.** The first mode may precede ON, and later analysis must distinguish deliberate sampling from missing evidence. Bounded counters provide this without creating another high-volume log stream.

**Rejected alternative.** Looking only for bag files or counting accepted writes hides unrecorded topics and failures. A message-by-message diagnostic publication can feed recording back into itself.

**Consequence.** Complete means complete according to frozen admission policy, with no detected unexpected gaps and every accepted message written and closed. Quiet configured inputs are reported as not observed, not proven loss-free. Supported loss events cannot establish unknown publisher-level drop counts. Summary-write failure is a visible recording failure; if storage is unavailable, retain the reason in existing node logs.

### 10. Split bags without recycling evidence and stop on observed storage limits

**Choice.** Use the existing SQLite ROS bag backend without introducing compression or another storage plugin. Set a default file-size splitting target of 268435456 bytes (256 MiB). A split is another file within the same session, not a new ON/OFF round. The threshold is a splitting target, not a promise that every file's final filesystem size is exactly bounded by it; one write and database overhead can cross it.

Use these startup limits:

| Parameter | Default | Meaning |
|---|---:|---|
| `bag.max_file_bytes` | 268435456 | Target for starting the next file |
| `bag.max_total_bytes` | 5368709120 | Total owned bag directories, including previous/incomplete sessions and summaries |
| `bag.min_free_bytes` | 5368709120 | Observed free-space protection threshold on the actual filesystem |

All limits are positive. Existing queue limits remain fixed globally; lower per-entry limits and rates can be configured. Bag ownership is restricted to the recorder's `drone_rosbag_` directories beneath the resolved `save_path`. Never follow symlinks while accounting for old owned evidence. Generate a session name from run identity and round identity as well as the trigger time, and create it exclusively without overwriting a collision.

Inspect storage in the writer, not in ROS callbacks: at open, before a new split, and at least every 100 ms of active writing. Account for actual file growth, including incomplete files and summaries. Storage checks account for the shared filesystem's free space, including camera video and diagnostics. The bag quota is separate from the existing camera-video quota; the release configuration and board report must show both and the disk capacity. There is no atomic free-space reservation across independent writers, so a concurrent writer can cross a threshold between checks.

When a check detects a quota or low-space limit, atomically seal bag admission with reason `storage_limit`. Attempt to drain accepted work only while writes remain safe under the guard. Report any accepted work left unwritten. A subsequent write/close error is a failure with its original reason, not a successful storage-limited close. Keep the same session closed until the next legitimate ON; do not automatically restart recording after space recovers.

**Why.** Splitting keeps individual files manageable; total/free-space limits protect retention capacity. Both can operate together without deleting evidence.

**Rejected alternative.** Treating every split as a stop loses long-session evidence. Circular deletion silently destroys prior sessions.

**Consequence.** Tracking, display, and independent recorders remain healthy when bag recording stops. A partial observation pair can still exist across an interrupted write, and ordinary split boundaries can lie between pair members; readers evaluate the complete multi-file bag. Power loss can lose queued and unflushed data; no durability interval is promised.

### 11. Package typed dependencies and keep rollback configuration matched

**Choice.** Require the real target-architecture `quadrotor_msgs` package for RK3576 recording independently of the optional OSD feedback adapter. Add the dependency through RK3576 build configuration and package metadata without making Jetson code link the new manager. Extend release dependency closure to required non-Python generated message libraries, ROS package discovery resources, and the selected startup recording settings. Continue using the system RKNN Runtime.

Put startup policy in the existing ROS parameter configuration, including compiled defaults and explicit profile/rate/storage values in the RK3576 templates. A session records the effective expanded configuration fingerprint. Release packaging retains the exact selected configuration and its fingerprint rather than synthesizing different defaults. Enabling the bench profile adds only its known status topic; it does not start the bench tool. Framework topics and test publishers remain outside the application profile.

The standard `deployment/cpu-affinity.yaml` adds `topic_recording: 2` and changes `allowed_cpus` to `[2, 4, 5, 6, 7]`. The other role assignments stay unchanged. Packaging records the selected policy's source and fingerprint. Fresh installation and upgrade both install that exact policy; any independent camera-configuration retention rule must not leave the previous CPU-policy file in use. The service and direct launcher continue to obtain the complete allowed set from this one installed policy, without another hardcoded mask.

Keep explicit `--cpu-policy` selection as an operator override. A new recorder-enabled, role-pinned package requires an explicit `topic_recording` role, including for a custom policy; omission fails with instructions to add the field and its core to `allowed_cpus`. An explicit alternative valid core is honored byte for byte. Explicit custom `mode: inherit` remains an intentional unpinned override and does not claim CPU 2. With `need_save=false`, a legacy policy does not need an unused topic-recording role. Do not silently patch selected input files. A later operator-edited legacy runtime policy keeps the compatibility behavior in decision 1.

Separate policy structure from target availability. Offline packaging checks types, required fields, mode, and allowed-set membership without requiring the build host to expose the target's CPU numbers. Installation preflight and startup check actual target cores. `deploy.sh --check` or installation must reject an unavailable selected core before changing installed files or services. Runtime and Python policy validation agree on explicit-field rules. Backup and rollback restore the matching prior binary, policy, configuration, and launcher.

The normal installation path is:

1. Select the repository default policy, or an explicit custom policy, and validate its structure against the selected recorder setting.
2. Package the unchanged selected policy with its content fingerprint; preflight the package against the actual board before installation changes anything.
3. Install the policy on fresh installation or upgrade. The launcher derives its allowed set from that file, and each newly created topic thread confirms its configured role before work.

**Why.** The catalog includes ControlCommand and LowLevelFeedback even when display feedback is not built. The current packager's message-library seeding must be checked for this expanded dependency set.

**Rejected alternative.** Assuming the sibling source exists at runtime or copying generated message headers does not deliver the needed libraries. Keeping a hidden legacy writer switch creates competing implementations.

**Consequence.** Implementation edits remain in drone_tracker. Sibling source revisions are validation inputs, not files to modify. Rollback restores the previous binary and matching configuration, leaving all evidence intact; `need_save=false` is the immediate recording-disable option, not a second old-recorder path. Standard installation gains CPU 2 automatically, while custom inherited policies and legacy direct-runtime policies retain their documented behavior.

### 12. Validate behavior before making a performance claim

**Choice.** Add deterministic policy/accounting tests and real ROS transport/bag-readback tests, then compare disabled recording, the preserved previous recorder binary, and the expanded recorder on one identified production HZ-RK3576_WuKong Board. Preserve baseline binaries and identities before implementation replaces them; do not retain old code as a production fallback mode.

Tests cover the full typed catalog, source-time readers, invalid-stamp fallback, payload round trips, late publishers, static configuration, protected transitions, per-pair sampling, in-flight accounting, reserved capacity, image metadata, retained mode, deferred ON, OFF/failure boundaries, stale handles, and writer/storage failures. Use controlled storage results rather than filling the production disk. Native ROS transport results are distinct from ARM64 cross-compilation or emulation.

Use separate, matched performance runs, each no longer than 180 seconds. Preserve camera, model files, scene/input, HDMI, diagnostic mode, and independent video settings. Record input rates and sizes, actual consumed/displayed frame rates, processing/control latency distributions, CPU and memory, disk throughput, retained queue peaks, all omission/loss counters, shutdown behavior, and complete multi-file bag readback. Cover at least 20 ON/OFF rounds in a bounded lifecycle run and at least 60 seconds of combined active workload. Include an image-enabled run as a separate workload; keep normal production `visualized_result=false` comparisons separate.

**Why.** The additional typed subscriptions and shared disk still cost resources. Unit tests cannot establish production overhead or end-to-end message delivery.

**Rejected alternative.** Comparing configured camera FPS or nominal HDMI refresh rates hides dropped work. Combining unmatched old reports cannot validate the new recorder.

**Consequence.** Normal declared-load runs must have no unexpected recorder loss or unexplained resource growth; deliberate sampling is visible. Report absolute and relative performance differences without inventing a zero-overhead guarantee. Unavailable hardware or missing baseline evidence leaves the related acceptance tasks open. Actual implementation checks and their revision boundaries are recorded in [validation.md](validation.md).

For the CPU 2 refinement, add focused policy/parser, thread-readiness/failure, package, installation, and rollback checks. Preserve the historical performance measurements. Fresh production-board evidence must identify the revised binary and packaged policy, exercise camera and target-video ON/OFF rounds, and show both topic threads on CPU 2 without runtime thread scanning or manual rebinding. Read back the resulting bags and confirm camera-video recording still uses CPU 7. Recreated session writers must each reapply the role. Historical CPU 2 masks from temporary benchmark binding do not complete these new tasks.

## Risks / Trade-offs

- [Typed reception still consumes CPU and memory bandwidth] -> Isolate callback scheduling, sample before enqueue/serialization, and measure the real combined workload. Do not reduce source publication rates as a shortcut.
- [Two best-effort subscribers observe different input samples] -> Record the subscription delivery profile and recorder counters; retain diagnostics for actual algorithm consumption rather than claiming replay equivalence.
- [Filesystem calls can block the one writer indefinitely] -> Keep them out of admission/control callbacks and preserve the existing shutdown limitation. A hard termination guarantee would require a separate process design.
- [Control arrives after another module's response boundary] -> Use authoritative local OFF plus the final pair, explicitly exclude post-seal responses, and do not silently extend the session.
- [Publisher configuration drifts from the frozen catalog] -> Ship explicit resolved names and type identities, audit sibling interfaces during implementation, and test profile/name overrides and delivery incompatibilities.
- [A writer or summary fails after only half a pair reaches disk] -> Preserve atomic pair admission but report failed/incomplete file results; never describe two separate ROS messages as a disk transaction.
- [Shared storage crosses a threshold between observations] -> Check in the writer at bounded intervals and split points, account for actual usage, and report the guard as an observed protection rather than an atomic filesystem reservation.
- [A legacy or custom policy is mistaken for the CPU 2 default] -> Require an explicit topic role in newly packaged role-pinned recorder profiles, preserve custom inheritance and direct-runtime legacy behavior explicitly, and inspect per-thread readbacks rather than the policy-loaded message.
- [Offline packaging mistakes build-host CPUs for board CPUs] -> Check policy structure during packaging and defer actual online/accessible-core checks to board preflight and startup.

## Migration Plan

1. Implement against these deltas without changing durable main specifications until the normal synchronization/archive step. Preserve the old recorder binary and its source/configuration identity for comparison.
2. Build the RK3576 target with the real shared-message package, verify all recorder libraries in the release, and run policy, transport, lifecycle, and readback tests. Leave Jetson behavior unchanged.
3. Run the identified production-board comparisons and retain evidence outside disposable build caches. Tune only explicitly configurable recording policies if needed; preserve protected events and paired observation semantics.
4. Package the selected configuration, including names consistent with the bridge and interceptor launch profiles. Include the standard CPU 2 topic-recording policy, or validate an explicitly selected custom policy under decision 11. Keep debug-image publication off in the ordinary production profile. Review the separate video/bag quotas and actual filesystem capacity.
5. Deploy only through the normal separately authorized deployment workflow. Verify one ON/OFF bag, its catalog/summary, and its split-file readback before operational use.
6. If rollback is required, stop the service safely and restore the prior binary and matching configuration. Do not delete or overwrite recordings from either version. Disabling `need_save` after restart can isolate recording while other investigation continues.

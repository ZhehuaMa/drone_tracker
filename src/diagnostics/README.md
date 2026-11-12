# RK3576 tracking diagnostics

[Project overview](../../README.md) · [Documentation index](../../docs/README.md)

Business threads submit fixed 256-byte numeric events that connect capture, detection,
history lookup, recovery and ROS publication calls. The independent
`rk3576_diagnostic_worker` collects them. `rk3576_diagnostic_inspect` reads stored
records without loading models or modifying the originals.

The installed service and `/opt/drone_tracker/bin/run-diagnostics` bind only the
collector to CPU 6. The shared CPU policy must allow CPU 6. The launcher requires an
exact `{6}` readback and ordinary `SCHED_OTHER` scheduling before executing the worker.
Failure prevents diagnostic startup; it does not choose another core or alter other
processes. CPU 6 is also used by display work. Direct experimental execution of the
bare worker retains the caller's affinity.

## Modes and identities

`diagnostics.mode` is fixed at startup. `off` creates no client connection or structured
log. `basic` records the causal event chain; `detail` adds model-stage timing. The
production template selects `basic`; other ROS templates select `off`. Jetson supports
only `off`. Changing diagnostic parameters requires a restart.

An event's exact identity is `(run_id, producer_id, producer_seq)`. `run_id` identifies
one business process, while `round_id` identifies an ON/OFF tracking round. Attempted
sequence numbers include submissions rejected by a full buffer. Ordinary and critical
records can interleave, so check actual event sets and gaps rather than only maxima.

`capture_id` numbers actual dequeued camera samples within one camera stream. Continuous
preview keeps that stream across tracking rounds; ON/OFF does not reset this counter.
`driver_sequence` separately preserves driver gaps and wraparound. Video and reference
sources leave camera identities unknown. `update_id` increments on every tracker call,
including early returns. `tracker_frame_id` increments only after input checks pass.
Requests use `request_id`, `source_update_id` and `source_tracker_frame_id`.
For example, request 17 can originate at tracker frame 100/update 105 and be read during
update 139. Its source remains frame 100; timestamp proximity never substitutes for identity.

## Ownership and submission

The client creates a fixed-size, shrink-protected memory file with `memfd_create`,
maps it, initializes its atomic objects and touches the allocated pages. This is shared
memory, not persistent storage. The receiver validates peer user identity, protocol
version, length, descriptors and region size before mapping. It never reinitializes it.

Current format 3 has five producers: Node, Camera, Yolo, Control and Dispatcher.
Each producer has one writer and separate ordinary/critical SPSC rings, meaning one
writer and one reader per ring. Critical slots occupy one third of slot capacity.
The persistent preview dispatcher owns Dispatcher; tracking owns Node; control/OFF
notifications own Control. Camera and Yolo retain their separate writers. Tracking
rounds emit RoundStart/RoundEnd without reopening capture. FrameSelect records source
selection; pending-task replacement records FrameSkip/LatestSkip.

The business allocation remains below 8 MiB. Events contain no strings, pointers,
images or extra frame references. Take numeric snapshots inside existing business locks
and submit after releasing them. The hot path performs no file I/O, dynamic allocation
or text formatting. Full rings reject new events without overwriting unread entries.
Shared gap summaries retain totals, endpoint sequences and up to 32 contiguous ranges;
a full range table explicitly marks incomplete detail. Critical records can also be lost.
An abnormal exit without an end record leaves an unknown tail.

`eventfd` supplies nonblocking, coalesced wakeups; ordinary notifications are throttled.
Notification failure does not revoke an accepted event. The collector targets 10 ms
polling, subject to scheduling and storage availability. Business code adds no dedicated
logging thread. Connection preparation has a 500 ms budget and normal client exit a
2-second budget. The existing control timer attempts reconnection at most every five
seconds, after `pidfd` confirms the previous process instance has exited. The collector
accepts at most two shared regions and batches at most 512 KiB of output.

## Frame, observation and time fields

`frame_time_us` is source time in microseconds. `prediction_us` records the supplied
`ctx.now_us` reference. `mono_ns` is monotonic nanoseconds for local order and elapsed
time; `duration_ns` records execution cost. A normal PublishEnd proves only return from
a ROS publish call, not subscriber receipt or bag persistence.

Generic `box` fields use top-left `x,y,width,height`, with explicit validity flags.
Format-2 ObservationBbox/ObservationLos payloads instead preserve exact double values
in `values[2..5]`. They use observation sequence `values[0]` and nanosecond header time
`values[1]`. Bbox values are center-x, center-y, width, height; LOS values are pitch,
yaw, pitch rate, yaw rate. `counts[0]` selects capture=0, qualified-pose=1 or prediction
fallback=2. Integer Output boxes cannot substitute for these publication payloads.
See the [regression diagnostic contract](../../docs/testing/tracking-regression/diagnostic_contract.md)
for format-1 compatibility and independent bbox/LOS call pairing.

For example, capture can advance from 200 to 224 while a tracker call takes 388 ms.
The next selection records skips for queued old frames and FrameSelect for 224.
Only checked frames actually submitted to the algorithm enter its history. Continuous
preview can additionally replace a pending tracking task; that replacement is a separate
skip. Warmup, termination, driver gaps and queue-full rejection have distinct reasons.
Repeated/backward frame time records RepeatedTime and PublishSkipped when not consumed.

## Decision fields

Keep NoTarget, LowIou, NanoUpdateFailed, YoloJump, NanoJump, NoResultTimeout,
HistoryMiss and InitFailed separate. IoU is intersection area divided by union area;
it measures box overlap. These causes are not interchangeable detector misses.

| Event | Field interpretation |
|---|---|
| RequestDecision | `values[0..5]`: inference active, pending slot, unread result, executing ID, pending ID, result ID. Complete means accepted. Active inference can coexist with an empty pending slot. |
| DetectStage | `values[0]`: region=0, full=1, tile=2; `values[1]`: stage index. `counts[0..3]`: candidates before threshold, after score, after size filter, after NMS. NMS suppresses overlapping duplicates. Highest score is collected during the original candidate loop; ScoreKnown distinguishes absent and unknown values. |
| HistoryLookup | `values[0..1]`: oldest/newest cached tracker frame. Source eviction is a HistoryMiss, not NanoTrack initialization failure. Missing detection completion leaves its outcome unknown. |
| Decision, YOLO | IoU operands and thresholds; `counts[0..1]`: unconfirmed before/after; `counts[2..3]`: jump before/after; `values[0..1]`: state before/after. DecisionDetails adds NanoTrack result in `values[2]`, loss limit in `[3]`, jump tolerance in `[4]`. LowIou takes precedence when both low overlap and an update failure occur. |
| Decision, NanoCounter | `counts[0..3]`: counter before, after increment, final after all branches, tolerance. With tolerance 3, two jumps produce 0/1/1/3 and 1/2/2/3; a normal frame then produces 2/2/0/3. Actual counters are retained even if already above the limit. `values[1..2]` preserve KF/prior-box jump checks. |
| JumpComparison | `values[0]`: NanoTrack/current KF=1, YOLO/source KF=2, YOLO/source tracker=3. KF means the motion-prediction Kalman filter. `box` is the real reference and `other_box` the tested box; BoxValid distinguishes missing references. `counts[0]` is the original comparison result. Score/threshold contain position and size-ratio limits. |
| Initialization failure | ErrorCodeKnown authorizes interpretation of `values[1]` domain and `[2]` numeric code. Domain 1 is PreprocessError; domain 2 is ModelError. Missing codes remain unknown, never successful zero. JSONL uses null and CSV blank cells for unknown codes. |
| CatchupStep / CatchupEnd | Each replay step retains its source, outcome, box, score and duration. End `counts[0..2]` records attempted/succeeded/failed steps. A successful 24-step recovery with two failed steps is 24/22/2, not 24 successes. |
| Timing | `values[0]` selects `timing::Stage`; Observed marks actual execution; Complete means all observed executions succeeded; `counts[0..1]` records execution/failure counts. Unexecuted stages have no duration sample. |

Preprocess error 6 means input storage busy and 7 means platform preparation failure.
Model error 2 means contract mismatch and 5 means execution failure. NanoTrack failure
stages are input=1, template preparation=2, template inference failure=3 and inference
exception=4. All records retain the original numeric outcomes rather than recomputing them.

## Storage and recovery

The default directory is `/var/lib/drone_tracker/diagnostics/<run_id>`. Limits are
64 MiB per segment, 1 GiB total and at least 5 GiB free space. These units use powers
of 1024. Reaching a limit stops writing; it never deletes or overwrites older logs.
Reconnection creates a new segment instead of appending to an abnormal tail.

Disk records carry version, length and CRC content checks. Slots are released only
after the complete batch is written. Generated, written and synced positions describe
attempts, complete writes and confirmed synchronization respectively; reconcile all
three with actual events and gaps. A write is not a persistence guarantee.

Synchronization defaults to 100 ms and optionally 200 ms. Critical events request
prompt synchronization when scheduling and storage permit. The worker synchronizes
data, writes a commit referencing that boundary, and synchronizes the commit. Both must
succeed before it advertises the boundary. Relevant parent directories are synchronized
too. Errors never advance success positions. An oldest unsynchronized age above 500 ms
marks degradation. Blocking storage calls can exceed polling and synchronization targets.

Recovery accepts a complete verifiable prefix. It cannot guarantee the final event
survives power loss. Recovered commit records supply the offline synchronized position;
file length cannot invent one. Actual power-loss validation has not been established.
Health `values[0..6]` records generated, written, synced, dropped, oldest-unsynced age,
last sync duration and error. Observed marks known synchronization. Routine business
and collector health summaries occur at most once per second, with a final snapshot.

## Read-only inspection and evidence retention

Back up required originals outside disposable work areas. Export into a new directory
outside the log directory:

```bash
/opt/drone_tracker/current/lib/drone_tracker/rk3576_diagnostic_inspect \
  /var/lib/drone_tracker/diagnostics/123456 --output /tmp/diagnostic-export-123456
```

Outputs are `timeline.txt` (currently Chinese), `events.jsonl`, `events.csv` and
`identities.txt`. Unknown identities, duplicates, known gaps, missing ends and corrupt
tails are explicit. A visible start without an end means unknown outcome.
`.docker-artifacts` and `/tmp` can be cleared routinely. Preserve required raw logs,
exports and manifests in an explicitly retained external location. Historical paths
are provenance, not a promise of present availability.

## Bounded verification and acceptance sampling

Use `./build.sh --test` for ARM64 programs through the default x86_64 compiler image;
`--backend arm64` selects the explicit fallback. See the [image guide](../../dockerfiles/README.amd64.md).
Container checks do not establish board model, camera or performance acceptance.
`diagnostic_tests` is the controlled component entry. `tests/bounded_run.py` includes
preparation, warmup, measurement and cleanup in one external deadline: at most 600 seconds
for general performance/stability, 180 for camera work and 120 for fault instances.
The production paired plan uses 150-second windows.

`DRONE_TRACKER_ACCEPTANCE_DIRECTORY` enables fixed sampling arrays only when explicitly
set to a new external-controller directory. Arrays are allocated and touched before
models run; CSV export occurs after business stop and node destruction. `measurement.h`
measures Recorder::submit including notification calls, using independent per-producer
arrays. Node call sampling surrounds tracker update in off/basic/detail alike. Clock
reads and array writes have real cost and remain included in the comparison.

Each producer holds at most 65536 submission samples; the call array holds 8192.
Arrays, shared regions and temporary events remain statically bounded below 8 MiB.
`sampling-status.txt` records overflow; overflowed arrays cannot establish full coverage.
The measurement copy uses 32-bit sequence/round values only for bounded short runs;
production event identities remain 64-bit.

Calls/submissions are selected by start time in their complete half-open measurement
windows. P99 is sorted sample `ceil(0.99*N)`, and every sample at or above it is kept.
Off-mode acceptance CSV is an explicit measurement output, not a structured event log.
The ten-pair task requires at least eight complete groups to meet submission P99 <=20 us,
call P99 increase <=0.2 ms, consumed-frame-rate decrease <=1% and no health gaps.
See the [ten-pair procedure](../../docs/testing/diagnostics/ten_round_acceptance.md). Changed business binaries
require a fresh complete batch.

The Linux deadline controller uses `PR_SET_CHILD_SUBREAPER` to adopt surviving descendants,
including new sessions. It tracks parentage and process start time, sends TERM then KILL,
and reaps within the original deadline. Failure to confirm recovery reports
`resources_recovered=false`. Platforms without that facility reject the test.
Systemd instances must register their unique units in `DRONE_DIAGNOSTIC_BOUNDED_UNITS`
before startup; the controller stops restart behavior and verifies remaining cgroup members.
Unknown registrations fail verification.

`board_acceptance.py` creates isolated board instances. `maintenance_window.py` requires
explicit authorization before stopping formal services. The independent restore guard
uses the first armed monotonic time, begins cleanup at 1720 seconds and attempts formal
service restoration by 1800 seconds. Retries cannot reset that origin. This maintenance
guard does not replace each group's deadline. Entry fixtures copy installed scripts,
record hashes/path remapping and execute the actual run-diagnostics -> cpu-policy.py ->
diagnostic-launch.py chain. A replacement worker records arguments, CPU mask and scheduling;
affinity failure must return nonzero without executing it. Isolated checks do not modify
formal launch entries.

## Format 3: verification evidence and ordered loss

Format 3 preserves the 256-byte event layout and five serialized producers. Readers accept formats 1 and 2 with their original meanings; unknown formats fail parsing. Historical age-only events do not acquire a rejection policy. Missing verification fields remain unavailable. No display-text parsing or integer display box can supply missing observation evidence.

New value records use exact integers. `ValidityState` stores attempt, permission revision, seed frame/request, attempt deadline, verification time, and confirming frame. Counts store phase, permission, and the two `not_evaluated` validator statuses. `EvidenceClock` stores mapping identifier (1=`capture-monotonic-v1`, 2=`standalone-admission-v1`), original anchor, decision time, deadline, confirmation source time, attempt, and revision. Execution times are nanoseconds; source time is microseconds. `CandidateMatch` retains submitted/replay comparison origin, candidate and matching counts, original anchor, decision time, freshness/identity eligibility, counter before/after, and successful stage identity.

`TrustLoss` and `TrustLossDisposed` identify revoked attempt/revision, revocation execution time, source-clock time sampled at revocation, construction/disposition execution time, and reason. All loss/OFF pairs carry `ControlObservation` (32768). Loss pairs additionally carry `TrustLossObservation` (262144); OFF pairs do not. `ValidAdmission` records attempt/revision, actual admission time, deadline, expected observation sequence, and eligibility. A timestamp-suppressed image is not a valid-pair admission. Existing per-topic start/end outcomes remain independent. These records establish attempted ordering and local timing, not subscriber receipt.

The older decision table above describes formats 1 and 2. In format 3, YOLO `Decision` retains overlap operands, maximum IoU, reset/calibration thresholds, and counter before/after values. Its `values[0..2]` identify comparison origin, candidate count, and overlap-match count. Jump counters stay zero. `SourceContext` preserves the original submitted tracker and prediction rectangles with their validity flags; `values[0..2]` retain source-pixel availability, source generation, and the no-result count. Replay measurements never rewrite that original provenance.

Synthetic standalone anchors start after decoding, copying, and scheduled waiting. They survive result arrival and catch-up unchanged. Production/regression anchors include pre-admission capture age. Mapping differences prevent affected same-condition timing comparisons. The [metrics contract](../../docs/testing/tracking-regression/metrics.md#freshness-policy-evidence) defines denominator/window handling, incomplete recoveries, and unset performance criteria. Score/motion calibration and manually labeled accuracy evaluation remain deferred.

## Format 4 scheduler and seed evidence

Format 4 retains the fixed 256-byte layout and reads historical formats 1–3 without changing their meanings. A submitted request identifies one admitted image. A job identifies one model call, and a plan identifies fixed-image discovery coverage. These are distinct denominators. `DetectStart` and `DetectEnd` values now identify job, plan, fallback plan, dispatch attempt/revision, and targeted purpose. `DetectStage` retains region and stage, with job/plan identities in values 2–4. Historic whole-cascade completion counts remain historic; a missing job field is unavailable.

`StageDisposition` values are job, parent plan, fallback plan, original request attempt, newly seeded attempt, applicable attempt, and original revision. Counts separately identify seed eligibility, confirmation eligibility, actual seed use, and complete coverage. Reasons distinguish disposed requests, wrong round, invalid source/clock, unavailable pixels, active replacement protection, partial coverage, and supersession. An expired-A result used as B's seed retains A's provenance and cannot count as verification. Parent verdicts use separate `PlanOutcome` records and cannot spend the adverse budget twice.

`DetectorControl` records short phase/seed/agreement feedback. Acceptance precedes expensive preparation. `SchedulerTiming` count 0 stores preprocessing, inference and postprocessing nanoseconds in values 2–4. Count 1 records result take and capacity release in value 2. Unmeasured waiting components remain unavailable; zero placeholders do not establish zero cost. Mailbox capacity and semantic seed disposition are different waits. `CleanupPolicy` version 1 identifies inactive frame-count cleanup, effective elapsed limits/cadence, and the experimental 2:1 targeted/discovery share. Old `NoResultTimeout` events are never renamed candidate expiry.

The controlled scheduler tests cover latest-image replacement, fixed-image continuation, T/T/D service, protected unread results, overlap with a locally owned result, and OFF device ownership. Production measurements must additionally identify model/input/profile hashes, source windows, cancellation/coverage and usable-confirmation outcomes. Model-call rate alone does not establish confirmation throughput or tracking accuracy.

`RegionHint` retains producing job/source, hint attempt, original request attempt, stage kind/index, and policy version; its box stores the region. It grants no permission. `PlanCancelled` identifies the plan/request/source and next region with its cancellation reason. Shutdown also gives any unread completed job one terminal `StageDisposition` with Cleaning or Stopped, after active device use finishes.

Plan cancellation remains applicable if its call finishes afterward or its result was already unread. Publication/take samples a bounded cancellation record without changing original source or candidate provenance. Cancelled coverage has no completed adverse authority; a neutral consumed disposition retains the cancellation reason. Positive evidence still undergoes the separate seed and unique-match checks. The controlled tests cover last-call cancellation, unread cancellation, first-miss fallback linkage, nonmatching feedback and rotated restarts.

`SchedulerTiming` counts 2/3/4 identify capacity, seed-disposition, and idle wait slices. Values 2/3 are monotonic start/end nanoseconds for these slices. Condition-variable wakes split changing wait causes rather than labeling an entire mixed wait as inference. Missing waiting records remain unavailable. Format-4 decoded video `capture_id` is a one-based source image index, allowing exact first-240-image (four-second) and first-480-image (eight-second) windows at 60 FPS without reconstructing skipped images from publication times.

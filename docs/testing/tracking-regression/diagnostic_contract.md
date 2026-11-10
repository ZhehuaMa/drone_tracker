# Diagnostic contract for configurable regression, formats 1 and 2

[Documentation index](../../README.md) · [Regression tool guide](../../../tests/tracking_regression/README.md)

The current dependency is `src/diagnostics/event.h` format 2. The collector also supports historical format-1 records. An event is a fixed 256-byte record. Its exact identity is `(run_id, producer_id, producer_seq)`. `round_id` identifies the ON/OFF round; `update_id` identifies the tracker call. Requests and results use `request_id`, `source_update_id`, and `source_tracker_frame_id`. Times are integer `mono_ns` (monotonic nanoseconds), `frame_time_us` and `prediction_us` (system microseconds). The fixture supplies an exact `E0 + relative_us` mapping to `fixture_frame_id`. Camera driver identities remain unknown for the synthetic source; original driver sequences are provenance only.

The collector reads binary storage directly and preserves every Event field, including frame round identity, all arrays and enum numbers. The existing inspector's presentation export is insufficient as an interchange contract: it lacks `frame.round_id`, the raw type/reason numbers and byte length. The regression collector preserves those fields from the raw binary instead of guessing or changing the production protocol.

| Metric or evidence | Source and association | Health or rejection check |
|---|---|---|
| Update duration/throughput | `UpdateStart` / `UpdateEnd`, same run, round and update | Exactly one pair; missing end is incomplete, not zero |
| Requests accepted/rejected | `RequestDecision`, `Complete`, `reason`, `values[0..5]` slot snapshot | Attempt request identity required; retain reason, distinguish pending from unread |
| Detection execution/results | `DetectStart` / `DetectEnd`, request; `HasTarget`; source IDs | Accepted request must execute/finish or cancel; match exact request |
| Actual detector inference count | `DetectStage`, stage `values[0..1]`, explicit observed inference flag | A stage preparation failure is not automatically an inference; absent execution evidence rejects formal count |
| Result read/overwritten/cancelled | `ResultRead`, `ResultOverwrite`, `RequestCancel`, request | Do not count unread overwritten success as initialization failure |
| History range and misses | `HistoryAdd`, `HistoryEvict`, `HistoryLookup`, source tracker ID, `values[0..1]` | Preserve `HistoryMiss`; do not infer absence of target |
| Clearing/recovery | `Decision`, before/after states `values[0..1]`, actual reason, `counts` | Count actual clear transition, not repeated unconfirmed detections; next valid publication ends recovery |
| Initialization | `InitializeStart` / outer `InitializeEnd`, run/round/update/request; detail failure stage/error flags | Detail `InitializeEnd` does not consume the outer pair; unknown underlying code stays unknown |
| Catch-up tracking | `CatchupStep`, `CatchupEnd`, source IDs, duration, summary `counts[0..2]` | Attempts/success/failure remain distinct; nested durations are not added to update duration |
| Integer display boxes | `Output.box`, `other_box`, `BoxValid`, `OtherBoxValid`, `values[1]` tracking | Format 1 uses prediction `other_box` for publication evidence. Format 2 records exact publication values separately below |
| Actual publication | `PublishStart` / `PublishEnd`, run/round/update, `Complete`, reason | Format 1 joins the corresponding Output. Format 2 also joins the observation sequence and independent LOS call below. Normal return proves call completion, not subscriber receipt |
| Frame age/prediction lag | Publication boundary `mono_ns`, exact source frame and reference timestamp; run M0/E0 | Format 1 uses prediction time; format 2 preserves the selected observation reference. Reject ambiguous identity or clock jumps; do not match closest timestamps |
| Frame destinations | input log plus `FrameSelect`, `FrameSkip`, source EOF residuals | Every fixture frame has exactly one terminal destination; separate rejected/skipped/residual |
| Stage timing | `Timing.values[0]`, `Observed`, `Complete`, counts | Optional detail metrics are not applicable in fixed basic mode |

All event numeric fields (`flags`, producer/run/round/frame/request/update identities, source IDs, times, duration, both boxes, score, IoU, threshold, highest score, `values[7]`, `counts[4]`) are retained. `CaptureKnown`, `DriverKnown`, `FrameTimeKnown`, `PredictionKnown`, `BoxValid`, `OtherBoxValid`, `ScoreKnown`, `Complete`, `HasTarget`, `Observed` and `ErrorCodeKnown` are interpreted only where their event defines them. No unknown scalar is converted into a successful zero.

Each binary storage segment must have valid headers, versions, endian marker and CRC, an actual synchronized commit and a normal end record. No partial tail, duplicate identity, producer sequence gap, `Gap`, `AbnormalEnd`, health error/drop count or unknown tail is qualified. The run and round lifecycle must end normally; required call pairs and request terminal paths must reconcile. `Health.values[0..6]` contains generated/written/synchronized positions, dropped count, oldest unsynchronized age, sync duration and error. An unknown final synchronization state rejects a formal comparison. Raw storage is retained as `events.bin` and its original segments; completion records hash all evidence files.

A missing dependency, unsupported inference-observation field, missing actual publication boundary or unavailable complete raw event export prevents formal measurement. A healthy run with no catch-up events has count zero and duration `null` with reason `no_event`; that is not a diagnostic gap.

## Source-context interpretation 2

Source-context interpretation 2 was introduced with binary format 1 and remains valid
in format 2. Both formats use 256-byte events and preserve existing event and reason numbers. `RunStart.values[6] = 2` advertises the append-only interpretation.
An absent marker means new availability/disposition counts are unavailable, even when
an old `HistoryLookup/HistoryMiss` remains a valid image-history miss.

`RequestCancel/Superseded` terminates an accepted, unstarted request.
`request_id` is the cancelled request and `values[0]` is the accepted successor.
The source frame/update and capture identities still describe the cancelled request.
It adds no execution, inference, or normal no-target result. Cleanup cancellations
retain `Cleaning`. Missing terminal evidence still rejects qualification. A superseded
identity must never execute and must have exactly one supersession record.

`SourceContext` uses the result's request, source frame and source update identities.
`frame_time_us` is original source capture time; `FrameTimeKnown` marks availability.
`box` is the clipped source tracker observation (`BoxValid`), and `other_box` is its
clipped prediction (`OtherBoxValid`). `values[0]` reports source-image availability;
`values[1]` is the business-owned round generation; `values[2]` is the watchdog after
its unchanged result-read reset. `iou` is measurable only with `BoxValid`.
`None` means a usable overlap basis, `ContextUnavailable` means no source tracker
observation, and `RecoveryUnavailable` records a required recovery with missing pixels.
The latter is a separate disposition, never an initialization attempt or success.
The ordinary `Decision` retains detection/source operands, overlap thresholds, and
before/after unconfirmed and jump counters. `JumpComparison` kinds 2/3 use only source
prediction/tracker operands; no current or previous display fallback remains.

Accepted `RequestDecision` includes the original capture time with `FrameTimeKnown`.
`ResultRead` preserves it for capture-to-consumption measurements. Queue wait uses
accepted-request and `DetectStart` monotonic timestamps. Detection execution uses
`DetectStart` and `DetectEnd`. Exact request identities join these stages. Only a known
fixture epoch mapping permits conversion between capture time and monotonic time.
No age threshold or guarantee is introduced.

## Observation protocol version 2

Version 2 retains 256-byte events, old event numbers, and the meaning of
`Output.box/other_box` as integer display/prediction evidence. New appended
`ObservationBbox` and `ObservationLos` events preserve exact double values.
`values[0]` is the round-local observation sequence, `values[1]` is the original
nanosecond header time, and `values[2..5]` contain four double bit patterns copied
with `memcpy`. BBox values are center-x, center-y, width and height; LOS values are
pitch, yaw, pitch-rate and yaw-rate. `BoxValid` applies to the individual payload.
`counts[0]` selects capture=0, qualified-pose=1, prediction-fallback=2.

Frame bbox calls retain `PublishStart/End`; LOS calls use `LosPublishStart/End`.
Their round/update and observation sequence identify the independent calls.
Each call keeps its own success/failure outcome; normal return proves only call
completion. `values[6]` on call records is the actual node-clock send boundary.
`ObservationSuppressed/RepeatedTime` identifies duplicate/backward reference
suppression and is not an unconsumed frame or a diagnostic gap.

OFF payloads carry `ControlObservation` and have no input frame or update.
`ControlBboxStart/End` and `ControlLosStart/End` identify their independent calls.
The Control producer owns these notifications; the preview dispatcher now owns
an appended Dispatcher producer. Node, Camera and Yolo producer IDs are unchanged.
Version 2 commit records therefore contain five written and five dropped counters
(96 bytes). Version 1 commits have four of each (80 bytes). Readers support both
versions explicitly and reject unknown versions. Live worker handshakes require
matching version and shared-region size; mismatch degrades diagnostics while
healthy tracking continues.

For each format-2 observation identity (run, round, observation sequence), the
collector and metric calculator require exactly one bbox payload, one LOS
payload, and one start/end pair for each topic call. All six records must share
the message timestamp and frame update identity. Control observations instead
have zero update/frame identities and carry the control flag on all six records.
Missing a whole topic call or payload makes the evidence incomplete; it never
becomes a zero-valued invalid LOS sample. Partial publication failures retain
their separate outcomes and also prevent a complete-run verdict.

## Separate CPU evidence (schema 1)

`cpu-observation.json` uses schema 1 for both diagnostic formats. It describes one
preallocated, single-writer array per diagnostic producer: four for format 1 and
five for format 2, which appends Dispatcher as slot 4. The event format determines
the required complete slot set; duplicate, missing and unknown slots are rejected. `cpu-points.csv` stores the
producer/writer, PID, Linux TID, monotonic nanoseconds, round, update, request, tracker frame,
source frame time, event/point ID, detail, duration, sampled CPU and measured write cost.
The node owns these arrays; only the regression entry installs the optional receiver.
The arrays are allocated and touched before preparation; work-point writes allocate no storage, write no files,
scan no threads and read no masks. Each writer reads its mask once at entry. Managed tracking, YOLO and control roles
(slots 0, 2 and 3) must register before `m0`, the playback clock origin. Camera and
Dispatcher slots may be unused; populated slots must retain complete point counts
and valid thread identities. Dispatcher remains outside the managed affinity roles.
A full array increments overflow and never overwrites earlier evidence. Any overflow disqualifies placement.

Existing diagnostic event IDs are also point IDs and retain their existing 256-byte binary layout.
Additional YOLO points are 100/101 (preparation begin/end), 102/103 (model call begin/end),
and 104/105 (postprocessing begin/end); `detail` identifies the stage index. Point 200 registers a role.
All diagnostic submission, CRC, sequence, pairing and normal-end checks remain enabled.
The CPU receiver does not change detector-result snapshot, request/skip or arbitration ordering.

For example, round 1/update 42 can emit twenty CatchupStep (point 23) records. Each has its own
source frame timestamp and duration. Join these with the same round/update/request and exact frame time
in `events.jsonl`, then check their TID against the Node writer registration and sampled CPU against 5.
The count must be exactly twenty. A policy file containing `tracking_cpu: 5` does not prove any step ran there.

`thread-snapshots.jsonl` is sampled externally at most once per second, plus explicit model-ready and
playback-ended boundaries. The node waits for the boundary acknowledgments outside timed playback.
Snapshots preserve thread name, PID/TID, mask, scheduling policy, priority and last CPU from `/proc`.
Threads that exit while being read remain marked as exit races. Threads without managed registration remain
`unknown_library_or_middleware`; no fixed count or inferred RKNN/OpenCV role is assigned.
Preparation decoder threads are distinguished by the snapshot phase and are released before model-ready.

The regression source retains the verified, immutable decoded pixels in real RK3576 host frames.
It keeps the complete cache until `stop()` and each frame/read view retains its own storage lifetime.
Timed supply creates frame metadata without another image allocation or pixel copy; production
`copyGrayFrame`/`copyHostFrame` still deep-copy. Both experiment builds must contain the same support
patch, separately audited from the observation patch. Historical experiments label
the reference “4871442 algorithm baseline + common observation patch + common source-support fix”.
Those archived results do not validate a rebased observation implementation. A new
experiment requires newly audited build identities and matching observation semantics.

The historical `supply-timing.csv` column names `copy_begin_ns`, `copy_end_ns`, and `copy_cpu_ns`
now measure the source's frame-construction call (`retainGrayFrame`); they remain for file compatibility.
The frozen source/driver identity distinguishes these records from older deep-copy runs. The original
50 ms supply qualification, input pixels, timestamps, queue capacity, and preparation budgets are unchanged.

Thread snapshots record `observed_mono_ns` before reading each thread. Compare its allowed mask with
the managed role only when that read started at or after the role's registration time. Earlier
snapshots preserve the legal preparation range. A scan that crosses registration uses each thread's
start time rather than the scan's later completion time. Wrong masks at or after registration still
fail, as do invalid sample times. Older snapshots without the field use their existing snapshot time;
historical failed records remain unchanged.

## Evidence retention

The integrity contract applies to the complete retained run tree. `.docker-artifacts` and `/tmp` are disposable storage locations and may be cleared. Preserve required raw segments, exports and checksum manifests in an explicitly retained location; historical path names alone do not establish continued availability.

## Verification policy events

Format 3 adds `ValidityState`, `EvidenceClock`, `CandidateMatch`, `TrustLoss`, `TrustLossDisposed`, and `ValidAdmission`. The fixed event layout and exact integer decoding remain unchanged. Clock mapping, attempt/revision, source comparison origin, freshness eligibility, counter disposition, and separate revocation/publication epochs are structured values. `not_evaluated` appearance and motion hooks reject no candidate. Readers preserve formats 1/2 without inventing these policy fields and reject unknown formats. See the [diagnostics payload guide](../../../src/diagnostics/README.md#format-3-verification-evidence-and-ordered-loss) and [freshness metric contract](metrics.md#freshness-policy-evidence).

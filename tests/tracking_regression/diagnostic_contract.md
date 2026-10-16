# Diagnostic contract for fixed regression, version 1

The dependency is `src/diagnostics/event.h` format 1, imported from committed RK3576 source. An event is a fixed 256-byte record. Its exact identity is `(run_id, producer_id, producer_seq)`. `round_id` identifies the ON/OFF round; `update_id` identifies the tracker call. Requests and results use `request_id`, `source_update_id`, and `source_tracker_frame_id`. Times are integer `mono_ns` (monotonic nanoseconds), `frame_time_us` and `prediction_us` (system microseconds). The fixture supplies an exact `E0 + relative_us` mapping to `fixture_frame_id`. Camera driver identities remain unknown for the synthetic source; original driver sequences are provenance only.

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
| Output boxes | `Output.box`, `other_box`, `BoxValid`, `OtherBoxValid`, `values[1]` tracking | Real-time publication uses prediction `other_box`; finite coordinates and positive dimensions required |
| Actual publication | `PublishStart` / `PublishEnd`, run/round/update, `Complete`, reason | Join the corresponding Output exactly; normal return proves call completion, not subscriber receipt |
| Frame age/prediction lag | Publish end `mono_ns`, exact source frame and prediction timestamp; run M0/E0 | Reject ambiguous identity or clock jumps; do not match closest timestamps |
| Frame destinations | input log plus `FrameSelect`, `FrameSkip`, source EOF residuals | Every fixture frame has exactly one terminal destination; separate rejected/skipped/residual |
| Stage timing | `Timing.values[0]`, `Observed`, `Complete`, counts | Optional detail metrics are not applicable in fixed basic mode |

All event numeric fields (`flags`, producer/run/round/frame/request/update identities, source IDs, times, duration, both boxes, score, IoU, threshold, highest score, `values[7]`, `counts[4]`) are retained. `CaptureKnown`, `DriverKnown`, `FrameTimeKnown`, `PredictionKnown`, `BoxValid`, `OtherBoxValid`, `ScoreKnown`, `Complete`, `HasTarget`, `Observed` and `ErrorCodeKnown` are interpreted only where their event defines them. No unknown scalar is converted into a successful zero.

Each binary storage segment must have valid headers, versions, endian marker and CRC, an actual synchronized commit and a normal end record. No partial tail, duplicate identity, producer sequence gap, `Gap`, `AbnormalEnd`, health error/drop count or unknown tail is qualified. The run and round lifecycle must end normally; required call pairs and request terminal paths must reconcile. `Health.values[0..6]` contains generated/written/synchronized positions, dropped count, oldest unsynchronized age, sync duration and error. An unknown final synchronization state rejects a formal comparison. Raw storage is retained as `events.bin` and its original segments; completion records hash all evidence files.

A missing dependency, unsupported inference-observation field, missing actual publication boundary or unavailable complete raw event export prevents formal measurement. A healthy run with no catch-up events has count zero and duration `null` with reason `no_event`; that is not a diagnostic gap.

## Source-context interpretation 2

The binary record encoding remains version 1 and 256 bytes. Existing event and reason
numbers are unchanged. `RunStart.values[6] = 2` advertises the append-only interpretation.
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

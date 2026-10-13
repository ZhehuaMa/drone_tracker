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

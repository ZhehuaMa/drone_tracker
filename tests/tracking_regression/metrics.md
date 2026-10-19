# Behavior metrics and comparison protocol

These metrics describe program behavior for each independently configured video case. There are no human target labels. A valid publication means a normally returned publication call carrying a tracking flag and a finite box with positive width and height. It does not establish that the box contains the drone. Model scores and overlap between model boxes are not accuracy measurements. Publication return also does not establish receipt by a subscriber.

## Inputs, identities and clocks

`metrics.py` consumes the complete `events.jsonl` export, `input.jsonl` and a clock relation `{m0_ns,e0_us}`. `m0_ns` is the replay's monotonic start in nanoseconds. `e0_us` is the corresponding system time in microseconds. The collector supplies its independent integrity verdict to `compute(events, inputs, clock, complete=...)`. The calculator also detects gaps, abnormal endings, unhealthy diagnostics, duplicate identities and unpaired required events. Missing slow calls make the affected statistics unknown; they are never dropped to improve a percentile.

Event joins use `(run_id,round_id,update_id)` for update and publication calls, and `(run_id,round_id,request_id)` for detector calls. Source frames are mapped by exact `frame_time_us = e0_us + relative_us` equality. There is no nearest-time matching. Samples retain producer sequence numbers and causal identities, so the values can be traced to raw events. Chinese inspector names and numeric event/reason IDs are accepted. Unknown names or malformed required times cause a field-specific error.

The production `PublishEnd` does not contain the output box. Its matching `Output.other_box` supplies the actual realtime box used for publication. `OtherBoxValid` (flag 32) and `Output.values[1]` supply box and tracking validity. `Output.box` is the display box and is not substituted. An enriched collector record may instead explicitly provide `box` and boolean `tracking`. `Complete` (128) and reason `None` distinguish a normal publication return. Failed publication calls remain in raw evidence and invalidate formal results.

The calculator requires `DetectStage.Observed` (2048) to identify an actual inference invocation. A stage that fails during input preparation before inference does not count. Successful or inference-failed stages without this marker make the evidence incomplete. A reference build therefore needs the same additive diagnostic marker as its candidate; older ambiguous logs cannot supply a formal inference count.

## Configured windows and sample placement

The `full` window is `[0,D)` for this case’s resolved playback duration D. Extra named windows exist only when declared in the case configuration and must lie within full. The historical reference explicitly declares core `[13.5,17.5)`; a five-second or whole-video case never inherits it. All windows are half-open and relative to `m0_ns`. Event times are actual monotonic times, rather than source video positions or prediction message times.

| Sample family | Window selection |
|---|---|
| Update, initialization | Actual call start |
| Detection duration, execution/completion count and inference stages | Actual `DetectStart` of the request |
| Request acceptance/rejection, reading, overwriting, cancellation and cache lookup | Actual corresponding event time |
| Publication ratio, age and prediction lag | Actual normal `PublishEnd` |
| Completed recovery latency | Actual clear decision time |
| Invalid/recovery spans | Intersection of the actual interval and the window |
| Planned input, enqueue/rejection, lateness and input residual count | Scheduled source-frame time |
| Frame selection/consumer skip/diagnostic residual event count | Actual event time |

Completed durations retain the entire call. For example, an update beginning at 17.499 seconds and ending at 17.887 seconds contributes 388 ms to the core window. It is not clipped to 1 ms. Events finishing after 20 seconds remain in raw evidence and can complete a call that began within the full window.

An invalid interval begins with the first known invalid publication. More invalid publications do not restart it. The next valid publication ends it. A core interval inherited from a clear or invalid publication before 13.5 seconds is marked `left_truncated`. An interval unresolved at 17.5 seconds is marked `right_truncated`. The prefix before the first publication is separately reported as `no_output_prefix_ms`; it is not assumed to be invalid tracking.

## Descriptor and availability

Every metric carries `metric_version:1`, `unit`, `window`, `sample_count`, `availability` and `reason`. Values use JSON numbers; unknown values use `null`.

| Availability | Meaning |
|---|---|
| `available` | Complete evidence supports the value, including genuine zero counts |
| `no_event`, reason `no_event` | There are no duration samples or no ratio denominator |
| `incomplete` | Necessary evidence or an individual sample is missing/ambiguous |
| `not_applicable` | Reserved for an explicitly inapplicable measurement |

Incomplete descriptors retain `observed_value` or `observed_statistics` and raw samples for investigation. Their authoritative `value` or `statistics` is `null`; these observations must not enter a formal baseline. A complete run with no catchup frames has catchup count 0 and catchup duration statistics `null` with `no_event`. This is a normal condition, not diagnostic loss.

Duration statistics include sample count, arithmetic mean, minimum, maximum, P50, P95 and P99. A percentile selects the one-based sorted item `ceil(p*N)`. For samples 1 through 20 ms, P50=10, P95=19 and P99=20 ms. Cross-run medians use the mean of the two central values for an even number of runs; this differs intentionally from the nearest-rank within-run P50.

## Continuity and latency metrics

| Metric | Unit and samples | Computation and unavailable behavior |
|---|---|---|
| `publication_count`, `valid_publication_count`, `invalid_publication_count` | Count; normal returns | Separate total, valid and invalid counts; publication failures are integrity errors |
| `valid_publication_ratio` | Ratio; normal returns | Valid/total, with the three counts also in the descriptor; total 0 gives `null/no_event` |
| `max_invalid_span_ms` | ms; known invalid intervals intersecting the window | Maximum intersection length, with each interval and truncation flags; 0 when there is no known invalid interval |
| `max_no_publication_gap_ms` | ms; normal returns | Maximum adjacent gap including both window boundaries; a complete no-message window has its full window length; incomplete evidence gives `null` |
| `no_output_prefix_ms` | ms; initial prefix | Window intersection with time preceding the first output, with a right truncation flag |
| `clear_count`, `clear_count_by_reason` | Count; actual clear decisions | Only `Critical` decision events with clear reasons count. Unconfirmed results below the clear threshold do not count. Reasons remain distinct: `NoTarget`, `LowIou`, `YoloJump`, `NanoJump`, `NoResultTimeout`, `NanoUpdateFailed` |
| `recovery_latency_ms` | ms; clears within the window that eventually recover | Clear to next valid normal publication, retaining full latency. Unrecovered attempts have `value_ms:null`, `wait_lower_bound_ms` and truncation flags in `censored`; they do not enter completed quantiles. Earlier clears intersecting this window appear in `inherited` separately |
| `recovered_count`, `unrecovered_count` | Count; clears in the window | Distinguish completed and censored recovery attempts |
| `frame_age_at_publish_ms` | ms; normal returns | Actual publication minus exact source-frame planned time. Unknown source identity creates a null sample and an incomplete descriptor |
| `prediction_lag_at_publish_ms` | ms; normal returns | Publication minus `m0_ns + (prediction_us-e0_us)*1000`; absent prediction flag gives unknown, independently of frame age |

For a frame planned at 15.721 ms, prediction at 100 ms, and publication at 403.721 ms, frame age is 388 ms and prediction lag is 303.721 ms. The two values describe different delays.

## Pipeline and input metrics

| Metric family | Unit and meaning |
|---|---|
| `update_count`, `update_duration_ms` | Calls and start/end duration; includes all nested work, never adds it again |
| `update_throughput_hz` | Update starts divided by window seconds; never an average of inverse call durations |
| `detector_count`, `detector_duration_ms` | Executed requests and start/end duration |
| `request_accepted_count`, `request_rejected_count`, `request_rejected_count_by_reason` | Distinct request IDs and exact rejection reasons |
| `request_executed_count`, `request_completed_count` | Distinct requests starting/completing execution, grouped by execution start |
| `result_read_count`, `result_overwritten_count`, `request_cancelled_count` | Distinct request IDs for each lifecycle action; overwritten successful results are not counted as detection failure |
| `successful_result_history_miss_count` | Cache misses linked to a `DetectEnd` with `HasTarget` (512); source eviction is not initialization failure |
| `detector_stage_count`, `detector_inference_count` | Stage attempts versus actual inference calls. A single request can include region, full-image and tiled calls. Inference evidence includes stage kind/index and request identity |
| `initialization_count`, `initialization_success_count`, `initialization_failure_count`, `initialization_duration_ms` | Outer initialization attempts and matched start/end duration. Nested NanoTrack failure detail events retain evidence but do not count as extra attempts |
| `catchup_attempt_count`, `catchup_success_count`, `catchup_failure_count`, `catchup_duration_ms` | Nonempty catchup phases. Duration is successful outer `InitializeEnd` to `CatchupEnd`, including bookkeeping; an initialized tracker with zero follow-up frames contributes no catchup duration |
| `catchup_step_count`, `catchup_step_success_count`, `catchup_step_failure_count`, `catchup_step_duration_ms` | Individual follow-up attempts, success flags and recorded step durations. A successful phase can still contain failed steps |
| `recovery_update_duration_ms` | Entire update containing initialization; separate from initialization and catchup stage costs |
| `input_planned_count`, `input_enqueued_count`, `input_rejected_count`, `input_unsupplied_count` | Planned frames, successful enqueue, queue rejection after actual supply, and unsupplied frames. These categories retain the original schedule |
| `input_late_count`, `input_supply_lateness_ms` | Positive actual-minus-planned supply delays and all lateness samples; zero lateness is a real measured duration |
| `input_residual_count` | Explicit input rows marked as still queued at teardown; counted by their planned source time |
| `frame_selected_count`, `frame_skipped_count`, `frame_residual_count` | Actual `FrameSelect`, `FrameSkip/LatestSkip`, `FrameSkip/Termination` events and exact source identities. Input teardown residuals remain separate because they need not have a diagnostic termination event within the window |

The collector checks whole-run conservation across all 1,200 input IDs. Window event counts can differ from input counts because the two use distinct placement rules and teardown can occur after 20 seconds. A complete no-event duration is null; a real count of no actions is zero. Unpaired necessary events or mismatched catchup summary/step counts invalidate the whole record.

## Frozen baseline and differences

Run these commands from the repository root. All output destinations must be new:

```bash
python3 tests/tracking_regression/baseline.py \
  runs/group/reference-01 runs/group/reference-02 runs/group/reference-03 \
  --output baselines/reference-v1
python3 tests/tracking_regression/compare.py baselines/reference-v1 \
  runs/group/candidate-01 runs/group/candidate-02 runs/group/candidate-03 \
  --output comparisons/candidate-v1
```

Freezing requires at least three distinct qualified runs of the fixed 20-second realtime case. The completion checksum manifest must cover identity, input, full exported events, raw `.dtd` or `events.bin`, frame/message tables and metrics. All checksums are rechecked. Frozen references include the completion checksum too. The source snapshot and binary SHA-256 are mandatory; a commit alone cannot describe a dirty build. Existing destinations are refused. Comparing has no baseline update mode.

All comparison-group identity fields must be present and nonempty: fixture pixels/timing, attitude, models, algorithm, concrete board, runtime, driver, system library, dependencies, CPU policy, workload, observation, test driver, metric schema and metric calculator. Source code identity sits outside the group and may differ. Every differing identity leaf is reported by field name. Missing identity, altered evidence, unqualified runs and mismatched metric versions/windows return an incomparable report with no performance verdict.

For each scalar or duration statistic, reports retain every round, median and min/max range. If one round has no event, no numeric aggregate or difference is formed for that statistic; the round and availability transition remain visible. Sparse complete reason counts mean real zero counts and can be compared as such.

The difference is candidate minus baseline. Reference gap values 380,400,420 ms and candidate values 240,250,270 ms give medians 400→250 ms, difference -150 ms and relative change -37.5%. A count change 0→2 has relative percentage `null` with `zero_baseline_new_event`. 0→0 is 0%. A ratio .80→.90 has +10 percentage points and +12.5% relative change. The Markdown report uses Chinese, as the specification requires. JSON and CSV use stable field names and retain units and per-round availability.

## Optional, explicit thresholds

No thresholds means `decision.status=not_set` and “未设置判定规则” in the report. Changes remain increase/decrease/unchanged/unavailable. There is no inferred overall quality score.

Provide a separate versioned file when a performance decision is needed:

```json
{
  "schema_version": 1,
  "threshold_version": "limits-1",
  "rules": [
    {
      "metric": "full.max_no_publication_gap_ms",
      "direction": "lower",
      "required": true,
      "allowed_change": {"type": "relative_percent", "value": 5}
    },
    {
      "metric": "core.valid_publication_ratio",
      "direction": "higher",
      "required": true,
      "allowed_change": {"type": "percentage_points", "value": 0}
    }
  ]
}
```

Pass it with `--thresholds limits.json`. `lower` allows an increase up to the stated budget; `higher` allows a decrease up to that budget. Types are `absolute` (the metric's unit), `relative_percent` or `percentage_points` (ratios only). Every rule states whether its measurement is required. An unavailable required metric makes the decision `not_evaluated`. A zero-baseline relative threshold cannot be evaluated for a nonzero candidate. The entire rule file, its version and SHA-256 are copied into the comparison evidence; the comparator never modifies it based on results.

The comparison CLI returns 2 for incomparable sets, 1 for failed or unevaluable explicit rules and 0 for a completed comparable report with no rules or passing rules. Reports are written in each case. Input/format errors and existing destinations fail without overwriting evidence.

## Arbitration extensions (metric version 2)

Established descriptors retain metric version 1 and their original formulas.
`request_cancelled_count` still includes every cancellation. New descriptors use
metric version 2 and do not change that total:

- `request_superseded_count`: `RequestCancel/Superseded` records in the window.
- `request_cleanup_cancelled_count`: `RequestCancel/Cleaning` records in the window.
- `source_metadata_available_count`: `SourceContext/None` records in the window.
- `source_metadata_unavailable_count`: `SourceContext/ContextUnavailable` records.
- `recovery_unavailable_count`: `SourceContext/RecoveryUnavailable` records.
- `request_queue_wait_ms`: `DetectStart.mono_ns - accepted RequestDecision.mono_ns`,
  joined by exact request identity, attributed to execution start.
- `detection_execution_ms`: `DetectEnd.mono_ns - DetectStart.mono_ns`, attributed
  to execution start. This interval includes the whole existing search cascade.
- `source_capture_to_consumption_ms`: result-read time minus original capture time
  mapped through the frozen input clock, attributed to consumption.

All five new counts require `RunStart.values[6] = 2`. Without it their values are
null with `availability=unavailable` and reason `historical_field_absent`. This prevents
invented zeros for historical logs. Unknown capture/clock relationships retain unknown
samples. Every new duration retains the raw timestamps and exact identities alongside
its sample. Superseded requests have no execution or normal no-target samples.
Image-history misses retain their established meaning independently of source metadata.

Baseline freezing and comparison accept the explicitly named metric-2 extensions.
A historical-field absence does not invalidate an otherwise qualified run. A metric-2
capture-to-consumption duration with unknown source timestamps retains unknown samples
and is excluded from numeric aggregation. This exception applies only to that named new
measure. Incomplete established metrics, unsupported metric versions, and incomplete raw
records still prevent formal comparison. Reports retain each descriptor's metric version.

## Multi-case operation and record migration

Use `run_suite.py CONFIG.yaml` with one explicit `prepare`, `run`, `freeze` or `compare` operation. The complete sections and sequence are documented in [README.md](README.md) and [configs/example.yaml](configs/example.yaml). Every case has an independent fixture, at least three qualified reference runs, frozen baseline, at least three independent candidate runs and comparison output. References/candidates match by `case_id` and content, never list position. The specified four-case template requires twelve rounds per cohort and twenty-four total; no cross-case sample pooling or percentage averaging is permitted.

New metrics containers use `schema_version: 2`; unchanged metric formulas retain their original `metric_version` (including existing version-2 diagnostic extensions). Old containers retain fixed full/core validation. Unknown versions, mixed old/new runs, changed numeric selection/windows or changed preparation/cleanup budgets cannot enter one formal group. File/config paths and unrelated case lists remain provenance. Comparison output distinguishes completed operation, comparable evidence and explicit threshold performance decision. Missing cases and unset thresholds cannot yield a suite pass.

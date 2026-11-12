# Spec Delta

## MODIFIED Requirements

### Requirement: Age evidence preserves original time and statistical boundaries

Reports SHALL separately retain queue wait, detection execution, and source-capture-to-consumption age when their clock relations are known. Unknown relations SHALL remain unknown. Reports SHALL record freshness eligibility and disposition under the effective confirmation policy. Stale provisional seeding SHALL remain distinguishable from verification and diagnostic-only rejection. Historical records MUST NOT acquire an age-rejection policy they did not implement.

Reports SHALL distinguish confirmation eligibility from seed eligibility for each consumed result. When a request submitted during expired A seeds B, reports SHALL retain the original request-attempt identity, the new seed-attempt identity, source image, and one terminal disposition. They SHALL retain specific rejection reasons for wrong round, disposed request, unavailable pixels, invalid identity or clock relation, and protection of an active replacement. A new seed MUST NOT be counted as restored output or verification.

#### Scenario: Long queue followed by short detection
- **WHEN** a request waits approximately 497 ms and then executes for approximately 73 ms
- **THEN** the report retains both intervals and includes the delay before consumption in total evidence age; under a 100 ms confirmation lifetime, this result cannot confirm output

#### Scenario: Stale result seeds without confirming
- **WHEN** a 120 ms old positive result seeds a provisional attempt while searching under a 100 ms confirmation lifetime
- **THEN** the report records stale-seed use and invalid output, without recording fresh verification or a counter change against an existing instance

#### Scenario: Old-attempt result has useful seed pixels
- **WHEN** D32 names expired A but is first disposed as B's seed while frame 42 is processed
- **THEN** the report preserves A as request provenance, B as the new seed attempt, and seeding as its terminal use; it records neither renewed A permission nor a verified B from D32

### Requirement: Version comparisons preserve measurement compatibility

Formal comparisons SHALL identify request-disposition and metadata-availability definitions. Missing historical fields SHALL be unavailable. Existing metrics SHALL retain their definitions unless a metric-version change explicitly declares otherwise. New observation evidence SHALL be versioned. Missing historical floating boxes, LOS, or per-topic outcomes MUST NOT be reconstructed from display boxes. Unrecognized formats SHALL be rejected. Clock-mapping differences SHALL be explicit comparison conditions.

Comparisons SHALL identify whether the no-result frame-count cleanup policy was active. Historical watchdog counts and timeout events SHALL retain their original meaning. Missing new seed-disposition or cleanup-policy evidence SHALL be unavailable. A historical frame-count timeout MUST NOT be relabeled as an elapsed-time candidate timeout.

#### Scenario: Reference predates metadata availability records
- **WHEN** the reference records historical image misses but has no independent metadata-availability field
- **THEN** that field is unavailable for the reference, existing compatible metrics remain independently usable, and no metadata-success count is invented

#### Scenario: 旧日志没有 LOS 证据
- **WHEN** format-1 records are read alongside a candidate using floating observation evidence
- **THEN** each format is parsed by its version, historical LOS metrics are unavailable, and integer boxes do not substitute for exact floating observations

#### Scenario: Clock mappings differ
- **WHEN** one run measures evidence age from synthetic standalone admission and another measures it from production capture
- **THEN** reports identify their mapping versions and mark evidence-age and timing comparisons incompatible; they do not issue a same-condition performance pass

### Requirement: Controlled evaluation includes costs of the new arbitration

Evaluation SHALL retain fixed cases, models, settings, board, Runtime, driver, and qualified-run rules. Reports SHALL include output continuity, publication intervals, recovery work, request latency, replacements, and process memory. They SHALL also report confirmation age, revocation delay, valid-output availability, and recovery duration with explicit windows and sample counts. Unlabeled footage MUST NOT establish tracking accuracy or true-target loss-detection latency.

#### Scenario: More valid output with unchanged watchdog
- **WHEN** a historical reference has higher valid output while unusable replies still reset its unchanged legacy watchdog, and a revised candidate removes frame-count cleanup
- **THEN** the report identifies both policies, preserves historical event meanings, and reports current attempt completion reasons without inventing candidate-timeout events from the old frame counts

#### Scenario: Delayed results cross former frame boundaries
- **WHEN** controlled interval-1 and interval-4 runs cross updates 15/16 and 60/61 before their injected candidate deadline, then advance to that deadline
- **THEN** evidence distinguishes retained provisional work from elapsed-time abandonment; confirmation expiry and loss deduplication are checked independently of frame-count cleanup

#### Scenario: Expiry succeeds while output is unavailable
- **WHEN** every expiry check behaves correctly but no acquisition verifies within the confirmation lifetime
- **THEN** the report records zero valid-output availability and incomplete recovery episodes separately from expiry correctness; it does not infer usable vision or overall performance acceptance

## ADDED Requirements

### Requirement: Evidence clock mappings are identifiable and reproducible

Reports SHALL retain a versioned clock-mapping identity, source timestamps, mapped monotonic evidence anchors, decision times, and deadlines as exact integers with declared units. Standalone replay admission anchors SHALL be identified as synthetic. Production capture age MUST NOT be reconstructed from tracker admission. Missing clock evidence SHALL be unavailable. Synthetic replay MUST NOT establish production timing or camera-rotation acceptance.

#### Scenario: Standalone replay retains source and execution time
- **WHEN** an image has source time 1020 ms, monotonic admission 5140 ms, consumption 5170 ms, and a 100 ms lifetime
- **THEN** the report retains all three times, records synthetic evidence age 30 ms and deadline 5240 ms, and does not relabel 1020 ms as a production capture-to-execution mapping

### Requirement: Availability and recovery milestones have separate meanings

Reports SHALL define the valid-frame ratio denominator and measurement window. They SHALL distinguish invalid output from absent output. Recovery records SHALL separate local verification, first valid-pair admission, and per-topic publication outcomes. Episodes without restoration SHALL be marked incomplete at the window boundary. Loss and OFF event pairs MUST NOT enter frame-valid ratios. Missing evidence MUST NOT become zero duration or successful recovery.

#### Scenario: Loss notification delays recovery publication
- **WHEN** revocation occurs at 1090 ms, recovery verifies at 1160 ms, and the first valid pair is admitted at 1180 ms after loss notification
- **THEN** the report records 70 ms to local verification and 90 ms to valid admission, retains each topic's publication outcome, and does not call 1160 ms downstream restoration

#### Scenario: No acceptance threshold is configured
- **WHEN** an evaluation measures expiry correctness and availability but provides no versioned availability or recovery threshold
- **THEN** it reports the measurements and unset performance criteria without inventing a performance pass; protocol checks retain their independent outcomes

### Requirement: Stage jobs and discovery plans preserve distinct historical meanings

Reports SHALL distinguish submitted image/request, one-call job, and fixed-image plan identities and completion counts. They SHALL retain exact source/region provenance, coverage, parent outcomes, replacements, cancellations, and supersession. Historical whole-cascade requests MUST NOT silently become one-call jobs. Changed layouts/metric meanings SHALL be versioned; missing historical fields SHALL be unavailable.

#### Scenario: One historic request has seven model calls
- **WHEN** a historical cascade record is compared with seven new stage-job records
- **THEN** the report preserves the distinct request/job/plan denominators and does not report a sevenfold useful-confirmation improvement from relabeling alone

### Requirement: Timing distinguishes execution from capacity and decision waiting

Reports SHALL separate preprocessing, model execution, postprocessing, queued waiting, unread-mailbox waiting, seed-disposition waiting, and tracker preparation blocking when measured. They SHALL retain take/credit-release and early seed-acceptance times, source age, inter-confirmation gaps, and remaining lifetime. Unmeasured components MUST remain unavailable. A synchronous call duration MUST NOT imply a tracking-thread wait.

#### Scenario: D20 waits during initialization
- **WHEN** a later-image call finishes while the tracker initializes B and its result waits in the mailbox
- **THEN** execution and mailbox delay remain separate, original source age includes both, and early acceptance is not mislabeled initialization completion

### Requirement: Scheduler evidence demonstrates bounded work and coverage

Evaluation SHALL record peak active/pending image/job/result ownership, targeted/discovery service order, coverage continuation, cancellation causes, tile rotation, and result no-overwrite checks. Partial misses SHALL remain distinct from terminal plan verdicts. A parent verdict SHALL count once. Newer-agreement supersession and first-miss fallback activation SHALL be identifiable.

#### Scenario: New images arrive during every discovery stage
- **WHEN** a fixed-image plan progresses while pending images are repeatedly replaced
- **THEN** the report can establish its bounded ownership and actual regional coverage without interpreting replacements as evaluated images or accumulating cross-image absence

### Requirement: Crop and scheduler gains require separate experiments

Board evaluation SHALL identify crop-only, scheduler-only, and combined experiments using fixed models, source input, and declared profiles. It SHALL compare current ROI, previous successful region, and recentered successful scale/aspect where available. It SHALL report source age, usable-confirmation gaps, availability, recovery, and first-four-second versus full-eight-second source windows separately. Unset performance criteria MUST NOT imply acceptance.

#### Scenario: More model completions but few useful confirmations
- **WHEN** one-call scheduling increases completed jobs without improving same-image fresh agreement
- **THEN** reports keep job rate separate from useful confirmation and valid-output availability and do not claim detection effectiveness from scheduling alone

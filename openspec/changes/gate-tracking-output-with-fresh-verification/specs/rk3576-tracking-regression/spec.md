# Spec Delta

## MODIFIED Requirements

### Requirement: Age evidence preserves original time and statistical boundaries

Reports SHALL separately retain queue wait, detection execution, and source-capture-to-consumption age when their clock relations are known. Unknown relations SHALL remain unknown. Reports SHALL record freshness eligibility and disposition under the effective confirmation policy. Stale provisional seeding SHALL remain distinguishable from verification and diagnostic-only rejection. Historical records MUST NOT acquire an age-rejection policy they did not implement.

#### Scenario: Long queue followed by short detection
- **WHEN** a request waits approximately 497 ms and then executes for approximately 73 ms
- **THEN** the report retains both intervals and includes the delay before consumption in total evidence age; under a 100 ms confirmation lifetime, this result cannot confirm output

#### Scenario: Stale result seeds without confirming
- **WHEN** a 120 ms old positive result seeds a provisional attempt while searching under a 100 ms confirmation lifetime
- **THEN** the report records stale-seed use and invalid output, without recording fresh verification or a counter change against an existing instance

### Requirement: Version comparisons preserve measurement compatibility

Formal comparisons SHALL identify request-disposition and metadata-availability definitions. Missing historical fields SHALL be unavailable. Existing metrics SHALL retain their definitions unless a metric-version change explicitly declares otherwise. New observation evidence SHALL be versioned. Missing historical floating boxes, LOS, or per-topic outcomes MUST NOT be reconstructed from display boxes. Unrecognized formats SHALL be rejected. Clock-mapping differences SHALL be explicit comparison conditions.

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
- **WHEN** a candidate has a higher valid-box ratio but still receives unusable results that reset the legacy watchdog
- **THEN** reports retain both observations; any confirmation-age bound is supported separately by the effective expiry policy and admission evidence, and neither observation proves tracking correctness

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

# Tracking Regression Delta

## ADDED Requirements

### Requirement: Superseded requests reconcile without invented inference

A superseded request is accepted but replaced before execution. Regression collection SHALL retain its cancellation reason and successor identity. It SHALL count supersession separately from rejection, execution, normal no-target detection, and cleanup cancellation.

#### Scenario: Pending request replaced twice
- **WHEN** A executes while accepted B is replaced by C and C is replaced by D
- **THEN** B and C each have one superseded terminal outcome, D retains its acceptance identity, and B and C add no inference or no-target count

#### Scenario: Cancellation record missing
- **WHEN** B is accepted, never executes, and has neither a supersession nor cleanup-cancellation terminal record
- **THEN** the run remains incomplete and is excluded from formal comparison

### Requirement: Comparison metadata and recovery pixels have separate availability

Reports SHALL distinguish unavailable source comparison metadata from an evicted source image. They SHALL preserve the comparison basis, actual overlap, recovery eligibility, and unconfirmed-counter change associated with the result.

#### Scenario: Image miss with usable metadata
- **WHEN** request 62 reads source frame 744 after image history reaches 748–777, and source metadata gives overlap 0.584
- **THEN** the report retains the image-history miss, marks metadata available, records overlap 0.584, and does not label the event an initialization failure unless initialization actually ran and failed

#### Scenario: Missing comparison does not mean no target
- **WHEN** a positive result cannot be compared because required metadata is unavailable
- **THEN** the report records a positive detection and unavailable comparison, rather than a completed no-target detection or a measured zero overlap

### Requirement: Age evidence preserves original time and statistical boundaries

Reports SHALL distinguish queue wait, detection execution, and source-capture-to-consumption age when their timestamps are known. Unavailable time relationships SHALL remain unknown. Age evidence MUST NOT imply a result-age rejection policy.

#### Scenario: Long queue followed by short detection
- **WHEN** a request waits approximately 497 ms and then executes for approximately 73 ms
- **THEN** the report preserves both intervals separately and includes consumption delay when computing total age

### Requirement: Version comparisons preserve measurement compatibility

Formal comparisons SHALL identify the request-disposition and metadata-availability metric definitions. Historical logs lacking a new field SHALL report that field as unavailable. Existing metrics SHALL retain their definitions unless an explicit metric-version change declares otherwise.

#### Scenario: Reference predates metadata availability records
- **WHEN** a reference log records image-history misses but has no independent metadata-availability field
- **THEN** the new field is unavailable for the reference, existing comparable metrics remain separately usable, and no metadata-success count is fabricated

### Requirement: Controlled evaluation includes costs of the new arbitration

Evaluation SHALL retain the fixed fixture, models, parameters, concrete board, Runtime, driver, and qualified-run rules. Reports SHALL include output continuity, publication gaps, recovery work, request delays, supersessions, and process-memory observations. Unannotated footage MUST NOT establish tracking accuracy or genuine-loss recognition delay.

#### Scenario: More valid output with unchanged watchdog
- **WHEN** a candidate has a higher valid-box ratio but continues receiving unusable results that reset the unchanged watchdog
- **THEN** the report states both observations and does not claim bounded confirmation age or proven tracking correctness

# generic-frame Spec Delta

## ADDED Requirements

### Requirement: Frames expose optional immutable recording receipts

A recording receipt identifies whether a video session accepted one exact frame. The shared frame contract SHALL expose an optional value-only receipt. Its fields SHALL be immutable before the frame enters a consumer queue. A receipt MUST NOT change the frame's pixels or capture timestamp.

#### Scenario: Shared consumers retain the same receipt
- **WHEN** capture publishes an accepted frame and detection and history retain it
- **THEN** both consumers observe the same receipt and original capture timestamp throughout their references' lifetimes

#### Scenario: Recording status changes after publication
- **WHEN** a queued frame was unaccepted and its recorder later becomes ready
- **THEN** that frame remains unaccepted

### Requirement: Recording identities remain usable without diagnostics

An accepted receipt SHALL identify the stream, tracking round, recording request generation, and 64-bit capture identity. A request generation distinguishes successive recording requests within one stream. Receipt identity SHALL remain available when diagnostics are disabled. Frame identity MUST NOT depend solely on timestamps or the wrapping driver sequence.

#### Scenario: Diagnostics are disabled
- **WHEN** SC233HGS capture records a frame with diagnostics off
- **THEN** the consumer can still identify its recording request and capture identity and determine its acceptance

#### Scenario: Driver sequence wraps
- **WHEN** two frames in one stream share a driver sequence after its counter wraps
- **THEN** their distinct capture identities prevent one frame's receipt from authorizing the other

### Requirement: Optional receipts preserve existing backend compatibility

Backends without recording receipts SHALL return an unavailable receipt by default. Receipt metadata MUST NOT expose hardware SDK types. Adding receipt metadata SHALL preserve the concrete storage compatibility checks, pixel ownership, and read-view lifetimes required by the selected backend.

#### Scenario: Receipt-bearing RK3576 frame reaches preprocessing
- **WHEN** an accepted Gray8 camera frame reaches RK3576 preprocessing
- **THEN** it passes the same concrete storage checks as an otherwise equivalent camera frame without receipt metadata

#### Scenario: Unaffected backend supplies a frame
- **WHEN** Jetson or file-input code produces a frame without a receipt
- **THEN** its existing pipeline remains usable without implementing camera-video acceptance

#### Scenario: Last frame reference outlives capture
- **WHEN** capture stops while a detector or read view retains a receipt-bearing frame
- **THEN** its pixels and immutable receipt remain valid until the last owning reference is released

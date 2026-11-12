# Spec Delta

## ADDED Requirements

### Requirement: Session recording configuration travels with the release

An RK3576 release SHALL contain the selected startup recording configuration and its content fingerprint. The configuration SHALL identify the fixed profile, name overrides, sampling limits, and storage limits. Packaging and installation MUST NOT silently substitute different recording defaults. Rollback SHALL restore configuration compatible with the selected program.

#### Scenario: Inspect the selected recording policy
- **WHEN** a release is packaged with an explicit camera configuration containing `bag.*` overrides
- **THEN** the installed configuration preserves those overrides byte for byte and the release manifest records its fingerprint

#### Scenario: Roll back the expanded recorder
- **WHEN** an operator restores a release from before the expanded recorder
- **THEN** the matching older configuration is restored rather than passing new `bag.*` settings to an incompatible program

### Requirement: Typed recording dependencies are available without the OSD adapter

RK3576 recorder builds SHALL use the real `quadrotor_msgs` source package for their target architecture. Releases SHALL include the non-Python message libraries required by all catalog types. This dependency MUST NOT depend on enabling the optional OSD feedback adapter. Release validation SHALL verify that the installed node can load its typed recording dependencies.

#### Scenario: OSD feedback adapter disabled
- **WHEN** an RK3576 release is built with the optional OSD low-level-feedback adapter disabled and session recording enabled
- **THEN** the recorder can still handle `ControlCommand` and `LowLevelFeedback` using the packaged, architecture-matched message libraries

#### Scenario: Missing shared-message dependency
- **WHEN** release verification finds a missing or wrong-architecture recording message library
- **THEN** verification fails with the missing dependency identified rather than delivering an apparently complete recorder

### Requirement: Expanded recording adds no recorder service

The expanded recorder SHALL run inside the existing tracker process. Deployment MUST NOT create a separate bag-recorder service or leave a second legacy RK3576 bag writer active. Existing independent camera-video and diagnostic services SHALL retain their separate responsibilities. System RKNN Runtime selection SHALL remain unchanged.

#### Scenario: Inspect the expanded release services
- **WHEN** the expanded recorder release is installed
- **THEN** the tracker owns its one bag writer, no bag-recorder service is added, and camera-video recording and diagnostics retain their existing lifecycle controls

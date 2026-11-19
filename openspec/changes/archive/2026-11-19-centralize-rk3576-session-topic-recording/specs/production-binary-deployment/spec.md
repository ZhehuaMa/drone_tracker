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

### Requirement: Standard installation selects CPU 2 for topic recording

The standard packaged policy SHALL use `mode: role-pinned`, allow `[2, 4, 5, 6, 7]`, and set `roles.topic_recording: 2`. Fresh installation and upgrade SHALL install the selected CPU-policy bytes and use them through the same service and direct launcher. Other standard role assignments SHALL remain unchanged. A policy-loaded message alone MUST NOT count as proof of worker binding.

#### Scenario: Install the standard policy
- **WHEN** a recorder-enabled release is packaged without `--cpu-policy` and is installed on a compatible board
- **THEN** its installed policy matches `deployment/cpu-affinity.yaml`, and both topic-recorder threads confirm CPU 2 before work while camera-video recording remains on CPU 7

#### Scenario: Upgrade an older installed CPU policy
- **WHEN** the old installation permits only CPU 4–7 and a release using the standard new policy is installed
- **THEN** the old policy is backed up, the new policy permitting CPU 2 is installed regardless of any independent camera-configuration retention rule, and both launch entries use that selected policy after restart

### Requirement: New role-pinned recorder packages name the topic CPU explicitly

Packaging and installation preflight SHALL require explicit `roles.topic_recording` when the selected configuration enables topic recording in role-pinned mode. A valid explicit custom CPU policy SHALL be preserved unchanged. Custom `mode: inherit` SHALL remain an intentional unpinned override. With `need_save=false`, an unused topic role MAY be absent. Missing required fields SHALL produce an actionable error without silently rewriting the selected policy.

#### Scenario: Explicit role on a custom core
- **WHEN** `--cpu-policy` selects a valid role-pinned policy with `topic_recording: 3` and CPU 3 in its allowed set
- **THEN** the release and installation preserve those policy bytes and the topic threads use CPU 3 rather than silently substituting CPU 2

#### Scenario: Legacy role-pinned input for a new recorder-enabled package
- **WHEN** the selected configuration has `need_save=true` but the selected role-pinned policy omits `topic_recording`
- **THEN** packaging or installation preflight fails before installed files or services change, identifies the missing field, and explains that its core must also be in `allowed_cpus`

#### Scenario: Explicit inherited-mode override
- **WHEN** an operator explicitly selects a valid `mode: inherit` policy for a recorder-enabled package
- **THEN** the selected policy is packaged and installed unchanged, workers retain inherited affinity, and the release does not claim CPU 2 binding

#### Scenario: Recorder-disabled package using an older policy
- **WHEN** the selected configuration has `need_save=false` and its otherwise valid policy lacks `topic_recording`
- **THEN** the package remains valid without adding an unused role or starting topic-recorder threads

### Requirement: CPU policy validation distinguishes packaging from target availability

Offline packaging SHALL validate CPU-policy structure without requiring the build host to expose the target CPU numbers. Installation preflight and startup SHALL check selected cores on the actual target. An unavailable selected core or invalid explicit topic role SHALL fail preflight before installed files or service states change. Check-only mode MUST NOT start or stop services or rewrite policy files.

#### Scenario: Build host lacks the target core topology
- **WHEN** a valid standard RK3576 policy is packaged on a build host that does not expose CPU 2
- **THEN** structural validation accepts the policy without treating build-host topology as the board's availability evidence

#### Scenario: Target CPU 2 is unavailable
- **WHEN** installation or `deploy.sh --check` validates the standard policy on a target where CPU 2 cannot be used
- **THEN** preflight reports CPU 2 unavailable, returns nonzero, and preserves the previous installation and service states

## MODIFIED Requirements

### Requirement: 收集器绑定不改动其他执行单元

Collector binding MUST NOT change other existing processes' or threads' CPU affinity. It MUST NOT rewrite the selected shared allowed set or business-role mapping. The business launcher, priorities, frequency control, and kernel isolation SHALL retain their behavior. The display SHALL keep its selected core. Diagnostic failure SHALL preserve independent business operation and existing degraded-diagnostics handling.

#### Scenario: 保留生产业务映射
- **WHEN** the collector starts through its service or direct entry using the standard policy with allowed CPUs `[2, 4, 5, 6, 7]`
- **THEN** that allowed set remains unchanged, topic recording remains CPU 2, control/tracking remain CPU 4, detection remains CPU 5, display remains CPU 6, and capture/camera-video recording remain CPU 7

#### Scenario: 保留旁路进程和调用方
- **WHEN** an existing auxiliary process and caller remain running while the two diagnostic entry points are started and stopped
- **THEN** their existing thread masks remain unchanged and do not inherit the collector's `{6}` mask

#### Scenario: 诊断无法绑定时业务继续
- **WHEN** the business process is healthy but the independent diagnostic entry fails because CPU 6 is unavailable
- **THEN** the business process remains alive with its existing selected CPU policy and existing diagnostics-unavailable handling

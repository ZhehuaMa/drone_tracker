# Proposal

## Why

An operating session currently saves only a small part of the messages exchanged by the bridge, tracker, and interceptor, leaving gaps when investigating flight behavior. Recording their known application topics through one component, with explicit sampling for frequent or large messages, will provide broader evidence while controlling storage work.

For example, today an ON/OFF session can contain camera orientation and target observations but omit the control commands and mission state that explain the response. After this change, the same session will retain those topics, preserve their source timestamps where available, and report exactly which samples were intentionally omitted or unexpectedly lost.

The expanded recorder also needs a repeatable CPU placement when installed. The CPU 2 measurements in [the production comparison](../../../docs/reports/performance/RECORDER_CORE_AFFINITY_20261113.md) support a standard installation policy that keeps topic recording away from the existing tracking, detection, and display cores. These measurements motivate the choice; they do not validate a new automatic-binding implementation or establish zero overhead.

## What Changes

- Introduce one RK3576 `RecordingManager` inside the existing tracker process. It owns the complete recording catalog, recording subscriptions, timestamp rules, sampling, session admission, status, and the existing asynchronous `BagSession` writer. Remove the distributed RK3576 recording paths rather than retaining separate old and new recorders.
- Freeze the application-topic catalog at startup. Cover the known published and consumed interfaces of `apm_bridge`, `drone_interceptor`, and `drone_tracker`, including optional radar inputs. Resolve configured topic names and remappings before subscriptions are created. Do not discover unrelated domain topics or rescan during recording.
- Keep recording session by session under `need_save` and ON/OFF. Service preview does not open a bag. EOF, round-ending failures, and normal service shutdown close an active session. A sudden power loss has no guaranteed final-message retention interval.
- Keep one bag writer thread. Give recording subscriptions a dedicated ROS callback executor, which runs their callbacks separately from tracker control. Preserve direct, paired bbox/LOS submission and authoritative trigger forwarding as narrow integration interfaces.
- Give the recording executor and bag writer one explicit `topic_recording` CPU role. The standard installed policy selects CPU 2 and permits `[2, 4, 5, 6, 7]`. Keep the camera-video role on CPU 7 and ROS communication-resource creation on CPU 6. Preserve deliberate custom-policy overrides and the separate legacy runtime bindings when the new role is absent.
- Use each message's `header.stamp`, or `.stamp` for the Header trigger. Use receipt time for headerless messages and explicitly diagnosed invalid-stamp fallbacks. Preserve original payloads and the shared observation timestamp.
- Apply fixed, configurable sampling rules only to recording. Retain commands, events, and observations by default. Limit high-rate telemetry and debug images without reducing the data delivered to the algorithms.
- **BREAKING (bag contents):** replace the current fixed five-topic bag contract with the expanded catalog. Permit sampled `/fpv/tracker_image` recording when that topic is actually published. This intentionally replaces its existing blanket bag exclusion; recording does not enable debug-image publication or preview messages.
- Keep the 256-message and 64 MiB admission limits. Add per-topic limits, reserved control capacity, actual variable-payload accounting, and separate counters for policy omissions and unexpected loss. Cache the latest retained gimbal mode for an initial session snapshot.
- Split large bags into additional files without deleting old evidence. Stop bag admission on its storage quota, low free space, or writer failure. Keep healthy tracking, HDMI, independent camera-video recording, and diagnostics running.
- Package the typed shared-message dependencies and frozen recording configuration with the RK3576 release. Do not introduce a recorder service or change Jetson recording.

## Capabilities

### New Capabilities

- `rk3576-session-topic-recording`: fixed application-topic coverage, centralized ownership, source-time selection, recording-only sampling, bounded session admission, retained-state snapshots, storage limits, and evidence-based validation.

### Modified Capabilities

- `rk3576-ros-video`: delegate RK3576 bag recording to the centralized session recorder, expand topic coverage, allow sampled published debug images, and retain paired observations and failure isolation.
- `rk3576-continuous-hdmi-stream`: preserve preview and tracking session boundaries while replacing the prohibition on recording published debug images.
- `production-binary-deployment`: include the recording catalog configuration, explicit topic-recorder CPU policy, and required non-Python message libraries without adding a recorder process. Install the standard CPU 2 policy on fresh installation and upgrade while preserving selected custom-policy bytes.
- `rk3576-thread-affinity`: bind both topic-recorder threads through a dedicated optional role, preserve legacy and inherited-mode behavior, and confirm each new thread's actual CPU mask before work.

## Impact

- Proposed implementation: `src/recording/`, `src/bag_session.h`, `src/platform/cpu_affinity.*`, `src/drone_tracker_node.*`, `src/drone_tracker_main.cpp`, recording/affinity tests, `CMakeLists.txt`, and `package.xml`.
- Configuration and release integration: RK3576 parameter templates, launch/configuration loading, `deployment/cpu-affinity.yaml`, `deployment/cpu-policy.py`, `deployment/package_core.py`, installation/preflight validation, release verification, and their owning guides. Existing `need_save`, `save_path`, and independent `recording.*` meanings remain intact.
- RK3576 builds need the real `quadrotor_msgs` source package even when the optional OSD feedback adapter is disabled, because the catalog includes `ControlCommand` and `LowLevelFeedback`. Existing model and hardware-runtime dependencies remain unchanged.
- Sibling repositories provide interface evidence only. This change does not modify their code, message definitions, publication rates, or submodule revisions.
- Validate with deterministic policy tests, real ROS transport and bag readback, lifecycle/fault tests, and identified production-board comparisons. Host or emulated checks do not establish production performance.
- Implementation, release construction and hardware evidence are tracked in
  [tasks.md](tasks.md) and [validation.md](validation.md). Deployment remains a
  separately authorized operation.

# Proposal

## Why

An operating session currently saves only a small part of the messages exchanged by the bridge, tracker, and interceptor, leaving gaps when investigating flight behavior. Recording their known application topics through one component, with explicit sampling for frequent or large messages, will provide broader evidence while controlling storage work.

For example, today an ON/OFF session can contain camera orientation and target observations but omit the control commands and mission state that explain the response. After this change, the same session will retain those topics, preserve their source timestamps where available, and report exactly which samples were intentionally omitted or unexpectedly lost.

## What Changes

- Introduce one RK3576 `RecordingManager` inside the existing tracker process. It owns the complete recording catalog, recording subscriptions, timestamp rules, sampling, session admission, status, and the existing asynchronous `BagSession` writer. Remove the distributed RK3576 recording paths rather than retaining separate old and new recorders.
- Freeze the application-topic catalog at startup. Cover the known published and consumed interfaces of `apm_bridge`, `drone_interceptor`, and `drone_tracker`, including optional radar inputs. Resolve configured topic names and remappings before subscriptions are created. Do not discover unrelated domain topics or rescan during recording.
- Keep recording session by session under `need_save` and ON/OFF. Service preview does not open a bag. EOF, round-ending failures, and normal service shutdown close an active session. A sudden power loss has no guaranteed final-message retention interval.
- Keep one bag writer thread. Give recording subscriptions a dedicated ROS callback executor, which runs their callbacks separately from tracker control. Preserve direct, paired bbox/LOS submission and authoritative trigger forwarding as narrow integration interfaces.
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
- `production-binary-deployment`: include the recording catalog configuration and required non-Python message libraries without adding a recorder process.

## Impact

- Proposed implementation: `src/recording/`, `src/bag_session.h`, `src/drone_tracker_node.*`, `src/drone_tracker_main.cpp`, recording tests, `CMakeLists.txt`, and `package.xml`.
- Configuration and release integration: RK3576 parameter templates, launch/configuration loading, `deployment/package_core.py`, release verification, and their owning guides. Existing `need_save`, `save_path`, and independent `recording.*` meanings remain intact.
- RK3576 builds need the real `quadrotor_msgs` source package even when the optional OSD feedback adapter is disabled, because the catalog includes `ControlCommand` and `LowLevelFeedback`. Existing model and hardware-runtime dependencies remain unchanged.
- Sibling repositories provide interface evidence only. This change does not modify their code, message definitions, publication rates, or submodule revisions.
- Validate with deterministic policy tests, real ROS transport and bag readback, lifecycle/fault tests, and identified production-board comparisons. Host or emulated checks do not establish production performance.
- This delivery contains planning artifacts only. Implementation, release construction, deployment, and hardware acceptance remain unchecked tasks.

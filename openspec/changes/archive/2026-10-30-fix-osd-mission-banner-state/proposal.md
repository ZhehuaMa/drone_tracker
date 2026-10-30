# Proposal

## Why

The HDMI display can continue showing `ON MISSION` after interception control has ended, even when the flight mode already reads `ACRO` or the aircraft is disarmed. The current display remembers a start trigger and clears it only when the tracker publishes a changed target-tracking state, so a target that remains tracked can keep the banner visible indefinitely.

## What Changes

- Replace the trigger-driven mission banner with confirmation from the existing `/fpv/mission_state` and `/mavros/state` topics. A topic is a named ROS message channel. The first topic reports whether the interceptor considers a mission active; the second reports flight-controller connection, arming, and OFFBOARD status.
- Show `ON MISSION` only after receiving an active-mission event and fresh, connected, armed flight status with `guided=true`. In this deployment, `apm_bridge` sets `guided=true` when the flight controller reports OFFBOARD. A `MISSION` trigger alone will no longer show the banner.
- Clear the banner on mission completion, confirmed OFFBOARD exit, disarming, flight-status loss, or the existing `OFF` and `DISMISS` cancellation events. `DISMISS` continues to leave target tracking running.
- Keep mission display independent of target-tracking state changes. Preserve target-box styling, camera preview, tracking output messages, and existing arming and warning displays.
- Treat mission status as an event remembered inside the tracker, without a time-based expiry. The existing interceptor does not publish periodically. Apply the existing `osd.telemetry_timeout_ms` freshness rule only to flight status for this decision; its default is 2000 ms.
- Invalidate a cancelled or confirmed-ended display attempt so later flight-mode recovery or repeated `true` messages cannot resurrect it. Conservatively require an observed mission `false` followed by a later `true` to establish another attempt after invalidation.
- Redraw mission changes without waiting for another frame or tracking-state publication. Reject obsolete overlays before hardware submission without invalidating valid video packets.
- Record the accepted limitations: a restarted tracker cannot reconstruct a missed mission-start event; interceptor death cannot be identified from these topics while the bridge continues reporting valid armed OFFBOARD status.

Example: today, the interceptor publishes `false`, the bridge returns the aircraft to `ACRO`, and HDMI may retain `ON MISSION` while the target remains `Tracking=2`. After this change, receipt of that `false` removes the banner within 500 ms on a healthy display and restores `TRK TRACKING`, without stopping tracking or preview.

## Capabilities

### New Capabilities

None. This change revises existing display and telemetry behavior.

### Modified Capabilities

- `osd-presentation`: replace trigger/tracker-state banner lifetime with a confirmed mission-display state machine; define cancellation, redraw, and diagnostic behavior.
- `osd-ros-telemetry`: consume existing mission events, retain the flight `guided` field, and distinguish event memory from periodic telemetry freshness.
- `rk3576-hdmi-osd`: reconcile the visible HUD examples that currently allow `ON MISSION` with disarming or a start trigger alone.

## Impact

- Implementation is confined to the `drone_tracker` repository. Expected areas are `src/drone_tracker_node.*`, `src/osd/presentation.h`, `src/osd/ros_telemetry.*`, `src/osd/presenter.*`, a small portable mission-display policy under `src/osd/`, and the corresponding CMake and test files.
- Update `docs/RK3576_HDMI_OSD.md` and relevant deployment guidance to describe the display rule and restart limitations.
- Existing specifications and presenter tests explicitly require the old behavior. Their mission-specific requirements and assertions must change together; unrelated requirements remain applicable.
- Add only a tracker subscription to the existing Boolean mission topic. Keep current topic names, message types, publisher behavior, and flight-state subscription compatibility. No new ROS interfaces, dependencies, heartbeat, or freshness parameter are required.
- Do not modify `drone_interceptor`, `apm_bridge`, `quadrotor_msgs`, `rk_hdmi_osd`, or the `nickluo` root repository. Do not change interception algorithms, collision detection, flight commands, or ROS recording in this change.
- The unfinished replay and diagnostic changes have different scope. The completed RSSI change overlaps HUD requirements; preserve its RSSI behavior when reconciling mission examples.
- This proposal authorizes planning only. Implementation, builds, board acceptance, and deployment are separate work; this artifact does not claim they have occurred.

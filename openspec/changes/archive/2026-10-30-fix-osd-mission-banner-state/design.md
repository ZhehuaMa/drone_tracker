# Design

## Context

HDMI can assert that interception is running after control has ended. The intended outcome is a banner that reflects a received interceptor mission start and confirmed OFFBOARD control, and that clears promptly when either authorization ends. See `proposal.md` for the observed failure and scope.

OSD is the on-screen display over the camera video. Its HUD is the set of status text and graphics. Relevant current behavior is:

1. `src/drone_tracker_node.cpp` handles `MISSION` by calling `showMission()` on the presenter that manages screen updates.
2. `src/osd/presenter.cpp` remembers `mission_active_` until `trackerStatePublished()` clears it. The node calls that method only when the local target-tracking state changes.
3. `src/osd/ros_telemetry.cpp` already subscribes to `/mavros/state`. Its `FlightState` value retains connection, arming, and mode text, but discards `guided`.
4. The telemetry adapter sends a snapshot to the presenter every 100 ms. A snapshot is a copy of the latest measurements, not a history of every received event.
5. The presenter uses `version_` to authorize display work and reject outdated tracking packets. Changing that same version for mission text could unintentionally reject otherwise valid video packets.

The existing interface contracts provide two different kinds of information:

| Input | Existing producer | Meaning and availability |
|---|---|---|
| `/fpv/mission_state`, `std_msgs/msg/Bool` | `drone_interceptor` | `true` at mission start and `false` when control stops; no periodic repetition and no mission identifier |
| `/mavros/state`, `mavros_msgs/msg/State` | `apm_bridge` | Repeated flight-controller status; `guided` is set from the actual OFFBOARD flag |
| `/fpv/tracker_trigger`, `std_msgs/msg/Header` | Existing trigger producers, including `apm_bridge` | Start/stop requests; `OFF` and `DISMISS` can cancel the display immediately |

The current specifications deliberately preserve the old banner lifetime. This change therefore updates their behavior contract as well as implementation tasks. Existing Chinese requirement and scenario titles are retained as matching identifiers in the English delta specifications. Where a legacy title describes the old behavior, the revised WHEN/THEN text defines the intentional replacement; the title is not an additional assertion that the old behavior remains.

## Goals / Non-Goals

**Goals:**

- Make one portable display policy own mission eligibility, independent of target state and screen-rendering backend.
- Preserve every received mission end or flight-authorization loss, including transitions shorter than the 100 ms telemetry-summary interval.
- Keep callback work bounded and avoid waiting for inference, disk activity, or HDMI from a ROS callback.
- Give a healthy display a 500 ms budget to remove invalid mission text, including when video processing is stalled.
- Keep all implementation and validation changes inside `drone_tracker`.

**Non-Goals:**

- Changing interception control, collision detection, bridge commands, ROS recording, camera calibration, or the reference `rk_hdmi_osd` repository.
- Adding a heartbeat, changing an external publisher's QoS, or introducing a new topic/message. QoS means the ROS delivery and retention policy.
- Reconstructing a missed mission start, identifying interceptor death while fresh armed OFFBOARD reports continue, or inferring global ordering between independent topics.
- Adding a visible pending-mission label or changing existing target-box styling, trigger hints, warning priority, or camera/recording lifecycle.

## Decisions

### 1. Remember mission events, but expire periodic flight status

**Choice:** Add a reliable, volatile subscription with depth 10 to the existing mission Boolean. Volatile means earlier events are not replayed to a newly connected subscriber. Preserve compatible delivery on the existing flight subscription. Retain `guided` in the internal flight value. Use `connected && armed && guided` with the original flight receipt time to determine confirmation. Use the existing `osd.telemetry_timeout_ms`, default 2000 ms, without adding parameters.

**Why:** Mission silence is normal because its publisher emits transitions only. Repeated flight reports can establish whether control authority is still current. `guided` handles both `OFFBOARD` and `OFFBOARD|ANGLE` without parsing display text.

**Rejected alternatives:** Expiring mission `true` after two seconds would hide healthy long missions. Requiring a heartbeat would change `drone_interceptor`. Exact mode-string matching would reject a valid existing OFFBOARD variant. Replaying a retained mission `true` would require a different publisher contract and could revive obsolete state.

**Consequence:** One received `true` can authorize a long mission while flight reports remain valid. A tracker that missed that event remains unconfirmed. A monotonic clock, which measures elapsed time independently of calendar-clock changes, determines flight freshness. Snapshot forwarding must preserve the receipt timestamp and must not renew it.

### 2. Use an explicit local display state machine

**Choice:** Add a small ROS-independent policy under `src/osd/mission_state.*`. A display attempt means one locally observed mission start and its eventual termination. It is not an identifier supplied by the interceptor. The policy states are:

| State | Meaning | Banner |
|---|---|---|
| Unknown | No mission event has been received since startup | Hidden |
| Inactive | Mission `false` has been observed | Hidden |
| Pending | A permitted mission `true` is awaiting flight confirmation | Hidden; existing tracking text continues |
| Active | That attempt has fresh connected armed OFFBOARD confirmation | `ON MISSION` |
| Invalidated | A pending/active attempt was cancelled or lost authorization | Hidden until mission `false` establishes an end boundary |

Transition rules:

| Current state / input | Result |
|---|---|
| Any state receives mission `false` | Inactive; remember the end boundary |
| Unknown or Inactive receives mission `true` | Pending, then evaluate the currently available flight report |
| Pending receives fresh connected armed `guided=true` | Active |
| Pending receives fresh connected armed `guided=false` | Stay Pending; OFFBOARD entry is not yet confirmed |
| Active receives fresh `guided=false` | Invalidated |
| Pending or Active receives a fresh disarmed/disconnected report | Invalidated |
| Pending or Active loses freshness of a previously received flight report | Invalidated |
| Pending has never received any flight report | Stay Pending, without asserting confirmation |
| Pending/Active receives `OFF` or `DISMISS` | Invalidated immediately |
| Unknown receives `OFF` or `DISMISS` | Invalidated, blocking a delayed mission `true` |
| Inactive receives repeated `OFF` or `DISMISS` | Stay Inactive; do not erase the already observed end boundary |
| Invalidated receives mission `true`, mode recovery, or `MISSION` | Stay Invalidated |
| Pending or Active receives repeated mission `true` | No new attempt and no renewed flight timestamp |
| Any state receives `ON`, `MISSION`, or an unrecognized trigger | No mission authorization is created |

A mission `true` evaluated against an already available disarmed/disconnected or expired flight report is invalidated. Initial absence of any flight report is different: it permits Pending until the first report arrives. Repeated cancellation is idempotent, meaning that applying it again does not create another attempt or consume an already observed end boundary.

**Why:** A remembered Boolean alone can resurrect a completed display when mode returns to OFFBOARD. Pending also avoids treating the initial ACRO reports during entry as a confirmed OFFBOARD loss.

**Rejected alternatives:** A trigger-driven flag repeats the current defect. Showing a banner from OFFBOARD alone confuses interceptor missions with other OFFBOARD users. Requiring `Tracking=2` would couple the banner to target state and misrepresent a continuing interceptor mission after visual target loss.

**Consequence:** After invalidation, a received `false` followed by a later `true` is required for another attempt. If that end event is missed, the banner can remain hidden during a subsequent real mission. This conservative re-arm rule is intentional because the existing Boolean has no timestamp or mission identifier. Trigger cancellation and mission status have no guaranteed global ordering. Process their locally received order without claiming to reconstruct the producer's original order.

Example with an armed aircraft initially in ACRO:

1. A `MISSION` trigger arrives. Tracking text remains visible.
2. Mission `true` arrives. The policy enters Pending.
3. Initial fresh armed ACRO reports leave it Pending.
4. A fresh armed report with `guided=true` makes it Active and shows `ON MISSION`.
5. Mission `false` returns it to Inactive. The display restores `TRK TRACKING` even if the target and flight mode have not yet changed.

### 3. Deliver events directly instead of polling the latest Boolean

**Choice:** Extend `OsdTelemetryAdapter` with distinct mission-event and flight-event sinks. A sink is a callback that hands a received value to the display policy. The mission subscription calls its sink for each received Boolean. The flight subscription updates ordinary telemetry and hands the same received flight value, including `guided` and its unchanged receipt time, to the flight-event sink. Dispatch the sinks outside the telemetry-store lock.

Wire both sinks from `src/drone_tracker_node.*` into explicit presenter methods. Handle `OFF` and `DISMISS` through a separate cancellation method before any tracking cleanup. Retain the current `ON` and `OFF` tracker behavior. The policy and display revision are updated under the presenter's existing mutex. Remove `showMission()` as trigger authorization and remove the mission-clearing effect of `trackerStatePublished()`.

The 10 Hz telemetry snapshot continues to update ordinary flight, battery, arming, and warning fields. It must not replay mission events or revive mission policy from a cached flight snapshot. Mission state is stored in a distinct type; do not overload the existing Boolean telemetry receiver, which currently means failsafe.

**Why:** A 30 ms mission could start and end between summaries. Similarly, `guided=false` followed by `true` must invalidate the old attempt even if the next summary contains only `true`.

**Rejected alternatives:** Adding only `Measurement<bool>` to the periodic snapshot loses intermediate transitions. Queuing an unbounded history adds unnecessary resource and ordering complexity. A second flight subscription duplicates the existing source and complicates receipt ordering.

**Consequence:** Each callback performs a bounded value update rather than pixel work. ROS delivery, mutex acquisition, and screen submission have distinct times. Tests must distinguish them. The policy serializes local events, but does not add cross-topic ordering guarantees or alter external control.

### 4. Separate overlay revisions from tracking packet versions

**Choice:** Keep the existing tracking packet `control_version` and stream/round acceptance rules. Add a separate revision for mission-overlay changes. A revision is a counter used to detect that prepared display work became obsolete. Wake the presenter on mission transitions. Evaluate flight expiry during the presenter's existing bounded wake interval, even if no new packet arrives.

Before handing an overlay to the display device, authorize both its video/control version and mission-overlay revision. If mission state changed before this handoff, discard the prepared overlay and retry with the current policy over retained video. A handoff already authorized for hardware is allowed to finish safely; the next update must clear the banner within the display budget. For a revision-only rejection, retain the last successfully displayed video and allow the next valid packet to proceed. Do not change video acceptance merely because mission state changed.

**Why:** The current presenter can render mission state while tracking or disk cleanup is blocked. Its existing version also gates packet acceptance; using it for all mission updates would interfere with continuous preview and tracking packets.

**Rejected alternatives:** Waiting for the next frame repeats the stale-overlay failure. Bumping the tracking version for every mission update rejects valid packets. Cancelling hardware work after authorization violates existing buffer ownership rules.

**Consequence:** Both `legacy` and `cached-overlay` receive the same resolved mission Boolean in `PresentationState`. Their layout still draws `ON MISSION` in its current location. Target rendering remains paired with its original video. Expiry removal is due by the last flight receipt plus timeout plus 500 ms, including detection and rendering delay.

### 5. Make transitions and actual display results separately observable

**Choice:** Log policy transitions with an overlay revision, local attempt counter, and monotonic event time. The attempt counter is diagnostic only and must not be presented as an interceptor-provided mission identifier. Use reasons such as `mission_true`, `offboard_confirmed`, `mission_false`, `offboard_lost`, `disarmed`, `disconnected`, `flight_status_stale`, `trigger_off`, and `trigger_dismiss`. Log successful banner changes with the displayed stream/round/source sequence and overlay revision. Keep `target_label=""` and avoid per-frame or unchanged-flight-report log repetition.

**Why:** The field investigation needed to distinguish a control event from the screen actually updating.

**Rejected alternative:** Logging only callback receipt could falsely suggest the hardware displayed the new state.

**Consequence:** Event-to-display latency can be checked with no dependence on a changed tracker state. Preserve existing warning priority, preview hints, and RSSI behavior while updating mission-specific logs.

## Risks / Trade-offs

- **A tracker restart misses mission `true`.** Mitigation: start Unknown and keep the banner hidden. Document that another actual start event is required; do not infer active interception from mode alone.
- **Interceptor death leaves a remembered `true` while flight status stays valid.** Mitigation: document this detection gap explicitly. Continue clearing on flight exit, disarm, disconnection, or expiry. Solving the remaining gap requires a separate publisher/interface change outside this proposal.
- **An end event or an OFFBOARD transition is never delivered.** Mitigation: compatible reliable subscriptions reduce transport loss while connected, but cannot recover missed history. Existing messages do not carry enough information for complete reconstruction.
- **Cancellation arrives after a mission end on another topic.** Mitigation: keep Inactive idempotent so a repeated cancel does not erase an already received `false`. For genuinely ambiguous late starts, prefer a hidden banner over unsupported mission confirmation.
- **Snapshot coalescing hides short transitions.** Mitigation: direct event delivery, deterministic rapid start/end tests, and ROS integration tests with events inside a 100 ms summary interval.
- **HUD changes disturb video throughput or resources.** Mitigation: a separate overlay revision, existing bounded packet ownership, resource checks, and tests that retain valid current-round packets during cancellation.
- **Overlapping completed RSSI change still contains the old mission examples.** Mitigation: update only this change's delta specs now. During later synchronization, reconcile the shared target-decoration requirement and preserve the RSSI replacement. Do not blindly apply the older mission examples after this change.

## Migration Plan

1. Implement only the tasks in this tracker change and update mission-specific documentation. Do not change other repositories or the existing topic publishers.
2. Run the portable mission-policy and presenter tests, then ROS telemetry integration and the configured tracker test/build entry point. Keep build outputs outside source. Report native, ARM64 cross-build, and emulated checks separately.
3. In a separately authorized board session, validate both HDMI rendering paths with controlled status messages and the configured camera/models. Do not start flight control or publish device control commands to reproduce display-only cases. Use an isolated ROS domain for synthetic message sources so they cannot reach production consumers. A ROS domain is a numeric message-discovery group used here to separate test nodes from production nodes.
4. Record real HDMI event-to-display latency, continued preview, and unchanged target styling. Builds or fake display backends do not establish physical-screen acceptance.
5. Before an authorized deployment, retain the old tracker release and configuration. Roll back by restoring that tracker release and its matching configuration; there is no external message migration to reverse. Normal deployment and desktop/device ownership rules remain applicable.
6. Synchronize or archive the planning change only after implementation verification, resolving the RSSI overlap without undoing either feature. This planning turn does not perform implementation, board checks, deployment, or specification synchronization.

# Tasks

This checklist is for implementation after a separate apply request. All source, tests, and documentation belong to `drone_tracker`. Do not add a heartbeat, change external publishers, edit other repositories, or update the parent repository's submodule pointer. Preserve the current topic names and message types. Board checks require a separately authorized session; no deployment is included here.

## 1. Portable mission-display policy

- [x] 1.1 Preserve flight authorization in `src/osd/presentation.h` and the related value-conversion interfaces by retaining `guided` alongside connection, arming, mode, and receipt time; completion means an `OFFBOARD|ANGLE` report is represented without exact mode-string matching or altered existing field meanings.
- [x] 1.2 Establish the Unknown/Inactive/Pending/Active/Invalidated transitions in new `src/osd/mission_state.h` and `.cpp`, including the observed false-then-true re-arm boundary; completion means the policy accepts mission, flight, cancellation, and monotonic-time inputs without ROS, image, or device dependencies.
- [x] 1.3 Cover the policy contract in `src/osd/tests/mission_state_test.cpp` and register it in `src/osd/CMakeLists.txt`; checks must include both input arrival orders, ACRO during entry, confirmed OFFBOARD loss, disarm, disconnect, flight expiry, never-received flight status, repeated events, false-before-cancel idempotence, cancellation before a delayed true, and later re-authorization.
- [x] 1.4 Document mission confirmation and event memory in `docs/RK3576_HDMI_OSD.md`; completion means the document explains why mission `true` does not expire, why flight status does, and why tracking-state changes no longer govern the banner.

## 2. ROS inputs and tracker orchestration

- [x] 2.1 Deliver every received status transition through `src/osd/ros_telemetry.h` and `.cpp` using distinct mission and flight callbacks; completion means a reliable volatile depth-10 mission subscription exists, flight callbacks retain their original receipt time, callback sinks execute outside the telemetry-store lock, and the existing failsafe Boolean path remains distinct.
- [x] 2.2 Connect display cancellation and confirmation in `src/drone_tracker_node.h` and `.cpp`; completion means the existing trigger callback cancels on `OFF`/`DISMISS` before cleanup, `MISSION` no longer calls `showMission()` to authorize text, and tracker-state publication no longer clears mission display.
- [x] 2.3 Cover receipt-time preservation and no-heartbeat behavior in `src/osd/tests/telemetry_test.cpp`; checks must prove that repeating a snapshot does not renew flight data, `guided` survives conversion, mission silence beyond 2000 ms does not end a healthy mission, and calendar-time changes do not alter expiry.
- [x] 2.4 Cover actual ROS delivery in `src/tests/ros_osd_telemetry_test.cpp`; use an isolated test domain and wait for matched subscriptions before single mission events. Checks must include mission false while Tracking=2 remains unchanged, true/false/true within one 100 ms summary interval, a received brief OFFBOARD exit, remapped topics, startup without a received true, and continued existing telemetry behavior with low-level feedback enabled and disabled.
- [x] 2.5 Document startup and missed-event limits in `docs/RK3576_HDMI_OSD.md` and `deployment/README.md`; completion means both explain hidden mission state after a missed start, conservative re-authorization after invalidation, and the inability to identify interceptor death during continuing valid OFFBOARD without changing external interfaces.

## 3. Presenter updates and visible behavior

- [x] 3.1 Make mission events and flight expiry update the screen through `src/osd/presenter.h` and `.cpp`; completion means policy transitions wake the renderer, expiry is checked without a new video packet, and no callback waits for inference, recording cleanup, or HDMI work.
- [x] 3.2 Reject obsolete mission overlays before hardware handoff in `src/osd/presenter.*` using a separate overlay revision; completion means mission-only changes do not increment the tracking packet version or reject valid current-round packets, and already-authorized device work retains its existing safe-completion rules.
- [x] 3.3 Replace the old mission-lifetime assertions in `src/osd/tests/presenter_test.cpp`; both fake rendering paths must cover clearing while Tracking=2 stays unchanged, no-frame cancellation, blocked inference, pending entry, `OFF`/`DISMISS`, repeated status, stale-flight expiry, cancellation before authorization, safe in-flight completion, and continued valid video acceptance. Event clearing must meet 500 ms and default expiry must meet 2500 ms after the last flight report under healthy test conditions.
- [x] 3.4 Verify visible consistency through `src/osd/tests/layout_test.cpp` and `hud_style_test.cpp`; fixtures must represent confirmed missions, both real drawing paths must clear mission text on end/disarm without residue, and paired target styling, preview behavior, arming text, RSSI, and trigger-hint placement must retain their existing rules.
- [x] 3.5 Add transition diagnostics in `src/osd/presenter.cpp` and their assertions in `presenter_test.cpp`; completion means pending/confirmed/ended reasons, overlay revisions, local attempt counters, monotonic times, and successful display updates can be correlated while `target_label=""` is preserved and unchanged status messages do not spam logs.
- [x] 3.6 Document the event-to-display timing and log interpretation in `docs/RK3576_HDMI_OSD.md`; completion means readers can distinguish received status, policy transition, and successful HDMI submission and can identify the 500 ms event and timeout-plus-500 ms expiry limits.

## 4. Integration and acceptance evidence

- [x] 4.1 Verify the complete implementation with the repository entry point `./build.sh --test --artifacts-dir /tmp/tracker-mission-banner-validation`; retain discovered test counts, results, compiler/image identity, and target architecture. Completion requires all applicable registered policy, presenter, telemetry, ROS integration, and existing regression checks to pass; a cross-build alone is not board acceptance.
- [x] 4.2 In a separately authorized board session, verify actual HDMI output on the HZ-RK3576_WuKong Board for `cached-overlay` and `legacy` using isolated status publishers and the configured camera/models. Record mission-end-with-continuing-target, entry failure, disarm, `DISMISS`, `OFF`, received OFFBOARD loss, stale flight status, and restart-with-missed-start behavior. Completion requires real screen evidence and measured clearing bounds while preview continues; do not publish production control commands or change other modules to exercise the display.
- [x] 4.3 Record verification boundaries in this change's `validation.md`; completion means software checks, ARM64 build/emulation, and physical-screen acceptance are reported separately, no missing board check is represented as passing, and the diff is confirmed to contain only this repository's intended implementation and documentation changes.

## Workflow follow-up

- Review the implementation against all three delta specifications before requesting synchronization or archive.
- Reconcile the shared HUD requirement with `replace-osd-temperature-and-battery-gauge-with-rssi` before later specification synchronization; preserve RSSI behavior and do not restore its obsolete mission examples.
- Deploy only through a separate authorized release action, retaining the previous tracker release and matching configuration for rollback.

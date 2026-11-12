# Tasks

All items are implementation or verification work for a later apply request. None is complete merely because this planning change exists. The design has no unresolved choice that changes what is built.

## 1. Baseline, catalog, and typed dependencies

- [ ] 1.1 Preserve a reproducible comparison baseline before replacing recording code: retain the previous recorder binary or a reproducible build from `e17c43e`, its configuration, message-package identities, and checksums in a retained evidence location. Completion requires a report identifying which inputs can actually be reproduced; do not rely solely on disposable caches.
- [ ] 1.2 Establish the fixed typed catalog in `src/recording/topic_catalog.*` and its RK3576 build target. Completion requires all 31 default entries and the optional bench-status entry to match the specification's exact types, default names, timestamp rules, and direct/subscription source kind.
- [ ] 1.3 Make the real target-architecture `quadrotor_msgs` dependency available to RK3576 recording through `CMakeLists.txt`, `package.xml`, and the owning build input workflow. Completion requires recorder linkage with the OSD feedback adapter disabled and no new manager linkage in the Jetson target.
- [ ] 1.4 Add catalog/structure contract tests under `src/recording/tests/`. Completion requires generated message types to compile, all catalog entries to be covered, no duplicate defaults, and ControlCommand/LowLevelFeedback vectors and timestamps to be exercised without copied message definitions.
- [ ] 1.5 Document the catalog, real shared-message build dependency, and source-interface baseline in the owning recording/build guides. Completion requires the documented catalog and dependency switches to match the compiled RK3576 target and selected sibling configurations.

## 2. Frozen settings, timestamps, and recording policies

- [ ] 2.1 Expose startup recording settings through `src/recording/` and the RK3576 parameter templates. Completion requires `bag.topics.<id>.name`, ordinary maximum frequencies, the shared observation-pair frequency, image byte limits, optional bench profile, and storage limits to resolve to one immutable effective configuration.
- [ ] 2.2 Reject conflicting names and invalid or runtime recording changes through manager validation and the node parameter guard. Completion requires invalid recording-only configuration to report the recorder unavailable while otherwise healthy tracking remains usable, and runtime `bag.*` updates to return a restart-required failure.
- [ ] 2.3 Preserve source time through typed timestamp extractors in `src/recording/`. Completion requires H/S/R selection, receipt-time capture, unchanged payloads, reason-specific invalid-stamp fallback, and no substitution of expected_execution_time or writer time.
- [ ] 2.4 Bound selected sample rates through a monotonic sampler and image token bucket in `src/recording/`. Completion requires immediate first selection, minimum intervals, reset on a new session, protected-event exemptions, joint pair selection, OFF-pair bypass, and distinct byte-limit/oversized outcomes.
- [ ] 2.5 Add deterministic timestamp and policy tests in `src/recording/tests/`. Completion requires irregular arrivals, sparse bursts, clock rollback, zero/malformed headers, old valid source stamps, full command defaults, short protected transitions, and image burst accounting to pass with an injected clock.
- [ ] 2.6 Document policy configuration in `parameters/README.md` and the recording guide. Completion requires a concrete 200-to-100-Hz example, the meaning of zero Hz, the fact that reception still costs resources, and the distinction between source, receipt, and monotonic time.

## 3. Bounded writer admission and reporting

- [ ] 3.1 Correct retained-work accounting in `src/bag_session.h` and recording payload helpers. Completion requires pending and in-flight work to share the 256-slot/64-MiB bounds and dynamic strings, arrays, layouts, and nested detection data to be charged conservatively until release.
- [ ] 3.2 Preserve control capacity and observation pairing in bag admission. Completion requires per-entry limits, the image limits, the 16-slot/1-MiB reserve, two-slot all-or-neither pairs, per-entry ordering, and the bounded control/ordinary scheduling policy to operate within the global limits.
- [ ] 3.3 Expose reconcilable session counters and terminal results from the recording component. Completion requires separate policy, byte, oversized, queue, timestamp, accepted, written, and accepted-but-unwritten outcomes; snapshots and direct submissions must be identifiable.
- [ ] 3.4 Add controlled slow-writer and admission fault tests in `src/recording/tests/` and adapt relevant `src/tests/ros_video_test.cpp` cases. Completion requires an in-flight large payload, one remaining pair slot, saturated image allowance, reserved OFF admission, fairness, and accepted-but-unwritten accounting to pass without disk waits in producers.
- [ ] 3.5 Document queue ownership and measured-memory boundaries in the recording guide. Completion requires clear separation of retained payload, ROS middleware buffers, serialization scratch space, and whole-process memory; no fixed-object-size estimate may be presented as a total memory guarantee.

## 4. Central manager and callback ownership

- [ ] 4.1 Consolidate all ordinary recording subscriptions in `src/recording/recording_manager.*` and remove the distributed RK3576 recording paths from `src/drone_tracker_node.*`. Completion requires moved record-only odometry/body-IMU subscriptions, a manager-owned recording gimbal subscription, unchanged algorithm pose-history handling, and one source for each recorded topic.
- [ ] 4.2 Isolate recording callbacks in a non-auto-attached callback group and dedicated executor, integrated with `src/drone_tracker_main.cpp` and existing CPU roles. Completion requires recording callbacks to run only on that executor and recorder ROS creation/destruction to respect the existing transport boundary.
- [ ] 4.3 Preserve initial retained state through the bounded gimbal-mode cache in the manager. Completion requires a known idle mode to be offered once at session opening, live changes to remain distinct, and oversized cache input to be reported without unbounded memory retention.
- [ ] 4.4 Add manager transport and ownership tests. Completion requires all subscription-backed catalog types to round-trip, name overrides/remappings to work, a publisher started after ON to connect without discovery, incompatible delivery to be visible when supported, and idle/disabled resource behavior to match the specification.
- [ ] 4.5 Document the unified ownership model in the recording guide and update applicable architecture/agent guidance. Completion requires the docs to identify the manager, sole writer, recording executor, direct pair/trigger interfaces, and the absence of a second recorder service.

## 5. Session and observation integration

- [ ] 5.1 Centralize authoritative trigger forwarding and deferred ON handling in `src/drone_tracker_node.*` and the manager. Completion requires an original ON occurrence to enter the session once, repeated ON to remain in the same session, identical Header contents to remain distinct real occurrences, and programmatic starts to avoid fabricated ROS messages.
- [ ] 5.2 Route constructed bbox/LOS observations through `submitObservationPair()` and remove duplicate recording subscriptions for those topics. Completion requires original common timestamps, all-or-neither sampling/admission, preservation across a ROS publication failure, and an unsampled final OFF pair.
- [ ] 5.3 Fence active sessions with immutable run/round handles in the manager. Completion requires OFF/EOF/failure sealing, no old-handle admission into a replacement session, and no MISSION/DISMISS recording-lifecycle change beyond the tracker's existing behavior.
- [ ] 5.4 Finalize recorder lifetime safely through the node/service shutdown paths. Completion requires executor cancellation/join, safe subscription destruction, writer drain/close, preserved failure reasons, and no background callback touching a destroyed manager or waiting on an already stopped executor.
- [ ] 5.5 Add real ROS/session and bag-readback tests under `src/recording/tests/` and `src/tests/ros_video_test.cpp`. Completion requires no startup-preview bag, two distinct ON/OFF bags, correct boundary events, initial mode snapshots, rapid ON/OFF races, blocked inference at OFF, EOF, failures, late handles, and documented post-seal exclusion.
- [ ] 5.6 Document operating intervals and stop behavior in the recording and deployment guides. Completion requires ON/OFF, MISSION, DISMISS, programmatic start, normal exit, disabled recording, and abrupt power loss to have unambiguous examples without claiming a post-OFF tail or power-loss guarantee.

## 6. Split files, storage protection, and session summaries

- [ ] 6.1 Continue large sessions across SQLite bag files through `BagSession` storage options. Completion requires a configurable 256-MiB default splitting target, one session identity across files, unchanged payload/time, and no automatic deletion of existing evidence.
- [ ] 6.2 Stop bag admission on observed storage limits through writer-side storage accounting. Completion requires the 5-GiB owned-bag budget, 5-GiB free-space threshold, actual incomplete/summary-file usage, exclusive new session directories, safe handling of symlinks, checks at open/split and active intervals, and independent tracking/video/diagnostic behavior.
- [ ] 6.3 Write versioned bounded `session.json` summaries in the newly owned session directory. Completion requires effective configuration/source fingerprints, boundaries, catalog activity, snapshot origins, terminal reasons, queue peaks, and reconcilable counters, with summary failures visible in existing logs when storage is unavailable.
- [ ] 6.4 Add split/storage/failure integration tests. Completion requires multi-file readback, preserved existing evidence, deterministic quota/free-space injection, open/write/close/summary faults, interrupted pair writing, and storage-limited versus failed classification; tests must not fill a production disk.
- [ ] 6.5 Document storage policy and recovery limits in `deployment/README.md` and the recording guide. Completion requires a worked file-split-versus-total-quota example, separate bag/video budgets on their actual filesystem, evidence retention, and a clear statement that free space is observed rather than atomically reserved across writers.

## 7. Release configuration and dependency closure

- [ ] 7.1 Include the full typed recorder runtime in `deployment/package_core.py` and release verification. Completion requires target-architecture non-Python generated message libraries and package-discovery resources, including quadrotor_msgs, to load without assuming a sibling checkout exists on the production board.
- [ ] 7.2 Preserve selected recording settings and matched rollback through the RK3576 templates, packaging, and deployment inputs. Completion requires exact configuration fingerprints, correct bridge/interceptor topic names, unchanged independent `recording.*` meanings, and previous-release configuration restoration without modifying sibling repositories.
- [ ] 7.3 Add package/configuration regression tests under `deployment/tests/`. Completion requires missing/wrong-architecture library rejection, OSD-feedback-disabled recorder packaging, exact selected policy installation, no added recorder service, and matched rollback behavior.
- [ ] 7.4 Update build, parameter, deployment, and architecture documentation affected by the new dependency and bag-image policy. Completion requires owning guides and applicable AGENTS guidance to agree that images are eligible only when published during ON, while ordinary production publication remains disabled.

## 8. Combined verification and production evidence

- [ ] 8.1 Validate the complete target build and integration suite using the repository's supported RK3576 build workflow, including `./build.sh --test` in the matching shared-message environment. Completion requires preserved commands, source/configuration identities, exit statuses, and separate identification of native ROS, ARM64 compilation, and emulated results; Jetson hardware checks remain explicitly unrun when unavailable.
- [ ] 8.2 Validate lifecycle/resource stability on one identified production HZ-RK3576_WuKong Board. Completion requires at least 20 ON/OFF rounds, at least 60 seconds of combined active workload, bounded resource behavior, and final OFF preview, with each individual run at most 180 seconds.
- [ ] 8.3 Compare disabled recording, the preserved previous recorder, and expanded recording under matched production workload identities. Completion requires actual consumed/displayed rates, latency distributions, input rates/sizes, CPU, memory, disk throughput, queue peaks, all policy/gap counters, shutdown results, and absolute/relative differences; normal declared-load runs must have no unexplained growth or unexpected recorder loss.
- [ ] 8.4 Measure the separate image-enabled workload on that production board without conflating it with the normal image-disabled profile. Completion requires retained source headers, image-rate/byte-limit evidence, no queue monopolization, and successful complete-bag readback within a run of at most 180 seconds.
- [ ] 8.5 Deliver the final requirement-to-evidence review in a retained report. Completion requires every changed capability and task to map to actual test/evidence results, unrun checks to remain open, performance assumptions to be distinguished from measurements, and `openspec validate centralize-rk3576-session-topic-recording --strict` to pass before implementation is presented for review.

## Workflow follow-up

- After implementation review, use the normal OpenSpec synchronization/archive workflow to reconcile durable specifications. Preserve the exact existing requirement/scenario titles in modified deltas so archival matches the original blocks.
- Deployment and any rollback operation require their normal explicit user instruction; producing this proposal does not authorize either operation.

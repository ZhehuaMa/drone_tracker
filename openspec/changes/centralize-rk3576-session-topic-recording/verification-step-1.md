# Verification report — step 1, 2026-11-12

Change: `centralize-rk3576-session-topic-recording`.
Repository: `/home/mzh/.codex/worktrees/ac88/drone_tracker`.
Source at review: `c47088f01c991d14f3e0b40980956cd3d3736847` plus the uncommitted
implementation documented in `validation.md`.

This is the initial report before this step's fixes. Applying a fix does not
change these verification counts. A fresh agent must verify the revised result.

| Dimension | Result |
|---|---|
| Completeness | 41/41 tracked tasks checked; implementation found for all 23 ADDED/MODIFIED requirements |
| Correctness | All 23 requirements mapped to code; one session-boundary divergence and three scenario-test gaps grouped into the warnings below; 48 scenarios inspected |
| Coherence | Twelve design decisions inspected; the control-event race diverges from the session-opening order in decision 4; code patterns inspected |

There are no REMOVED or RENAMED requirements. No schema check was skipped.
The historical native, emulated and board results remain limited to their
recorded program, configuration and workload identities. They are not evidence
that every concurrent interleaving is correct.

## CRITICAL: 0

No missing requirement implementation or unchecked task was found in the initial
input set.

## WARNING: 3

1. **Authoritative triggers can cross the session-opening control boundary.**
   In `src/drone_tracker_node.cpp:1009`, the ROS trigger callback calls
   `observeTrigger()` before `run()` or `stop()` acquires `control_mutex_`.
   While the continuous worker holds that mutex immediately before opening a
   session, OFF can reach the manager first. It clears the pending ON because
   there is no active session yet. The worker then opens the established round,
   and `stop()` seals it without either original trigger. ON can similarly be
   forwarded to an ending session before the control path requests the next
   round. This violates **Session boundaries retain authoritative control
   events**, the modified ROS bag requirement, and design decision 4.
   Serialize ON/OFF forwarding with the same control decision that establishes
   or stops the round. Preserve receipt time captured at callback entry. Add a
   gated real-ROS regression that races OFF with session opening and reads back
   the original ON/OFF messages. The same boundary defect includes inactive ON
   being queued even when finite-input control ignores it during cleanup, and
   pending ON surviving a failed recording/start operation. Those stale commands
   must not be replayed into a later programmatic start. Start and camera failures
   also need their actual boundary reasons rather than a generic round end.

2. **Paired sampling and timestamp-fallback counters lack manager-level tests.**
   `src/recording/tests/catalog_policy_test.cpp:50` tests the policy helpers,
   while `src/recording/tests/transport_test.cpp:63` uses an unsampled pair and
   valid source headers only. The **Sampled observation pair followed by OFF**
   and **Unset header** scenarios are not exercised through manager admission
   and bag readback. Add deterministic tests that verify joint omission, final
   OFF bypass, preserved zero/malformed payload stamps, and reason-specific
   fallback counters. Include image byte-limited versus oversized accounting
   in the same controlled policy coverage.

3. **Storage protection after opening lacks scenario coverage.**
   `src/recording/tests/writer_test.cpp:141` injects quota/free-space exhaustion
   at open only. It does not exercise **Space consumed by another writer**
   during an active session or the promised non-following accounting of old
   symlinks, incomplete files and summaries. Add controlled active/split checks
   and filesystem-accounting fixtures without filling the board disk.

## SUGGESTION: 1

The new recording core contains dense multi-operation lines and lines longer
than the C++ norms' 120-character limit. Consider expanding the touched control
and writer paths into focused statements when maintaining them. This is a
readability suggestion and does not independently require another loop step.

## Requirement mapping

| Requirement group | Implementation/evidence inspected |
|---|---|
| Fixed catalog; names and delivery | `topic_catalog.*`, `config.*`, native transport/configuration tests |
| Sessions; authoritative boundaries | `recording_manager.*`, node run/stop/worker paths; warning 1 |
| Source time; invalid fallback | `policy.h`, typed callbacks, transport/readback helpers; warning 2 |
| Sampling; protected events; image policy | `policy.h`, manager submission, catalog defaults; warning 2 |
| Retained mode | Bounded manager cache, idle retained publisher and oversized-mode tests |
| Bounded admission; control reserve | `bag_session.h`, in-flight/one-slot/reserve/fairness tests |
| Completeness; inspectable coverage | Session counters and JSON summary, complete board bag readback |
| Splitting; storage protection | Guarded SQLite writer and fault/split tests; warning 3 |
| Normal/abrupt shutdown | Node shutdown, manager/executor lifetime, writer errors; documented durability limits |
| Production acceptance | Matched retained profiles, model/configuration identities, 20 rounds, separate image workload |
| Release configuration/dependencies/services | Actual package, target-loader result, architecture tests and existing matched rollback checks |
| Modified ROS-video/preview boundaries | Real node lifecycle tests, image metadata checks, independent camera video; warning 1 |

## Initial assessment

No critical issues were found. Three warnings need fixes and fresh verification.
The code-style suggestion can remain after the required fixes.

## Fix and validation outcome

All three warnings were fixed and validated in this step. The revised executable
SHA-256 is `8fbad8fc5ccce24b20877ea0a68769200febf99ada89f0aafd6875f5b4c2ad37`.
The original counts above remain **0 CRITICAL, 3 WARNING and 1 SUGGESTION**;
they are not replaced with inferred zero counts after applying fixes. A fresh
verification agent must report its own result.

| Finding | Fix and evidence |
|---|---|
| W1 | `src/drone_tracker_node.cpp:181` and `:280` serialize original triggers with their control decision; one run-state snapshot governs finite-input start admission. `src/recording/recording_manager.cpp:120` limits deferral and `:134` cancels abandoned starts. `src/drone_tracker_node.cpp:378` freezes shutdown intent before callback teardown. Real-ROS regressions gate opening/OFF, finite cleanup/ignored ON, preparation/shutdown, retained/canceled deferred ON, failed open/thread start and actual failure boundaries. |
| W2 | `src/recording/tests/transport_test.cpp:176` exercises actual manager policy/admission, final pair bypass, reason-specific fallback/payload readback, protected events and distinct image budget outcomes. |
| W3 | `src/recording/tests/writer_test.cpp:159` and `:205` exercise active/split storage limits, accepted-but-unwritten work, incomplete/summary accounting and symlink exclusion. |

Changed implementation/test files in this step are `src/drone_tracker_node.cpp`,
`src/drone_tracker_node.h`, `src/recording/recording_manager.cpp`,
`src/recording/recording_manager.h`, `src/tests/ros_video_test.cpp`,
`src/recording/tests/transport_test.cpp` and `src/recording/tests/writer_test.cpp`.
The owning session guide, tasks, validation addendum and this report were updated.
The loop parent owns `verify-fix-summary.md`; this agent did not edit it.

The final supported RK3576 build/test command exited 0: **69/69 tests passed in
195.82 s**, plus the native OS memory-limit check. Its random localhost ROS domain
was 126. Native ROS transport/storage, ARM64 compilation and emulation are
identified separately in `validation.md`. Final package construction and actual
board loader checks exited 0. `clang-tidy`/`clangd`/`clang-format` remain unavailable;
the compile database and compiler warning checks are retained. Jetson hardware
execution remains explicitly unrun.

The revised board data cover matched previous/disabled/expanded/image profiles
and twenty lifecycle rounds (62.643 active / 80.685 total seconds). All 20 bags
passed forensic readback, with 38,941 accepted/written messages and no unexpected
recorder losses. Active file descriptors/threads stayed at 54/21, memory remained
bounded, and 126 new preview frames followed final OFF. All services were restored,
no temporary processes remain, and production Runtime/configuration checksums
are unchanged. No commit, archive, persistent deployment or rollback occurred.

**The stricter lifecycle harness did not pass:** it returned 1 because one actual
diagnostic synchronization backlog reached 612.5 ms. Settings and the raw failure
were preserved. Final diagnostic inspection exited 0 with valid checksums,
normal completion, and zero cumulative gaps for all five producers. Later lag
recovered; timing-sampling overflow was zero. The current change requires matched
diagnostics, measured performance, bounded resources, readback and no unexpected
recorder loss, but does not specify that stricter 500 ms guard. Its explicit
recording/lifecycle criteria are therefore supported without claiming the
independent diagnostic latency target passed. The full requirement-based
assessment and failed camera/socket-path attempts remain in `validation.md`
and the retained evidence. The next fresh verifier must independently assess it.

Evidence root:
`/home/mzh/nickluo/validation-retained/session-recording-20261112/verify-fix-step-1/final/`.
The formatting suggestion remains. No actionable recording fix is blocked.

### W1 state/event review before final build

| Mode/state and event | Required recording action |
|---|---|
| Continuous idle/starting, accepted ON | Retain each original occurrence once for the established round |
| Continuous active, repeated ON | Record an event in the existing session; create no new writer |
| Continuous closing, accepted ON | Defer the original command for the next round requested by control |
| Finite-input idle, ON | Cache the initiating occurrence, open once, then consume it once |
| Finite-input starting/active, ON | Offer to an existing session; do not create a future deferred start |
| Finite-input closing/EOF cleanup, ON | Preserve the existing ignored-control behavior; do not cache for a later start |
| Worker holds control while opening, OFF | Finish opening with pending ON, then offer OFF/final pair before sealing |
| OFF cancels a start before it is established | Clear pending ON; create no bag for the canceled request |
| Repeated or post-seal OFF | Create no extra invalid pair or new recording session |
| Active MISSION/DISMISS | Record received events without independently changing recording lifecycle |
| Post-seal MISSION/DISMISS or ordinary callback | Exclude it from the old interval and every future session |
| Finite-input EOF | Seal with `eof`; do not fabricate a trigger |
| Tracking failure | Seal with the actual tracking/round failure boundary and cancel any control-canceled pending start |
| Camera failure | Seal with `camera_failure`; clear canceled pending starts and preserve service-failure behavior |
| Round-start failure | Clear pending start; seal any opened bag with `round_start_failure` |
| Recording-open failure | Report recording unavailable, clear its unused pending ON, and keep healthy tracking |
| Programmatic start | Supply no fabricated ROS Header and reuse no ignored or failed-start command |
| Service exit | Cancel pending starts, seal, stop/join callbacks, drain/close and destroy safely |

The finite-input path uses one run-state snapshot for both forwarding and the
start decision because its worker may finish cleanup without the control lock.
Inactive ON deferral is explicitly allowed only when the control decision can
establish a round. Jetson retains its original state read and recorder paths.
Programmatic OFF cancels a deferred real ON just as ROS OFF cancels its control
request. Service exit freezes `running`, `desired_on_` and `ready_request_`
before cancellation/sealing and executor shutdown. A separate gated regression
releases an already prepared worker while shutdown is paused at that boundary
and checks that it creates no bag.

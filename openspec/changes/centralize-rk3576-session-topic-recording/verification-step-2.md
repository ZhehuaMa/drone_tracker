# Verification report — step 2, 2026-11-12

Change: `centralize-rk3576-session-topic-recording`.
Repository: `/home/mzh/.codex/worktrees/ac88/drone_tracker`.
Source base: `c47088f01c991d14f3e0b40980956cd3d3736847` plus the retained
uncommitted implementation.
Executable SHA-256:
`8fbad8fc5ccce24b20877ea0a68769200febf99ada89f0aafd6875f5b4c2ad37`.

This is a fresh verification after step 1's fixes. The user authorized loading
`openspec-verify-change` from the root checkout's `.agents/skills/` directory;
implementation and planning remain in this worktree. The selected change is
explicit. A different selection would use `openspec-verify-change <other>`.

## Summary

| Dimension | Result |
|---|---|
| Completeness | 41/41 tasks complete; implementation and evidence found for all 23 ADDED/MODIFIED requirements |
| Correctness | All 23 requirements and 48 scenarios reviewed against code, tests and exact-source evidence; no divergence found |
| Coherence | All twelve design decisions followed; code patterns reviewed with one readability suggestion |

There are no REMOVED or RENAMED requirements. No applicable OpenSpec check was
skipped. This assessment uses source inspection and retained runtime results;
it does not claim that every test or board run was re-executed during this step.

## CRITICAL: 0

No incomplete task, missing requirement implementation or unresolved required
acceptance condition was found.

## WARNING: 0

The previous boundary-ordering and policy/storage coverage warnings are fixed.
The active normal-service-exit path and diagnostic-lag evidence were assessed
independently, as detailed below. Neither establishes an additional divergence
from this change's requirements.

## SUGGESTION: 1

Expand dense, overlong C++ statements when maintaining the recording core.
Examples include `src/recording/recording_manager.cpp:88` and
`src/bag_session.h:269`. Focused statements and shorter expressions would make
the control and accounting logic easier to review. This is a readability
suggestion; it does not require another verify-fix iteration.

## Requirement and scenario mapping

The scenario count in each row covers every scenario beneath that requirement.
The four delta-spec files are the authority for the exact titles and conditions.

| Requirement | Scenarios | Implementation and evidence reviewed |
|---|---:|---|
| Fixed startup catalog covers the application interfaces | 3 | `topic_catalog.*`; fixed subscription construction in `recording_manager.*`; catalog count/type/source tests and late-radar transport/readback |
| Topic names and delivery settings are explicit | 3 | `config.cpp`; node parameter guard; remapping, override, duplicate-name, invalid-setting and runtime-rejection tests |
| Recording uses explicit operating sessions | 3 | Node run/stop/continuous worker; enabled-manager construction; preview, two-session, disabled and MISSION/DISMISS cases |
| Session boundaries retain authoritative control events | 2 | Locked trigger decisions, deferred-command cancellation and immutable handles; gated opening/OFF, blocked inference and stale-handle tests |
| Source timestamps and payloads are preserved | 2 | `policy.h`, typed callbacks and direct trigger path; real generated command/vector roundtrip and bag timestamp checks |
| Invalid source-time fallback is visible | 1 | Typed zero/negative/malformed fallback; manager-level reason counters and unchanged-payload readback |
| Frequency sampling affects only recording | 2 | Monotonic minimum-interval sampler; first-sample, irregular/sparse/rollback and extremely low-rate cases; algorithm subscriptions remain separate |
| Commands and observation boundaries are protected from sampling | 2 | Catalog protection and configuration rejection; brief Boolean transitions; joint pair sampling and unsampled final pair tests |
| Published debug images have independent recording limits | 2 | Image sampler/token bucket and lazy topic creation; admitted/byte-limited/oversized/queue cases, image-off metadata and separate image board profile |
| Retained mode is available at session start | 1 | Bounded idle cache and snapshot-before-active-handle opening order; retained AIM and oversized-cache transport cases |
| Admission is bounded and keeps observation pairs intact | 2 | `BagSession::admit()` and release accounting; one-slot pair rejection and large in-flight payload/slow-writer tests |
| Topic limits preserve room for control evidence | 1 | Per-entry/global limits and reserved control class; saturated image, OFF pair, full 256-slot and fair-scheduling tests |
| Recording completeness distinguishes deliberate omissions | 2 | Separate policy/loss/accepted/written counters and terminal classification; policy and queue-fault tests plus independent SQLite reconciliation |
| Session coverage and errors remain inspectable | 1 | Fixed-catalog session JSON, source/configuration identities, direct/snapshot/coverage counters and quiet-entry reporting |
| File splitting does not delete evidence | 1 | Guarded SQLite splitting and exclusive session ownership; multi-file payload/time readback and retained old-evidence fixtures |
| Storage protection ends recording independently | 1 | Writer-side open/active/split checks; controlled free-space/quota faults and incomplete/summary/symlink accounting tests |
| Normal shutdown and abrupt loss have different guarantees | 1 | Writer drain/close, original failure reasons and unwritten totals; open/write/close/summary/partial-write faults and documented power-loss limits |
| Production acceptance measures the combined workload | 1 | Exact-source matched previous/disabled/expanded profiles; actual input rates, latency/CPU/memory/disk/counters, readback and separate image workload |
| ROS bag 记录与关闭互斥 | 9 | Node/manager/writer ownership and paired direct submission; EOF versus callbacks, recording failures, queue limits, image policies, disabled and complete-bag tests |
| 记录与 ROS 调试图像保持业务边界 | 3 | Continuous preview and image-publication gates; rapid ON/OFF, independent video and image/no-image cases |
| Session recording configuration travels with the release | 2 | Exact selected camera/policy content and fingerprints; package tests, actual archive and matched configuration restoration in `rollback.sh` |
| Typed recording dependencies are available without the OSD adapter | 2 | RK3576-only manager linkage; real shared-message build input; architecture rejection tests, packaged libraries/resources and target loader evidence |
| Expanded recording adds no recorder service | 1 | Existing tracker-owned manager/writer; unchanged independent video/diagnostic services, package/service checks and preserved system Runtime |

The sum is 23 changed requirements and 48 scenarios. Catalog-only exclusions
are supported by the fixed compiled registry and its structure tests, rather
than assuming runtime graph discovery. No schema artifact is missing or skipped.

## Active service-exit and generation boundaries

Normal active shutdown was checked separately from the prepared-round test:

1. `src/drone_tracker_node.cpp:382` acquires the existing control lock and clears
   tracking intent. This excludes another regular publication or new round.
2. `notifyObservationOff()` is called at line 387, before sealing at line 389.
   It acts only for an established observation lifecycle and prevents repetition.
3. `notifyObservationOff()` at line 1628 builds a final invalid pair with one
   common, advancing reference time. Its call to `publishObservations()` does
   not require `running` to remain true.
4. `publishObservations()` admits the pair through the manager at line 1598
   before its separate ROS publications. The manager passes the control flag
   to joint sampling/admission, so this final pair bypasses frequency sampling.
5. Sealing then fences the handle. Executor cancellation/join and writer close
   follow; late old-round work cannot resolve itself into a replacement handle.

The shared final-pair path is exercised with `running=false` by the existing
OFF regressions and complete bag readback. The preparation/shutdown gate proves
the additional no-new-bag case; it is not the sole basis for active-shutdown
correctness. No separate active-shutdown runtime run is claimed in this step.

The state/event review also checks the single finite-input state snapshot,
ignored ON during writer cleanup, accepted and canceled deferred ON, failed
recording/round starts, programmatic calls, EOF, tracking/camera failures and
MISSION/DISMISS. Original Header occurrences remain distinct. Programmatic
starts/stops do not fabricate ROS trigger messages. Jetson keeps its original
state read and writer path behind the platform guards.

## Independent source and evidence checks

The following checks ran during this step using the conda base interpreter and
ordinary read-only commands:

- Compared all **1,920** entries in retained `SOURCE-FILES.json` against current
  worktree bytes: **zero mismatches**. The parent-owned summary is explicitly
  excluded by that record because it changes between iterations.
- Checked the installed executable SHA-256: exact match to `8fbad8fc...` above.
- Ran `sha256sum -c --quiet EVIDENCE.sha256` in the final evidence directory:
  **exit 0**, with no mismatches.
- Independently queried all SQLite files in the **20 lifecycle bags**, read-only.
  Every per-topic message count matches its session summary. All sessions are
  complete with `user_off`, and every summary identifies the same executable.
  Totals are **57,084 offered**, **38,941 accepted/written**, **18,143 policy
  skips**, and **zero** queue, middleware, oversized or accepted-but-unwritten
  losses.
- Ran strict OpenSpec validation and `git diff --check`: both **exit 0**.

The retained exact-source supported suite passed **69/69 in 195.82 seconds**,
plus the native memory-limit check, with random localhost ROS domain 126.
Actual target loader, matched profiles and lifecycle results are distinct from
native ROS checks and ARM64 compilation/emulation. No new ROS process or board
service change was needed for this verification.

Final evidence directory:
`/home/mzh/nickluo/validation-retained/session-recording-20261112/verify-fix-step-1/final/`.

## Diagnostic-lag assessment

The strict lifecycle harness **failed with exit 1**. It detected a real
612.5 ms diagnostic synchronization backlog above its 500 ms guard. This report
does not relabel that harness result as passing.

The actual production-acceptance requirement and design decision 12 require
matched diagnostic settings, measured performance/counters/readback, bounded
resources and no unexpected recorder loss. They do not specify a 500 ms
diagnostic synchronization limit. Independent inspection of the raw results,
assessment and checksum-verified evidence supports the distinction:

- The tracker exited 0 after 20 rounds and 62.643 active / 80.685 total seconds.
- All 20 bags are complete and independently reconcile with accepted writes.
- The diagnostic inspector reported a checksum-valid 15,617,040-byte prefix,
  732 commits, normal completion and zero cumulative gaps for all five producers.
- Later diagnostic synchronization age returned to 20–71 ms with no error/drop
  indication. Timing-array overflow counters are zero.
- Active file descriptors/threads stayed at 54/21. Per-round median resident
  memory was 228.30–232.95 MiB (first 230.45, last 229.78), with no monotonic
  growth. Final OFF preview produced 126 fresh frames.

Thus the explicit recording/lifecycle criteria are supported, while the separate
stricter diagnostic-latency target remains failed and visible. No diagnostic
setting, workload or source was changed to waive it. Socket-path and physical
camera timestamp failures remain retained, and camera fail-fast validation was
not weakened. This is an evidence-based scope distinction, not an assertion
that diagnostic latency or general camera reliability is guaranteed.

## Limits and assessment

Jetson hardware execution, target-acquisition quality, calibrated LOS accuracy,
power-loss durability, device-memory measurement and per-process disk attribution
remain unclaimed. The compile database and compiler checks are retained;
clang-tidy/clangd/clang-format were unavailable in the prior implementation
environment. These limits do not skip a change requirement or substitute host
results for board measurements.

No implementation fix was needed during this fresh step. Only this report and
its retained check record were added. The parent owns the loop summary.

**No critical issues or warnings. One readability suggestion remains. Ready
for archive with that noted improvement; no archival or deployment was performed.**

# Verification report — CPU 2 step 1, 2026-11-13

Change: `centralize-rk3576-session-topic-recording`.
Worktree: `/home/mzh/.codex/worktrees/ac88/drone_tracker`.
Source base: `c47088f01c991d14f3e0b40980956cd3d3736847` plus uncommitted changes.
The initially reviewed executable SHA-256 is
`bc999a725aa6fd2050ba4111c9b10eaa59359b2721adf09cc547607fa8e57216`.

This is a fresh independent review of all current planning artifacts, code,
tests, and retained evidence. It uses the root checkout's explicitly selected
`openspec-verify-change` skill. The CLI selected the repository-local
`spec-driven` schema, with no standalone store and no linked editing scope.
All proposal, design, task, and five delta-spec files returned by
`contextFiles` were read. A different selection uses
`openspec-verify-change <other>`.

## Original verification scorecard

| Dimension | Result before fixes |
|---|---|
| Completeness | 52/52 tasks checked on entry; implementation found for all 30 ADDED/MODIFIED requirements. One release-validation gap requires reopening tasks. |
| Correctness | All 30 requirements and 69 scenarios reviewed; one supported packaging path can deliver a binary that cannot honor the selected topic role. |
| Coherence | All twelve design decisions reviewed; the selected candidate follows automatic CPU placement. One release-boundary warning and one existing readability suggestion. |

There are no REMOVED or RENAMED requirements. All six applicable OpenSpec checks
ran: Task Completion, Spec Coverage, Requirement Implementation Mapping,
Scenario Coverage, Design Adherence, and Code Pattern Consistency.
**Skipped OpenSpec checks: 0.** Retained physical-board evidence was inspected;
this scorecard does not claim that those runs were repeated during verification.
The later fix-validation addendum has its own source/binary boundary.

## CRITICAL: 0

No missing requirement implementation or incomplete task was listed by the CLI
at verification entry. The checked release tasks nevertheless need correction
for the concrete warning below; their original checkmarks are not proof of
complete release validation.

## WARNING: 1

**W1 — A selected pre-role binary can be released with the CPU 2 policy.**
`deployment/package_core.py:163` originally checked the node's diagnostic
protocol version only. Protocol 2 also identifies the preserved expanded
recorder that predates `TopicRecording`. The supported `--install-root` option
accepts that older installation. Its runtime ignores the additional policy
field and retains the recording-executor CPU 6 / writer CPU 7 split. The new
package and installer can therefore succeed while the default installation's
promised topic CPU 2 placement is absent.

The current packager was run with the preserved prior expanded installation:

```bash
./deployment/package.sh \
  /home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/verify-fix/step1/old-node-package \
  --install-root /home/mzh/nickluo/validation-retained/session-recording-20261112/expanded-build/backends/amd64/install/drone_tracker \
  --message-install /home/mzh/nickluo/validation-retained/session-recording-20261112/expanded-build/backends/amd64/install/quadrotor_msgs \
  --model-root /home/mzh/nickluo/validation-retained/session-recording-20261112/models \
  --without-models
```

It succeeded and emitted `drone-tracker-20261113T091103Z-8p03ec1w.tar.gz`.
Its `SOURCE.json` identifies the old node as
`8fbad8fc5ccce24b20877ea0a68769200febf99ada89f0aafd6875f5b4c2ad37` and the new
policy as `95604fdb423e9dfd7d8d4b427a2a1f39fb112825875b208e09e9dab6fad5cfbc`.
Recording these hashes does not reject their incompatible behavior. No board
deployment was needed to reproduce the release-construction defect.

Recommendation: verify that a selected node implements the explicit topic role
at both package construction and installation preflight. Reject an older node
before publishing a release or changing services/files. Preserve deliberate
inherited-mode and recorder-disabled compatibility. Use a probe that exits
safely on older nodes rather than trying an unknown option that might start
ROS or camera initialization. This enforces the existing standard-installation
requirement and design decision 11; it does not change diagnostic protocol 2.

The controller was notified of **0 CRITICAL / 1 WARNING / 1 SUGGESTION before
implementation changes**. Tasks 10.2, 10.3, and 11.1–11.3 were reopened and CLI
apply instructions refreshed before fixes. Their validation is tracked below.

## SUGGESTION: 1

**S1 — Expand dense recording-core statements during future maintenance.**
Examples remain in `src/recording/recording_manager.cpp:173` and
`src/bag_session.h:116`. Splitting lock acquisition, decisions, and updates onto
focused lines would ease review of ownership and counters. This is the prior
readability suggestion, not a correctness defect or a required rewrite.

## Requirement and scenario mapping

Every scenario under each row was checked. The exact titles and normative
wording remain in the delta specifications.

| Requirement | Scenarios | Implementation and evidence |
|---|---:|---|
| Fixed startup catalog covers the application interfaces | 3 | `topic_catalog.*`, fixed manager subscription construction, catalog tests and late-publisher transport test |
| Topic names and delivery settings are explicit | 3 | `config.cpp`, node parameter guard; override/remapping, conflicts, invalid settings and runtime rejection tests |
| Recording uses explicit operating sessions | 4 | Node lifecycle and manager enablement; preview, disabled, two-session, mission-event tests; new camera/replay automatic-role evidence |
| Session boundaries retain authoritative control events | 2 | Locked node forwarding, canceled deferred ON, immutable session handles; gated OFF/opening, blocked inference and stale-handle tests |
| Source timestamps and payloads are preserved | 2 | Typed callbacks, `policy.h`, direct trigger/pair submission; generated-type and full readback tests |
| Invalid source-time fallback is visible | 1 | Zero/negative/malformed fallback counters and original payload readback |
| Frequency sampling affects only recording | 2 | Monotonic sampler, irregular/sparse/rollback tests; separate algorithm subscriptions |
| Commands and observation boundaries are protected from sampling | 2 | Catalog protection, validation, brief transitions and joint sampling/final OFF bypass tests |
| Published debug images have independent recording limits | 2 | Image token bucket, lazy topic creation, byte/oversized/queue tests and historical separate image-enabled board run |
| Retained mode is available at session start | 1 | Bounded idle cache, snapshot before active-handle publication, retained AIM and oversized-cache tests |
| Admission is bounded and keeps observation pairs intact | 2 | Pending plus in-flight accounting, atomic pair admission, one-slot rejection and slow-writer tests |
| Topic limits preserve room for control evidence | 1 | Per-entry/global bounds, 240 ordinary plus 16 reserved slots, saturated-image and final OFF tests |
| Recording completeness distinguishes deliberate omissions | 2 | Separate counters and result phases; deterministic faults and independent SQLite reconciliation |
| Session coverage and errors remain inspectable | 1 | Bounded `session.json`, fixed catalog, quiet entries, snapshot/direct counts and supported delivery-error reporting |
| File splitting does not delete evidence | 1 | Exclusive session directory, guarded SQLite splitting, complete multi-file readback and preserved-evidence tests |
| Storage protection ends recording independently | 1 | Writer open/active/split checks; injected quota/free-space faults and old incomplete/summary/symlink accounting |
| Normal shutdown and abrupt loss have different guarantees | 1 | Drain/close, original failure reasons, unwritten counts; open/write/close/summary/partial-pair fault tests and documented limits |
| Production acceptance measures the combined workload | 1 | Preserved matched previous/disabled/expanded comparison and twenty-round lifecycle; revised role candidate's separate camera/replay evidence |
| ROS bag 记录与关闭互斥 | 9 | Node/manager/writer boundaries; EOF/callback races, failure isolation, capacity/pair admission, image-off and complete readback tests |
| 记录与 ROS 调试图像保持业务边界 | 3 | Preview/image gates, ON/OFF lifecycle, independent camera video and image metadata tests |
| Session recording configuration travels with the release | 2 | Exact camera bytes/fingerprints, selected configuration in actual packages and isolated matched rollback |
| Typed recording dependencies are available without the OSD adapter | 2 | RK3576-only manager, real message generation, ARM64 library/resource closure, architecture rejection and board loader evidence |
| Expanded recording adds no recorder service | 1 | Existing tracker-owned recorder, unchanged independent services and protected system Runtime |
| Standard installation selects CPU 2 for topic recording | 2 | Default policy, fresh/upgrade byte-preservation fixtures, new candidate native logs/masks; W1 for stale selected binaries |
| New role-pinned recorder packages name the topic CPU explicitly | 4 | Shared Python validator at both boundaries; custom CPU, inherited mode, disabled and missing-role tests |
| CPU policy validation distinguishes packaging from target availability | 2 | Offline validation, mocked absent topology, actual target preflight and taskset rejection before mutation |
| 收集器绑定不改动其他执行单元 | 3 | Unchanged diagnostic launcher/transport boundary, default allowed set, CPU-role tests, existing diagnostic tests and retained board role masks |
| Topic recording has a dedicated optional CPU role | 4 | `cpu_affinity.*`, strict explicit-field parsing, separate legacy fallbacks, inherited masks and parser tests |
| Topic threads confirm binding before work and on recreation | 3 | `cpu_affinity::start`, manager and writer paths; actual readiness/failure/recreated-writer tests and board native binding logs |
| Topic placement preserves other affinity responsibilities | 2 | Transport-bound resource construction/destruction, camera recording role, disabled-worker tests and board CPU 7 controller/encoder masks |

Total: **30 ADDED/MODIFIED requirements; 69 scenarios**. All twelve design
decisions were assessed against code and evidence. The manager remains the
sole owner of ordinary typed recording subscriptions. Direct trigger and paired
observations remain narrow node interfaces. The one writer owns storage calls;
no production code scans or retargets thread IDs. Camera-video recording and
general ROS resource creation retain their separate roles. Jetson branches
remain outside the RK3576 manager linkage.

## Independent checks of the initially reviewed candidate

Evidence root **V**:
`/home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/verify-fix/step1/`.
Original apply root **E**:
`/home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/apply/`.

- Compared all 1,927 files in `E/source-manifest.json` against current worktree
  bytes before fixes. Only expected `tasks.md`, `validation.md`, and the
  controller-owned `verify-fix-summary.md` differed. No implementation mismatch.
- Verified the original package archive/node hashes and every `SHA256SUMS`
  entry; checksum command exit 0. The source and standard policy match that
  candidate. `V/independent-evidence.json` retains results and complete summaries.
- Opened every SQLite file in all four new camera/replay bags read-only and ran
  `PRAGMA integrity_check`; each returned `ok`. Per-topic SQLite counts and
  session summaries are retained independently for reconciliation.
- Camera process: 43.794 seconds, two rounds, executor and both new writers
  logged requested/actual CPU 2. Replay: 43.760 seconds with the same three
  independent binding confirmations. No thread-ID adjustment was used.
- The disabled replay process ran 23.785 seconds with no topic bindings or bags.
  Retained endpoint and worker records corroborate the disabled resource path.
- Read the before-work native logs and read-only observations, camera controller
  and encoder CPU 7 masks, other managed roles, complete bag counters and
  restoration records. All phase and restoration statuses are 0.
- The harness AST contains one affinity-setting call, targeting its publisher's
  own process on CPUs 0–3. It contains no tracker-thread retargeting call.
- Independently ran the CPU-policy script, six package tests and six install
  tests before fixes; all passed. The stale-binary case was absent from those
  tests and is not excused by their passing results.

These checks ran on the x86_64 development host. System Python was used for
PyYAML-dependent tests because conda base lacks PyYAML. General evidence
processing used `/home/mzh/miniconda3/bin/python`. The original supported build
used the default amd64 cross compiler, produced ARM64 binaries, passed 71/71
CTests and the separate native memory-limit test. Native ROS integration and
emulated ARM64 execution are distinct from the identified production board.

The physical evidence identifies `HZ-RK3576_WuKong Board`, hostname `hzhy`,
Ubuntu 22.04.5 ARM64, kernel `6.1.141-rt36`, GCC 11.4.0 and 3.8 GiB RAM.
It records actual V4L2/media topology and SC233HGS `/dev/video22` NV12
1920×1200/60 Hz capture. The replay is the hashed `fixed-balloon.avi` input.
All four approved RKNN model hashes remain in `E/board/models-inputs.sha256`.
System Runtime remains 2.3.2, hash
`d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`, driver 0.9.8.

## Explicit limits and checks not performed

The following are not verified by new execution in this step. They are not
silently scored as passing tests:

1. Initial verification did not repeat the full supported build or physical
   workload. It inspected retained exact-source results. Fix validation below
   identifies any fresh runs separately.
2. Production installation and rollback were not run. Those paths were exercised
   only in isolated filesystem/service fixtures; the physical run was staged.
3. Jetson hardware/SDK execution is unavailable on this x86_64 host.
4. `clang-tidy`, `clangd`, and `clang-format` were unavailable when checked.
   Compiler warnings and the retained compile database are available; no lint
   or language-server pass is claimed.
5. Target acquisition quality, annotated accuracy, calibrated LOS/exposure
   latency, device memory, power-loss durability, and per-process disk I/O are
   unmeasured. Shared-device traffic is not recorder-only throughput.
6. The historical lifecycle harness failed its stricter 500 ms diagnostic-lag
   gate. That failure remains visible. Its zero-gap close and readable bags do
   not convert the diagnostic-lag check to a pass. The current change does not
   specify that extra threshold.
7. Earlier eight-core/manual-affinity and recorder-disabled comparisons identify
   different binaries. They motivate the placement and cannot prove this
   candidate's automatic binding or zero overhead. The shorter fresh disabled
   case is a resource-path check, not a matched performance comparison.
8. The camera source timestamp age is uncalibrated. Native publication rates
   differ from the slower Python receiver's DDS delivery; neither proves
   physical exposure latency or tracking accuracy.

These limits do not omit any of the six advisory OpenSpec verification checks.
No required check was waived to remove W1. Existing production diagnostic
ENOSPC logs were preserved; no unrelated evidence was deleted.

## Original assessment

**No critical issues. One warning and one suggestion found.** W1 must be
addressed before treating the new default installation as verified. The report's
original counts remain **0 CRITICAL / 1 WARNING / 1 SUGGESTION** after fixes.
A new independent verification step must assess the resulting implementation;
fixes in this step do not reverify themselves.

## Fix-validation addendum

Implementation and validation results are recorded here after the W1 fix.
This section records apply work, not a fresh verification verdict.

The selected node now reports `topic_recording_affinity: 1` through the existing
safe diagnostic-version inspection exit when
`DRONE_TRACKER_RELEASE_CAPABILITIES=1` is requested. Ordinary inspection still
prints `2`. Compatible older nodes also exit safely on this known flag, but
their scalar `2` response fails the feature check. The query starts no ROS or
camera work. Diagnostic protocol version 2 was not changed.

`cpu-policy.py --node` enforces the feature only for recorder-enabled,
role-pinned releases. Both packaging and installer preflight invoke it in the
correct target-library environment. Installation performs it before filesystem
or service mutation. Explicit inherited-mode and disabled-recorder inputs
remain valid without the unused feature. The deployment guide explains the
rebuild and matching `--install-root` remedy.

Changed implementation files: `src/drone_tracker_main.cpp`,
`deployment/cpu-policy.py`, `deployment/package_core.py`, `deployment/deploy.sh`,
`deployment/tests/test_cpu_policy.py`, `deployment/tests/test_install_policy.py`,
`CMakeLists.txt`, and `deployment/README.md`. The planning changes are task
status updates and validation/report evidence. No spec was weakened or removed.

Fresh apply checks:

- Supported `./build.sh --test --artifacts-dir E/build --quadrotor-msgs
  /home/mzh/nickluo/quadrotor_msgs`: **exit 0**, **72/72** tests in **199.88 s**,
  plus separate native memory-limit **1/1**. The default amd64 cross backend
  produced ARM64 output; no backend switch occurred.
- Focused CPU-policy tests, **7** installation/rollback fixture tests, and **6**
  package tests passed. New cases reject old scalar protocol output and malformed
  capability responses, while preserving inherited/disabled compatibility.
- Repeating actual packaging against the pre-role `8fbad8fc...` installation
  now returns **1**, identifies `roles.topic_recording`, and recommends rebuilding
  with a matching `--install-root`. No archive is published by the failing run.
- Revised packaging and package checksum verification both return **0**.
  Target `deploy.sh --check` returns **0** and independently prints ordinary
  query `2` and feature query
  `{"diagnostic_protocol_version":2,"topic_recording_affinity":1}`.

| Revised candidate | Identity |
|---|---|
| Node SHA-256 | `de6c8d79dfa5ef81244c68845f2604c73e7dd4be0aeb0289565fce811b6d20b0` |
| Package | `V/packages/drone-tracker-20261113T091721Z-n6mr05j8.tar.gz` |
| Package SHA-256 | `717cdd728cfa178173878035c240cc2280640ca944c9c040445ea17964a010d5` |
| Standard policy SHA-256 | `95604fdb423e9dfd7d8d4b427a2a1f39fb112825875b208e09e9dab6fad5cfbc` |

`V/fixed-source-snapshot.tar.gz`, `fixed-source-manifest.json`, and
`fixed-source-diff.patch` retain the compiled implementation. Only task/report
closure edits follow that source snapshot. `V/build-evidence/` retains the
compile database and full test logs. `command-statuses.json`, package logs,
`board-command-statuses.json`, and `run_fix_board.py` retain commands and actual
exit statuses. Existing unrelated systemd unit warnings remain in target
preflight output; they are not modified by this fix.

Fresh physical-board validation used the same identified production board and
hashed camera/models/replay input, with a new staged root
`/var/lib/drone_tracker/tc2vf1`. The exact revised package policy was used. Each
enabled process logged one executor plus two independently recreated writers
with `role=topic_recording requested=2 actual=2` before work. Subsequent mask
observations were read-only. The camera controller and both encoder processes
remained on CPU 7; other managed roles retained their selected masks.

| Case | Rounds | Process seconds | Accepted = written | Policy skips | Peak slots / accounted bytes |
|---|---:|---:|---:|---:|---:|
| Camera, recorder enabled | 2 | 43.791 | 22,039 | 10,445 | 85 / 24,052 |
| Balloon replay, recorder enabled | 2 | 43.762 | 21,271 | 10,515 | 73 / 21,856 |
| Balloon replay, recorder disabled | 2 | 23.775 | 0 | 0 | 0 / 0 |

All four bags passed complete typed deserialization and separate read-only
SQLite integrity/count reconciliation. Accepted-but-unwritten, queue, oversized,
byte-limit and supported middleware-loss counts were zero. No image metadata
was present. Both diagnostic exports ended normally with zero gaps and complete
timing arrays. The disabled case had no dedicated topic workers, bags, or
recorder-only endpoints. Every tracker process stayed below 180 seconds.

`V/board-analysis.json` reports all acceptance checks true. The full raw logs,
bags, manifests, actual model/input identities, diagnostic exports, and
before/after system records are retained under `V/board/`. Target preflight,
all workload/readback/export phases, service restoration, protected-file
restoration, and current-link restoration exited 0. The independent
`post-restore-check.log` confirms all four services active, unchanged Runtime,
configuration and current executable hashes, no staged tracker process, and
an inactive restoration timer. No production installation or rollback ran.

Tasks 10.2, 10.3, and 11.1–11.3 were completed again against these actual
results. Strict OpenSpec validation and `git diff --check` passed after closure.
Historical diagnostic-lag failure and performance limits remain unchanged.
No actionable blocker remains from W1. The readability suggestion is retained.
**This is fix validation only; the controller must launch fresh verification.**

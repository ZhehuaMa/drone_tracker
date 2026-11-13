# Verification Report: centralize-rk3576-session-topic-recording

Independent CPU 2 verification, Step 2, 2026-11-13. This step reviewed the current artifacts, implementation and retained evidence. It made no implementation changes.

## Verdict and scorecard

**CRITICAL: 0. WARNING: 0. SUGGESTION: 1.**

| Dimension | Status |
|---|---|
| Completeness | Task Completion: 52/52 complete. Spec Coverage: all 30 ADDED/MODIFIED requirements found in implementation. No REMOVED or RENAMED requirements. |
| Correctness | Requirement Implementation Mapping: 30/30 assessed. Scenario Coverage: 69/69 assessed through code, tests and identified evidence. |
| Coherence | Design Adherence: all 12 decisions followed. Code Pattern Consistency: reviewed; one optional readability suggestion. |

All six applicable OpenSpec checks ran. **Skipped or partially verified OpenSpec checks: 0. Not-applicable OpenSpec checks: 0.** Additional checks not performed or properties not established are listed explicitly below. No task was reopened because no actionable critical issue or warning was found.

No critical issues or warnings. One suggestion to consider. Ready for archive with the noted optional improvement. This advisory verdict does not perform archival, specification synchronization, deployment or rollback.

## Inputs and independent method

The selected change is repository-local at `/home/mzh/.codex/worktrees/ac88/drone_tracker`; no store was selected. The verifier read the authorized repository-local `openspec-verify-change/SKILL.md`, current `AGENTS.md`, applicable local instructions and development norms. Fresh `openspec list`, `status` and `instructions apply` outputs are retained. The schema is `spec-driven`; the CLI reports `all_done`, with 52 complete tasks and none remaining.

Every current `contextFiles` artifact was read: proposal, design, tasks and the five concrete delta specifications. The implementation review covered catalog/configuration/policy helpers, manager and writer ownership, node lifecycle integration, affinity parsing and startup, build boundaries, packaging, installer, launcher and rollback behavior. Tests and retained raw data were checked independently; the earlier report's verdict was not used as evidence of correctness.

Evidence roots used in this report:

- **Step 2:** `/home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/verify-fix/step2/`. Contains this step's scripts, command results, identity audit and independent SQLite checks. SQLite is the database format used by these bags.
- **Current candidate:** the adjacent `step1/` directory. Contains the compiled source snapshot, package, 72-test suite and fresh production-board camera/replay data for `de6c8d79...`.
- **Original contract:** `/home/mzh/nickluo/validation-retained/session-recording-20261112/verify-fix-step-1/final/`. Contains the original full workload comparison, image run and 20-round lifecycle for `8fbad8fc...`. These results retain that historical executable identity.

## Source, package and environment identities

| Item | Independently checked identity |
|---|---|
| Git base | `c47088f01c991d14f3e0b40980956cd3d3736847`, plus retained uncommitted implementation |
| Current ARM64 node | `de6c8d79dfa5ef81244c68845f2604c73e7dd4be0aeb0289565fce811b6d20b0` |
| Archive | `drone-tracker-20261113T091721Z-n6mr05j8.tar.gz` |
| Archive SHA-256 | `717cdd728cfa178173878035c240cc2280640ca944c9c040445ea17964a010d5` |
| Standard CPU policy SHA-256 | `95604fdb423e9dfd7d8d4b427a2a1f39fb112825875b208e09e9dab6fad5cfbc` |
| Selected production camera YAML SHA-256 | `3c2e90046d06f95fd24f0fa76fab8e7587cca64cbc3344561443aa0ee3e377de` |
| Compiler/test image | `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`, native x86_64 image producing ARM64 output |
| Packaging image | `sha256:620524fe768a8c7664a24c40504c86dfea35538d53b2da63b6dbf4cc4849293d`, ARM64 packaging environment |

The Step 2 audit compared **1,923 source-manifest entries** against current files. Exactly two entries differ from the compiled snapshot: this change's `tasks.md` and `validation.md`, both closure documentation. No compiled implementation or configuration entry differs. Later verification reports and the controller-owned summary are workflow records, not changed compiled inputs. All **853** current-candidate retained evidence checksums passed. The archive's embedded node and policy match the extracted package and current selected policy. All package checksums passed; all **100** bundled ROS libraries have ARM64 ELF headers.

The original-contract source archive was also compared with current source. Node integration, catalog, configuration, sampling, payload accounting and the original transport/writer/node regression tests are unchanged. The recording implementation differences are the manager's executor affinity/cleanup and writer role selection, retained in `original-contract-source.diff`. Those differences have fresh focused tests and fresh current-candidate board acceptance. Historical workload measurements are not relabeled as measurements of the current binary.

The physical-board evidence identifies hostname `hzhy`, `HZ-RK3576_WuKong Board`, Ubuntu 22.04.5 ARM64, kernel `6.1.141-rt36`, GCC 11.4.0, 3.8 GiB RAM and no swap. The retained preflight records storage, current device permissions, `v4l2-ctl --list-devices`, `media-ctl -p` and selected capture format before the runs. SC233HGS used `/dev/video22`, NV12 1920×1200 at 60 Hz. The board had eight online CPUs. System `/usr/lib/librknnrt.so` remained Runtime 2.3.2 with SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`; the driver remained 0.9.8. No system Runtime copy was overwritten.

| Model or replay input | SHA-256 from the matched before/after identities |
|---|---|
| YOLO `balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| Template Backbone `fp16/template_backbone.rknn` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| Search Backbone `fp16/search_backbone.rknn` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| Head `fp16/head.rknn` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |
| `fixed-balloon.avi` | `c25aec33208f5dfca315eb9c2c4fb86b04d98883ec5d74b68f5ac7746a669a95` |

Live-camera data are the declared production-board bench scene, not an annotated tracking-quality dataset. Replay used `/home/hzhy/drone-diagnostic-acceptance-20261012/fixed-balloon.avi`.

## Commands and actual results

General host evidence processing used `/home/mzh/miniconda3/bin/python`. PyYAML-dependent tests used `/usr/bin/python3` because the preferred conda interpreter lacks that dependency.

| Command/check | Scope and actual result |
|---|---|
| `/usr/bin/python3 deployment/tests/test_cpu_policy.py "$PWD"` | Fresh Step 2; exit 0. Strict role values, legacy fallbacks, inherited/disabled compatibility and safe feature-response validation. |
| `/usr/bin/python3 -m unittest discover -s deployment/tests -p test_package.py` | Fresh Step 2; 6/6, exit 0. Includes missing/wrong-architecture message dependency rejection and selected inputs. |
| `/usr/bin/python3 -m unittest discover -s deployment/tests -p test_install_policy.py` | Fresh Step 2; 7/7, exit 0. Real install/rollback scripts in isolated redirected fixtures, with service/hardware stubs. |
| Docker invocation of `Step 2/run_focused.sh` using the immutable compiler image above | Fresh Step 2; exit 0. Read-only build/install mounts; native transport, writer and affinity binaries pass. ARM64 CPU-affinity and catalog/policy tests pass through QEMU emulation. Both node query forms pass. Exact script retained. |
| Docker invocation of `Step 2/check_stale_binary.sh` | Fresh Step 2; exit 0. Real old ARM64 node `8fbad8fc...` returns scalar `2` for both ordinary and feature queries. Current helper rejects it with expected exit 1 and rebuild/`--install-root` remedy. |
| `Step 2/audit_evidence.py` | Fresh Step 2; exit 0. Source/package/evidence checksums, architecture, current board identities, worker logs, harness affinity calls and four-bag SQLite reconciliation. |
| `Step 2/audit_original_contract.py` | Fresh Step 2; exit 0. Original evidence checksums, historical binary/source identity and 22 original expanded/image/lifecycle bags independently reconcile. |
| `./build.sh --test --artifacts-dir /home/mzh/nickluo/validation-retained/topic-recorder-cpu2-install-20261113/apply/build --quadrotor-msgs /home/mzh/nickluo/quadrotor_msgs` | Retained current-candidate build; exit 0, 72/72 CTests and separate native memory-limit 1/1. Default amd64 cross backend; no backend switch. Step 2 counted the individual passing test records. |
| Current-candidate package command with explicit matching installation/message prefixes and `--without-models` | Retained exit 0; exact inputs/command in `step1/command-statuses.json`. `SHA256SUMS` independently passes again in Step 2. |
| Stale-node package reproduction after fix | Retained expected exit 1 before publishing an archive, with remedy. Its pre-fix accepted package remains labeled reproduction evidence. |
| Staged board `bash ./deploy.sh --check` and both query forms | Retained current-candidate exit 0. Actual target checks and library environment precede service/installation mutation. |
| Current-candidate board workload/readback/export/restoration commands | Retained exit 0 for all nine workload/readback/export phases and all restoration/orchestration results. |
| `openspec validate centralize-rk3576-session-topic-recording --strict` | Fresh Step 2; exit 0 after the completed report. |
| `git diff --check` | Fresh Step 2; exit 0. |

The first Step 2 focused-container attempt exited 1 while sourcing ROS with shell `nounset` enabled. No test ran in that attempt. The retained `focused-runtime-setup-failure.log` records it. The evidence-only runner now follows the repository's ROS setup convention and enables `nounset` after sourcing; the complete rerun passed. No production source was changed for this harness correction.

The installer fixtures verify rejection of scalar-only older nodes in both `--check` and install modes before the target directory or service-call log exists. Missing role and unusable-core cases likewise fail before mutation. Fresh install, upgrade, custom-core, inherited-mode and recorder-disabled cases preserve selected policy bytes. Upgrade/rollback restores the previous policy and camera configuration with its previous release link. These isolated tests are not a claim of production installation or rollback execution.

## Current-candidate board and bag review

The fresh retained runs use the current node and standard policy, with two ON/OFF rounds per input. Each enabled round has an 18-second active interval; the disabled resource check uses shorter 8-second intervals. No run approaches the 180-second limit.

| Mode | Process seconds | Bags | Accepted = written | Intentional policy omissions | Peak messages / bytes |
|---|---:|---:|---:|---:|---:|
| Camera, enabled | 43.791 | 2 | 22,039 | 10,445 | 85 / 24,052 |
| Balloon replay, enabled | 43.762 | 2 | 21,271 | 10,515 | 73 / 21,856 |
| Balloon replay, disabled | 23.775 | 0 | 0 | 0 | 0 / 0 |

Step 2 opened every retained `.db3` read-only, checked `PRAGMA integrity_check`, counted messages by topic, and compared every count with `session.json`. Every topic satisfies `offered = accepted + policy_skipped + byte_limited + oversized + queue_dropped` and `accepted_but_unwritten = accepted - written`. All four phases are `complete`. Totals are **43,310 accepted = written** and **20,960 intentional policy omissions**. Byte-limited, oversized, queue, supported middleware-loss, incompatible-QoS and unwritten counters are zero. There is no image topic data or metadata.

| Bag | Offered | Accepted = written | Policy omissions | Peak messages / bytes |
|---|---:|---:|---:|---:|
| Camera round 1 | 16,289 | 11,057 | 5,232 | 85 / 24,052 |
| Camera round 2 | 16,195 | 10,982 | 5,213 | 50 / 13,258 |
| Balloon replay round 1 | 15,910 | 10,659 | 5,251 | 73 / 21,856 |
| Balloon replay round 2 | 15,876 | 10,612 | 5,264 | 42 / 11,352 |

The preserved full typed readbacks deserialize all messages, check valid source stamps, command/feedback vectors, paired observation timestamps and final boundaries. Step 2 reviewed that checker and its error-free outputs, independently checked SQLite data and confirmed their evidence checksums. It did not substitute summary-file presence for complete readback.

Each enabled log has three before-work `role=topic_recording requested=2 actual=2` bindings: one executor and two independently recreated writers. Read-only observer samples then confirm those masks. The camera controller and both encoder processes remain on CPU 7; other managed roles retain their configured cores. The harness's only `sched_setaffinity` call is for its own publisher process on CPUs 0–3. It never retargets tracker thread IDs. Disabled replay has no topic workers, recorder-only endpoints or bags.

Both enabled diagnostic exports have normal completion and zero producer gaps. Protected-file checks, service restoration, executable identity and current-link comparisons all pass. Four originally active services were restored, the restoration timer is inactive, and no staged process remains. Existing production diagnostic ENOSPC logs were preserved.

## Original recording contract and historical acceptance

The original recording scope remains covered, beyond the CPU 2 refinement. Current native tests exercise all catalog types, source-time extraction, retained mode, policy outcomes, old-handle exclusion, writer faults, queue reserves and file splitting. The current retained 72-test suite also includes real node video/session, continuous-preview races, failure isolation and disabled recording.

Step 2 verified the original evidence manifest and independently reconciled **22 complete bags**: one expanded, one image-enabled and 20 lifecycle bags. The image run contains **31** original images. The historical expanded run has 9,473 accepted/written and 4,492 intentional omissions; the image run has 9,356 accepted/written and 5,214 omissions. The 20-round lifecycle has **38,941 accepted = written**, 18,143 omissions and no unexpected recorder loss. Its active workload is 62.643 seconds within an 80.685-second process. File descriptors remain 54 and threads 21; RSS does not accumulate monotonically. Final OFF retains preview.

The matched previous/disabled/expanded comparison includes actual camera consumption, display rate, latency, CPU, memory, shared-filesystem writes, input rates/sizes and policy counters. The recorded expanded-vs-previous difference is +32.48 CPU percentage points and +13.37 MiB median RSS. Those measurements belong to `8fbad8fc...`, with input-rate and scene limitations. They are historical support for the completed original workload tasks, not a fresh performance claim about `de6c8d79...` or the CPU 2 policy.

**The historical lifecycle harness exited 1 because its stricter 500 ms diagnostic-lag gate failed.** It observed 612,498,243 ns of synchronization backlog. The tracker exited 0; all 20 bags and the final diagnostic prefix read back, with a normal diagnostic close and zero producer gaps. The backlog recovered. This remains a failed diagnostic-latency gate. The selected change's recorder/lifecycle criteria do not impose that 500 ms threshold, so it is not silently counted as a pass or used to weaken a requirement. Existing production ENOSPC and other superseded failures remain disclosed in `validation.md`.

## Task completeness

| Tasks assessed | Implementation and independent completion evidence |
|---|---|
| 1.1–1.5 | Preserved baseline/source/message identities; exact fixed catalog; generated real messages; RK3576-only linkage; fresh catalog/transport and package dependency checks; owning guides. |
| 2.1–2.6 | Immutable `Config`, name and parameter validation, H/S/R timestamp selection, monotonic sampler and image tokens; deterministic policy and native transport tests; current retained node configuration tests. |
| 3.1–3.5 | Dynamic-capacity payload accounting; in-flight charge, per-entry/global limits, reserve and fairness; fresh writer and transport checks, reconcilable board counts; documented memory limits. |
| 4.1–4.5 | Sole ordinary subscription owner, non-auto callback group, dedicated executor, bounded mode cache; native typed transport and affinity checks; reviewed node removal of distributed RK recording paths. |
| 5.1–5.6 | Authoritative triggers under control lock, deferred/ignored command cleanup, paired submission before publication, immutable round handles and shutdown joins; current retained node race/EOF/fault tests and current board boundaries. |
| 6.1–6.5 | Exclusive directories, no-follow storage accounting, split guard and interval checks, bounded summaries; fresh writer split/quota/free-space/open/write/close/summary failure tests and complete multi-file readback. |
| 7.1–7.4 | Real message dependency closure independent of OSD feedback; 100 ARM64 libraries, unchanged selected YAML/policy; fresh package/install fixtures and source/docs review; no additional recorder service or Runtime copy. |
| 8.1–8.5 | Current supported full build plus independently checked original matched performance/image/lifecycle evidence, preserved identities and failure limits; complete requirement mapping and strict validation. Historical measurements retain their original identity. |
| 9.1–9.4 | Optional shared topic role, separate legacy fallbacks, inherited behavior, readiness/readback and failure cleanup; fresh CPU/recording-affinity tests, current native worker logs/read-only board samples and owning docs. |
| 10.1–10.4 | Exact standard policy and selected overrides, structural offline checks, target usability preflight before mutation, safe selected-binary capability guard; fresh policy/package/install tests and actual stale-node probe; documented migration/rollback. |
| 11.1–11.3 | Current candidate's supported build/package, two camera and two replay rounds, disabled-resource case, full readback, mask and restoration evidence; source/archive/config identities and explicit historical-performance boundary. |

## Design adherence

| Decision | Assessment and implementation |
|---|---|
| 1. One manager/writer/executor | Followed: `recording_manager.cpp:10–73`, `bag_session.h:51–63`; non-auto callback group; separate resource and worker roles; startup confirmation and cleanup. |
| 2. Typed startup registry | Followed: `topic_catalog.h:34–95`, `recording_manager.h:60–82`; every ordinary entry has one typed subscription. |
| 3. Narrow pair/trigger interfaces | Followed: `recording_manager.cpp:129–169`, `drone_tracker_node.cpp:183–204,280–289,1596–1628`; pair admission precedes independent ROS publications. |
| 4. Explicit session generations | Followed: `recording_manager.cpp:107–180`, node control/shutdown paths; no preview bag, fabricated trigger or post-OFF tail. |
| 5. Frozen names/QoS | Followed: `config.cpp:25–89`, `topic_catalog.h:34–95`, parameter guard at `drone_tracker_node.cpp:120–135`. QoS is ROS delivery reliability, retained-state behavior and depth. |
| 6. Source time separate from selection time | Followed: `policy.h:17–60`, manager callback-entry clocks and pair source stamp; original payload retained. |
| 7. Explicit rate/image policy | Followed: `topic_catalog.h:34–79`, `policy.h:40–75`, `recording_manager.h:42–57`; protected events and final pair bypass. |
| 8. Bounded retained work | Followed: `policy.h:77–121`, `bag_session.h:161–230`; pending plus in-flight charge, two-slot pair, reserve and fairness. |
| 9. Retained mode and bounded evidence | Followed: `recording_manager.cpp:69–128`, `bag_session.h:258–281`; bounded cache and summary, distinct snapshot/counter origin. |
| 10. Splits/storage without deletion | Followed: `bag_session.h:230–257,282–365`; writer-only checks, no symlink traversal or automatic deletion, original failure precedence. |
| 11. Typed release/matched policy | Followed: `deployment/package_core.py:25–40,117–289`, `cpu-policy.py:15–105`, `deploy.sh:28–81,116–133`; exact selected bytes and pre-mutation checks. |
| 12. Evidence before performance claims | Followed: deterministic/native/emulated checks separated from identified board runs; original measured comparison retained; fresh automatic CPU 2 evidence; no zero-overhead claim. |

## Issues by priority

### CRITICAL — 0

None.

### WARNING — 0

None. The previous selected-binary warning is closed by the safe capability query, actual stale-node rejection and installer pre-mutation tests. Ordinary diagnostic protocol remains `2`; feature-query output is `{"diagnostic_protocol_version":2,"topic_recording_affinity":1}`. Legacy direct-runtime fallback remains supported, while new recorder-enabled role-pinned releases require the role and a capable selected node.

### SUGGESTION — 1

**S1: Expand dense recording code when next maintaining these paths.** `src/bag_session.h:161–230,258–365` and `src/recording/recording_manager.cpp:77–180` compress several state changes and branches onto single lines. Split queue-admission, terminal-result and summary-output blocks into focused statements or helpers. Keep the existing fault, accounting and lifecycle checks unchanged. The current implementation is traceable and tested; this optional readability improvement does not require an unrelated rewrite in this verification loop.

## Skipped checks, limits and properties not established

No applicable OpenSpec check is skipped or partially verified. This is an evidence-backed verification rather than a claim that every possible validation command was repeated.

- Step 2 did not rebuild the full project or repeat board workloads. It verified the retained current source/package/run identities and reran focused meaningful binaries/tests. The current candidate's supported complete build is the retained 72/72 + native-memory 1/1 result.
- Jetson hardware build/runtime execution was not run; matching hardware/SDKs were unavailable. Platform selection, preprocessor boundaries and existing configuration checks were reviewed. No Jetson acceptance is claimed.
- `clang-tidy`, `clangd` and `clang-format` remain unavailable in the recorded host/image environments. No lint or language-server pass is claimed. Compiler warnings and manual pattern review are not described as those tools.
- No production installation or rollback was performed. Actual target preflight passed; installation/upgrade/rollback mutation behavior was checked in isolated fixtures.
- A fresh three-way performance comparison, 20-round endurance run or image-enabled hardware run was not repeated for `de6c8d79...`. Original task evidence remains valid at its stated identity; current automatic binding has its separate camera/replay acceptance. No new performance improvement or zero-overhead conclusion follows.
- The historical 500 ms diagnostic-lag gate failed and remains failed. It is separate from complete recorder readback and zero diagnostic producer gaps. Existing production ENOSPC was not repaired or hidden.
- Device-memory usage, per-process physical disk attribution, calibrated camera exposure age, flight accuracy, target quality, sudden-power-loss retention and long-duration reliability are not established. Filesystem calls can block; observed free space is not an atomic reservation across writers.
- Receiver callback delay is fixture instrumentation. Native publication and frame-processing measurements do not establish calibrated source-to-receiver latency. No such claim is made.

No commit, push, specification synchronization, archival, production installation/rollback or system Runtime overwrite occurred. All pre-existing uncommitted work was preserved. The controller owns `verify-fix-summary.md`; this step did not edit it.

## Requirement and scenario coverage

The appendix below maps every requirement and every scenario to implementation and tests/evidence. Existing non-English titles are reproduced only as exact specification identifiers. All assessment prose is English.

### production-binary-deployment

| Requirement | Implementation evidence |
|---|---|
| Session recording configuration travels with the release | `deployment/package_core.py:117–149,272–289; deployment/deploy.sh:91–133; deployment/rollback.sh` |
| Typed recording dependencies are available without the OSD adapter | `CMakeLists.txt:82–102; deployment/package_core.py:25–40,190–251; src/recording/topic_catalog.h:15–16` |
| Expanded recording adds no recorder service | `src/drone_tracker_node.cpp:1101–1112; deployment/package_core.py:19–21,148–170; deployment/deploy.sh` |
| Standard installation selects CPU 2 for topic recording | `deployment/cpu-affinity.yaml; deployment/package_core.py:134–149; deployment/deploy.sh:116–133; deployment/run-camera` |
| New role-pinned recorder packages name the topic CPU explicitly | `deployment/cpu-policy.py:15–105; deployment/package_core.py:139–142; deployment/deploy.sh:28–61` |
| CPU policy validation distinguishes packaging from target availability | `deployment/cpu-policy.py:46–82; deployment/package_core.py:141–142; deployment/deploy.sh:28–34,81` |
| 收集器绑定不改动其他执行单元 | `deployment/diagnostic-launch.py; deployment/run-diagnostics; src/platform/cpu_affinity.cpp; deployment/cpu-affinity.yaml` |

| Scenario | Condition handling and test/evidence coverage |
|---|---|
| Inspect the selected recording policy | Assessed; Fresh package selected-input tests; package camera hash and source record; installer exact configuration copy. |
| Roll back the expanded recorder | Assessed; Fresh installer test_upgrade_and_matching_rollback restores old camera, policy and release. |
| OSD feedback adapter disabled | Assessed; Current compiled OSD-feedback-disabled configuration, 100 ARM64 libraries, typed native transport and board readback. |
| Missing shared-message dependency | Assessed; Fresh RecordingLibraryTest.test_complete_missing_and_wrong_architecture; actual package ELF audit. |
| Inspect the expanded release services | Assessed; Package service inventory and fresh install fixture; current camera board run shows one bag writer and independent camera controller/encoders. |
| Install the standard policy | Assessed; Fresh default-policy assertion, exact package/archive/installed policy bytes; current board native logs and read-only CPU 2/7 masks. |
| Upgrade an older installed CPU policy | Assessed; Fresh installer test_upgrade_and_matching_rollback replaces older policy and preserves backup; both entries derive allowed cores from shared policy. |
| Explicit role on a custom core | Assessed; Fresh custom-core install/byte fixture, explicit-role native affinity and CPU-policy tests; no hardcoded worker CPU. |
| Legacy role-pinned input for a new recorder-enabled package | Assessed; Fresh missing-role check/install before-mutation fixture plus helper validation and current packaging guard. |
| Explicit inherited-mode override | Assessed; Fresh inherited-mode install and capability-bypass tests; CPU tests preserve inherited masks. |
| Recorder-disabled package using an older policy | Assessed; Fresh disabled-recorder compatibility fixture plus current disabled board case with no workers/files. |
| Build host lacks the target core topology | Assessed; Fresh test_custom_core_and_offline_topology mocks absent host topology and accepts offline structure. |
| Target CPU 2 is unavailable | Assessed; Fresh test_unavailable_cpu_fails_before_mutation and actual successful target --check; syntax/core errors do not reach mutation. |
| 保留生产业务映射 | Assessed; Current board masks and exact standard-policy bytes, unchanged business assignments; retained deployment_diagnostics_tests. |
| 保留旁路进程和调用方 | Assessed; Retained deployment_diagnostics_tests plus CPU transition/caller restoration tests and current read-only board observer. |
| 诊断无法绑定时业务继续 | Assessed; Retained deployment_diagnostics_tests and diagnostic startup failure tests; existing degraded-diagnostics path remains independent. |

### rk3576-continuous-hdmi-stream

| Requirement | Implementation evidence |
|---|---|
| 记录与 ROS 调试图像保持业务边界 | `src/drone_tracker_node.cpp:340–374,530–621,1596–1628; src/recording/recording_manager.cpp:107–180` |

| Scenario | Condition handling and test/evidence coverage |
|---|---|
| 开启调试图像但未触发 | Assessed; Current retained ros_continuous_camera_tests: preview lifecycle and no preview bag; current camera board preview. |
| 结束一轮后继续预览 | Assessed; Current retained continuousCameraLifecycle/continuousBlockedNanoAndTelemetry; current camera OFF/next-round boundaries. |
| 调试图像不等于消息录像 | Assessed; Current retained recordingSessions and bagWithoutImagesAndDualRecording; historical image run/full readback and independent video workload. |

### rk3576-ros-video

| Requirement | Implementation evidence |
|---|---|
| ROS bag 记录与关闭互斥 | `src/bag_session.h:51–365; src/recording/recording_manager.cpp:107–180; src/drone_tracker_node.cpp:183–315,633–708,1596–1628` |

| Scenario | Condition handling and test/evidence coverage |
|---|---|
| EOF 与姿态回调并发 | Assessed; Current retained recordingSessions: publishers continue during EOF/close; fresh old-handle native transport test. |
| 目录不可写或已存在 | Assessed; Fresh writer faults existing-evidence collision test; current retained open-fault node tests preserve tracking. |
| bag 写入失败 | Assessed; Fresh writer faults and current retained bagWithoutImagesAndDualRecording; original reason/unwritten counts retained. |
| 队列达到容量 | Assessed; Fresh writer inFlight/fairnessAndReserve; retained node bagQueueLimits proves nonblocking bounded admission. |
| 禁用调试图像 | Assessed; Current retained bagWithoutImagesAndDualRecording and all four current bags have no image data/metadata. |
| bag 完整性回查 | Assessed; Fresh writer split readback, native typed transport equality; current four-bag typed/SQLite reconciliation. |
| 开启图像也不写入 bag | Assessed; Current retained recordingSessions reads original image payloads/stamps; historical image bag contains 31 images. |
| 仅余一个消息位置 | Assessed; Retained pairedBagAdmission/bagQueueLimits and fresh reserved-capacity writer test; pair admission is one operation. |
| 无记录请求 | Assessed; Retained disabled-node cases and current disabled replay prove no recorder resources; camera video remains independently configured. |

### rk3576-session-topic-recording

| Requirement | Implementation evidence |
|---|---|
| Fixed startup catalog covers the application interfaces | `src/recording/topic_catalog.h:34–95; topic_catalog.cpp:4–16; recording_manager.cpp:10–20; recording_manager.h:60–82` |
| Topic names and delivery settings are explicit | `src/recording/config.cpp:25–89; recording_manager.h:60–82; src/drone_tracker_node.cpp:120–135,1101–1112` |
| Recording uses explicit operating sessions | `src/drone_tracker_node.cpp:183–315,340–435,530–621; recording_manager.cpp:107–180` |
| Session boundaries retain authoritative control events | `src/drone_tracker_node.cpp:183–204,280–315,1596–1640; recording_manager.cpp:129–180` |
| Source timestamps and payloads are preserved | `src/recording/policy.h:17–36; recording_manager.h:42–57; recording_manager.cpp:129–169` |
| Invalid source-time fallback is visible | `src/recording/policy.h:17–36; recording_manager.h:45–47; src/bag_session.h:97–108,266–278` |
| Frequency sampling affects only recording | `src/recording/policy.h:40–52; recording_manager.h:44–49; recording_manager.cpp:13–19` |
| Commands and observation boundaries are protected from sampling | `src/recording/topic_catalog.h:49–74; recording_manager.cpp:153–169; policy.h:40–52` |
| Published debug images have independent recording limits | `src/recording/recording_manager.h:46–57; topic_catalog.h:73; topic_catalog.cpp:13–15; src/drone_tracker_node.cpp image publish path` |
| Retained mode is available at session start | `src/recording/recording_manager.cpp:69–77,121–128; recording_manager.h:73–78` |
| Admission is bounded and keeps observation pairs intact | `src/bag_session.h:64–95,161–211; src/recording/policy.h:77–121` |
| Topic limits preserve room for control evidence | `src/bag_session.h:124–125,161–211,220–230; src/recording/topic_catalog.cpp:13–15` |
| Recording completeness distinguishes deliberate omissions | `src/bag_session.h:97–123,161–211,258–281,348–365` |
| Session coverage and errors remain inspectable | `src/recording/recording_manager.cpp:63–68,78–108; src/bag_session.h:51–58,109–123,258–281` |
| File splitting does not delete evidence | `src/bag_session.h:141–151,282–346; src/recording/config.h:15–17` |
| Storage protection ends recording independently | `src/bag_session.h:230–257,282–325; src/recording/config.h:15–17` |
| Normal shutdown and abrupt loss have different guarantees | `src/bag_session.h:117–123,214–219,282–365; src/drone_tracker_node.cpp:658–683` |
| Production acceptance measures the combined workload | `validation.md; retained original comparison/raw workloads; current board-analysis and identity audits` |

| Scenario | Condition handling and test/evidence coverage |
|---|---|
| Default application catalog and policies | Assessed; Fresh catalog exact-ID/default policy test and full typed transport across all 31 entries plus bench. |
| Late radar publisher without graph rescanning | Assessed; Fresh transport() creates radar_target publisher after manager/session; existing fixed subscription connects. |
| Unrelated topic | Assessed; Structural review: fixed compiled entries are the only subscription loop; catalog exact-ID test excludes unknown entries. No graph discovery path exists. |
| Interceptor configured without its packaged profile | Assessed; Fresh configuration/name-override/remapping transport tests; startup configurable mission_start uses same validated ordinary name path. |
| Duplicate resolved names | Assessed; Fresh configuration() duplicate-name rejection plus retained node invalid-recording-only tests. |
| Runtime policy update | Assessed; Retained node configuration() tests runtime bag parameter guard and restart-required failure. |
| Preview followed by two sessions | Assessed; Retained continuousCameraLifecycle and current two camera sessions: preview creates no bag. |
| Mission cancellation while tracking continues | Assessed; Retained continuousBlockedNanoAndTelemetry: MISSION/DISMISS recorded; DISMISS does not stop tracking. |
| Recording disabled | Assessed; Current disabled replay and retained disabled-node tests: no subscriptions/executor/writer/bags. |
| Installed topic role across two rounds | Assessed; Current enabled camera and replay logs/read-only masks: executor plus both writers on CPU 2 with distinct bags. |
| OFF during blocked inference | Assessed; Retained continuousBlockedNanoAndTelemetry/offDuringSessionOpening; final pair and OFF before seal; stale results excluded. |
| Delayed response in another module | Assessed; Fresh transport() submits through old handle after replacement and proves rejection; sealed active handle is removed, with no grace interval. |
| Delayed control command | Assessed; Fresh typed command transport roundtrip preserves expected_execution_time while indexing header time; catalog command test. |
| Header trigger and headerless motor values | Assessed; Fresh full Header and Float32MultiArray transport roundtrip; direct trigger source stamp and headerless receipt rule. |
| Unset header | Assessed; Fresh policyAdmission() checks zero/negative/malformed fallbacks and unchanged payloads/readback; catalog policy reasons. |
| High-rate IMU | Assessed; Fresh deterministic policy() first sample/5 ms rejection/10 ms selection; recording-only independent subscription path. |
| System-clock correction | Assessed; Fresh policy() elapsed rollback/sparse-rate tests; sampling clock and original source stamp are separate. |
| Brief state transition | Assessed; Fresh policyAdmission() writes three brief protected failsafe events and catalog protected-event checks. |
| Sampled observation pair followed by OFF | Assessed; Fresh policyAdmission() joint pair sampling and final_off bypass; both accepted/written counters reconcile. |
| Existing image publisher enabled | Assessed; Current retained recordingSessions image payload/stamp readback; fresh image policyAdmission; historical original image bag/rate/byte evidence. |
| Production image switch remains off | Assessed; Current four board bags and retained bagWithoutImagesAndDualRecording: no generated image or image metadata. |
| AIM published before ON | Assessed; Fresh transport() publishes retained AIM while idle and asserts one snapshot in the next session, with snapshot-origin metadata. |
| One remaining message slot | Assessed; Retained pairedBagAdmission checks one remaining slot and both drop counters; atomic pair Work admission. |
| Large array and slow disk | Assessed; Fresh inFlight() blocks a 20 MiB image; charge stays retained and other admission completes promptly; dynamic payload tests. |
| Images saturate their allowance | Assessed; Fresh inFlight()/fairnessAndReserve() saturate ordinary/image limits, admit reserved OFF and pair, and enforce global 256 bound. |
| Successful sampled recording | Assessed; Fresh typed transport and current board bag accounting: intentional policy skips with accepted=written and complete phase. |
| Queue overflow | Assessed; Fresh writer limit tests and retained node bagQueueLimits explicitly require incomplete after queue rejection. |
| Quiet optional radar input | Assessed; Structural metadata review: every initialized catalog entry remains in summary with observed=false at zero attempts; incompatibleDelivery() distinguishes detected QoS error from silence. |
| Multiple files within one session | Assessed; Fresh split() writes multiple SQLite files and reads all messages with preserved timestamps; faults() preserves existing evidence. |
| Space consumed by another writer | Assessed; Fresh activeStorageLimits()/storageAccounting(): injected shared free-space loss during writing and split, owned incomplete/summary accounting and symlink exclusion. |
| Mid-session write failure | Assessed; Fresh faults() open/write/close/summary and interrupted pair checks; retained node bag faults preserve tracking/video; no power-loss guarantee in docs. |
| Board comparison report | Assessed; Independent original evidence/22-bag audit, original matched previous/disabled/expanded/image profiles and resource data; current automatic binding acceptance assessed separately. |

### rk3576-thread-affinity

| Requirement | Implementation evidence |
|---|---|
| Topic recording has a dedicated optional CPU role | `src/platform/cpu_affinity.h:10–19; cpu_affinity.cpp:58–73,104–151,158–256; deployment/cpu-policy.py:46–82` |
| Topic threads confirm binding before work and on recreation | `src/platform/cpu_affinity.cpp:158–189,237–256; src/recording/recording_manager.cpp:24–60; src/bag_session.h:51–63` |
| Topic placement preserves other affinity responsibilities | `src/recording/recording_manager.cpp:10–20,24–73; src/platform/cpu_affinity.cpp:191–256; existing camera Recording role` |

| Scenario | Condition handling and test/evidence coverage |
|---|---|
| Standard installed role | Assessed; Fresh CPU and recording-affinity tests; current native logs/read-only board masks and exact selected policy. |
| Legacy policy used directly at runtime | Assessed; Fresh cpu_affinity_tests cover distinct executor RosTransport and writer Recording fallbacks, with no generic fallback substitution. |
| Invalid explicit topic role | Assessed; Fresh Python/C++ policy tests reject null/bool/string/fractional/negative/outside-set explicit values; actual availability checked at preflight/start. |
| Explicit inherited mode | Assessed; Fresh inherited/no-policy CPU tests preserve masks; installer inherited override stays byte-identical and unpinned. |
| A second session creates a new writer | Assessed; Fresh affinity exercise() opens/seals two sessions with before-work mask hooks; current two-round camera/replay writer logs. |
| Executor binding is rejected | Assessed; Fresh affinity exercise() injects OS mask rejection, observes creator exception, joins thread and leaves ROS/caller healthy. |
| Writer binding is rejected | Assessed; Fresh affinity exercise() rejects writer before open hook; no active handle is published; current node catch reports recorder unavailable. |
| Concurrent camera video and topic recording | Assessed; Current camera board: both topic workers CPU 2, camera controller/encoders CPU 7, ROS helpers CPU 6; fresh caller/mapping restoration tests. |
| Topic recording is disabled | Assessed; Current disabled replay and retained disabled-node/camera tests; need_save guard creates no manager/workers, independent video retains Recording role. |

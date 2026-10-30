# Camera-recording delivery validation

Date: 2026-09-24. Final state at validation: the feature, related regressions, one five-minute real recording, and basic process-failure checks were completed. The task list was 37/37 complete. Cancelled long-duration acceptance was not run.

## Scope and environment

The user selected five minutes of continuous recording with the default configuration as the final load acceptance. No multi-sync-period performance matrix, twenty-round hardware restart run, or thirty-minute load run was performed. Completed controlled tests and truncation evidence were retained.

The acceptance board was HZ-RK3576_WuKong, Ubuntu 22.04 ARM64, kernel `6.1.141-rt36`. Input was SC233HGS `/dev/video22`, 1920×1200 NV12 at 60 fps, with media path `/dev/media2`. Existing YOLO and FP16 NanoTrack models were used with simulated attitude and intrinsics. These conditions do not establish detection accuracy or flight performance.

System RKNN Runtime was 2.3.2 and RKNPU driver was 0.9.8. The system library was unchanged. Baseline commit was `aa97b7f7dea4d888fca48e95c236a79c33e061dc`; delivery included uncommitted changes. See [source identity](evidence/components/final-source-identity.json) for the source-set digest and [binary identities](evidence/components/final-binary-identities.txt) for the accepted program, worker, models, and system-library fingerprints. Production `/opt/drone_tracker/current` and production configuration were not switched.

## Completed checks

| Check | Result and evidence |
|---|---|
| Production encoding compatibility | Original dimensions, actual capture rate, and hardware H.264 encoding passed; see [encoding gate](encoding-gate.md) |
| Branching and warmup | Latest production-board camera checks passed 15/15, including pixels, color metadata, queue isolation, and cancellation at the final boundary; see [camera regression](evidence/components/camera-latest.log) |
| ROS and display integration | Related regressions passed 7/7 after a clean rebuild, including recording parameters, no-target behavior, display switching, legacy bag behavior, and repeated restart; see [build and regressions](evidence/components/final-target-build-tests.log) |
| Storage and worker | Short writes, EINTR, EIO, ENOSPC, file/directory synchronization failures, dynamic space protection, and twenty controlled resource rounds passed; see [component results](evidence/components/latest-storage-session.log) |
| Recovery and evidence protection | 194 truncation positions were checked. The independent inspector detected damaged tails; repeated inspection left originals unchanged. See [truncation results](truncation-validation.md) and [inspector results](evidence/components/recovery-and-isolation-tests.log) |
| Default segmentation | Both segments of the 70-second joint check decoded independently; all 4511 frame timestamps matched. See [segment evidence](evidence/components/joint-smoke-decode.json) |
| Cancellation/failure before first frame | Both produced no video file and zero video frames. Cancellation and camera-warmup failure remained distinct; a new round succeeded after removing the injection. See [lifecycle results](evidence/components/lifecycle-final.json) |
| Actual synchronization stall | An injected library blocked video `fdatasync`. Recording stopped after five continuous seconds without persistence progress; tracking continued and the next recording round recovered. Drops during this failure were not counted as healthy-run results. See the same lifecycle results |

Cross-host extraction timestamps once prevented stale objects from rebuilding and caused a `FrameSourceStatus` destructor segmentation fault. A clean rebuild removed it without a feature-logic change. The targeted rerun and subsequent seven regressions passed. Future source transfer should use extraction-time modification timestamps or clean the affected build to avoid mixing different structure layouts. The earlier 33/34 failure record remains a failure.

An earlier implementation snapshot passed 34/34 in the local ARM64 container. The final code is supported by this production-board build and its related regressions. Board and container results do not replace each other.

## Final hardware checks

The default configuration passed a 300-second observation window while tracking and OSD continued. The main round included about five stable seconds before observation and contained 18310 qualified frames. Every frame was accepted, encoded, written, and synchronized; recording drops were zero. A subsequent ON/OFF saved 122 frames; duplicate ON created no extra session. All six main-round segments and the subsequent round's one segment decoded independently. Every PTS matched its frame record and there were no B-frames. See [decoding and accounting](evidence/components/five-minute-decode.json).

The main round dequeued 18320 camera frames: 10 warmup discards, 0 terminal dispositions, and 18310 qualified frames. The tracking queue accepted 18238 and dropped 72. Those are drops on the original tracking branch; recording still saved all qualified frames. Driver-sequence gaps were zero. Recording preparation took 130.018 ms; warmup took 300.542 ms. First qualified input to encoded output took 13.061 ms. Recording stop took 38.328 ms and camera stop took 9.540 ms; both reclaimed their resources.

[Five-minute metrics](evidence/components/five-minute-metrics.json) report synchronization lag P99 of 1.032998 seconds and a maximum of 1.066331 seconds, below the 2-second threshold. All 3000 synchronization samples in the observation window were known. There were 11 unknown synchronization samples from ON to OFF in the main round and 11 in the subsequent round. They remain separate and are not included in those percentiles. Maximum synchronization-call duration was 26.215 ms. Tracking processing P95 was 47.575 ms and consumption throughput was 27.04 frames/second. These are observations from this run, not a performance guarantee relative to a baseline.

Mean CPU use was about 2.15 cores for the node and 0.19 cores for the recording worker. Peak RSS, the resident process memory, was 324732 KiB for the node and 30868 KiB for the worker. Worker file descriptors remained at 42. Maximum sensor temperature was 85.923°C and acceptance-partition writes increased by 419237888 bytes. Node memory and descriptors vary with model/display state; five minutes does not establish the absence of long-term growth.

Each basic process-failure check ran once. After force-killing the recording worker, 43 tracking-box messages still arrived and a new recording round recovered. After force-killing the parent, the worker exited and was reaped within the check deadline. The two abnormal sessions retained 123 and 132 decodable frames; the normal retry after the worker failure retained 136 decodable frames. Original SHA256 values matched before and after independent inspection; no original was overwritten or modified. See [process-failure results](evidence/components/process-faults-final.json), [worker-recording inspection](evidence/components/worker-kill-inspection.jsonl), and [parent-recording inspection](evidence/components/parent-kill-inspection.jsonl).

All acceptance processes exited, no recording worker remained, the camera was unoccupied, and the desktop display service was restored to active. The production deployment was unchanged. Historical raw log, CSV, and video locations were local `.docker-artifacts/recording-validation/diagnostic/` and the corresponding `*-final-20260924` directories under production-board `/home/hzhy/drone-recording-validation-20260923/`. Videos were not committed as ordinary source. The local artifact directory is disposable; these paths do not guarantee continuing availability.

## Usage and limits at delivery

Recording was disabled by default in the accepted delivery. SC233HGS used `need_save=false` with `recording.enabled=true` for independent recording; defaults were 12 Mbps, 60-second segments, and 1000 ms synchronization. Persistent storage, space protection, evidence files, and rollback by disabling recording are described in the [platform guide](../../../../src/platform/README.md). That live guide describes the current system; this report retains the settings of its historical delivery.

No power interruption, unplugging, reboot, collision, or production-disk fill was performed. This report makes no maximum-loss guarantee for real power failure or recovery after hardware damage. The user removed the three field-image-quality scenarios and bitrate comparisons from scope. Reliable power instrumentation was unavailable, so endurance was not validated. The production kernel did not provide per-process I/O byte counters; partition writes include other processes. clang-tidy/clangd and the Jetson environment were unavailable. Five-minute success does not establish long-term stability or zero performance loss.

## Specification and implementation reconciliation

The recording delta's four capabilities had been synchronized to the main specifications. Startup warmup was synchronized first and its existing requirements/scenarios were preserved. HDMI OSD displayed independently; recording obtained unannotated NV12 at the capture boundary. Parameter defaults, space protection, three-second preparation/close budgets, two-second IDR monitoring, and five-second persistence monitoring matched that implementation. Existing regressions covered the six business topics, four tracking-state values, and legacy bag semantics. Main-spec validation passed 19/19 and strict change validation passed. This validation step did not automatically archive the change.

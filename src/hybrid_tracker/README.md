# Shared hybrid tracker

[Project overview](../../README.md) · [Documentation index](../../docs/README.md)

This directory implements the shared detection and tracking algorithm. `HybridTracker` receives model backends, preprocessing, and frame-compatibility checks through `TrackerComponents`. Platform implementations own hardware SDK dependencies.

Read the [algorithm review](../../docs/architecture/ALGORITHM_REVIEW.md) for the current update sequence, detection scheduling, center fusion, recovery, and output semantics. The [architecture guide](../../docs/architecture/RK3576_REFACTORING_PLAN.md) explains ownership and platform boundaries. The [platform guide](../platform/README.md) covers component creation and runtime integration.

## Source entry points

| File | Responsibility |
|---|---|
| [hybrid_tracker.cpp](hybrid_tracker.cpp) | Per-frame state, recovery, detection/tracking arbitration, and output |
| [yolo_detector.cpp](yolo_detector.cpp) | Asynchronous detection requests, results, and decoding |
| [nano_tracker.cpp](nano_tracker.cpp) | NanoTrackV3 image features and bounding-box estimation |
| [dcf_tracker.cpp](dcf_tracker.cpp) | Center fusion using the optional grayscale correlation filter or separate center Kalman filter |
| [rotation_kf.cpp](rotation_kf.cpp) | Rotation-compensated filtering and historical correction |
| [target_los.h](target_los.h) | Camera-to-target direction and angular-rate estimation |

## Portable verification

The standalone configuration requires a C++17 compiler, CMake, OpenCV development libraries, and threads. From the repository root:

```bash
cmake -S src/hybrid_tracker -B /tmp/drone-tracker-contracts \
  -DDRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON
cmake --build /tmp/drone-tracker-contracts
ctest --test-dir /tmp/drone-tracker-contracts --output-on-failure
```

These checks use the shared tracker with injected dependencies. They do not load actual models or access cameras, NPU devices, or HDMI. Use the [build guide](../../dockerfiles/README.amd64.md#native-portable-checks) for container setup and the [regression tools](../../tests/tracking_regression/README.md) for recording-based comparisons.

## Verified-output permission

Initialization starts provisional tracking. A fresh detection from a later image must uniquely match the same attempt's tracking measurement on that image before output can become valid. Each one-call detector result retains surviving target-class candidates from one region, whole-image, or tile. Searching retains highest-score seed selection. Confirmation continues beyond unrelated regional positives; partial misses do not establish absence.

Every normal update preserves source snapshot, one result read, current submission or interval skip, then saved-result arbitration. Provisional acquisition offers every image; verified tracking uses configurable `yolo_interval`. Outstanding requests retain bounded value-only replay evidence. Models and image history belong to the worker. `OutputValidity` holds only short-locked values and pending loss identities for independent expiry and final publication checks.

## Scheduler and recovery examples

A stage job is one completed model call. A plan stores one source image and its remaining regions, rather than queuing one cascade per incoming image. One worker serves two targeted checks then one discovery stage when both remain eligible. The bounds are one active call, one shared latest-image mailbox, one discovery plan, one protected unread result, and one locally taken result. Taking a result releases capacity before initialization or arbitration. An empty partial stage does not require full-decision feedback. A positive Searching plan alone pauses for short seed disposition. OFF wakes waits and joins device use before releasing buffers.

For example, whole-image D10 starts on F10. F11 is pending and F12 replaces it. D10's next tiles still use F10. A first targeted miss can start one fallback on its own image; later-image misses cannot accumulate plans or mix coverage. Newer agreement cancels older discovery, and a superseded adverse result stays neutral. Tile order rotates when a cancelled plan restarts.

Cancellation also removes a cancelled plan's adverse authority from a call already executing or a result waiting unread. The call finishes with its original source, job and candidates. Bounded value-only cancellation records are sampled when publishing and taking the result; cancelled coverage cannot become a no-target/disagreement verdict or spend the adverse budget. A separately eligible positive can still support seed or unique same-image confirmation checks.

If D10 is accepted, the tracker allocates B and sends `SeedAccepted` before template preparation. An already submitted F20 can then run during catch-up. D20 verifies only when B independently produced T20 on that image. Seed feedback grants no publication permission and never rewrites original request metadata.

Seed eligibility and confirmation eligibility differ. If A expires near F35 and undisposed D32 arrives at F42, original pixels and valid source/clock checks may allow D32 to initialize B while Searching. B replays buffered images and starts with count zero. A fresh D41 submitted before initialization can verify B only through B's T41. Old A measurements, D32 itself, duplicate jobs, and substituted newer images cannot verify B. An active provisional or verified B is protected from old-seed replacement.

There is no no-result frame-count cleanup. Updates 15/16 or 60/61 do not abandon an unexpired provisional attempt. The elapsed candidate deadline, distinct usable adverse-plan budget `lost_max_count`, actual failures, and round/service shutdown retain their separate roles. Confirmation expires independently, including during input or inference stalls. This adds no detector-health restart policy.

Targeted regions remain untrusted search hints. Crop effectiveness must be evaluated on exact pixels and fixed model/profile identities before interpreting completion rates as useful confirmation. Crop-only, scheduling-only, and combined experiments retain separate evidence and do not establish tracking accuracy without labels.

The evaluated targeted hint retains the successful region's size/aspect and recenters it at the current genuine measurement. It retains producing job/source and attempt provenance, supplies no permission, and resets on abandonment. The November 12 formal-profile exact-pixel comparison returned positives in 1/12 current-320 regions, 1/12 exact prior regions, and 2/12 recentered regions. Four initial samples were all negative. This limited evidence does not establish accuracy or a performance improvement; scheduler-only and combined board measurements retain separate identities.

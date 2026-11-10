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

Initialization starts provisional tracking. A fresh detection from a later image must uniquely match the same attempt's tracking measurement on that image before output can become valid. The detector returns surviving target-class candidates from its first successful region, whole-image, or tile stage. It retains highest-score seed selection without claiming that other regions were searched.

Every normal update preserves source snapshot, one result read, current submission or interval skip, then saved-result arbitration. Provisional acquisition offers every image; verified tracking uses configurable `yolo_interval`. Outstanding requests retain bounded value-only replay evidence. Models and image history belong to the worker. `OutputValidity` holds only short-locked values and pending loss identities for independent expiry and final publication checks.

# Proposal

## Why

Tracking can lose its output after the tracker becomes faster, even when YOLO finds a target, because a delayed detection is compared with a box from a different frame. Preserving the original comparison data and refreshing queued detector input should reduce these avoidable disruptions without increasing the image-history capacity.

## What Changes

- Preserve source-frame box metadata independently of image pixels for every outstanding YOLO request. An outstanding request is accepted and still pending, executing, or awaiting result disposition.
- Handle missing source metadata and unavailable recovery images explicitly. Never substitute a current-frame box for a missing source-frame comparison.
- Replace an unstarted pending request with the newest eligible input. Keep one executing request and at most one pending request.
- Record superseded requests, metadata availability, image availability, and arbitration outcomes so the regression tools can reconstruct each decision.
- Preserve the current watchdog exactly. It counts tracking updates without a result, resets when any result is read, and uses the existing threshold and timeout action.
- Defer a maximum result-age limit. Capture times and observed delay remain evidence, not a new rejection threshold.

Concrete example: request 62 in the recorded candidate run uses tracker frame 744. Its result is read at frame 777, after image history has advanced to 748–777. Today the old detection is compared with the current box and gets intersection-over-union (IoU, overlapping area divided by combined area) 0. With this change, retained source metadata permits a comparison against frame 744. The recorded source box gives approximately 0.584, above the existing 0.4 boundary. Missing pixels still prevent image-based recovery. This reconstruction illustrates the mechanism; it is not a measured result from an implementation.

## Capabilities

### New Capabilities

- `detection-tracking-arbitration`: Source-frame comparison metadata, explicit handling of unavailable context, bounded replacement of pending detector input, and compatibility with the existing watchdog and tracker states.

### Modified Capabilities

- `rk3576-tracking-regression`: Reconstruct superseded requests and separate metadata misses from image-history misses in version comparisons.

## Impact

- Production algorithm boundaries: `src/hybrid_tracker/hybrid_tracker.cpp`, `hybrid_tracker.h`, `yolo_detector.cpp`, and `yolo_detector.h`.
- Diagnostic protocol and readers under `src/diagnostics/`; regression collection, metrics, reports, and contract documentation under `tests/tracking_regression/`.
- Existing injectable model tests in `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp` and `preprocess_contract_test.cpp` provide controlled delayed-result and cleanup coverage.
- Request admission intentionally changes from preserving the first pending input to preserving the latest eligible input. Source-frame arbitration intentionally changes when image history is missing. Cleanup and diagnostics remain behavior-neutral beyond these authorized changes.
- Keep models, preprocessing, detector search stages, confidence thresholds, submission cadence, image-buffer capacity, ROS interfaces, and state numeric values unchanged.
- Synchronous initialization and historical replay remain synchronous. This change does not promise a bound on publication gaps or an accuracy improvement.
- Implementation requires deterministic concurrency tests and a new controlled comparison on the same production board. No source changes, builds, deployment, or board runs are part of this planning task.

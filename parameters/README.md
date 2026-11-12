# RK3576 configuration

[Project overview](../README.md) · [Documentation index](../docs/README.md)

This directory contains ROS parameter templates. Copy a template into your own configuration before filling model paths, input-image calibration, and the actual camera topology.

| Template | Input and defaults |
|---|---|
| [rk3576_video_params.yaml](rk3576_video_params.yaml) | Local MJPEG video; model paths and calibration must be supplied |
| [rk3576_ar0234_camera_params.yaml](rk3576_ar0234_camera_params.yaml) | AR0234 camera; software triggering by default; model paths must be supplied |
| [rk3576_sc233hgs_production_params.yaml](rk3576_sc233hgs_production_params.yaml) | SC233HGS continuous capture; production paths and display/recording defaults require review for the actual board |

Camera calibration values in the camera templates are simulated examples. They do not establish measured angular accuracy. Model, capture, clock, display, and recording settings are fixed at startup; restart the node after changing them. The [platform guide](../src/platform/README.md#camera-sources-and-configuration) explains supported fields and constraints.

RK3576 target observations use `publish_prediction=false` and `los.rate_filter_tau=0.12` by default. Both are startup-only parameters. Intrinsics remain explicit input-pixel values, independent of HDMI dimensions. See [platform observations](../src/platform/README.md#rk3576-paired-target-observations) for time modes, message fields, QoS and bag migration.

## Fresh verification and startup deadlines

`tracker.confirmation_timeout_ms` (100), `tracker.candidate_timeout_ms` (1000), and `tracker.expiry_check_interval_ms` (10) are positive integer startup-only settings, in milliseconds. The check interval must not exceed confirmation lifetime. Defaults are engineering starting values, not validated flight limits. Runtime edits require restarting the node. Standalone replay YAML uses the same names inside its `tracker` map and serializes their effective values.

An initialized track stays provisional until a fresh later-image detection uniquely agrees with that attempt's measurement on the same image. Confirmation expires from the image's immutable evidence anchor, including inference and later stalls. `lost_max_count` counts distinct usable adverse completed plans or closing ambiguities and abandons unsuccessful provisional attempts; it no longer delays the first invalidation. Legacy `max_position_jump`, `max_size_jump_ratio`, and `jump_tolerance` are accepted but inactive. Appearance and motion validators report `not_evaluated`.

RK3576 production camera, AR0234, video, FP16 replay, and INT8 replay templates start with `yolo_interval: 1`. Other positive intervals remain configurable. Searching and provisional acquisition offer each processed image; verified tracking follows the interval. More submission opportunities do not promise more completed inferences. Existing replay FPS values remain unchanged. See the [algorithm clock examples](../docs/architecture/ALGORITHM_REVIEW.md#45-separate-evidence-and-observation-clocks).

The former no-result frame-count cleanup is inactive. Detector silence across updates 15/16 at interval 1 or 60/61 at interval 4 cannot abandon a provisional attempt before `candidate_timeout_ms`. That deadline starts at seed acceptance and is checked at the worker's first servicing opportunity. Confirmation expiry remains independent of frame count and inference completion. Actual execution/geometry failures and explicit shutdown remain separate cleanup paths; no detector-health timeout or restart setting is introduced.

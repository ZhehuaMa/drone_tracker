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

# RK3576 configuration

RK3576 target observations use `publish_prediction=false` and `los.rate_filter_tau=0.12` by default. Both are startup-only parameters. Intrinsics remain explicit input-pixel values, independent of HDMI dimensions. See [platform observations](../src/platform/README.md#rk3576-paired-target-observations) for time modes, message fields, QoS and bag migration.

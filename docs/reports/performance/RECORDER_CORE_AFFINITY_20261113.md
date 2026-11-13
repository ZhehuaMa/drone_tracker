# Topic-recorder CPU placement on the production RK3576 — 2026-11-13

**Recommendation: CPU 2 for the two topic-recorder threads under this measured workload.** It preserved normal detection speed while giving the strongest replay throughput among the A53 placements. Keeping the recorder on this core also leaves the busy A72 tracking, detection, display, and decoding cores available for their existing work. CPU 0 was close; this single-pass experiment does not establish a statistically significant advantage over CPU 0.

Eight sequential CPU-placement tests were run, each by a new subagent. Each test included a live-camera phase and two balloon-video phases. This gives 24 measured phases. All phases used the existing uncommitted `centralize-rk3576-session-topic-recording` implementation. Production services and installed configuration were restored after measurement; this experiment did not install a persistent affinity change.

The main tradeoffs observed in this comparison were:

- CPU 4: long-replay throughput was 40.67 FPS versus CPU 2’s 48.47 FPS.
- CPU 5: tracking throughput was highest, but long-replay detection-job p95 increased to 485 ms versus CPU 2’s 310 ms. Camera detection slowed too, from about 3.66 to 2.30 jobs/s.
- CPU 6: tracker throughput stayed near 48 FPS, but HDMI display throughput fell to 37.67 FPS versus CPU 2’s 47.75 FPS on the long replay. Camera HDMI throughput fell to 47.38 FPS.
- CPU 7: the decoder and recorder shared a core that averaged 98.29% busy on the long replay.
- CPU 2: all per-core means remained below 73% across its three phases. Both topic-recorder threads used 62.75–66.53% of one A53 core. CPU 1’s higher measured load includes the observer fixed on that core.

## What was measured

Processed FPS counts frames consumed by the tracker per actual steady second. It is not the completed YOLO detection rate; detection runs asynchronously. A steady window excludes the configured startup warmup and the final 0.3 seconds of a round. A bbox–LOS pair contains a bounding box and the line-of-sight observation for the same reference timestamp. LOS describes target direction and angular rates. Pair frequency below counts both native ROS publication calls completing successfully. It is not proof that every remote subscriber received the pair. Valid pairs require both messages to be valid.

The camera had no intentional target. Its zero valid-pair frequency is expected. Live-camera Tracking with an actual target was not measured; target tracking was exercised through the video clips. The longer replay provides sustained Tracking with real models. The real-flight clip includes detection and target-loss periods, so valid-pair frequency and Tracking occupancy also depend on the asynchronous algorithm state. Higher total FPS alone does not establish better target tracking.

## Throughput and output pairs

| Recorder CPU | Camera FPS | Long replay FPS | Flight FPS | Camera valid Hz | Long valid Hz | Flight valid Hz |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 60.03 | 48.22 | 58.46 | 0.00 | 47.85 | 24.90 |
| 1 | 60.01 | 47.70 | 58.57 | 0.00 | 47.43 | 18.89 |
| 2 | 60.01 | 48.47 | 58.84 | 0.00 | 48.35 | 25.14 |
| 3 | 60.01 | 46.98 | 58.77 | 0.00 | 45.66 | 22.23 |
| 4 | 60.02 | 40.67 | 56.18 | 0.00 | 40.02 | 21.51 |
| 5 | 60.02 | 49.40 | 59.29 | 0.00 | 49.38 | 35.20 |
| 6 | 60.04 | 47.73 | 58.56 | 0.00 | 47.75 | 21.28 |
| 7 | 60.02 | 47.54 | 58.28 | 0.00 | 44.75 | 32.52 |

The following table shows total, valid, and invalid pair frequencies separately. Small differences from processed FPS can occur at the measurement-window boundaries. It also retains the measured state mixture.

| CPU | Input | Steady s | All pair Hz | Valid Hz | Invalid Hz | Tracking % | HDMI FPS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Live camera | 51.71 | 60.03 | 0.00 | 60.03 | 0.0 | 60.02 |
| 0 | Long balloon replay | 51.70 | 48.22 | 47.85 | 0.37 | 99.0 | 47.43 |
| 0 | Real-flight replay | 37.87 | 58.46 | 24.90 | 33.56 | 41.1 | 57.99 |
| 1 | Live camera | 51.71 | 60.01 | 0.00 | 60.01 | 0.0 | 60.02 |
| 1 | Long balloon replay | 51.70 | 47.70 | 47.43 | 0.27 | 99.2 | 46.80 |
| 1 | Real-flight replay | 37.90 | 58.54 | 18.89 | 39.65 | 31.1 | 57.83 |
| 2 | Live camera | 51.70 | 60.01 | 0.00 | 60.01 | 0.0 | 60.02 |
| 2 | Long balloon replay | 51.71 | 48.47 | 48.35 | 0.12 | 99.6 | 47.75 |
| 2 | Real-flight replay | 37.63 | 58.84 | 25.14 | 33.70 | 41.6 | 58.43 |
| 3 | Live camera | 51.70 | 60.01 | 0.00 | 60.01 | 0.0 | 60.02 |
| 3 | Long balloon replay | 51.71 | 46.98 | 45.66 | 1.32 | 97.3 | 45.83 |
| 3 | Real-flight replay | 37.96 | 58.77 | 22.23 | 36.54 | 36.8 | 58.13 |
| 4 | Live camera | 51.70 | 60.02 | 0.00 | 60.02 | 0.0 | 60.02 |
| 4 | Long balloon replay | 51.70 | 40.69 | 40.02 | 0.68 | 97.6 | 40.73 |
| 4 | Real-flight replay | 37.85 | 56.28 | 21.51 | 34.77 | 38.3 | 55.19 |
| 5 | Live camera | 51.70 | 60.02 | 0.00 | 60.02 | 0.0 | 60.00 |
| 5 | Long balloon replay | 51.70 | 49.42 | 49.38 | 0.04 | 99.8 | 48.77 |
| 5 | Real-flight replay | 37.73 | 59.31 | 35.20 | 24.12 | 58.7 | 59.14 |
| 6 | Live camera | 51.70 | 60.02 | 0.00 | 60.02 | 0.0 | 47.38 |
| 6 | Long balloon replay | 51.70 | 47.75 | 47.75 | 0.00 | 100.0 | 37.67 |
| 6 | Real-flight replay | 37.82 | 58.56 | 21.28 | 37.28 | 34.9 | 49.40 |
| 7 | Live camera | 51.70 | 60.02 | 0.00 | 60.02 | 0.0 | 60.02 |
| 7 | Long balloon replay | 51.70 | 47.56 | 44.75 | 2.80 | 94.0 | 46.24 |
| 7 | Real-flight replay | 37.77 | 58.33 | 32.52 | 25.82 | 53.5 | 57.02 |

## CPU utilization on all eight cores

Each number is the time-weighted mean busy percentage of one physical CPU. Busy excludes idle and I/O wait. CPUs 0–3 are Cortex-A53 and CPUs 4–7 are Cortex-A72; equal busy percentages across these groups do not represent equal computing capacity. The selected recorder CPU is the row, while the measured physical core is the column. Full p95, peaks, CPU components, frequencies, and per-thread counters are in `analysis.json`.

### Live camera

| Recorder CPU | CPU 0 | CPU 1 | CPU 2 | CPU 3 | CPU 4 | CPU 5 | CPU 6 | CPU 7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 74.8% | 53.1% | 42.9% | 43.8% | 12.8% | 72.4% | 31.0% | 21.6% |
| 1 | 40.5% | 91.6% | 39.6% | 39.8% | 13.1% | 72.3% | 31.6% | 21.7% |
| 2 | 39.7% | 51.6% | 72.9% | 39.2% | 13.0% | 72.4% | 33.0% | 21.5% |
| 3 | 39.5% | 51.7% | 39.8% | 73.1% | 12.9% | 72.6% | 34.4% | 21.7% |
| 4 | 30.2% | 39.5% | 29.7% | 29.8% | 52.8% | 72.4% | 32.7% | 22.2% |
| 5 | 29.6% | 38.5% | 28.7% | 29.3% | 12.9% | 84.7% | 31.1% | 20.4% |
| 6 | 29.7% | 39.8% | 29.0% | 30.0% | 14.6% | 72.9% | 77.7% | 22.9% |
| 7 | 30.1% | 40.2% | 29.3% | 29.6% | 14.1% | 72.6% | 34.8% | 58.9% |

### Long balloon replay

| Recorder CPU | CPU 0 | CPU 1 | CPU 2 | CPU 3 | CPU 4 | CPU 5 | CPU 6 | CPU 7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 71.0% | 49.6% | 37.6% | 36.8% | 55.3% | 64.4% | 27.4% | 57.1% |
| 1 | 35.6% | 90.5% | 33.8% | 34.5% | 55.2% | 65.4% | 27.4% | 56.9% |
| 2 | 37.4% | 50.2% | 68.7% | 34.8% | 54.8% | 64.9% | 27.6% | 57.1% |
| 3 | 37.2% | 49.7% | 34.9% | 68.6% | 55.5% | 66.2% | 28.4% | 57.9% |
| 4 | 27.1% | 36.7% | 27.0% | 27.5% | 80.6% | 60.0% | 24.0% | 58.4% |
| 5 | 27.7% | 37.1% | 27.3% | 27.4% | 54.4% | 81.1% | 27.9% | 58.4% |
| 6 | 27.7% | 37.5% | 27.2% | 27.9% | 58.1% | 65.3% | 73.7% | 58.0% |
| 7 | 28.3% | 37.9% | 27.1% | 27.3% | 54.9% | 65.2% | 28.8% | 98.3% |

### Real-flight replay

| Recorder CPU | CPU 0 | CPU 1 | CPU 2 | CPU 3 | CPU 4 | CPU 5 | CPU 6 | CPU 7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 68.8% | 46.6% | 35.5% | 36.3% | 29.8% | 71.9% | 30.2% | 51.7% |
| 1 | 34.6% | 85.2% | 33.4% | 34.3% | 25.5% | 71.8% | 30.0% | 52.2% |
| 2 | 36.4% | 48.3% | 68.1% | 35.1% | 29.1% | 71.9% | 31.1% | 51.5% |
| 3 | 35.6% | 47.1% | 35.6% | 67.6% | 27.4% | 71.9% | 29.6% | 52.2% |
| 4 | 27.2% | 36.6% | 26.4% | 27.3% | 64.5% | 72.3% | 28.7% | 52.1% |
| 5 | 26.9% | 35.4% | 27.3% | 26.6% | 34.9% | 83.9% | 30.8% | 51.2% |
| 6 | 27.1% | 36.5% | 26.9% | 26.8% | 30.6% | 72.2% | 74.7% | 53.0% |
| 7 | 27.3% | 37.2% | 27.2% | 27.2% | 36.4% | 72.5% | 32.3% | 93.6% |

## Processing latency

A 95th percentile, or p95, bounds 95% of observed calls. Selection-to-pair latency runs from the native frame-selection event to successful completion of both output publication calls on the same monotonic clock. It includes frame handoff, tracker processing, and publication. Update latency measures only the tracker update call. Detection-job latency measures one asynchronous YOLO job, which can contain several model invocations. None is pure execution time on the neural processing unit (NPU).

| CPU | Input | Update p50 / p95 / p99 ms | Select-to-pair p95 / p99 ms | Detection p95 ms | YOLO jobs/s |
| --- | --- | --- | --- | --- | --- |
| 0 | Live camera | 0.025 / 0.041 / 0.048 | 0.509 / 0.984 | 277.2 | 3.66 |
| 0 | Long balloon replay | 9.881 / 53.903 / 118.206 | 54.196 / 118.470 | 306.1 | 6.65 |
| 0 | Real-flight replay | 0.038 / 10.533 / 11.389 | 10.782 / 11.584 | 309.6 | 3.30 |
| 1 | Live camera | 0.026 / 0.041 / 0.049 | 0.485 / 0.751 | 278.5 | 3.62 |
| 1 | Long balloon replay | 9.911 / 69.811 / 119.222 | 70.000 / 119.458 | 310.6 | 6.32 |
| 1 | Real-flight replay | 0.032 / 10.481 / 11.200 | 10.733 / 11.470 | 309.2 | 3.35 |
| 2 | Live camera | 0.026 / 0.041 / 0.049 | 0.502 / 0.872 | 275.1 | 3.66 |
| 2 | Long balloon replay | 9.814 / 53.262 / 118.301 | 53.509 / 118.516 | 309.9 | 6.54 |
| 2 | Real-flight replay | 0.039 / 10.476 / 11.045 | 10.751 / 11.314 | 311.0 | 3.27 |
| 3 | Live camera | 0.026 / 0.041 / 0.049 | 0.484 / 0.946 | 276.7 | 3.64 |
| 3 | Long balloon replay | 9.934 / 70.904 / 124.669 | 71.153 / 124.905 | 313.6 | 6.30 |
| 3 | Real-flight replay | 0.034 / 10.582 / 11.315 | 10.842 / 11.589 | 310.0 | 3.29 |
| 4 | Live camera | 0.025 / 0.040 / 0.055 | 0.621 / 1.198 | 275.1 | 3.66 |
| 4 | Long balloon replay | 15.336 / 49.757 / 162.929 | 50.319 / 163.527 | 307.8 | 5.92 |
| 4 | Real-flight replay | 0.033 / 17.916 / 22.147 | 19.231 / 23.540 | 311.4 | 3.30 |
| 5 | Live camera | 0.025 / 0.037 / 0.046 | 0.481 / 0.623 | 453.0 | 2.30 |
| 5 | Long balloon replay | 9.724 / 11.850 / 170.036 | 12.073 / 170.272 | 485.0 | 5.14 |
| 5 | Real-flight replay | 9.179 / 10.409 / 10.877 | 10.642 / 11.140 | 502.7 | 2.04 |
| 6 | Live camera | 0.026 / 0.044 / 0.050 | 0.509 / 0.880 | 277.8 | 3.64 |
| 6 | Long balloon replay | 10.149 / 63.058 / 123.928 | 63.246 / 124.155 | 307.4 | 6.69 |
| 6 | Real-flight replay | 0.033 / 10.877 / 11.718 | 11.127 / 11.959 | 310.4 | 3.38 |
| 7 | Live camera | 0.024 / 0.039 / 0.047 | 1.239 / 1.735 | 277.1 | 3.62 |
| 7 | Long balloon replay | 9.966 / 63.449 / 118.566 | 63.660 / 118.770 | 308.8 | 6.36 |
| 7 | Real-flight replay | 9.330 / 10.895 / 12.103 | 11.168 / 12.343 | 314.5 | 3.18 |

Camera source timestamps already appeared roughly 1.6 seconds old when the application received frames. That driver timestamp/clock/acquisition boundary is uncalibrated. It must not be interpreted as tracker computation time or physical exposure-to-output latency. `camera-clock-investigation.json` records a similarly sized MONOTONIC-minus-RAW clock difference as an investigation clue, not proof of the driver clock source. No timestamp correction was applied. Source-timestamp ages, ROS publish-call durations, and display-submit timing remain available in the detailed analysis.

The Python measurement receiver falls behind native publications. Its arrival frequency and callback delay describe the measurement receiver, not the native output rate or production DDS transport latency. DDS is the middleware that carries ROS 2 messages. The report uses native paired publication evidence for output frequency. Detailed received counts and delays are retained for transparency.

## Recorder and other resources

| CPU | Input | Recorder CPU % | Node CPU % | Encoder CPU % | Fixture / observer CPU % | RSS MiB | Root writes MiB/s | Body IMU offered Hz |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Live camera | 67.2 | 192.7 | 9.0 | 96.0 / 21.1 | 230.9 | 1.82 | 129.8 |
| 0 | Long balloon replay | 64.5 | 266.5 | 0.0 | 95.6 / 19.8 | 230.2 | 1.78 | 135.3 |
| 0 | Real-flight replay | 63.2 | 243.6 | 0.0 | 95.5 / 18.3 | 244.9 | 1.60 | 136.9 |
| 1 | Live camera | 67.7 | 193.8 | 9.1 | 98.7 / 17.9 | 232.3 | 1.80 | 133.9 |
| 1 | Long balloon replay | 65.0 | 268.6 | 0.0 | 99.1 / 17.6 | 231.2 | 1.70 | 137.4 |
| 1 | Real-flight replay | 60.9 | 238.8 | 0.0 | 97.2 / 15.8 | 239.2 | 2.31 | 139.4 |
| 2 | Live camera | 66.5 | 194.1 | 9.0 | 96.3 / 20.9 | 232.0 | 1.82 | 132.4 |
| 2 | Long balloon replay | 63.7 | 266.0 | 0.0 | 95.3 / 20.1 | 229.3 | 1.05 | 134.8 |
| 2 | Real-flight replay | 62.7 | 242.3 | 0.0 | 95.0 / 18.5 | 237.5 | 2.28 | 136.0 |
| 3 | Live camera | 66.4 | 195.8 | 9.0 | 96.4 / 21.0 | 232.1 | 1.81 | 132.1 |
| 3 | Long balloon replay | 63.8 | 269.4 | 0.0 | 95.3 / 19.6 | 228.3 | 1.74 | 135.0 |
| 3 | Real-flight replay | 62.5 | 240.4 | 0.0 | 95.1 / 18.2 | 224.6 | 0.86 | 136.2 |
| 4 | Live camera | 38.8 | 167.8 | 9.4 | 94.7 / 16.1 | 229.8 | 1.90 | 149.3 |
| 4 | Long balloon replay | 33.6 | 220.8 | 0.0 | 94.8 / 14.6 | 235.7 | 1.70 | 154.7 |
| 4 | Real-flight replay | 35.1 | 214.8 | 0.0 | 94.3 / 14.5 | 224.6 | 2.05 | 153.0 |
| 5 | Live camera | 38.5 | 137.2 | 8.5 | 93.9 / 15.2 | 230.2 | 1.88 | 153.2 |
| 5 | Long balloon replay | 37.2 | 218.6 | 0.0 | 94.3 / 15.1 | 231.4 | 1.76 | 154.9 |
| 5 | Real-flight replay | 36.2 | 197.0 | 0.0 | 93.6 / 14.0 | 221.1 | 2.38 | 154.1 |
| 6 | Live camera | 47.8 | 175.2 | 9.4 | 94.5 / 15.9 | 230.9 | 1.82 | 150.8 |
| 6 | Long balloon replay | 46.3 | 251.7 | 0.0 | 94.7 / 15.5 | 230.9 | 0.76 | 154.3 |
| 6 | Real-flight replay | 44.6 | 227.7 | 0.0 | 94.2 / 14.8 | 240.2 | 2.34 | 154.7 |
| 7 | Live camera | 37.2 | 167.8 | 9.1 | 94.5 / 16.3 | 229.9 | 1.86 | 149.5 |
| 7 | Long balloon replay | 39.9 | 244.4 | 0.0 | 94.9 / 15.4 | 229.9 | 1.76 | 152.3 |
| 7 | Real-flight replay | 39.9 | 229.9 | 0.0 | 94.1 / 14.5 | 240.5 | 2.37 | 151.4 |

100% process CPU means one fully occupied core. Recorder CPU sums the exact topic callback executor and bag-writer threads. Node CPU includes these threads. Encoder CPU is the independent original-camera video process. Fixture and observer costs are material and are included in the all-core utilization tables. The fixture can use CPUs 0–3; the observer is fixed on CPU 1. Measurements therefore include benchmark contention on the A53 cores. Root writes are shared block-device counters and are not per-process attribution or durability proof. RSS is resident memory of the tracker process.

Maximum sampled temperature across all phases was 61.00 °C. Detailed frequency samples and cooling-device readbacks are retained in the analysis. These observations do not prove that no unobserved thermal throttling occurred.

All **80 session bags** closed complete and passed full readback. **844,441 accepted messages were all written.** Reported queue drops, middleware losses, and accepted-but-unwritten messages were all zero. These counters do not prove the middleware can detect every possible loss. Policy sampling is intentional and is accounted separately. Every phase had zero published ROS debug images, no diagnostic event gaps, and complete measurement arrays. Complete counters and queue peaks are retained per phase.

Independent camera-video completion metadata and accounting are shown below. The encoded TS files were retained but were not fully decoded for a separate video-quality check. Cancelled frames were excluded before encoder acceptance during lifecycle transitions. They are different from accepted frames lost by the encoder.

| Recorder CPU | Accepted / encoded frames | Dropped | Cancelled | Camera sequence gaps (whole stream) |
| --- | --- | --- | --- | --- |
| 0 | 3593 / 3593 | 0 | 0 | 1 |
| 1 | 3592 / 3592 | 0 | 1 | 0 |
| 2 | 3593 / 3593 | 0 | 0 | 0 |
| 3 | 3592 / 3592 | 0 | 0 | 0 |
| 4 | 3591 / 3591 | 0 | 0 | 0 |
| 5 | 3593 / 3593 | 0 | 0 | 0 |
| 6 | 3592 / 3592 | 0 | 0 | 0 |
| 7 | 3582 / 3582 | 0 | 0 | 0 |

Sequence-gap counters cover camera warmup and the active stream; they do not identify the steady-window loss rate by themselves. Warmup sequence observations are retained in each camera session.

## Fixed platform, inputs, and procedure

- Board: hostname `hzhy`, `HZ-RK3576_WuKong Board`, Ubuntu 22.04.5 ARM64, kernel `6.1.141-rt36`, approximately 3.8 GiB RAM, no swap, eMMC root storage. Current device, OS, toolchain, permission, and service snapshots are in `preflight.txt`.
- Source HEAD: `c47088f01c991d14f3e0b40980956cd3d3736847` plus the uncommitted recorder implementation. The 1,920-entry saved build-source comparison found no implementation differences; only the documentation index differed. Executable SHA-256: `8fbad8fc5ccce24b20877ea0a68769200febf99ada89f0aafd6875f5b4c2ad37`.
- System RKNN Runtime 2.3.2 SHA-256: `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`. Driver: 0.9.8. The existing system library was used and left unchanged.
- Live camera: SC233HGS, 1920×1200, nominal 60 Hz, NV12 from `/dev/video22`. Device topology and capture format were checked before camera phases. Original-camera video recording was enabled.
- Long replay: `fixed-balloon.avi`, 3,900 MJPEG frames, 1920×1200 at nominal 60 Hz. SHA-256: `c25aec33208f5dfca315eb9c2c4fb86b04d98883ec5d74b68f5ac7746a669a95`. Its original acquisition chain is unavailable; this is not authenticated continuous flight footage or a target-accuracy benchmark.
- Real-flight replay: `nov4-round2-first8s.avi`, 480 MJPEG frames, 1920×1200 at nominal 60 Hz, derived from the November 4 production recording. SHA-256: `ac506822b26499863d1fc50af1324fbc29afd3fa76c82e67d03ce74a452f9ae4`. No artificial target observations were injected.
- Four existing approved model files were unchanged. Full model identities are in `board/models-inputs.sha256`. Camera intrinsics retain the existing uncalibrated production placeholders; this experiment does not validate LOS angular accuracy.
- Both dedicated topic-recorder threads were bound to the tested CPU. The shared `recording` role stayed on CPU 7 so the camera controller and video encoder did not move. Actual recorder masks were checked throughout steady windows, including each new writer in the short flight rounds. Other managed thread masks stayed fixed.
- Control/tracking=4, YOLO=5, general ROS transport/display=6, capture/decoder/original-camera recording=7. HDMI, real inference, basic diagnostics, and bag recording were enabled. `visualized_result=false` throughout. The camera-only encoder was disabled for video-file input.
- Camera and long replay each requested 60 active seconds, excluding eight seconds of warmup and the final 0.3 seconds. Flight used eight 6.5-second rounds, excluding 1.5 seconds of warmup and the final 0.3 seconds per round. Actual monotonic windows determine the denominators.
- Every phase used a randomly selected ROS domain and localhost-only discovery. Sequential phases fully shut down before the next. Domain values are retained in `comparison.csv`; a repeated random value across completed phases does not create overlap.
- The tracker, diagnostics, bridge, and interceptor services were stopped during measurement. Typed synthetic telemetry supplied recorder load; their business computations are outside this experiment. The requested body IMU rate was 200 Hz, but the fixture achieved the actual offered rates above. This is not proof of capacity at 200 Hz.
- Two preliminary camera qualifications were excluded: one exposed the shared camera/topic recording role, and another exposed missing `/proc/.../children` support in this kernel. Their failures and evidence are retained. Accepted phases use one frozen workload/observer/readback harness identified by `HARNESS.sha256`. CPU 0 evidence transfer ended before CPU 1’s actual steady window; the boundary audit is retained in `handoff-core0-core1.json`.

CPU 6 completed every measurement, export, transfer, and analysis stage. Its outer zsh logging wrapper then failed when assigning the reserved variable `status`. The wrapper exit was 1; all nine board-stage statuses were 0. Harness exit 0 is inferred from the completed fail-fast stages and final analysis, rather than separately captured. No measurement was rerun. Exact evidence is in `core6-command-status-evidence.json`.

The restored production node reports diagnostics storage error 28 (`ENOSPC`). Journal entries at 11:34:56–11:34:59, before benchmark maintenance, show the same existing error. All four services are active and camera preview is running, but the production diagnostics storage issue remains. Production recordings were preserved. This is separate from the healthy benchmark collectors. See `production-diagnostics-investigation.txt` and `final-board-health.txt`.

## Reproduction and evidence

Retained evidence root: `/home/mzh/nickluo/validation-retained/recorder-core-sweep-20261113`. It includes the executable package, source snapshot and comparison, input clips, exact scripts, effective phase configurations, native diagnostics, per-core/thread samples, received observations, SQLite bags, readbacks, analysis, and command exit statuses. Board staging was `/var/lib/drone_tracker/recorder-core-sweep-20261113`. Completed board phase copies were removed only after every regular file matched the retained host copy by SHA-256; `pruned-board-copy-*.json` manifests record this check. The retained host evidence is outside disposable build caches.

The retained scripts use exclusive phase directories. Do not rerun them over these evidence paths. For a new experiment, choose fresh host and board roots, preserve service states, and prepare the board as recorded in `METHOD.md`. The following commands describe the actual eight completed tests:

```bash
root=/home/mzh/nickluo/validation-retained/recorder-core-sweep-20261113
bash "$root/run-core.sh" 0  # exit 0
bash "$root/run-core.sh" 1  # exit 0
bash "$root/run-core.sh" 2  # exit 0
bash "$root/run-core.sh" 3  # exit 0
bash "$root/run-core.sh" 4  # exit 0
bash "$root/run-core.sh" 5  # exit 0
bash "$root/run-core.sh" 6  # stages completed; harness exit 0 inferred; outer logger exit 1
bash "$root/run-core.sh" 7  # exit 0
bash "$root/restore_board.sh"  # exit 0
/home/mzh/miniconda3/bin/python "$root/analyze_all.py"  # exit 0
/usr/bin/python3 "$root/verify_evidence.py"  # exit 0
```

All 412 aggregate verification checks and 5280 independent configuration/accounting/restoration checks passed. Host analysis used conda base Python. The independent configuration check used system Python because conda base did not provide PyYAML. Board commands sourced ROS Humble and used its matching system Python. All four originally active services were restored, protected installed checksums matched, and no benchmark processes remained; final restoration evidence is retained.

There is one accepted test per CPU placement and no randomized repeated placement or recording-disabled control. Differences combine CPU contention, asynchronous state variation, and the measured telemetry workload. Achieved telemetry input rates also differ between placements because the Python source does not sustain every requested rate. This confounds a causal estimate of affinity cost at an identical offered load. Small differences do not establish a statistically reliable optimum. These results do not establish flight accuracy, physical exposure-to-output latency, maximum telemetry capacity, a whole-flight-stack CPU budget, or Jetson behavior.

## Recorder-disabled control and overhead comparison — 2026-11-13

The preceding sections describe the original eight tests with topic recording enabled. One additional test, run by a new subagent, now provides a recorder-disabled reference for the same three inputs. The camera phase and each video workload were tested once. This control was not repeated on each CPU.

The only deliberate application-setting difference is `need_save=false`; output paths use fresh directories. The source gates construction of `RecordingManager` on this setting. The control verified the effective parameter, the absence of the dedicated recorder executor and bag writers, empty recorder-only odometry/control subscriptions, and zero bag artifacts. HDMI keeps its legitimate body-IMU subscription. The camera controller and independent video encoder remain on CPU 7.

The binary, models, input files, other CPU roles, HDMI, basic diagnostics, telemetry publishers, receiver, observer, and window definitions match the earlier sweep. Original-camera video recording stays enabled for camera input and disabled for video files. The observer is byte-identical. The copied workload changes only recorder-specific guards and proof collection; its source diff is retained.

All paired values below are **disabled / enabled on CPU 2**. A delta is enabled minus disabled. A relative change divides that delta by the disabled value. CPU deltas use percentage points of one CPU: 100% means one fully occupied core.

The clearest observed cost was CPU time: whole-node CPU demand rose by 67.18–70.73 percentage points across the three inputs. The dedicated recorder threads themselves used 62.75–66.53% of one A53 core.

Long-replay throughput changed from 48.80 to 48.47 FPS (-0.68%). Its processing p95 changed from 41.82 to 53.51 ms. These are single-run observations, with the interpretation limits stated below.

### Throughput and observation output

| Input | Processed FPS off / on | FPS change | All pair Hz off / on | Valid pair Hz off / on | Tracking % off / on |
| --- | --- | --- | --- | --- | --- |
| Live camera | 60.02 / 60.01 | -0.01% | 60.02 / 60.01 | 0.00 / 0.00 | 0.0 / 0.0 |
| Long balloon replay | 48.80 / 48.47 | -0.68% | 48.80 / 48.47 | 48.80 / 48.35 | 100.0 / 99.6 |
| Real-flight replay | 58.35 / 58.84 | +0.84% | 58.35 / 58.84 | 25.09 / 25.14 | 41.4 / 41.6 |

Pair frequencies use native successful bbox and LOS publications with matching observation identities. They do not use the slower Python receiver. Camera valid output remains zero for the current no-target scene. Different Tracking occupancy reflects different asynchronous algorithm state; a change in valid-pair rate does not establish target accuracy.

### Processing, detection, and display

| Input | Selection-to-pair p95 ms off / on | Update p95 ms off / on | Detection-job p95 ms off / on | Detection jobs/s off / on | HDMI FPS off / on |
| --- | --- | --- | --- | --- | --- |
| Live camera | 0.452 / 0.502 | 0.039 / 0.041 | 270.0 / 275.1 | 3.73 / 3.66 | 60.02 / 60.02 |
| Long balloon replay | 41.823 / 53.509 | 41.649 / 53.262 | 302.1 / 309.9 | 6.98 / 6.54 | 48.01 / 47.75 |
| Real-flight replay | 10.724 / 10.751 | 10.506 / 10.476 | 302.5 / 311.0 | 3.28 / 3.27 | 56.97 / 58.43 |

Selection-to-pair latency uses the same monotonic clock from frame selection through completion of both publication calls. It excludes the uncalibrated camera source-timestamp offset. Update latency measures the tracker call, and detection latency measures a complete asynchronous job. They are different processing boundaries.

### CPU time, memory, and storage

| Input | Node CPU % off / on | Node CPU delta pp | Recorder CPU % on | RSS MiB off / on | Root writes MiB/s off / on | Body IMU offered Hz off / on |
| --- | --- | --- | --- | --- | --- | --- |
| Live camera | 123.4 / 194.1 | +70.73 | 66.5 | 212.9 / 232.0 | 1.74 / 1.82 | 155.9 / 132.4 |
| Long balloon replay | 195.6 / 266.0 | +70.43 | 63.7 | 224.5 / 229.3 | 1.67 / 1.05 | 157.8 / 134.8 |
| Real-flight replay | 175.2 / 242.3 | +67.18 | 62.7 | 212.7 / 237.5 | 2.21 / 2.28 | 158.5 / 136.0 |

The disabled recorder has zero thread CPU demand because its dedicated threads were proven absent. The enabled thread cost is measured directly from the executor and writer counters. The node-CPU difference also includes other node work and state variation. Root writes are shared block-device traffic, including diagnostics and camera video; they are not isolated bag-write costs. RSS is resident RAM of the tracker process.

| Input | Fixture CPU % off / on | Observer CPU % off / on | Camera encoder CPU % off / on |
| --- | --- | --- | --- |
| Live camera | 93.2 / 96.3 | 15.6 / 20.9 | 8.7 / 9.0 |
| Long balloon replay | 93.2 / 95.3 | 15.1 / 20.1 | 0.0 / 0.0 |
| Real-flight replay | 92.2 / 95.0 | 14.3 / 18.5 | 0.0 / 0.0 |

The instrumentation costs above are included in whole-board utilization. In particular, the synthetic publisher shares CPUs 0–3 with the enabled CPU 2 recorder, while the observer stays on CPU 1. Removing the recorder can change the achieved publisher rates and how its work spreads across those cores.

### Utilization of every physical CPU

These values are time-weighted mean busy percentages. Busy excludes idle and I/O wait. CPUs 0–3 are Cortex-A53 and CPUs 4–7 are Cortex-A72; equal percentages do not imply equal compute capacity.

**Live camera**

| Physical CPU | Disabled busy % | Enabled CPU-2 busy % | Delta pp |
| --- | --- | --- | --- |
| 0 | 29.63 | 39.70 | +10.06 |
| 1 | 39.06 | 51.65 | +12.59 |
| 2 | 28.81 | 72.92 | +44.11 |
| 3 | 29.31 | 39.19 | +9.88 |
| 4 | 13.71 | 12.97 | -0.74 |
| 5 | 72.20 | 72.36 | +0.16 |
| 6 | 28.75 | 33.00 | +4.25 |
| 7 | 20.74 | 21.47 | +0.73 |

**Long balloon replay**

| Physical CPU | Disabled busy % | Enabled CPU-2 busy % | Delta pp |
| --- | --- | --- | --- |
| 0 | 27.35 | 37.37 | +10.02 |
| 1 | 37.23 | 50.21 | +12.98 |
| 2 | 26.74 | 68.73 | +41.99 |
| 3 | 27.19 | 34.84 | +7.66 |
| 4 | 54.88 | 54.78 | -0.11 |
| 5 | 62.00 | 64.92 | +2.92 |
| 6 | 24.15 | 27.63 | +3.48 |
| 7 | 56.90 | 57.14 | +0.24 |

**Real-flight replay**

| Physical CPU | Disabled busy % | Enabled CPU-2 busy % | Delta pp |
| --- | --- | --- | --- |
| 0 | 26.78 | 36.40 | +9.62 |
| 1 | 35.50 | 48.32 | +12.83 |
| 2 | 26.81 | 68.13 | +41.32 |
| 3 | 26.70 | 35.10 | +8.40 |
| 4 | 30.37 | 29.05 | -1.32 |
| 5 | 71.63 | 71.91 | +0.29 |
| 6 | 25.89 | 31.12 | +5.23 |
| 7 | 51.21 | 51.47 | +0.26 |

### The same disabled reference compared with all eight placements

The table reuses the one disabled reference; it does not represent eight new disabled tests. Complete absolute values and deltas for every metric and placement are in the new `comparison.csv` and `comparison.json`.

| Enabled recorder CPU | Camera FPS change | Long replay FPS change | Flight FPS change | Camera detection p95 change | Long replay HDMI FPS change |
| --- | --- | --- | --- | --- | --- |
| 0 | +0.02% | -1.19% | +0.19% | +2.65% | -1.21% |
| 1 | -0.01% | -2.26% | +0.38% | +3.14% | -2.53% |
| 2 | -0.01% | -0.68% | +0.84% | +1.87% | -0.55% |
| 3 | 0.00% | -3.73% | +0.73% | +2.49% | -4.54% |
| 4 | 0.00% | -16.65% | -3.72% | +1.88% | -15.16% |
| 5 | 0.00% | +1.23% | +1.61% | +67.77% | +1.57% |
| 6 | +0.03% | -2.18% | +0.36% | +2.90% | -21.53% |
| 7 | 0.00% | -2.58% | -0.12% | +2.63% | -3.70% |

### Interpretation and retained evidence

These are observed differences between one later disabled run and the earlier enabled runs. There are no randomized repeated pairs or confidence intervals. The achieved telemetry rates and exact Tracking state mixture can differ. Consequently, the deltas are not an isolated causal estimate of recorder overhead at identical offered load. Small differences should be treated as measurement and run-to-run variation. The direct recorder thread CPU cost is separately identifiable.

Live-camera Tracking with a real target remains unmeasured. Both target replays use the original models and inputs. The camera timestamp and Python receiver-delay boundaries described above still apply.

New evidence root: `/home/mzh/nickluo/validation-retained/recorder-disabled-control-20261113`. It contains the copied and diffed harness, source comparison, fresh board/device/model/runtime preflight, all three workloads, native diagnostics, per-core/thread samples, effective parameters, proof of recorder absence, camera recording completion, service restoration, detailed analysis, and comparison files. The original sweep evidence at `/home/mzh/nickluo/validation-retained/recorder-core-sweep-20261113` was preserved.

The accepted full baseline command and its nine workload/absence/export stages all exited 0. All 67 subagent baseline checks passed. The four service states and protected system checksums match their initial snapshots, and no benchmark processes remain. The existing production diagnostics storage warning is separate from these healthy temporary collectors.

One pre-ON startup qualification exited 1 because its guard initially misclassified the legitimate HDMI body-IMU subscription. It was excluded before any tracking round. The guard was corrected to check recorder-only subscriptions; none of the accepted phases was rerun. `qualification-startup/` and `command-summary.json` retain the failure and accepted statuses.

Commands and actual exit statuses:

```bash
root=/home/mzh/nickluo/validation-retained/recorder-disabled-control-20261113
bash "$root/run-baseline.sh"  # exit 0; all three phases, exports, transfer and analysis
/usr/bin/python3 "$root/verify_comparison_configs.py"  # exit 0
/home/mzh/miniconda3/bin/python "$root/append_comparison.py"  # exit 0
```

All 122 baseline/comparison checks passed. The original report is retained as `report-before.md`; `report-after.md` and `report-appendix.md` preserve this addition. Command statuses, any startup qualification, and restoration evidence are recorded alongside the new measurements.

# RK3576 HDMI display: build and operation

[Project overview](../../README.md) · [Documentation index](../README.md)

OSD means the text and graphics over the video. The display runs inside `drone_tracker_node`, using the existing camera source and shared read-only frames. During tracking it displays an actually consumed image with its own target result. Continuous camera preview displays fresh unboxed images before ON and after OFF. It never opens a second camera.

RGA performs display scaling and color conversion. DRM/KMS, the Linux display interface, submits video and overlay planes together. Model preprocessing is separate from this display path. The renderer does not crop odd-sized grayscale input: it converts such input to equal-channel BGR before scaling when required.

## Build

From the repository root:

```bash
./build.sh --test
```

The default x86_64 image cross-compiles ARM64 output and enables HDMI and recording support. `--backend arm64` explicitly selects the ARM64-image fallback. The runtime `osd.enabled` parameter controls display creation. See [the image guide](../../dockerfiles/README.amd64.md) for target SDK paths, test launchers, and packaging.

For a native ARM64 ROS 2 Humble SDK environment, manual configuration can use:

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select drone_tracker --cmake-args \
  -DCMAKE_BUILD_TYPE=Release \
  -DDRONE_TRACKER_PLATFORM=rk3576 \
  -DDRONE_TRACKER_BUILD_OSD=ON \
  -DDRONE_TRACKER_OSD_LOW_LEVEL_FEEDBACK=OFF \
  -DDRONE_TRACKER_BUILD_TESTS=ON \
  -DRKNN_INCLUDE_DIR=/path/to/rknn/include \
  -DRKNN_LIBRARY=/usr/lib/librknnrt.so
```

The x86_64 entry point supplies the cross toolchain and target SDK paths automatically. HDMI configuration uses pkg-config to find `libdrm`, `librga`, and their development headers. Disabling `DRONE_TRACKER_BUILD_OSD` avoids display SDK discovery. Use the system RKNN Runtime 2.3.2 and RKNPU driver 0.9.8 on the production board; do not overwrite Runtime with a container copy.

Portable drawing and policy checks do not need HDMI hardware:

```bash
cmake -S src/osd -B /tmp/drone-osd-tests -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/drone-osd-tests
ctest --test-dir /tmp/drone-osd-tests --output-on-failure
```

The optional `DRONE_TRACKER_OSD_LOW_LEVEL_FEEDBACK=ON` adapter also requires OSD and a matching `quadrotor_msgs` installation. Its checked battery levels are unknown=0, normal=1, low=2, and critical=3. Missing or incompatible definitions fail compilation.

The installed node remains `lib/drone_tracker/drone_tracker_node`. OSD is an internal static library. Fonts are embedded, including the DejaVu-derived ASCII font and existing localized hints. Licenses are retained in `src/osd/FONT_LICENSE.txt` and `src/osd/DEJAVU_FONT_LICENSE.txt`. Build and runtime do not read a sibling `rk_hdmi_osd` checkout.

## Devices, defaults, and startup

Verify the current display connector, mode, camera topology, and device users before starting. Access is needed to the selected DRM card, DMA heap, RGA devices, and readable debugfs client records. Node startup does not mount debugfs or elevate privileges.

The backend requests display control with `drmSetMaster()`, then verifies `/sys/kernel/debug/dri/<device-minor>/clients`. It proceeds only when the kernel reports exactly one client belonging to this process with `master=y`. A successful DRM API return alone is insufficient. Unreadable/unrecognized records, competing clients, or missing ownership reject initialization and release acquired resources.

| Parameter | Declaration/default value | Meaning |
|---|---|---|
| `osd.enabled` | false | Create HDMI resources |
| `osd.render_path` | `cached-overlay` | Renderer; `legacy` is the alternative |
| `osd.timing_enabled` | false | Collect display-stage durations |
| `osd.drm_device` | `/dev/dri/card0` | Display device |
| `osd.connector` | `HDMI-A-1` | Exact connector name |
| `osd.width`, `osd.height` | 720, 480 | Even output dimensions, 2..4096 |
| `osd.refresh_hz` | 60.0 | Nominal mode rate; corresponding approximately 59.94 Hz modes are accepted |
| `osd.frame_stale_ms` | 1000 | Clear targets and show waiting when results stop |
| `osd.telemetry_timeout_ms` | 2000 | Monotonic telemetry validity interval |
| `osd.craft_name` | `FPV` | Up to 24 printable ASCII characters |
| `osd.battery_cells` | 0 | Unknown; never infer the count from pack voltage |

Declaration defaults are not every template's runtime configuration. [The production SC233HGS template](../../parameters/rk3576_sc233hgs_production_params.yaml) explicitly enables OSD, timing, independent recording, and ROS bag. AR0234 and video templates disable these features. Inspect the selected YAML before use. All `osd.*` parameters are fixed at startup. Timeouts accept 1..3600000 ms, battery cells 0..32, and refresh rate 1..240 Hz. Legal dimensions do not prove the physical mode exists or text is readable.

The installed `/opt/drone_tracker/bin/run-camera` entry applies the CPU policy and invokes display preparation. Preparation stops `display-manager.service` and `lightdm.service`, then checks display-device release when OSD is enabled. The node also applies configured CPU roles. The display backend itself does not manage desktop services. Stopping the tracker does not automatically restore the desktop. See [deployment instructions](../../deployment/README.md) for service and manual-entry behavior.

For direct `ros2 run` operation outside that wrapper, arrange desktop shutdown and device release first. A typical board sequence, only within an authorized maintenance window, is:

```bash
sudo systemctl stop lightdm
sudo cat /sys/kernel/debug/dri/0/clients
# For card0, verify that only the table header remains before starting.
source /opt/ros/humble/setup.bash
source /path/to/install/setup.bash
ros2 run drone_tracker drone_tracker_node --ros-args \
  --params-file /absolute/path/validated-camera.yaml
# After the node has exited and released the display:
sudo cat /sys/kernel/debug/dri/0/clients
sudo systemctl start lightdm
```

OFF is insufficient for desktop restoration: the process retains HDMI, and continuous-camera mode also retains capture. Stop the service or node and confirm resource release first. OSD initialization failure fails node initialization. Runtime display failure disables display while healthy tracking/ROS output continues; recovery requires restart.

## Video, tracking, and control

Continuous preview applies to RK3576 ROS camera input with `osd.enabled=true`, for both render paths. Startup initializes parameters, models, message interfaces, and display before opening capture. The screen starts with black waiting video and available telemetry until a qualified frame arrives.

ON starts tracking and explicitly enabled recording/bag sessions. OFF closes new tracking-result and recording reception, clears target decorations, and returns to fresh preview without waiting for model or storage cleanup. Duplicate ON keeps the active round. Pending rapid ON/OFF changes retain only the latest desired state. SC233HGS warmup belongs to the camera stream and is not repeated on ON.

During Running, video and targets remain paired to actually consumed tracker frames. A 60 FPS capture rate does not guarantee 60 new HDMI frames per second. A blocked inference can reach stale-result waiting; OFF resumes preview while safe cleanup continues. Preview frames never publish bbox, LOS, tracker state, or `/fpv/tracker_image`. Debug-image publication remains controlled by `visualized_result` within an active consumed-frame window. Published images are eligible for sampled ON-session recording under the [session recorder's independent policy](RK3576_SESSION_RECORDING.md). Recording does not enable image publication or produce preview messages.

Camera startup/runtime failure ends continuous service with nonzero status so the existing service restart policy can recover. ON does not reopen a failed continuous stream. Tracking failure ends only the round if preview remains healthy. Recording/bag/display failures preserve their own isolation boundaries.

Finite video, OSD-disabled cameras, independent tools, and Jetson retain their existing round-based input lifetime. In finite-video mode OFF/EOF freezes the last video base, clears targets, and continues available telemetry. A new round rejects old results. In continuous-camera mode OFF keeps accepting fresh unboxed preview. The stale-frame timeout clears decorations without falsely reporting EOF or TargetLost.

## Telemetry and HUD

HUD means the text and graphics presented to the operator. The base geometry derives from the 720×480 `rk_hdmi_osd` reference at commit `3e605a26c7c135b468ef2a3638ffd47144594603`; subsequent approved changes define the current fields. Main characters are 16×32 and small characters 8×16. Output resizing scales the HUD, while video-space target geometry is transformed separately.

Telemetry topics, types, and reliability are defined in [the telemetry specification](../../openspec/specs/osd-ros-telemetry/spec.md). IMU, GPS, pressure, temperature, and RSSI inputs use best-effort delivery; other status inputs use reliable delivery, with depth 10 for these OSD subscriptions. ROS remapping can select a compatible source, for example `-r /mavros/rc/in:=/my_bridge/rc_in`. Fill valid input/model parameters before starting.

| Area | Current content | Unknown/stale behavior |
|---|---|---|
| Upper left/left column | Craft name, subtitle, altitude, groundspeed, heading, fix grade, RSSI label | Numeric placeholders use `--`; speed converts m/s to km/h; fix grade uses GPSRAW |
| Upper center/center | Amber home arrow/distance, red circular reticle, white pitch ladder | Hide geometry whose required position or attitude is unavailable; the reticle is not a target lock |
| Right column | Flight mode, tracking/mission text, arm timer, battery values, current, consumed mAh, latitude/longitude | Independent placeholders; no inferred cell count or charge |
| Lower left/bottom | RSSI gauge/percentage, local calendar clock, frame identity, messages, trigger hint, persistent arm status | Preserve independent warnings and applicable waiting/freeze hints |
| Target | State-colored corner geometry; Tracking adds outer corners and a center diamond | Clear when invalid, stale, or stopped; no visible target label, invented confidence, ID, or second target |

Home is captured only at an armed rising edge with fresh connection and valid position. Missing position at arming is not filled later in that arm cycle. Ground pressure reference updates only with fresh connected/disarmed/landed status. Battery `charge` is remaining Ah: 13 Ah capacity and 10 Ah remaining mean 3000 mAh consumed. Current from `/mavros/battery` displays its absolute value, so -3.0 A displays `CUR 03.0A`; unknown/non-finite current displays `CUR --A`. Original messages are unchanged.

Severe warnings blink red approximately every 300 ms; connection warnings are amber. The clock is the runtime device's local calendar time to tenths of a second, so timezone changes affect it. Frame identity is not capture time. `TRIG ON/OFF` is a separate cyan two-second control hint at logical (22,80); it does not establish that tracking is running.

### RSSI signal strength

RSSI here describes the flight controller's remote-control receive-link quality. `/mavros/rc/in` supplies `mavros_msgs/msg/RCIn.rssi` as integer percent 0..100, following `apm_bridge` commit `390668d2d822731e95651cd3b88547e5912e66a1`. Do not divide by 254. Other publishers must convert to this percent contract. The value can represent receiver RSSI or CRSF link quality; it is not video strength or dBm received power.

The `RSSI` label begins at logical (22,278). The gauge begins at (20,330), filling a 148-pixel interior in yellow by `floor(148 * percentage / 100)`. Percentage text begins at (178,318). RSSI 85 produces 125 yellow pixels and `85%`, independently of battery charge. Weak valid values remain yellow. Battery charge does not drive the gauge, and temperature reception no longer produces a visible row.

Zero is valid and displays `0%` with an empty gauge. Missing, expired, or above-100 values, including 255, display `--%` without fill. RSSI uses its own original monotonic receipt time and `osd.telemetry_timeout_ms`: it remains valid at exactly the timeout and becomes unknown afterward. Other telemetry/video updates do not refresh it.

### Coordinates and persistent arm status

`LAT`/`LON` come from `/mavros/global_position/global` (`sensor_msgs/msg/NavSatFix`). Coordinates (31.2304,121.4737) display `LAT +31.230400` and `LON +121.473700`. Negative latitude/longitude means south/west. Six decimals describe formatting, not positional accuracy. Valid zero, including rounded negative zero, displays `+0.000000`. Arm state, home capture, and GPSRAW are not prerequisites. Invalid, missing, or expired position changes both rows to `LAT --` and `LON --`.

The coordinate rows use the outlined white main font at y=314 and y=350; full bounds end at x=702. The longest longitude `LON -180.000000` begins at x=458. Position has its own monotonic freshness interval.

The bottom center always shows one outlined main-font arm state, centered at x=360 with top y=430:

| Flight state | Text | Color |
|---|---|---|
| Fresh, connected, armed | `ARMED` | Green |
| Fresh, connected, disarmed | `DISARMED` | White |
| Missing, stale, or disconnected | `ARM --` | Amber |

This follows `/mavros/state` during preview, tracking, waiting, and frozen video. It has no blink or three-second expiry. Tracking control messages do not arm or disarm the aircraft.

### Confirmed mission display

`ON MISSION` requires a received `/fpv/mission_state=true` event and fresh `/mavros/state` with `connected=true`, `armed=true`, and `guided=true`. The bridge derives guided status from the flight controller's OFFBOARD flag; text such as `OFFBOARD|ANGLE` need not exactly equal a mode string. A MISSION trigger alone is insufficient. Tracking-state publications do not end the banner or change mission authorization.

Mission starts/ends are events without a heartbeat. Flight status is periodic, using its original monotonic receipt time and `osd.telemetry_timeout_ms`, default 2000 ms. Repeated summary snapshots do not renew that receipt. Every received mission/flight transition enters the policy directly, preserving even short false events or OFFBOARD exits between summaries.

A mission start waits hidden for first OFFBOARD confirmation. Fresh armed ACRO keeps it pending. After confirmation, mission false, OFFBOARD loss, disarm, disconnect, or flight expiry clears it. OFF and DISMISS cancel before tracking cleanup; DISMISS preserves tracking. Repeated true or flight recovery cannot revive an invalidated attempt: a later false then true is required. Cancellation after an already received false preserves that end boundary.

For example, if a target stays Tracking=2 while mission false arrives, a healthy display restores `TRK TRACKING` within 500 ms without waiting for another frame or tracker-state publication. Flight expiry clears by last receipt plus timeout plus 500 ms, 2500 ms with defaults. This remains independent of blocked inference/recording cleanup. Prepared stale overlays are rejected before hardware handoff; valid prepared video can be redrawn with the current overlay. Already submitted hardware work completes safely.

A tracker restart cannot reconstruct a missed volatile mission start from OFFBOARD alone. If the interceptor dies without an end event while fresh armed OFFBOARD reports continue, the existing topics cannot prove mission termination and the banner can remain visible. These are external-message limitations.

`journalctl -u drone-tracker.service -f` exposes trigger receipt, tracker-state publication, `OSD mission` policy transitions, and `OSD display` submissions. Mission logs carry `phase`, `reason`, `overlay_revision`, `attempt`, and `monotonic_us`. Attempt is a local counter, not an interceptor mission ID. Reasons distinguish start, confirmation, end, OFFBOARD loss, disarm, disconnect, expiry, OFF, and DISMISS. Unchanged reports do not generate transitions.

Correlate display submission with overlay revision, attempt, stream, round, source sequence, and kind. `target_label=""` is always retained because visible target labels have been removed. `has_video=1` means a retained base exists; increasing source sequences show continuing preview. Submission logs do not prove physical-screen latency.

## Renderers and display-stage timing

`osd.render_path` accepts `cached-overlay` and `legacy`, fixed at startup. Cached overlay uses a video plane and a transparent text/target plane with two buffers each. Only a confirmed page flip makes a buffer displayed and reusable. Telemetry refreshes and non-continuous freezes reuse the current video plane; continuous OFF can admit fresh preview. Unknown flip completion stops display safely rather than overwriting a potentially scanned buffer.

Cached layout retains a canvas and glyph cache, restoring/redrawing changed field regions. Geometry, theme, or round changes can rebuild the layout. Legacy draws complete images. Compatible BGR input uses one synchronous RGA conversion; grayscale copies luminance and uses neutral chroma for conversion. Byte strides that cannot be represented as integer pixels are repacked into reusable storage. Source views remain owned until synchronous device reads complete.

### Display stage timing

`osd.timing_enabled` enables measurement only after restart. C++ defaults it false; the production camera template sets it true. With OSD disabled it does not create display resources. Disable and restart to stop sampling.

All durations use a monotonic clock. A telemetry callback takes fixed-size snapshots and prints after releasing the submission lock. New-round, shutdown, and fault handling flush bounded windows; abrupt kill/power loss may lose the tail. Events belong to their actual measurement window, so submission and display can land in different windows.

| Metric | Boundary |
|---|---|
| `source_copy` | Legacy source mapping, allocation, synchronization, and row copying |
| `source_prepare` | Cached source validation/import and necessary repacking |
| `scale_convert` | Geometry, scaling, and color conversion |
| `layout` | Text, telemetry, and target drawing |
| `compose` | Legacy plane/image copy, synchronization, and RGA composition |
| `overlay_upload` | Cached overlay transfer |
| `commit` | Request creation, authorization check, and device submission |
| `flip_wait` | Display completion event wait; not light reaching the screen |
| `render_total` | Parent duration from packet take through processing completion |
| `other` | Parent duration minus measured children; do not add parents and children twice |
| `pending_wait` | Acceptance-to-take for packets actually taken; replaced packets are absent |
| `submit_to_display` | Acceptance-to-completion for successful new video; not camera-to-screen delay |
| `new_frame_interval` | Adjacent successful new-video completions within one continuous segment |

Stop, EOF, failure, new-round, and stale-wait transitions break the interval chain. Repeated frozen bases do not count as new video. The first processed video is tagged `first_frames=1`, retaining initial allocation cost. Logs distinguish new/repeat video and success/rejected/failure. Unexecuted stages have no samples, rather than fabricated zero durations. Failed stages retain elapsed time before failure.

Counters `submitted/replaced/displayed/rejected/commits` in timing logs are window increments. `commits`, internally `refreshes`, counts successful display updates rather than physical scans. Cumulative `submit_calls`, `submit_mean_us`, and `submit_max_us` measure the full submit call including replaced-packet release, excluding packet construction and hardware completion wait. They are not end-to-end latency.

Means and maxima retain raw microseconds. P95/P99 use approximate power-of-two bucket upper bounds from 1 through 67108864 microseconds, followed by overflow. There are 28 colon-separated buckets; an estimate can approach twice the exact duration. Aggregate buckets before computing a cross-window percentile; never average window percentiles. Divide counts by actual `window_us`. RGA stage duration includes CPU scheduling and synchronization, not pure accelerator execution.

## Acceptance and rollback

Use a specific production `HZ-RK3576_WuKong Board`, matching Runtime/driver, approved models, and identified camera/input. Save release/configuration/model/system-library fingerprints and initial service states. Run candidate programs from an isolated directory and ROS domain when synthetic messages are used. Stop conflicting camera/display users only within the authorized window, and restore their prior states afterward.

Current continuous-preview acceptance uses the dedicated continuous-stream specification: at least 20 ON/OFF rounds and at least 60 seconds of joint tracking/recording/HDMI workload, with each run at most 180 seconds. Legacy behavior and an OSD-disabled baseline have separate comparable runs. Historical ten-minute or thirty-minute protocols for earlier lifecycles do not replace this acceptance. Mission-display acceptance checks actual captures and monotonic receipt-to-display times for both renderers, including end/expiry while inference is blocked.

To compare timing overhead, keep the board, models, input, recording, display mode, and other configuration identical. Alternate timing off/on for at least three rounds of at least 60 seconds each, within applicable run limits. Record source/configuration/model identities, Runtime, rates, CPU/memory, and comparable windows. A throughput-median drop over 5% requires investigation rather than a pass claim.

For renderer rollback, stop the node and restart with `osd.render_path=legacy`; models, topics, and CPU policy remain unchanged. This changes rendering, not continuous-camera lifetime. To disable HDMI, set `osd.enabled=false` and restart after confirming old display resources are released. To restore an earlier input lifecycle, restore the complete older release and matching configuration/entry. Never replace system RKNN Runtime. Restore the desktop only after node exit and device-release verification.

Dated results are in [original HDMI validation](../../openspec/changes/archive/2026-09-23-integrate-rk3576-hdmi-osd/final-validation.md), [cached-overlay validation](../../openspec/changes/archive/2026-09-24-optimize-rk3576-hdmi-osd-pipeline/validation.md), [HUD alignment](../../openspec/changes/archive/2026-09-27-align-hud-with-rk-hdmi-osd/validation.md), and [mission policy validation](../../openspec/changes/archive/2026-10-30-fix-osd-mission-banner-state/validation.md). They establish only their recorded binaries, devices, inputs, and checks. `.docker-artifacts` and `/tmp` are disposable and may be cleared; preserve needed originals in separately managed storage. Historical artifact paths do not assure current availability. This guide update performs no new hardware acceptance.

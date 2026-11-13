# RK3576 production deployment package

[Project overview](../README.md) · [Documentation index](../docs/README.md)

Build ARM64 programs with the default x86_64 cross-compiler image or explicit ARM64
fallback. `deployment/package.sh` separately uses the ARM64 image to collect genuine
ARM ROS dependencies from the selected local installation. It does not read a live
production board. The package contains the node, recording worker/inspector, diagnostic
worker/inspector, package-discovery files, selected unchanged configuration and referenced
RKNN models. Required ROS libraries and non-Python vision_msgs type support are bundled
under each release's `lib/bundled-ros`. Startup prefers those libraries without overwriting
system ROS. Source, benchmark programs, RKNN Runtime and base system libraries are excluded.

The tracker contains the unified session-topic recorder; deployment adds no bag
recorder service. Its real generated `quadrotor_msgs` libraries are required
independently of the optional OSD feedback adapter. Package the matching
`install/quadrotor_msgs` prefix alongside the selected tracker installation.
Use `--message-install /absolute/install/quadrotor_msgs` when it is not beside
the selected tracker prefix. Packaging rejects missing or non-ARM64 generated
libraries and includes the full catalog's non-Python type-support and ROS
package-discovery resources. The board needs no sibling source checkout.
The selected camera YAML, including all `bag.*` overrides, is copied byte for
byte and fingerprinted. Rollback must restore the prior program and its matching
configuration, without deleting either version's evidence.

See the [session recording guide](../docs/operations/RK3576_SESSION_RECORDING.md)
for intervals and retention. A 256 MiB file split continues the same session;
the default 5 GiB total bag quota also counts old/incomplete bags and summaries.
The production video quota remains a separate 20 GiB setting. Both writers use
their actual filesystem's observed free space, with a default 5 GiB threshold;
space is not atomically reserved across writers. Storage guards stop bag
recording independently. Startup preview creates no bag, and post-OFF responses
are outside the sealed interval. Normal exit attempts to drain accepted work;
abrupt power loss has no guaranteed final-message retention interval.

## Board clock synchronization

Attach the intended board over ADB and verify the development host's clock first:

```bash
./deployment/sync-board-time.sh
```

With multiple devices, pass its explicit serial, for example
`./deployment/sync-board-time.sh b31f8847ff67f9d1`. The script accepts only
HZ-RK3576_WuKong Board and requires a root ADB shell. It temporarily stops running
tracker/interceptor services, sets system/RTC time, checks readback and restores their
original running states. Restoration is attempted on clock-setting failure as well.
Run this tool on the development host, not inside the board.

## Install on a production board

The target requires HZ-RK3576_WuKong Board, Ubuntu 22.04 ARM64, ROS 2 Humble, system
RKNN Runtime 2.3.2, RKNPU driver 0.9.8, base dependencies such as OpenCV/FFmpeg, and
working camera drivers. Bundled libraries remove the need to install vision_msgs or
cv_bridge separately through apt; the base ROS environment is still required.

1. Copy the archive and its `.sha256` companion to the same board directory.
2. Replace PACKAGE below with the archive name without `.tar.gz`, then verify and inspect:

   ```bash
   sha256sum -c PACKAGE.tar.gz.sha256
   tar -xzf PACKAGE.tar.gz
   cd PACKAGE
   sudo ./deploy.sh --check
   ```

3. Install and enable startup at boot:

   ```bash
   sudo ./deploy.sh
   ```

   A first installation does not start immediately or close the desktop. Use
   `sudo ./deploy.sh --start` for immediate startup. Upgrading an already running service
   stops it and restores running state after installation. Starting the node closes the
   desktop session.
4. Check actual service state:

   ```bash
   systemctl is-enabled drone-tracker.service
   systemctl status drone-tracker.service
   journalctl -u drone-tracker.service -f
   ```

Enabled means boot startup, not current running state or passed camera acceptance.
Deployment neither reboots the board nor sends `/fpv/tracker_trigger`.

## Configuration, devices and CPU policy

Camera configuration installs as `/opt/drone_tracker/config/camera.yaml`; CPU policy
installs as `/opt/drone_tracker/config/cpu-affinity.yaml`. Model/configuration paths
below `/opt/drone_tracker` are target-board paths. Both configurations preserve their
selected source bytes. `SOURCE.json` records input paths/hashes, Git state and image
identity; `SHA256SUMS` covers package files and `MODELS.sha256` covers models.

The production template enables OSD, timing, ROS bag and independent camera recording.
It uses `/dev/video22`, `/dev/media2`, `/dev/dri/card0` and
`/var/lib/drone_tracker/recordings`. These are explicit wiring-dependent settings.
Before capture, use `v4l2-ctl --list-devices` and `media-ctl -p` to verify topology and
formats. Device existence alone does not establish the correct sensor/display.

The launcher stops display-manager.service and lightdm.service synchronously. When
OSD is enabled it then checks `/sys/kernel/debug/dri/<device-number>/clients`, allowing
up to five seconds for device release. Failed stops, unreadable lists or remaining
clients prevent node execution. Debugfs must already be mounted; the launcher does not
mount it. `deploy.sh --check` checks conditions without stopping the desktop.

Boot startup runs after desktop startup, then stops the desktop before launching the
node. Do not start the desktop while the node owns display resources. Node stop does
not restore it automatically. To restore the desktop, stop the service, confirm all
manual nodes have exited and released the display, then start lightdm.service.

Service and direct startup use `/opt/drone_tracker/bin/run-camera`, running as root for
device/storage access. It applies the shared allowed CPU range with taskset and passes
the same policy to the node. Systemd supervises the node directly. Abnormal exits restart
until five failures within one minute exhaust the limit; after repair use
`sudo systemctl reset-failed drone-tracker.service` before restarting.

`allowed_cpus` is the complete permitted range. The standard installed policy uses
`[2, 4, 5, 6, 7]`. The role-pinned production template uses:

| Role | CPU | Responsibility |
|---|---:|---|
| topic_recording | 2 | Topic-recording callback executor and each session bag writer |
| ros_transport | 6 | ROS initialization, node construction, communication and executor-resource creation |
| control | 4 | Models and single-threaded ROS callback executor |
| tracking | 4 | Tracking worker |
| yolo | 5 | Detection worker |
| display | 6 | HDMI display |
| capture | 7 | Capture and persistent frame dispatcher |
| recording | 7 | Independent camera-video controller, encoder process and helpers |

Fresh installation and upgrade install the selected CPU-policy bytes, with a
matching content fingerprint. Both service and direct startup read that same
installed file; no second hardcoded allowed mask is used. CPU-policy replacement
is independent of any camera-configuration retention behavior.

The default selects `topic_recording: 2`. An explicit `--cpu-policy` remains an
operator override: another valid topic core is honored, and custom `mode: inherit`
intentionally leaves workers unpinned. For `need_save=true`, a new role-pinned
release must explicitly include `roles.topic_recording` and its core in
`allowed_cpus`. Packaging and installer preflight reject an omitted field before
installation or service mutation. `need_save=false` does not require an unused
role. Selected inputs are never silently rewritten.

Packaging and installer preflight also ask the selected node whether it implements
the topic role. An older binary that only understands the legacy executor/writer
split is rejected for a recorder-enabled role-pinned release. Rebuild the revised
source and pass its matching `--install-root` to packaging. The check uses the
existing `--diagnostic-protocol-version` inspection exit with
`DRONE_TRACKER_RELEASE_CAPABILITIES=1`; it never starts ROS or camera resources,
including when an older binary returns only its diagnostic version. Ordinary
diagnostic-version output remains unchanged. Inherited-mode and recorder-disabled
releases do not require an unused topic-role capability.

Offline packaging validates policy structure without consulting build-host CPU
topology. Deployment preflight, including `deploy.sh --check`, checks the actual
target's online/usable cores before changing services or installed files. For a
structural-only check, run:

```bash
python3 deployment/cpu-policy.py /absolute/policy.yaml \
  --offline --camera-config /absolute/camera.yaml
```

The normal launcher retains runtime legacy compatibility; new-package completeness checks
are stricter. Backups and rollback restore the previous policy with its matching
binary, launcher, and configuration.

A legacy version-1 policy used directly at runtime without `topic_recording`
keeps the executor on `ros_transport` and the writer on `recording`; it does not
claim CPU 2. Explicit topic values reject null, strings, Booleans, invalid numbers,
and cores outside `allowed_cpus`. Both topic threads apply/read back the role
before work; a recreated writer repeats the handshake.

Version-1 policies omitting ros_transport use control for communication creation.
Explicit transport values must be online allowed integers; null/strings/booleans fail.
Inherit mode leaves threads unbound. Running the bare node without
DRONE_TRACKER_CPU_CONFIG retains inherited affinity. Policy changes require restart.

ROS resources are created before models. Main applies RosTransport before rclcpp::init;
direct DroneTrackerNode construction applies it before the ROS base constructor.
Application publishers/subscriptions use that boundary. Main explicitly applies and
reads back Control before input/model initialization, including repeated initialization.
Workers apply their own roles. Later communication creation through DroneTrackerNode
restores the caller's previous mask afterward. Plain rclcpp::Node callers must wrap it
in `cpu_affinity::withRole(Role::RosTransport, ...)`. Creation failures restore the prior
mask; restoration failures propagate. Production never scans/moves existing middleware
threads or changes priorities/scheduling policies.

`CPU policy loaded` proves parsing only. Per-role requested/actual logs prove managed
binding/readback. Middleware helpers inherit creator masks; inspect their actual masks
separately instead of inferring every Fast DDS thread from a configuration log.
Stop sends SIGINT to finish recording; after 30 seconds systemd cleans remaining group
members. Check masks with:

```bash
pid=$(systemctl show -p MainPID --value drone-tracker.service)
grep Cpus_allowed_list /proc/"$pid"/task/*/status
```

Deployment leaves drone-interceptor.service, system librknnrt.so and the ROS installation
unchanged.

## Backups and rollback

Every installation records old entries, model/configuration state, current link and both
services under `/opt/drone_tracker/backups/TIMESTAMP`. Existing release directories remain.
The installer prints the selected backup; retain that path even after installation failure.
Use the matching package's rollback script from its extracted root:

```bash
sudo ./rollback.sh /opt/drone_tracker/backups/TIMESTAMP
```

Rollback restores program links, launch entries, configuration, services and their original
enabled/active states. Rolling back to a version without diagnostics stops/removes the
new diagnostic service. Release directories and diagnostic originals remain available
unless separately cleaned. Read new-format logs with the matching retained inspector.
The rollback script requires `installation.tar` and rejects a first-install backup
without it. To undo such an installation, stop and disable the newly installed tracker
and diagnostic services, remove their installed service files, and move aside the new
configuration, launch entries and current link. Run `systemctl daemon-reload` afterward.
Retain models, recordings, diagnostics and release directories for inspection. Restore
only service states that actually existed before installation. Inspect the backup and
rollback result before removing any retained data.

## Build and package locally

Run `./build.sh --test` for ARM64 output. Use `--clean` when cache identity requires it;
`--backend arm64` deliberately selects the older compiler-image workflow. Select the
resulting backend installation explicitly when packaging.

Place four approved models in a local directory mirroring target paths. For example,
`/opt/drone_tracker/models/fp16/head.rknn` corresponds to MODEL_ROOT/fp16/head.rknn.
Include YOLO, template backbone, search backbone and head; shared nano_backbone stays empty.

```bash
./deployment/package.sh .docker-artifacts/packages \
  --install-root "$PWD/.docker-artifacts/backends/amd64/install/drone_tracker" \
  --model-root /absolute/retained-model-directory
```

Defaults are parameters/rk3576_sc233hgs_production_params.yaml, the legacy
`.docker-artifacts/install/drone_tracker`, and deployment/cpu-affinity.yaml.
Override them with existing absolute `--camera-config`, `--install-root`, `--cpu-policy`
paths. For AR0234, fill its four model paths and select that template explicitly:

```bash
./deployment/package.sh .docker-artifacts/packages \
  --install-root "$PWD/.docker-artifacts/backends/amd64/install/drone_tracker" \
  --model-root /absolute/retained-model-directory \
  --camera-config "$PWD/parameters/rk3576_ar0234_camera_params.yaml"
```

Models are included by default. `--without-models` still requires complete local model
files and generates MODELS.sha256; board preflight verifies the existing model bytes.
Missing inputs or changes during copying fail packaging without a successful partial
archive. Packaging does not compile, contact the board, start capture or change services.
Run board preflight separately before deployment. Keep models and generated packages
outside ordinary source control.

`.docker-artifacts` and `/tmp` are disposable work areas and may be cleared routinely.
Retain required release archives, checksums, source/build identities and acceptance
results in an explicitly retained external location. Paths in historical reports do not
guarantee surviving files. A successful package check does not establish camera/NPU acceptance.

## Display timing

`osd.timing_enabled` is startup-only. Its C++ default is false; the production template
sets true. Save the release path and camera.yaml before editing, then restart to apply.
Metric boundaries are in the [platform guide](../src/platform/README.md).

For an authorized board comparison keep models/input/recording/display mode fixed,
alternate timing off/on for at least three 60-second rounds per side, and record identities,
Runtime, display mode, capture/consume/display rates, CPU and memory. Investigate a median
throughput decrease above 5% as an unmet observation-cost target. Restore the original
configuration/release afterward. Disabling only timing requires false plus restart;
never replace the system Runtime.

## Independent diagnostics

`drone-tracker-diagnostics.service` is an independent collector. Business Wants/After
attempt its startup and establish order, without coupling stop. Business exit/restart/OFF
leaves it running. Its own failures restart separately. The client continues after a
500 ms connection budget with diagnostics marked unavailable.

The production template uses basic; other entries default off. Mode is startup-only.
Off disables only that business client's submissions, not the global collector. Detail
adds stage timing. Normal client exit waits at most two seconds; service stop has a
three-second budget. The socket `/run/drone_tracker/diagnostics.sock` is mode 0600 and
requires the same local user as the collector. Installed service/direct entries currently
use root. `/var/lib/drone_tracker/diagnostics` contains only diagnostic records; installation
does not remove prior records or overwrite camera video, ROS logs or bags.

Package-root `sudo ./deploy.sh --check` validates ARM64 programs, dependencies, protocol,
service syntax and configuration without starting/stopping services or rewriting logs.
Upgrade backs up first, stops business then old diagnostics within budgets, installs a
matching set, attempts diagnostics restoration and then business startup. Collector failure
degrades recording rather than stopping healthy tracking.

```bash
sudo systemctl start drone-tracker-diagnostics.service
sudo systemctl start drone-tracker.service
sudo systemctl stop drone-tracker.service
sudo systemctl stop drone-tracker-diagnostics.service
```

For direct startup, stop the collector service first; two collectors cannot own one
socket. `/opt/drone_tracker/bin/run-diagnostics` validates the shared CPU policy, requires
CPU 6 and verifies exact `{6}` readback before exec with ordinary SCHED_OTHER scheduling.
An outer taskset cannot select another core. Missing/excluded CPU or failed set/readback
returns nonzero without starting the collector or altering business assignments.
CPU 6 remains shared with display work.

Space protection, write errors or synchronization age above 500 ms degrade diagnostics;
business continues and full buffers record gaps. Back up originals and export with the
matching inspector into a new directory. A 100 ms sync period does not guarantee a fixed
maximum power-loss window. See the [diagnostic guide](../src/diagnostics/README.md).

### Continuous preview upgrade and rollback

`enabled` means the service is configured to start at boot. It does not mean the
node is currently running. With the production RK3576 camera configuration and
`osd.enabled=true`, actually starting the node immediately occupies the camera
and HDMI and begins preview. ON still controls tracking and explicitly enabled
recording/bag sessions; service startup itself creates neither. OFF leaves the
camera and HDMI occupied and keeps the desktop stopped. Stop the service and
confirm device release before restoring the desktop or using another camera tool.

`deploy.sh --check` remains a read-only preflight: it does not stop the desktop,
start a node, capture images or create any session. Preserve the selected model,
algorithm, capture, bag, recording, HDMI and CPU policy values when upgrading.
The existing CPU policy also applies to the persistent frame dispatcher as the
Capture role; ON/OFF do not recreate capture or dispatcher threads.

To restore the previous ON/start and OFF/freeze lifecycle, stop the new service
and confirm it has exited and released devices. Then restore the complete old
release, its matching configuration and launch entry using the backup procedure
above. Restore the previous enabled/active service state and restart if it was
running. `osd.render_path=legacy` only selects the legacy renderer and cannot
restore the old lifecycle.

## Mission-banner startup and validation

Start the tracker before the interceptor's mission-start event when mission display
is required. The banner uses the existing `/fpv/mission_state` start/end events and
fresh connected, armed `/mavros/state` with `guided=true`. A `MISSION` trigger or
OFFBOARD alone does not authorize it. Mission events do not expire; periodic flight
status uses `osd.telemetry_timeout_ms` (default 2000 ms) and its original monotonic
receipt time. Tracking-state changes do not control banner lifetime.

A restart can miss the volatile start event and leave the banner hidden during a
real mission. Cancellation or flight-authorization loss also requires a later
received mission `false` followed by `true` before another attempt is eligible.
Do not repeatedly publish `true` to bypass this boundary. Interceptor death without
an end event cannot be detected while fresh armed OFFBOARD reports continue. These
limitations follow from the existing external messages; this change adds no heartbeat.

Validate both `cached-overlay` and `legacy` on the identified production board with
the configured camera and models. Use an isolated ROS domain for synthetic display
status so test messages cannot reach production consumers. Record actual HDMI
captures and monotonic receipt-to-display times: terminating events must clear
within 500 ms on a healthy display; flight expiry must clear within timeout plus
500 ms of the last report. Preview and paired target styling must retain their
existing behavior. Temporary validation binaries do not constitute a release.
Keep the current release and configuration unchanged until a separate deployment
is authorized. If services are stopped for validation, record their initial active
states and restore those states when the work finishes.

## Isolated ROS transport verification

Build with `./build.sh --test`. For cross-compilation, cross-process ROS checks compile and run the same probe
natively in a separate build directory. The selected QEMU user runtime fails cross-process discovery even though
the identical native probe passes in the same offline container. The ARM64 probe remains built for actual board
validation; native success does not count as ARM64 timing or transport acceptance. The `ros_transport_tests` and `ros_transport_contention_tests` CTests run separate
receiver and publisher processes. The receiver creates two successive subscriptions after returning to the control
core, which exercises discovery and later endpoint creation. `ros_affinity_initialization_tests` additionally
constructs the actual tracker node with injected model components, checks model initialization on the control
core, and checks cleanup after model and ROS-node-base construction failures.

On the production board, copy the ARM64 test executable and `src/tests/run_ros_transport_test.py` to a temporary
candidate directory. Use a ROS domain distinct from the online system, for example:

```bash
source /opt/ros/humble/setup.bash
ROS_DOMAIN_ID=183 ROS_LOCALHOST_ONLY=1 python3 /tmp/candidate/run_ros_transport_test.py \
  /tmp/candidate/ros_transport_tests --control 4 --transport 6 --samples 1100 --contention
```

This runs two message rounds and at least 22 seconds of bounded CPU 4 competition. The load helper uses a 75%
duty cycle with ordinary inherited priority and always has a fixed deadline. The coordinator terminates all its
children on completion or failure. It reports message counts, losses, P50/P95/maximum callback latency and DDS
latency when middleware timestamps are available. Callback timing uses the shared host monotonic clock; DDS timing
uses the middleware's system-clock source and receive timestamps. Either a lost message or a one-second callback
or DDS delivery delay fails the check. `--negative-checks` deliberately omits one message and uses a zero latency
threshold to prove those failures are detected.

During the run, record `/proc/<receiver-pid>/task/*/status`, `stat` and `schedstat`. Identify communication threads
using read-only stack samples and the installed Fast DDS/RMW symbols. Record unknown threads as unknown, rather
than assuming all helper threads are DDS. Check the identified receive, discovery and event thread masks after
startup and after the second endpoint appears. Thread IDs and counts are observations, never binding rules.

Before a camera/HDMI candidate run, save the production release link, configuration fingerprints and active
service states. Stop services as needed to avoid camera/display conflicts, then run the candidate in the isolated
domain using a copy of the installed configuration and existing model files. Stop the candidate and all test
publishers before restoring the original services. Do not overwrite the production release, policy or system RKNN
library during this check. Report actual tracking and recording coverage separately from transport verification;
synthetic CPU contention does not establish flight or target-tracking acceptance.

# platform-factory Specification

## Purpose

Create compatible capture, frame, preprocessing, and inference resources for one selected platform. Keep hardware SDK types behind platform boundaries and define ownership, partial-failure cleanup, and the single camera-queue consumer used by continuous preview.

## Requirements

### Requirement: Builds select one implemented platform explicitly

A platform backend adapts capture, image preparation, and inference to hardware. `DRONE_TRACKER_PLATFORM` SHALL select `jetson` or `rk3576`. Raw CMake SHALL default to `jetson`; the repository RK3576 build entry SHALL explicitly select `rk3576`. Unknown values SHALL fail before hardware language or SDK discovery, with the requested and supported values. The system MUST NOT switch backends after failure.

#### Scenario: Existing Jetson configuration
- **WHEN** raw CMake is configured without a platform option in a matching Jetson SDK environment
- **THEN** it selects Jetson without adding a runtime platform parameter

#### Scenario: RK3576 configuration
- **WHEN** `DRONE_TRACKER_PLATFORM=rk3576` is configured with matching ARM64 target ROS and RKNN dependencies
- **THEN** the ROS node builds and installs without CUDA, TensorRT, VPI, Jetson Multimedia API, or Tegra discovery, whether the compiler runs natively or cross-compiles

#### Scenario: Unsupported platform
- **WHEN** `DRONE_TRACKER_PLATFORM=other` is requested
- **THEN** the first platform error reports `other` and `jetson/rk3576`, rather than a missing CUDA dependency

#### Scenario: Missing RKNN dependency
- **WHEN** RK3576 headers or target libraries are missing
- **THEN** configuration names the platform and missing dependency, states Runtime 2.3.2/driver 0.9.8 compatibility, and does not fall back to Jetson

### Requirement: Production factories provide matching components

Preprocessing prepares image data for a model; a model backend executes inference. The selected platform factory SHALL provide compatible frame sources, host-frame creation, YOLO, NanoTrack, and preprocessing components. Callers MUST NOT independently mix component platforms within one production pipeline. Failed assembly SHALL return a platform/component/path diagnostic without partial usable tracking dependencies.

#### Scenario: Camera and replay use the same selected platform
- **WHEN** production-camera and replay callers create Jetson pipelines
- **THEN** both obtain internally compatible Jetson resources without choosing hardware types in the caller

#### Scenario: Third model cannot load
- **WHEN** YOLO and NanoTrack Backbone resources exist but Head is unreadable
- **THEN** complete assembly fails with platform, component, and path, without returning a detection-only pipeline

### Requirement: Frame creation preserves pixels and independent ownership

A host image is CPU-readable pixel storage. The factory SHALL deep-copy valid Gray8 and BGR8 images into selected-platform frames. It SHALL preserve dimensions, pixel format, effective pixels, and microsecond timestamps. Modifying or freeing the source MUST NOT change the frame. Frames and read views SHALL retain storage independently of their factory. Invalid input SHALL return a diagnostic without a partial frame.

#### Scenario: Original image changes after copying
- **WHEN** a grayscale image is copied with timestamp 1000000 and the original pixels are overwritten
- **THEN** the frame retains the original pixels and timestamp 1000000

#### Scenario: Input has row padding
- **WHEN** a legal grayscale or BGR subimage has a non-contiguous row stride
- **THEN** copying respects that stride and excludes row padding from image pixels

#### Scenario: Image format is unsupported
- **WHEN** the input is empty or has four channels
- **THEN** creation fails diagnostically without exposing a partially initialized frame

### Requirement: Frame sources preserve one capture-queue consumer

A frame source supplies camera or video frames through a queue. Its public interface SHALL expose start, stop, running state, dimensions, and frame retrieval without hardware SDK types. The source queue SHALL keep one producer and one consumer. Continuous preview SHALL use one persistent dispatcher as that consumer, with at most one pending tracking task. Display and tracking MUST NOT compete for source frames or open another camera.

#### Scenario: Jetson camera recording starts
- **WHEN** a Jetson camera receives ON with `need_save=true`
- **THEN** existing camera-image recording, directory/prefix, ROS bag, queue, timestamp, and input-selection behavior remain intact

#### Scenario: RK3576 video recording request
- **WHEN** a local RK3576 video receives ON with `need_save=true`
- **THEN** it starts no camera-image recording and independently attempts ROS bag creation under `rk3576-ros-video`

#### Scenario: Jetson capture restarts
- **WHEN** one Jetson input receives ON, OFF, then ON
- **THEN** no old capture thread or additional queue consumer remains, and existing queue allocation, full-queue, timestamp, and history-time rules remain intact

#### Scenario: RK3576 video restarts
- **WHEN** video and prior consumption stop, then the source starts again
- **THEN** playback starts from the beginning without old queued frames; realtime mode establishes a new base and history mode retains recording time

#### Scenario: RK3576 camera accepts message recording
- **WHEN** a camera is configured with `need_save=true`
- **THEN** initialization succeeds, tracking/recording wait for ON, continuous preview can already run, and the node attempts bag creation without forwarding the legacy camera-recording request

#### Scenario: SC233HGS independent recording
- **WHEN** enabled recording receives qualified SC233HGS frames during an ON session
- **THEN** its bounded branch receives only frames that passed final camera checks; staged pixels stay unavailable to the encoder until publish, and the public camera queue still has one consumer

#### Scenario: Independent recording is disabled
- **WHEN** `recording.enabled=false`
- **THEN** no recording pixel copy, recording queue, or recording worker is created; Jetson, AR0234, and video keep their input contracts

#### Scenario: Warmup is independent of recording
- **WHEN** an SC233HGS stream starts with recording disabled
- **THEN** normal warmup still runs and the public frame interface supplies no unqualified warmup pixels

#### Scenario: Recording is requested for an unsupported input
- **WHEN** RK3576 AR0234 or video receives ON with `recording.enabled=true`
- **THEN** independent video recording is reported unavailable, `need_save` still governs bag creation, and healthy tracking continues

#### Scenario: Continuous preview dispatches while tracking is busy
- **WHEN** tracking processes A while source frames B and C arrive
- **THEN** the sole dispatcher keeps at most C pending, releases replaced B safely, and preserves A until its users finish; this dispatch is permitted by `rk3576-continuous-hdmi-stream`

### Requirement: Creation and final destruction respect active resources

The system SHALL start detection only after complete assembly and executable static contract checks succeed. Ordinary initialization failure SHALL reclaim acquired resources without active capture/detection threads. Factory destruction MUST NOT invalidate retained frames, views, or prepared inputs. Final destruction SHALL wait for resource users before release. Models SHALL survive OFF/ON. Unconfirmed device stop SHALL use the existing fatal boundary.

#### Scenario: Component loading fails partway
- **WHEN** the second or third model fails during assembly
- **THEN** acquired resources are released exactly once, detection never starts, and the created frame source can be destroyed safely

#### Scenario: Detection thread cannot start
- **WHEN** complete components exist but detection thread creation fails
- **THEN** initialization fails, components remain safely reclaimable, and capture has not started

#### Scenario: Factory is destroyed before retained frame
- **WHEN** factory and builder are destroyed while a consumer retains a frame or view
- **THEN** pixels remain readable until their final owner releases storage

#### Scenario: Exit occurs during detection
- **WHEN** shutdown finds active detection and cached frames
- **THEN** it waits for those accesses before releasing storage, with no early or duplicate release

#### Scenario: Model creation fails before capture starts
- **WHEN** a camera has acquired device/buffer resources but model creation fails before capture
- **THEN** destroying the source still releases those resources despite capture never having started

#### Scenario: Camera initialization or thread creation is partial
- **WHEN** construction throws or only some camera threads start
- **THEN** cleanup joins created threads before reclaiming resources, repeated cleanup is safe, and no usable source is returned

### Requirement: Common algorithms build without hardware SDKs

Common algorithms and factory interfaces SHALL compile without CUDA, VPI, TensorRT, RKNN, RGA, or Jetson Multimedia API headers. Common targets MUST NOT directly or transitively link platform implementations. Final programs SHALL explicitly combine common algorithms with the selected backend. Software-only tests SHALL execute real common algorithms with injectable dependencies.

#### Scenario: Portable common build
- **WHEN** only C++17, OpenCV, and threads are available
- **THEN** common tests build without hardware SDK discovery and exercise compatibility using injected components

#### Scenario: Production dependency declarations are reviewed
- **WHEN** Jetson CMake declarations are reviewed without configuring or running Jetson
- **THEN** its platform target owns Jetson dependencies, common targets do not link back to it, and Jetson configuration does not require RKNN or RGA; review is not hardware acceptance

### Requirement: RK3576 factories expose video and explicit camera capability

A legal local MJPEG request SHALL return a real unstarted source. MJPEG stores each video frame as JPEG. Explicitly camera-enabled standalone factories and the production factory SHALL accept valid camera requests. Other standalone factories SHALL reject camera requests. Root production builds SHALL support both ROS camera and video. Sources SHALL not require models or Jetson SDKs. Video SHALL return its actual dimensions and ignore camera-only image settings.

#### Scenario: Video source is created
- **WHEN** a factory receives a legal MJPEG path and arbitrary camera output dimensions
- **THEN** it returns an unstarted source reporting video dimensions that can subsequently start and supply frames

#### Scenario: Root camera and video entries are available
- **WHEN** a root RK3576 build requests a legal camera and a legal MJPEG file
- **THEN** both return unstarted, startable sources under their respective contracts

#### Scenario: Software video dependencies stay isolated
- **WHEN** independent video-source tests build and run
- **THEN** they need no Jetson libraries, real models, or NPU inference

#### Scenario: Standalone camera capability is enabled
- **WHEN** a standalone caller explicitly enables camera support and supplies a valid configuration
- **THEN** the returned source supports dimension queries, start, retrieval, and stop

#### Scenario: Standalone camera capability is omitted
- **WHEN** an existing standalone caller omits camera enablement
- **THEN** camera remains rejected while video and model assembly retain their behavior

### Requirement: Source status explains termination without consuming frames

The system SHALL expose a thread-safe hardware-neutral status snapshot. RK3576 video SHALL distinguish ready, running, stopped, EOF, and failure. Diagnostics SHALL include relevant path, cause, sequence, and read/enqueue/drop counts. Snapshots MUST NOT consume queued frames. Ordinary timeout MUST NOT mean EOF. Stop SHALL retain established termination evidence; restart SHALL establish fresh state and counts. Other platforms can report unknown detail.

#### Scenario: Video has a long interframe interval
- **WHEN** the next video frame is ten seconds away and retrieval times out
- **THEN** status still reports running instead of EOF or failure

#### Scenario: Decode failure survives cleanup
- **WHEN** an intermediate JPEG fails to decode and the source is stopped
- **THEN** status retains the path, failure cause, and corresponding sequence

#### Scenario: Query does not consume the final queued frame
- **WHEN** EOF has occurred but the queue contains a tail frame and status is queried repeatedly
- **THEN** status reports EOF and the original consumer can still retrieve the frame

### Requirement: Reused models isolate consecutive tracking rounds

A round is one interval of tracking input acceptance. With no concurrent tracker update, round cleanup SHALL cancel unstarted detection requests, wait for executing detection/frame access, and clear unread results and retained old requests. It SHALL retain loaded models. A new round MUST NOT consume old results, even when frame numbers repeat. Cleanup MUST NOT change normal scheduling, thresholds, or arbitration. Final destruction SHALL join detection safely.

#### Scenario: Round ends during inference
- **WHEN** the caller stops supplying old-round frames and requests cleanup during detection
- **THEN** cleanup waits for access to end, clears old results, and retains models for the next round

#### Scenario: Detection request has not started
- **WHEN** cleanup finds a pending old request
- **THEN** it cancels that request and releases its frame without executing it in a later round

#### Scenario: Frame numbers repeat across rounds
- **WHEN** old frame 1 has a ready result and the next round also begins at frame 1
- **THEN** only the new round's own result can produce its box

### Requirement: Camera implementation follows explicit sensor selection

RK3576 camera configuration SHALL select `ar0234` or `sc233hgs`, defaulting to `ar0234` for existing callers. Unknown values SHALL fail with the requested and supported values. A selected sensor's missing controls, ISP preparation, or other dependencies MUST NOT trigger automatic fallback to another sensor implementation.

#### Scenario: SC233HGS is selected
- **WHEN** a camera request sets `sensor_model=sc233hgs`
- **THEN** the factory returns the SC233HGS source without AR0234 trigger-control initialization

#### Scenario: Sensor selection is omitted
- **WHEN** a camera request supplies no sensor model
- **THEN** AR0234 remains selected with its existing behavior

#### Scenario: Sensor selection is unknown
- **WHEN** a requested model is neither `ar0234` nor `sc233hgs`
- **THEN** creation fails with the unsupported value and supported choices, without attempting either sensor

### Requirement: Sensor implementations retain independent capture contracts

AR0234 SHALL obey `rk3576-camera-frame-source` and its two capture modes. SC233HGS SHALL obey `rk3576-sc233hgs-camera-frame-source` and continuous capture. Shared capture-resource code MUST NOT change either sensor's pixel, timestamp, queue-drop, or start/stop contract. Continuous-preview lifecycle exceptions SHALL follow `rk3576-continuous-hdmi-stream`.

#### Scenario: AR0234 remains stable when SC233HGS support is present
- **WHEN** a production binary supports both sensors but selects AR0234
- **THEN** AR0234 preserves its pixels, timestamp policy, queue handling, and applicable lifecycle without acquiring SC233HGS-specific behavior

### Requirement: Message recording and original-camera recording stay separate

RK3576 `need_save` SHALL control only ROS bag recording. SC233HGS `recording.enabled` SHALL independently control original NV12 video through a bounded recording branch. Recording configuration/status MUST NOT expose encoder SDK types to the ROS caller. Continuous preview alone SHALL start neither recording session. ON SHALL attempt enabled sessions independently; recording faults SHALL preserve healthy capture/tracking/display under their existing contracts.

#### Scenario: Recording and bag are both enabled
- **WHEN** a healthy continuous-preview SC233HGS service receives ON with both options enabled
- **THEN** bag and original-video sessions start independently, while the existing dispatcher remains the sole source-queue consumer

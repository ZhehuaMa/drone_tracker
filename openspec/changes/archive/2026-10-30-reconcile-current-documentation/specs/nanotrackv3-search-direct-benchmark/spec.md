# Spec Delta

## ADDED Requirements

### Requirement: Standalone entries retain compatible configuration

`nanotrackv3_search_direct` SHALL accept baseline-compatible video, initial box, three model paths, precision evidence, warmup, update limit, rounds, and output settings. It SHALL support repository and ROS-free standalone builds. It SHALL reject existing output directories and run NanoTrack without YOLO, DCF, Kalman filtering, or ROS publication. Both current executable names SHALL remain available. All three models SHALL pass the baseline FP16 export-evidence checks.

#### Scenario: Both entries use one configuration
- **WHEN** a valid configuration is given to baseline and search-direct with distinct new output directories
- **THEN** input ranges and tracker settings match and search-direct records its variant; current baseline also uses the production shared-memory loader, so executable names alone do not establish a CPU-transfer comparison

#### Scenario: Output already exists
- **WHEN** the configured output directory exists
- **THEN** the benchmark fails before measurement without overwriting evidence

### Requirement: Search transfer preserves the established workload

A Backbone extracts features; Head compares them. FP16 and Float32 are 16-bit and 32-bit floating types. The benchmark SHALL retain local MJPEG luminance decoding, Float32 image preparation, initial-box setup, sequential updates, and common box/score computation. MJPEG stores each frame as JPEG. Normal initialization SHALL run one template inference; each normal update SHALL run one search inference and one Head inference.

#### Scenario: Fractional input and template work remain
- **WHEN** grayscale resize produces fractional pixels followed by ten updates
- **THEN** Float32 fractions are retained, template features are ordinarily submitted each update, and search and Head each execute ten times

#### Scenario: A historical CPU-transfer comparison is reproduced
- **WHEN** an explicitly identified historical build provides the ordinary host-transfer reference
- **THEN** the same three FP16-evidenced model hashes, input preparation, template generation/storage/submission, and Head output retrieval are used; template conversion caching, precision changes, and cross-frame parallelism are not added to one side

#### Scenario: Search features pass through shared device memory
- **WHEN** the benchmark executes the direct path
- **THEN** CPU code does not read, copy, or convert the intermediate search feature, while actual submission, binding, and synchronization costs remain measured

### Requirement: Native compatibility is checked without silent fallback

Native attributes describe actual device storage. NC1HWC2 groups channels; stride is storage distance between rows or dimensions. The benchmark SHALL validate logical roles and compatible native FP16 shapes, grouping, effective strides, and safe capacities. It SHALL diagnose unsupported attributes, import, or binding failure without host-transfer fallback. File names or logical element counts alone MUST NOT establish compatibility.

#### Scenario: Equivalent effective strides have different fields
- **WHEN** one supported native stride is zero and the other explicitly equals logical width
- **THEN** effective strides are compared using SDK semantics rather than rejecting different raw fields alone

#### Scenario: Matching element count conceals incompatible storage
- **WHEN** native grouping or types differ despite equal logical element counts
- **THEN** measurement fails with the difference instead of conversion disguised as direct transfer

#### Scenario: Known storage is checked without a model whitelist
- **WHEN** native search storage is `[1,12,16,16,8]` with 49152 bytes, or another supported contract-compatible representation
- **THEN** actual shape/stride/capacity and floating metadata are validated without a fixed model-hash whitelist or repeated affine rescaling

### Requirement: Bindings refer to current-frame features

A binding selects memory used by the device. After each ordinary submission, the benchmark SHALL re-establish the corresponding shared bindings before execution. It MUST NOT rely on initialization-time bindings surviving submission. Head SHALL run after the current search output is complete. The next frame MUST NOT overwrite shared storage before Head finishes reading.

#### Scenario: First search does not read empty storage
- **WHEN** the first search image and template feature are submitted after initialization
- **THEN** bindings are refreshed after their respective submissions and Head reads the first generated search feature

#### Scenario: Alternating inputs and replacement templates
- **WHEN** search inputs alternate A/B/A and templates change during verification
- **THEN** every output matches its independent ordinary-submission reference rather than the previous search or template

### Requirement: Lifecycle failures cannot return stale predictions

Shared storage SHALL outlive device access by both owners. After confirmed stop, consumer imports SHALL be released before producer memory and sessions. Reset or template replacement SHALL invalidate the old template; search without a valid template SHALL fail. Input rejection SHALL precede device submission. Binding/execution errors SHALL identify role, operation, and cause without old/partial outputs. Unconfirmed stop or failed release SHALL terminate the process.

#### Scenario: Head binding fails
- **WHEN** search features exist but Head binding fails
- **THEN** the frame fails without executing Head or returning the previous box, and completed stages remain in the failure record

#### Scenario: Template replacement fails
- **WHEN** template A exists and generating B fails
- **THEN** later searches cannot use A as if B had succeeded, and invalidated device sessions cannot continue until rebuilt

### Requirement: Timing includes direct-transfer costs and preserves provenance

`decode_to_result` is monotonic elapsed time from JPEG decoding to readable box/score. The benchmark SHALL retain integer-nanosecond timing and separate initialization/update samples. Rebinding and device waits SHALL remain included. Unexecuted host-fetch/copy stages SHALL be absent rather than zero-duration samples. Existing raw files, human-readable reports, machine statistics, warmup exclusion, failure retention, and exit semantics SHALL remain compatible.

#### Scenario: Binding and waiting have measurable cost
- **WHEN** a frame rebinds producer and consumer storage and waits for device completion
- **THEN** those costs remain in stage and end-to-end intervals, with no subtraction or double counting of nested spans

#### Scenario: Search output is not fetched to the host
- **WHEN** search output passes through shared memory
- **THEN** no ordinary search-output fetch/copy span is invented, while final Head retrieval remains timed

#### Scenario: Direct benchmark records its identity
- **WHEN** search-direct writes `frames.csv`, `spans.csv`, manifest, summary, and report
- **THEN** it retains `benchmark_variant=search_direct`, `search_handoff=native_fp16_shared`, binding policy, actual model attributes, shared capacity, and program/input identities

#### Scenario: Older baseline evidence lacks variant fields
- **WHEN** an existing baseline record has no explicit variant metadata
- **THEN** it remains readable, but its actual transfer path must be interpreted from its recorded build rather than assuming every current baseline binary uses host transfer

### Requirement: Numerical verification uses an independent reference

Verification SHALL compare the same Float32 inputs against independent ordinary search and Head sessions. The reference MUST NOT derive its inputs from the tested shared buffer. Verification SHALL cover first use, A/B/A inputs, template changes, independent processes, and input-sensitive outputs. Diagnostic shared-buffer reads SHALL occur after consumer completion and stay outside formal performance samples.

#### Scenario: Distinct inputs produce suspiciously identical outputs
- **WHEN** tested and reference outputs are identical across different inputs but input-sensitivity checks fail
- **THEN** equality or finite values alone do not establish verification; the missing coverage is reported

#### Scenario: Only a temporary Python probe has passed
- **WHEN** 720 probe comparisons passed but the formal C++ replay has not run
- **THEN** only low-level feasibility is established; formal numerical, replay, and performance acceptance remain incomplete

#### Scenario: Numerical tolerance is checked
- **WHEN** fixed verified models and a complete same-video replay are compared
- **THEN** fixed-input Head outputs match elementwise, box component error is at most `1e-4` pixels, and score error is at most `1e-6`; violations fail acceptance without relaxed tolerances

### Requirement: Performance conclusions require comparable identified paths

Formal comparisons SHALL fix board, Runtime 2.3.2, driver 0.9.8, models, video, initial box, warmup, frame range, rounds, and environment. Each observation-mode group SHALL have at least three rounds, with default warmup 30 updates. Reports SHALL retain per-round/combined distributions, slow and low-score frames, and actual build identities. Current baseline/direct results MUST NOT be interpreted as host-versus-shared transfer solely from their names.

#### Scenario: No speedup is observed
- **WHEN** numerically matching comparable paths show higher direct-path latency
- **THEN** all results remain and the report states no benefit without deleting samples, changing work, or inventing a post-hoc threshold

#### Scenario: Model hashes differ
- **WHEN** any model hash differs between the compared groups
- **THEN** the comparison is marked unsuitable for a single-variable speedup conclusion

#### Scenario: Observation and resource acceptance remain explicit
- **WHEN** detailed and total-only runs are evaluated
- **THEN** both modes retain at least three rounds and matched conditions; separate create/reset/destroy and sustained-resource checks are reported, unreadable metrics remain unknown, and normal exit alone does not establish no leaks

#### Scenario: Finite numerical evidence is interpreted
- **WHEN** a finite replay comparison passes
- **THEN** it does not establish tracking accuracy or absolute absence of internal hardware data movement

## REMOVED Requirements

### Requirement: 独立入口与基线兼容

**Reason**: Replaced by the English contract distinguishing current loaders from historical CPU-transfer comparisons.

**Migration**: Use "Standalone entries retain compatible configuration" and its translated scenarios. Existing measurements and runtime behavior are unchanged.

### Requirement: 搜索特征交接是唯一优化变量

**Reason**: Replaced by the English contract distinguishing current loaders from historical CPU-transfer comparisons.

**Migration**: Use "Search transfer preserves the established workload" and its translated scenarios. Existing measurements and runtime behavior are unchanged.

### Requirement: 原生特征兼容性与明确拒绝

**Reason**: Replaced by the English contract distinguishing current loaders from historical CPU-transfer comparisons.

**Migration**: Use "Native compatibility is checked without silent fallback" and its translated scenarios. Existing measurements and runtime behavior are unchanged.

### Requirement: 每帧使用当前特征且绑定有效

**Reason**: Replaced by the English contract distinguishing current loaders from historical CPU-transfer comparisons.

**Migration**: Use "Bindings refer to current-frame features" and its translated scenarios. Existing measurements and runtime behavior are unchanged.

### Requirement: 生命周期和失败不产生虚假结果

**Reason**: Replaced by the English contract distinguishing current loaders from historical CPU-transfer comparisons.

**Migration**: Use "Lifecycle failures cannot return stale predictions" and its translated scenarios. Existing measurements and runtime behavior are unchanged.

### Requirement: 直连成本完整计时且路径可追溯

**Reason**: Replaced by the English contract distinguishing current loaders from historical CPU-transfer comparisons.

**Migration**: Use "Timing includes direct-transfer costs and preserves provenance" and its translated scenarios. Existing measurements and runtime behavior are unchanged.

### Requirement: 数值验证具有独立参照

**Reason**: Replaced by the English contract distinguishing current loaders from historical CPU-transfer comparisons.

**Migration**: Use "Numerical verification uses an independent reference" and its translated scenarios. Existing measurements and runtime behavior are unchanged.

### Requirement: 性能比较保持同条件且不过度推断

**Reason**: Replaced by the English contract distinguishing current loaders from historical CPU-transfer comparisons.

**Migration**: Use "Performance conclusions require comparable identified paths" and its translated scenarios. Existing measurements and runtime behavior are unchanged.

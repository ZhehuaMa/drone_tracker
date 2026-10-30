# Spec Delta

## ADDED Requirements

### Requirement: Compatible models can be replaced by explicit paths

A Backbone extracts image features; the Head compares template and search features. The system SHALL accept separate template Backbone, search Backbone, and Head paths. Recreating the backend SHALL load those files. Contract-compatible replacements MUST NOT require recompilation or a filename, hash, or version whitelist. Recorded model identities SHALL serve diagnostics and verification only.

#### Scenario: Compatible replacement model
- **WHEN** a caller stops the old process and supplies new files with matching semantic and supported storage contracts
- **THEN** recreating the backend loads them without recompiling the program

#### Scenario: Matching shapes have different meanings
- **WHEN** a replacement model emits classification probabilities but the contract requires raw scores
- **THEN** it is semantically incompatible; the provider must establish meaning through fixed-input verification because shape checks alone do not prove compatibility

### Requirement: Model roles and logical tensor shapes match completely

NCHW orders batch, channels, height, and width. The system SHALL validate logical template `[1,3,127,127] -> [1,96,8,8]` and search `[1,3,255,255] -> [1,96,16,16]` shapes. Head inputs SHALL match these features. Head outputs SHALL be classification `[1,2,15,15]` and location `[1,4,15,15]`. Roles SHALL be unique and shape-derived, independent of names or enumeration order. Missing, extra, duplicate, or incompatible roles SHALL reject complete assembly.

#### Scenario: Head tensors are enumerated in another order
- **WHEN** Head inputs enumerate search before template and outputs enumerate location before classification
- **THEN** unique shapes determine their roles, classification remains unnormalized scores, and location retains left/top/right/bottom distance meaning

#### Scenario: Head cannot be assembled
- **WHEN** both Backbones load but Head loading or feature compatibility fails
- **THEN** assembly reports the relevant model and role, releases acquired resources, and returns no partial backend

### Requirement: Logical precision and native transfer compatibility are distinct

FP16, Float32, and affine INT8 are supported logical types, subject to their contracts. Template features SHALL use Float32 host values. Public classification/location outputs SHALL be Float32 NCHW. Quantized values SHALL retain real-number meaning. The production search edge SHALL additionally satisfy the native shared-memory contract below. Logical precision alone MUST NOT guarantee load success. Changing model files MUST NOT mean runtime requantization.

#### Scenario: Quantization differs across ordinary feature submission
- **WHEN** template integer 10 represents 1.0 at scale=0.1 and zero point=0, while Head input uses scale=0.2 and zero point=0
- **THEN** ordinary submission preserves approximately 1.0 rather than reinterpreting the original byte as 2.0

#### Scenario: Mixed logical precision is requested
- **WHEN** compatible INT8 Backbone and FP16 Head files also expose a compatible native FP16 search edge
- **THEN** the common calling interface can initialize and search without precision branches in the tracking algorithm

#### Scenario: Logical shapes pass but native storage fails
- **WHEN** a model combination passes logical tensor validation but its search edge has unsupported native storage
- **THEN** production loading fails with the actual native mismatch instead of silently selecting host transfer

### Requirement: Grayscale preparation preserves floating pixel semantics

Gray8 stores one 8-bit luminance value per pixel. The system SHALL preserve `model-input-preprocessing` crop rounding, target mean, reset, and failure rules. Template/search inputs SHALL be Float32 NCHW `[1,3,127,127]` and `[1,3,255,255]`. It SHALL convert to floating values before linear resize, replicate three channels, and retain 0–255 values. It MUST NOT divide by 255 or quantize resized values back to bytes. Retained prepared inputs MUST NOT be overwritten.

#### Scenario: Search window crosses an image edge
- **WHEN** padding and linear interpolation produce fractional pixels
- **THEN** outside pixels use the initialized target's mean and fractional results remain in the Float32 input

#### Scenario: A prepared-input lease remains held
- **WHEN** a prior template input still occupies reusable storage and another template is requested
- **THEN** preparation reports input busy, preserves the retained values, and submits no model execution

### Requirement: Template and search failure preserve existing lifecycle rules

Starting template replacement SHALL invalidate the previous template. A new template SHALL become valid only after successful compatible feature generation. Search MUST NOT overwrite template features or run without a valid template. Recoverable input rejection SHALL retain a valid template. An invalidated execution session SHALL report not ready until rebuilt. Reset SHALL clear template validity.

#### Scenario: Replacement template fails
- **WHEN** template A exists but creating template B fails
- **THEN** later searches cannot use A as if B had succeeded

#### Scenario: Recoverable search input rejection
- **WHEN** an input is rejected while the session and template remain valid, followed by a legal input
- **THEN** the legal search uses the retained template; after reset, search instead reports not ready without device execution

### Requirement: Standalone query and replay expose actual outcomes

The system SHALL provide an ARM64 standalone tool without ROS or Jetson SDK dependencies. Query SHALL report all three paths, tensor roles/attributes, and runtime versions. Replay SHALL accept local MJPEG video, an initial pixel box, and model paths, run the common tracker, and output frame, success, box, score, and duration. Invalid configuration or loading SHALL exit with a diagnostic. Failed searches MUST NOT be presented as successful boxes.

#### Scenario: MJPEG enters grayscale replay
- **WHEN** replay receives legal local MJPEG, a valid initial box, and compatible models
- **THEN** the reader extracts Gray8 luminance, the first frame initializes the real common tracker, and later frames report actual results without simulated backends or an invented initialization score

### Requirement: Verification separates implementation correctness from model quality

Fixed-input verification SHALL identify the models, inputs, versions, tolerances, and feature/Head comparisons. Supported precision combinations SHALL be evaluated under the same grayscale replay conditions. Numerical precision differences alone MUST NOT be labelled implementation defects. Color evaluation MUST NOT substitute for grayscale acceptance. Public box updates, thresholds, and states SHALL retain their contracts. Unexecuted hardware checks SHALL be listed.

#### Scenario: INT8 quality differs from FP16
- **WHEN** same-model numerical comparisons pass but grayscale INT8 tracking quality is below FP16
- **THEN** the report distinguishes model quality from backend correctness and does not claim complete platform equivalence

#### Scenario: Only build evidence exists
- **WHEN** ARM64 compilation succeeds but target-hardware replay has not run
- **THEN** only compilation is reported as passed; hardware numerical, resource, and tracking acceptance remain incomplete

### Requirement: Production search transfer requires compatible native memory

Native attributes describe device storage rather than logical tensor order. Production SHALL require matching search-output and Head-search-input FP16 grouped-channel shapes, effective strides, and safe capacities. Compatible features SHALL pass through shared device memory without CPU reads, copies, or conversion. An incompatible contract SHALL reject loading with the difference. It MUST NOT silently fall back to Float32 host transfer.

#### Scenario: Native search edge is compatible
- **WHEN** both sides have matching supported FP16 grouping, strides, and capacity
- **THEN** loading succeeds and search features reach Head through the same underlying device storage without ordinary CPU feature submission

#### Scenario: Logical element counts conceal an incompatible layout
- **WHEN** logical element counts match but native grouping or types differ
- **THEN** loading fails with that native mismatch rather than converting or falling back

### Requirement: Each search re-establishes bindings and uses current features

A binding selects the memory a model accesses. The system SHALL rebind search output after ordinary search-image submission and before execution. It SHALL rebind Head search input after ordinary template-feature submission and before Head execution. Head SHALL wait for the current search write. The next search MUST NOT overwrite shared storage before Head finishes reading it.

#### Scenario: First search uses real generated features
- **WHEN** template and search inputs are submitted after backend creation
- **THEN** Head reads the first search model's output instead of initially empty shared memory

#### Scenario: Consecutive searches refresh bindings
- **WHEN** two search frames execute consecutively
- **THEN** both frames re-establish their producer and consumer bindings and Head uses each frame's current features

### Requirement: Direct-transfer failures retain safe resource boundaries

Recoverable binding failures SHALL return the existing single-search failure outcome when device access has not started or has stopped. Unconfirmed stop or failed shared-memory release SHALL emit a fatal diagnostic and terminate the process. Destruction SHALL release the consumer import before producer storage after confirmed device stop. Reset SHALL clear template validity while retaining allocated shared memory for later per-call rebinding.

#### Scenario: One binding is rejected safely
- **WHEN** a valid template exists and a search binding fails with device access confirmed absent or stopped
- **THEN** the search fails through existing model-error semantics without terminating healthy process execution

#### Scenario: Template reset reuses shared allocation
- **WHEN** template A is reset and template B is created
- **THEN** shared memory is neither freed nor allocated again solely because of reset, and subsequent ordinary submissions still require fresh bindings

#### Scenario: Shared memory release fails
- **WHEN** a backend destruction release call fails
- **THEN** a fatal diagnostic terminates the process instead of continuing ordinary resource destruction

## REMOVED Requirements

### Requirement: 兼容模型可通过路径替换

**Reason**: Replaced by its English contract and the already implemented transfer constraints.

**Migration**: Use "Compatible models can be replaced by explicit paths" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 三模型接口匹配且完整装配

**Reason**: Replaced by its English contract and the already implemented transfer constraints.

**Migration**: Use "Model roles and logical tensor shapes match completely" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 共用执行链支持不同精度组合

**Reason**: Replaced by its English contract and the already implemented transfer constraints.

**Migration**: Use "Logical precision and native transfer compatibility are distinct" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 灰度预处理保持既有浮点语义

**Reason**: Replaced by its English contract and the already implemented transfer constraints.

**Migration**: Use "Grayscale preparation preserves floating pixel semantics" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 模板生命周期与搜索失败保持既有语义

**Reason**: Replaced by its English contract and the already implemented transfer constraints.

**Migration**: Use "Template and search failure preserve existing lifecycle rules" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 独立查询与真实跟踪回放

**Reason**: Replaced by its English contract and the already implemented transfer constraints.

**Migration**: Use "Standalone query and replay expose actual outcomes" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 验证区分实现正确性与模型质量

**Reason**: Replaced by its English contract and the already implemented transfer constraints.

**Migration**: Use "Verification separates implementation correctness from model quality" and its translated scenarios. Runtime behavior is unchanged.

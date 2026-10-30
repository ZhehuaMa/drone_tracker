# Spec Delta

## ADDED Requirements

### Requirement: Model sessions expose every fixed input and output

A session owns one loaded model and its resources. A tensor is a multidimensional model input or output. The system SHALL expose each tensor's index, name, shape, type, layout, and quantization attributes. It SHALL support the model's actual fixed input/output counts. It SHALL reject unresolved dynamic shapes.

#### Scenario: Two-input two-output model
- **WHEN** a model has two valid fixed inputs and two outputs
- **THEN** querying returns all four tensor descriptions and submitting both compatible inputs produces both outputs

#### Scenario: Unresolved shape
- **WHEN** a queried dimension has no resolved size
- **THEN** the session returns a contract diagnostic without submitting device execution

### Requirement: Inputs are validated before device submission

The system SHALL check input counts, indices, shapes, types, layouts, storage, and byte sizes before ordinary execution. It SHALL reject size multiplication overflow. Application submission types can differ from native model types only through supported conversion. Any incompatible input SHALL reject the entire execution.

#### Scenario: Second input storage is too small
- **WHEN** the first input is valid but the second buffer is one byte shorter than required
- **THEN** execution is rejected with expected and actual second-input attributes, without increasing the device execution count

#### Scenario: Duplicate index
- **WHEN** both inputs specify index 0 and index 1 is absent
- **THEN** execution is rejected without inferring roles from submission order

### Requirement: Output conversion preserves numerical meaning

Float32 and FP16 are 32-bit and 16-bit floating-point types. Affine INT8 represents real values using scale and zero point. The system SHALL convert supported logical outputs to Float32 once. It SHALL reject unsupported layouts or quantization. INT8 scale SHALL be finite and positive, with a valid zero point. Floating AFFINE metadata MUST NOT cause integer validation or rescaling. Generic execution MUST NOT add normalization, probabilities, or box decoding.

#### Scenario: Quantized output is converted once
- **WHEN** INT8 value 10 with scale=0.1 and zero point=0 has already been returned as Float32 1.0
- **THEN** the consumer reads approximately 1.0, rather than a second conversion to 0.1

#### Scenario: Floating values remain unchanged
- **WHEN** a model returns valid floating output 128.0
- **THEN** the session does not divide it by 255 or convert it to a probability, and the returned description identifies the actual type and layout

#### Scenario: Floating tensor has affine metadata
- **WHEN** an FP16 or Float32 tensor carries AFFINE metadata and scale/zero-point fields
- **THEN** its floating values are accepted without rescaling or rejection using INT8 parameter ranges

### Requirement: Synchronous results own independent output storage

One session SHALL execute serially. Callers MUST finish all session calls before destruction. Successful synchronous return SHALL confirm the device has finished accessing that call's inputs and outputs. Returned host results SHALL own storage independently of temporary SDK outputs. Later execution or session destruction MUST NOT invalidate retained results.

#### Scenario: A retained feature outlives its session
- **WHEN** a consumer retains the first output, performs another inference, and destroys the producing session
- **THEN** the first output remains readable and unchanged, including when used as another model's input

### Requirement: Failure reports preserve the device-stop boundary

The system SHALL distinguish load, contract, storage-type, readiness, and execution failures. Diagnostics SHALL identify the model, operation, cause, and relevant tensor mismatch. Failure MUST NOT return partial outputs. Partial initialization SHALL release acquired resources. A submitted execution that fails SHALL invalidate the session after confirmed stop. Unconfirmed device stop or destruction failure SHALL emit a fatal diagnostic and terminate the process.

#### Scenario: Output retrieval fails after synchronous completion
- **WHEN** blocking execution succeeded but ordinary output retrieval then fails
- **THEN** the call returns an execution failure without partial outputs, resources are safely reclaimed, and later execution reports not ready

#### Scenario: Device stop cannot be confirmed
- **WHEN** execution and its subsequent wait both fail
- **THEN** the process reports the model and operation and terminates without normal destruction of potentially active device storage

### Requirement: General sessions preserve the existing YOLO contract

YOLO is the detection model. General execution SHALL retain the input, six-output, class-count, failure, command-line, and asynchronous-call contracts in `rk3576-yolo-backend`. NanoTrack support MUST NOT relax YOLO-specific validation.

#### Scenario: General model is unsuitable for YOLO
- **WHEN** the generic session loads a two-input two-output model but a caller tries to use it as YOLO
- **THEN** YOLO assembly reports a contract mismatch

#### Scenario: Asynchronous detection is stopped
- **WHEN** a caller requests stop while YOLO inference is active
- **THEN** it waits for the detection thread before destroying execution resources, preserving existing output and stop semantics

### Requirement: Native tensor attributes remain separately queryable

Native attributes describe device storage, which can group channels differently from logical tensor shapes. The system SHALL expose native layout, channel grouping, stride, and capacity for every input and output. This query MUST NOT replace or change logical shape, type, layout, or quantization queries.

#### Scenario: Native storage groups logical channels
- **WHEN** an output has a fixed logical shape but grouped native storage
- **THEN** native queries expose its actual grouping, stride, and capacity while logical queries retain their original results

### Requirement: Partial submission and binding support shared memory

Binding selects caller-owned device memory for a tensor. The session SHALL support partial ordinary input submission alongside explicit shared-memory bindings. It SHALL validate that capacity after the binding offset covers the native tensor. Bindings SHALL be re-established after each partial submission before execution. Existing whole-input submission and whole-output retrieval SHALL remain available.

#### Scenario: Only one input is ordinarily submitted
- **WHEN** one input of a two-input model is submitted and the other is bound to shared memory
- **THEN** both sources are validated without rejecting the bound input as a missing ordinary submission

#### Scenario: Binding capacity is insufficient
- **WHEN** remaining capacity after the offset is smaller than the tensor requires
- **THEN** binding fails with required and actual capacity, without submitting device execution

#### Scenario: New submission invalidates prior bindings
- **WHEN** another partial submission follows a completed submit-bind-run sequence
- **THEN** related bindings must be established again before the next execution

### Requirement: Completion can be confirmed without fetching output

The system SHALL allow synchronous completion confirmation without ordinary output retrieval when bound output already resides in shared device memory. Confirmation SHALL use the existing device-stop evidence. Successful submission alone MUST NOT be treated as completed device access.

#### Scenario: Bound output needs no host copy
- **WHEN** execution writes output through an explicit shared-memory binding
- **THEN** the caller can confirm completion without ordinary output retrieval or a Float32 host copy of that output

### Requirement: Shared memory has explicit ownership and failure rules

The system SHALL support allocation, equivalent-handle import, and release of registered shared device memory. Returned capacity SHALL cover the requested or original capacity. Consumer imports SHALL be released before producer storage, after confirmed device stop. Unconfirmed stop or a failed release SHALL emit a fatal diagnostic and terminate the process before unsafe reclamation.

#### Scenario: Allocation returns insufficient capacity
- **WHEN** returned capacity is below the requested byte count
- **THEN** allocation fails without returning a usable handle

#### Scenario: Import refers to the same storage
- **WHEN** an equivalent identifier for allocated memory is imported
- **THEN** the import refers to the same underlying storage with at least the original capacity

#### Scenario: Shared owners are destroyed in order
- **WHEN** producer and consumer sessions own original and imported handles and device access has ended
- **THEN** the imported handle is released before original storage; unconfirmed stop instead terminates the process without releasing that storage

### Requirement: Missing shared-memory operations preserve ordinary execution

Shared-memory binding, allocation, import, and release SHALL be optional operations. Missing operations SHALL produce explicit capability diagnostics when requested, without changing ordinary execution. Partial host submission SHALL remain available independently. Loading SHALL require successful native input/output queries. Production NanoTrack loading SHALL additionally require shared-memory operations under its separate contract.

#### Scenario: Runtime interface omits shared-memory operations
- **WHEN** a session can query logical/native attributes and execute ordinary calls but has no shared-memory operations
- **THEN** ordinary execution retains its behavior and extension requests report unavailable capabilities

#### Scenario: Native attribute query fails during loading
- **WHEN** a native input or output query fails
- **THEN** loading fails diagnostically rather than promising an ordinary-execution fallback

## REMOVED Requirements

### Requirement: 单模型执行支持查询与多输入输出

**Reason**: Superseded by the English contract with the implementation corrections recorded in this change.

**Migration**: Use "Model sessions expose every fixed input and output" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 全部输入在设备提交前校验

**Reason**: Superseded by the English contract with the implementation corrections recorded in this change.

**Migration**: Use "Inputs are validated before device submission" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 类型转换不改变模型数据语义

**Reason**: Superseded by the English contract with the implementation corrections recorded in this change.

**Migration**: Use "Output conversion preserves numerical meaning" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 同步返回与独立输出所有权

**Reason**: Superseded by the English contract with the implementation corrections recorded in this change.

**Migration**: Use "Synchronous results own independent output storage" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 失败结果与设备停止边界明确

**Reason**: Superseded by the English contract with the implementation corrections recorded in this change.

**Migration**: Use "Failure reports preserve the device-stop boundary" and its translated scenarios. Runtime behavior is unchanged.

### Requirement: 通用化保持已有 YOLO 行为

**Reason**: Superseded by the English contract with the implementation corrections recorded in this change.

**Migration**: Use "General sessions preserve the existing YOLO contract" and its translated scenarios. Runtime behavior is unchanged.

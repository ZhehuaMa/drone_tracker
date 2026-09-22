## Why

RK3576 目标平台上，NanoTrackV3（单目标跟踪模型组合）的生产代码在“搜索 Backbone”和“Head”这两个模型之间交接特征时，仍然要先把搜索 Backbone 的输出从设备内存复制回 CPU 主机内存、转换成 Float32，再重新提交给 Head——这一步 CPU 往返对每一帧跟踪都会发生。已经完成的独立性能对照测试（`nanotrackv3-search-direct-benchmark`）在真实 RK3576 板卡上证明：把这次交接改成让 Head 直接读取搜索 Backbone 写在设备内存里的原生 FP16 数据、不经过 CPU，能让每帧端到端耗时降低约 8%~11%，且数值结果与现有实现完全一致。但这个验证结果目前只停留在一个独立的测试程序里，生产节点还没有用上它。这次改造把该验证结果落地到生产代码，让实际跟踪流程获得这部分性能收益。

## What Changes

- 扩展生产会话层 `rk3576::RknnSession`（RK3576 平台上代表一个已加载 RKNN 模型的执行单元），使其在保留现有 `execute()`（一次性提交全部输入、运行模型、取回全部输出为 Float32 主机数据）不变的前提下，新增可选的原子操作：
  - 查询 RKNN 原生张量属性（设备实际读写的排列方式，而不是对外统一简化后的逻辑形状/布局）；
  - 允许只提交模型的一部分输入（而不是必须一次性提交全部输入）；
  - 把某个输入或输出绑定到一块共享设备内存，而不通过 CPU 传输；
  - 单独确认一次同步执行已完成，但不取回该次输出；
  - 分配、从文件描述符导入、释放共享设备内存。
- 这些新增操作只在提供了共享设备内存相关的 RKNN 调用（新增的可选 API 扩展）时才可用；未提供时的会话行为与今天完全一致，YOLO（目标检测模型）路径不受影响。
- 修改生产 NanoTrack 后端 `rk3576::RknnNanoTrackBackend`：在加载时探测搜索 Backbone 输出与 Head 搜索输入的原生属性是否满足直连所需的契约（原生形状、FP16 类型、通道分组、跨度、容量一致）；满足则改为“搜索 Backbone 输出 → Head 搜索输入”全程走设备内存直连，不再经过 CPU 复制或类型转换；不满足则加载直接失败并报告具体差异，不静默退回现有的 CPU 交接方式。
- 模板特征交接、Head 的两个输出取回、跟踪算法的其余全部行为（裁剪、缩放、分数计算、框更新、状态机语义）保持不变。
- **不新增**运行时开关：一个后端实例要么全程使用直连交接（契约满足时），要么加载失败（契约不满足时），不存在“先直连、失败后自动改用 CPU 交接”的运行期回退路径。

## Capabilities

### New Capabilities
（无）

### Modified Capabilities
- `rk3576-model-session`：扩展执行会话的能力边界，新增原生属性查询、部分输入提交、输入输出与共享设备内存的绑定、仅确认完成而不取回输出、共享设备内存的分配/导入/释放；明确这些新增能力在缺少对应扩展 API 时必须被拒绝而不是静默降级，且失败/设备停止确认语义与现有执行路径保持一致。
- `rk3576-nanotrack-backend`：将“共用执行链支持不同精度组合”需求中“首版 SHALL 通过 Float32 特征交接模型”这一现状描述，改为“搜索 Backbone 与 Head 之间的特征交接 SHALL 优先采用原生共享设备内存直连,不满足直连契约时加载 SHALL 失败”，并新增对应的契约校验、失败报告和资源生命周期要求。

## Impact

- 代码：`src/platform/rk3576/rknn_session.{h,cpp}`、`src/platform/rk3576/rknn_api.h`（新增可选的共享内存 API 扩展结构体）、`src/platform/rk3576/rknn_nanotrack_backend.{h,cpp}`、`src/platform/rk3576/rknn_nanotrack_contract.{h,cpp}`（原生属性契约校验）。
- 不受影响：`src/platform/rk3576/rknn_yolo_backend.cpp`、`rknn_runtime.{h,cpp}`（继续只使用 `execute()`）、`src/hybrid_tracker/`（`NanoTracker`、`model::NanoTrackBackend` 公共接口、ROS 话题、参数含义均不变）。
- 参考实现：`tests/nanotracev3_performance_benchmarks/search_direct/`（独立性能对照程序）中已验证的共享内存交接机制、原生属性校验逻辑和数值/性能证据，作为本次生产化改造的既有事实依据，不重新验证其本身的正确性。
- 测试：新增/扩展 `src/platform/rk3576/tests/` 下针对 `RknnSession` 新增能力和 `RknnNanoTrackBackend` 直连路径的契约、失败场景单元测试；实机验收需要在 RK3576 板卡上用真实录像做一次生产路径与已有 benchmark `search_direct` 数值结果的逐帧对比。

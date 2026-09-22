## 1. 扩展 `RknnSession` 的原生属性查询能力

- [x] 1.1 在 `src/platform/rk3576/rknn_session.h`/`.cpp` 中新增 `nativeInputs()`/`nativeOutputs()` 查询能力(基于 `RKNN_QUERY_NATIVE_INPUT_ATTR`/`RKNN_QUERY_NATIVE_OUTPUT_ATTR`),并验证既有 `inputs()`/`outputs()` 的返回值和调用者不受影响。
- [x] 1.2 在 `src/platform/rk3576/tests/rknn_session_test.cpp` 中新增覆盖原生属性查询的单元测试,验证 `rk3576-model-session` delta spec 中"支持查询设备原生张量属性"需求的两个场景(原生排列与逻辑形状不同时两者互不影响)。

## 2. 扩展 `RknnApi` 边界,新增可选共享内存扩展

- [x] 2.1 在 `src/platform/rk3576/rknn_api.h` 中新增 `RknnSharedMemoryApi` 结构体(`create_mem`/`create_mem_from_fd`/`destroy_mem`/`set_io_mem`/`mem_sync` 五个函数指针),并提供返回真实 `librknnrt` 绑定的构造函数,不修改现有 `RknnApi` 的字段。
- [x] 2.2 给 `RknnSession::load()` 新增接受 `(path, RknnApi, RknnSharedMemoryApi)` 的重载;确认原有 `(path, RknnApi)` 重载行为不变,且 YOLO 相关测试(`rknn_yolo_backend_test.cpp` 等)无需修改即可通过。

## 3. 在 `RknnSession` 中新增原子执行与共享内存操作

- [x] 3.1 把 `execute()` 内部逻辑拆分为 `submit(vector<SessionInput>, allow_partial)`、`run()`、`fetch()` 三个独立方法,`execute()` 重写为三者的组合,并验证既有 `rknn_session_test.cpp` 全部用例无需修改即通过。
- [x] 3.2 新增 `bind(rknn_tensor_attr, rknn_tensor_mem*)` 与 `confirmCompleted()` 方法,并新增单元测试覆盖"部分输入提交与显式绑定支持零拷贝路径"和"支持确认执行完成而不取回输出"两条 delta spec 需求的全部场景(只提交部分输入、绑定容量不足、绑定不跨提交生效、直连输出不触发普通取回)。
- [x] 3.3 新增 `allocateShared(bytes)`/`importShared(rknn_tensor_mem&)`/`releaseShared(rknn_tensor_mem*)`/`syncFromDevice(...)` 方法,并新增单元测试覆盖"共享设备内存生命周期与失败边界明确"需求的全部场景(分配容量不足、导入引用同一存储、释放顺序与设备停止确认)。
- [x] 3.4 新增单元测试验证"扩展能力缺失时不影响既有执行路径":未提供 `RknnSharedMemoryApi` 的会话上,`execute()` 行为与当前实现一致,调用共享内存相关方法返回能力缺失诊断而非崩溃。

## 4. 扩展 NanoTrack 契约校验,支持原生直连判定

- [x] 4.1 在 `src/platform/rk3576/rknn_nanotrack_contract.{h,cpp}` 中新增原生属性兼容性校验函数(参照 `tests/nanotracev3_performance_benchmarks/search_direct/feature_transfer.cpp` 里 `matchSharedFeature()` 的判定逻辑:原生形状、FP16 类型、通道分组、有效跨度、安全容量),返回明确的兼容或不兼容差异说明。
- [x] 4.2 新增单元测试覆盖该校验函数在"两端逻辑元素数相同但原生通道分组或类型不匹配"等场景下的拒绝行为,不依赖真实 RKNN 设备。

## 5. 改造 `RknnNanoTrackBackend` 装配流程,加入直连契约探测

- [x] 5.1 修改 `RknnNanoTrackBackend::load()`:在三模型逻辑契约校验(既有 `validateNanoTrackSessions`)通过后,调用第 4 步新增的原生兼容性校验;不满足时返回失败诊断并回收已加载的三个会话,不返回可用后端。
- [x] 5.2 契约满足时,在 `load()` 内一次性调用 `allocateShared`/`importShared` 建立共享内存绑定关系,并记录 `search_handoff`/`bind_policy` 等身份字段用于诊断追溯;新增单元测试验证"满足直连契约"和"原生属性不兼容"两个场景。

## 6. 改造 `search()` 调用顺序,实现搜索特征的原生共享内存直连

- [x] 6.1 修改 `RknnNanoTrackBackend::search()`:按"提交搜索图像 → 绑定搜索输出 → 运行搜索 Backbone → 确认完成 → 提交模板特征 → 绑定 Head 搜索输入 → 运行 Head → 取回 Head 输出"的顺序调用第 3 步新增的原子操作,搜索特征全程不经过 CPU 复制或类型转换。
- [x] 6.2 新增单元测试验证"首次搜索不得读取空缓冲区"和"连续帧的绑定重建"两个场景:每帧都重新建立绑定,不复用旧绑定或初始化时的空数据。

## 7. 落实失败语义与资源生命周期的生产化处理

- [x] 7.1 确认并测试:装配期契约不匹配、绑定被拒绝且设备未提交执行时,`search()`/`setTemplate()` 返回既有的单帧失败结果(映射到 `NanoTracker::update()` 现有的 `-1.0f` 返回路径),不终止进程。
- [x] 7.2 确认并测试:设备执行已提交但无法确认停止、或共享内存释放调用失败时,复用既有 `model::terminateDeviceFailure()` 终止进程;新增覆盖该路径的单元测试(通过注入失败的 mock API 触发)。
- [x] 7.3 修改 `RknnNanoTrackBackend::reset()`:确认其只清除 `template_feature_` 的有效性,不释放或重新分配共享内存;新增单元测试验证"重置不影响共享内存分配"场景。
- [x] 7.4 修改后端析构逻辑:按"先释放 Head 侧导入句柄、再释放搜索 Backbone 侧原始内存"的顺序回收共享内存,且仅在确认设备停止访问后执行;新增单元测试验证该释放顺序与"释放失败终止进程"场景。

## 8. 契约与回归验证

- [x] 8.1 运行 `colcon build --packages-select drone_tracker --cmake-args -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON`(RK3576 目标环境),确认新增代码可编译且不影响 YOLO 相关目标的构建。
- [x] 8.2 运行 `hybrid_tracker_tests` 及 `src/platform/rk3576/tests/` 下全部单元测试,确认第 1~7 步新增测试与既有测试(包括 `rknn_nanotrack_backend_test.cpp`、`rknn_yolo_backend_test.cpp`)全部通过,不依赖真实硬件。

## 9. 实机验收(需要 RK3576 板卡)

- [x] 9.1 在 RK3576 板卡上用现有真实回放录像运行改造后的生产 `RknnNanoTrackBackend`,采集逐帧框和分数。
- [x] 9.2 将 9.1 的逐帧结果与 `nanotrackv3-search-direct-benchmark` 已产出的 `search_direct` 侧数值结果对比,确认框各分量绝对误差不超过 `1e-4` 像素、分数误差不超过 `1e-6`;产出对比报告(标注所用板卡编号、模型指纹、录像身份),作为本次改造的实机验收证据。
- [x] 9.3 在验收报告中明确列出因缺少多板卡样本而未覆盖的范围(仅在已用板卡上验证,不泛化为"全部 RK3576 硬件批次均有此收益")。

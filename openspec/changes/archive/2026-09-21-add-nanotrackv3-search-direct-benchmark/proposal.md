## Why

现有 NanoTrackV3 性能基线会把搜索特征取回 CPU、整理后再交给匹配模型，无法单独测量省去这段处理能节省多少时间。开发板 2 已验证同一组模型可以共享搜索特征内存，本变更将其变成可与 baseline 公平对照的独立测试程序。

## What Changes

- 新增 C++17 程序 `nanotrackv3_search_direct`，源码位于 `tests/nanotracev3_performance_benchmarks/search_direct/`。复用 baseline 的配置、运行框架和统计口径。
- 只改变搜索 backbone（提取搜索图像特征的模型）到 head（比较模板与搜索特征的模型）的交接方式：从 CPU Float32 交接改为原生 FP16 共享内存。Float32 和 FP16 分别表示 32 位和 16 位浮点数据。
- 保持同一组三份模型、灰度解码、搜索图像输入准备、模板生成及每帧普通提交、最终输出读取和公共跟踪算法。示例：处理 B 帧时，原来由 CPU 取回 B 的搜索特征并重新提交；新程序让搜索模型写入共享内存 S，head 直接读取 S 中的 B 特征。
- 每帧先普通提交输入，再重新绑定对应共享内存。绑定表示指定模型输入或输出所用的内存，不是复制特征。按已验证的同步顺序执行，不引入跨帧并行。
- 增加原生属性检查、资源及失败处理、直连路径身份、绑定计时和独立数值对照。绑定或兼容性检查失败时明确退出，不回退为 CPU 路径。
- 将 2026-09-20 的 720 次实机对照及失败顺序纳入设计依据。后续仍须完成正式 C++ 实现的回放、性能和资源验收；已有探针不代表这些任务完成。

## Capabilities

### New Capabilities

- `nanotrackv3-search-direct-benchmark`：仅搜索特征直连的独立性能测试、共享缓冲区执行顺序、单变量对照及验收要求。

### Modified Capabilities

无。`nanotrackv3-performance-baseline` 继续要求 Float32 特征交接；`rk3576-model-session` 的独立输出所有权和 `rk3576-nanotrack-backend` 的生产浮点交接约定保持不变。新路径由测试专用后端实现，不改写现有能力的含义。

## Impact

- 新增测试入口、专用后端、受控测试及使用说明；接入仓库构建和无 ROS 的独立构建。
- 复用 `baseline::runBenchmark` 的 `ModelLoader` 注入点，以及现有预处理和公共 NanoTracker。共享报告与观测代码只做向后兼容的必要扩展。
- 设备内存分配、导入、绑定、释放和原生属性查询使用现有 RKNN API。正式环境固定 Runtime 2.3.2、驱动 0.9.8，不新增平台库、不转换模型。
- 不改变 ROS、YOLO、DCF、卡尔曼滤波、生产工厂或 baseline 默认执行路径。构建 SDK 与实机 Runtime 的区别按 `AGENTS.local.md` 处理。
- 临时验证脚本和完整原始数据保留在开发板 `/tmp/nanotrack-search-direct-check-20260920/`，本机副本在 `/private/tmp/nanotrack-search-direct-check-20260920/`。设计记录核心结果和身份，不依赖临时目录永久存在。

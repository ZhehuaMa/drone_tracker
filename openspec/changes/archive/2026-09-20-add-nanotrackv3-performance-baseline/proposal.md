## Why

目前无法用一套可复查的数据回答：NanoTrackV3 在 RK3576 上从图像解码到输出跟踪框和分数需要多久，以及时间主要消耗在哪个阶段。需要建立与当前实现对齐的性能基线，作为后续优化的比较依据。

## What Changes

- 新增独立 C++17 性能测试程序，位于 `tests/nanotracev3_performance_benchmarks/baseline`，保留指定目录拼写。
- 仅测试 NanoTrackV3，明确不包含 YOLO、DCF、卡尔曼滤波或 ROS 消息发布。通过配置提供首帧目标框。
- 首版仅接受本地 MJPEG 录像。MJPEG 是逐帧独立 JPEG 编码的视频；复用生产亮度解码路径，输出原尺寸八位灰度图。
- 模板 backbone、搜索 backbone 和 head 均使用 FP16 模型，即 16 位浮点模型。Backbone 提取特征，head 比较模板和搜索特征；CPU 侧继续使用当前 Float32 数据交接和前后处理。
- 以 `decode_to_result` 作为唯一端到端延迟，从当前帧开始解码到结果可读。记录前处理、推理、后处理及更细阶段；不提供 `read_to_result`、`read_demux` 或相关指标。
- 保留预热、初始化、正式更新及失败的全部原始测量记录，输出机器可读 JSON 汇总和人类可读 Markdown 报告。
- 为复用代码增加默认关闭的计时观测边界，不改变算法计算、模型调用频率和生产运行行为。

例如，旧 NanoTrack probe 在读取图像后才开始计时，只能看到一次更新的合计耗时。新程序对同一录像和初始框逐帧记录解码至结果延迟，并分别展示前处理、搜索模型、head 和后处理耗时，之后可从原始记录重新计算统计结果。

## Capabilities

### New Capabilities

- `nanotrackv3-performance-baseline`：独立 NanoTrackV3 FP16 性能基线，包含输入与执行范围、分阶段计时、原始数据、报告和可复现验证。

### Modified Capabilities

无。复用 `rk3576-nanotrack-backend`、`model-input-preprocessing` 和 `rk3576-video-frame-source` 的既有计算与解码语义。直接调用视频读取器不改变生产帧源的播放节奏、队列或重启约定；benchmark 的 FP16 限制不收窄通用后端支持的精度组合。

## Impact

- 新增测试目录及构建接入；入口、配置、计时记录、统计、报告与测试均使用 C++。
- 涉及 `src/hybrid_tracker/nano_tracker.cpp`、RK3576 前处理、模型后端、会话和 MJPEG 读取器的可选观测接口。
- 复用 OpenCV、RKNN、FFmpeg、JPEG 库及仓库已有 OpenSSL 文件指纹方案；不引入 ROS 运行依赖或新的模型转换任务。
- 实机基线固定 Runtime 2.3.2、RKNPU 驱动 0.9.8。容器构建仅证明可构建，不代替实机性能与数值验证。
- 与 `assemble-rk3576-tracker-replay` 的完整跟踪回放验收独立，不改变其未完成的质量验收结论。

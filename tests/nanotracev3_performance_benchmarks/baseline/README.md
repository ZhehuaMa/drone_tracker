# NanoTrackV3 FP16 性能基线

本程序仅运行公共 NanoTracker 和 RK3576 后端。输入为本地 MJPEG 录像，即每帧独立 JPEG 编码的视频。解码直接得到原尺寸八位亮度，不经过彩色往返转换。程序不启动 ROS，不加载 YOLO，也不运行 DCF 或卡尔曼滤波。

FP16 指导出模型采用 16 位浮点精度。主机前处理和模型之间交接仍使用 Float32，即 32 位浮点。程序不会转换或重新训练模型。

## 构建

使用仓库 `AGENTS.local.md` 指定的 ARM64 镜像。仓库入口为 `./build.sh --test`，安装结果在 `.docker-artifacts/install/drone_tracker/lib/drone_tracker/nanotrackv3_baseline`。

也可在具有 OpenCV、FFmpeg、JPEG、OpenSSL 和 RKNN SDK 的 ARM64 环境独立构建，不需要 ROS：

```sh
cmake -S tests/nanotracev3_performance_benchmarks/baseline -B build-baseline \
  -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON \
  -DRKNN_INCLUDE_DIR=/opt/rknn-runtime/include \
  -DRKNN_LIBRARY=/opt/rknn-runtime/lib/librknnrt.so
cmake --build build-baseline -j4
LD_LIBRARY_PATH=/opt/rknn-runtime/lib ctest --test-dir build-baseline --output-on-failure
build-baseline/nanotrackv3_baseline --help
```

每次构建基准程序前会检查提交号和源码差异，生成嵌入程序的身份快照。源码身份内容不变时不会重写头文件，也不会因此重新编译。此过程需要可读取的 Git 仓库；Git 查询失败会使构建失败。构建期间应保持源码和 Git 状态稳定，避免一边修改一边编译。仓库入口和独立入口共用此机制。

将程序放入开发板专用目录，使用板端系统的 Runtime 2.3.2 和驱动 0.9.8。不要用构建 SDK 的库覆盖系统库。容器测试不能代替实机验证。

## 准备输入和精度证据

1. 准备本地普通 MJPEG 文件及首帧框。框为原图像素坐标 `[x, y, width, height]`，x/y 是左上角。宽高必须为正。部分越界按生产跟踪器原有裁剪和填充规则处理。
2. 准备模板 backbone、搜索 backbone 和 head 三份 FP16 模型。Backbone 提取特征；head 比较模板与搜索特征。必须保存导出记录，不能仅凭文件名宣称 FP16。
3. 为每份模型准备一个导出证据索引。下面例子中的 SHA-256 是文件内容指纹，必须替换成真实值。`source_record` 指向已有原始转换日志或清单；索引应由这些记录整理，不能凭运行时接口类型补造精度声明。

```yaml
%YAML:1.0
---
role: "template_backbone"
model_sha256: "实际模型的64位十六进制SHA-256"
precision: "FP16"
status: "success"
source_record: "original-export-record.json"
source_record_sha256: "原始记录的SHA-256"
```

原始记录必须能追溯模型导出所用精度及实际文件身份。索引是对已有证据的整理，不是内部算子精度的运行时探测。INT8、混合精度、指纹不符、缺失或矛盾证据都不能作为本基线模型。程序额外检查实际接口为浮点；浮点属性中附带的 AFFINE 元数据不会单独导致拒绝。

4. 创建 `evidence/models.yaml`，三项角色分别为 `template_backbone`、`search_backbone`、`head`：

```yaml
%YAML:1.0
---
schema_version: 1
models:
  - { role: "template_backbone", sha256: "模型指纹", precision: "FP16", export_record: "template.yaml", export_record_sha256: "索引指纹" }
  - { role: "search_backbone", sha256: "模型指纹", precision: "FP16", export_record: "search.yaml", export_record_sha256: "索引指纹" }
  - { role: "head", sha256: "模型指纹", precision: "FP16", export_record: "head.yaml", export_record_sha256: "索引指纹" }
```

5. 复制 `config.example.yaml` 并设置真实路径、板号和首帧框。所有相对路径相对于其所在配置或证据文件解析；不展开 `~`、环境变量或 shell 表达式。OpenCV YAML 文件必须保留 `%YAML:1.0` 头。

## 运行与对照

```sh
./nanotrackv3_baseline config.yaml
```

输出目录必须不存在，父目录必须已存在。默认先初始化并预热 30 次更新，再从录像首帧开始运行三轮。每轮重新建立模板，模型会话复用。`max_updates: 0` 表示读到文件结束，正整数表示每轮更新上限。预热不足会失败，不会自动减少预热数。

每帧按顺序处理，不按录像帧率休眠，也不因为低分重置或跳帧。首帧只初始化模板，不输出预测分数。预热帧、早期更新、慢帧和失败记录均保留。

使用第二份配置，将 `detailed_observation` 改为 0，并使用新的输出目录，可关闭细分观测、保留总计时。两组均执行相同预热和至少三轮正式测试。对齐帧号比较框与分数；实机框各分量绝对误差不得超过 1e-4 像素，分数不得超过 1e-6。另比较两组延迟分布，观测开销不从原始数据中扣除。

## 数据格式版本 1

- `manifest.json`：完整配置、输入/模型/程序指纹、代码快照指纹、实际模型属性及 Runtime/驱动、板号、环境身份和最终状态。源码差异与新增文件保存在 `code_snapshot.txt`。导出证据及原始记录另存副本。
- `frames.csv`：运行/轮次/阶段/操作/帧号、原始 PTS、框、分数、顶层耗时和错误。PTS 是录像显示时刻，时间单位见 manifest；缺失时 `pts_valid=0`。初始化框的 `bbox_kind=input`，更新框为 `prediction`。
- `spans.csv`：区间 ID、父区间、模型角色、相对进程启动时单调时钟起点的整数纳秒和完成状态。`residual_ns` 是父区间扣除直接子区间后的剩余时间。
- `environment.csv`：计时外采样的温度、CPU/NPU 频率和策略。不能读取时记录原因，不提权、不修改频率。
- `summary.json` 和 `report.md`：由落盘 CSV 计算的统计，分别面向程序和中文阅读。
- `diagnostics.log`：准备和运行诊断。SDK 自身日志也可能出现在标准错误流，部署时应另保存控制台日志。

CSV 的空单元格表示未执行或无有效值；初始化分数与后处理为空。JSON 中原始 64 位时间及纳秒统计用十进制字符串表示，避免读取端精度丢失。浮点结果保存可往返精度。状态失败的帧不混入成功更新分布。

唯一端到端指标 `decode_to_result` 从实际 JPEG 解码入口开始，到 `init()` 或 `update()` 返回结束。构造读取器时提前解码的首帧也计入。模型加载、解封装、记录写盘和环境采样在外部完成。父子耗时不能重复相加，`other_ns` 为总区间减去互斥顶层区间。

统计按每轮及合并样本区分初始化、更新和失败。P50/P90/P95/P99 使用排序后第 `ceil(p*N)` 项，总体标准差使用 N 作分母。空样本为 null。解码至结果处理速率等于有效更新数除以其累计延迟；两帧耗时 20 ms、30 ms 时为 40 帧/秒。该速率不包含读盘、写盘和等待。

## 退出与恢复边界

运行中状态为 `running`。正常 EOF 或帧上限且存在有效正式更新，数据和报告完整时，状态为 `complete`、退出码为 0。配置错误退出 2；运行或写盘失败退出 1；处理到 SIGINT/SIGTERM 后状态为 `interrupted`、退出 130。

每帧使用固定容量的内存区间缓冲，帧结束后同步排空。可处理的异常尽力保留失败帧和部分报告。输出介质故障时以标准错误流报告，不能保证还能在该介质生成总结。SIGKILL、设备致命退出或断电可能留下 `running` 和不完整尾部；完整 CSV 行可用于诊断，不应直接认定为完整基线。

性能基线不代表跟踪质量验收。即使取得低延迟，没有合适标注或质量分析也不能宣称质量达标。

## 独立重算与数值核对

启用测试构建后提供 `baseline_verify_records`。它读取 CSV，独立计算均值、总体标准差、分位数和处理速率，并检查父子区间、顶层时间、模型调用次数和报告样本数。第二个可选目录用于逐帧比较框和分数。

```sh
./baseline_verify_records baseline-run-001
./baseline_verify_records baseline-run-001 total-only-run-001
```

成功时输出 JSON，包含检查数量和最大框/分数误差。任一项不符返回非零。报告生成与核对工具不共享分布计算函数。SDK 调用阶段记录的是主机耗时，包含等待和转换，不称为纯 NPU 运算时间。

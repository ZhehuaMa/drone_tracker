# RK3576 NanoTrack 局部窗口验证

本目录验证局部窗口准备是否保持旧模型输入，并提供同输入的恢复耗时对照。恢复是从旧检测框建立模板，再按时间顺序跟踪其后的历史帧。工具使用仓库真实 `NanoTracker` 和 `NanoDcfTracker`，后者在关闭 DCF（灰度相关滤波跟踪）时仍保留中心卡尔曼滤波，即依据时间戳平滑中心位置。

## 旧参考与输入边界

`whole_frame_preprocessor.h/.cpp` 来自提交 `ea9cd822b9717b2759ece8e251bb977c3f88449b` 的 `src/platform/rk3576/nanotrack_preprocessor.h/.cpp`。具体来源见 `reference_source.txt`。参考只改类名、命名空间和结果类型引用，保留整帧 Float32 转换、整帧补边、裁剪、浮点缩放、全图均值及结果租约。Float32 是 32 位浮点；租约保证消费者持有输入时，缓存不会被后续调用覆盖。生产节点不链接此参考，诊断程序也不安装为运行时回退。

指定输入来自录像 `test-01-20260929-1513`，录像 SHA-256 为 `11d45885dc32859c5aba16541ecc83f769ad47ba9ae66b54908b19d805d4c152`。原重建源码提交为 `09006db9ea11bd4fa137efc47ecb117660e0fad2`。25 帧对应从 0 开始的视频索引 4146～4170。每个处理时刻选择不晚于该时刻的最近录像帧。该映射没有还原原始未压缩相机帧，也没有还原当日完整并发负载。结果不能作为现场约 388 ms 的分项测量。

本仓库不保存这些大图像和模型。现有输入包只读使用，不重新解码录像来替换输入。配置的 `bbox` 固定为 `[756.689,461.019,39.712,36.7552]`。样例模型指纹沿用原生产证据，仍须与实际二进制核对。输入样例的时间戳和像素指纹是必须填写的占位符，不能用于正式测量；不要根据视频帧率推造时间戳。

## 配置与只读检查

将 `config.example.yaml`、`input-manifest.example.yaml` 和 `environment-notes.example.txt` 复制到独立实验目录。配置相对路径以配置文件目录为基准。帧文件路径以清单目录为基准，不能包含 `..`。时间戳用十进制字符串表示微秒，避免 OpenCV YAML 对大整数的精度限制。

输入清单必须有 25 个条目。每项记录 `order`、`video_index`、`timestamp_us`、`path` 和 `pixel_sha256`。像素 SHA-256 按解码后的 Gray8 有效像素计算：从上到下逐行，每行恰好 1920 字节，不包含行末填充。图像必须为 1920×1200 单通道 8 位灰度，不能隐式把彩色图转成灰度。

例如，第一个条目的 `order=0`、`video_index=4146`，其时间戳和指纹从原输入包读取。工具先校验全部清单、图像和时间顺序，再创建帧和运行恢复。缺失、尺寸错误、像素变化或乱序都退出失败。

```bash
nanotrack_window_recovery --check-inputs experiment/config.yaml
```

此模式只读配置与图像，不加载模型，不创建输出目录，不访问开发板设备。

## 构建与独立数值检查

在已有 ARM64 RKNN 2.3.2 验证环境中，仓库入口为 `./build.sh --test`。worktree 的 `.git` 文件指向主仓库时，还需让容器只读访问对应主仓库 Git 元数据路径，否则既有源码身份收集会失败。不要改写 Git 身份或用其他 checkout 冒充当前代码。

也可以在同一 SDK 环境独立构建本目录：

```bash
cmake -S tests/nanotracev3_performance_benchmarks/window_preprocess \
  -B build/window-preprocess -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON
cmake --build build/window-preprocess -j4
ctest --test-dir build/window-preprocess --output-on-failure
```

`baseline_nanotrack_preprocess_tests` 分别检查旧路径逐元素精确一致，以及双精度四邻点插值参考的 `5e-4` 误差门槛。四邻点参考直接给定裁剪原点，不依赖生产几何生成预期坐标。测试覆盖奇偶尺寸、分数中心、四侧/四角、全图外、边长 1、上/下采样、固定随机图、不同图案和非连续行；还覆盖工作区残留、均值变化、114 回退、源帧释放、缓存占用及预处理器销毁。

`nanotrack_window_tests` 使用真实跟踪器与受控模拟模型，检查 1 次初始化、24 次更新、逐帧框/分数/成功状态及模板/搜索/Head 调用次数。`recovery_regression_tests` 使用真实 HybridTracker 和检测线程，控制检测结果可用时刻，检查初始化失败、中间补帧失败后继续及恢复成功。模拟模型测试只能证明流程约束，不能替代生产板模型结果对照。

`nanotrack_window_performance_tests` 将固定统计送入正式诊断使用的 `performanceVerdict`，该函数一次生成判定、报告文字、状态文字和退出码。10 个用例覆盖比例 30%、恢复中位数相等/增加和恢复 P95 增加时失败，以及恰好 20%、P95 相等和正常收益时通过。门槛附近还使用相邻可表示的浮点数，核对严格或非严格比较没有写反。`nanotrack_window_performance_exit_tests` 启动这 10 个实际子进程，核对失败退出码 1、成功退出码 0 及相应报告。测试不执行模型，不制造或改写性能采样。

## 生产板恢复对照

正式测量需 HZ-RK3576_WuKong Board、Ubuntu 22.04.5、内核 `6.1.141-rt36`、Runtime 2.3.2 和驱动 0.9.8。必须使用系统 `/usr/lib/librknnrt.so`，不要部署容器中的库。工具只读取环境，不调整生产服务、调频策略或相机。若实验获准临时停止正式服务，须由外层脚本在正常和异常退出时恢复，并核实 active/running；建议另设板端独立恢复定时器以应对连接断开。实验前在环境说明文件填写具体板卡、正式服务状态和并发进程。读取驱动版本可能需要现有的 debugfs 只读权限。

```bash
source /opt/ros/humble/setup.bash
build/window-preprocess/nanotrack_window_recovery --measure experiment/config.yaml
```

完整 ROS 构建的工具位置为 `.docker-artifacts/build/drone_tracker/tests/nanotracev3_performance_benchmarks/window_preprocess/nanotrack_window_recovery`，不是安装目录。将隔离诊断程序与必要项目动态库提供给实验环境，不切换正式节点版本。

程序先检查输入、模型和系统库身份。正式预热之前，使用真实跟踪轨迹的窗口逐次精确比较新旧输入；这批额外恢复和张量比较位于正式计时外。模型加载、图像解码、帧复制、每批 reset 和写盘位于计时外。每条路径预热至少两批，正式执行至少十对恢复；每对交替先后顺序。CPU 核心与 OpenCV 线程数由配置固定。每批执行初始化一次和按时间戳顺序的 24 次更新；任何更新失败或输出差异都使指定用例失败。

输出目录必须不存在，工具拒绝覆盖已有结果。记录包含：

- `exact-inputs.txt`：计时外实际恢复的 25 个张量逐元素检查、元素总数和各次窗口边长。
- `other-windows.csv`：固定第一张真实图像与中心，1/32/77/154/255/511/1024/2048 边长的模板/搜索预处理原始对照，各预热两次并至少十对测量；不包含模型执行。
- `identity.txt`、`code-snapshot.txt`、配置和清单副本：代码、可执行文件、模型、库、输入与参数身份。
- `frames.csv`：预热/正式标记、对照次序、逐帧成功/初始化状态、框、分数、各阶段与模型调用数；实际模型次数按 RKNN `Run`（调用 NPU 执行的区间）及祖先阶段区分。既有搜索特征直通路径缺少 `SearchBackbone` 外层区间，其中直接属于 `Inference` 的 Run 对应搜索模型；模板与 Head 均由其模型祖先辨识。完整原始区间仍保存在 `intervals.csv`。没有 Run 的模拟测试使用外层模型事件；`batch_ns` 在每行重复，是所属批次总值，统计时每批只取一次。
- `intervals.csv`：原始父子计时区间；`Preprocess` 是整体预处理，`ImagePrepare` 是其图像准备子阶段。两者不能相加当成总耗时。
- `environment-snapshots.txt`：每批前后的 CPU/NPU 频率、温度、调频策略、CPU 计数及负载；`environment-notes.txt` 保存人工记录的服务/并发状态。
- `report.txt`、`status.txt`：正式批次统计及通过/失败结论。预热记录保留，但排除在统计之外。

中位数的偶数样本取中间两项均值，P95 取排序后第 `ceil(0.95×N)` 项。指定用例须同时满足逐帧精确对照和 1/24/24 模型调用数，新预处理中位数不高于旧路径 20%，整体恢复中位数降低且 P95 不增加。性能门槛未满足时退出码为 1；成功为 0。部分运行失败会保留已完成批次及失败原因，不生成通过结论。

其他窗口尺寸的预处理数据与恢复批次分别保存；它们使用相同模板/搜索输出尺寸，不改变指定恢复初始框。指定输入的单次通过不能推广到全部窗口，也不能换算成现场约 388 ms 的优化后耗时。板端验收结果、服务恢复和未运行检查见本变更的 `validation.md`。

# RK3576 视频帧源验证记录

## 结论与范围

2026-09-14 首轮完成本变更所有不需要开发板的实现与验证，任务进度为 22/26；当时按用户要求暂缓第 5 组。随后按继续实施的请求连接 RK3576 补做实机验收，结果见本记录末尾。新增帧源只处理视频，不加载 YOLO 或 NanoTrack，也不执行 NPU 推理；NPU 是开发板的神经网络计算设备。

约定 ARM64 容器构建成功，完整 CTest 为 27/27 通过。地址、内存泄漏与未定义行为检查为 4/4 通过。根据真实编译数据库对本次修改的 10 个 C++ 编译单元执行 `-Wall -Wextra -Werror -fsyntax-only`，全部通过；随后相关视频与装配测试 7/7 通过。`openspec validate implement-rk3576-video-frame-source --strict` 通过，未同步主规格，未归档任何变更。

这些结果是容器中的逻辑与软件解码证据，不是 RK3576 开发板的停止耗时、长期资源稳定性或性能验收。未执行 Jetson、ROS、相机、模型质量或跨平台算法等价性验证。前序完整回放的 7.2、7.3 质量任务仍未完成。

## 代码、输入与环境身份

- 基础提交：`175db856aa715f1bbb834142c1680db86ea03d88`，分支 `rk3576`。本次实现位于工作区，未创建提交。
- 镜像：`roc-rk3576-ros2-build:humble-arm64`，身份 `sha256:422f87e51551f014e12a46c6307c192c87341e739b9ee5d47e302a9984b932a2`。未重建或修改镜像。
- 编译环境：容器 `aarch64`，GCC 11.4.0，C++17；Release 选项为 `-march=armv8-a -O3 -DNDEBUG -std=gnu++17`。
- 软件库：libavformat 58.76.100、libavcodec 58.134.100、libavutil 56.70.100、libjpeg-turbo 2.1.2、OpenCV 4.5.4。JPEG 固定使用 `JDCT_ISLOW`，即整数形式的逆离散余弦变换，用于从压缩系数恢复像素。
- 独立顶层配置沿用本地已有 RKNN 构建 SDK，目标运行基线仍为 Runtime 2.3.2 / 驱动 0.9.8。本次未运行或查询开发板上的 Runtime 和驱动。
- 源文件 SHA-256 清单保存在本地 `.docker-artifacts/log/video-source-sha256.txt`，清单自身指纹为 `fe5842d034f5f4e5a93f03f79a7cf1edd009fbd599a8d43fae780f8b8d7fa0c1`。SHA-256 是用于辨认文件内容的摘要值。
- `video_source_driver` Release 产物指纹为 `5c9287b83e61910b25d019e71aafb85b3ae87735d783d19b147e875a2c865773`。

测试素材由 `src/platform/rk3576/tests/video_fixture_generator.cpp` 生成到构建目录，不提交二进制。生成器使用固定像素公式和 JPEG 质量参数，通过 libavformat 封装 MOV，不依赖额外的 ffmpeg 命令行。固定版本下的部分输入指纹如下；全部 MOV 指纹保存在本地依赖日志。

| 输入 | 内容 | SHA-256 |
| --- | --- | --- |
| `0.mov` | 三帧灰度，641×479 | `215910572896d02ac6888a063503998424e15d2e324b57f0e39dbece7a3c863e` |
| `420.mov` | 三帧 4:2:0，加 PCM 音频包，641×479 | `6ebee54041480e5d7ee71734514ffbbeae73a260c279134f3feade0ffa50975f` |
| `422.mov` | 三帧 4:2:2，641×479 | `cc1ed82c2b413788de689789ee2bda270075ada592711823d584ed3cd8fa8932` |
| `444.mov` | 三帧 4:4:4，641×479 | `cb48ed404ca5db50526c1ae75f0c9a562f39aa1ee408ecacbd5651cf96982922` |
| `slow.mov` | 90 帧，间隔 33333 微秒 | `7d9c2e0961678812126fca978b74da2125449a9e90b61f4d57573aff34ae0a2b` |
| `offset.mov` | 首帧偏移 100000 微秒 | `2abd2b1353d7f717253a53906e898ee2d23aff6efced560b569b5c9824d37704` |

## 可复现构建命令

每次均用 `docker run --rm` 创建临时容器。仓库挂载为 `/work/ros2_ws/src/drone_tracker`，宿主机 `.docker-artifacts/{build,install,log}` 分别挂载为 `/work/ros2_ws/{build,install,log}`，工作目录为 `/work/ros2_ws`。本地完整约定见仓库根目录 `AGENTS.local.md`。

容器内 Release 构建与回归命令：

```sh
cmake -S src/drone_tracker/src/platform/rk3576 -B build/video-source \
  -DRKNN_INCLUDE_DIR=/work/ros2_ws/src/drone_tracker/.docker-artifacts/rknn/include \
  -DRKNN_LIBRARY=/work/ros2_ws/src/drone_tracker/.docker-artifacts/rknn/lib/librknnrt.so \
  -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON
cmake --build build/video-source -j4
ctest --test-dir build/video-source --output-on-failure
```

完整 27 项通过，总耗时 9.53 秒。该耗时只描述这次容器测试，不作为部署性能指标。测试包含既有装配、模型契约、平台选择和 ReplayReader 回归。原回放源码、命令及配置文件未改动，不要求新增配置字段。

内存检测使用独立构建目录：

```sh
cmake -S src/drone_tracker/src/platform/rk3576 -B build/video-source-asan \
  -DRKNN_INCLUDE_DIR=/work/ros2_ws/src/drone_tracker/.docker-artifacts/rknn/include \
  -DRKNN_LIBRARY=/work/ros2_ws/src/drone_tracker/.docker-artifacts/rknn/lib/librknnrt.so \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build/video-source-asan --target video_fixture_generator \
  mjpeg_video_reader_tests video_timing_tests video_frame_source_tests -j4
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build/video-source-asan \
  -R 'video_generate_fixtures|mjpeg_video_reader_tests|video_timing_tests|video_frame_source_tests' \
  --output-on-failure
```

4 项通过，总耗时 3.01 秒。AddressSanitizer 检查越界、释放后访问和内存泄漏，UndefinedBehaviorSanitizer 检查运行中的未定义行为。这些检查不能替代实机长时间资源曲线，也不等于线程竞态检测器验证。

## 测试与规格对照

| 工件要求 | 实现与检查 | 结果 |
| --- | --- | --- |
| proposal：增加持续视频输入，保留顺序回放 | 独立 `rk3576_video` 库、工厂 Video 分支；原 ReplayReader 源码未变，原回归通过 | 通过 |
| design 1–2 / 视频规格：本地 MJPEG、首帧、原尺寸亮度 | `mjpeg_video_reader.*`；四种采样各三帧逐像素对照，含 641×479 边缘、音频包、跨实例读取 | 通过 |
| 视频规格：亮度参考独立于生产路径 | 生产使用 raw MCU 路径；参考使用 JPEG 灰度 scanline 路径。MCU 是 JPEG 按块解码的最小单元，scanline 指按图像行读取。两者使用相同整数 IDCT，所有有效像素一致 | 通过；不是 Jetson 实机等价证明 |
| design 3 / 视频规格：严格时间、保留偏移 | `video_timing.*`；大小写与歧义 Artist、非法十进制、非整数时间基、缺失/负值/重复 PTS、int64 及截止时间边界 | 通过 |
| design 3 / 视频规格：系统校时独立、落后追赶、停止等待 | 注入系统时钟跳变，验证帧时间及单调截止时刻；模拟落后不增加等待；真实条件变量等待 10 秒时发出停止 | 通过；容器断言停止后 1 秒内结束，未测板上耗时 |
| design 4–5 / 视频规格：队列、停止、重启 | 一条构造参数 30 的 SPSC 队列，非扩容入队；SPSC 表示单生产者和单消费者。测试满队列仅丢新帧、尾帧读取、超时清空、停止与单消费者并发、历史/实时重启、尺寸变更后重试 | 通过 |
| design 5–6 / 视频规格：失败、析构与所有权 | 线程创建失败、读取错误、重复时间失败、自然 EOF 后析构；持有帧/视图跨源销毁，复制像素不随读取缓冲区变化 | 通过；诊断异常不逃逸线程 |
| design 6 / 视频规格：错误不冒充 EOF | 真实坏 JPEG 与后续尺寸变化分别报告；注入读取错误；非法历史基准创建失败，实时模式忽略该元数据 | 通过 |
| design 7 / platform-factory delta：真实视频、相机拒绝、构建隔离 | `tracker_assembly_test.cpp` 验证真实文件、实际尺寸、相机配置忽略、失败路径及模型装配回归；根目录平台选择拒绝测试通过 | 通过 |
| design 7 / 视频规格：不加载模型的集成驱动 | `video_source_driver` 使用真实工厂、公共帧源和 `waitForLatestFrame()`，输出轮次、帧时间、像素摘要及统计 | 通过 |
| design 8：旧变更的规格协调 | 本记录下节明确处理顺序，未修改主规格及旧变更验收状态 | 通过 |
| 实机步骤与资源验收 | 首轮暂缓；续做使用真实 RK3576 与固定生成素材 | 见末尾实机续做记录 |

真实文件驱动的容器观察：历史模式正常播放两轮，每轮 `read=3 enqueued=3 dropped=0 consumed=3`；实时模式 60 ms 慢消费一轮，`read=90 enqueued=90 dropped=0 consumed=48`。`consumed` 是消费者实际收到的帧数，差值来自消费者取最新帧时排空积压；该轮没有队列满丢弃。队列满丢新帧另由暂停消费者的 100 帧受控测试验证，断言入队数加丢弃数为 100，保留的旧帧按原顺序可读。

`ldd` 核对 `video_source_driver` 与 `mjpeg_video_reader_tests`，未链接 CUDA、TensorRT、VPI、Tegra 或 RKNN Runtime。公共算法目标的链接声明未增加视频/JPEG/FFmpeg/RKNN 依赖。这里的软件视频目标不要求加载模型；独立顶层仍要求 ARM64 与已有 RKNN 构建 SDK，根目录 RK3576 拒绝规则不变。

## 检查缺口与历史失败

容器内没有 clang-tidy 或 clangd，也没有仓库配置的专用 C++ lint 命令。宿主机虽有这两个工具，但不能直接使用容器编译数据库中的 ARM64 系统头文件路径。因此记录该静态检查缺口，没有把宿主机分析冒充目标环境检查。已在同一容器读取 `compile_commands.json`，沿用每个单元的参数并执行 GCC 严格警告检查，10/10 通过。调度脚本使用容器 Python，因为该容器未提供 conda 环境。

首次构建发现测试缺少 `<thread>`，已补齐。第一次测试发现参考图像与注入图像的三参数花括号初始化被 OpenCV 解析为数值列表，导致错误图像尺寸及越界；已改为带 `cv::Scalar` 的明确图像构造。随后完整测试、额外边界测试与内存检测均通过。没有保留已知实现偏差。

首轮暂缓的 RK3576 第 5 组包括固定视频的实际 PTS 与慢消费、至少 20 轮启停、100 次新建销毁和至少 10 分钟资源负载。续做情况见下节。历史时间跨轮重复是已批准契约，不属于偏差。阻塞的本地文件系统调用不能保证被强制中断，符合设计已声明的边界。

## 规格同步顺序

`assemble-rk3576-tracker-replay` 尚未归档，其 delta 含有先前拒绝视频的限制。本次保留该变更及原验收记录不动。

后续同步时，先保留旧变更非冲突的模型装配和顺序回放条款，再应用本变更的视频支持。冲突条款应统一为：相机仍拒绝；视频按 `rk3576-video-frame-source` 提供；根目录仍拒绝 RK3576 生产构建。若旧变更晚于本变更归档，必须先协调这项冲突，不能把拒绝视频的旧条款机械覆盖回来。本次没有自动同步或归档，也没有勾选旧变更的质量验收。

## 本地日志

以下文件均为 `.docker-artifacts` 下的本地证据，不提交 SDK、模型或构建产物：

- `log/video-build-sanitizer.log`：最终 Release 27 项和内存检测 4 项结果。
- `build/video-source/Testing/Temporary/LastTest.log`：最近一轮视频与装配 CTest 的详情及正常、慢消费输出。
- `build/video-source-asan/Testing/Temporary/LastTest.log`：内存检测测试详情。
- `log/video-dependencies.log`：库版本、动态链接项、输入和产物指纹。
- `log/video-source-sha256.txt`：本次 C++ 实现、测试与 CMake 文件的内容指纹。
- `log/video-warning-check.log`：10 个修改单元的编译数据库警告检查。
- `log/video-after-warning-tests.log`：严格警告检查后的 7 项相关测试结果。

## RK3576 实机续做：环境与命令

2026-09-14 在 `firefly` 开发板执行。内核为 Linux 6.1.141，架构为 aarch64。板上 libavformat 58.76.100、libavcodec 58.134.100、libavutil 56.70.100、libjpeg-turbo 2.1.2、OpenCV 4.5.4，与本次容器一致。视频程序的 `ldd` 未包含 RKNN 或 Jetson 库。本次没有加载模型，也没有查询或修改 RKNN Runtime 与驱动；软件视频测试不依赖它们。

新增 `video_board_validation` 只用于验收，通过真实工厂创建视频源，不改变生产接口。它记录停止耗时，并从 Linux `/proc/self/status`、`/proc/self/fd` 采样当前进程的内存、线程数和文件描述符数。文件描述符是进程打开文件等资源的句柄。采样自身会打开状态文件及目录，所以句柄数包含固定的采样开销。

新增驱动仍在约定临时容器中构建，未重建镜像。严格警告检查扩展为 11 个编译单元，全部通过；随后完整 CTest 为 27/27，通过用时 9.37 秒。clang-tidy/clangd 的环境缺口与前述记录相同。

```sh
# 在前述挂载配置的临时容器中执行
cmake -S src/drone_tracker/src/platform/rk3576 -B build/video-source
cmake --build build/video-source --target video_board_validation -j4
python3 src/drone_tracker/.docker-artifacts/video_warning_check.py
ctest --test-dir build/video-source --output-on-failure

# 复制产物与 video-fixtures 后，在 RK3576 的独立测试目录执行
cd /home/firefly/drone_tracker_test/video-source-20260914
./mjpeg_video_reader_tests video-fixtures
./video_timing_tests video-fixtures
./video_frame_source_tests video-fixtures
./video_source_driver video-fixtures/420.mov 1 0 2
./video_source_driver video-fixtures/offset.mov 1 0 1
./video_source_driver video-fixtures/slow.mov 1 60 1
./video_source_driver video-fixtures/slow.mov 0 60 1
./video_board_validation video-fixtures 600 > board-validation.log 2>&1
ffprobe -v error -select_streams v:0 \
  -show_entries stream=time_base:packet=pts,pts_time -of csv video-fixtures/slow.mov
```

上述 `video_source_driver` 参数依次为输入、历史模式开关、消费延时毫秒和轮数。`video_board_validation` 的 600 是持续负载的最短秒数，不包含之前的功能检查及 100 次新建销毁。负载在同一进程、同一帧源内交替执行 60 ms 慢消费到正常结尾，以及消费 10 帧后主动停止再从头启动。

基础提交仍为 `175db856aa715f1bbb834142c1680db86ea03d88`，源码保留在未提交工作区。续做源码清单为本地 `video-board-20260914/source-sha256.txt`，其 SHA-256 为 `d35ae290bdde6c0de942000d74616f0ada3a8e837a3c9abae942863f2b977381`。新增驱动产物 SHA-256 为 `f5223c6ed964295832199d0d2d2caad760ba4a76522ad678d6c06baea62ff768`。原驱动及固定 MOV 素材的指纹与上文一致。

## 实机续做：时间、队列与失败

三个板上测试程序全部通过，包括四种 JPEG 采样的逐像素对照、时间边界、可控错误与并发/所有权检查。FFmpeg 对故意伪造 PNG 编码的拒绝测试输出格式警告，测试按预期通过，该警告不是正常素材失败。

`ffprobe` 记录 `slow.mov` 的 time_base 为 1/1000000，表示 PTS 每单位为一微秒。90 帧的实际 PTS 从 0 起，每帧增加 33333，末帧为 2966637。`offset.mov` 两帧分别为 100000 和 133333。

| 场景 | 实机结果 |
| --- | --- |
| 历史模式正常播放两轮 | 每轮读取、入队、消费均为 3；首帧时间 1700000000000000，末帧 1700000000066666，两轮像素摘要相同 |
| 首帧非零偏移 | 两帧时间 1700000000100000、1700000000133333，保留 100000 微秒偏移 |
| 历史模式 60 ms 慢消费 | 读取/入队 90，消费 44，队列满丢弃 0；首末帧时间为 1700000000000000、1700000002966637 |
| 实时模式 60 ms 慢消费 | 读取/入队 90，消费 46，队列满丢弃 0；首末帧时间为 1789375092445415、1789375095412052 |
| 暂停消费直到 EOF | 读取 90，入队 31，丢弃新帧 59；EOF 后取回全部 31 帧，时间依次对应原始前 31 帧 |
| 历史与实时重启 | 各在同一帧源执行 20 轮；交替中途停止与 EOF，故意保留尾帧后重启，每轮仍取得正确首帧 |
| 10 秒帧间等待中停止 | 取得首帧后等待 100 ms 再停止，停止调用耗时 1.00869 ms |
| 坏 JPEG 后重试 | 连续两轮均记录 Failed，原因 `JPEG decode: Premature end of JPEG file`，未冒充 EOF |
| 非法 Artist | 工厂创建失败，原因 `Artist must be positive decimal int64 microseconds`，包含平台及路径 |

慢消费的已交付时间严格递增，时间差均是 33333 微秒的整数倍。消费数量减少来自取最新帧时跳过积压帧，独立的满队列试验才发生生产者丢新帧。两种慢消费都取回了末帧。这里的 31 是本次队列实现的实际可用槽位，不把构造参数 30 解释为严格容量。

实机日志保存在板上上述目录，并取回到本地 `.docker-artifacts/video-board-20260914/`。`identity.log` 保存库、动态链接与产物/输入摘要，`unit-tests.log` 保存测试结果，`playback.log` 和 `playback-summary.json` 保存逐帧输出及核对结果，`pts-slow.csv` 与 `pts-offset.csv` 保存独立时间读取结果。容器续做检查见 `.docker-artifacts/log/video-board-warning-check.log` 和 `video-board-ctest.log`。

## 实机续做：资源结果与最终状态

同一进程先完成 100 次新帧源创建/启动/停止/销毁，再在同一个帧源上完成 318 轮交替负载，持续 **600.187 秒**。该时长不包含前面的创建销毁检查。程序输出 `BOARD_VALIDATION_PASS`，退出码为 0；退出后 `ps -C video_board_validation` 无进程。

前 100 次检查持有第一轮帧及只读视图跨越源和工厂销毁，持续核对像素。随后先释放帧，再确认视图仍可读，最后释放视图并采样基线。没有把测试自身保留的图像内存误判为泄漏。

RSS 是当前驻留物理内存，VmSize 是进程虚拟地址空间大小。以下数值来自进程自身采样；内存单位沿用 Linux 输出的 kB（每单位 1024 字节）。

| 阶段 | 样本数 | RSS 范围（kB） | 线程数 | 文件句柄数 |
| --- | ---: | ---: | ---: | ---: |
| 负载前初始值 | 1 | 31104 | 1 | 5 |
| 每次创建销毁后 | 100 | 31204–31672 | 1 | 5 |
| 释放持有帧和视图后 | 1 | 31504 | 1 | 5 |
| 负载运行中 | 318 | 31644–32132 | 2 | 6 |
| 每轮停止后 | 318 | 31208–32280 | 1 | 5 |
| 最终销毁后 | 1 | 31208 | 1 | 5 |

所有样本的 VmSize 均为 223384 kB。以资源阶段起点划分的五个两分钟窗口中，停止后 RSS 中位数依次为 31576、31658、31576、31576、31508 kB，均在固定范围内波动，没有持续上升。最终 RSS 比初始值高 104 kB，且虚拟空间、线程和句柄均保持基线；未观察到随轮数累积的资源增长。这是本次输入和时长下的资源证据，不宣称能排除所有泄漏。

所有记录的停止调用最大耗时为 **33.1238 ms**，小于规格的一秒限制。长 PTS 等待的专门检查为 **1.00869 ms**。生产运行时只有主线程与一个生产线程；每轮停止后均恢复一个主线程。主动停止的尾队列会保留到下一轮启动，因此停止阶段的 RSS 不要求每轮精确相等；最终销毁值已单独记录。

完整原始曲线在本地 `video-board-20260914/board-validation.log`，SHA-256 为 `95bb396bbba68ae39de45116ebc9d8c3ecc789b2271c1358039708fae0bda4c9`，与板上日志一致。提取后的 739 个资源样本保存在 `resource-samples.json`，SHA-256 为 `72f14a9bd137009b75bb533ca9d1efe22c16d9891f2d98680cf125c16002e564`；分阶段汇总为 `resource-summary.json`。上述本地路径均以 `.docker-artifacts/` 为根。

本变更最终进度为 **26/26**。续做只增加验收驱动及其构建目标、更新证据与说明，没有修改生产算法、模型或 ROS 接口。proposal、design 和两份 delta 的验收范围未收缩。严格 OpenSpec 校验通过，未同步或归档。本次仍未执行 Jetson、ROS、真实相机、检测质量或完整跨平台算法等价性验收，前序回放的两项模型质量任务保持未完成。

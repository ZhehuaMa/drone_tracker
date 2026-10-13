# 独立诊断进程：离线实施与交接记录

本记录描述 `add-rk3576-tracking-diagnostic-process` 的第一阶段交付。新增诊断代码、部署文件与无需实机运行的检查已经完成。当前任务进度为 **34/40**。尚未连接、部署或运行生产板，不能把本记录当作生产板验收报告。

## 环境与最终检查

在本仓库现有 ARM64 Linux 构建镜像 `drone-tracker-rk3576:humble-arm64-rknn2.3.2` 执行 `./build.sh --test`。镜像由既有 `colima ros2-arm64` 环境运行，没有重建或修改镜像。构建配置为 C++17、Release、`DRONE_TRACKER_PLATFORM=rk3576`、`DRONE_TRACKER_BUILD_TESTS=ON`、`DRONE_TRACKER_BUILD_OSD=ON`、`DRONE_TRACKER_BUILD_RECORDING=ON`。

最终编译成功，CTest 为 **47/47 通过，31.84 秒**。CTest 是 CMake 的自动测试运行器。原始结果见 `offline-build-console.txt` 和 `offline-test-console.txt`。此次没有使用真实 RKNN 模型或相机，输入为受控数值张量及合成视频。容器中的 RKNN Runtime 为既定 2.3.2。容器没有生产板的 RKNPU 驱动和外设链路，因此不能证明驱动 0.9.8 下的实机运行结果。

`git diff --check` 通过。`openspec validate add-rk3576-tracking-diagnostic-process --strict` 通过。新增 C++ 文件按项目规范排版。仓库没有配置可运行的 clang-tidy/clangd 检查，构建镜像也未提供它们；静态分析仍是明确缺口。构建出现虚拟机共享文件时间轻微超前的 `Clock skew` 警告，最终编译和测试均完成。

一次中间全套运行在既有 `osd_presenter_tests` 出现 `unchanged video frame produced diagnostic log`。随后在未修改的 HEAD 源码上建立独立 Release 构建，30 轮短复验的第 24 轮复现同一断言。见 `osd-head-release-failure.txt` 与 `osd-head-release-results.txt`。这证明失败并非本次诊断代码独有；具体调度根因尚未修复。OSD 实现和断言均未修改。最终全套复验通过。

## 交付文件与检查覆盖

- `src/diagnostics/` 包含固定 256 字节事件、共享内存协议、业务提交器、独立收集器、二进制存储和只读检查器。业务端不创建日志线程，关键槽占容量三分之一，共享区小于 8 MiB。
- `src/diagnostics/tests/diagnostic_test.cpp` 覆盖协议、跨进程可见性、环回、普通/关键溢出、通知遗漏、未提交半条事件、短写、写入/双阶段同步/目录失败、存储阻塞、空间保护、全部小样本截断位置、CRC 损坏、未知版本、重复、缺口、缺提交、只读校验和同区域唯一消费者重连。
- `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp` 使用真实公共算法和受控模型返回值。它验证来源淘汰、单次初始化失败、第五次低 IoU 清框、结果覆盖和 24 步补跟踪中 2 步失败仍恢复成功。IoU 是框交集面积除以并集面积。真实事件经过存储，再由检查器核对中文报告。检测就绪使用仅测试编译的屏障，不依靠固定睡眠猜测完成。
- 三模式对照验证完整框、状态、分数、预测消息时间和模型调用次数。逐像素哈希与帧引用计数保持一致。`src/tests/ros_video_test.cpp` 额外验证真实 ROS 节点的重复时间、未消费、发布与 EOF 末帧边界。共用发布观察函数覆盖正常返回和异常传播；长调用使用受控延迟验证单调耗时。
- `deployment/tests/test_diagnostics.py` 覆盖配置、包内程序、独立服务关系、预检无服务副作用、旧回退边界和外部截止。它没有运行 systemd 实例，不能勾选完整任务 6.3。

新增程序已安装到 `.docker-artifacts/install/drone_tracker/lib/drone_tracker/`：`rk3576_diagnostic_worker`、`rk3576_diagnostic_inspect`。三份程序（包括主节点）的 SHA-256、大小及绝对路径记录在 `offline-artifacts.json`。SHA-256 是核对文件内容的指纹。源码文件指纹见 `offline-source-files.sha256`。

新增生产文件为 `deployment/drone-tracker-diagnostics.service`、`run-diagnostics`、`diagnostic-launch.py`、`diagnostics.yaml` 和 `rollback.sh`。打包器纳入它们及两个程序，核对主业务、收集器和检查器的协议版本。部署预检只读，回退恢复匹配入口及服务并保留历史诊断原件。此阶段没有打生产包、切换服务或执行回退。

## 第二阶段的未完成项与可运行入口

仅以下任务保留未勾选：

| 任务 | 必须取得的证据 |
| --- | --- |
| 6.3 | 隔离 systemd 实例的日志失败、业务异常退出、各服务重启、有限排空、唯一消费者和旧版本回退。离线边界检查已通过，实机部分未执行。 |
| 7.3 | 同一 HZ-RK3576_WuKong Board 的三组关闭/基础成对对照，P99 提交 ≤20 μs，调用 P99 增量 ≤0.2 ms，帧率下降 ≤1%，健康记录无缺口。P99 表示 99% 样本不超过的值。 |
| 7.4 | 同一板卡的细分模式定向对照，单列开销、失败和慢样本。 |
| 7.5 | 一轮 ≤150 秒的真实模型、跟踪、检测、录像、bag、HDMI 和日志并行运行，有限 ON/OFF、资源检查及健康无缺口。 |
| 7.6 | 隔离实例分别终止业务/日志，注入满缓冲、写入和同步失败，证明健康业务不等待且同步位置准确。 |
| 7.7 | 根据真实结果写正式验收报告，保存具体板卡、程序/配置/模型/输入指纹、Runtime 2.3.2、驱动 0.9.8、窗口和全部样本。 |

第二阶段先按 `AGENTS.local.md` 使用 `ssh firefly-production` 读取现场，并在每个非交互命令加载 `/opt/ros/humble/setup.bash`。采集前复核 `v4l2-ctl --list-devices` 和 `media-ctl -p`。正式实例不得用于故障终止实验。生产库 `/usr/lib/librknnrt.so` 必须保持既定指纹，不能被容器库覆盖。

现有入口：

```bash
# 本机重新取得完整自动检查结果
./build.sh --test

# 检查相同格式版本，不加载模型或连接服务
.docker-artifacts/install/drone_tracker/lib/drone_tracker/rk3576_diagnostic_worker --protocol-version
.docker-artifacts/install/drone_tracker/lib/drone_tracker/rk3576_diagnostic_inspect --protocol-version
.docker-artifacts/install/drone_tracker/lib/drone_tracker/drone_tracker_node --diagnostic-protocol-version
```

上面的程序为 ARM64 ELF，须在匹配 Linux 环境执行，不能直接在 macOS 运行。

外部截止入口为 `src/diagnostics/tests/bounded_run.py`。第二阶段创建隔离实例与采样脚本后，用该入口包住整个准备、预热、测量和收尾流程。下面展示预算用法，末尾参数应替换为实际隔离测试脚本：

```bash
python3 src/diagnostics/tests/bounded_run.py --camera --seconds 150 \
  --prepare-seconds 10 --warmup-seconds 10 --measurement-seconds 90 \
  --cleanup-seconds 40 --output /tmp/新的测试窗口记录.json 隔离测试脚本
```

总截止由外部控制器执行。被测脚本仍需按各阶段预算采样、保留慢样本及检查资源；本阶段未生成真实模型开销数据，第二阶段需完成对应控制和采样脚本。故障实例使用 `--fault --seconds 120`。达到截止、样本不足或资源未回收即失败，不延长测试。相机相关最多 180 秒，其他性能或稳定性运行最多 600 秒。

检查原件时使用 `rk3576_diagnostic_inspect 原日志或目录 --output 新目录`。输出中文时间线、JSONL（每行一条 JSON 记录）、CSV（按列组织的文本表）和身份清单。已有目录、原日志目录内导出和原件覆盖均被拒绝。缺少结束事件只能报告结果未知。

本阶段没有实机性能、相机、长期稳定性或真实断电结论。没有执行提交、归档，也没有修改另一个 `optimize-rk3576-nanotrack-window-preprocessing` 变更。

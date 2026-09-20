# support-rk3576-yolo-fp16-input 核验报告

核验日期：2026-09-20。对象：`rk3576` 当前工作树及本变更的 proposal、design、tasks、增量规格和验证证据。此次仅核验并新增本报告，未修改实现、任务勾选或执行归档。

## 结论

无 CRITICAL（归档前必须修复）问题，无 WARNING（应修复）问题，有 1 项 SUGGESTION（非阻塞改进建议）。实现符合本变更范围，可以归档；建议先清理验证记录中的过时表述。

| 维度 | 结果 |
|---|---|
| 完整性 | 15/15 任务完成，4/4 需求找到实现或验证证据 |
| 正确性 | 15 个场景有代码、测试或实机记录支持；其中直通参考场景为未选用的条件分支 |
| 一致性 | 遵循四项设计决策；1 处历史文档表述需澄清 |

## 需求与场景映射

下列源码路径均相对仓库根目录。

| 需求 | 实现及覆盖证据 |
|---|---|
| 兼容模型可以替换而不绑定文件身份 | `src/platform/rk3576/rknn_yolo_contract.cpp:12` 分离 INT8 与 FP16 检查，共用形状和输出结构检查；没有文件名或哈希白名单。`rknn_runtime_test.cpp:195` 覆盖 C=1/4/6/64；`src/hybrid_tracker/tests/yolo_output_adapter_test.cpp:25` 检查 8400 候选和按索引配对。运行时测试覆盖 FP16 NONE/AFFINE、原值像素、字节数、非法 INT8 参数、Float32/UINT8 拒绝及不兼容结构。实机 FP16 查询和气球检测覆盖 C=1、类别 0。 |
| 同步执行与资源生命周期可验证 | `src/platform/rk3576/rknn_session.cpp` 保持同步执行、失败等待及释放；`rknn_runtime_test.cpp` 覆盖保留输出后销毁、运行中停止、执行失败不返回部分输出及后续未就绪。`tracker_assembly_test.cpp` 覆盖两类 YOLO 输入的装配失败清理和类别越界；合法 FP16 组合启动检测线程并正常销毁。开发板 2 日志确认 Runtime 2.3.2、驱动 0.9.8 和两轮停止。 |
| 诊断区分存储属性与模型语义 | `src/platform/rk3576/rknn_runtime.cpp:33` 报告模型属性、UINT8 NHWC 提交及 pass_through=0，明确 mean/std 未由属性查询验证。契约失败诊断列出两个支持分支。`src/platform/README.md:19` 说明转换来源和数值验证要求。未加入私有模型解析或归一化猜测。 |
| FP16 接入具有独立数值与集成证据 | `validation.md` 及 `evidence/` 分别记录模拟测试、固定图像六输出 ONNX 对照、气球检测、原 INT8 新旧版本比较及真实相机集成。同一 RKNN 的 FP16 直通参考属于备选方案，本次采用同源 ONNX，因此该条件场景不适用，不能视为漏测。 |

设计一致性检查确认：新增支持仅位于 YOLO 专属契约；程序仍提交 UINT8 并请求 Float32 输出；mean=0/std=255 由模型来源和独立数值证据确认；测试分为无设备、数值及集成三层。没有改动公共状态机、ROS 接口、阈值或资源所有权。相机装配测试的既有断言修正已在验证记录中说明。

## 此次实际执行

- `openspec validate support-rk3576-yolo-fp16-input --strict`：通过。
- `git diff --check`：通过。
- `./build.sh --test`：ARM64 Release 增量构建通过，ROS/公共 CTest 12/12 通过。日志：`.docker-artifacts/log/build-console.log`、`.docker-artifacts/log/test-console.log`。
- 在既有 `roc-rk3576-ros2-build:humble-arm64` 容器中，对 `.docker-artifacts/yolo-fp16-build` 执行 `cmake --build ... -j4` 和 `ctest --test-dir ... --output-on-failure`：独立构建通过，33/33 测试通过。日志：`.docker-artifacts/log/yolo-fp16-verify-20260920.log`。

容器以 linux/arm64、关闭网络方式运行，使用现有 RKNN SDK 2.3.0。测试输入为合成数据及 SDK 替身；SDK 替身模拟设备接口，不执行真实 NPU 推理。构建结果不能代替目标板验证。

## 复核的既有实机证据

本次读取已有文件，没有重新运行实机模型、相机或 ONNX 对照。

- `evidence/numerical.json` 与 `compare_onnx.py` 一致：同一实际提交输入转换为 Float32 NCHW 并除以 255；六输出均满足逐元素绝对误差不超过 `0.5 + 0.02 * abs(reference)`、各输出平均绝对误差不超过 0.1。最大绝对误差最高为 0.46470642。记录包含来源、输入哈希、相对误差定义及容差依据。
- `evidence/int8-comparison.json`：同一 INT8 模型及 scene.png 的新旧输入、六个原始输出和解码预测逐字节一致，类别 2 检测结果一致。
- `evidence/balloon-detect.log`：固定可见气球图像，类别 0、分数 0.793586。
- `evidence/balloon-present/camera-balloon-results.json` 与节点日志：两轮均观察到 0 → 1 → 2，有效框分别为 196、96，控制恢复成功，进程退出码 0。原六个 ROS 话题和消息类型有记录。

开发板 2 使用 FP16 气球 YOLO 和既有三个 NanoTrack 文件，版本为 Runtime 2.3.2、RKNPU 0.9.8。模型和固定输入指纹见 `validation.md`。相机目标为纸上气球图案；模拟内参和短时场景的证据范围不变。

## 问题与建议

CRITICAL：无。

WARNING：无。

SUGGESTION：`validation.md:33` 仍写“相机目标验收仍缺证据”，与开头结论及后文最终补测结果矛盾。建议改为“首次检查时相机目标验收仍缺证据，后续补测已完成，见最终补测章节”。已有原始记录足以支持任务 4.5 完成，因此该问题不阻塞归档。

## 未执行及证据边界

未重新运行实机验证；本次实机结论仅为既有证据复核。未执行 clang-tidy/clangd，现有镜像不提供这些工具。当前无 Jetson 环境，未执行 Jetson 构建或实机等价性验证。长时质量、正式标定、NPU 设备内存和其他变更的回放质量验收不属于本次通过结论。三项核验维度均已执行，没有因缺少规划工件而跳过检查。

# 6.2 固定浮点输入逐层数值对照

日期：2026-09-11。任务 6.2 完成，四种精度组合均通过。整个变更为 29/29，尚未归档。

## 方法与独立性

[board_numeric.cpp](board_numeric.cpp) 链接真实 `RknnNanoTrackBackend` 和现有 ARM64 静态库。它调用实际预处理器生成模板、搜索裁剪，再执行真实 `setTemplate()` 和 `search()`。SDK 包装只记录输入和成功获取的浮点输出，不替换推理结果，不改变量化参数。最终公共分类、位置张量另行保存。

[compare_numeric.py](compare_numeric.py) 使用板上 RKNNLite 2.3.2、NumPy 1.26.4 和 Python 3.10 独立加载同一组三个 RKNN 文件。参考实现仅读取固定裁剪输入，通过 NumPy 独立转换排列，并直接调用 RKNNLite。它不调用仓库 C++ 会话、后端或特征重排函数，也不读取后端特征作为参考 Head 输入。

两个执行路径均使用真实 RK3576 NPU 和 Runtime 2.3.2，因此这是应用适配层的独立对照，不是验证 RKNN 内核自身正确性的第二种硬件实现。没有运行同源 ONNX 或 Jetson，本项不扩展为模型转换前后精度完全等价。

## 平台、模型和输入

- 开发板：RK3576，Linux 6.1.141，Runtime `2.3.2 (429f97ae6b@2025-04-09T09:09:27)`，驱动 `0.9.8`。
- 构建：原有 Runtime 2.3.0 SDK、约定 ARM64 临时容器，保留 `--rm`，未更新镜像。链接已经修复 FP16 检查的实际后端库。
- Python：板上未配置 conda，使用已有 `/home/firefly/venvs/rknn-toolkit2-2.3.2/bin/python`。
- 模型：沿用 [FP16 修复记录](fp16-fix.md) 的四组合。FP16 Backbone + INT8 Head 使用专属混合精度 Head，其余路径和哈希均逐次保存在 comparison.json。
- 图像：`/home/firefly/drone_tracker_test/yolo_acceptance_20260910/video-frame-1.png`，640×512，SHA-256 为 `bd3783fca65e01383666dcbf44c466f3f4bd584364bac3aa83f8e5ebecdafa93`，按灰度读取。

模板裁剪中心为 `(472.5,285.0)`、原边长 43、输出 127；搜索裁剪中心为 `(470.5,286.0)`、原边长 87、输出 255。二者使用实际公共几何和 OpenCV 浮点缩放，复制三通道，保持 0～255，不除以 255。四组合输入完全相同，Float32 NCHW 文件身份为：

| 输入 | SHA-256 | 非整数元素数（含三通道） |
| --- | --- | --- |
| template.f32 | `01292604f39bdad7b6cc017eb693ad1af6699ce993daa5f93702c8921dea9b3b` | 22968 |
| search.f32 | `0319c43a6caf4ee9740c2c9d5f27ea72f4b1d7f032d4776f21545c3a67becba4` | 104979 |

验收程序 SHA-256：`board_numeric` 为 `ca84f7addd040c1e7f6e6ae37d89aeffebfcc0fb1fec89eb372c17200251dc0d`；`compare_numeric.py` 为 `5208f66ab42a9d5c81d20d5c89b3425799c7ef1f4b0f2f7342dfeabc81d5407b`。

## 重复误差、容差和结果

每组后端和参考各执行 5 次完整三模型调用。参考先测 5 次的模板特征、搜索特征、分类和位置重复误差，最大绝对差均为 0。预先规定参考重复误差不能超过 `1e-5`，超过则停止，不按后端差异放宽。

参考将 `atol=1e-5, rtol=1e-5` 及重复误差写入每组 `tolerance.json` 后，才读取后端保存的输入和输出进行比较。容差公式为 `|实际−参考| <= atol + rtol × |参考|`，atol 是接近零时的绝对容差，rtol 是随数值幅度变化的相对容差。实际比较结果无需使用这部分余量：全部差值均为 0。

每次比较 10 项：模板图像提交、搜索图像提交、模板特征、搜索特征、Head 模板输入、Head 搜索输入、SDK 分类输出、SDK 位置输出、后端最终分类及后端最终位置。每组 5 次共 50 项，总计 200 项。

| Backbone / Head | 参考重复最大差 | 后端重复最大差 | 50 项对照最大差 | 非有限值 / 超差元素 |
| --- | --- | --- | --- | --- |
| FP16 / FP16 | 0 | 0 | 0 | 0 / 0 |
| INT8 / INT8 | 0 | 0 | 0 | 0 / 0 |
| FP16 / INT8 | 0 | 0 | 0 | 0 / 0 |
| INT8 / FP16 | 0 | 0 | 0 | 0 / 0 |

图像实际提交与原始浮点裁剪的独立 NHWC 转换一致，说明此次输入没有被量化回 uint8 或额外归一化。Head 输入与参考 Backbone 的浮点输出逐元素一致，即使两端 INT8 比例、零点不同，也没有把原始整数直接传递。SDK 浮点结果与后端最终结果一致，未再次应用 scale/zp。以上结论限于这组真实固定输入及所记录模型；边缘裁剪、其他模型和失败路径另由既有契约测试覆盖。

## 复现及证据

在约定临时容器的 `nanotrack-contracts` 构建目录执行（保留仓库和 build/install/log 挂载）：

```sh
repo=/work/ros2_ws/src/drone_tracker
c++ -std=c++17 -O2 -I"$repo/src" -I"$repo/src/hybrid_tracker" \
  -iquote "$repo/src/platform/rk3576" -I"$repo/.docker-artifacts/rknn/include" -I/usr/include/opencv4 \
  "$repo/openspec/changes/implement-rk3576-nanotrack-backend/board_numeric.cpp" -o board_numeric \
  librk3576_yolo_sdk.a librk3576_yolo_runtime.a librk3576_yolo_host.a \
  common/libhybrid_tracker.a common/libmodel_common.a common/libpreprocess_geometry.a \
  "$repo/.docker-artifacts/rknn/lib/librknnrt.so" -lopencv_video -lopencv_dnn \
  -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -pthread
```

将程序和 Python 脚本放入板上 `/home/firefly/drone_tracker_test/nanotrack_numeric_20260911`。按 fp16-fix.md 的模型路径表对四组合逐一执行，`OUT` 为组合名子目录：

```sh
timeout 120 ./board_numeric "$TEMPLATE" "$SEARCH" "$HEAD" \
  /home/firefly/drone_tracker_test/yolo_acceptance_20260910/video-frame-1.png "$OUT" > "$OUT/backend.log" 2>&1
/home/firefly/venvs/rknn-toolkit2-2.3.2/bin/python compare_numeric.py \
  "$OUT" "$TEMPLATE" "$SEARCH" "$HEAD" > "$OUT/reference.log" 2>&1
```

所有后端和参考进程退出 0。RKNNLite 查询静态模型动态范围时打印告警，但推理返回完整有限结果；没有因此放宽比较或修改模型。

小型报告在 [evidence/numeric](evidence/numeric/)：每组含 `backend.log`、`reference.log`、`tolerance.json` 和逐项 `comparison.json`。固定裁剪及各轮原始张量留在板上同名目录，未将模型或大张量提交源码。

新增 C++ 文件使用专用编译数据库执行 clang-tidy `clang-analyzer-*` 无诊断、clangd 为 0 errors；随后 ARM64 编译及上述实机检查通过。本次未修改生产 C++，未重复运行无关测试。静态日志保存在 `.docker-artifacts/nanotrack/numeric/`。完成 OpenSpec 严格校验与 `git diff --check`。Jetson、完整平台/ROS 和长期性能验收不属于这项数值对照，仍未执行。

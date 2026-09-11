# FP16 AFFINE 元数据误拒绝修复

日期：2026-09-11。代码修复及四组合实机查询、灰度回放复测已完成。固定输入逐层数值对照随后也已完成，见 [6.2 数值验证](numeric-validation.md)。下文保留修复阶段的时间顺序记录。

## 根因及处理

实机 FP16 模板、Head 张量报告 AFFINE 标记。`RknnSession` 已接受 FP16/Float32 的 NONE 或 AFFINE 属性，但 `validateNanoTrackSessions()` 又要求浮点张量只能为 NONE，导致装配提前失败。

Rockchip [官方 YOLOv8 实现](https://github.com/airockchip/rknn_model_zoo/blob/main/examples/yolov8/cpp/rknpu2/yolov8.cc) 使用 INT8 类型与 AFFINE 标记共同判断整数仿射量化；其他情况请求浮点输出。这支持按实际类型区分整数和浮点，不能仅看到 AFFINE 就把 FP16 当作 INT8。SDK 头文件也分别定义 type、qnt_type 和 want_float。

本修复删除 NanoTrack 的重复拒绝规则，由通用会话继续校验类型、尺寸、布局及受支持的量化形式。FP16/Float32 不使用 scale/zp 再次反量化，也不按 INT8 的参数取值范围拒绝；INT8 的有限正 scale、合法 zp 及 AFFINE 要求保持不变。未支持的 DFP 形式仍拒绝，未扩大为接受任意量化表示。

没有更换模型、归一化方式、SDK 构建版本或公共算法参数。design.md 和单模型会话规格补充了该规则。

## 新增回归测试

`floatingAffineMetadata()` 对 FP16 和 Float32 的三模型输入输出设置 AFFINE 元数据，分别使用 scale=1、0.2、NaN，并包含超出 INT8 范围的 zp。测试确认装配、模板和搜索成功；独立 Head 输入检查逐元素确认特征不被改写，最终位置输出保持 30.224。NaN 仅用于构造无关元数据，不作为张量数据值。

原有 INT8 无效量化、DFP 拒绝、换序、布局、失败释放和公共跟踪测试继续执行。新增测试不是实际 SDK 转换精度的独立参考；修复阶段任务 6.2 尚未完成，后续通过结果见 [数值验证](numeric-validation.md)。

## 验证结果

- 用既有 Runtime 2.3.0 SDK 在约定 ARM64 临时容器中构建成功，没有升级或重建镜像。执行 `.docker-artifacts/nanotrack/test-all.sh`，16/16 CTest 通过。
- 改动的两个 C++ 翻译单元使用既有编译数据库运行 clang-tidy `clang-analyzer-*`，无诊断；clangd 均为 0 errors。
- 日志：`.docker-artifacts/nanotrack/fp16-fix-build.log`、`fp16-fix-tidy.log`、`fp16-fix-clangd.log`、`fp16-fix-test-clangd.log`。
- 执行 OpenSpec 严格校验及 `git diff --check`。

尝试新建板上 `/home/firefly/drone_tracker_test/nanotrack_fp16_fix_20260911/`，上传新 probe 并顺序查询、回放四组合。SSH 命令退出 255，没有得到任何组合的查询或回放结果。两次后续连接检查仍失败；详细 SSH 日志确认 TCP 22 端口可连接，但在协议握手、认证前报 `kex_exchange_identification: Connection closed by remote host`。未确认上传完成，也未确认测试进程启动，不能将失败归因于 FP16 推理。

上述为首次复测的连接故障记录。恢复后的成功复测见下节，旧产物和证据均保留。

## SSH 恢复后的实机复测

确认新目录尚不存在，且没有 NanoTrack probe 遗留进程后，创建目录、上传修复产物并执行四组合查询和回放。开发板仍为 Runtime `2.3.2 (429f97ae6b@2025-04-09T09:09:27)`、驱动 `0.9.8`。程序 SHA-256 为 `e40c80d9f2fbc22bb6f2a9cab3ddbb742ec7153d164d58939dc107822b7b9be5`，由现有 2.3.0 SDK 构建。

四组合的模型路径选择沿用 validation.md 的表格，均来自 `nanotrackv3-official-20260910/models/components`。FP16 Backbone + INT8 Head 使用其专属 `int8/rknn_fp16_backbone_int8_head/head.rknn`，哈希为 `d1b2c3b78a40d31a4c3b3bcfaadb9ea18852ede21ebb76eaaf0238a036527225`；其余模型哈希见 validation.md，未替换权重。

### 6.1 原生属性、提交契约和责任

四组合 query 均退出 0，模板/搜索/Head 的属性完整保存在 [复测证据目录](evidence/fp16-fix/)。FP16 输入输出仍报告 AFFINE、scale=1、zp=0，现在正常接受。所有组合的角色均为模板输入 0、搜索输入 1、分类输出 0、位置输出 1。

原生图像输入为 NHWC，模板和搜索分别为 `[1,127,127,3]`、`[1,255,255,3]`；特征输出为 NCHW `[1,96,8,8]`、`[1,96,16,16]`。Head 特征输入为 NHWC，分类/位置输出为 NCHW `[1,2,15,15]`、`[1,4,15,15]`。各文件原生 FP16/INT8 类型、比例、零点、逻辑字节与带步长字节均见对应 query 日志。

实际提交统一为 Float32、按各模型输入布局整理、`pass_through=0`；SDK 输出请求 `want_float=1`。预处理负责灰度复制三通道、先浮点后缩放、保持 0～255；模型提供方负责模型内部所需归一化及特征含义，后端不再除以 255，不再对浮点结果反量化。Head 两端 INT8 比例不同时由 SDK 从浮点实数转换，不直接传递量化整数。

以上完成属性及责任核对，不声称查询能够证明模型内部归一化正确。固定输入独立参考检查在后续任务 6.2 中完成，证据范围见 [数值验证](numeric-validation.md)。

### 6.4 同条件四精度灰度回放

四组均使用 validation.md 中同一 `V_DRONE_051.mp4`、首帧框 `461.99968,275.00032,21.00032,19.999744` 和默认算法参数。依次运行，无本次测试间的并发负载。每组命令为：

```sh
./rk3576_nanotrack_probe --template-model TEMPLATE --search-model SEARCH --head-model HEAD --mode query
timeout 120 ./rk3576_nanotrack_probe --template-model TEMPLATE --search-model SEARCH --head-model HEAD \
  --mode replay --video /home/firefly/drone_tracker_test/yolo_acceptance_20260910/V_DRONE_051.mp4 \
  --bbox 461.99968,275.00032,21.00032,19.999744 > replay-COMBO.csv 2> replay-COMBO.log
/usr/bin/python3 summarize_replay.py "$PWD" COMBO
```

`COMBO` 为下表对应的 `fp16-fp16`、`int8-int8`、`fp16-int8` 或 `int8-fp16`。所有 query/replay 退出码均为 0；每组处理 309 帧，排除初始化帧后 308/308 次搜索返回有效框，并匹配 308 份唯一目标标注。统计口径、输入身份、标注索引和耗时范围与 validation.md 相同。

| Backbone / Head | 平均 IoU | 平均中心误差（像素） | 平均置信度 | 平均 / P95 耗时（ms） | 末帧 IoU / 中心误差（像素） |
| --- | --- | --- | --- | --- | --- |
| FP16 / FP16 | 0.207852 | 6.85248 | 0.986787 | 17.8382 / 20.2024 | 0.058373 / 13.4959 |
| INT8 / INT8 | 0.315898 | 2.34935 | 0.997392 | 10.7184 / 11.1807 | 0.091477 / 1.34885 |
| FP16 / INT8 | 0.620075 | 3.70196 | 0.999768 | 17.6962 / 21.3376 | 0.726825 / 3.43906 |
| INT8 / FP16 | 0.543530 | 3.59226 | 0.997997 | 11.2333 / 12.8833 | 0.658341 / 1.36639 |

IoU 是预测框和标注框的交并比。四组与标注完全无重叠的帧数均为 0，但全 FP16 末帧中心误差和框重叠表现出更明显的偏离；有效框或高置信度不等于准确。逐帧误差已保存，不通过改阈值掩盖差异。这只是一个灰度序列的结果，不能据此断定所有场景的最优精度，亦不能在完成 6.2 前把差异完全归因于模型量化质量。

所有原始查询、CSV、回放日志及带标注哈希的统计均在 `evidence/fp16-fix/`。本阶段结束时任务 6.1、6.4 完成，进度为 28/29，剩余 6.2 尚未完成。后续数值验证完成后进度为 29/29，当前状态见 [验证汇总](validation.md)；尚未归档。

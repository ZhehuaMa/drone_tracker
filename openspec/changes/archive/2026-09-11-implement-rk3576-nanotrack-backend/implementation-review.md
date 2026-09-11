# 实现与工件核对

日期：2026-09-11。此记录对应任务 7.3，核对 proposal、design、两份增量规格（本变更新增的行为要求）与实现及验证记录。它不是所有实机验收通过的声明。后续复测已完成 6.1、6.4，6.2 也已取得四组合逐层数值证据，见 fp16-fix.md 与 numeric-validation.md。当前任务 29/29。

## 方案与范围

| 工件 / 决策 | 实现和证据 |
| --- | --- |
| proposal：独立 NanoTrack、通用会话、保持公共算法 | `rknn_session.*` 提取 SDK 执行，新增 NanoTrack 三文件工厂；本变更未修改公共框更新及 ROS 配置 |
| design 1：按语义接受模型替换 | `rknn_nanotrack_contract.cpp` 按形状和类型检查；没有模型名称或哈希白名单；提供方责任已写入 README |
| design 2：三层分离 | `rknn_api.*` 是 SDK 边界，`rknn_session.*` 是单模型会话，YOLO/NanoTrack 分别检查各自模型契约；旧 Runtime 只包装会话，没有第二套 SDK 执行 |
| design 3：Float32 模型交接 | `submission()` 按目标排列复制浮点数据，`want_float=1` 后不再缩放；注入测试验证特征、换序及不同量化参数，实机数值对照仍待 6.2 |
| design 4：模板与搜索状态 | `setTemplate()` 先 reset；搜索不改模板；会话失效使 ready 为 false；测试覆盖前置输入失败、设备失败及重复重置 |
| design 5：灰度浮点预处理 | `nanotrack_preprocessor.cpp` 使用公共几何与 TargetMean，先转 Float32 再 resize；独立像素参考、均值回退、缓存占用和生命周期测试已有覆盖 |
| design 6：保守失败策略 | 会话在提交后失败调用 wait，无法确认停止则终止；成功 get 后复制并释放输出；保留旧输出和失败释放有注入测试；共存实机停止检查通过 |
| design 7：分层验收 | 独立 probe 调用真实 NanoTracker；构建、实机 YOLO、全 INT8 回放及有限共存分别记录，未冒充 Jetson 或完整平台验收 |

## 逐项规格对应

| 规格要求 | 代码 / 验证 |
| --- | --- |
| 单模型查询、多输入输出、拒绝动态尺寸 | `RknnSession::initialize/checkAttribute`；`rknn_session_test.cpp` 双输入及非法尺寸用例 |
| 全部输入在提交前校验 | `RknnSession::execute` 先检查数量、索引、形状、字节和 owner；11 类非法输入测试确认没有 SDK 提交 |
| 类型转换保持语义 | Float32 输出描述去除量化元数据；固定 1.0/128.0 输出测试防止再次反量化或归一化；四组合真实 SDK 独立参考对照通过，详见 numeric-validation.md |
| 同步及独立输出所有权 | `OutputStorage` 自有 vector；后续推理、销毁后旧输出存活测试；头文件声明单会话串行 |
| 失败与设备停止边界 | set/run/get/release/错误字节数注入；失败结果为空、会话失效；实际共存检查见 validation.md |
| 保持 YOLO 行为 | `validateYoloSession` 保留单输入、六输出、尺寸、比例和 NCHW 检查；本次 309 帧和原始张量回归通过 |
| 三文件替换、完整装配、角色唯一 | 三个独立 load 后统一检查；部分失败由所有权对象回收；输入输出换序、额外/重复角色均有测试 |
| 四精度共用调用链 | 注入测试覆盖四精度、两布局和换序；四组合均已通过实机查询和灰度回放；6.2 独立数值对照已通过 |
| 灰度预处理、模板生命周期 | 独立浮点参考和真实 NanoTracker 注入调用覆盖；全 INT8 灰度回放补充实际调用证据 |
| 独立查询及真实跟踪 | probe 接受三个路径、query/replay、首帧框；显式转灰度；普通失败帧不输出成功框；平台选择仍拒绝完整 rk3576 |
| 区分适配正确性与模型质量 | validation.md 分列未完成的固定输入对照与已取得的 INT8 质量统计；没有更改阈值或把置信度当准确率 |

## 已解释但尚未解决的边界

1. 规划原文使用 Runtime 2.3.0，当前项目目标为 2.3.2。用户明确要求先使用现有构建产物，因此未重建 SDK 镜像。YOLO 同环境回归通过，NanoTrack 全 INT8 可执行；不能扩展为所有模型数值兼容。
2. NanoTrack 原先额外拒绝带量化标记的浮点张量，实际 FP16 模型带恒等 AFFINE 标记。该检查现已移除，设计和规格补充浮点 AFFINE 元数据场景，新增测试确认 scale/zp 不改写浮点特征或输出。修复后 16 项测试通过；SSH 恢复后四组合实机复测通过；固定输入数值对照随后也通过，见 [修复记录](fp16-fix.md)。
3. 通用会话明确拒绝不受支持的量化形式；旧 YOLO 对浮点输出没有单独排除 DFP 标记。真实现有 YOLO 的 FP16 输出为 AFFINE，本次回归通过；对不受支持的 DFP 浮点模型的接受范围不能宣称与旧代码逐项相同。这项防御性检查对应新会话规格的“不支持的量化形式明确拒绝”，不作为放宽现有模型契约的理由。
4. 共存测试只覆盖全 INT8 NanoTrack、既有四类别 YOLO、固定图像及约 18 秒负载。未测任意长时间泄漏、设备专属内存或完整 ROS 生命周期。

## 校验与完成定义

执行 `openspec validate implement-rk3576-nanotrack-backend --strict` 和 `git diff --check`。结构校验、证据核对和已解释偏差的记录完成后可勾选 7.3；6.1、6.4 已有后续实机证据，6.2 四组合独立参考对照也已通过。任务 29/29，尚未执行归档。

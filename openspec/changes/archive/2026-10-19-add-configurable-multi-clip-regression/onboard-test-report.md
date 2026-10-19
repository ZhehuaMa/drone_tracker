# 四用例生产板测试报告：前置输入缺失

后续数据已迁移并完成恢复后的测试。**当前结论见 [恢复后的测试报告](onboard-test-report-resumed.md)**。本文件保留当时输入缺失的历史状态。

日期：2026-10-16（Asia/Shanghai）。变更：`add-configurable-multi-clip-regression`。

**结论：用户流程任务 2 失败，阻塞后续任务。** 指定的两段叠框录像在当前可访问的来源、迁移目录及开发板目录中均未找到。逐帧日志、云台姿态和 session 配置也没有可证明与指定叠框录像对应的来源。因此未生成正式测试输入，24 轮中实际启动 0 轮、完成 0 轮、合格 0 轮。没有性能比较结论。

## 执行范围与状态

工作树为 `/home/mzh/nickluo/drone_tracker-worktrees/configurable-video-regression`，分支为 `codex/configurable-video-regression`。HEAD 为 `306e33b9e06a0cc85a8a84608b18cf6b4a85ec78`。任务 1 的实现仍为未提交修改；该提交号不能单独代表全部实现内容。本次保留这些修改，没有提交、推送、切换正式部署或修改生产服务。

本次使用 `openspec-apply-change` 技能读取本变更的提案、设计、规格和任务。CLI 返回的英文工件提示与用户明确要求的 OpenSpec 简体中文规则不一致，本报告遵循用户规则。任务清单仍为 **28/31**：2.7、6.2 未完成；6.3 属于用户流程任务 3，由于任务 2 失败而未启动。本报告不是正式的 OpenSpec 实现核验，也没有执行用户流程任务 4。

## 四项计划与实际执行

“参考”表示准备冻结为基线的一组独立运行；“候选”表示随后与该基线比较的一组独立运行。每项原计划分别运行三次，两侧合计六次。三个 test-03 区间分别初始化状态，不拼接。

| 用例身份 | 指定录像 | 选择范围 | 参考计划／启动／完成 | 候选计划／启动／完成 | 结果 |
|---|---|---|---|---|---|
| `wlcb-1605-whole` | `test-02-20260929-1605.叠框.mp4` | 全部帧，省略区间字段 | 3／0／0 | 3／0／0 | 未执行：缺少正式输入 |
| `wlcb-104206-020-035` | `test-03-20260929-104206.叠框.mp4` | `[20,35)` 秒 | 3／0／0 | 3／0／0 | 未执行：缺少正式输入 |
| `wlcb-104206-100-130` | 同上 | `[100,130)` 秒 | 3／0／0 | 3／0／0 | 未执行：缺少正式输入 |
| `wlcb-104206-165-end` | 同上 | `[165,end)` 秒 | 3／0／0 | 3／0／0 | 未执行：缺少正式输入 |

方括号表示包含起点，圆括号表示不包含终点。`end` 表示经源数据验证的正常录像终点。本次无法核验两段录像的 SHA-256 内容校验值、真实帧数、首末时间、末帧保留或叠框像素。因此这些数据保持未知，没有根据文件名、名义帧率或历史录像推算。

## 输入阻塞与查找证据

指定源目录为 `/Users/mazhehua/Desktop/乌兰察布测试数据/叠框视频_20261010`。本机不存在 `/Users`，原目录不可访问。

1. 在宿主机 `/home/mzh`、`/data`、`/mnt`、`/media`、`/tmp` 使用 `rg --files --hidden --no-ignore` 查找两段指定文件名及含“叠框”的文件名。包含迁移归档目录、原仓库被忽略的产物及工作树。未发现匹配文件。查找遇到部分系统私有临时目录及 JetPack 根文件系统的权限拒绝，原始错误完整保留；这不是对整台机器每个目录均已查尽的声明。
2. `/home/mzh/Desktop` 为空；迁移记录只证明项目目录的迁移，不能证明源工作站 Desktop 中的媒体已迁移。没有挂载未挂载磁盘，也没有访问原工作站或其他网络来源。
3. 仅通过 adb 在目标板的 `/home`、`/root`、`/opt`、`/userdata`、`/mnt`、`/media`、`/tmp`、`/srv`、`/var/tmp`、`/oem`、`/info`、`/rockchip-test` 查找，跟随符号链接。未发现指定录像、`.db3` 姿态包或 `session.json`；发现的 `/oem/SampleVideo_1280x720_5mb.mp4` 是其他视频，未使用。
4. 板端保留原参考用例的 FFV1 片段、帧映射及历史结果。这些只对应原参考输入，不能代替本次四个叠框用例。
5. `tests/tracking_regression/configs/wlcb-overlay-four-cases.yaml` 中每项的 `frame_logs`、`attitude_bags`、`session` 仍为明确的 `TODO_verified_*` 路径。本次逐项检查记录在 `input-path-checks.json`，所有配置源文件定位均不可用。没有根据近似名称填入其他录制的配套数据。

没有启动 `run_suite.py` 或板端回归程序。未将“媒体查找”计为任何视频测试轮次，也未把任务 1 的合成视频或 QEMU 检查计为生产板结果。未生成基线、候选集合、指标或性能判定。

## 实际板卡及模型

板端所有访问均使用 `adb -s 5ceb49407502303d`，没有使用 SSH、scp、rsync 或板端网络接口。

| 项目 | 本次只读查询结果 |
|---|---|
| 板卡 | `HZ-RK3576_WuKong Board` |
| adb／设备树序列号 | `5ceb49407502303d` |
| machine ID | `1fe0a397f2ae4343896c7cf9d253a608` |
| 主机名 | `hzhy` |
| 系统／内核 | Ubuntu 22.04.5 LTS ARM64／`6.1.141-rt36` |
| 系统可见内存 | 8,112,044 KiB，即 8,306,733,056 字节，约 7.74 GiB |
| 系统 RKNN Runtime | `/usr/lib/librknnrt.so`；`/lib/librknnrt.so` 解析为同一文件 |
| Runtime SHA-256 | `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`，与既有 2.3.2 基线一致 |
| 驱动 | `/sys/kernel/debug/rknpu/version` 返回 `RKNPU driver: v0.9.8` |
| NPU 节点 | `/dev/dri/renderD128`、`/dev/dri/renderD129` |

序列号、machine ID 和内存与仓库 `tests/tracking_regression/acceptance.md` 的既往生产板验收记录一致。因此本次连接的是该记录的同一板卡。通用 AGENTS 中 3.8 GiB 的描述与实测内存不一致；本报告保留实际值，没有改写硬件说明或以其他板卡替代。没有启动 `rknn_server`，也没有加载模型来查询 Runtime。

板端既有模型只做了文件校验，没有运行推理。以下校验不证明它们与缺失的两段录像 session 相符：

| 板端模型路径 | SHA-256 |
|---|---|
| `/opt/drone_tracker/models/balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| `/opt/drone_tracker/models/fp16/template_backbone.rknn` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| `/opt/drone_tracker/models/fp16/search_backbone.rknn` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| `/opt/drone_tracker/models/fp16/head.rknn` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

## 可复核证据与恢复条件

证据根目录为 `/home/mzh/nickluo/drone_tracker-validation/configurable-video-regression-task2`：

- `test-report.json`：四项逐项计划、启动和完成数量、输入定位检查、板卡身份及失败状态。
- `evidence/commands.json`：实际命令参数、退出码和原始输出文件位置。查找命令返回 2 的权限错误没有当作通过。
- `evidence/host-media-search.*.txt`：宿主机查找结果和权限限制。
- `evidence/board-media-search.*.txt`：adb 板端查找结果。
- `evidence/input-path-checks.json`：模板中四项源文件的配置路径、解析路径及存在性。
- `evidence/board-identity.stdout.txt`、`evidence/board-models.stdout.txt`：本次板卡、系统库、驱动和既有模型查询。
- `evidence/git-status.stdout.txt`、`evidence/git-source.stdout.txt`：当前源码状态。
- `evidence/openspec-list.stdout.txt`、`evidence/openspec-apply.stdout.txt`：项目根和 28/31 任务状态。

任务 1 的成功证据仍保存在 `/home/mzh/nickluo/drone_tracker-validation/configurable-video-regression-task1`；本报告不重复执行或改变其结论。

恢复任务 2 需要提供这两段指定叠框视频的可访问、可验证副本，以及与每一帧顺序和时间对应的真实帧日志、云台姿态和 session 配置。输入核验后才能按原预算执行四项各三轮参考、三轮候选。不得截短、替换像素、模拟姿态、放宽截止或以其他视频补齐轮次。本次按用户指定的失败规则停止；任务 3、4 均不启动。

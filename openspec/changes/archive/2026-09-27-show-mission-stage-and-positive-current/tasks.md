# Tasks

## 1. 任务状态栏

- [x] 1.1 让 `src/drone_tracker_node.cpp` 精确识别 `/fpv/tracker_trigger` 的 `frame_id="MISSION"`，并在下一条本节点 `/fpv/tracker_state` 发布时解除任务覆盖；核对回调对 `ON`、`OFF`、`MISSION` 和其他字符串的精确分支，并用显示管理器测试验证触发与状态事件顺序。
- [x] 1.2 在 `src/osd/presenter.*` 和 `src/osd/presentation.h` 保存任务覆盖并交给渲染状态；用显示管理器测试验证跨帧持续、状态事件解除和两秒提示到期后仍持续。
- [x] 1.3 在 `src/osd/layout.cpp` 把任务覆盖绘制为状态栏 `ON MISSION`，保留目标框的原跟踪状态样式；用布局测试验证 `cached-overlay` 与 `legacy` 一致且状态 `3` 仍显示 `TARGET LOST`。

## 2. 电流显示

- [x] 2.1 在 `src/osd/short_text.cpp` 对有效电流取绝对值后格式化；用短文本测试验证 `-3.0 A` 显示 `CUR 03.0A`，未知和非有限值显示 `CUR --A`。

## 3. 说明与综合验证

- [x] 3.1 更新 `docs/RK3576_HDMI_OSD.md` 的任务阶段和电流说明；核对文档明确 `MISSION` 来源、解除条件及 `TrackerState=3` 的原含义。
- [x] 3.2 运行新变更涉及的 OSD 与 ROS 定向测试及 `openspec validate show-mission-stage-and-positive-current --strict`；记录平台、通过结果和未执行的实机检查。

## 4. 生产板状态切换诊断

- [x] 4.1 记录触发接收、任务覆盖置位与解除、跟踪状态发布及停轮请求，并包含轮次和事件结果。
- [x] 4.2 只在 HDMI 成功提交且状态栏文字、跟踪框标签或显示状态变化时记录显示结果；验证 `OFF` 后的冻结底图与清框字段，以及无变化帧不会逐帧记录。
- [x] 4.3 全新编译、运行相关测试并制作调试部署包；在 `firefly-production` 预检和部署后确认服务与日志入口，保留旧版本及回退记录。

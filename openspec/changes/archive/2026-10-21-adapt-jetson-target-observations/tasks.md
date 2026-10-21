# Tasks

本清单实施 `f176960cd6e1e98c5ea341b5fcac856b508b9865` 中已选定的 RK3576 观测改进。需求见本 change 的五份 delta specs，技术选择见 `design.md`。勾选项表示已完成的实施与验收，实际结果和证据路径见 `validation.md`。

LOS 指目标视线方向，KF 指卡尔曼滤波器。QoS 指 ROS 消息队列及可靠性设置。对模型或生产板的检查必须记录实际输入，缺少资源时保留未完成状态。

## 1. 公共几何与浮点输出

- [x] 1.1 提供旋转补偿的目标方向与角速度，在 `src/hybrid_tracker/target_los.h` 移植来源数学逻辑；完成条件是独立于 ROS、CUDA、VPI 和 RKNN，支持独立 fx/fy、tau=0、300 ms 间隙及输入校验。
- [x] 1.2 保留发布所需的小数坐标，在 `src/hybrid_tracker/hybrid_tracker.*` 增加浮点观测框和重锚定复位标记；完成条件是按实际帧尺寸裁剪，保留原整数显示框、已有成功恢复判据与检测仲裁。
- [x] 1.3 验证几何和 KF 输出，在公共算法测试及其 `CMakeLists.txt` 增加无设备用例；完成条件是来源参考数值、静止世界目标复合旋转、运动目标、非等比内参、非法旋转、长间隙、小数与完全越界框均通过，既有仲裁测试通过。
- [x] 1.4 说明数学输出边界，更新 `src/hybrid_tracker/ALGORITHM_REVIEW.md`；完成条件是解释方向符号、rad/rad/s、相机自转补偿、浮点与显示框用途，并注明模拟内参不能证明物理精度。

## 2. RK3576 配置、依赖与消息端点

- [x] 2.1 声明 ROS 消息依赖，在根 `CMakeLists.txt` 与 `package.xml` 显式增加 geometry_msgs；完成条件是 ROS 目标可发现依赖，纯公共算法测试仍不要求 ROS 消息头。
- [x] 2.2 提供固定启动配置，在 `src/drone_tracker_node.*` 及三个 `parameters/rk3576_*_params.yaml` 模板加入 publish_prediction=false、los.rate_filter_tau=0.12；完成条件是 tau 有限非负、运行期修改两项被拒绝、没有 enable_preview。
- [x] 2.3 建立新端点与队列设置，在节点增加 reliable/depth 10 的 LOS publisher、best-effort/depth 100 的 MAVROS IMU subscriber；完成条件是 gimbal 为 best-effort/depth 10、tracker_image 为 best-effort/depth 1，MAVROS 回调只提交原消息记录且不更新相机旋转。
- [x] 2.4 验证配置与实际像素契约，在配置测试、帧源测试及 `src/tests/ros_video_test.cpp` 核对实际尺寸与显式内参；完成条件是支持 fx!=fy，不按 HDMI 或名义输出重缩放，三个模板可加载，非法 tau 与运行期更新失败，专用 launch 保持 params_file 行为。
- [x] 2.5 验证消息兼容性，在 ROS 测试中按 best-effort 订阅调试图像并检查各端点 QoS；完成条件是 ON 内有合法图像，ON 前/OFF 后没有 ROS 预览，只有 MAVROS IMU 时不能得到有效相机姿态。
- [x] 2.6 记录使用方法，更新 `src/platform/README.md` 与 RK3576 配置说明；完成条件是给出两种时间模式、best-effort 图像订阅和显式像素内参示例，且未引入新的预览开关或 launch 隐式算法覆盖。

## 3. 成对观测、时间与停止顺序

- [x] 3.1 统一消息构造，在 `src/target_observation.h` 实现浮点 bbox 与 LOS 配对；完成条件是相同纳秒时间、规定的 frame_id/字段、缺姿态仅使 LOS 无效、无目标两者无效、复位先于重复时间守卫。
- [x] 3.2 选择匹配的参考时间与旋转，在 `rotation_history.h` 和节点 `processFrames()` 接入采集默认及合格姿态预测；完成条件是 300000/300001 微秒边界、未来/早于帧姿态、空历史、重复姿态及倒退历史符合规格，不改帧时间。
- [x] 3.3 在正确轮次重置观测，接入普通 `run()` 与持续 `trackingLoop()`；完成条件是旧访问结束后才重置，重复 ON 不清空活动状态，新轮可接受较早时间基准。
- [x] 3.4 建立停止失效边界，在节点控制与发布路径串行化构造器访问；完成条件是 OFF 拒绝正常结果后只尝试一对无效通知、时间递增、在 bag seal 前提交，重复/首次 ON 前 OFF 不多发，持续模式不等待推理或写盘。
- [x] 3.5 验证消息数学和时间规则，在 ROS 消息构造测试中覆盖来源用例；完成条件是小数坐标、LOS 字段、复合旋转、缺失/非法姿态、丢失/恢复、重锚定遇到重复时间、新 ON 清空守卫均通过。
- [x] 3.6 验证跨线程停止与原预览，在 `src/tests/ros_video_test.cpp` 的普通与 continuous-camera 场景阻塞推理后发 OFF/ON；完成条件是旧轮无新有效发布、通知去重、独立图像/状态不因观测重复而抑制、单消费者和原 HDMI 清框行为保留。
- [x] 3.7 记录协议迁移，更新节点运行说明；完成条件是解释 TwistStamped 中 linear.y/z 为角度、共同时间不保证网络原子接收、capture/pose/实际发送时间区别，以及校时回退后的 OFF/ON 恢复步骤。

## 4. 异步消息记录

- [x] 4.1 扩展记录话题，在 `src/bag_session.h` 注册 LOS 与 MAVROS IMU，移除图像元数据及仅为图像服务的构造字段；完成条件是所有 visualized_result 设置下话题表均无 tracker_image，原 gimbal/odom/bbox 保留。
- [x] 4.2 保证队列成对接纳，在 BagSession 增加一次容量检查和一次追加的双消息提交，并在节点使用；完成条件是每对占两条消息额度、总字节受限，容量不足时两条 attempted/dropped 计数一致，仍保持 256 条与 64 MiB 上限。
- [x] 4.3 接通原始 IMU 与观测记录并移除图像提交，在节点 record/openRecording/sealRecording 路径整合新策略；完成条件是输入载荷与 header 时间不变、OFF 通知先于封存、原画面录像仍独立、回调不会写已关闭会话。
- [x] 4.4 验证 bag 内容和容量，在 ROS/bag 测试中重新读取文件；完成条件是五个规定话题的载荷/时间/计数可核对，visualized_result=true/false 均无图像数据和元数据，剩一个位置/字节不足/封存竞争均不接纳半对。
- [x] 4.5 验证记录故障隔离，在既有打开/写入/关闭错误及溢出测试中覆盖新话题；完成条件是中途半对写盘不能被报告完整，失败或 incomplete 状态可查，健康跟踪、HDMI 及录像继续，退出无遗留 writer。
- [x] 4.6 记录 bag 兼容性变化，更新平台记录说明与示例；完成条件是明确 need_save、recording.enabled、visualized_result 三者职责，说明图像 bag 已移除及独立录像/实时图像替代方式。

## 5. 诊断证据与回归报告

- [x] 5.1 定义可识别的新观测证据，在 `src/diagnostics/` 增加版本 2、固定 256 字节的浮点框/LOS 载荷及独立发布、抑制、控制通知事件；完成条件是保留旧事件编号和 Output 字段含义，double 数值与观测身份不丢失，Control 与跟踪生产者不混写同一单生产者缓冲。
- [x] 5.2 接通证据生产与双版本读取，在节点、diagnostic worker/inspector 更新格式协作；完成条件是两个实际发布调用及部分失败独立可查，OFF 不伪造帧身份，版本 1 可读，未知格式拒绝，诊断不兼容时业务仍健康运行。
- [x] 5.3 生成真实观测指标，在 `tests/tracking_regression/{collect.py,metrics.py,identity.py}` 及对应 schema 中读取新证据；完成条件是不用整数 Output 冒充发布框，分别统计框、LOS、重复抑制、控制通知、参考时间与真实发布，新增条件进入比较身份。
- [x] 5.4 验证证据与比较兼容，在 diagnostic 和 Python 回归测试中覆盖双话题同 update、旧格式、部分发布失败、重复抑制和 OFF；完成条件是既有配对/缺口检查保留，不把两个话题算成双倍框频率，旧新时间协议标为不可比，旧基线文件不被改写。
- [x] 5.5 记录格式和统计定义，更新 `tests/tracking_regression/diagnostic_contract.md`、`metrics.md` 及说明；完成条件是明示版本 1/2、double 载荷、每话题调用身份、控制通知排除规则和缺字段 unavailable 的含义。

## 6. 集成验证与生产证据

- [x] 6.1 验证默认构建与整体验收，在新工作树运行 `./build.sh --test`；完成条件是保存后端、镜像/工具链、ARM64 产物与完整测试结果，无设备测试及既有仲裁/预览/记录/Jetson 配置检查均通过，未将编译结果当作硬件通过。
- [x] 6.2 验证固定输入回归，在指定生产板使用同一已核对录像与既有模型分别执行两种时间模式；完成条件是保存输入/程序/配置/模型身份、每种模式至少三次合格完整运行及新协议独立参考，旧协议仅作标明不可比的说明性对照，每次性能/负载运行不超过 180 秒。
- [x] 6.3 验证生产相机联合运行，在指定 HZ-RK3576_WuKong Board 完成至少 20 轮 ON/OFF、至少 60 秒跟踪/录像/HDMI 有效负载及启动和最终 OFF 各至少 10 秒预览；完成条件是每次总长不超过 180 秒、相机不因切轮重开、清框不超过 500 ms、无 ROS 预览、时间与观测配对正确、资源回收可查。
- [x] 6.4 验证生产记录与开关边界，在同板覆盖 need_save 与 visualized_result 开关、LOS 和 MAVROS IMU，按相同负载核对 bag 开关对跟踪/HDMI 的影响；完成条件是保存实际计数、时长、帧率与丢弃，bag 可读且无图像，独立录像按原配置工作，Runtime 2.3.2/驱动 0.9.8 及系统库指纹不变，每次性能/负载运行不超过 180 秒。
- [x] 6.5 汇总实现与规格一致性，创建本 change 的验证报告；完成条件是逐项关联需求、测试与证据，分别列出容器和生产结论，缺板卡/模型/录像保持未完成，模拟内参不被当作角度标定或实飞质量证明。

## Workflow follow-up

- 全部实施与所需验收完成后，另行执行 OpenSpec verify，再按用户请求同步或归档。归档不属于本次 propose 的执行范围。
- 生产部署仅在后续明确部署请求下进行；保留上一二进制、配置与诊断工具用于成套回退，不以切换 publish_prediction 代替版本回退。

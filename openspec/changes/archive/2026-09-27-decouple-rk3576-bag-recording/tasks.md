# Tasks

## 1. 拆开记录开关与初始化

- [x] 1.1 让 RK3576 相机在 `need_save=true` 下完成初始化：修改 `src/drone_tracker_node.cpp` 与 `src/platform/frame_source_ops.h` 的记录调用边界；用可控相机测试确认节点等待 ON、帧源未收到旧相机录像请求，且 Jetson 调用路径保持原样。
- [x] 1.2 让 RK3576 录像专属配置错误不阻止节点初始化：调整 `src/drone_tracker_node.cpp` 和 SC233HGS 录像会话创建路径；用 AR0234、视频及无效 SC233HGS 录像参数测试确认 ON 后报告录像不可用，而健康跟踪仍有输出。

## 2. 隔离 ROS bag 写入

- [x] 2.1 建立每轮独立的有界 bag 会话：在 `src/drone_tracker_node.*` 附近实现单工作线程拥有写入器、最多 256 条且最多 64 MiB 的队列；用容量边界测试确认提交方不等待写盘，队列溢出计数且标记不完整。
- [x] 2.2 保留原话题与时间戳：将姿态、里程计、检测框和条件图像的写入调用改为入队；用可控消息测试确认消息内容、时间戳和各话题顺序正确，且 `visualized_result=false` 时不注册图像话题。
- [x] 2.3 隔离 bag 打开、写入和关闭故障：调整 `src/drone_tracker_node.*` 的会话状态与失败报告；用现有测试注入点或等价替身确认 bag 故障不设置跟踪停止标志，不影响健康 HDMI 与独立录像，并保留目录排他创建。
- [x] 2.4 保证 OFF、EOF、退出与迟到回调安全收尾：调整 bag 会话和 `src/tests/ros_video_test.cpp`；用并发回调与重复 ON/OFF 测试确认旧轮消息不混入新轮、关闭后不访问已释放写入器，并测量收尾耗时。

## 3. 验证记录内容和完整性

- [x] 3.1 加入正常 bag 完整性回查：扩展 `src/tests/ros_video_test.cpp`，使用 `rosbag2_cpp::Reader` 读到文件末尾；核对各话题类型、消息数、原时间戳和可识别载荷，与本轮实际接受及写入计数一致，正常轮无丢弃才判完整。
- [x] 3.2 加入禁用图像的回查：以真实参数名 `visualized_result=false` 和 `need_save=true` 运行测试；读取已关闭 bag 并确认 `/fpv/tracker_image` 消息数为零、话题元数据不存在，同时检测框仍可读。
- [x] 3.3 覆盖 bag 不完整及双记录并行：增加队列溢出、写入故障及 SC233HGS 同时启用两个开关的可控测试；核对失败原因和计数，健康的跟踪与另一记录支路继续运行。

## 4. 文档、构建与生产板对照

- [x] 4.1 更新 `src/platform/README.md` 与 `parameters/rk3576_camera_params.yaml` 的说明和示例；核对 `need_save`、`save_path`、`recording.enabled`、`visualized_result` 的名称、作用、支持输入和失败表现均准确，并确认生产配置保持 `visualized_result=false`。
- [x] 4.2 在匹配 RK3576 SDK 环境构建并运行适用的 `ros_video_tests` 与相关录像测试；保存命令和结果，确认无新增失败，并单列未在 Jetson 实机执行的检查。
- [x] 4.3 在同一块生产 HZ-RK3576_WuKong Board 完成 `need_save=false/true` 成对对照；两轮均启用 `recording.enabled=true` 和 HDMI，并保持 `visualized_result=false`，每种状态建议测量 60 秒，整次性能或负载试验不超过 180 秒。交付报告须给出板卡、模型、相机、bag 存储身份，以及跟踪消费帧率、HDMI 实际显示新视频帧率、原始计数、差值和丢帧；不得用设定帧率替代实测。
- [x] 4.4 在生产板对成对对照中正常关闭的 bag 执行只读完整性检查；交付报告须列出可读取性、话题消息数、原时间戳、不含 `/fpv/tracker_image` 消息及元数据的证据与不完整标记，并与本轮节点计数对账。相关负载运行仍属于 4.3 的 180 秒成对测试，不额外延长运行时间。
- [x] 4.5 对照本 change 的 proposal、design 和四份 delta specs 核验实现与证据；在 `openspec validate decouple-rk3576-bag-recording --strict` 通过后记录任何未完成的实机或 Jetson 验证，不提前同步或归档未完成变更。

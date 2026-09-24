# Tasks

## 1. 渲染路径配置与边界

- [x] 1.1 在 `src/osd/config.h` 定义 `cached-overlay` 与 `legacy` 渲染路径类型和严格解析规则；以配置单元测试验证两个合法值可解析、未知值被拒绝且默认值为 `cached-overlay`
- [x] 1.2 在 `src/drone_tracker_node.h`、`src/drone_tracker_node.cpp` 和 RK3576 参数 YAML 接入只读启动参数 `osd.render_path`；以 `ros_video_tests` 验证启动选择、无效值报错和运行期修改被拒绝
- [x] 1.3 在 RK3576 OSD 工厂边界保留旧后端并按配置选择优化后端；以构造替身测试验证两条路径共享 `OsdPresenter` 且不会创建第二个视频消费者

## 2. 缓存式布局

- [x] 2.1 在 `src/osd/layout.h` 和 `src/osd/layout.cpp` 增加尺寸相关的持久化 RGBA 画布、静态底图和文字字形缓存；以 `osd_layout_tests` 验证相同尺寸重复渲染不重复分配或栅格化固定内容
- [x] 2.2 为目标框、状态和动态遥测字段实现脏区域失效，清除范围使用上一次与本次区域的并集；以 `osd_layout_tests` 验证移动、缩短文字、目标消失和状态切换后没有残影
- [x] 2.3 为分辨率、主题数据和跟踪轮次变化实现完整画布重建；以 `osd_layout_tests` 验证重建后的边框、文字、透明区域和目标框与既有布局基准一致

## 3. RK3576 视频与显示缓冲管线

- [x] 3.1 在 `src/platform/rk3576/osd/hdmi_backend.*` 增加同步 RGA 源视图包装，显式传递像素格式、可见宽度和行步长，对硬件无法表达的布局用可复用缓冲重排，并让 `FrameReadView` 存活到调用完成；以带填充步长的 `Gray8`、`Bgr8` 测试和生产板 RGA 探针验证颜色、行边界和生命周期
- [x] 3.2 当生产板探针不支持 RGA 虚拟地址输入时，在 RK3576 平台边界实现只读 DMA 帧视图回退，并在同步 RGA 完成后才归还采集缓冲；完成条件为虚拟地址路径或 DMA 回退路径至少一条在生产板通过连续输入验证，且无提前复用证据
- [x] 3.3 将优化路径改为“主平面纯视频双缓冲、覆盖平面双缓冲、单个持久化 CPU 画布”，删除该路径的中间视频双缓冲、纯视频额外复制和全帧 HUD 混合；以代码级 RGA 调用计数测试和板端阶段日志验证兼容 BGR 每帧一次同步缩放转换、常规灰度每帧两次同步操作（奇数尺寸按设计扩展为 BGR），且没有视频 `imcopy` 或 HUD `imblend`
- [x] 3.4 在 `src/platform/rk3576/osd/hdmi_backend.*` 分别实现主平面和覆盖平面的 `displayed`、`prepared` 索引及提交代次；以状态机测试验证成功翻页后才推进索引，提交失败、等待超时和过期授权均保留最后确认画面
- [x] 3.5 实现仅遥测变化和 OSD 关闭冻结时复用当前主平面、只准备新覆盖平面的原子提交；以显示后端替身测试验证主平面索引不变、覆盖平面更新且视频与遥测仍为同一提交组合
- [x] 3.6 将黑色背景、色度平面和尺寸相关 scratch 初始化移到创建或几何变化路径；以非零预填充测试验证每次写入覆盖完整有效区域，并以调用计数验证稳定输出期间不执行无条件全帧清空

## 4. 阶段计时与诊断

- [x] 4.1 扩展 `src/osd/timing.h` 和 `src/osd/timing.cpp`，使优化路径报告 `source_prepare` 与 `overlay_upload`，旧路径继续报告 `source_copy` 与 `compose`；以 `osd_timing_tests` 验证两种格式的阶段名称、样本数、均值、最大值和第 95 百分位
- [x] 4.2 在优化后端的源包装、缩放转换、布局更新、覆盖层上传、原子提交和翻页等待边界接入计时；以受控时钟替身验证各阶段不重叠计数，且 `render_total` 覆盖完整处理区间
- [x] 4.3 保持 `osd.timing_enabled=false` 的零采样行为，并让 `source_prepare` 在零复制路径仍产生有效样本；以 `osd_timing_tests` 和 `osd_presenter_tests` 验证关闭计时无调用、开启计时可区分新视频、仅遥测、拒绝和失败结果

## 5. 主机侧回归测试

- [x] 5.1 扩展 `src/osd/tests/layout_test.cpp`，覆盖字形缓存、脏区域、透明度、目标框移动、状态轮次和几何变化；运行 `ctest -R osd_layout_tests --output-on-failure` 并确认全部通过
- [x] 5.2 扩展 `src/osd/tests/presenter_test.cpp`，覆盖优化路径下严格配对、单待显示项、仅遥测更新、关闭冻结、过期授权、新轮次和异常停止；运行 `ctest -R osd_presenter_tests --output-on-failure` 并确认全部通过
- [x] 5.3 增加可脱离 DRM 硬件运行的缓冲索引与源步长测试目标，并在 `src/osd/CMakeLists.txt` 或 `src/platform/rk3576/osd/CMakeLists.txt` 注册；以 CTest 验证提交成功、提交失败、翻页超时、`Gray8` 和 `Bgr8` 带填充步长场景
- [x] 5.4 运行启用 `DRONE_TRACKER_BUILD_TESTS=ON` 的完整 CTest 集合；确认原有 OSD、ROS 初始化、视频回放和平台配置测试无回归，并在验证记录中列出测试总数与失败数

## 6. 文档与部署配置

- [x] 6.1 更新 `src/platform/README.md`，说明缓存式双平面数据流、`osd.render_path`、两条路径的阶段名称、生产板前置条件和 `legacy` 回退步骤；以文档审查确认无需参考对话即可执行启动和回退
- [x] 6.2 更新 RK3576 相机与视频参数文件，使验收通过后的默认路径为 `cached-overlay`，并保留显式 `legacy` 示例；以 YAML 加载测试确认两个入口取得一致默认值
- [x] 6.3 新建本变更的 `validation.md` 记录模板，包含板卡标识、系统版本、Runtime/驱动、模型、输入文件校验值、分辨率、散热、CPU governor、每轮命令和阶段统计表；以人工审查确认能够复现实机 A/B

## 7. 生产板功能与性能验收

- [x] 7.1 在指定 HZ-RK3576_WuKong Board 上验证 RGA 源输入路径，覆盖连续 `Gray8`、`Bgr8`、紧密步长和填充步长；将截图或校验结果及所选虚拟地址/DMA 路径写入 `validation.md`
- [x] 7.2 在同一生产板、同一系统设置、同一真实模型和固定回放输入下，分别执行 OSD 关闭、`legacy` 和 `cached-overlay` 基线；每条启用路径运行三次且每次至少 70 秒、排除前 10 秒，并在 `validation.md` 记录完整统计
- [x] 7.3 验证三轮 `cached-overlay` 均达到 `render_total` 均值不超过 40 ms、第 95 百分位不超过 65.536 ms、HDMI 新视频帧显示速率不低于 25 fps、`submit_to_display` 均值不超过 60 ms；同时确认相对 OSD 关闭基线的跟踪消费帧率下降不超过 5%
- [x] 7.4 在生产板执行 20 轮 OSD 开启与关闭，逐轮核对关闭冻结最后实际视频、恢复后显示新配对画面，并把通过轮数和任何异常写入 `validation.md`
- [x] 7.5 在生产板执行至少 10 分钟完整跟踪满负载运行，验证无撕裂、错误颜色、缓冲污染、持续积压、死锁或异常退出，并在 `validation.md` 保存资源占用和阶段耗时结果
- [x] 7.6 注入 DRM 设备打开失败、原子提交失败、翻页超时和进程退出场景；验证跟踪主流程的故障隔离、最后确认缓冲状态和资源释放，并将结果写入 `validation.md`
- [x] 7.7 复核程序在两条渲染路径下均未修改 CPU governor、进程优先级或线程亲和性；记录测试前后系统值一致后，确认 `cached-overlay` 可作为发布默认值


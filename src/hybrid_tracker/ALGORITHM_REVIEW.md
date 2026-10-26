# 混合跟踪器算法综述（Hybrid Tracker Algorithm Review）

## 1. 范围与组成

本文说明当前 `rk3576` 分支的代码行为。算法入口是 `HybridTracker::update()`；平台装配、ROS 参数覆盖和消息发布分别由平台工厂及 ROS 节点负责。下文默认值来自 C++ 结构体，不代表每份 YAML 或 launch 的最终运行配置。

管线由四部分组成：

1. `YoloV8Detector`：异步检测指定类别，负责首次发现目标和跟踪期间的再次确认。
2. `NanoTracker`：使用 NanoTrackV3 神经网络逐帧估计目标框。它不是相关滤波器。
3. `NanoDcfTracker`：融合 NanoTrack 尺寸与平滑后的中心。可选的 DCF（判别相关滤波器）使用灰度图定位中心；关闭 DCF 时改用独立的中心卡尔曼滤波器。
4. `RotationKalmanFilter`：估计中心、尺寸和中心速度，补偿相机旋转，并支持按测量时刻回溯校正。

公共算法通过 `TrackerComponents` 接收模型后端、预处理器和帧兼容性检查对象，不直接加载平台 SDK。Jetson 后端使用 TensorRT/VPI，RK3576 后端使用 RKNN Runtime。逻辑模型包括 YOLO、NanoTrackV3 Backbone 和 Head；RK3576 的 Backbone 分别提供模板和搜索模型文件。

## 2. 异步 YOLO 检测

### 2.1. 请求、结果与轮次

检测器只有一个工作线程。它最多保存一个尚未开始的请求；新请求可以替换这个等待项，但不会修改正在执行的输入。存在未读结果、正在清理轮次或已经停止时，请求会被拒绝。结果槽只保存最新结果。

每次正常进入检测或跟踪分支时，主线程只读取一次结果快照，释放未读结果对提交的限制。随后提交或跳过当前帧请求，再处理刚才取得的结果。同一次更新不会再次读取后来完成的检测。

请求携带源帧编号、轮次编号，以及源帧的跟踪框和预测框等比较信息。比较信息不持有图像。执行项、等待请求和补帧缓存分别管理所需的共享帧引用。

`finishRound()` 应在停止调用 `update()` 后执行。它取消等待请求，等待正在执行的检测不再访问帧，并清除未读结果。`reset()` 重置算法状态并递增轮次编号；旧轮次结果不会参与当前轮次的初始化或目标仲裁。

### 2.2. 检测级联与后处理

每个请求按下列顺序执行：

1. 请求包含有效 ROI 时先检测 ROI。ROI 是感兴趣区域，即优先检查的局部图像。此阶段阈值为 `yolo_conf_threshold * yolo_recall_relax_ratio`。
2. ROI 没有找到目标类别时，检测全图。
3. 全图仍未找到时，执行自适应重叠切片。短边分为两份，长边按图像比例决定份数；重叠比例为 `0.2`。找到目标的第一个切片结束搜索。

级联采用第一个找到目标的阶段，并返回该阶段置信度最高的单框。不会对全部阶段、全部切片的候选统一排序。当前没有单独的切片开关。

后处理只读取 `target_class_id` 对应的类别分数。它还原 letterbox 坐标、执行尺寸过滤和 NMS。letterbox 是保持宽高比缩放并补边的预处理；NMS 用于抑制高度重叠的候选框。`max_box_area_ratio` 分别限制候选框宽、高占当前检测区域宽、高的比例，不是面积比例。

## 3. NanoTrack 与中心融合

`NanoTracker` 使用 `127×127` 模板和 `255×255` 搜索输入。分类输出形状为 `[1,2,15,15]`，位置输出为 `[1,4,15,15]`。后处理结合尺度与长宽比惩罚、汉宁窗及尺寸插值选择结果。

当前没有 `tracking_conf_threshold` 参数，也不会用固定的 `0.8` 分数门槛决定是否更新主管线。搜索预处理、推理或输出契约检查失败时返回负分，融合跟踪器将本帧视为失败。原始分数由诊断接口保留。

`NanoDcfTracker` 保留 NanoTrack 的宽、高，按以下规则选择中心：

- `enable_dcf=true`：使用灰度 DCF 中心。PSR（峰值相对背景响应的强度）低于门槛，或 DCF 中心偏离 NanoTrack 太远时，采用 NanoTrack 中心并重新训练 DCF 模板。无法读取帧时退回 NanoTrack 中心。
- `enable_dcf=false`：使用 `CenterKalmanFilter` 平滑 NanoTrack 中心。预测中心与 NanoTrack 的偏差超过门槛时，采用 NanoTrack 中心并重置中心滤波器。

两条路径的中心偏差门槛均为 `dcf_max_div_factor * (width + height) / 2`。中心滤波器与主管线的 `RotationKalmanFilter` 相互独立；关闭 DCF 不等于直接输出原始 NanoTrack 框。

## 4. 每帧处理与状态

### 4.1. 输入检查与缓存

1. 检查帧、组件和平台兼容性。帧时间戳必须严格递增；重复或倒退时间戳返回 `consumed=false`，调用方跳过本帧发布。
2. 在采集时刻调用 `predictOnly()`，取得主管线卡尔曼预测。
3. 把当前帧加入补帧缓存，最多保存 `frame_buffer_size` 帧。缓存用于在检测结果晚到时恢复对应图像。
4. 根据是否已初始化跟踪器，进入检测或跟踪分支。

当前代码有一个需要保留理解的坐标细节：`predictOnly()` 返回 `(cx, cy, w, h)` 中心格式，但用于 ROI 与跳变比较的 `kf_pred_box` 直接把前两项写入 `cv::Rect2f` 的左上角字段。这是当前行为，不能把它描述为已完成中心到左上角转换。最终输出框另行执行正确转换。

### 4.2. 未初始化：发现目标并补帧

未初始化时，每个有效更新都尝试提交 YOLO 请求。有卡尔曼预测时，ROI 放大系数为 `roi_scale_factor * 1.5`；没有预测时从全图开始。

读到当前轮次的正检测结果后，必须在缓存中找到源图像。随后在源图像上用检测框初始化 `NanoDcfTracker`，并按时间顺序跟踪缓存中的后续图像。这称为补帧追赶，用来把晚到的检测推进到当前帧。

例如：第 100 帧的检测在第 104 帧更新时被读到。如果缓存仍含第 100 帧，就先在该帧初始化，再处理第 101–104 帧。源图像已淘汰时，不会拿第 104 帧替代它初始化。

初始化成功后进入 `TargetFound`，并重置主管线卡尔曼滤波器。补帧中的单步失败不会使整个 `catchupTracker()` 返回失败；显示框保留最近成功的结果。检测初始化连续失败 10 次时，清空卡尔曼状态，使后续请求不再依赖旧预测 ROI。

### 4.3. 已初始化：跟踪与跳变检查

每帧先执行 `NanoDcfTracker::update()`。跟踪框必须同时偏离卡尔曼预测和上一显示框，才算一次跳变。跳变条件是中心距离超过 `max_position_jump` 像素，或任一边长比例超出 `[1/max_size_jump_ratio, max_size_jump_ratio]`。

偶发跳变保留旧显示框，不用该测量校正主管线滤波器。连续跳变达到 `jump_tolerance` 后，清除跟踪初始化和框，并提前返回 `consumed=false`。该路径不会额外设置 `TargetLost`。

正常测量更新显示框及上一框，并保留浮点跟踪结果用于卡尔曼校正。跟踪更新失败时不输入测量，状态仍可为 `Tracking`，输出可继续使用已有预测。

每 `yolo_interval` 个有效更新尝试提交一次 YOLO 再检测请求。ROI 优先采用卡尔曼预测，否则采用显示框。ROI 宽、高按 `roi_scale_factor` 放大，并分别至少为 YOLO 输入宽、高的一半，再限制到图像范围。

### 4.4. 延迟检测仲裁

仲裁比较 YOLO 框与请求保存的同一源帧跟踪框和预测框。比较前把框裁剪到源图像范围，只比较目标的可见部分。IoU 是两个框的交集面积除以并集面积，用于衡量重叠程度。

按代码顺序执行以下规则：

| 条件 | 行为 |
| --- | --- |
| 结果属于旧轮次 | 不进行目标仲裁。 |
| 正检测缺少有效的源帧跟踪框 | 不确认，也不重锚定；不把旧显示框当作有效观测。 |
| 同时偏离源帧预测框与跟踪框，且 IoU 小于 `iou_calibrate_threshold` | 累计 YOLO 跳变。达到 `jump_tolerance` 后尝试补帧重锚定；未达到时累计未确认次数。 |
| 当前跟踪更新失败，或 IoU 小于 `iou_reset_threshold` | 尝试补帧重锚定，并累计未确认次数。 |
| IoU 大于 `iou_calibrate_threshold` | 视为确认，清空未确认及 YOLO 跳变计数。不覆盖当前框，不额外用 YOLO 框校正卡尔曼滤波器。 |
| 其余重叠中间带 | 清空未确认及 YOLO 跳变计数，保留当前跟踪结果。 |
| 检测完成但没有目标 | 清空 YOLO 跳变计数，累计未确认次数。 |

重锚定是用检测源图像重新初始化并追赶到当前帧。比较信息即使在源图像淘汰后仍可用于确认，但重锚定必须具有源图像。跳变达到接受门槛且源图像仍在时，现有代码会清空未确认计数，即使该次初始化失败；源图像不在时则累计未确认次数。低 IoU 或当前跟踪失败分支无论恢复是否成功，都会累计未确认次数。

### 4.5. 丢失、超时与状态数值

未确认次数达到 `lost_max_count` 时，清除跟踪初始化和框，并设置 `TargetLost`。未确认包括无目标、低 IoU 重初始化及尚未接受的跳变，不只是没有检测框。

已初始化时，连续未读取到检测结果的更新次数若严格大于 `lost_max_count * yolo_interval * 3`，也会清除初始化和框。默认乘积为 `60`，因此第 61 次无结果更新触发超时。任何读取到的结果都会清零此计数，包括无目标、缺少比较信息及旧轮次结果。此超时没有结果年龄门槛，也不会额外设置 `TargetLost`。

| 状态 | 数值 | 典型触发 |
| --- | --- | --- |
| `Detecting` | 0 | 未初始化的检测分支。 |
| `TargetFound` | 1 | 检测源图像初始化成功并完成补帧。 |
| `Tracking` | 2 | 已初始化分支，含预测和短暂跟踪失败。 |
| `TargetLost` | 3 | YOLO 未确认次数达到上限。 |

状态数值不能单独代替框有效性判断。持续跳变和无结果超时可能先清除内部初始化，下一次更新才进入 `Detecting`。

## 5. 卡尔曼输出与视线观测

`RotationKalmanFilter` 的状态为 `[cx, cy, w, h, vx, vy]`，测量为 `[cx, cy, w, h]`。它使用相机旋转补偿和历史状态回溯。`predictOnly()` 只查询指定时刻的预测，不修改内部状态或历史。

成功初始化或重锚定时，用追赶后的显示框在当前帧时刻重新初始化滤波器，并设置 `reset_los=true`。普通跟踪测量使用原始浮点框校正；只有 YOLO 确认时，不额外校正。最后按 `ctx.now_us` 与 `ctx.R_cam_now` 查询输出预测。

`display_box` 是整数显示框，`realtime_box` 是整数预测框。`observation_box` 保留双精度预测并裁剪到实际输入图像。内部滤波状态不按画面边界裁剪。ROS 节点根据时间模式选择观测时刻；`publish_prediction` 默认 `false`，不能据此认为所有消息都外推到墙钟当前时间。

LOS（视线）表示从相机指向目标框中心的方向。`TargetObservationBuilder` 在节点侧成对构建目标框和视线消息；这不属于 YOLO 后处理。对像素中心 `(u,v)`，相机 FLU 坐标系的射线为 `(1, -(u-cx)/fx, -(v-cy)/fy)`。FLU 的三个轴依次向前、向左、向上。

俯仰角向下为正，偏航角向左为正。角度单位是 rad，角速度单位是 rad/s。估计器先把上一射线及滤波速度旋转到当前相机系，再计算方向差，从而补偿相机自身旋转。平滑系数为 `1-exp(-dt/tau)`，`los.rate_filter_tau` 默认 `0.12` 秒；设为 `0` 时不平滑。

首个样本或超过 `0.3` 秒的间隔从零角速度开始。无效框、缺失或无效旋转及成功重锚定会清除速度历史。使用模拟内参只能验证接口行为，不能证明物理角度精度。

## 6. C++ 默认参数

以下值来自 `TrackerSettings`。ROS 节点、YAML 和 launch 可以覆盖它们；模型路径由平台装配层解释。

| 参数 | 默认值 | 当前含义 |
| --- | --- | --- |
| `yolo_engine_path` | `drone_int8.engine` | 保留的 YOLO 模型路径字段。RK3576 使用其模型路径配置选择 `.rknn` 文件。 |
| `nanotrack_backbone_path` | `nanotrackv3_backbone_int8.engine` | 保留的 Backbone 模型路径字段。 |
| `nanotrack_head_path` | `nanotrack_head_int8.engine` | 保留的 Head 模型路径字段。 |
| `target_class_id` | `0` | 只检测这个精确类别。 |
| `yolo_conf_threshold` | `0.4` | 全图和切片的置信度门槛。 |
| `yolo_nms_threshold` | `0.4` | NMS 的重叠门槛。 |
| `yolo_recall_relax_ratio` | `1.0` | ROI 阈值乘数；默认不放宽。 |
| `yolo_interval` | `4` | 跟踪分支的请求间隔，单位为有效更新次数。 |
| `iou_reset_threshold` | `0.4` | 低于此值时尝试重锚定。 |
| `iou_calibrate_threshold` | `0.8` | 高于此值为 YOLO 确认，不表示覆盖跟踪框。 |
| `max_position_jump` | `150.0` | 中心跳变距离门槛，单位为像素。 |
| `max_size_jump_ratio` | `2.0` | 宽、高分别使用的跳变比例门槛。 |
| `max_box_area_ratio` | `0.8` | 框的宽、高分别占检测区域的最大比例。 |
| `lost_max_count` | `5` | 连续未确认次数上限。 |
| `roi_scale_factor` | `2.0` | 跟踪阶段 ROI 放大系数。 |
| `frame_buffer_size` | `30` | 补帧图像缓存上限，单位为帧。 |
| `jump_tolerance` | `3` | 跟踪跳变重置及 YOLO 跳变接受的计数门槛。 |
| `enable_dcf` | `true` | 启用灰度 DCF 中心；关闭后改用中心滤波器。 |
| `dcf_max_div_factor` | `0.5` | 中心偏差门槛相对目标平均边长的倍数。 |
| `kf_fx`, `kf_fy`, `kf_cx0`, `kf_cy0` | 均为 `0.0` | 焦距与主点，单位为输入图像像素；运行时需配置有效内参。 |
| `kf_r_pos`, `kf_r_size` | 均为 `4.0` | 位置和尺寸测量噪声方差。 |
| `kf_q_a` | `5e4` | 中心运动的加速度过程噪声参数。 |
| `kf_q_size` | `1e3` | 尺寸过程噪声参数。 |
| `kf_history_len` | `60` | 主管线卡尔曼历史长度。 |

`dcf` 子结构来自 `GrayscaleDCF::Params`：`model_sz=96`、`pad=1.0`、`sigma_factor=0.1`、`learning_rate=0.01`、`psr_thresh=5.0`、`lambda_reg=1e-4`、`fixed_template=true`、`eps=1e-5`。模板默认在初始化或重新锚定时学习；仅关闭固定模板时才在跟踪期间按学习率更新。

NanoTrack 固定常量来自 `nano_tracker.h`：`score_size=15`、`min_wh=5`、`template_size=127`、`instance_size=255`、`window_influence=0.455`、`track_lr=0.348`、`track_penalty_k=0.138`、`stride=16`。这些不是 `TrackerSettings` 的可调参数。

## 7. 源码与验证入口

- [hybrid_tracker.cpp](hybrid_tracker.cpp)：分支流程、补帧、跳变、仲裁与输出。
- [yolo_detector.cpp](yolo_detector.cpp)：请求替换、结果快照、级联检测与后处理。
- [nano_tracker.cpp](nano_tracker.cpp)、[dcf_tracker.cpp](dcf_tracker.cpp)：神经网络跟踪及中心融合。
- [rotation_kf.cpp](rotation_kf.cpp)、[target_los.h](target_los.h)：旋转补偿滤波与视线估计。
- [target_observation.h](../target_observation.h)、[drone_tracker_node.cpp](../drone_tracker_node.cpp)：成对消息构造、时间模式与发布。

算法测试位于 `tests/` 子目录，覆盖模型与预处理契约、恢复、诊断仲裁和视线行为。构建及硬件验证边界见仓库根目录 `AGENTS.md`；本文描述代码，不代表已完成真实模型、相机、HDMI、长时间负载或实飞验收。

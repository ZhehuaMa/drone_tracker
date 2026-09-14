# 两组纯精度实机输入核验

2026-09-11，通过 `ssh firefly` 在 RK3576 开发板执行任务 7.1。系统为 Linux aarch64、内核 6.1.141。查询报告确认 RKNN Runtime 2.3.2（429f97ae6b）和 RKNPU 驱动 0.9.8。本记录只证明模型属性与输入身份已固定，不证明完整跟踪或质量验收通过。

## 模型与工具

模型根目录为 `/home/firefly/benchmarks/nanotrackv3_evaluation_runs/nanotrackv3-official-20260910/models/components/`。FP16 使用 `fp16/`，INT8 使用 `int8/rknn_int8/`。两组均显式指定模板 Backbone、搜索 Backbone 和 Head；Backbone 提取图像特征，Head 比较模板与搜索特征。

实际属性见 [FP16 查询](evidence/query-fp16.log) 和 [INT8 查询](evidence/query-int8.log)。FP16 三模型的所有查询输入输出均为 FP16；INT8 三模型均为 INT8。FP16 携带 AFFINE 元数据，但数据类型仍为浮点，不能仅根据该元数据判断精度。布局、尺寸、量化零点与比例均保存在查询日志。

SHA-256 是文件内容指纹，用于确认复查时读取的是同一文件：

| 文件角色 | SHA-256 |
| --- | --- |
| FP16 模板 | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| FP16 搜索 | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| FP16 Head | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |
| INT8 模板 | `848ec40a659b5a3484c0d76ba6ded353250f524c7f384fec95a0f85cf2682bdf` |
| INT8 搜索 | `ecabaefdeadba721b466d942f76195bb9a5184e8e0a7d2a8262cc2d86e38ccdd` |
| INT8 Head | `f4fc6259eaeb83860515773df49391c860a3be31507f613b2c15fcde6430e736` |
| 共用 YOLO | `4d8a22bc8da9fbbc43f190b7ea90c27d9980f77e3e27064ec32cf6566c2680f3` |
| 共用录像 | `01c49506366d2b8f273df4951bc8546e2f0b5920203f815589950d90aa5b53d7` |
| 当前 NanoTrack 查询工具 | `893cb960e50f4d02fc398c20ec364052c0a54218047e6fb929293652af1bb41c` |
| 当前完整回放工具 | `7e787d84e6474a6a7d05f42f8eb1f3a8a35587e50da552d231ecc510b10605ef` |

当前两个工具来自 `.docker-artifacts/build/tracker-replay/`，已复制至板上独立目录 `/home/firefly/drone_tracker_test/tracker_replay_acceptance_20260911/`。首次误用历史 `nanotrack_acceptance_20260911/` 内旧工具，触发已知 FP16 AFFINE 拒绝。换用当前工具后两组查询均返回 0。旧工具失败日志保存在板上 `query-fp16-historical-tool-failure.log`，没有因此修改模型或放宽当前契约。

复查查询时，在该独立目录执行以下命令，并将 `MODEL_DIRECTORY` 替换为上文某组完整模型目录：

```sh
./rk3576_nanotrack_probe \
  --template-model MODEL_DIRECTORY/template_backbone.rknn \
  --search-model MODEL_DIRECTORY/search_backbone.rknn \
  --head-model MODEL_DIRECTORY/head.rknn --mode query
```

## 录像、配置与覆盖边界

原录像为 `/home/firefly/drone_tracker_test/yolo_acceptance_20260910/V_DRONE_051.mp4`，尺寸 640×512，共 309 帧。`ffprobe` 报告帧率 30/1；进一步检查全部 309 个解码时间戳，相邻间隔与 1/30 秒的最大误差小于 1 微秒，未仅凭平均帧率判断。见 [时间检查摘要](evidence/input-timing.txt)。同名标注在 `/home/firefly/Desktop/drone_yolo_dataset/labels/` 各子目录共 309 份；本次尚未读取标注计算跟踪误差。

两份完整配置为 [FP16 配置](evidence/replay-fp16-dcf1-001.yaml) 和 [INT8 配置](evidence/replay-int8-dcf1-001.yaml)，也已复制到板上独立目录。它们使用相同 YOLO、录像、目标类别 2、相机参数和全部算法设置，仅 NanoTrack 三路径及独立输出目录不同。当前均启用 DCF，即灰度相关滤波辅助跟踪；任务 7.2/7.3 仍须分别覆盖关闭 DCF 和三次重复。

配置焦距 800 像素沿用示例，主点为图像中心 (320,256)。这些是单位旋转离线回归的明确诊断参数，不是原相机标定。真实相机、旋转补偿及部署验收不能使用该记录作为标定证据。

已搜索板上 `drone_tracker_test`、`Desktop`、`Videos`、`Downloads` 和 `benchmarks` 的常见录像文件类型，仅找到上述已知回归录像。它可以作为持续目标回归候选；自动捕获结果仍须真实完整回放确认。没有取得已确认的“目标离开后返回”或“无目标”录像，不能认定这些场景已覆盖，也未通过拼接、擦除目标或使用人工首框替代。

任务 7.1 允许明确记录缺失场景，因此此项完成。任务 7.2 要求所需场景实际通过，目前无法满足。按照 apply 技能遇阻暂停的要求，在 7.2 前等待补充录像路径。7.2–7.6 均未勾选，尚未启动完整回放、质量比较、30 分钟负载或故障退出实验。

## 补充录像检查

用户随后提供 `/home/firefly/2025-08-15_airsim_tracking_test.mp4`，指出 20–30 秒有目标短暂离开并重新出现。文件为 153102281 字节，960×968 像素，声明 1743 帧、59.0714 秒。`ffprobe` 的名义帧率为 179/6，平均帧率为 8715000/295357。实际取到 1742 个帧时间戳，相邻间隔范围为 0.0333–0.0514 秒，明显不是固定间隔。原始时间戳保存在 `.docker-artifacts/log/airsim-input-timestamps.txt`。声明帧数与可取时间戳数存在差异，尚未确认原因，不能把该文件声明为已通过完整解码检查。

当前回放仅接受固定帧率素材，不能直接把平均帧率写入配置冒充固定帧率。此录像的离开返回场景由用户指出，尚未完成画面检查与实际回放。若制作固定帧率派生录像，须明确记录转换方法、原始与派生文件身份，以及帧重复、丢弃或时间轴调整情况，再进行验收。

板上 `data.yaml` 确认类别映射为 AIRPLANE=0、BIRD=1、DRONE=2、HELICOPTER=3。对于目标类别固定为 2 的回放，仅包含其他三类且不含无人机的视频可以用作“无目标类别”负样本。必须保持目标编号为 2，并记录这是其他类别干扰场景。用户尚无此类可用录像，具体输入待确定。

## 经用户授权补齐素材

用户随后要求下载 BIRD 原始录像，并制作 AirSim 的固定 30 FPS 副本。以下工作已完成，原文件均保留：

| 输入 | 板上绝对路径 | 实际检查 |
| --- | --- | --- |
| BIRD 原片 | `/home/firefly/drone_tracker_test/tracker_replay_acceptance_20260911/V_BIRD_001.mp4` | 640×512，30 FPS，305/305 帧完整解码，10.166667 秒 |
| AirSim 副本 | `/home/firefly/2025-08-15_airsim_tracking_test_30fps.mp4` | 960×968，30 FPS，1772/1772 帧完整解码，59.066667 秒 |

BIRD 来自 [原始数据集固定版本的视频文件](https://github.com/DroneDetectionThesis/Drone-detection-dataset/blob/442d0c6750708cc221636f7a1fd31cbd8cb0aeb8/Data/Video_V/V_BIRD_001.mp4)，直接下载原始 MP4，没有从 JPEG 拼接。数据集版本为 `442d0c6750708cc221636f7a1fd31cbd8cb0aeb8`。板上同名 305 份标注共 305 个框，类别全部为 1（BIRD）。缩略图显示田野上空的鸟类场景。它用作其他类别干扰负样本，不代表所有无目标环境。

| 文件 | SHA-256 |
| --- | --- |
| BIRD 原片 | `062059c958360e552fa475f73ad32431441abc3970939e6a12f464a76d1095bf` |
| AirSim 原片 | `829864c29f56cf19934b69bc73c0ef52e461c08d77a832161895cdf8cbba8b7f` |
| AirSim 30 FPS 副本 | `95725ccfc3e926dd700e629f5a7d0f6cd34d9b5c9e0810cc7027a3b5888218af` |

转换命令如下。`fps=30` 根据原始时间戳重采样，保留近似原时长；它会重复或丢弃帧，不是运动插帧。`-an` 仅输出视频轨，`-n` 拒绝覆盖已有文件。

```sh
ffmpeg -nostdin -n -hide_banner -loglevel verbose \
  -i /home/firefly/2025-08-15_airsim_tracking_test.mp4 \
  -map 0:v:0 -an -vf fps=30 -c:v libx264 -preset fast -crf 18 \
  -pix_fmt yuv420p -movflags +faststart \
  /home/firefly/2025-08-15_airsim_tracking_test_30fps.mp4
```

转换器读取 1742 个解码帧，丢弃 1 帧并重复 31 帧，得到 1772 帧。见 [转换日志](evidence/airsim-convert-30fps.log)。原容器有 1743 个包，不能把包数当作实际解码帧数；派生副本的声明帧数和实际解码帧数均为 1772。尺寸不变，H.264 重新编码可能改变像素，因此原片与副本的文件身份分别保存。

两段新素材的全部帧时间戳均已检查，相邻间隔与 1/30 秒的最大差约 0.667 微秒，来自输出小数舍入。见 [逐帧时间核验](evidence/supplemental-timing.json)。AirSim 每 5 秒缩略图可见目标离开并重新出现；具体状态转换仍以回放结果为准。

新配置见 [BIRD](evidence/replay-fp16-bird-dcf1-001.yaml) 和 [AirSim](evidence/replay-fp16-airsim-dcf1-001.yaml)。AirSim 主点按其尺寸设为 (480,484)，焦距仍为明确的离线诊断值 800 像素，不宣称真实标定。两类素材已补齐，此前缺少输入的阻塞解除，继续任务 7.2。

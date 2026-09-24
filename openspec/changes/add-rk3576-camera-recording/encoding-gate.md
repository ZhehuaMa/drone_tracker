# 独立编码集成门槛复核

日期：2026-09-23。对象是 `firefly-production` 对应的 HZ-RK3576_WuKong Board，主机名 `hzhy`。本次仅运行独立编码探测器，没有启用模型、ROS 跟踪或 HDMI OSD，也未修改正式部署。

## 结论与修复

任务 1.4 的固定编码配置通过独立复核：1920×1200 NV12、60 fps、12 Mbps、四个原始共享槽。优化后一分钟及五分钟样本均无录像主动丢帧，全部视频帧通过 FFmpeg 完整解码。这不代表后续 TS 文件封装、持久化、故障隔离或跟踪并发验收通过。

原探测器直接逐字节扫描 MPP 返回的编码包，以判断 IDR。IDR 是可以独立开始解码的关键帧。板端该编码包位于不可缓存的 DMA 内存，即硬件和 CPU 共享但不经过普通 CPU 缓存的映射。大包扫描形成约 100 ms 的停顿，超过四槽能吸收的时间。

修复先批量复制编码包到普通内存，再扫描并把同一份数据移交写盘队列。没有增加原始槽、改变输入像素或放宽帧率。新增有界耗时样本，运行结束后汇总各阶段，采集期间不逐帧打印。

| 检查 | 优化前一分钟 | 优化后一分钟 | 优化后五分钟 |
|---|---:|---:|---:|
| 编码帧数 | 3600 | 3600 | 18060 |
| 录像槽满丢帧 | 97 | 0 | 0 |
| 驱动序号缺口（整轮） | 1 | 0 | 1 |
| 录像无效输入 | 0 | 0 | 0 |
| 合格录像帧时间跨度内 fps | 58.444 | 60.0192 | 60.0192 |
| 扫描/入写盘队列最大耗时 | 100.425 ms | 9.414 ms | 11.074 ms |
| 硬件提交/取包最大耗时 | 8.856 ms | 12.828 ms | 12.833 ms |
| 峰值 RSS | 36828 KiB | 36844 KiB | 37616 KiB |

驱动缺口独立保留，不计入录像槽满丢帧，也不宣称原始驱动零缺帧。两次优化样本预热均丢弃 10 帧；这些帧未录制。五分钟样本的第一合格序号为 11。日志没有逐帧保存驱动缺口发生位置，因此不把该缺口确定归因于预热。

五分钟样本为 436828224 字节、18060 帧，原尺寸、Constrained Baseline、无 B 帧、全范围。采集时间跨度约 300.887 秒；日志中的 301.298 秒总耗时包含预热和收尾。完整解码无错误，视频不作为普通源码提交。证据见 [日志](evidence/encoding-gate/camera-five-min.log)、[流检查](evidence/encoding-gate/camera-five-min-stream.json) 和 [解码日志](evidence/encoding-gate/camera-five-min-decode.log)。

## 内存边界

显式原始共享槽为 4×3456000＝13824000 字节，另加固定槽头。编码输出队列包含正在写入的数据，五分钟峰值为 841028 字节，上限 8 MiB。MPP 输入缓冲申请 3456000 字节，页对齐实际占用 3457024 字节。

板端库报告 MPP 提交 `520ab553`。核对该提交的 [H.264 硬件缓冲实现](https://github.com/rockchip-linux/mpp/blob/520ab553/mpp/hal/rkenc/h264e/hal_h264e_vepu510.c) 及 [输出缓冲实现](https://github.com/rockchip-linux/mpp/blob/520ab553/mpp/codec/mpp_enc_impl.c)，本次固定尺寸、默认参考模式及串行一帧在途配置的 DMA 缓冲为：

- 四组重建参考缓冲，每组 3543040 字节图像、147456 字节缩略数据和页对齐 4096 字节辅助数据，共 14778368 字节。
- 一个页对齐 2306048 字节编码输出缓冲。
- 一个上述 3457024 字节输入缓冲。

总计 14 个编码相关 DMA 对象、20541440 字节；内部参考及输出缓冲合计 17084416 字节。开始、中段、后段快照均与此相符。该边界限于本次固定配置，不是任意尺寸或参考帧配置的通用上限，也不是 MPP 所有 CPU 堆内存上限。RSS 单独报告，不与 DMA 重复相加。

采样 RSS 为 36492、36756、37284 KiB，结束峰值 37616 KiB。其中探测器预留的六组耗时样本逐步写入，18060 帧最多使用 866880 字节；该诊断内存不属于正式录像队列。结束后全系统 DMA 对象回到 5 个、11149312 字节，相机无占用，MPP 会话消失。原始快照见本目录 `evidence/encoding-gate/dma-five-min-*.txt` 和 [退出检查](evidence/encoding-gate/post-run.txt)。

## 可复现身份与限制

基线提交为 `aa97b7f7dea4d888fca48e95c236a79c33e061dc` 加当前未提交变更。探测器源码 SHA-256 为 `d9bcee73c39b73992f96160ac56abdba6ce3220c0fe955dedbcc12809c7a8a60`；板端程序及 Runtime 指纹见退出检查，样本和所核对官方源码指纹见 [sha256.txt](evidence/encoding-gate/sha256.txt)。

板端工作目录为 `/home/hzhy/drone-recording-validation-20260923/diagnostic`，构建使用 GCC 11.4.0、MPP 1.3.9 pkg-config 和 libavformat 58.76.100。采集前复核设备列表、`/dev/media2` 拓扑及 `/dev/video22` 格式。命令为：

```bash
source /opt/ros/humble/setup.bash
timeout 330 build/rk3576_recording_probe camera-five-min.h264 60 12000000 18060 \
  /dev/video22 /dev/media2 'm00_b_sc233hgs 0-0030'
```

本地样本保存在 `.docker-artifacts/recording-validation/diagnostic/`。一次误省略相机参数的合成试跑已停止并保留为 `interrupted-synthetic.h264`，未纳入任何相机性能结果。真实相机复测在它退出后单独进行。

系统 RKNN Runtime 指纹未改变。未执行静态检查、Jetson 检查、功耗或真实掉电实验。仅改独立探测器，既有采集回归证据不冒充本轮重跑。三类现场画质验收已按用户要求移出范围，未声称其质量通过。

# 生产板录像依赖基线

核验日期：2026-09-23。对象为 SSH 别名 `firefly-production` 对应的生产板，主机名 `hzhy`。本次查询设备、软件包及文件指纹，没有开流、切换正式版本或替换系统库。原始结果见 [board-baseline.txt](evidence/board-baseline.txt)。

| 项目 | 实际结果 |
|---|---|
| 板卡 | HZ-RK3576_WuKong Board |
| 内核 | 6.1.141-rt36，ARM64 |
| 相机 | SC233HGS，实体 `m00_b_sc233hgs 0-0030`；ISP 输出 `/dev/video22`，媒体设备 `/dev/media2` |
| 图像格式 | 1920×1200 NV12，单内存平面，行跨度 1920 字节，总大小 3456000 字节 |
| 色彩标记 | Colorspace/Transfer/YCbCr 为 Default，Quantization 为 Full Range；默认标记不能作为已确认的色彩矩阵 |
| 帧率 | 正式配置为 60；传感器拓扑为 60 fps；采集节点不支持 `VIDIOC_G_PARM`。本次未以实际出队间隔重新测量帧率 |
| MPP 包 | `librockchip-mpp-dev`、`librockchip-mpp1` 均为 `1.5.0-1` |
| MPP pkg-config | `rockchip_mpp` 返回 `1.3.9`，与 Debian 包版本不同；不据此认定调用接口不兼容 |
| MPP 头文件 | `/usr/include/rockchip/`；全部头文件 SHA-256 记录在原始结果中 |
| MPP 库 | `/usr/lib/aarch64-linux-gnu/librockchip_mpp.so.1`；SHA-256 为 `f03882324fddb343ff714e77b1fe37587ab70a42d3e40057327e5fa9afa46261` |
| 编码设备 | `/dev/mpp_service`，root:video，权限 0660 |
| 封装库 | `libavformat-dev` 包 `7:4.4.2-0ubuntu0.22.04.1`，pkg-config 版本 `58.76.100` |
| 持久挂载 | `/var/lib` 位于 `/dev/mmcblk0p7` 的 ext4 根分区；检查时约 47 GiB 可用，运行时须重新核验 |
| RKNN | 系统 Runtime 2.3.2 基线；`/usr/lib/librknnrt.so` 指纹为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`，与已验收版本一致 |
| RKNPU | 本次读取驱动版本为 0.9.8 |

MPP 是硬件编码接口。头文件和共享库同时存在，只说明可以开展编译验证，尚不证明接口兼容或编码吞吐达标。SHA-256 是文件内容指纹，用于确认后续验收使用同一组依赖。

相机预热前置变更已归档，其 [核验报告](../archive/2026-09-23-fix-sc233hgs-startup-timestamp-warmup/verification.md) 记录独立相机生产板验收。该证据不代替新增录像验证。

尚未验证：MPP 实际编码参数及读回、1920×1200 全帧率吞吐、输入对齐、色彩标记传递、编码内存、录像同步与恢复、跟踪/OSD 并发性能和三类场景画质。本次未执行断电或其他破坏性测试，不承诺真实掉电损失上限。

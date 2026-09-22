# RK3576 开发板 2 实机验收记录

## 运行身份

- 板卡：开发板 2，`firefly`，`rockchip,rk3576-firefly-roc-rk3576-pc-ext`
- 内核：`6.1.141-rt52`
- RKNN Runtime：`2.3.2`，`429f97ae6b@2025-04-09T09:09:27`
- RKNPU 驱动：`0.9.8`
- 探针 SHA-256：`5f39a8dae31d34e52095644aa3b3a9046a2104585145bbd008988e54d8fdf72d`
- 模板 Backbone SHA-256：`622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2`
- 搜索 Backbone SHA-256：`9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44`
- Head SHA-256：`2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889`
- 输入录像 SHA-256：`bc0f21ed1cbdf0ab8b7399d295fbea8bbb0587c9ec74bf192067f8efdf67cc3b`

## 结果

当前源码构建的 `rk3576_nanotrack_probe` 已改为通过完整的 `RknnApi` 和
`RknnSharedMemoryApi` 装配生产 `RknnNanoTrackBackend`，并复用历史基线使用的
`MjpegVideoReader` 解码器。在上述板卡上使用历史完全相同的三模型和 `input.mov`
完成 301 帧回放。回放成功产生逐帧框和分数，证明生产直连路径可在 RKNN Runtime
2.3.2 上运行。

将 300 个跟踪帧与历史 `search-direct-observed-2` 的第 1 次测量逐帧对齐后，框
分量最大绝对误差为 `4.999999987376214e-7` 像素，分数最大绝对误差为
`4.95880114748104e-10`，满足 `1e-4` 像素和 `1e-6` 分数阈值。此前较大的差异
由误用不同模型、使用不同视频解码器以及探针默认只输出 6 位有效数字共同造成，
不是共享内存直连计算结果的差异。

本记录只覆盖开发板 2、上述模型和上述录像。没有覆盖其他 RK3576 硬件批次，
也不将本次单板结果泛化为全部 RK3576 设备。

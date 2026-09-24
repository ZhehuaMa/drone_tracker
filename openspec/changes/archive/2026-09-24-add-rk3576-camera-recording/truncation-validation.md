# 非断电截断副本验证

本记录验证任务 7.1。输入来自指定 `HZ-RK3576_WuKong Board` 生产板的 SC233HGS `/dev/video22`，原尺寸 1920×1200、约 60 帧/秒，H.264 12 Mbps，测试分段长度 10 秒。该轮没有加载跟踪模型，也未启用 OSD。它不代替默认 60 秒分段及联合性能验收。

原始会话为 `session-7a39d81bbeb1657ba069cee589cba89c`，共 1,484 帧，三个 MPEG-TS 分段。原件保留于板端 `/home/hzhy/drone-recording-validation-20260923/sessions/` 和本地 `.docker-artifacts/recording-validation/diagnostic/first-ts-session/`。

## 方法与结果

选择帧结束、同步记录和分段结束附近的 194 个不同偏移。其中 116 个归类为帧或分段边界，78 个归类为同步提交边界。每个偏移只截断临时副本；原始文件测试前后的 SHA-256 完全一致。清单及原件身份见 [逐项结果](evidence/truncation/results.jsonl) 和 [原件哈希](evidence/truncation/source-sha256.json)。

使用 FFmpeg 软件解码，每次只检查一个副本，不生成转换后视频。严格检查增加 `-err_detect explode -fflags +discardcorrupt`，拒绝将损坏尾帧的容错画面当成完整解码结果。`framecrc` 输出每个解码帧的时间和校验值；结果中的 `timebase` 为时间单位，`last_decoded_frame` 的第三列是最后解码帧 PTS。例如时间单位为 `1/90000` 时，PTS 除以 90,000 得到秒数。无完整帧时记录 `null`。

- 188 个副本具有可解码前缀，逐项保存最后完整解码时间。
- 6 个副本都在本段首个完整 IDR 帧之前截断，没有可解码帧。IDR 是不依赖前面图像的独立解码起点；不完整的首个 IDR 不能构成可恢复视频。这 6 项如实报告为空，不能声称从任意少量字节恢复。
- 普通容错解码曾对全部 194 个副本输出画面，其中 128 项有诊断消息。最终结果采用严格检查，不以普通模式的退出码零掩盖损坏尾部。

三个完整原始分段分别通过完整解码检查。各段首帧可独立解码，截断副本不需要其他段作为输入。

## 复现与证据边界

验证脚本副本见 [check_truncated_prefixes.py](evidence/truncation/check_truncated_prefixes.py)。其运行时位置是本地 `.docker-artifacts/recording-validation/diagnostic/`，使用 conda base 的 Python，命令为 `python check_truncated_prefixes.py --strict-decode`。脚本默认读取同目录的 `first-ts-session/`；严格报告写入 `truncation-strict-evidence/`，报告排他创建以防覆盖。完整逐项 FFmpeg 诊断保留于该目录。

这是故障副本检查脚本，不是产品离线转换工具，不修改生产回放入口。该测试保持开发板供电，未断电、重启或填满磁盘。它证明上述已写入文件的完整解码前缀，不能证明真实掉电时磁盘控制器已经保全哪些字节，也不能承诺硬件损坏后的恢复。

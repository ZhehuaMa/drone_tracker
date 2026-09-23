# RKNN 与 HDMI 隔离诊断（2026-09-23）

## 对象与方法

本次对象是 `firefly-production` 对应的生产 `HZ-RK3576_WuKong Board`，主机名 `hzhy`，内核 `6.1.141-rt36`。没有使用开发板 1、2 的结果替代。任务是定位故障，不是完成验收。

沿用 `validation.md` 中的正式节点、FP16 NanoTrack 模型、YOLO、`synthetic.yaml` 和 640×480、20 FPS、660 秒合成视频。关闭 OSD 时仅覆盖 `osd.enabled=false`。系统 RKNN Runtime 仍是 2.3.2，驱动仍是 0.9.8，系统库指纹未变。模型与配置指纹见原验证记录及本次 `identities.txt`。

各轮顺序执行。开始运行前完成编译，没有同时运行其他验收进程。HDMI 长探针不加载模型，使用自带的 1920×1200 灰度和彩色输入。探针新增可选的每次打开持续秒数和打开次数参数；独占长测命令为 `osd_hdmi_probe 610 1`。遇到真实错误仍立即退出，没有重试或忽略错误。

诊断用 `LD_PRELOAD` 加载独立审计库。这种方式在接口调用外增加日志，不改变接口参数、返回值或等待时限。显示侧立即保存返回值和 `errno`，并统计真正执行的翻页完成回调，而非仅统计读取事件的次数。RKNN 侧记录上下文编号、调用序号和单调时钟耗时。上下文编号标识一个已加载模型，不是 NPU 内部任务编号。

板端证据目录：`/home/hzhy/drone-tracker-isolation-20260923/`。本机副本：`.docker-artifacts/hdmi-isolation-20260923/`。这些目录包含原始日志和诊断工具，不包含新增模型。

## RKNN：关闭 OSD 仍能触发

三轮有效的关闭 OSD 测试都在首次 Search Backbone 等待时退出。其中最后一轮启用行缓冲，避免进程紧急退出时丢失 Runtime 的日志：

```text
RKNN_RUN ctx=367955136768 call=474 rc=0 us=2803
W RKNN: rknn_wait fence fd = -1 is invalid
RKNN_WAIT ctx=367955136768 call=1 rc=-1 errno=110 us=71
Fatal model device failure [.../fp16/search_backbone.rknn]: RKNN wait failed; device access cannot be excluded
```

前两轮等待耗时分别为 34、68 微秒，原始返回码同为 `-1`。首次测试前后未出现新增 NPU 内核错误。`errno=110` 是调用后现场值，RKNN 接口以自己的返回码为契约；不能仅凭这个可能沿用先前值的 `errno` 宣称发生了设备超时。

定位到的调用链是：`rknn_nanotrack_backend.cpp` 的共享特征路径先调用 `search_session_->run()`，再调用 `confirmCompleted()`；后者进入 `rknn_session.cpp::confirmStopped()`，执行 `rknn_wait(context_, nullptr)`。初始化没有启用异步标志，`run()` 也没有传入非阻塞扩展参数。

`fence fd` 是用来等待设备完成的文件描述符，`-1` 表示这里没有有效凭据。固定系统库的 `rknn_wait` 反汇编和对应诊断字符串也表明：凭据无效时返回 `-1`。因此，本次失败的直接原因已定位为等待调用与当前执行路径不匹配，并触发了项目的保守退出保护。它不依赖 OSD，不应通过放宽超时解决。

修复前仍须明确同步 `rknn_run` 的完成保证及失败路径的资源所有权，再调整完成确认逻辑和对应测试。不能简单忽略 `rknn_wait` 失败，也不能删除所有失败路径的安全检查。本次没有修改模型后端。

## DRM：不加载模型仍能触发

桌面开启时，四次独立显示诊断均在 610 秒目标之前失败：

| 轮次 | 成功提交数 | 完成回调数 | 失败现场 |
|---|---:|---:|---|
| 基础审计 | 382 | 382 | `rc=-28 errno=28 flags=0x201` |
| 临时内核 DRM 日志 | 260 | 260 | 同上；退出恢复另有 `rc=-22 errno=22` |
| 系统调用跟踪 | 757 | 757 | 同上；`DRM_IOCTL_MODE_ATOMIC = -1 ENOSPC` |
| 完整同步最终后端 | 13 | 13 | `rc=-28 errno=28 flags=0x201` |

`flags=0x201` 表示非阻塞原子提交并请求翻页完成事件。系统调用跟踪确认错误来自内核调用，不是上层拼接错误名称造成的假象。失败前提交与完成回调数相等，没有观察到应用遗漏消费完成回调。这个结果不能排除内核内部资源问题，也不能证明显存或带宽不足。

前三轮使用的旧独立探针后端与正式节点仅有奇数灰度尺寸处理和读视图校验差异，原子提交与等待代码一致。本次输入是偶数尺寸。第四轮完整同步正式节点的最终后端后仍复现。该后端 SHA-256 为 `64260f6570bdbbe6ba5174d7746902a717c262966da70cc6414991c26f7e57bb`，与本地源码和正式节点源码副本一致。

还发现显示隔离不完整：`/sys/kernel/debug/dri/0/clients` 显示 Xorg 仍是 DRM master（显示控制权持有者），探针也打开同一设备。内核日志中出现桌面帧缓冲的插入提交。恢复阶段还出现 `1280×720` 源矩形与 `720×480` 帧缓冲不匹配，随后后端禁用其显示层。启动时的 `drmIsMaster()` 检查没有阻止此次并存。

### 停止桌面后的独占对照

用户明确允许临时停止桌面并在结束后恢复。执行 `systemctl stop lightdm` 后，DRM 客户端列表为空；探针启动后，列表仅有探针，且其 `master=y`。构建已经结束，没有模型或其他验收进程参与。

相同最终后端于 14:05:25 开始，14:15:36 结束。设置的连续运行时间为 610 秒，另有启动和收尾时间，结果如下：

```text
reopen=0 submitted=5981 replaced=0 displayed=5981 refreshes=8984
exit=0
```

即成功显示 5981 个新输入包，完成 8984 次画面提交，没有 ENOSPC 或等待失败。`refreshes` 是画面提交完成次数，不是物理屏幕扫描次数。运行后段采样的 RSS 为 26776 KiB，线程数为 2；这些少量采样不代替正式资源趋势验收。证据为 `probe-exclusive.log`、`probe-exclusive-exit.txt`、`exclusive-start.txt`、`exclusive-end.txt` 和 `exclusive-resource-samples.txt`。

### 控制权检查的具体缺陷

桌面恢复后，新打开一个 DRM 文件描述符，仅查询控制权，不提交显示：

```text
drmIsMaster 1 errno 0
drmAuthMagic(0) 0 errno 0
Xorg     master=y
python3  master=n
```

因此，生产板环境中 `drmIsMaster()` 会把实际非 master 的文件描述符判断为 master。当前 `HdmiBackend::initialize()` 只依赖这一返回值，未能拒绝已被 Xorg 占用的显示设备。此次通过 SSH 执行的用户是 root；不把这一结果推广为所有内核或用户权限下的行为。

[libdrm 官方源码](https://cgit.freedesktop.org/drm/libdrm/tree/xf86drm.c) 中，`drmIsMaster()` 通过 `drmAuthMagic(fd, 0) != -EACCES` 推断控制权。本板实测该调用返回 0，解释了为什么检查会通过。确切的板端内核权限处理实现未提供完整源码，不进一步臆测它为何允许该调用。

结论：已定位到**显示独占条件没有得到保证，且初始化控制权检查发生误判**。桌面开启四轮失败、停止桌面后同版本连续 610 秒成功，为该问题提供了对照证据。修复方向是可靠取得或验证独占控制权，无法取得时按规格明确拒绝启动；产品节点不自行停止桌面。ENOSPC 在内核中对应哪一项具体资源仍未定位，不能将其写成带宽不足或事件队列泄漏，也不应先增加 ENOSPC 重试来掩盖竞争。

## 验收边界与环境收尾

- RKNN 单独运行仍失败；独占 HDMI 单独连续 610 秒成功。两条链路尚未分别稳定，未进入联合稳定性测试，也未重跑健康显示的“同进程 20 轮启停＋额外 10 分钟”。
- 桌面开启的四轮 HDMI 长测提前失败，不能记为已完成 10 分钟。独占探针通过也不代替带真实模型的持续负载验收。实际屏幕像素观察仍按用户原决定单独安排。
- 未升级 SDK、修改模型、放宽超时或忽略错误。临时 DRM 日志参数已恢复为原值 `0`。
- 最高 RKNN 日志级别会额外产生张量转储和大量日志，发现后已主动停止该诊断轮次，不将其作为有效性能对照。最终直接证据来自仅错误级别日志与行缓冲。
- 旧恢复工具引用的帧缓冲已失效，未把它的执行当作恢复成功。随后通过现有 Xorg 的 `xrandr` 重新启用 HDMI，核对内核实际输出恢复为 `1280×720p60`。
- 独占测试结束后退出处理自动启动 `lightdm`。已复核服务为 active，Xorg 为唯一 DRM 客户端，实际输出为 `1280×720p60`。没有遗留测试进程。
- 最终探针已在生产板构建；`git diff --check` 和 `openspec validate integrate-rk3576-hdmi-osd --strict` 通过。本轮没有运行整个项目回归；未改产品推理或显示后端逻辑。板端未配置可用的 clang-tidy/clangd 检查。
- 当前任务进度保持 **28/32**，任务 4.6、6.2、6.3、6.4 不勾选，不同步或归档。

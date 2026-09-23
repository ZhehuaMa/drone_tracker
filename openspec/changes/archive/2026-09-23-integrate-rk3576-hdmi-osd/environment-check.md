# 生产板显示环境检查

检查日期：2026-09-23。对象为 SSH 别名 `firefly-production` 指向的生产板，主机名 `hzhy`。本记录只覆盖只读能力查询，不代表本项目 HDMI 输出验收通过。

## 已确认的前提

- 设备树型号：`HZ-RK3576_WuKong Board`；架构 ARM64，内核 `6.1.141-rt36`。
- `/dev/dri/card0` 的 `HDMI-A-1` 已连接。连接器当前编号 177，显示控制器编号 75；这些编号只用于本次记录，实现不能硬编码。
- 模式包含逐行 720×480，实际刷新率包括 60.00 Hz 和 59.94 Hz。当前画面为 1280×720，检查没有改变模式。
- `pkg-config` 报告 libdrm 2.4.124、librga 2.1.0；编译参数为 `-I/usr/include/libdrm -I/usr/include/rga -ldrm -lrga -pthread`。`/usr/include/rga/im2d.hpp` 存在。这里只记录包元数据版本，未据此推断厂商 SDK 的其他版本标识。
- 对 card0 新打开的连接，`drmIsMaster` 返回 1，表示本次连接拥有显示控制权。`drmSetClientCap` 的 universal planes 与 atomic 均返回 0，表示可启用完整层枚举和原子接口。
- 层 58、103、137、154 的 possible CRTCs 掩码为 1，可关联本次显示控制器。它们包含 `AR24`（ARGB8888，带透明度的像素格式）、`XR24` 及透明度混合属性。
- 桌面显示服务 `display-manager` 处于 active，但本次探测可取得控制权。服务是否运行不能单独用于判断设备是否被占用；程序仍须在初始化时检查实际控制权。

## 可重复命令与证据

非交互 SSH 命令先加载 `/opt/ros/humble/setup.bash`。主要查询：

```sh
uname -a
cat /proc/device-tree/model
pkg-config --modversion libdrm librga
pkg-config --cflags --libs libdrm librga
modetest -M rockchip -a -c -p
```

完整枚举输出保存在本机 `.docker-artifacts/log/hdmi-osd-preflight-20260923/modetest.txt`。另用 Python ctypes 加载 `libdrm.so.2`，仅打开 card0、查询控制权并设置本连接的能力标志，随后关闭文件描述符；未提交显示内容。

## 验证边界

接口及透明格式的枚举前提满足。尚未执行本后端双层原子提交、透明混合像素核对、显示完成等待及资源释放测试；能枚举不等于任意层组合都能通过驱动检查。没有改变桌面服务、显示模式、相机或系统 Runtime。任务 4.6 以及生产负载验收仍须单独完成。

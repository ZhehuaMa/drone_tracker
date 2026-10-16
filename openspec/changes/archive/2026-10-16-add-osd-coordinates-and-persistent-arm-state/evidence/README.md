# 软件绘制证据

本目录 `coordinates-*.png` 是本项目真实 `OsdLayout` 绘制器的 CPU 输出，不是生产板截图。`reference/` 中的图是生成示意图，不能代替本目录或实机证据。

生成时间为 2026-10-16（Asia/Shanghai）。绘制时刻固定为 Unix 秒 `1760000000`，时区 `UTC`，因此时钟为 `08:53:20.0`。输出分别为 720×480 与 1280×720，含透明背景。输入由 `src/osd/tests/hud_style_test.cpp` 定义：真实字库、`MAH 00410`、最长坐标 `(-90,-180)`、75 秒解锁计时、任务覆盖、启动提示、冻结底图状态及 FAILSAFE 告警。四种快照分别为已解锁、未解锁、遥测未知及已解锁且 `TRIG OFF`。

复现命令（从仓库根目录执行）：

```sh
bash openspec/changes/add-osd-coordinates-and-persistent-arm-state/evidence/generate-software.sh /tmp/osd-software-reproduction
```

独立基线使用实施前 `HEAD` 的 `src/osd`、`src/frame` 和 `src/diagnostics`，只复制当前 `hud_style_test.cpp` 作为相同输入的生成器。基线与实施版本均设置 `TZ=UTC`。`software-diff.json` 保存每幅图与基线的变化像素数及外接矩形。`diff-coordinates-*.png` 为逐通道绝对差。全部差异位于设计允许的坐标、底部解锁及迁移触发区域；允许区域之外变化像素为零。未把不同区域之间的空白计为新绘制内容。

`native.log` 记录独立 x86_64 Docker 环境的 9/9 CTest 通过。该镜像未提供 `clang-tidy` 或 `clangd`；未运行这两种静态检查。首次直接在主机执行时，CMake 选到缺少 PyYAML 的 Conda Python，YAML 测试失败；改按既定 Docker 入口、显式使用镜像 `/usr/bin/python3` 后全部通过，没有改动产品行为或测试断言。

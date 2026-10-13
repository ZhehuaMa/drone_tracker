# RK3576 NanoTrack 局部窗口预处理归档记录

归档日期：2026-10-13（开发机日期）。变更名为 `optimize-rk3576-nanotrack-window-preprocessing`，使用 `spec-driven` 工作流。本记录补充规格同步和归档结果，不覆盖历史实施报告或独立验收报告。

用户已明确选择“同步规格并归档”，并接受 W1 作为已知限制。本轮按该选择执行。W1 未调查、未解决，硬件标准没有修改。归档只表示本变更退出活动变更清单，不表示正式部署完成或全部检查通过。

## 规格同步

增量规格是本变更新增和调整行为的文档。主规格是合并这些行为后供后续工作遵循的长期约定。本轮仅使用 `openspec status --json` 返回的 `artifactPaths.specs.existingOutputPaths`，其中只有 `specs/model-input-preprocessing/spec.md`。

该增量规格已合并到 [model-input-preprocessing 主规格](../../../specs/model-input-preprocessing/spec.md)。新增 8 个要求、23 个场景；修改、删除和重命名要求均为 0。主规格原有标题、Purpose、要求和场景完整保留。新增内容位于同一个 `## Requirements` 下，没有保留增量操作标题。

全部 8 个要求的描述和 23 个场景逐块核对一致，重复同步不会产生新内容。检查结果见 [同步核对](evidence/archive-20261013/sync-check.json)，改动内容见 [主规格差异](evidence/archive-20261013/main-spec.diff)。

## 完成状态与验证

- 规划工件 `proposal`、`specs`、`design`、`tasks` 均为 `done`。
- CLI 唯一匹配本变更，任务为 23/23；独立读取任务文件确认 23 个复选框全部为 `x`。
- 已有[独立验收报告](verification-report.md)核对要求 8/8、场景 23/23、设计决策 6/6，未发现 CRITICAL（归档前必须修复的问题）。W2 性能失败验证缺口已关闭。
- `openspec validate --specs --json`：21/21 主规格通过，见 [主规格验证结果](evidence/archive-20261013/specs-validation.json)。已有多份主规格存在描述超过 500 字符的警告；本次主规格中的警告来自原有 YOLO 要求，本次新增要求未产生这类警告。没有重写无关要求以消除警告。
- `openspec validate optimize-rk3576-nanotrack-window-preprocessing --strict --json`：通过，无问题，见 [变更严格验证结果](evidence/archive-20261013/change-strict-validation.json)。本记录没有将常规主规格验证称为严格验证。
- `git diff --check`：通过，见 [空白检查结果](evidence/archive-20261013/diff-check.txt)。

历史指定重建输入的数值、恢复工作量和性能门槛通过，结论仍归于原实测板与历史代码身份。具体统计、模型与输入身份见 [validation.md](validation.md) 和 [verification-report.md](verification-report.md)。本轮没有重新执行构建、板端测试或性能测量。

## 归档后的独立复算入口

[validation.md 的独立复算入口](validation.md#独立复算入口)保留了归档前的活动目录路径。该路径已随归档移动，历史报告原文不作修改。当前脚本为 `openspec/changes/archive/2026-10-13-optimize-rk3576-nanotrack-window-preprocessing/evidence/production-20261012/analyze-results.py`。

从本 worktree 的仓库根目录运行以下完整命令。`python` 应指向 Python 3；本次核验使用 conda base 环境。命令先把归档脚本和 `board-results/` 原始记录复制到临时目录，再运行副本。这样可以复算历史数据，同时保留归档证据原样。

```bash
(
    set -e
    evidence_dir='openspec/changes/archive/2026-10-13-optimize-rk3576-nanotrack-window-preprocessing/evidence/production-20261012'
    recalc_dir=$(mktemp -d "${TMPDIR:-/tmp}/nanotrack-window-recalc.XXXXXX")
    cp "$evidence_dir/analyze-results.py" "$recalc_dir/analyze-results.py"
    cp -R "$evidence_dir/board-results" "$recalc_dir/board-results"
    python "$recalc_dir/analyze-results.py"
    cmp "$evidence_dir/analysis.json" "$recalc_dir/analysis.json"
    printf '复算结果与归档 analysis.json 完全相同；临时结果：%s\n' "$recalc_dir/analysis.json"
)
```

脚本按自身文件位置查找同级 `board-results/`，不按 shell 的当前目录寻找原始记录。它读取 `frames.csv`、`intervals.csv` 和 `other-windows.csv`，并为该目录中的文件计算 SHA-256。输出 `analysis.json` 写在脚本同级目录，因此上述命令只写入临时目录。`cmp` 逐字节比较复算输出与[归档结果](evidence/production-20261012/analysis.json)；不一致时命令会失败。

复算只需要 Python 标准库和已归档文本记录，无需开发板、板端模型或输入大图。它核对已有记录与统计，不重新执行模型，也不重新采集性能样本。本次在开发机执行上述临时副本复算，输出与归档 `analysis.json` 逐字节相同。复算前后核对历史报告和原证据的 SHA-256，均未改变；历史验收结论及其适用限制继续保留。

## 接受并保留的限制

W1 是标准生产内存环境一致性未验证的问题。原实测板 `MemTotal=8112044 kB`，约 7.74 GiB，与 `AGENTS.md` 的 3.8 GiB 不同。现有结果不能推定覆盖标准 3.8 GiB 配置。W1 完整原文保留在 [独立验收报告的 WARNING 部分](verification-report.md)，实际读数及证据保留在 [validation.md](validation.md)。本轮不猜测原因，也不修改硬件规格。

其他未验证项原样保留：目标环境的 C++ 静态工具检查、正常墙钟后的无条件板端增量身份复验、本轮当前诊断二进制完整十对 NPU A/B。Jetson、历史现场完整负载、长期压力等不适用边界继续以独立验收报告为准。没有把这些记录删除或改写为通过。

## 归档范围和实施代码

归档目录为 `openspec/changes/archive/2026-10-13-optimize-rk3576-nanotrack-window-preprocessing/`。移动前确认该目录不存在。同步完成并验证后，再移动完整变更目录；没有并行执行同步和移动。

完整目录包含 `.openspec.yaml`、规划工件、增量规格、`validation.md`、`verification-report.md` 及全部证据。移动前的 89 个原有文件指纹见 [原工件 SHA-256 清单](evidence/archive-20261013/original-artifact-sha256.json)。SHA-256 是用于核对文件内容是否变化的摘要；归档收尾逐项比较该清单，并另存结果。

本轮仅新增主规格内容、归档记录和同步检查证据。实施代码及 W2 补充代码继续保留为未提交改动。没有提交、重置、部署、同步其他变更或访问开发板，也没有操作正式服务。

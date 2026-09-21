# NanoTrackV3 搜索特征直连性能测试

本程序 `nanotrackv3_search_direct` 与 [`nanotrackv3_baseline`](../baseline/README.md) 做同一件事：在 RK3576 上按录像逐帧运行 NanoTrackV3，并记录每帧延迟。两者只有一个差别，就是**搜索特征怎么从搜索模型交到 head 模型**。

- backbone 指提取图像特征的模型。模板 backbone 处理 127×127 的模板图，搜索 backbone 处理 255×255 的搜索图。
- head 指比较模板特征和搜索特征、输出分类与位置的模型。
- baseline：搜索特征先被取回主机、转成 Float32（32 位浮点）、必要时重排，再普通提交给 head。
- 本程序：搜索模型直接把原生 FP16（16 位浮点）特征写进一块设备内存，head 从同一块内存读取。主机不读取、不复制、不转换这段中间特征。

举例说明差别。处理第 B 帧时，baseline 的顺序是"搜索模型算出 B 的特征 → 主机取回 B 的特征 → 主机再把 B 的特征提交给 head"；本程序的顺序是"搜索模型把 B 的特征写进共享内存 S → head 直接读取 S"。

除此之外，解码、前处理、模板生成、模板每帧普通提交、head 输出读取、公共跟踪算法、统计口径和记录格式都与 baseline 一致。程序不启动 ROS，不加载 YOLO，不运行 DCF 或卡尔曼滤波，也不转换或重新训练模型。

## 前提与边界

- 目标平台固定为 RK3576，RKNN Runtime 2.3.2，RKNPU 驱动 0.9.8。版本不符时程序在测量前退出。
- 只支持搜索边的原生特征为 FP16、排列为 NC1HWC2 的模型。NC1HWC2 表示把逻辑通道拆成若干组存储，例如逻辑 96 通道存成 12 组、每组 8 通道，形状写作 `[1,12,16,16,8]`。
- 兼容性只看 SDK 实际返回的属性，不看文件名，也不使用模型指纹白名单。属性不支持、内存导入失败或绑定失败时，程序明确报错退出，**不会**悄悄回退成 baseline 的主机交接。
- 本程序是性能对照工具，不是生产后端。生产工厂和 baseline 的默认加载路径没有改变。

## 构建

仓库入口（推荐，使用 `AGENTS.local.md` 指定的 ARM64 镜像）：

```sh
./build.sh --test
```

产物在 `.docker-artifacts/install/drone_tracker/lib/drone_tracker/` 下，包含 `nanotrackv3_search_direct` 和比较工具 `nanotrackv3_benchmark_compare`。

也可在具备 OpenCV、FFmpeg、JPEG、OpenSSL 和 RKNN SDK 的 ARM64 环境独立构建，不需要 ROS：

```sh
cmake -S tests/nanotracev3_performance_benchmarks/search_direct -B build-search-direct \
  -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON \
  -DRKNN_INCLUDE_DIR=/opt/rknn-runtime/include \
  -DRKNN_LIBRARY=/opt/rknn-runtime/lib/librknnrt.so
cmake --build build-search-direct -j4
LD_LIBRARY_PATH=/opt/rknn-runtime/lib ctest --test-dir build-search-direct --output-on-failure
build-search-direct/nanotrackv3_search_direct --help
```

独立入口会顺带构建 baseline 目标，因此同一个构建目录里同时有两个程序，便于做同条件对照。构建前会检查提交号和源码差异并嵌入程序身份，该快照已覆盖本目录的源码；Git 查询失败会使构建失败。

把程序复制到开发板的独立目录运行，使用板端系统的 Runtime 2.3.2。不要用构建镜像里的 SDK 库覆盖系统库；容器测试不能代替实机验证。

## 配置与运行

配置格式与 baseline 完全相同，详见 [baseline 的说明](../baseline/README.md)：录像、首帧框、三份 FP16 模型、导出证据、预热次数、更新上限、轮次、板号和输出目录。可直接复制 [`config.example.yaml`](config.example.yaml)。

做对照时，**只改输出目录**，其余项保持一致：

```sh
./nanotrackv3_baseline        baseline-observed.yaml     # output: baseline-run-001
./nanotrackv3_search_direct   search-direct-observed.yaml # output: search-direct-run-001
```

输出目录必须不存在，父目录必须已存在。退出码含义与 baseline 相同：0 表示完成，1 表示运行或写盘失败，2 表示配置错误，130 表示收到 SIGINT/SIGTERM。

## 每帧的调用顺序

顺序是实机验证过的，不能随意调整。绑定指 `rknn_set_io_mem`，只是告诉设备"这个输入或输出用哪块内存"，本身不执行模型，也不复制特征。普通提交指 `rknn_inputs_set`，由 Runtime 接收主机数据。

1. 按 baseline 的方式准备搜索图像的 Float32 数据。
2. 用 `rknn_inputs_set` 把搜索图像提交给搜索模型。
3. **提交之后**用 `rknn_set_io_mem` 重新绑定搜索模型的输出到共享内存 S。
4. 同步执行搜索模型。阻塞返回成功即代表本帧特征已写入 S。
5. 按 baseline 的方式准备模板特征，并只把模板输入普通提交给 head。
6. **模板提交之后**重新绑定 head 的搜索输入到 S，并设置原样读取。此后到 head 执行前不再对 head 普通提交。
7. 执行 head，用普通接口取回两项 Float32 输出，交给公共跟踪算法。
8. head 读取完成之后，下一帧才允许覆盖 S。

为什么必须每帧重新绑定：2026-09-20 在开发板 2 的实测表明，任何一次普通输入提交都会让该模型已建立的绑定失效。只在初始化时绑定会读到全零特征；在模板提交之前绑定也不可靠。仅"提交之后再绑定"这一种顺序通过了 720 次独立进程验证。

共享内存由搜索模型用 `rknn_create_mem` 分配，head 用 `rknn_create_mem_from_fd` 按同一个文件描述符导入，因此两个会话管理各自的句柄，但只有一份特征数据。销毁时先释放 head 的导入句柄，再释放搜索模型的原始内存，最后销毁会话。无法确认设备停止或释放失败时，按仓库的致命诊断策略终止进程，不提前释放可能仍被设备使用的存储。

## 兼容性拒绝

程序在测量前检查搜索模型的原生输出与 head 原生搜索输入，两者必须：维数为 5、排列为 NC1HWC2、类型为 FP16、各维相同、有效行跨度相同、带跨度容量相同且与按有效跨度算出的容量一致、量化元数据相同，并且原生通道分组能覆盖普通属性报告的逻辑通道数。

跨度表示相邻数据行在内存中的距离，可能含填充。SDK 把输出的 `w_stride` 为 0 定义为"等于宽度"，因此一端报 0、另一端报显式宽度仍算相同，程序按有效值判断，不比较字段字面值。`h_stride` 被 SDK 标注为只写字段，不作为准入依据。

任一项不符，程序报出包含预期与实际属性的诊断并失败。例如两端逻辑元素数相同但通道分组不同，会被明确拒绝，而不是转换后冒充直连。

## 记录与结果对比

输出文件与 baseline 相同：`manifest.json`、`frames.csv`、`spans.csv`、`environment.csv`、`summary.json`、`report.md`、`diagnostics.log`、`code_snapshot.txt`。字段含义见 baseline 说明。本程序额外记录：

- `manifest.json` 的身份：`benchmark_variant=search_direct`、`search_handoff=native_fp16_shared`、`bind_policy=rebind_after_each_inputs_set`、`search_completion_evidence`、`shared_feature_bytes`、`shared_feature_fd`、`search_native_output`、`head_native_search_input`、`head_role_indices`。
- `summary.json` 与中文 `report.md` 同时记录 `benchmark_variant`、`search_handoff`、`bind_policy`、`shared_feature_bytes`、`search_native_output`、`head_native_search_input` 和 `model_attributes_runtime_driver`。最后一项包含普通模型属性及 Runtime/驱动描述。summary 将该多行描述保存为字符串数组，每个元素对应一行，避免旧 OpenCV 核对器的单字符串长度限制；manifest 原字符串字段保持不变。容量沿用 manifest 的字节数字符串。JSON 使用标准字符串转义，中文报告用缩进代码块保留多行属性。baseline 默认报告不新增直连身份。
- `spans.csv` 增加阶段 `io_mem_bind`，分别出现在 `search_backbone` 和 `head` 之下，表示每帧重新绑定的耗时。这段时间**包含**在模型区间和端到端区间内，不从原始数据中扣除。
- 搜索模型不再取回输出，因此该模型下**不会**出现 `outputs_get`、`output_copy`、`outputs_release`、`normalize`。未执行的阶段记为空，不写成零耗时。

旧 baseline 记录没有 `benchmark_variant` 字段，读取工具会把它按普通交接路径处理。

### 独立重算与逐帧数值核对

`baseline_verify_records` 读取落盘 CSV 独立重算统计、父子区间和模型调用次数，并按变体检查阶段集合是否合法。第二个目录用于逐帧比较框与分数，容差为框各分量 `1e-4` 像素、分数 `1e-6`：

```sh
./baseline_verify_records search-direct-run-001
./baseline_verify_records search-direct-run-001 baseline-run-001
```

### 实机数值探针

`nanotrackv3_search_direct_probe` 只在开发板上运行，用固定种子的 Float32 输入逐元素比较三条路径的 **head 原始输出**：正式程序的"先 search 后 head"顺序、已通过实机验证的探针顺序，以及完全独立的普通 search 会话加普通 head 会话参照。参照不使用被测共享缓冲区生成自身输入。

```sh
./nanotrackv3_search_direct_probe \
  --template-model models/template_backbone.rknn \
  --search-model   models/search_backbone.rknn \
  --head-model     models/head.rknn \
  --iterations 240 --output probe.json
```

模板种子为 11、29，搜索种子为 101、202、303，搜索按 A/B/A/C 交替，每四次切换模板，因此同时覆盖首次调用、缓冲区复用和模板切换。任一样本不一致、出现非有限值、参照输出没有多样性或模板切换不足时，程序以非零码退出。与回放对照不同，它比较的是未经后处理的原始张量，能发现"最终框相同但原始输出不同"的情况。

### 性能比较

`nanotrackv3_benchmark_compare` 读取两侧的多次运行，先核对板号、机器、系统与内核、主机名、CPU 亲和性、录像指纹、三份模型指纹、RKNN 库路径与指纹、精度校验状态、提交号与源码快照指纹、编译选项与编译器、依赖库版本、PTS 时间基、Runtime/驱动版本、首帧框、预热次数、更新上限、轮次与环境采样间隔。任一项不同或缺失就拒绝输出提速结论，因为那已经不是单变量对照。

工具还要求 baseline 与直连两侧、细分观测开启与关闭两种模式共四个分组都存在，且每组不少于 `--min-rounds`（默认 3）轮，否则拒绝比较。

```sh
./nanotrackv3_benchmark_compare \
  --baseline baseline-observed-1 --baseline baseline-observed-2 --baseline baseline-observed-3 \
  --baseline baseline-total-1 --baseline baseline-total-2 --baseline baseline-total-3 \
  --direct search-direct-observed-1 --direct search-direct-observed-2 --direct search-direct-observed-3 \
  --direct search-direct-total-1 --direct search-direct-total-2 --direct search-direct-total-3 \
  --report compare.md
```

每个 `--baseline` 或 `--direct` 只接收一个目录。上例显式列出四组、每组三次运行，共 12 个目录，不能用一个选项后接多个展开目录替代。

标准输出是 JSON，包含每次运行按轮次和合并的延迟分布、两侧分组统计、均值差值与倍数、绑定成本分布，以及细分观测开启与关闭的差值。没有取得性能收益时如实报告，不设事后速度门槛，也不丢弃慢帧或低分帧。

## 验证边界

延迟结果不代表跟踪质量验收。逐元素一致只说明在所测模型和输入下两条路径的数值相同，不证明设备内部完全没有数据搬运。本目录的受控测试使用设备替身，通过不等于实机验收通过；实机结论见本变更的 `validation.md`。

#include "benchmark_runner.h"
#include "platform/rk3576/tests/rknn_test_support.h"
#include "result_writer.h"
#include "statistics.h"
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
namespace {
using namespace baseline;
using namespace rknn_test;
void write(const std::filesystem::path &path, const std::string &text) { std::ofstream(path) << text; }
std::vector<CsvRow> readRecords(const std::filesystem::path &path) {
    std::ifstream input{path};
    require(bool(input), "部分记录未落盘");
    std::vector<std::string> headers, values;
    require(readCsvRecord(input, headers), "部分记录表头缺失");
    std::vector<CsvRow> records;
    while (readCsvRecord(input, values)) {
        require(values.size() == headers.size(), "部分记录列数错误");
        CsvRow row;
        for (std::size_t i{0}; i < headers.size(); ++i) {
            row.emplace(headers[i], values[i]);
        }
        records.push_back(std::move(row));
    }
    return records;
}
void verifyPartialRun(const Config &config, bool model_failure, const std::filesystem::path &verifier) {
    const auto frames{readRecords(config.output / "frames.csv")};
    const auto spans{readRecords(config.output / "spans.csv")};
    // 预热初始化/更新各一条，正式初始化一条，随后三条正式更新（末条可能失败）。
    require(frames.size() == 6, "中断或失败丢失已有帧，或继续处理了后续帧");
    for (std::size_t i{0}; i < frames.size(); ++i) {
        const auto &frame{frames[i]};
        const bool warmup{i < 2};
        const bool init{i == 0 || i == 2};
        const bool failed{model_failure && i == 5};
        require(frame.at("repeat_id") == (warmup ? "0" : "1") &&
                    frame.at("phase") == (warmup ? "warmup" : "measure") &&
                    frame.at("operation") == (init ? "init" : "track") &&
                    frame.at("frame_index") == std::to_string(warmup ? i : i - 2),
                "部分记录帧身份或顺序错误");
        require(frame.at("status") == (failed ? "failed" : "success"), "部分记录状态错误");
        for (const char *field : {"decode_ns", "frame_copy_ns", "preprocess_ns", "inference_ns",
                                 "other_ns", "decode_to_result_ns"}) {
            require(!frame.at(field).empty(), "已执行的顶层阶段丢失");
        }
        require(frame.at("score").empty() == (init || failed) &&
                    frame.at("postprocess_ns").empty() == (init || failed),
                "初始化或失败帧虚构分数/后处理，或成功帧丢失结果");
        require(frame.at("bbox_valid") == (failed ? "0" : "1") &&
                    frame.at("error").empty() == !failed,
                "失败帧有效性或错误身份丢失");
        if (failed) {
            for (const char *field : {"x", "y", "width", "height"}) {
                require(frame.at(field).empty(), "失败帧不应保留预测框");
            }
        }
        std::map<std::string, int> stages;
        for (const auto &span : spans) {
            if (span.at("repeat_id") != frame.at("repeat_id") ||
                span.at("frame_index") != frame.at("frame_index")) {
                continue;
            }
            const auto &stage{span.at("stage")};
            ++stages[stage];
            const bool incomplete{failed && (stage == "decode_to_result" || stage == "inference" ||
                                             stage == "search_backbone" || stage == "outputs_get")};
            require(span.at("complete") == (incomplete ? "0" : "1"), "已测区间完成标记错误");
        }
        require(stages["decode_to_result"] == 1 && stages["decode"] == 1 && stages["frame_copy"] == 1 &&
                    stages["preprocess"] == 1 && stages["inference"] == 1 &&
                    stages["template_backbone"] == (init ? 1 : 0) &&
                    stages["search_backbone"] == (init ? 0 : 1) &&
                    stages["head"] == (init || failed ? 0 : 1) &&
                    stages["postprocess"] == (init || failed ? 0 : 1),
                "已执行区间丢失或记录了未执行阶段");
        if (failed) {
            require(stages["inputs_set"] == 1 && stages["run"] == 1 && stages["outputs_get"] == 1 &&
                        stages["output_copy"] == 0 && stages["outputs_release"] == 0 && stages["normalize"] == 0,
                    "模型失败前的区间丢失，或虚构输出整理区间");
        }
    }
    const std::string status{model_failure ? "failed" : "interrupted"};
    const std::string reason{model_failure ? "model_error" : "signal"};
    for (const char *file : {"manifest.json", "summary.json"}) {
        const auto text{readText(config.output / file)};
        require(text.find("\"status\":\"" + status + "\"") != std::string::npos &&
                    text.find("\"reason\":\"" + reason + "\"") != std::string::npos,
                "部分报告或 manifest 的最终状态错误");
    }
    const auto summary{readText(config.output / "summary.json")};
    require(summary.find(model_failure ? "\"valid_updates\":2" : "\"valid_updates\":3") != std::string::npos &&
                summary.find(model_failure ? "\"failed_frames\":1" : "\"failed_frames\":0") != std::string::npos &&
                summary.find("\"warmup_frames\":2") != std::string::npos &&
                summary.find("\"initializations\":1") != std::string::npos,
            "部分报告计数错误或失败混入成功样本");
    require(readText(config.output / "report.md").find("状态：" + status) != std::string::npos,
            "中文部分报告未生成");
    // 独立进程只读取落盘 CSV/JSON，核对每组分布、父子区间、调用次数和处理速率。
    const pid_t child{fork()};
    require(child >= 0, "不能启动部分报告核对程序");
    if (child == 0) {
        execl(verifier.c_str(), verifier.c_str(), config.output.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    int result{};
    pid_t waited{};
    do {
        waited = waitpid(child, &result, 0);
    } while (waited < 0 && errno == EINTR);
    require(waited == child && WIFEXITED(result) && WEXITSTATUS(result) == 0, "部分报告独立重算失败");
}
Sdk models() {
    Model z, x, head;
    z.inputs = {attribute(0, {1, 3, 127, 127})};
    z.outputs = {attribute(0, {1, 96, 8, 8})};
    x.inputs = {attribute(0, {1, 3, 255, 255})};
    x.outputs = {attribute(0, {1, 96, 16, 16})};
    head.inputs = {attribute(0, {1, 96, 8, 8}), attribute(1, {1, 96, 16, 16})};
    head.outputs = {attribute(0, {1, 2, 15, 15}), attribute(1, {1, 4, 15, 15})};
    head.value = [](std::size_t output, std::size_t index) {
        return output == 1 ? 10.0f : (index >= 225 ? -10.0f : 0.0f);
    };
    return {{z, x, head}};
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 2, "素材目录缺失");
        const auto root{std::filesystem::temp_directory_path() / ("baseline-runner-" + std::to_string(getpid()))};
        std::filesystem::create_directory(root);
        Config config;
        config.source = root / "config.yaml";
        write(config.source, "%YAML:1.0\n---\nschema_version: 1\n");
        config.board = "受控模拟，非实机";
        config.video = std::filesystem::path(argv[1]) / "420.mov";
        config.bbox = {4, 5, 20, 20};
        config.warmup_updates = 1;
        config.repeats = 3;
        config.evidence = root / "models.yaml";
        const std::array<std::string, 3> roles{"template_backbone", "search_backbone", "head"};
        std::string evidence{"%YAML:1.0\n---\nschema_version: 1\nmodels:\n"};
        for (std::size_t i{0}; i < 3; ++i) {
            config.models[i] = root / (roles[i] + ".rknn");
            write(config.models[i], "controlled fake model");
            const auto source{root / (roles[i] + "-source.txt")};
            write(source, "controlled fake export FP16");
            const auto record{root / (roles[i] + ".yaml")};
            write(record, "%YAML:1.0\n---\nrole: " + jsonString(roles[i]) +
                              "\nmodel_sha256: " + jsonString(sha256(config.models[i])) +
                              "\nprecision: FP16\nstatus: success\nsource_record: " + jsonString(source.string()) +
                              "\nsource_record_sha256: " + jsonString(sha256(source)) + "\n");
            evidence += "  - { role: " + jsonString(roles[i]) + ", sha256: " + jsonString(sha256(config.models[i])) +
                        ", precision: FP16, export_record: " + jsonString(record.string()) +
                        ", export_record_sha256: " + jsonString(sha256(record)) + " }\n";
        }
        write(config.evidence, evidence);
        for (const std::string mode : {"success", "warmup", "empty", "bad", "model", "precision", "missing_evidence",
                                       "mixed", "interrupt", "mid_interrupt", "mid_model", "disk", "output"}) {
            auto fake{models()};
            sdk = &fake;
            auto scenario{config};
            scenario.output = root / mode;
            interrupted = 0;
            if (mode == "warmup") {
                scenario.warmup_updates = 3;
            }
            if (mode == "empty") {
                scenario.video = std::filesystem::path(argv[1]) / "bad-artist.mov";
                scenario.warmup_updates = 0;
            }
            if (mode == "bad") {
                scenario.video = std::filesystem::path(argv[1]) / "bad.mov";
                scenario.warmup_updates = 0;
            }
            if (mode == "model") {
                fake.models[1].get_status = -1;
            }
            if (mode == "missing_evidence") {
                scenario.evidence = root / "missing.yaml";
            }
            if (mode == "mixed") {
                fake.models[0].inputs[0] = attribute(0, {1, 3, 127, 127}, RKNN_TENSOR_INT8);
            }
            if (mode == "precision") {
                write(config.evidence, "%YAML:1.0\n---\nschema_version: 1\nmodels: []\n");
            }
            if (mode == "interrupt") {
                interrupted = 1;
            }
            if (mode == "mid_interrupt" || mode == "mid_model") {
                scenario.video = std::filesystem::path(argv[1]) / "slow.mov";
                scenario.max_updates = 5;
                fake.models[1].inspect = [&](std::uint32_t, rknn_input *) {
                    // 一次预热加两次正式更新后，在第三次正式搜索提交时触发。
                    if (fake.models[1].sets == 4) {
                        if (mode == "mid_interrupt") {
                            interrupted = 1;
                        } else {
                            fake.models[1].get_status = -1;
                        }
                    }
                };
            }
            if (mode == "output") {
                std::filesystem::create_directory(scenario.output);
            }
            const int code{runBenchmark(scenario, [&](const Config &c) {
                if (mode == "disk") {
                    const int full{open("/dev/full", O_WRONLY)};
                    require(full >= 0, "无法注入写盘故障");
                    for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd")) {
                        std::error_code error;
                        const auto target{std::filesystem::read_symlink(entry.path(), error)};
                        if (!error && target == scenario.output / "frames.csv") {
                            require(dup2(full, std::stoi(entry.path().filename().string())) >= 0, "写盘故障注入失败");
                        }
                    }
                    close(full);
                }
                auto loaded{
                    rk3576::loadNanoTrack(c.models[0].string(), c.models[1].string(), c.models[2].string(), api())};
                if (loaded) {
                    loaded.value.description = "controlled runtime=2.3.2 driver=0.9.8";
                }
                return loaded;
            })};
            const bool signal_exit{mode == "interrupt" || mode == "mid_interrupt"};
            require(code == (mode == "success" ? 0 : signal_exit ? 130 : 1), "运行器退出码错误");
            if (mode == "mid_interrupt" || mode == "mid_model") {
                require(fake.models[0].runs == 2 && fake.models[1].runs == 4 &&
                            fake.models[2].runs == (mode == "mid_model" ? 3 : 4),
                        "中途退出后仍在执行模型或发生重试");
                verifyPartialRun(scenario, mode == "mid_model",
                                 std::filesystem::absolute(argv[0]).parent_path() / "baseline_verify_records");
                std::cout << mode << "：部分数据、区间、状态和独立重算通过\n";
            }
            if (mode != "output" && mode != "disk") {
                const auto summary{readText(scenario.output / "summary.json")};
                if (mode == "success") {
                    require(summary.find("\"valid_updates\":6") != std::string::npos, "低分更新或重复轮次丢失");
                    require(fake.models[0].runs == 4 && fake.models[1].runs == 7 && fake.models[2].runs == 7,
                            "预热或正式轮次模型次数错误");
                }
                if (mode == "warmup") {
                    require(readText(scenario.output / "manifest.json").find("warmup_insufficient") !=
                                std::string::npos,
                            "预热不足未标识");
                }
                if (mode == "empty") {
                    require(summary.find("\"valid_updates\":0") != std::string::npos, "空更新误成功");
                }
            }
            write(config.evidence, evidence);
        }
        interrupted = 0;
        const std::string valid_config{
            "%YAML:1.0\n---\nschema_version: 1\nboard: test\nvideo: " +
            jsonString(std::filesystem::absolute(config.video).string()) +
            "\ntemplate_model: template_backbone.rknn\nsearch_model: search_backbone.rknn\nhead_model: head.rknn"
            "\nprecision_evidence: models.yaml\noutput: new-output\nbbox: [1., 2., 3., 4.]\n"};
        write(config.source, valid_config);
        const auto loaded{loadConfig(config.source)};
        require(loaded.models == config.models && loaded.warmup_updates == 30 && loaded.repeats == 3,
                "相对路径或默认值错误");
        for (const std::string invalid : {"warmup_updates: -1", "repeats: 0", "max_updates: 1.5",
                                          "environment_interval_ms: 0", "detailed_observation: 2"}) {
            write(config.source, valid_config + invalid + "\n");
            bool rejected{false};
            try {
                loadConfig(config.source);
            } catch (...) {
                rejected = true;
            }
            require(rejected, "非法配置未拒绝");
        }
        write(config.source, valid_config);
        std::filesystem::create_directory(root / "new-output");
        bool rejected{false};
        try {
            loadConfig(config.source);
        } catch (...) {
            rejected = true;
        }
        require(rejected, "既有输出目录未拒绝");
        std::filesystem::remove_all(root);
        std::cout << "基准成功、低分、重复、预热、空数据、坏帧、模型、证据、中断、输出测试通过\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

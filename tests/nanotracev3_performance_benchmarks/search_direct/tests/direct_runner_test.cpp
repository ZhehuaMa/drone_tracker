#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "benchmark_runner.h"
#include "direct_test_support.h"
#include "platform/rk3576/rknn_nanotrack_factory.h"
#include "result_writer.h"
#include "search_direct_backend.h"
#include "statistics.h"

namespace {
using namespace baseline;
using namespace search_direct_test;

std::filesystem::path verify_tool, compare_tool;

void write(const std::filesystem::path &path, const std::string &text) { std::ofstream(path) << text; }

std::vector<CsvRow> records(const std::filesystem::path &path) {
    std::ifstream input{path};
    require(bool(input), "记录未落盘: " + path.string());
    std::vector<std::string> headers, values;
    require(readCsvRecord(input, headers), "记录表头缺失");
    std::vector<CsvRow> rows;
    while (readCsvRecord(input, values)) {
        require(values.size() == headers.size(), "记录列数错误");
        CsvRow row;
        for (std::size_t i{0}; i < headers.size(); ++i) {
            row.emplace(headers[i], values[i]);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

int runTool(const std::filesystem::path &program, const std::vector<std::string> &arguments) {
    const pid_t child{fork()};
    require(child >= 0, "无法启动外部工具");
    if (child == 0) {
        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(program.c_str()));
        for (const auto &argument : arguments) {
            argv.push_back(const_cast<char *>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execv(program.c_str(), argv.data());
        _exit(127);
    }
    int status{};
    pid_t waited{};
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    require(waited == child && WIFEXITED(status), "外部工具异常终止");
    return WEXITSTATUS(status);
}

/** @brief 统计某一帧下每个模型角色记录到的阶段次数。 */
std::map<std::string, std::map<std::string, int>> stagesOf(const std::vector<CsvRow> &spans,
                                                           const std::string &repeat, const std::string &index) {
    std::map<std::string, std::map<std::string, int>> result;
    for (const auto &span : spans) {
        if (span.at("repeat_id") != repeat || span.at("frame_index") != index || span.at("model_role").empty()) {
            continue;
        }
        ++result[span.at("model_role")][span.at("stage")];
    }
    return result;
}

Config baseConfig(const std::filesystem::path &root, const std::filesystem::path &fixtures) {
    Config config;
    config.source = root / "config.yaml";
    write(config.source, "%YAML:1.0\n---\nschema_version: 1\n");
    config.board = "受控模拟，非实机";
    config.video = fixtures / "420.mov";
    config.bbox = {4, 5, 20, 20};
    config.warmup_updates = 1;
    config.max_updates = 2;
    config.repeats = 1;
    config.evidence = root / "models.yaml";
    const std::array<std::string, 3> roles{"template_backbone", "search_backbone", "head"};
    std::string evidence{"%YAML:1.0\n---\nschema_version: 1\nmodels:\n"};
    for (std::size_t i{0}; i < 3; ++i) {
        config.models[i] = root / (roles[i] + ".rknn");
        write(config.models[i], "controlled fake model " + roles[i]);
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
    return config;
}

int runDirect(const Config &config, FakeDevice &fake, std::map<std::string, std::string> *recorded_identity = nullptr) {
    device = &fake;
    std::map<std::string, std::string> backend_identity;
    RunnerExtensions extensions;
    extensions.variant = "search_direct";
    extensions.identity = [&](std::map<std::string, std::string> &identity) {
        identity.insert(backend_identity.begin(), backend_identity.end());
        if (recorded_identity) {
            *recorded_identity = identity;
        }
    };
    const int code{runBenchmark(
        config,
        [&backend_identity](const Config &c) {
            return search_direct::loadSearchDirect(c.models[0].string(), c.models[1].string(), c.models[2].string(),
                                                   fakeApi(), &backend_identity);
        },
        extensions)};
    device = nullptr;
    return code;
}

void checkIdentityReports(const std::filesystem::path &directory,
                          const std::map<std::string, std::string> &identity) {
    const auto manifest{readText(directory / "manifest.json")};
    const auto summary{readText(directory / "summary.json")};
    const auto report{readText(directory / "report.md")};
    for (const char *key : {"benchmark_variant", "search_handoff", "bind_policy", "shared_feature_bytes",
                           "search_native_output", "head_native_search_input", "model_attributes_runtime_driver"}) {
        const auto &value{identity.at(key)};
        const auto field{jsonString(key) + ":" + jsonString(value)};
        require(manifest.find(field) != std::string::npos, std::string("manifest 身份值错误: ") + key);
        if (std::string(key) == "model_attributes_runtime_driver") {
            std::string expected{jsonString(key) + ":["};
            std::istringstream lines{value};
            std::string line;
            bool first{true};
            while (std::getline(lines, line)) {
                if (!first) {
                    expected += ',';
                }
                first = false;
                expected += jsonString(line);
            }
            expected += ']';
            require(summary.find(expected) != std::string::npos, "summary 普通属性行内容或转义错误");
        } else {
            require(summary.find(field) != std::string::npos, std::string("summary 身份值或转义错误: ") + key);
        }
        if (std::string(key) == "benchmark_variant") {
            require(report.find("benchmark_variant=" + value) != std::string::npos, "中文报告缺少变体字段和值");
            continue;
        }
        require(report.find(std::string("### ") + key + "\n\n") != std::string::npos, "中文报告缺少身份字段");
        std::istringstream lines{value};
        std::string line;
        while (std::getline(lines, line)) {
            require(report.find("    " + line + "\n") != std::string::npos, "中文报告未保留属性内容或换行");
        }
    }
    require(identity.at("search_handoff") == "native_fp16_shared" &&
                identity.at("bind_policy") == "rebind_after_each_inputs_set" &&
                identity.at("shared_feature_bytes") == "49152",
            "直连身份值不符合契约");
    require(identity.at("model_attributes_runtime_driver").find('\n') != std::string::npos,
            "普通属性测试必须覆盖多行描述");
}

int runConventional(const Config &config, FakeDevice &fake) {
    device = &fake;
    const int code{runBenchmark(config, [](const Config &c) {
        return rk3576::loadNanoTrack(c.models[0].string(), c.models[1].string(), c.models[2].string(),
                                     fakeApi().core);
    })};
    device = nullptr;
    return code;
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 4, "用法：direct_runner_tests 素材目录 verify_records 比较工具");
        const std::filesystem::path fixtures{argv[1]};
        verify_tool = argv[2];
        compare_tool = argv[3];
        const auto root{std::filesystem::temp_directory_path() / ("search-direct-runner-" + std::to_string(getpid()))};
        std::filesystem::create_directory(root);
        const auto config{baseConfig(root, fixtures)};

        auto direct_config{config};
        direct_config.output = root / "direct-observed";
        auto direct_fake{nanoTrackModels()};
        const std::string special_name{"feature\"\\\n### not a heading"};
        std::copy(special_name.begin(), special_name.end(), direct_fake.models[1].native_outputs[0].name);
        direct_fake.models[1].native_outputs[0].name[special_name.size()] = '\0';
        std::map<std::string, std::string> direct_identity;
        require(runDirect(direct_config, direct_fake, &direct_identity) == 0, "搜索直连运行未成功结束");
        checkIdentityReports(direct_config.output, direct_identity);

        auto baseline_config{config};
        baseline_config.output = root / "baseline-observed";
        auto baseline_fake{nanoTrackModels()};
        require(runConventional(baseline_config, baseline_fake) == 0, "普通路径运行未成功结束");

        // 两条路径的模型执行次数必须一致：预热一次初始化加一次更新，正式一次初始化加三次更新。
        for (const auto *fake : {&direct_fake, &baseline_fake}) {
            require(fake->models[0].runs == 2 && fake->models[1].runs == 3 && fake->models[2].runs == 3,
                    "模型执行次数与基线不一致");
        }

        const auto manifest{readText(direct_config.output / "manifest.json")};
        for (const char *field : {"\"benchmark_variant\":\"search_direct\"", "\"search_handoff\":\"native_fp16_shared\"",
                                  "\"bind_policy\":\"rebind_after_each_inputs_set\"", "\"shared_feature_bytes\"",
                                  "\"search_native_output\"", "\"head_native_search_input\""}) {
            require(manifest.find(field) != std::string::npos, std::string("manifest 缺少路径身份字段: ") + field);
        }
        require(readText(baseline_config.output / "manifest.json").find("benchmark_variant") == std::string::npos,
                "baseline 记录不应出现变体字段");
        for (const char *file : {"summary.json", "report.md"}) {
            require(readText(baseline_config.output / file).find("search_handoff") == std::string::npos,
                    "baseline 报告不应出现直连身份");
        }
        require(readText(direct_config.output / "summary.json").find("\"benchmark_variant\":\"search_direct\"") !=
                    std::string::npos,
                "统计摘要缺少变体标记");
        require(readText(direct_config.output / "report.md").find("search_direct") != std::string::npos,
                "中文报告未标明执行路径变体");

        // 阶段归属：直连路径的搜索模型不取回输出，两端都记录重新绑定。
        const auto direct_spans{records(direct_config.output / "spans.csv")};
        const auto direct_stages{stagesOf(direct_spans, "1", "1")};
        require(direct_stages.at("search_backbone").count("outputs_get") == 0, "直连路径不应记录搜索输出取回");
        require(direct_stages.at("search_backbone").at("io_mem_bind") == 1, "搜索输出缺少重新绑定区间");
        require(direct_stages.at("head").at("io_mem_bind") == 1, "head 搜索输入缺少重新绑定区间");
        require(direct_stages.at("head").at("outputs_get") == 1, "head 最终输出仍须计时");
        const auto baseline_stages{stagesOf(records(baseline_config.output / "spans.csv"), "1", "1")};
        require(baseline_stages.at("search_backbone").at("outputs_get") == 1, "普通路径必须取回搜索输出");
        require(baseline_stages.at("search_backbone").count("io_mem_bind") == 0, "普通路径不应出现共享绑定");

        require(runTool(verify_tool, {direct_config.output.string()}) == 0, "直连记录独立重算失败");
        require(runTool(verify_tool, {baseline_config.output.string()}) == 0, "普通记录独立重算失败");
        require(runTool(verify_tool, {direct_config.output.string(), baseline_config.output.string()}) == 0,
                "两条路径逐帧数值超出容差");

        // 关闭细分观测后仍应完整运行，且不再产生子区间。
        auto plain_config{config};
        plain_config.output = root / "direct-total-only";
        plain_config.detailed = false;
        auto plain_fake{nanoTrackModels()};
        std::map<std::string, std::string> plain_identity;
        require(runDirect(plain_config, plain_fake, &plain_identity) == 0, "关闭观测的直连运行未成功结束");
        checkIdentityReports(plain_config.output, plain_identity);
        for (const auto &span : records(plain_config.output / "spans.csv")) {
            require(span.at("stage") == "decode_to_result", "关闭观测后仍记录了子区间");
        }
        require(runTool(verify_tool, {plain_config.output.string()}) == 0, "关闭观测的记录独立重算失败");

        auto plain_baseline_config{config};
        plain_baseline_config.output = root / "baseline-total-only";
        plain_baseline_config.detailed = false;
        auto plain_baseline_fake{nanoTrackModels()};
        require(runConventional(plain_baseline_config, plain_baseline_fake) == 0, "关闭观测的普通运行未成功结束");

        // 直接解析 README 中可复制的比较命令，并用四组受控记录执行全部 12 个目录参数。
        const auto readme{readText(std::filesystem::path{__FILE__}.parent_path().parent_path() / "README.md")};
        const auto command_start{readme.find("./nanotrackv3_benchmark_compare \\")};
        require(command_start != std::string::npos, "README 缺少比较命令");
        const auto command_end{readme.find("```", command_start)};
        require(command_end != std::string::npos, "README 比较命令代码块未闭合");
        std::istringstream command{readme.substr(command_start, command_end - command_start)};
        std::string token;
        command >> token;
        std::vector<std::string> documented_arguments;
        std::map<std::string, int> documented_groups;
        std::set<std::string> documented_directories;
        while (command >> token) {
            if (token == "\\") {
                continue;
            }
            require(token == "--baseline" || token == "--direct" || token == "--report",
                    "README 命令必须为每个目录重复选项");
            const auto option{token};
            require(static_cast<bool>(command >> token), "README 选项缺少参数");
            documented_arguments.push_back(option);
            if (option == "--report") {
                documented_arguments.push_back((root / token).string());
                continue;
            }
            const auto suffix{token.find_last_of('-')};
            require(suffix != std::string::npos, "README 目录缺少运行编号");
            const auto group{token.substr(0, suffix)};
            const std::map<std::string, std::filesystem::path> sources{
                {"baseline-observed", baseline_config.output}, {"baseline-total", plain_baseline_config.output},
                {"search-direct-observed", direct_config.output}, {"search-direct-total", plain_config.output}};
            require(sources.count(group) == 1, "README 目录分组不正确");
            require((option == "--baseline") == (group.find("baseline-") == 0), "README 选项与目录分组不符");
            require(documented_directories.insert(token).second, "README 重复目录");
            ++documented_groups[group];
            const auto destination{root / token};
            std::filesystem::copy(sources.at(group), destination, std::filesystem::copy_options::recursive);
            documented_arguments.push_back(destination.string());
        }
        require(documented_groups.size() == 4, "README 必须覆盖四个分组");
        for (const auto &group : documented_groups) {
            require(group.second == 3, "README 每组必须列出三个目录");
        }
        require(runTool(compare_tool, documented_arguments) == 0, "README 比较命令不能被现有工具成功执行");

        // 比较工具：四个分组齐备且轮次达标才可比较；身份不符或分组缺失必须拒绝。
        const std::vector<std::string> complete{"--min-rounds",
                                                "1",
                                                "--baseline",
                                                baseline_config.output.string(),
                                                "--baseline",
                                                plain_baseline_config.output.string(),
                                                "--direct",
                                                direct_config.output.string(),
                                                "--direct",
                                                plain_config.output.string()};
        require(runTool(compare_tool, complete) == 0, "完整的同身份对照被拒绝");
        require(runTool(compare_tool, {"--baseline", baseline_config.output.string(), "--direct",
                                       direct_config.output.string(), "--min-rounds", "1"}) != 0,
                "缺少仅总计时分组的对照未被拒绝");
        {
            auto insufficient{complete};
            insufficient[1] = "2";
            require(runTool(compare_tool, insufficient) != 0, "轮次不足的对照未被拒绝");
        }
        require(runTool(compare_tool, {"--min-rounds", "1", "--baseline", direct_config.output.string(), "--direct",
                                       direct_config.output.string()}) != 0,
                "变体标记不符的运行未被拒绝");
        auto other_config{config};
        other_config.output = root / "baseline-other-bbox";
        other_config.bbox = {6, 7, 20, 20};
        auto other_fake{nanoTrackModels()};
        require(runConventional(other_config, other_fake) == 0, "对照运行未成功结束");
        {
            auto mismatched{complete};
            mismatched[3] = other_config.output.string();
            require(runTool(compare_tool, mismatched) != 0, "首帧框不同的运行未被拒绝为不可比较");
        }

        // 绑定失败：本帧失败保留记录，不返回上一帧框，后续不再继续测量。
        auto failing_config{config};
        failing_config.output = root / "direct-bind-failure";
        auto failing_fake{nanoTrackModels()};
        auto search_compute{failing_fake.models[1].compute};
        failing_fake.models[1].compute = [&failing_fake, search_compute](FakeModel &m) {
            search_compute(m);
            // 预热更新与第一次正式更新之后，在第三次搜索执行时让随后的 head 绑定失败。
            if (m.runs == 3) {
                failing_fake.models[2].bind_status = -1;
            }
        };
        device = &failing_fake;
        std::map<std::string, std::string> identity;
        RunnerExtensions failing_extensions;
        failing_extensions.variant = "search_direct";
        std::map<std::string, std::string> failed_identity;
        failing_extensions.identity = [&](std::map<std::string, std::string> &fields) {
            fields.insert(identity.begin(), identity.end());
            failed_identity = fields;
        };
        const int failed_code{runBenchmark(
            failing_config,
            [&identity](const Config &c) {
                return search_direct::loadSearchDirect(c.models[0].string(), c.models[1].string(),
                                                       c.models[2].string(), fakeApi(), &identity);
            },
            failing_extensions)};
        device = nullptr;
        require(failed_code == 1, "绑定失败必须以失败状态退出");
        checkIdentityReports(failing_config.output, failed_identity);
        const auto failed_frames{records(failing_config.output / "frames.csv")};
        require(failed_frames.size() == 5, "失败运行的已有帧记录丢失或继续处理了后续帧");
        const auto &last{failed_frames.back()};
        require(last.at("status") == "failed" && last.at("bbox_valid") == "0" && last.at("x").empty() &&
                    last.at("score").empty(),
                "失败帧保留了旧预测或伪造了分数");
        const auto failed_stages{stagesOf(records(failing_config.output / "spans.csv"), last.at("repeat_id"),
                                          last.at("frame_index"))};
        require(failed_stages.count("head") == 1 && failed_stages.at("head").count("run") == 0,
                "head 绑定失败后不应执行模型");
        require(failing_fake.models[2].runs == 2, "绑定失败后 head 仍被执行或发生重试");
        require(readText(failing_config.output / "manifest.json").find("\"status\":\"failed\"") != std::string::npos,
                "失败运行的 manifest 状态错误");

        std::filesystem::remove_all(root);
        std::cout << "search_direct 运行器、记录、阶段归属与比较工具测试通过\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

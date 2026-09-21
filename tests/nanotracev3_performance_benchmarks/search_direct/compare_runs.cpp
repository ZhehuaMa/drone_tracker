#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "result_writer.h"
#include "statistics.h"

namespace {
using baseline::CsvRow;
using baseline::Distribution;

void check(bool ok, const std::string &message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

std::vector<CsvRow> rows(const std::filesystem::path &path) {
    std::ifstream input(path);
    check(bool(input), "无法打开 " + path.string());
    std::vector<std::string> header, cells;
    check(baseline::readCsvRecord(input, header), "表头缺失: " + path.string());
    std::vector<CsvRow> result;
    while (baseline::readCsvRecord(input, cells)) {
        check(cells.size() == header.size(), "列数错误: " + path.string());
        CsvRow row;
        for (std::size_t i{0}; i < cells.size(); ++i) {
            row.emplace(header[i], cells[i]);
        }
        result.push_back(std::move(row));
    }
    return result;
}

/** @brief 一次运行的可比身份、原始延迟样本和阶段耗时。 */
struct Run {
    std::filesystem::path path{};
    std::string variant{};
    std::string status{};
    bool detailed{};
    std::map<std::string, std::string> identity{};
    std::vector<std::int64_t> total{};
    std::map<std::string, std::vector<std::int64_t>> repeats{};
    std::map<std::string, std::vector<std::int64_t>> stages{};
};

/**
 * @brief 只读 JSON 值树。
 *
 * manifest.json 里的模型属性描述很长且包含换行，通用的 OpenCV 读取器无法处理，
 * 所以这里用一个仅供本工具使用的最小解析器读取需要的字段。
 */
struct Json {
    enum class Kind { Null, Boolean, Number, String, Array, Object };
    Kind kind{Kind::Null};
    std::string text{};
    std::vector<Json> items{};
    std::map<std::string, Json> members{};
    const Json *find(const std::string &key) const {
        const auto position{members.find(key)};
        return position == members.end() ? nullptr : &position->second;
    }
};

class JsonReader {
public:
    explicit JsonReader(const std::string &raw) : raw_{raw} {}
    Json parse() {
        auto value{parseValue()};
        skipSpace();
        check(position_ == raw_.size(), "JSON 末尾有多余内容");
        return value;
    }

private:
    void skipSpace() {
        while (position_ < raw_.size() && std::isspace(static_cast<unsigned char>(raw_[position_]))) {
            ++position_;
        }
    }
    char take() {
        check(position_ < raw_.size(), "JSON 意外结束");
        return raw_[position_++];
    }
    bool consume(char expected) {
        skipSpace();
        if (position_ < raw_.size() && raw_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }
    std::string parseString() {
        check(consume('"'), "JSON 字符串缺少起始引号");
        std::string value;
        for (;;) {
            const char c{take()};
            if (c == '"') {
                return value;
            }
            if (c != '\\') {
                value += c;
                continue;
            }
            const char escape{take()};
            switch (escape) {
            case 'n':
                value += '\n';
                break;
            case 't':
                value += '\t';
                break;
            case 'r':
                value += '\r';
                break;
            case 'b':
                value += '\b';
                break;
            case 'f':
                value += '\f';
                break;
            case 'u': {
                std::string digits;
                for (int i{0}; i < 4; ++i) {
                    digits += take();
                }
                const auto code{std::stoul(digits, nullptr, 16)};
                // 记录中的转义只用于控制字符；其余内容以原始 UTF-8 字节保存。
                value += code < 0x80 ? static_cast<char>(code) : '?';
                break;
            }
            default:
                value += escape;
                break;
            }
        }
    }
    Json parseValue() {
        skipSpace();
        check(position_ < raw_.size(), "JSON 意外结束");
        Json value;
        const char c{raw_[position_]};
        if (c == '"') {
            value.kind = Json::Kind::String;
            value.text = parseString();
        } else if (c == '{') {
            ++position_;
            value.kind = Json::Kind::Object;
            if (!consume('}')) {
                do {
                    skipSpace();
                    const auto key{parseString()};
                    check(consume(':'), "JSON 对象缺少冒号");
                    value.members[key] = parseValue();
                } while (consume(','));
                check(consume('}'), "JSON 对象缺少右括号");
            }
        } else if (c == '[') {
            ++position_;
            value.kind = Json::Kind::Array;
            if (!consume(']')) {
                do {
                    value.items.push_back(parseValue());
                } while (consume(','));
                check(consume(']'), "JSON 数组缺少右括号");
            }
        } else if (raw_.compare(position_, 4, "true") == 0) {
            position_ += 4;
            value.kind = Json::Kind::Boolean;
            value.text = "1";
        } else if (raw_.compare(position_, 5, "false") == 0) {
            position_ += 5;
            value.kind = Json::Kind::Boolean;
            value.text = "0";
        } else if (raw_.compare(position_, 4, "null") == 0) {
            position_ += 4;
            value.kind = Json::Kind::Null;
        } else {
            value.kind = Json::Kind::Number;
            while (position_ < raw_.size() && std::string("+-.eE0123456789").find(raw_[position_]) != std::string::npos) {
                value.text += raw_[position_++];
            }
            check(!value.text.empty(), "JSON 出现无法识别的值");
        }
        return value;
    }
    const std::string &raw_;
    std::size_t position_{};
};

/** @brief 必须完全一致才允许做提速比较的身份项；不一致即不是单变量对照。 */
const std::vector<std::string> &comparableKeys() {
    static const std::vector<std::string> keys{
        "board",         "machine",       "system",       "kernel",          "hostname",
        "cpu_affinity",  "video_sha256",  "model_0_sha256", "model_1_sha256", "model_2_sha256",
        "rknn_library_path", "rknn_library_sha256", "precision_status", "code_commit", "code_snapshot_sha256",
        "build_flags",   "compiler",      "opencv",       "ffmpeg_avcodec",  "ffmpeg_avformat",
        "jpeg_version",  "pts_time_base"};
    return keys;
}

/** @brief 缺失即无法证明单变量对照的核心身份项；空值不允许通过。 */
const std::vector<std::string> &requiredKeys() {
    static const std::vector<std::string> keys{"board",          "machine",        "video_sha256",
                                               "model_0_sha256", "model_1_sha256", "model_2_sha256",
                                               "code_commit",    "code_snapshot_sha256"};
    return keys;
}

/**
 * @brief 从模型属性描述中取出 Runtime 与驱动版本。
 *
 * 该描述整体在两条路径下不同（直连会多记录原生属性），但版本必须一致。
 */
std::string runtimeVersion(const std::string &description) {
    const auto position{description.find("runtime=")};
    if (position == std::string::npos) {
        return {};
    }
    const auto end{description.find('\n', position)};
    return description.substr(position, end == std::string::npos ? std::string::npos : end - position);
}

std::string text(const Json *node) {
    if (!node) {
        return {};
    }
    if (node->kind == Json::Kind::Array) {
        std::string joined{"["};
        for (const auto &item : node->items) {
            joined += text(&item) + ",";
        }
        return joined + "]";
    }
    return node->text;
}

Run readRun(const std::filesystem::path &directory) {
    Run run;
    run.path = directory;
    const auto raw{baseline::readText(directory / "manifest.json")};
    JsonReader reader{raw};
    const auto manifest{reader.parse()};
    run.status = text(manifest.find("status"));
    check(run.status == "complete", "只有 complete 的运行可用于性能比较: " + directory.string() + " 状态 " + run.status);
    const auto *config{manifest.find("config")};
    const auto *identity{manifest.find("identity")};
    check(config && identity, "manifest 缺少配置或身份: " + directory.string());
    run.detailed = text(config->find("detailed_observation")) == "1";
    run.variant = text(identity->find("benchmark_variant"));
    for (const auto &key : comparableKeys()) {
        run.identity[key] = text(identity->find(key));
    }
    run.identity["runtime_driver"] = runtimeVersion(text(identity->find("model_attributes_runtime_driver")));
    for (const char *key : {"bbox", "warmup_updates", "max_updates", "repeats", "environment_interval_ms"}) {
        run.identity[std::string("config.") + key] = text(config->find(key));
    }
    for (const auto &key : requiredKeys()) {
        check(!run.identity.at(key).empty(), "缺少可比身份字段 " + key + ": " + directory.string());
    }
    check(!run.identity.at("runtime_driver").empty(), "缺少 Runtime/驱动版本记录: " + directory.string());
    for (const auto &frame : rows(directory / "frames.csv")) {
        if (frame.at("phase") != "measure" || frame.at("status") != "success" || frame.at("operation") != "track") {
            continue;
        }
        check(!frame.at("decode_to_result_ns").empty(), "正式更新缺少端到端耗时: " + directory.string());
        const auto value{std::stoll(frame.at("decode_to_result_ns"))};
        run.total.push_back(value);
        run.repeats[frame.at("repeat_id")].push_back(value);
    }
    check(!run.total.empty(), "没有可比较的正式更新: " + directory.string());
    if (run.detailed) {
        std::set<std::string> measured;
        for (const auto &frame : rows(directory / "frames.csv")) {
            if (frame.at("phase") == "measure" && frame.at("status") == "success" &&
                frame.at("operation") == "track") {
                measured.insert(frame.at("repeat_id") + "/" + frame.at("frame_index"));
            }
        }
        for (const auto &span : rows(directory / "spans.csv")) {
            if (!measured.count(span.at("repeat_id") + "/" + span.at("frame_index")) ||
                span.at("model_role").empty()) {
                continue;
            }
            run.stages[span.at("model_role") + "/" + span.at("stage")].push_back(
                std::stoll(span.at("end_ns")) - std::stoll(span.at("start_ns")));
        }
    }
    return run;
}

void writeDistribution(std::ostream &out, const std::string &name, const Distribution &d) {
    out << baseline::jsonString(name) << ":{\"count\":" << d.count;
    if (d.count) {
        out << ",\"mean\":" << baseline::jsonString(baseline::number(d.mean))
            << ",\"standard_deviation\":" << baseline::jsonString(baseline::number(d.standard_deviation))
            << ",\"minimum\":\"" << d.minimum << "\",\"maximum\":\"" << d.maximum << "\",\"p50\":\"" << d.p50
            << "\",\"p90\":\"" << d.p90 << "\",\"p95\":\"" << d.p95 << "\",\"p99\":\"" << d.p99 << "\"";
    }
    out << '}';
}

std::string sideName(const std::string &variant) { return variant.empty() ? "baseline" : variant; }
} // namespace

int main(int argc, char **argv) {
    try {
        std::vector<std::filesystem::path> baseline_runs, direct_runs;
        std::filesystem::path report;
        std::size_t minimum_rounds{3};
        for (int i{1}; i < argc; ++i) {
            const std::string option{argv[i]};
            check(i + 1 < argc, "选项 " + option + " 缺少参数");
            const std::string value{argv[++i]};
            if (option == "--baseline") {
                baseline_runs.emplace_back(value);
            } else if (option == "--direct") {
                direct_runs.emplace_back(value);
            } else if (option == "--report") {
                report = value;
            } else if (option == "--min-rounds") {
                minimum_rounds = static_cast<std::size_t>(std::stoul(value));
                check(minimum_rounds >= 1, "最低轮次必须为正");
            } else {
                check(false, "未知选项 " + option);
            }
        }
        check(!baseline_runs.empty() && !direct_runs.empty(),
              "用法：nanotrackv3_benchmark_compare --baseline 目录 [...] --direct 目录 [...] "
              "[--min-rounds 3] [--report 文件.md]");
        std::vector<Run> runs;
        for (const auto &path : baseline_runs) {
            auto run{readRun(path)};
            check(run.variant.empty(), "标记为 " + run.variant + " 的运行不能作为 baseline 侧: " + path.string());
            runs.push_back(std::move(run));
        }
        for (const auto &path : direct_runs) {
            auto run{readRun(path)};
            check(run.variant == "search_direct",
                  "缺少 search_direct 变体标记的运行不能作为直连侧: " + path.string());
            runs.push_back(std::move(run));
        }
        // 身份或配置不一致时不输出可比提速结论。
        for (std::size_t i{1}; i < runs.size(); ++i) {
            for (const auto &entry : runs[0].identity) {
                const auto &actual{runs[i].identity.at(entry.first)};
                check(actual == entry.second, "不满足单变量对照：" + entry.first + " 在 " + runs[0].path.string() +
                                                  " 为 " + baseline::jsonString(entry.second) + "，在 " +
                                                  runs[i].path.string() + " 为 " + baseline::jsonString(actual));
            }
        }
        std::map<std::string, std::vector<std::int64_t>> groups;
        std::map<std::string, std::map<std::string, std::vector<std::int64_t>>> group_stages;
        std::map<std::string, std::size_t> group_rounds;
        for (const auto &run : runs) {
            const std::string group{sideName(run.variant) + "/" + (run.detailed ? "observed" : "total_only")};
            ++group_rounds[group];
            auto &samples{groups[group]};
            samples.insert(samples.end(), run.total.begin(), run.total.end());
            for (const auto &stage : run.stages) {
                auto &values{group_stages[group][stage.first]};
                values.insert(values.end(), stage.second.begin(), stage.second.end());
            }
        }
        // 细分观测开启与关闭必须在两侧都有数据，且每组达到最低轮次，否则不是完整对照。
        for (const char *side : {"baseline", "search_direct"}) {
            for (const char *mode : {"observed", "total_only"}) {
                const std::string group{std::string(side) + "/" + mode};
                const auto rounds{group_rounds[group]};
                check(rounds >= minimum_rounds, "分组 " + group + " 只有 " + std::to_string(rounds) +
                                                    " 轮，少于要求的 " + std::to_string(minimum_rounds) + " 轮");
            }
        }
        std::ostringstream json;
        json << "{\"schema_version\":1,\"status\":\"passed\",\"comparable\":true,\"minimum_rounds\":"
             << minimum_rounds << ",\"identity\":{";
        bool first{true};
        for (const auto &entry : runs[0].identity) {
            json << (first ? "" : ",") << baseline::jsonString(entry.first) << ':'
                 << baseline::jsonString(entry.second);
            first = false;
        }
        json << "},\"runs\":[";
        for (std::size_t i{0}; i < runs.size(); ++i) {
            const auto &run{runs[i]};
            json << (i ? "," : "") << "{\"directory\":" << baseline::jsonString(run.path.string())
                 << ",\"benchmark_variant\":" << baseline::jsonString(sideName(run.variant))
                 << ",\"detailed_observation\":" << (run.detailed ? 1 : 0) << ",\"updates\":" << run.total.size()
                 << ",\"repeats\":{";
            bool first_repeat{true};
            for (const auto &repeat : run.repeats) {
                json << (first_repeat ? "" : ",");
                first_repeat = false;
                writeDistribution(json, repeat.first, baseline::distribution(repeat.second));
            }
            json << "},\"combined\":";
            std::ostringstream combined;
            writeDistribution(combined, "decode_to_result_ns", baseline::distribution(run.total));
            json << '{' << combined.str() << "}}";
        }
        json << "],\"groups\":{";
        first = true;
        for (const auto &group : groups) {
            json << (first ? "" : ",") << baseline::jsonString(group.first) << ":{";
            first = false;
            std::ostringstream body;
            writeDistribution(body, "decode_to_result_ns", baseline::distribution(group.second));
            json << body.str();
            for (const auto &stage : group_stages[group.first]) {
                json << ',';
                writeDistribution(json, "span/" + stage.first, baseline::distribution(stage.second));
            }
            json << '}';
        }
        json << "},\"comparison\":{";
        std::ostringstream markdown;
        markdown << "# NanoTrackV3 搜索直连与基线对照\n\n"
                 << "两侧使用同一块板卡、同一组模型、同一段录像和同一首帧框；任一身份不同都不会生成本报告。\n"
                 << "延迟单位为纳秒，指标为 `decode_to_result`，即从当前帧开始解码到框与分数可读。\n\n";
        first = true;
        for (const char *mode : {"observed", "total_only"}) {
            const std::string left{std::string("baseline/") + mode};
            const std::string right{std::string("search_direct/") + mode};
            if (!groups.count(left) || !groups.count(right)) {
                continue;
            }
            const auto a{baseline::distribution(groups[left])};
            const auto b{baseline::distribution(groups[right])};
            const double ratio{b.mean > 0 ? a.mean / b.mean : 0.0};
            json << (first ? "" : ",") << baseline::jsonString(mode) << ":{\"baseline_mean_ns\":"
                 << baseline::jsonString(baseline::number(a.mean))
                 << ",\"search_direct_mean_ns\":" << baseline::jsonString(baseline::number(b.mean))
                 << ",\"mean_difference_ns\":" << baseline::jsonString(baseline::number(a.mean - b.mean))
                 << ",\"speedup\":" << baseline::jsonString(baseline::number(ratio))
                 << ",\"faster\":" << (b.mean < a.mean ? "1" : "0") << '}';
            first = false;
            markdown << "## 观测模式 " << (std::string(mode) == "observed" ? "细分观测开启" : "仅总计时") << "\n\n"
                     << "| 侧 | 样本 | 均值 | P50 | P90 | P95 | P99 |\n|---|---:|---:|---:|---:|---:|---:|\n"
                     << "| baseline | " << a.count << " | " << baseline::number(a.mean) << " | " << a.p50 << " | "
                     << a.p90 << " | " << a.p95 << " | " << a.p99 << " |\n"
                     << "| search_direct | " << b.count << " | " << baseline::number(b.mean) << " | " << b.p50
                     << " | " << b.p90 << " | " << b.p95 << " | " << b.p99 << " |\n\n"
                     << "均值差值：" << baseline::number(a.mean - b.mean) << " 纳秒；倍数："
                     << baseline::number(ratio) << "。"
                     << (b.mean < a.mean ? "直连更快。" : "直连没有取得性能收益，结果如实保留。") << "\n\n";
        }
        json << "},\"observation_overhead_ns\":{";
        first = true;
        for (const char *side : {"baseline", "search_direct"}) {
            const std::string observed{std::string(side) + "/observed"};
            const std::string plain{std::string(side) + "/total_only"};
            if (!groups.count(observed) || !groups.count(plain)) {
                continue;
            }
            const auto difference{baseline::distribution(groups[observed]).mean -
                                  baseline::distribution(groups[plain]).mean};
            json << (first ? "" : ",") << baseline::jsonString(side) << ':'
                 << baseline::jsonString(baseline::number(difference));
            first = false;
            markdown << "观测开销（" << side << "）：细分观测比仅总计时多 " << baseline::number(difference)
                     << " 纳秒。观测开销不从原始数据中扣除。\n\n";
        }
        json << "},\"bind_cost_ns\":{";
        first = true;
        for (const auto &group : group_stages) {
            for (const auto &stage : group.second) {
                if (stage.first.find("/io_mem_bind") == std::string::npos) {
                    continue;
                }
                const auto d{baseline::distribution(stage.second)};
                json << (first ? "" : ",");
                first = false;
                writeDistribution(json, group.first + "/" + stage.first, d);
                markdown << "绑定成本 " << group.first << "/" << stage.first << "：样本 " << d.count << "，均值 "
                         << baseline::number(d.mean) << " 纳秒。该时间已经包含在模型和端到端区间内。\n\n";
            }
        }
        json << "}}\n";
        markdown << "本报告只比较延迟分布，不判定跟踪质量，也不宣称设备内部完全没有数据搬运。\n";
        std::cout << json.str();
        if (!report.empty()) {
            std::ofstream out;
            out.exceptions(std::ios::badbit | std::ios::failbit);
            out.open(report, std::ios::binary);
            out << markdown.str();
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

#include "statistics.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <numeric>
#include <sstream>
#include <stdexcept>
namespace baseline {
bool readCsvRecord(std::istream &input, std::vector<std::string> &row) {
    row.clear();
    std::string field;
    bool quoted{false}, started{false};
    char c{};
    while (input.get(c)) {
        started = true;
        if (c == '"') {
            if (quoted && input.peek() == '"') {
                input.get(c);
                field += '"';
            } else {
                quoted = !quoted;
            }
        } else if (c == ',' && !quoted) {
            row.push_back(field);
            field.clear();
        } else if (c == '\n' && !quoted) {
            row.push_back(field);
            return true;
        } else {
            field += c;
        }
    }
    if (quoted || started || input.bad()) {
        throw std::runtime_error("CSV 尾行不完整或读取失败");
    }
    return false;
}
Distribution distribution(std::vector<std::int64_t> samples) {
    Distribution result;
    result.count = samples.size();
    if (samples.empty()) {
        return result;
    }
    std::sort(samples.begin(), samples.end());
    long double total{};
    for (const auto value : samples) {
        total += value;
    }
    const long double mean{total / samples.size()};
    long double squared{};
    for (const auto value : samples) {
        squared += (value - mean) * (value - mean);
    }
    result.mean = static_cast<double>(mean);
    result.standard_deviation = static_cast<double>(std::sqrt(squared / samples.size()));
    result.minimum = samples.front();
    result.maximum = samples.back();
    const auto percentile{
        [&](double p) { return samples.at(static_cast<std::size_t>(std::ceil(p * samples.size())) - 1); }};
    result.p50 = percentile(0.50);
    result.p90 = percentile(0.90);
    result.p95 = percentile(0.95);
    result.p99 = percentile(0.99);
    return result;
}
namespace {
using Groups = std::map<std::string, std::map<std::string, std::vector<std::int64_t>>>;
void readRows(const std::filesystem::path &path, const std::function<void(const CsvRow &)> &consume) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("统计输入无法打开");
    }
    std::vector<std::string> headers, values;
    if (!readCsvRecord(file, headers)) {
        throw std::runtime_error("CSV 表头缺失");
    }
    while (readCsvRecord(file, values)) {
        if (values.size() != headers.size()) {
            throw std::runtime_error("CSV 列数不匹配");
        }
        CsvRow row;
        for (std::size_t i{0}; i < headers.size(); ++i) {
            row[headers[i]] = values[i];
        }
        consume(row);
    }
}
std::string frameKey(const CsvRow &row) {
    return row.at("repeat_id") + "/" + row.at("phase") + "/" + row.at("frame_index");
}
} // namespace
std::string jsonForOpenCv(const std::string &raw) {
    static const std::array<std::pair<const char *, const char *>, 3> literals{
        {{"null", "\"__null__\""}, {"true", "1"}, {"false", "0"}}};
    std::string result;
    bool quoted{false}, escaped{false};
    for (std::size_t i{0}; i < raw.size(); ++i) {
        const char c{raw[i]};
        if (!quoted) {
            bool replaced{false};
            for (const auto &literal : literals) {
                const std::size_t length{std::strlen(literal.first)};
                if (raw.compare(i, length, literal.first) == 0) {
                    result += literal.second;
                    i += length - 1;
                    replaced = true;
                    break;
                }
            }
            if (replaced) {
                continue;
            }
        } else if (!escaped && c == '\\' && i + 5 < raw.size() && raw[i + 1] == 'u') {
            // OpenCV 不支持 \uXXXX 转义。控制字符改写成可见的 <u+XXXX>，
            // 保留差异但不再是控制字符；这些字段只用于展示和一致性比较。
            result += "<u+";
            result.append(raw, i + 2, 4);
            result += '>';
            i += 5;
            continue;
        }
        result += c;
        if (c == '\\' && quoted && !escaped) {
            escaped = true;
            continue;
        }
        if (c == '"' && !escaped) {
            quoted = !quoted;
        }
        escaped = false;
    }
    return result;
}
std::size_t writeReports(const std::filesystem::path &directory, const std::string &status, const std::string &reason,
                         const std::string &variant, const std::map<std::string, std::string> &identity) {
    Groups groups;
    std::map<std::string, std::string> frame_groups;
    std::map<std::string, std::size_t> counts;
    std::size_t valid{}, failed{}, warmup{}, initializations{};
    const std::array<std::string, 7> metrics{"decode_ns",      "frame_copy_ns", "preprocess_ns",      "inference_ns",
                                             "postprocess_ns", "other_ns",      "decode_to_result_ns"};
    readRows(directory / "frames.csv", [&](const CsvRow &row) {
        const bool measure{row.at("phase") == "measure"};
        const bool ok{row.at("status") == "success"};
        const std::string kind{ok ? row.at("operation") : "failed"};
        const std::string group{row.at("repeat_id") + "/" + kind};
        if (!measure) {
            ++warmup;
            return;
        }
        if (!ok) {
            ++failed;
        } else if (kind == "track") {
            ++valid;
        } else {
            ++initializations;
        }
        for (const auto *category : {"init", "track", "failed"}) {
            for (const auto &metric : metrics) {
                groups[row.at("repeat_id") + "/" + category][metric];
            }
        }
        frame_groups[frameKey(row)] = group;
        for (const auto &key : {group, "all/" + kind}) {
            ++counts[key];
            for (const auto &metric : metrics) {
                auto &samples{groups[key][metric]};
                if (!row.at(metric).empty()) {
                    samples.push_back(std::stoll(row.at(metric)));
                }
            }
        }
    });
    readRows(directory / "spans.csv", [&](const CsvRow &row) {
        const auto frame{frame_groups.find(frameKey(row))};
        if (frame == frame_groups.end()) {
            return;
        }
        const auto slash{frame->second.find('/')};
        const std::string kind{frame->second.substr(slash + 1)};
        const std::string metric{"span/" + row.at("model_role") + "/" + row.at("stage")};
        const auto duration{std::stoll(row.at("end_ns")) - std::stoll(row.at("start_ns"))};
        if (duration < 0) {
            throw std::runtime_error("负计时区间");
        }
        for (const auto &key : {frame->second, "all/" + kind}) {
            groups[key][metric].push_back(duration);
            groups[key][metric + "/residual"].push_back(std::stoll(row.at("residual_ns")));
        }
    });
    for (const auto &kind : {"init", "track", "failed"}) {
        for (const auto &metric : metrics) {
            groups[std::string("all/") + kind][metric];
        }
    }
    std::ofstream json, markdown;
    json.exceptions(std::ios::badbit | std::ios::failbit);
    markdown.exceptions(std::ios::badbit | std::ios::failbit);
    json.open(directory / "summary.json");
    markdown.open(directory / "report.md");
    json << "{\"schema_version\":1,\"status\":" << jsonString(status) << ",\"reason\":" << jsonString(reason);
    if (!variant.empty()) {
        json << ",\"benchmark_variant\":" << jsonString(variant);
    }
    if (!variant.empty()) {
        for (const char *key : {"search_handoff", "bind_policy", "model_attributes_runtime_driver",
                               "search_native_output", "head_native_search_input", "shared_feature_bytes"}) {
            const auto found{identity.find(key)};
            if (found != identity.end()) {
                json << ',' << jsonString(found->first) << ':';
                if (found->first == "model_attributes_runtime_driver") {
                    // 按行保存完整描述，避免旧 OpenCV 记录核对器的单字符串长度上限。
                    json << '[';
                    std::istringstream lines{found->second};
                    std::string line;
                    bool first{true};
                    while (std::getline(lines, line)) {
                        if (!first) {
                            json << ',';
                        }
                        first = false;
                        json << jsonString(line);
                    }
                    json << ']';
                } else {
                    json << jsonString(found->second);
                }
            }
        }
    }
    json << ",\"valid_updates\":" << valid << ",\"failed_frames\":" << failed
         << ",\"initializations\":" << initializations << ",\"warmup_frames\":" << warmup << ",\"groups\":{";
    markdown << "# NanoTrackV3 性能基线\n\n";
    if (!variant.empty()) {
        markdown << "执行路径变体：`benchmark_variant=" << variant << "`。该变体只改变模型之间的特征交接方式，"
                 << "其余解码、前处理、模型次数和统计口径与基线一致。\n\n";
        markdown << "## 执行路径身份\n\n";
        for (const char *key : {"search_handoff", "bind_policy", "model_attributes_runtime_driver",
                               "search_native_output", "head_native_search_input", "shared_feature_bytes"}) {
            const auto found{identity.find(key)};
            if (found != identity.end()) {
                markdown << "### " << key << "\n\n";
                // 缩进代码块保留属性换行，并避免模型名称中的 Markdown 标记改变报告结构。
                std::istringstream lines{found->second};
                std::string line;
                while (std::getline(lines, line)) {
                    markdown << "    " << line << '\n';
                }
                markdown << '\n';
            }
        }
    }
    markdown << "状态：" << status << "；原因：" << reason << "。\n\n有效正式更新 " << valid << "，正式初始化 "
             << initializations << "，正式失败 " << failed << "，预热记录 " << warmup
             << "。低分有效结果保留。\n\n"
                "唯一端到端延迟为 `decode_to_result`（从开始解码到结果可读）。表中时长单位为纳秒。"
                "父区间与子区间不能相加；residual 表示父区间扣除直接子区间的剩余耗时。"
                "初始化、更新、失败分别统计，预热不参与统计。\n\n"
                "分位数按排序后第 ceil(p×N) 项计算，标准差为总体标准差。"
                "解码至结果处理速率按有效更新数除以总延迟计算，不包含读盘、写盘和等待。"
                "本报告不判定跟踪质量，也不代表 Jetson 等价性或相机系统验收。\n";
    bool first_group{true};
    std::vector<double> repeat_means;
    for (const auto &group : groups) {
        if (!first_group) {
            json << ',';
        }
        first_group = false;
        json << jsonString(group.first) << ":{\"frames\":" << counts[group.first] << ",\"metrics\":{";
        markdown << "\n## 分组 " << group.first
                 << "\n\n"
                    "| 阶段 | 样本数 | 均值 | 标准差 | 最小 | 最大 | P50 | P90 | P95 | P99 |\n"
                    "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
        bool first_metric{true};
        for (const auto &metric : group.second) {
            const auto d{distribution(metric.second)};
            if (!first_metric) {
                json << ',';
            }
            first_metric = false;
            json << jsonString(metric.first) << ":{\"count\":" << d.count;
            markdown << "| " << metric.first << " | " << d.count;
            const std::array<std::pair<const char *, std::string>, 8> values{
                {{"mean", number(d.mean)},
                 {"standard_deviation", number(d.standard_deviation)},
                 {"minimum", std::to_string(d.minimum)},
                 {"maximum", std::to_string(d.maximum)},
                 {"p50", std::to_string(d.p50)},
                 {"p90", std::to_string(d.p90)},
                 {"p95", std::to_string(d.p95)},
                 {"p99", std::to_string(d.p99)}}};
            for (const auto &value : values) {
                json << ',' << jsonString(value.first) << ':' << (d.count ? jsonString(value.second) : "null");
                markdown << " | " << (d.count ? value.second : "—");
            }
            json << '}';
            markdown << " |\n";
        }
        json << "},\"decode_to_result_processing_fps\":";
        const auto &total{group.second.at("decode_to_result_ns")};
        const bool tracks{group.first.substr(group.first.find('/') + 1) == "track"};
        const long double sum{std::accumulate(total.begin(), total.end(), 0.0L)};
        if (tracks && !total.empty() && sum > 0) {
            const double fps{static_cast<double>(total.size() * 1e9L / sum)};
            json << number(fps);
            markdown << "\n解码至结果处理速率：" << number(fps) << " 帧/秒。\n";
            if (group.first.rfind("all/", 0) != 0) {
                repeat_means.push_back(static_cast<double>(sum / total.size()));
            }
        } else {
            json << "null";
        }
        json << '}';
    }
    json << "},\"repeat_update_mean_range_ns\":";
    if (repeat_means.empty()) {
        json << "null";
    } else {
        const auto range{std::minmax_element(repeat_means.begin(), repeat_means.end())};
        json << '[' << jsonString(number(*range.first)) << ',' << jsonString(number(*range.second)) << ']';
        markdown << "\n各轮更新均值范围：" << number(*range.first) << "～" << number(*range.second) << " 纳秒。\n";
    }
    json << "}\n";
    json.close();
    markdown.close();
    return valid;
}
} // namespace baseline

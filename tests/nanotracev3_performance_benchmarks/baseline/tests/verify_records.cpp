#include "statistics.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
namespace {
using namespace baseline;
using Samples = std::map<std::string, std::map<std::string, std::vector<std::int64_t>>>;
void check(bool ok, const std::string &message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}
std::vector<CsvRow> rows(const std::filesystem::path &path) {
    std::ifstream input(path);
    check(bool(input), "无法打开 " + path.string());
    std::vector<std::string> header, cells;
    check(readCsvRecord(input, header), "表头缺失");
    std::vector<CsvRow> result;
    while (readCsvRecord(input, cells)) {
        check(cells.size() == header.size(), "列数错误");
        CsvRow row;
        for (std::size_t i{0}; i < cells.size(); ++i) {
            row.emplace(header[i], cells[i]);
        }
        result.push_back(std::move(row));
    }
    return result;
}
std::string key(const CsvRow &row) { return row.at("repeat_id") + "/" + row.at("phase") + "/" + row.at("frame_index"); }
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 2 || argc == 3, "用法：baseline_verify_records RUN_DIRECTORY [REFERENCE_DIRECTORY]");
        const std::filesystem::path root{argv[1]};
        const auto frames{rows(root / "frames.csv")};
        const auto spans{rows(root / "spans.csv")};
        // OpenCV 4.5 的 JSON 读取器不接受标准 null；仅在内存解析副本中替换该字面量。
        const auto raw_json{readText(root / "summary.json")};
        std::string parse_json;
        bool quoted{false}, escaped{false};
        for (std::size_t i{0}; i < raw_json.size(); ++i) {
            const char c{raw_json[i]};
            if (!quoted && raw_json.compare(i, 4, "null") == 0) {
                parse_json += "\"__null__\"";
                i += 3;
                continue;
            }
            parse_json += c;
            if (c == '\\' && quoted && !escaped) {
                escaped = true;
                continue;
            }
            if (c == '\"' && !escaped) {
                quoted = !quoted;
            }
            escaped = false;
        }
        cv::FileStorage summary(parse_json,
                                cv::FileStorage::READ | cv::FileStorage::MEMORY | cv::FileStorage::FORMAT_JSON);
        check(summary.isOpened(), "JSON 汇总无法打开");
        Samples samples;
        std::map<std::string, const CsvRow *> frame_index;
        std::map<std::string, const CsvRow *> span_index;
        std::map<std::string, std::vector<const CsvRow *>> child_index;
        std::map<std::string, std::map<std::string, int>> calls;
        std::size_t valid{}, failed{}, warmup{}, init{};
        for (const auto &frame : frames) {
            check(frame_index.emplace(key(frame), &frame).second, "重复帧身份");
            if (frame.at("phase") != "measure") {
                ++warmup;
                continue;
            }
            const std::string kind{frame.at("status") == "success" ? frame.at("operation") : "failed"};
            if (kind == "track") {
                ++valid;
            } else if (kind == "init") {
                ++init;
            } else {
                ++failed;
            }
            for (const auto &field : frame) {
                if (field.first.size() < 3 || field.first.substr(field.first.size() - 3) != "_ns") {
                    continue;
                }
                for (const auto &group : {frame.at("repeat_id") + "/" + kind, "all/" + kind}) {
                    auto &values{samples[group][field.first]};
                    if (!field.second.empty()) {
                        values.push_back(std::stoll(field.second));
                    }
                }
            }
            if (!frame.at("decode_to_result_ns").empty()) {
                std::int64_t sum{};
                for (const auto &field :
                     {"decode_ns", "frame_copy_ns", "preprocess_ns", "inference_ns", "postprocess_ns", "other_ns"}) {
                    if (!frame.at(field).empty()) {
                        sum += std::stoll(frame.at(field));
                    }
                }
                check(sum == std::stoll(frame.at("decode_to_result_ns")), "顶层阶段与总计时不相等");
            }
            if (kind == "init") {
                check(frame.at("score").empty() && frame.at("postprocess_ns").empty(), "初始化伪造分数或后处理");
            }
        }
        for (const auto &span : spans) {
            check(span_index.emplace(span.at("span_id"), &span).second, "重复区间 ID");
            child_index[span.at("parent_id")].push_back(&span);
        }
        for (const auto &span : spans) {
            const auto start{std::stoll(span.at("start_ns"))}, end{std::stoll(span.at("end_ns"))};
            check(end >= start, "负计时");
            const auto parent{span.at("parent_id")};
            if (!parent.empty()) {
                check(span_index.count(parent) == 1, "父区间缺失");
                const auto &p{*span_index.at(parent)};
                check(key(p) == key(span) && std::stoll(p.at("start_ns")) <= start && std::stoll(p.at("end_ns")) >= end,
                      "区间不属于父帧或超出父区间");
            }
            std::int64_t children{};
            std::int64_t previous_end{start};
            for (const auto *child_pointer : child_index[span.at("span_id")]) {
                const auto &child{*child_pointer};
                {
                    check(std::stoll(child.at("start_ns")) >= previous_end, "同层区间重叠");
                    previous_end = std::stoll(child.at("end_ns"));
                    children += previous_end - std::stoll(child.at("start_ns"));
                }
            }
            check(end - start - children == std::stoll(span.at("residual_ns")), "residual 不一致");
            const auto &frame{*frame_index.at(key(span))};
            if (span.at("stage") == "template_backbone" || span.at("stage") == "search_backbone" ||
                span.at("stage") == "head") {
                ++calls[key(span)][span.at("stage")];
                check(span.at("model_role") == span.at("stage"), "模型角色错误");
            }
            if (frame.at("phase") != "measure") {
                continue;
            }
            const std::string kind{frame.at("status") == "success" ? frame.at("operation") : "failed"};
            const auto metric{"span/" + span.at("model_role") + "/" + span.at("stage")};
            for (const auto &group : {frame.at("repeat_id") + "/" + kind, "all/" + kind}) {
                samples[group][metric].push_back(end - start);
                samples[group][metric + "/residual"].push_back(end - start - children);
            }
        }
        if (!calls.empty()) {
            for (const auto &frame : frames) {
                if (frame.at("status") != "success") {
                    continue;
                }
                const auto &c{calls[key(frame)]};
                const auto count{[&](const char *name) {
                    const auto it{c.find(name)};
                    return it == c.end() ? 0 : it->second;
                }};
                const bool initialization{frame.at("operation") == "init"};
                check(count("template_backbone") == (initialization ? 1 : 0) &&
                          count("search_backbone") == (initialization ? 0 : 1) &&
                          count("head") == (initialization ? 0 : 1),
                      "模型调用次数不符");
            }
        }
        check(int(summary["valid_updates"]) == static_cast<int>(valid) &&
                  int(summary["failed_frames"]) == static_cast<int>(failed) &&
                  int(summary["initializations"]) == static_cast<int>(init) &&
                  int(summary["warmup_frames"]) == static_cast<int>(warmup),
              "汇总帧数不一致");
        std::size_t metrics{};
        for (const auto &group : summary["groups"]) {
            const auto name{group.name()};
            for (const auto &metric : group["metrics"]) {
                auto values{samples[name][metric.name()]};
                std::sort(values.begin(), values.end());
                check(int(metric["count"]) == static_cast<int>(values.size()), "统计样本数不同");
                if (values.empty()) {
                    continue;
                }
                const long double sum{std::accumulate(values.begin(), values.end(), 0.0L)};
                const long double mean{sum / values.size()};
                long double variance{};
                for (const auto value : values) {
                    variance += (value - mean) * (value - mean);
                }
                const auto compare{[&](const char *field, long double expected) {
                    const long double actual{std::stold(metric[field].string())};
                    check(std::abs(actual - expected) <= std::max(0.001L, std::abs(expected) * 1e-12L),
                          "统计重算不符: " + name + "/" + metric.name() + "/" + field);
                }};
                compare("mean", mean);
                compare("standard_deviation", std::sqrt(variance / values.size()));
                compare("minimum", values.front());
                compare("maximum", values.back());
                for (const auto &p : std::array<std::pair<const char *, int>, 4>{
                         {{"p50", 50}, {"p90", 90}, {"p95", 95}, {"p99", 99}}}) {
                    compare(p.first, values[(values.size() * p.second + 99) / 100 - 1]);
                }
                ++metrics;
            }
            if (name.substr(name.find('/') + 1) == "track") {
                const auto &v{samples[name]["decode_to_result_ns"]};
                const long double total{std::accumulate(v.begin(), v.end(), 0.0L)};
                if (total > 0) {
                    check(std::abs(double(group["decode_to_result_processing_fps"]) - v.size() * 1e9L / total) < 1e-9,
                          "速率重算不符");
                }
            }
        }
        double max_box{}, max_score{};
        if (argc == 3) {
            const auto reference{rows(std::filesystem::path(argv[2]) / "frames.csv")};
            check(reference.size() == frames.size(), "对照帧数不同");
            for (std::size_t i{0}; i < frames.size(); ++i) {
                check(key(frames[i]) == key(reference[i]) && frames[i].at("status") == reference[i].at("status"),
                      "对照身份不同");
                for (const auto *field : {"x", "y", "width", "height", "score"}) {
                    if (frames[i].at(field).empty()) {
                        check(reference[i].at(field).empty(), "有效性不同");
                        continue;
                    }
                    const double delta{std::abs(std::stod(frames[i].at(field)) - std::stod(reference[i].at(field)))};
                    if (std::string(field) == "score") {
                        max_score = std::max(max_score, delta);
                    } else {
                        max_box = std::max(max_box, delta);
                    }
                }
            }
            check(max_box <= 1e-4 && max_score <= 1e-6, "真实有无观测数值超过容差");
        }
        std::cout << "{\"status\":\"passed\",\"frames\":" << frames.size() << ",\"spans\":" << spans.size()
                  << ",\"metrics_checked\":" << metrics << ",\"max_bbox_error\":" << number(max_box)
                  << ",\"max_score_error\":" << number(max_score) << "}\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

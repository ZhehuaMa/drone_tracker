#include "storage.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <tuple>

namespace diagnostics {
const char *typeName(Type type) {
    static constexpr const char *names[]{"运行开始",   "运行结束", "轮次开始", "轮次结束",   "采集",       "节点跳过",
                                         "节点选择",   "调用开始", "调用结束", "缓存追加",   "缓存淘汰",   "缓存查找",
                                         "未投递",     "请求决策", "检测开始", "检测阶段",   "检测结束",   "结果读取",
                                         "结果覆盖",   "请求取消", "仲裁决策", "初始化开始", "初始化结束", "补跟踪步骤",
                                         "补跟踪汇总", "输出",     "发布开始", "发布结束",   "跳过发布",   "阶段耗时",
                                         "诊断缺口",   "异常结束", "时钟对应", "健康", "跳变比较", "SourceContext"};
    const auto index{static_cast<std::size_t>(type)};
    return index < std::size(names) ? names[index] : "未知事件";
}
const char *reasonName(Reason reason) {
    static constexpr const char *names[]{
        "无",           "未知",           "输入无效",     "时间重复或倒退", "已停止",       "正在清理",
        "待检测槽占用", "未读结果",       "待检测槽忙",   "未到检测间隔",   "预热丢弃",     "终止丢弃",
        "驱动序号缺口", "队列满",         "取最新帧跳过", "无目标",         "低IoU",        "检测跳变",
        "跟踪跳变",     "没有新结果超限", "缓存未命中",   "初始化失败",     "图像准备失败", "推理异常",
        "发布异常",     "输入未消费",     "记录缓冲满",   "写入失败",       "同步失败",     "空间保护",
        "不完整", "跟踪更新失败", "Superseded", "ContextUnavailable", "RecoveryUnavailable", "PreviousRound"};
    const auto index{static_cast<std::size_t>(reason)};
    return index < std::size(names) ? names[index] : "未知理由";
}
void json(std::ostream &output, const Event &event) {
    output << "{\"version\":" << event.format_version << ",\"run_id\":" << event.run_id
           << ",\"round_id\":" << event.round_id << ",\"producer_id\":" << event.producer_id
           << ",\"producer_seq\":" << event.producer_seq << ",\"type\":\"" << typeName(event.type) << "\",\"reason\":\""
           << reasonName(event.reason) << "\",\"flags\":" << event.flags << ",\"mono_ns\":" << event.mono_ns
           << ",\"capture_id\":" << event.frame.capture_id << ",\"driver_sequence\":" << event.frame.driver_sequence
           << ",\"frame_flags\":" << event.frame.flags << ",\"update_id\":" << event.update_id
           << ",\"tracker_frame_id\":" << event.tracker_frame_id << ",\"request_id\":" << event.request_id
           << ",\"source_update_id\":" << event.source_update_id
           << ",\"source_tracker_frame_id\":" << event.source_tracker_frame_id
           << ",\"frame_time_us\":" << event.frame_time_us << ",\"prediction_us\":" << event.prediction_us
           << ",\"duration_ns\":" << event.duration_ns << ",\"score\":" << event.score << ",\"iou\":" << event.iou
           << ",\"threshold\":" << event.threshold << ",\"highest_score\":" << event.highest_score << ",\"box\":[";
    for (std::size_t index{}; index < event.box.size(); ++index) {
        if (index) {
            output << ',';
        }
        output << event.box[index];
    }
    output << "],\"other_box\":[";
    for (std::size_t index{}; index < event.other_box.size(); ++index) {
        if (index) {
            output << ',';
        }
        output << event.other_box[index];
    }
    output << "],\"values\":[";
    for (std::size_t index{}; index < event.values.size(); ++index) {
        if (index) {
            output << ',';
        }
        output << event.values[index];
    }
    output << "],\"counts\":[";
    for (std::size_t index{}; index < event.counts.size(); ++index) {
        if (index) {
            output << ',';
        }
        output << event.counts[index];
    }
    const bool error_known{event.type == Type::InitializeEnd && (event.flags & ErrorCodeKnown) != 0};
    output << "],\"error_code_known\":" << (error_known ? "true" : "false")
           << ",\"error_domain\":";
    if (error_known) { output << event.values[1]; } else { output << "null"; }
    output << ",\"error_code\":";
    if (error_known) { output << event.values[2]; } else { output << "null"; }
    output << "}\n";
}
} // namespace diagnostics
int main(int argc, char **argv) {
    using namespace diagnostics;
    if (argc == 2 && std::string{argv[1]} == "--protocol-version") {
        std::cout << FORMAT_VERSION << '\n';
        return 0;
    }
    if (argc != 4 || std::string{argv[2]} != "--output") {
        std::cerr << "用法: rk3576_diagnostic_inspect 原日志或目录 --output 新目录\n";
        return 2;
    }
    try {
        const auto source{std::filesystem::canonical(argv[1])};
        const auto destination{std::filesystem::absolute(argv[3]).lexically_normal()};
        if (std::filesystem::exists(destination) || destination == source) {
            throw std::runtime_error{"导出目录已存在"};
        }
        const auto source_root{std::filesystem::is_directory(source) ? source : source.parent_path()};
        for (auto parent{std::filesystem::canonical(destination.parent_path())};
             !parent.empty() && parent != parent.parent_path(); parent = parent.parent_path()) {
            if (parent == source_root) {
                throw std::runtime_error{"禁止向原日志目录内导出"};
            }
        }
        std::vector<std::filesystem::path> files{};
        if (std::filesystem::is_directory(source)) {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(source)) {
                if (entry.is_regular_file() && entry.path().extension() == ".dtd") {
                    files.push_back(entry.path());
                }
            }
        } else {
            files.push_back(source);
        }
        std::sort(files.begin(), files.end());
        std::vector<Event> events{};
        std::set<std::tuple<std::uint64_t, std::uint32_t, std::uint64_t>> identities{};
        std::size_t duplicates{};
        bool insufficient{false};
        std::filesystem::create_directory(destination);
        std::ofstream report{destination / "timeline.txt"}, lines{destination / "events.jsonl"},
            csv{destination / "events.csv"};
        std::ofstream manifest{destination / "identities.txt"};
        report << "诊断时间线：发布返回只表示调用完成，不证明订阅者收到。\n";
        for (const auto &file : files) {
            const auto result{scan(file)};
            report << "文件 " << file.filename().string() << "，完整可校验前缀 " << result.valid_bytes
                   << " 字节，提交记录 " << result.commits.size() << " 条，"
                   << (result.complete ? "正常完成标志可见" : "缺少完成标志：未知尾部") << '\n';
            if (result.damaged) {
                report << "截断或 CRC 损坏；仅接受之前完整批次。\n";
            }
            if (result.unknown_version) {
                report << "未知版本；停止解析。\n";
            }
            if (result.commits.empty()) {
                report << "成功同步位置未知，不能从文件大小推断。\n";
            } else {
                const auto &commit{result.commits.back()};
                report << "可恢复提交偏移 " << commit.data_end
                       << "；位置是实际写入最大序号，须联合事件集合与缺口核对。\n";
                for (std::size_t index{}; index < PRODUCERS; ++index) {
                    report << "生产者 " << index << "：提交序号 " << commit.written[index] << "，累计缺口 "
                           << commit.dropped[index] << '\n';
                }
            }
            insufficient = insufficient || !result.complete || result.damaged || result.unknown_version;
            for (const auto &value : result.events) {
                if (value.producer_seq &&
                    !identities.emplace(value.run_id, value.producer_id, value.producer_seq).second) {
                    ++duplicates;
                    continue;
                }
                events.push_back(value);
            }
            auto identity_file{file.parent_path() / "identity.json"};
            if (std::filesystem::is_regular_file(identity_file)) {
                std::ifstream identity{identity_file};
                manifest << identity.rdbuf() << '\n';
            } else {
                manifest << file.string() << "：程序/配置/模型/板卡身份未知\n";
            }
        }
        std::stable_sort(events.begin(), events.end(),
                         [](const Event &left, const Event &right) { return left.mono_ns < right.mono_ns; });
        csv << "run_id,round_id,producer_id,producer_seq,mono_ns,type,reason,capture_id,update_id,tracker_frame_id,"
               "request_id,frame_time_us,prediction_us,duration_ns,flags,score,iou,threshold,highest_score,box,other_box,error_code_known,error_domain,error_code,values,counts\n";
        std::map<std::pair<std::uint64_t, std::uint64_t>, bool> requests{};
        std::set<std::uint64_t> runs{};
        for (const auto &value : events) {
            runs.insert(value.run_id);
            json(lines, value);
            csv << value.run_id << ',' << value.round_id << ',' << value.producer_id << ',' << value.producer_seq << ','
                << value.mono_ns << ',' << typeName(value.type) << ',' << reasonName(value.reason) << ','
                << value.frame.capture_id << ',' << value.update_id << ',' << value.tracker_frame_id << ','
                << value.request_id << ',' << value.frame_time_us << ',' << value.prediction_us << ','
                << value.duration_ns << ',' << value.flags << ',' << value.score << ',' << value.iou << ','
                << value.threshold << ',' << value.highest_score << ",\"";
            for (auto coordinate : value.box) { csv << coordinate << ' '; }
            csv << "\",\"";
            for (auto coordinate : value.other_box) { csv << coordinate << ' '; }
            csv << "\"," << ((value.type == Type::InitializeEnd && (value.flags & ErrorCodeKnown)) ? 1 : 0)
                << ',';
            if (value.type == Type::InitializeEnd && (value.flags & ErrorCodeKnown)) { csv << value.values[1]; }
            csv << ',';
            if (value.type == Type::InitializeEnd && (value.flags & ErrorCodeKnown)) { csv << value.values[2]; }
            csv << ",\"";
            for (auto number : value.values) { csv << number << ' '; }
            csv << "\",\"";
            for (auto number : value.counts) { csv << number << ' '; }
            csv << "\"\n";
            report << value.mono_ns << " run=" << value.run_id << " round=" << value.round_id
                   << " p=" << value.producer_id << ':' << value.producer_seq << " update=" << value.update_id
                   << " frame=" << value.tracker_frame_id << " request=" << value.request_id << ' '
                   << typeName(value.type) << "，理由=" << reasonName(value.reason) << '\n';
            if (value.type == Type::InitializeEnd && value.reason == Reason::InitFailed) {
                report << "失败阶段 " << value.values[0] << "；数值错误";
                if (value.flags & ErrorCodeKnown) {
                    report << "域=" << (value.values[1] == 1 ? "预处理" : "模型") << "，码=" << value.values[2];
                } else { report << "未知"; }
                report << "。\n";
            }
            if (value.type == Type::Decision && (value.flags & DecisionDetails)) {
                report << "实际计数 [";
                for (auto number : value.counts) { report << number << ' '; }
                report << "]，依据见同调用的跳变比较记录。\n";
            }
            if (value.type == Type::JumpComparison) {
                report << "比较来源=" << value.values[0] << "；位置阈值=" << value.score
                       << "；尺寸比阈值=" << value.threshold << "；跳变=" << value.counts[0]
                       << "；参考框" << ((value.flags & BoxValid) ? "有效" : "未知，原分支默认跳变") << "。\n";
            }
            if (value.type == Type::Gap) {
                insufficient = true;
                report << "已知缺口 " << value.values[0] << " 条，范围 " << value.values[1] << '-' << value.values[2]
                       << "；范围详情" << (value.values[4] ? "不完整" : "见逐范围记录") << '\n';
            }
            if (value.type == Type::HistoryLookup && value.reason == Reason::HistoryMiss) {
                report << "检测结果已被读取，但来源帧 " << value.source_tracker_frame_id << " 不在缓存 "
                       << value.values[0] << '-' << value.values[1] << "；不能推断为检测无目标或初始化失败。\n";
            }
            if (value.type == Type::CatchupEnd) {
                report << "补跟踪尝试 " << value.counts[0] << "，成功 " << value.counts[1] << "，失败 "
                       << value.counts[2] << "；整体恢复" << ((value.flags & Complete) ? "成功" : "失败") << "。\n";
            }
            if (value.type == Type::DetectStart) {
                requests[{value.run_id, value.request_id}] = false;
            }
            if (value.type == Type::DetectEnd || value.type == Type::RequestCancel) {
                requests[{value.run_id, value.request_id}] = true;
            }
        }
        for (const auto &request : requests) {
            if (!request.second) {
                insufficient = true;
                report << "请求 " << request.first.second << " 缺少结束事件：结果未知。\n";
            }
        }
        for (auto run : runs) {
            manifest << "run_id=" << run << '\n';
        }
        report << "重复事件 " << duplicates << " 条。\n";
        if (insufficient) {
            report << "证据不足：缺失处不能推导确定原因；未记录到成功不等于从未成功。\n";
        }
        if (!report || !lines || !csv || !manifest) {
            throw std::runtime_error{"导出写入失败"};
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

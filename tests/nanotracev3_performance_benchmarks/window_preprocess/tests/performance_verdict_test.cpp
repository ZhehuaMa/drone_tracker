#include "recovery.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wp = window_preprocess;
namespace {
struct Case {
    std::string name;
    wp::Distribution recovery, preprocess;
    bool expected_pass;
};
std::vector<Case> cases() {
    return {
        {"通过正例", {80, 110}, {18, 19}, true},
        {"预处理比例30%", {80, 110}, {30, 31}, false},
        {"恢复中位数相等", {100, 110}, {18, 19}, false},
        {"恢复中位数增加", {101, 110}, {18, 19}, false},
        {"恢复P95增加", {80, 121}, {18, 19}, false},
        {"预处理恰好20%", {80, 110}, {20, 21}, true},
        {"预处理刚超过20%", {80, 110}, {std::nextafter(20.0, 21.0), 21}, false},
        {"恢复中位数刚降低", {std::nextafter(100.0, 99.0), 110}, {18, 19}, true},
        {"恢复P95相等", {80, 120}, {18, 19}, true},
        {"恢复P95刚增加", {80, std::nextafter(120.0, 121.0)}, {18, 19}, false}
    };
}
wp::PerformanceVerdict evaluate(const Case &sample) {
    return wp::performanceVerdict({100, 120}, sample.recovery, {100, 110}, sample.preprocess);
}
void verify(const Case &sample, const wp::PerformanceVerdict &verdict) {
    const std::string heading{sample.expected_pass ? "指定录像重建恢复对照：通过\n"
                                                  : "指定录像重建恢复对照：失败\n"};
    const std::string status{sample.expected_pass ? "通过\n" : "性能门槛未通过\n"};
    if (verdict.passed != sample.expected_pass || verdict.exit_code != (sample.expected_pass ? 0 : 1)
        || verdict.report.rfind(heading, 0) != 0 || verdict.status != status
        || (!sample.expected_pass && verdict.report.find("通过") != std::string::npos)) {
        throw std::runtime_error(sample.name + "：判定、报告、状态或退出码错误");
    }
    // 数值仍写入报告，失败结论不能取代原始统计。
    for (const auto &field : {"旧恢复中位数=100", "旧恢复P95=120", "旧预处理中位数=100", "预处理比例="}) {
        if (verdict.report.find(field) == std::string::npos) {
            throw std::runtime_error(sample.name + "：报告缺失统计字段");
        }
    }
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 3 && std::string{argv[1]} == "--case") {
        for (const auto &sample : cases()) {
            if (sample.name == argv[2]) {
                const auto verdict{evaluate(sample)};
                std::cout << verdict.report << "状态：" << verdict.status;
                return verdict.exit_code;
            }
        }
        std::cerr << "未知测试用例\n";
        return 2;
    }
    if (argc != 1) {
        std::cerr << "用法：nanotrack_window_performance_tests [--case 用例名]\n";
        return 2;
    }
    for (const auto &sample : cases()) {
        const auto verdict{evaluate(sample)};
        verify(sample, verdict);
        std::cout << sample.name << "：断言通过；诊断退出码=" << verdict.exit_code << '\n';
    }
    std::cout << "10 个固定统计用例通过；调用正式诊断共用逻辑，无模型执行\n";
    return 0;
}

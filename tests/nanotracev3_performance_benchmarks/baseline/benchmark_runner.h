#ifndef NANOTRACK_BASELINE_BENCHMARK_RUNNER_H
#define NANOTRACK_BASELINE_BENCHMARK_RUNNER_H
#include "config.h"
#include "platform/rk3576/rknn_nanotrack_factory.h"
#include <csignal>
#include <functional>
#include <map>
#include <string>
namespace baseline {
extern volatile std::sig_atomic_t interrupted;
using ModelLoader = std::function<model::Result<rk3576::NanoTrackComponents>(const Config &)>;
/**
 * @brief 共用运行器对新测试入口的向后兼容扩展点；baseline 不传入时行为完全不变。
 *
 * variant 写入 manifest 的 `benchmark_variant` 并出现在报告标题；留空表示既有普通特征交接路径。
 * identity 在模型装配成功、精度校验之前调用，用于补充本次执行路径的身份字段。
 * 非空 variant 的路径身份同时写入 manifest、summary 和中文报告；普通属性沿用模型描述字段。
 */
struct RunnerExtensions {
    std::string variant{};
    std::function<void(std::map<std::string, std::string> &)> identity{};
};
int runBenchmark(const Config &config, const ModelLoader &loader = {}, const RunnerExtensions &extensions = {});
} // namespace baseline
#endif // NANOTRACK_BASELINE_BENCHMARK_RUNNER_H

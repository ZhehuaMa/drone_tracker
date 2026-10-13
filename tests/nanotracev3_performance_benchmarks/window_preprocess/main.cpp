#include "recovery.h"

#include <fstream>
#include <iomanip>
#include <dlfcn.h>
#include <iostream>
#include <sched.h>
#include <sstream>
#include <sys/utsname.h>

#include "whole_frame_preprocessor.h"
#include "../baseline/result_writer.h"
#include "../baseline/timing_recorder.h"
#include "code_identity.h"
#include "platform/rk3576/rknn_nanotrack_factory.h"

namespace wp = window_preprocess;
namespace {
std::string read(const std::filesystem::path &path) {
    std::ifstream file{path};
    if (!file) { throw std::runtime_error("环境记录无法读取：" + path.string()); }
    return {std::istreambuf_iterator<char>{file}, {}};
}
std::string environment() {
    std::ostringstream result;
    result << "loadavg=" << read("/proc/loadavg") << "stat=" << read("/proc/stat");
    for (const auto &directory : {"/sys/devices/system/cpu/cpufreq", "/sys/class/devfreq", "/sys/class/thermal"}) {
        if (!std::filesystem::is_directory(directory)) { continue; }
        for (const auto &entry : std::filesystem::directory_iterator{directory}) {
            for (const auto &name : {"scaling_cur_freq", "scaling_governor", "cur_freq", "governor", "temp", "type"}) {
                const auto path{entry.path() / name};
                if (std::filesystem::is_regular_file(path)) {
                    try { result << path.string() << '=' << read(path); }
                    catch (const std::exception &error) { result << error.what() << '\n'; }
                }
            }
        }
    }
    return result.str();
}
void write(const std::filesystem::path &path, const std::string &text) {
    std::ofstream file{path};
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file << text;
}
std::unique_ptr<NanoDcfTracker> tracker(const wp::Config &config, bool legacy,
                                     std::unique_ptr<::NanoTrackPreprocessor> checked = {}) {
    auto loaded{rk3576::loadNanoTrack(config.models[0].string(), config.models[1].string(), config.models[2].string())};
    if (!loaded) { throw std::runtime_error(model::describe(loaded.error)); }
    if (legacy) { loaded.value.preprocessor = std::make_unique<wp::WholeFramePreprocessor>(); }
    if (checked) { loaded.value.preprocessor = std::move(checked); }
    auto nano{std::make_unique<NanoTracker>(std::move(loaded.value.backend), std::move(loaded.value.preprocessor))};
    return std::make_unique<NanoDcfTracker>(config.parameters, std::move(nano));
}
std::string raw(const std::vector<wp::Batch> &batches) {
    std::ostringstream file;
    file << std::setprecision(9);
    file << "warmup,pair,order,legacy,frame,timestamp_us,success,initialized,x,y,w,h,score,elapsed_ns,preprocess_ns,"
            "image_prepare_ns,template_calls,search_calls,head_calls,batch_ns\n";
    for (const auto &batch : batches) {
        for (std::size_t i{0}; i < batch.frames.size(); ++i) {
            const auto &f{batch.frames[i]};
            file << batch.warmup << ',' << batch.pair << ',' << batch.order << ',' << batch.legacy << ','
                     << i << ','
                 << f.timestamp << ',' << f.success << ',' << f.initialized << ',' << f.box.x << ',' << f.box.y << ','
                 << f.box.width << ',' << f.box.height << ',' << f.score << ',' << f.elapsed << ','
                 << f.recorder.duration(timing::Stage::Preprocess) << ','
                 << f.recorder.duration(timing::Stage::ImagePrepare) << ','
                 << f.recorder.modelCalls(timing::Stage::TemplateBackbone) << ','
                 << f.recorder.modelCalls(timing::Stage::SearchBackbone) << ','
                 << f.recorder.modelCalls(timing::Stage::Head) << ',' << batch.elapsed << '\n';
        }
    }
    return file.str();
}
std::string intervals(const std::vector<wp::Batch> &batches) {
    std::ostringstream file;
    file << "warmup,pair,order,legacy,frame,id,parent,stage,start_ns,end_ns,complete\n";
    for (const auto &batch : batches) {
        for (std::size_t i{0}; i < batch.frames.size(); ++i) {
            const auto &recorder{batch.frames[i].recorder};
            for (std::size_t j{0}; j < recorder.size(); ++j) {
                const auto &span{recorder.intervals()[j]};
                file << batch.warmup << ',' << batch.pair << ',' << batch.order << ',' << batch.legacy << ','
                     << i << ','
                     << span.id << ',' << span.parent << ',' << baseline::stageName(span.stage) << ','
                     << span.start << ',' << span.end << ',' << span.complete << '\n';
            }
        }
    }
    return file.str();
}
std::string windowSamples(const FrameRef &frame, int warmups, int pairs) {
    std::ostringstream output;
    output << "side,warmup,pair,order,legacy,template_ns,search_ns,exact\n";
    const ModelInputContract template_contract{{1, 3, 127, 127}}, search_contract{{1, 3, 255, 255}};
    // 此诊断只测预处理，固定同一真实图像与中心；不运行模型或修改恢复初始框。
    for (const int side : {1, 32, 77, 154, 255, 511, 1024, 2048}) {
        wp::WholeFramePreprocessor old;
        rk3576::NanoTrackPreprocessor current;
        std::array<std::vector<float>, 2> expected;
        for (const auto &tag : wp::plan(warmups, pairs)) {
            auto &preprocessor{tag.legacy ? static_cast<::NanoTrackPreprocessor &>(old)
                                         : static_cast<::NanoTrackPreprocessor &>(current)};
            preprocessor.reset();
            std::array<std::int64_t, 2> elapsed{};
            for (int role{0}; role < 2; ++role) {
                const auto start{wp::Recorder::now()};
                auto prepared{role == 0 ? preprocessor.prepareTemplate(frame, {776.045f, 478.8966f},
                    side, template_contract) : preprocessor.prepareSearch(frame, {776.045f, 478.8966f},
                    side, search_contract)};
                elapsed[role] = wp::Recorder::now() - start;
                if (!prepared) { throw std::runtime_error("其他窗口预处理失败"); }
                const auto input{std::dynamic_pointer_cast<const rk3576::FloatInput>(prepared.value)};
                if (!input) { throw std::runtime_error("其他窗口缺少 Float32 结果"); }
                if (tag.order == 0) { expected[role] = *input->values(); }
                else if (expected[role] != *input->values()) {
                    throw std::runtime_error("其他窗口逐元素对照失败");
                }
            }
            output << side << ',' << tag.warmup << ',' << tag.pair << ',' << tag.order << ','
                   << tag.legacy << ',' << elapsed[0] << ',' << elapsed[1] << ",1\n";
        }
    }
    return output.str();
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 2 && std::string{argv[1]} == "--help") {
        std::cout << "用法：nanotrack_window_recovery --check-inputs 配置.yaml\n"
                     "      nanotrack_window_recovery --measure 配置.yaml\n"
                     "检查模式只读图像；测量模式要求生产板、系统 Runtime、真实模型及未存在的输出目录。\n";
        return 0;
    }
    std::filesystem::path output;
    std::vector<wp::Batch> batches;
    bool created{false};
    try {
        if (argc != 3 || (std::string{argv[1]} != "--check-inputs"
            && std::string{argv[1]} != "--measure")) {
            throw std::runtime_error("参数错误，请使用 --help");
        }
        const auto config{wp::loadConfig(argv[2])};
        const auto frames{wp::loadFrames(config)}; // 解码与复制位于计时外；检查模式不读取模型。
        if (std::string{argv[1]} == "--check-inputs") { std::cout << "25 帧身份校验通过\n"; return 0; }
        output = config.output;
        wp::createOutputDirectory(output);
        created = true;
        write(output / "status.txt", "未完成\n");
        std::filesystem::copy_file(config.source, output / "config.yaml");
        std::filesystem::copy_file(config.manifest, output / "input-manifest.yaml");
        std::filesystem::copy_file(config.environment, output / "environment-notes.txt");
        write(output / "code-snapshot.txt", BASELINE_SOURCE_SNAPSHOT);
        struct utsname system{};
        if (uname(&system) != 0 || std::string{system.machine} != "aarch64") {
            throw std::runtime_error("恢复测量必须在 ARM64 生产板运行");
        }
        const auto device_tree{read("/proc/device-tree/model")};
        const auto os{read("/etc/os-release")};
        const auto driver{read("/sys/kernel/debug/rknpu/version")};
        const auto runtime{baseline::sha256("/usr/lib/librknnrt.so")};
        if (runtime != "d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8"
            || driver.find("0.9.8") == std::string::npos || std::string{system.release} != "6.1.141-rt36"
            || os.find("22.04.5") == std::string::npos || device_tree.find("3576") == std::string::npos) {
            throw std::runtime_error("系统 Runtime、驱动、内核或操作系统与生产板基线不符");
        }
        Dl_info library{};
        if (!dladdr(dlsym(RTLD_DEFAULT, "rknn_run"), &library) || !library.dli_fname
            || !std::filesystem::equivalent(library.dli_fname, "/usr/lib/librknnrt.so")) {
            throw std::runtime_error("进程必须实际加载生产板系统 RKNN Runtime");
        }
        std::ostringstream identity;
        identity << "board=" << config.board << "\nkernel=" << system.release << "\nmodel=" << device_tree
                 << "\nruntime_path=" << library.dli_fname << "\nruntime=" << runtime
                 << "\ndriver=" << driver << "\ncommit=" << BASELINE_COMMIT
                 << "\nflags=" << BASELINE_BUILD_FLAGS << "\nexecutable=" << baseline::sha256("/proc/self/exe")
                 << "\nconfig=" << baseline::sha256(config.source)
                 << "\nmanifest=" << baseline::sha256(config.manifest);
        identity << "\nmodel_call_method=actual RKNN Run with model ancestors; unlabelled Inference Run is "
                    "the existing direct search backbone path";
        for (std::size_t i{0}; i < 3; ++i) {
            const auto hash{baseline::sha256(config.models[i])};
            if (hash != config.model_hashes[i]) { throw std::runtime_error("模型校验失败"); }
            identity << "\nmodel_" << i << '=' << hash;
        }
        write(output / "identity.txt", identity.str());
        if (config.cpu >= CPU_SETSIZE) { throw std::runtime_error("CPU 编号越界"); }
        cpu_set_t affinity{};
        CPU_ZERO(&affinity);
        CPU_SET(config.cpu, &affinity);
        if (sched_setaffinity(0, sizeof(affinity), &affinity) != 0) { throw std::runtime_error("固定 CPU 失败"); }
        cv::setNumThreads(config.threads);
        auto exact{std::make_unique<wp::ExactPreprocessor>()};
        const auto *input_check{exact.get()};
        auto validating{tracker(config, false, std::move(exact))};
        const auto checked{wp::recover(*validating, frames, config.box, false)};
        if (input_check->sizes().size() != 25) { throw std::runtime_error("实际输入对照不足 25 次"); }
        std::ostringstream numeric;
        numeric << "实际恢复逐元素精确对照通过\n张量数=25\n元素数=" << input_check->elements()
                << "\n原始窗口边长=";
        for (const auto side : input_check->sizes()) { numeric << side << ','; }
        numeric << '\n';
        write(output / "exact-inputs.txt", numeric.str());
        validating.reset(); // 精确比较与额外模型执行均在正式预热/测量之前完成。
        auto old{tracker(config, true)}, current{tracker(config, false)}; // 同时装配，同模型加载在计时外。
        const auto initial_environment{environment()};
        if (initial_environment.find("scaling_cur_freq=") == std::string::npos
            || initial_environment.find("npu/cur_freq=") == std::string::npos
            || initial_environment.find("/temp=") == std::string::npos) {
            throw std::runtime_error("CPU/NPU 频率或温度记录不完整，拒绝正式测量");
        }
        std::ostringstream snapshots;
        snapshots << "initial\n" << initial_environment;
        bool equal{true};
        std::array<wp::Batch, 2> paired;
        for (const auto &tag : wp::plan(config.warmups, config.pairs)) {
            snapshots << "pair=" << tag.pair << " order=" << tag.order << " before\n" << environment();
            auto batch{wp::recover(tag.legacy ? *old : *current, frames, config.box)};
            snapshots << "pair=" << tag.pair << " order=" << tag.order << " after\n" << environment();
            batch.pair = tag.pair;
            batch.order = tag.order;
            batch.legacy = tag.legacy;
            batch.warmup = tag.warmup;
            paired[tag.legacy ? 0 : 1] = batch;
            batches.push_back(std::move(batch));
            if (tag.order == 1) {
                try {
                    wp::compare(paired[0], paired[1]);
                    wp::compare(checked, paired[1], false);
                }
                catch (const std::exception &) { equal = false; }
                if (!equal) { break; }
            }
        }
        write(output / "frames.csv", raw(batches));
        write(output / "intervals.csv", intervals(batches));
        write(output / "environment-snapshots.txt", snapshots.str());
        if (!equal) { throw std::runtime_error("新旧恢复输出或工作量不一致，原始记录已保存"); }
        const auto a{wp::distribution(wp::formalSamples(batches, true, false))};
        const auto b{wp::distribution(wp::formalSamples(batches, false, false))};
        const auto pa{wp::distribution(wp::formalSamples(batches, true, true))};
        const auto pb{wp::distribution(wp::formalSamples(batches, false, true))};
        const auto verdict{wp::performanceVerdict(a, b, pa, pb)};
        write(output / "report.txt", verdict.report);
        snapshots << "other_windows_before\n" << environment();
        write(output / "other-windows.csv", windowSamples(frames.front(), config.warmups, config.pairs));
        snapshots << "other_windows_after\n" << environment();
        write(output / "environment-snapshots.txt", snapshots.str());
        write(output / "status.txt", verdict.status);
        return verdict.exit_code;
    } catch (const std::exception &error) {
        if (created) {
            try {
                write(output / "frames.csv", raw(batches));
                write(output / "intervals.csv", intervals(batches));
                write(output / "status.txt", std::string{"失败："} + error.what() + '\n');
            } catch (...) {}
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}

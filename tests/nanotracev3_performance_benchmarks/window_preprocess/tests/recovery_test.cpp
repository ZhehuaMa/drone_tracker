#include "recovery.h"
#include "whole_frame_preprocessor.h"

#include <fstream>
#include <iostream>

#include "platform/rk3576/host_frame.h"
#include "tests/model_test_support.h"

#include <opencv2/imgcodecs.hpp>

namespace wp = window_preprocess;
namespace {
using model_test::require;
struct Trace { int templates{}, searches{}, resets{}; std::vector<std::vector<float>> inputs{}; };
class Model final : public model::NanoTrackBackend {
public:
    explicit Model(std::shared_ptr<Trace> trace) : trace_{std::move(trace)} {}
    model::Status setTemplate(const PreparedInputRef &input) override {
        timing::Span model{timing::Stage::TemplateBackbone};
        ++trace_->templates;
        save(input);
        return {true};
    }
    model::Result<model::NanoPrediction> search(const PreparedInputRef &input) override {
        {
            timing::Span model{timing::Stage::SearchBackbone};
            ++trace_->searches;
            save(input);
        }
        timing::Span head{timing::Stage::Head};
        return {{model_test::tensor({1, 2, 15, 15}), model_test::tensor({1, 4, 15, 15}, 32.0f)}};
    }
    void reset() noexcept override { ++trace_->resets; }
private:
    void save(const PreparedInputRef &input) {
        const auto values{std::dynamic_pointer_cast<const rk3576::FloatInput>(input)};
        require(static_cast<bool>(values), "恢复未使用真实浮点预处理器");
        trace_->inputs.push_back(*values->values());
    }
    std::shared_ptr<Trace> trace_;
};
std::unique_ptr<NanoDcfTracker> make(bool legacy, bool dcf, const std::shared_ptr<Trace> &trace) {
    std::unique_ptr<::NanoTrackPreprocessor> preprocessor;
    if (legacy) { preprocessor = std::make_unique<wp::WholeFramePreprocessor>(); }
    else { preprocessor = std::make_unique<rk3576::NanoTrackPreprocessor>(); }
    auto nano{std::make_unique<NanoTracker>(std::make_unique<Model>(trace), std::move(preprocessor))};
    NanoDcfTracker::Params params{};
    params.enable_dcf = dcf;
    return std::make_unique<NanoDcfTracker>(params, std::move(nano));
}
std::vector<FrameRef> frames() {
    std::vector<FrameRef> result;
    cv::Mat source{cv::Size{1920, 1200}, CV_8UC1};
    for (int i{0}; i < 25; ++i) {
        for (int y{0}; y < source.rows; ++y) {
            for (int x{0}; x < source.cols; ++x) { source.at<std::uint8_t>(y, x) = (x * 7 + y * 11 + i * 3) % 256; }
        }
        result.push_back(rk3576::copyGrayFrame(source, 1000000 + i * 16667));
    }
    return result;
}
template <typename Work> void rejected(Work work) {
    bool failed{false};
    try { work(); } catch (const std::exception &) { failed = true; }
    require(failed, "非法用例未被拒绝");
}
std::int64_t tick{};
std::int64_t clock() noexcept { return ++tick; }
void timingChecks() {
    wp::Recorder direct;
    const auto inference{direct.begin(timing::Stage::Inference)};
    const auto search{direct.begin(timing::Stage::Run)};
    direct.end(search, true);
    const auto head{direct.begin(timing::Stage::Head)};
    const auto head_run{direct.begin(timing::Stage::Run)};
    direct.end(head_run, true);
    direct.end(head, true);
    direct.end(inference, true);
    require(direct.modelCalls(timing::Stage::SearchBackbone) == 1
        && direct.modelCalls(timing::Stage::Head) == 1
        && direct.modelCalls(timing::Stage::TemplateBackbone) == 0,
        "直通路径未按实际 Run 区分搜索模型与 Head");
    wp::Recorder unidentified;
    const auto outside{unidentified.begin(timing::Stage::Run)};
    unidentified.end(outside, true);
    rejected([&] { unidentified.modelCalls(timing::Stage::SearchBackbone); });
    const auto labels{wp::plan(2, 10)};
    require(labels.size() == 24, "预热与正式批次总数错误");
    std::vector<wp::Batch> batches;
    for (std::size_t i{0}; i < labels.size(); ++i) {
        const auto &tag{labels[i]};
        require(tag.warmup == (i < 4) && tag.pair == static_cast<int>(i / 2)
            && tag.order == static_cast<int>(i % 2)
            && tag.legacy == ((tag.pair + tag.order) % 2 == 0), "预热标记或交替顺序错误");
        wp::Batch batch{};
        batch.warmup = tag.warmup;
        batch.legacy = tag.legacy;
        batch.elapsed = tag.warmup ? 9999 : tag.pair;
        batch.preprocess = tag.warmup ? 9999 : tag.pair * 2;
        batches.push_back(batch);
    }
    const auto formal{wp::formalSamples(batches, true, false)};
    require(formal.size() == 10 && wp::distribution(formal).median == 6.5
        && wp::distribution(wp::formalSamples(batches, false, true)).p95 == 22,
        "正式统计混入预热或混淆总耗时与预处理");
    rejected([] { wp::plan(1, 10); });
    rejected([] { wp::plan(2, 9); });
    wp::Recorder recorder{clock};
    const auto parent{recorder.begin(timing::Stage::Preprocess)};
    const auto child{recorder.begin(timing::Stage::ImagePrepare)};
    recorder.end(child, true);
    recorder.end(parent, true);
    require(recorder.valid() && recorder.duration(timing::Stage::Preprocess) == 3
        && recorder.duration(timing::Stage::ImagePrepare) == 1 && recorder.intervals()[1].parent == parent,
        "父子计时区间错误");
    const auto outer{recorder.begin(timing::Stage::Preprocess)};
    const auto inner{recorder.begin(timing::Stage::Preprocess)};
    recorder.end(inner, true);
    recorder.end(outer, true);
    require(recorder.duration(timing::Stage::Preprocess) == 6, "同名嵌套区间重复累计");
    wp::Recorder unfinished{clock};
    unfinished.begin(timing::Stage::Head);
    require(!unfinished.valid(), "未闭合区间仍有效");
    wp::Recorder invalid{clock};
    invalid.end(42, true);
    require(!invalid.valid(), "错误区间未拒绝");
    require(wp::distribution({3, 1, 2}).median == 2 && wp::distribution({4, 1, 2, 3}).median == 2.5,
            "奇偶中位数错误");
    std::vector<std::int64_t> values;
    for (int i{1}; i <= 20; ++i) { values.push_back(i); }
    require(wp::distribution(values).p95 == 19, "P95 未使用 ceil(0.95×N)");
    rejected([] { wp::distribution({}); });
    rejected([] { wp::distribution({-1}); });
}
void recovery() {
    const auto input{frames()};
    const cv::Rect2f box{756.689f, 461.019f, 39.712f, 36.7552f};
    wp::ExactPreprocessor exact;
    for (std::size_t i{0}; i < input.size(); ++i) {
        const auto prepared{i == 0 ? exact.prepareTemplate(input[i], {776.045f, 478.8966f}, 77,
            ModelInputContract{{1, 3, 127, 127}}) : exact.prepareSearch(input[i], {776.045f, 478.8966f},
            154, ModelInputContract{{1, 3, 255, 255}})};
        require(static_cast<bool>(prepared), "计时外实际输入检查失败");
    }
    require(exact.sizes().size() == 25 && exact.sizes().front() == 77 && exact.sizes().back() == 154
        && exact.elements() == 3 * 127 * 127 + 24 * 3 * 255 * 255, "精确对照缺少窗口或元素身份");
    exact.reset();
    require(exact.sizes().empty() && exact.elements() == 0, "精确对照重置未清除旧计数");
    for (bool dcf : {false, true}) {
        auto old_trace{std::make_shared<Trace>()}, new_trace{std::make_shared<Trace>()};
        auto old{make(true, dcf, old_trace)}, current{make(false, dcf, new_trace)};
        const auto a{wp::recover(*old, input, box)}, b{wp::recover(*current, input, box)};
        wp::compare(a, b);
        require(old_trace->inputs == new_trace->inputs, "真实恢复中的新旧张量不一致");
        require(old_trace->templates == 1 && old_trace->searches == 24 && new_trace->templates == 1
            && new_trace->searches == 24, "恢复模型工作量错误");
        const auto without{wp::recover(*current, input, box, false)};
        wp::compare(b, without, false);
        for (const auto &result : without.frames) { require(result.recorder.size() == 0, "关闭观测仍记录区间"); }
        auto mutated{b};
        mutated.frames[12].timestamp++;
        rejected([&] { wp::compare(a, mutated); });
        mutated = b;
        mutated.frames[12].score += 0.1f;
        rejected([&] { wp::compare(a, mutated); });
        mutated = b;
        mutated.frames[12].box.x++;
        rejected([&] { wp::compare(a, mutated); });
        mutated = b;
        mutated.frames[12].success = false;
        rejected([&] { wp::compare(a, mutated); });
        mutated = b;
        mutated.frames[12].recorder = wp::Recorder{};
        rejected([&] { wp::compare(a, mutated); });
    }
    auto reordered{input};
    std::swap(reordered[1], reordered[2]);
    auto trace{std::make_shared<Trace>()};
    auto current{make(false, false, trace)};
    rejected([&] { wp::recover(*current, reordered, box); });
    require(trace->templates == 0 && trace->searches == 0, "错误输入仍执行模型");
}
void inputs() {
    const auto directory{std::filesystem::temp_directory_path()
        / ("window-input-test-" + std::to_string(wp::Recorder::now()))};
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{directory};
    cv::Mat gray{1200, 1920, CV_8UC1, cv::Scalar{42}};
    require(cv::imwrite((directory / "gray.png").string(), gray), "生成灰度测试输入失败");
    wp::createOutputDirectory(directory / "result");
    std::ofstream{directory / "result/sentinel"} << "保留";
    rejected([&] { wp::createOutputDirectory(directory / "result"); });
    require(std::filesystem::exists(directory / "result/sentinel"), "输出目录保护破坏已有证据");
    rejected([&] { wp::createOutputDirectory(directory / "result/sentinel"); });
    wp::Config config{};
    config.manifest = directory / "manifest.yaml";
    auto manifest{[&](int count, bool wrong_order, bool wrong_hash, bool repeated) {
        std::ofstream file{config.manifest};
        file << "%YAML:1.0\nsource_commit: \"09006db9ea11bd4fa137efc47ecb117660e0fad2\"\n"
             << "video_sha256: \"11d45885dc32859c5aba16541ecc83f769ad47ba9ae66b54908b19d805d4c152\"\n"
             << "mapping: latest_video_frame_not_after_processing_time\nframes:\n";
        for (int i{0}; i < count; ++i) {
            file << "  - { order: " << (wrong_order ? 0 : i) << ", video_index: " << 4146 + i
                 << ", timestamp_us: \"" << (repeated ? 1 : 1000000 + i * 16667)
                 << "\", path: gray.png, pixel_sha256: \""
                 << (wrong_hash ? std::string(64, '0') : wp::pixelHash(gray)) << "\" }\n";
        }
    }};
    manifest(25, false, false, false);
    const auto loaded{wp::loadFrames(config)};
    require(loaded.size() == 25 && loaded.back()->timestampUs() == 1400008, "输入清单未保留顺序和时间戳");
    manifest(24, false, false, false); rejected([&] { wp::loadFrames(config); });
    manifest(25, true, false, false); rejected([&] { wp::loadFrames(config); });
    manifest(25, false, true, false); rejected([&] { wp::loadFrames(config); });
    manifest(25, false, false, true); rejected([&] { wp::loadFrames(config); });
    manifest(25, false, false, false);
    std::filesystem::remove(directory / "gray.png"); rejected([&] { wp::loadFrames(config); });
    cv::imwrite((directory / "gray.png").string(), cv::Mat{1200, 1920, CV_8UC3, cv::Scalar{42, 42, 42}});
    rejected([&] { wp::loadFrames(config); });
    cv::Mat padded{1200, 1924, CV_8UC1, cv::Scalar{255}};
    gray.copyTo(padded(cv::Rect{0, 0, 1920, 1200}));
    require(wp::pixelHash(gray) == wp::pixelHash(padded(cv::Rect{0, 0, 1920, 1200})), "像素指纹读取行末填充");
}
} // namespace
int main() {
    timingChecks();
    recovery();
    inputs();
    std::cout << "恢复状态/工作量、观测透明性、计时统计和输入校验通过（模拟模型，无 NPU）\n";
}

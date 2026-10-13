#include "recovery.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "platform/rk3576/host_frame.h"

#include <openssl/evp.h>
#include <opencv2/imgcodecs.hpp>

namespace window_preprocess {
namespace {
void require(bool valid, const char *message) {
    if (!valid) { throw std::runtime_error(message); }
}
std::filesystem::path resolve(const std::filesystem::path &base, const std::string &value) {
    require(!value.empty(), "配置路径缺失");
    return std::filesystem::absolute(base / value).lexically_normal();
}
std::uint64_t timestamp(const std::string &value) {
    require(!value.empty() && value.find_first_not_of("0123456789") == std::string::npos, "时间戳必须是十进制字符串");
    const auto result{std::stoull(value)};
    require(result > 0 && result <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), "时间戳越界");
    return result;
}
} // namespace
PreparedInputResult ExactPreprocessor::checked(PreparedInputResult current,
                                              const PreparedInputResult &reference, int size) {
    require(current && reference, "实际恢复预处理失败");
    const auto a{std::dynamic_pointer_cast<const rk3576::FloatInput>(current.value)};
    const auto b{std::dynamic_pointer_cast<const rk3576::FloatInput>(reference.value)};
    require(a && b && *a->values() == *b->values(), "实际恢复模型输入逐元素不一致");
    sizes_.push_back(size);
    elements_ += a->values()->size();
    return current;
}
PreparedInputResult ExactPreprocessor::prepareTemplate(const FrameRef &frame, const cv::Point2f &position,
                                                       int size, const ModelInputContract &contract) {
    return checked(current_.prepareTemplate(frame, position, size, contract),
                   reference_.prepareTemplate(frame, position, size, contract), size);
}
PreparedInputResult ExactPreprocessor::prepareSearch(const FrameRef &frame, const cv::Point2f &position,
                                                     int size, const ModelInputContract &contract) {
    return checked(current_.prepareSearch(frame, position, size, contract),
                   reference_.prepareSearch(frame, position, size, contract), size);
}
void ExactPreprocessor::reset() noexcept {
    current_.reset();
    reference_.reset();
    sizes_.clear();
    elements_ = 0;
}
std::int64_t Recorder::now() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
std::uint32_t Recorder::begin(timing::Stage stage) noexcept {
    if (size_ == intervals_.size()) { valid_ = false; return 0; }
    const auto id{static_cast<std::uint32_t>(size_ + 1)};
    intervals_[size_++] = {id, top_, stage, clock_(), 0, false};
    top_ = id;
    return id;
}
void Recorder::end(std::uint32_t id, bool complete) noexcept {
    if (!id || id != top_ || id > size_) { valid_ = false; return; }
    auto &interval{intervals_[id - 1]};
    interval.end = clock_();
    interval.complete = complete;
    valid_ = valid_ && interval.end >= interval.start;
    top_ = interval.parent;
}
std::int64_t Recorder::duration(timing::Stage stage) const {
    std::int64_t total{};
    for (std::size_t i{0}; i < size_; ++i) {
        // 同名嵌套阶段只统计最外层，其他父子阶段分别报告，禁止加成总耗时。
        const auto &span{intervals_[i]};
        if (span.stage != stage) { continue; }
        bool nested{false};
        for (auto id{span.parent}; id; id = intervals_[id - 1].parent) {
            nested = nested || intervals_[id - 1].stage == stage;
        }
        if (!nested) { total += span.end - span.start; }
    }
    return total;
}
int Recorder::count(timing::Stage stage) const {
    int total{};
    for (std::size_t i{0}; i < size_; ++i) { total += intervals_[i].stage == stage; }
    return total;
}
int Recorder::modelCalls(timing::Stage stage) const {
    require(stage == timing::Stage::TemplateBackbone || stage == timing::Stage::SearchBackbone
        || stage == timing::Stage::Head, "只能查询三个 NanoTrack 模型的执行次数");
    if (count(timing::Stage::Run) == 0) { return count(stage); }
    int total{};
    for (std::size_t i{0}; i < size_; ++i) {
        const auto &run{intervals_[i]};
        if (run.stage != timing::Stage::Run) { continue; }
        bool inference{false}, identified{false};
        auto role{timing::Stage::SearchBackbone};
        for (auto id{run.parent}; id; id = intervals_[id - 1].parent) {
            const auto ancestor{intervals_[id - 1].stage};
            inference = inference || ancestor == timing::Stage::Inference;
            if (ancestor == timing::Stage::TemplateBackbone || ancestor == timing::Stage::SearchBackbone
                || ancestor == timing::Stage::Head) {
                require(!identified, "模型执行区间存在冲突祖先");
                role = ancestor;
                identified = true;
            }
        }
        // 现有直通后端的搜索 Run 直接属于 Inference；模板/Head 均有独立模型祖先。
        require(run.complete && (identified || inference), "实际模型执行缺少可识别上下文");
        total += role == stage;
    }
    return total;
}
std::vector<BatchTag> plan(int warmups, int pairs) {
    require(warmups >= 2 && pairs >= 10 && warmups <= 10000 && pairs <= 10000, "批次数量不符合测量要求");
    std::vector<BatchTag> result;
    for (int pair{0}; pair < warmups + pairs; ++pair) {
        for (int order{0}; order < 2; ++order) {
            result.push_back({pair < warmups, (pair + order) % 2 == 0, pair, order});
        }
    }
    return result;
}
std::vector<std::int64_t> formalSamples(const std::vector<Batch> &batches, bool legacy, bool preprocessing) {
    std::vector<std::int64_t> result;
    for (const auto &batch : batches) {
        if (!batch.warmup && batch.legacy == legacy) {
            result.push_back(preprocessing ? batch.preprocess : batch.elapsed);
        }
    }
    return result;
}
Batch recover(NanoDcfTracker &tracker, const std::vector<FrameRef> &frames, const cv::Rect2f &box, bool observed) {
    require(frames.size() == 25, "恢复需要恰好 25 帧");
    std::uint64_t last{};
    for (const auto &frame : frames) {
        require(frame && frame->timestampUs() > last, "恢复帧必须按时间戳严格递增");
        last = frame->timestampUs();
    }
    tracker.reset();
    Batch batch{};
    batch.frames.resize(frames.size()); // 分配位于计时外。
    const auto start{Recorder::now()};
    for (std::size_t i{0}; i < frames.size(); ++i) {
        auto &result{batch.frames[i]};
        result.timestamp = frames[i]->timestampUs();
        result.box = i == 0 ? box : cv::Rect2f{};
        const auto begin{Recorder::now()};
        {
            timing::Observation observation{observed ? &result.recorder : nullptr};
            result.success = i == 0 ? tracker.init(frames[i], box) : tracker.update(frames[i], result.box);
        }
        result.elapsed = Recorder::now() - begin;
        result.initialized = tracker.isInitialized();
        result.score = tracker.lastScore();
        batch.preprocess += result.recorder.duration(timing::Stage::Preprocess);
    }
    batch.elapsed = Recorder::now() - start;
    for (const auto &frame : batch.frames) { require(frame.recorder.valid(), "阶段计时区间不完整"); }
    return batch;
}
void compare(const Batch &old_batch, const Batch &new_batch, bool check_calls) {
    require(old_batch.frames.size() == 25 && new_batch.frames.size() == 25, "恢复输出缺帧");
    for (std::size_t i{0}; i < 25; ++i) {
        const auto &a{old_batch.frames[i]}, &b{new_batch.frames[i]};
        require(a.timestamp == b.timestamp && a.success == b.success && a.initialized == b.initialized
            && a.box == b.box && a.score == b.score, "新旧恢复状态、框、分数或帧顺序不一致");
        require(a.success && a.initialized, "指定恢复用例未全部成功");
        if (check_calls) {
            for (const auto *frame : {&a, &b}) {
                require(frame->recorder.modelCalls(timing::Stage::TemplateBackbone) == (i == 0 ? 1 : 0)
                    && frame->recorder.modelCalls(timing::Stage::SearchBackbone) == (i == 0 ? 0 : 1)
                    && frame->recorder.modelCalls(timing::Stage::Head) == (i == 0 ? 0 : 1),
                    "模型调用次数不符合 1/24/24");
            }
        }
    }
}
Distribution distribution(std::vector<std::int64_t> samples) {
    require(!samples.empty(), "统计样本为空");
    require(std::all_of(samples.begin(), samples.end(), [](auto value) { return value >= 0; }), "耗时不能为负");
    std::sort(samples.begin(), samples.end());
    const auto n{samples.size()};
    const double median{n % 2 ? static_cast<double>(samples[n / 2]) :
        static_cast<double>(samples[n / 2 - 1]) / 2 + static_cast<double>(samples[n / 2]) / 2};
    return {median, static_cast<double>(samples[static_cast<std::size_t>(std::ceil(0.95 * n)) - 1])};
}
std::string pixelHash(const cv::Mat &gray) {
    require(!gray.empty() && gray.type() == CV_8UC1, "像素校验仅接受 Gray8");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    require(context && EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1, "无法初始化像素 SHA-256");
    for (int y{0}; y < gray.rows; ++y) {
        require(EVP_DigestUpdate(context.get(), gray.ptr(y), gray.cols) == 1, "像素 SHA-256 更新失败");
    }
    std::array<unsigned char, 32> digest{};
    unsigned int size{};
    require(EVP_DigestFinal_ex(context.get(), digest.data(), &size) == 1 && size == digest.size(), "像素校验失败");
    std::ostringstream output;
    for (auto byte : digest) { output << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte); }
    return output.str();
}
void createOutputDirectory(const std::filesystem::path &path) {
    require(!std::filesystem::exists(path) && std::filesystem::create_directories(path), "输出目录已存在或不可创建，拒绝覆盖");
}
Config loadConfig(const std::filesystem::path &path) {
    cv::FileStorage file{path.string(), cv::FileStorage::READ};
    require(file.isOpened(), "无法读取恢复配置");
    Config result{};
    result.source = std::filesystem::absolute(path);
    const auto base{result.source.parent_path()};
    result.manifest = resolve(base, file["manifest"].string());
    result.output = resolve(base, file["output"].string());
    result.environment = resolve(base, file["environment"].string());
    result.board = file["board"].string();
    require(result.board == "HZ-RK3576_WuKong Board", "正式测量板卡身份必须为 HZ-RK3576_WuKong Board");
    const std::array<std::string, 3> roles{"template", "search", "head"};
    for (std::size_t i{0}; i < roles.size(); ++i) {
        result.models[i] = resolve(base, file["models"][roles[i]]["path"].string());
        result.model_hashes[i] = file["models"][roles[i]]["sha256"].string();
        require(result.model_hashes[i].size() == 64, "模型必须给出 SHA-256");
    }
    const auto box{file["bbox"]};
    require(box.isSeq() && box.size() == 4, "初始框必须含 x、y、宽、高");
    result.box = {static_cast<float>(box[0]), static_cast<float>(box[1]),
                  static_cast<float>(box[2]), static_cast<float>(box[3])};
    require(result.box == cv::Rect2f{756.689f, 461.019f, 39.712f, 36.7552f}, "初始框不符合指定恢复输入");
    result.warmups = static_cast<int>(file["warmups"]);
    result.pairs = static_cast<int>(file["pairs"]);
    result.cpu = static_cast<int>(file["cpu"]);
    result.threads = static_cast<int>(file["opencv_threads"]);
    require(result.warmups >= 2 && result.pairs >= 10 && result.cpu >= 0 && result.threads > 0, "预热、配对或线程配置无效");
    const auto params{file["tracker"]};
    const std::array<std::string, 11> keys{"enable_dcf", "max_div_factor", "model_sz", "pad", "sigma_factor",
        "learning_rate", "psr_thresh", "lambda_reg", "fixed_template", "eps", "r_pos"};
    for (const auto &key : keys) { require(!params[key].empty(), "跟踪参数缺失"); }
    require(!params["q_a"].empty(), "中心滤波 q_a 参数缺失");
    result.parameters.enable_dcf = static_cast<int>(params["enable_dcf"]) != 0;
    result.parameters.max_div_factor = static_cast<float>(params["max_div_factor"]);
    auto &dcf{result.parameters.dcf};
    dcf.model_sz = static_cast<int>(params["model_sz"]);
    dcf.pad = static_cast<float>(params["pad"]);
    dcf.sigma_factor = static_cast<float>(params["sigma_factor"]);
    dcf.learning_rate = static_cast<float>(params["learning_rate"]);
    dcf.psr_thresh = static_cast<float>(params["psr_thresh"]);
    dcf.lambda_reg = static_cast<float>(params["lambda_reg"]);
    dcf.fixed_template = static_cast<int>(params["fixed_template"]) != 0;
    dcf.eps = static_cast<float>(params["eps"]);
    result.parameters.center_kf.r_pos = static_cast<double>(params["r_pos"]);
    result.parameters.center_kf.q_a = static_cast<double>(params["q_a"]);
    require(dcf.model_sz > 1 && dcf.pad > 0 && dcf.sigma_factor > 0 && dcf.lambda_reg > 0 && dcf.eps > 0
        && result.parameters.max_div_factor > 0 && result.parameters.center_kf.r_pos > 0
        && result.parameters.center_kf.q_a >= 0, "跟踪参数超出有效范围");
    plan(result.warmups, result.pairs);
    return result;
}
std::vector<FrameRef> loadFrames(const Config &config) {
    cv::FileStorage file{config.manifest.string(), cv::FileStorage::READ};
    require(file.isOpened(), "输入清单缺失");
    require(file["source_commit"].string() == "09006db9ea11bd4fa137efc47ecb117660e0fad2"
        && file["video_sha256"].string() == "11d45885dc32859c5aba16541ecc83f769ad47ba9ae66b54908b19d805d4c152"
        && file["mapping"].string() == "latest_video_frame_not_after_processing_time", "重建来源或映射身份不符");
    const auto entries{file["frames"]};
    require(entries.isSeq() && entries.size() == 25, "输入清单必须包含 25 帧");
    std::vector<FrameRef> frames;
    std::uint64_t last{};
    int order{};
    for (const auto &entry : entries) {
        require(static_cast<int>(entry["order"]) == order && static_cast<int>(entry["video_index"]) == 4146 + order,
                "图像顺序或视频索引不符");
        const auto ts{timestamp(entry["timestamp_us"].string())};
        require(ts > last, "输入时间戳必须严格递增");
        const std::filesystem::path relative{entry["path"].string()};
        require(!relative.empty() && relative.is_relative(), "帧路径必须相对清单目录");
        for (const auto &part : relative) { require(part != "..", "帧路径不能离开输入目录"); }
        const auto gray{cv::imread((config.manifest.parent_path() / relative).string(), cv::IMREAD_UNCHANGED)};
        require(gray.cols == 1920 && gray.rows == 1200 && gray.type() == CV_8UC1, "需要 1920×1200 原有重建 Gray8 图像");
        require(pixelHash(gray) == entry["pixel_sha256"].string(), "输入像素校验失败，拒绝测量");
        frames.push_back(rk3576::copyGrayFrame(gray, ts));
        last = ts;
        ++order;
    }
    return frames;
}
} // namespace window_preprocess

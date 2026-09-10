#include "model_test_support.h"
#include "nano_tracker.h"
#include "yolo_detector.h"
#include "preprocess/input_validation.h"
#include "preprocess/reusable_input.h"
#include "preprocess/target_mean.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

// 只读检查真实内部状态，不提供可绕过生产初始化的入口。
struct NanoTrackerTestAccess {
    static std::pair<cv::Point2f, cv::Point2f> state(const NanoTracker &tracker) {
        return {tracker.center_pos, tracker.size};
    }
};
namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
class MockView final : public FrameReadView {
  public:
    explicit MockView(cv::Mat image) : image_{std::move(image)} {}
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::size_t rowStrideBytes() const noexcept override { return image_.step; }
    const cv::Mat &mat() const noexcept override { return image_; }

  private:
    cv::Mat image_;
};
class MockFrame final : public Frame {
  public:
    MockFrame(int value = 20, FramePixelFormat format = FramePixelFormat::Gray8, bool map_failure = false)
        : image_{80, 100, CV_8UC1, cv::Scalar{static_cast<double>(value)}}, format_{format}, map_failure_{map_failure} {
    }
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return format_; }
    std::uint64_t timestampUs() const noexcept override { return 123; }
    std::unique_ptr<FrameReadView> mapRead() const override {
        if (map_failure_) {
            return {};
        }
        return std::make_unique<MockView>(image_);
    }

  private:
    cv::Mat image_;
    FramePixelFormat format_;
    bool map_failure_;
};
class MockInput final : public PreparedModelInput {
  public:
    MockInput(std::shared_ptr<int> storage, ModelInputContract contract)
        : storage_{std::move(storage)}, contract_{std::move(contract)} {}
    const ModelInputContract &contract() const noexcept override { return contract_; }
    int value() const { return *storage_; }

  private:
    std::shared_ptr<int> storage_;
    ModelInputContract contract_;
};
PreparedInputResult prepareMock(ReusableInput<int> &cache, const ModelInputContract &contract) {
    auto storage{cache.acquire([] { return std::make_shared<int>(41); })};
    if (!storage) {
        return PreparedInputResult::failure(PreprocessError::InputBusy, "busy");
    }
    return {std::make_shared<MockInput>(std::move(storage), contract)};
}
struct SessionTrace {
    int begins{0};
    int inferences{0};
    bool fail_model{false};
    int hit_call{0};
    float hit_score{0.9f};
    bool fail_prepare{false};
    std::vector<cv::Rect> regions{};
};
class MockSession final : public YoloFrameSession {
  public:
    MockSession(std::shared_ptr<SessionTrace> trace, ModelInputContract contract)
        : trace_{std::move(trace)}, contract_{std::move(contract)} {}
    cv::Size sourceSize() const noexcept override { return {100, 80}; }
    PreprocessResult<YoloPreparedRegion> prepare(const cv::Rect &region) override {
        using Result = PreprocessResult<YoloPreparedRegion>;
        trace_->regions.push_back(region);
        const auto resolved{resolveYoloRegion(sourceSize(), region)};
        if (!resolved) {
            return Result::failure(resolved.error, resolved.message);
        }
        if (trace_->fail_prepare) {
            return Result::failure(PreprocessError::PlatformFailure, "simulated failure");
        }
        auto input{prepareMock(cache_, contract_)};
        if (!input) {
            return Result::failure(input.error, input.message);
        }
        return {YoloPreparedRegion{std::move(input.value),
                                   calculateLetterboxTransform(resolved.value.size(), {64, 64}).value}};
    }

  private:
    std::shared_ptr<SessionTrace> trace_;
    ModelInputContract contract_;
    ReusableInput<int> cache_{};
};
class MockYoloPreprocessor final : public YoloPreprocessor {
  public:
    explicit MockYoloPreprocessor(std::shared_ptr<SessionTrace> trace) : trace_{std::move(trace)} {}
    PreprocessResult<std::unique_ptr<YoloFrameSession>> beginFrame(const FrameRef &frame,
                                                                   const ModelInputContract &contract) override {
        using Result = PreprocessResult<std::unique_ptr<YoloFrameSession>>;
        ++trace_->begins;
        const auto error{validatePreprocessFrame(frame)};
        if (error != PreprocessError::None) {
            return Result::failure(error, "invalid frame");
        }
        if (!matchesImageContract(contract, 1.0f / 255.0f)) {
            return Result::failure(PreprocessError::ContractMismatch, "invalid contract");
        }
        return {std::make_unique<MockSession>(trace_, contract)};
    }

  private:
    std::shared_ptr<SessionTrace> trace_;
};
class MockYoloCalls final : public model::YoloBackend {
  public:
    explicit MockYoloCalls(std::shared_ptr<SessionTrace> trace) : trace_{std::move(trace)} {}
    ModelInputContract inputContract() const override {
        return {{1, 3, 64, 64}, TensorElementType::Float32, TensorLayout::Nchw, 1.0f / 255.0f};
    }
    model::Result<model::YoloPrediction> infer(const PreparedInputRef &) override {
        ++trace_->inferences;
        if (trace_->fail_model) {
            return model::Result<model::YoloPrediction>::failure(
                {model::ModelError::ExecutionFailure, "mock-yolo", {}, {}, {}, "injected failure"});
        }
        auto tensor{model_test::tensor({1, 5, 10})};
        // 测试数据构造期间存储尚未发布给消费者。
        float *data{const_cast<float *>(tensor.read().data())};
        data[0] = 32;
        data[10] = 32;
        data[20] = 10;
        data[30] = 10;
        data[40] = trace_->inferences == trace_->hit_call ? trace_->hit_score : 0.0f;
        return {{tensor, 1, 2}};
    }

  private:
    std::shared_ptr<SessionTrace> trace_;
};
void checkYolo(int hit_call, float score, bool fail_prepare = false, bool fail_model = false) {
    auto trace{std::make_shared<SessionTrace>()};
    trace->hit_call = hit_call;
    trace->hit_score = score;
    trace->fail_prepare = fail_prepare;
    trace->fail_model = fail_model;
    YoloV8Detector detector{
        std::make_unique<MockYoloCalls>(trace), std::make_unique<MockYoloPreprocessor>(trace), 0.5f, 0.5f, 0, 0.5f};
    require(detector.init(), "模拟 YOLO 初始化失败");
    const cv::Rect roi{20, 20, 40, 40};
    require(detector.request(std::make_shared<MockFrame>(), 77, roi), "请求失败");
    YoloResult result;
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{5}};
    bool received{false};
    while (!(received = detector.get_result(result)) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    detector.stop(); // join 后再读 trace，避免测试自身产生数据竞争。
    require(received && result.frame_id == 77, "异步结果未返回原帧号");
    require(trace->begins == 1, "一次请求建立了多个快照");
    require(trace->regions.front() == roi, "ROI 未优先执行");
    const bool expected_hit{!fail_prepare && !fail_model && hit_call > 0 && score >= (hit_call == 1 ? 0.25f : 0.5f)};
    require(result.has_target == expected_hit, "检测阈值或目标选择发生变化");
    if (expected_hit) {
        require(trace->inferences == hit_call && trace->regions.size() == static_cast<std::size_t>(hit_call),
                "有效目标没有终止级联或输入未释放");
    }
    if (trace->regions.size() > 1) {
        require(trace->regions[1] == cv::Rect{}, "全图阶段顺序错误");
    }
    if (trace->regions.size() > 2) {
        const auto tiles{YoloV8Detector::createOverlappingTiles(100, 80, 0.2f)};
        for (std::size_t i{2}; i < trace->regions.size(); ++i) {
            require(trace->regions[i] == tiles[i - 2], "切片顺序变化");
        }
    }
    if (fail_prepare) {
        require(trace->inferences == 0, "预处理失败仍触发推理");
    }
}
struct NanoTrace {
    int templates{0};
    int searches{0};
    int resets{0};
    int model_resets{0};
    bool fail_model_template{false};
    bool fail_model_search{false};
    bool fail_template{false};
    bool fail_search{false};
};
class MockNanoPreprocessor final : public NanoTrackPreprocessor {
  public:
    explicit MockNanoPreprocessor(std::shared_ptr<NanoTrace> trace) : trace_{std::move(trace)} {}
    PreparedInputResult prepareTemplate(const FrameRef &, const cv::Point2f &, int,
                                        const ModelInputContract &contract) override {
        if (trace_->fail_template) {
            return PreparedInputResult::failure(PreprocessError::PlatformFailure, "template");
        }
        require(matchesImageContract(contract, 1.0f, 127), "模板契约不正确");
        return prepareMock(template_, contract);
    }
    PreparedInputResult prepareSearch(const FrameRef &, const cv::Point2f &, int,
                                      const ModelInputContract &contract) override {
        if (trace_->fail_search) {
            return PreparedInputResult::failure(PreprocessError::PlatformFailure, "search");
        }
        require(matchesImageContract(contract, 1.0f, 255), "搜索契约不正确");
        return prepareMock(search_, contract);
    }
    void reset() noexcept override { ++trace_->resets; }

  private:
    std::shared_ptr<NanoTrace> trace_;
    ReusableInput<int> template_{};
    ReusableInput<int> search_{};
};
class MockNanoCalls final : public model::NanoTrackBackend {
  public:
    explicit MockNanoCalls(std::shared_ptr<NanoTrace> trace) : trace_{std::move(trace)} {}
    model::Status setTemplate(const PreparedInputRef &) override {
        ++trace_->templates;
        if (trace_->fail_model_template) {
            return model::Status::failure({model::ModelError::ExecutionFailure, "mock-nano", {}, {}, {}, "template"});
        }
        return {true};
    }
    void reset() noexcept override { ++trace_->model_resets; }
    model::Result<model::NanoPrediction> search(const PreparedInputRef &) override {
        ++trace_->searches;
        if (trace_->fail_model_search) {
            return model::Result<model::NanoPrediction>::failure(
                {model::ModelError::ExecutionFailure, "mock-nano", {}, {}, {}, "search"});
        }
        return {{model_test::tensor({1, 2, 15, 15}), model_test::tensor({1, 4, 15, 15}, 10)}};
    }

  private:
    std::shared_ptr<NanoTrace> trace_;
};
void checkNano() {
    auto trace{std::make_shared<NanoTrace>()};
    NanoTracker tracker{std::make_unique<MockNanoCalls>(trace), std::make_unique<MockNanoPreprocessor>(trace)};
    const FrameRef frame{std::make_shared<MockFrame>()};
    trace->fail_template = true;
    tracker.init(frame, {20, 20, 20, 20});
    require(!tracker.isInitialized() && trace->templates == 0, "模板失败仍初始化或推理");
    trace->fail_template = false;
    tracker.init(frame, {20, 20, 20, 20});
    require(tracker.isInitialized() && trace->templates == 1, "模板成功未初始化");
    cv::Rect2f bbox;
    require(tracker.update(frame, bbox) > 0, "模拟搜索未通过真实输出处理");
    const auto state{NanoTrackerTestAccess::state(tracker)};
    const auto old_bbox{bbox};
    trace->fail_search = true;
    require(tracker.update(frame, bbox) < 0, "搜索失败未传播");
    require(NanoTrackerTestAccess::state(tracker) == state && bbox == old_bbox, "搜索失败修改了中心、尺寸或输出框");
    require(trace->searches == 1, "搜索预处理失败触发了模型调用");
    trace->fail_search = false;
    require(tracker.update(frame, bbox) > 0 && trace->searches == 2, "失败后无法继续搜索或租约未释放");
    const auto before_model_failure{NanoTrackerTestAccess::state(tracker)};
    const auto bbox_before_model_failure{bbox};
    trace->fail_model_search = true;
    require(tracker.update(frame, bbox) < 0, "模型失败未传播");
    require(NanoTrackerTestAccess::state(tracker) == before_model_failure && bbox == bbox_before_model_failure,
            "模型失败修改了目标状态");
    trace->fail_model_search = false;
    require(tracker.update(frame, bbox) > 0, "模型失败后不能恢复搜索");
    trace->fail_model_template = true;
    tracker.init(frame, {10, 10, 10, 10});
    require(!tracker.isInitialized(), "新模板模型失败仍初始化");
    const int model_resets{trace->model_resets};
    const int resets{trace->resets};
    tracker.reset();
    require(trace->resets == resets + 1 && !tracker.isInitialized(), "目标重置未传递");
    require(trace->model_resets == model_resets + 1, "模型模板未同步重置");
}
} // namespace

int main() {
    const ModelInputContract contract{{1, 3, 64, 64}, TensorElementType::Float32, TensorLayout::Nchw, 1.0f / 255.0f};
    PreparedInputRef survivor;
    {
        ReusableInput<int> cache;
        auto first{prepareMock(cache, contract)};
        require(static_cast<bool>(first), "首次租约失败");
        require(prepareMock(cache, contract).error == PreprocessError::InputBusy, "存活结果未阻止缓存复用");
        auto alias{first.value};
        first.value.reset();
        require(prepareMock(cache, contract).error == PreprocessError::InputBusy, "最后引用尚未释放却复用缓存");
        alias.reset();
        survivor = prepareMock(cache, contract).value;
    }
    require(dynamic_cast<const MockInput &>(*survivor).value() == 41, "结果无法比预处理缓存活得更久");
    require(survivor->contract().shape == contract.shape, "输入契约无法只读查询");
    auto trace{std::make_shared<SessionTrace>()};
    MockYoloPreprocessor preprocessor{trace};
    require(preprocessor.beginFrame({}, contract).error == PreprocessError::EmptyFrame, "空帧错误未分类");
    require(preprocessor.beginFrame(std::make_shared<MockFrame>(20, FramePixelFormat::Bgr8), contract).error ==
                PreprocessError::UnsupportedFormat,
            "格式错误未分类");
    auto bad{contract};
    bad.shape[1] = 1;
    require(preprocessor.beginFrame(std::make_shared<MockFrame>(), bad).error == PreprocessError::ContractMismatch,
            "契约错误未分类");
    bad = contract;
    // 故意注入未知枚举值，验证来自其他实现的不兼容契约会被拒绝。
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    bad.layout = static_cast<TensorLayout>(99);
    require(!matchesImageContract(bad, 1.0f / 255.0f), "接受了不支持的布局");
    bad = contract;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    bad.element_type = static_cast<TensorElementType>(99);
    require(!matchesImageContract(bad, 1.0f / 255.0f), "接受了不支持的元素类型");
    auto session{preprocessor.beginFrame(std::make_shared<MockFrame>(), contract)};
    require(session.value->prepare({-1, 0, 10, 10}).error == PreprocessError::InvalidRegion, "非法区域未拒绝");
    auto region{session.value->prepare({})};
    require(session.value->prepare({}).error == PreprocessError::InputBusy, "同一会话覆盖存活输入");
    region.value.input.reset();
    require(static_cast<bool>(session.value->prepare({10, 10, 20, 20})), "释放后无法准备下一区域");
    TargetMean mean;
    mean.initialize(std::make_shared<MockFrame>(20));
    mean.commit();
    require(mean.valid() && mean.value() == 20, "初始目标均值错误");
    mean.reset();
    require(!mean.valid(), "旧目标均值仍有效");
    mean.initialize(std::make_shared<MockFrame>(90));
    mean.commit();
    require(mean.value() == 90, "新目标复用了旧均值");
    mean.initialize(std::make_shared<MockFrame>(90, FramePixelFormat::Gray8, true));
    require(mean.value() == 114 && !mean.valid(), "映射失败回退或模板提交边界错误");
    checkYolo(1, 0.4f); // ROI 使用放宽阈值。
    checkYolo(2, 0.9f);
    checkYolo(3, 0.9f);
    checkYolo(2, 0.4f); // 同分数在全图被正常阈值拒绝。
    checkYolo(0, 0.0f, true);
    checkYolo(1, 0.9f, false, true); // 模型失败仍返回原帧号且继续既有级联。
    checkNano();
    std::cout << "真实级联、状态更新、结果租约和错误传播测试通过\n";
}

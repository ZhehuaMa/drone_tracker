#include "jetson_nanotrack_preprocessor.h"

#include <map>
#include "frame/vpi_frame.h"
#include "jetson_image_ops.h"
#include "jetson_model_input.h"
#include "preprocess/input_validation.h"
#include "preprocess/target_mean.h"
#include <vpi/algo/Rescale.h>

using namespace jetson_preprocess;
struct JetsonNanoTrackPreprocessor::State {
    Stream stream{};
    Image canvas{};
    Image fill{};
    std::map<int, Image> resized{};
    std::map<int, ReusableInput<TensorBuffer>> inputs{};
    TargetMean mean{};
    cv::Size fill_size{};
    bool fill_dirty{true};
};
JetsonNanoTrackPreprocessor::JetsonNanoTrackPreprocessor() : state_{std::make_unique<State>()} {}
JetsonNanoTrackPreprocessor::~JetsonNanoTrackPreprocessor() = default;
void JetsonNanoTrackPreprocessor::reset() noexcept {
    state_->mean.reset();
    state_->fill_dirty = true;
}
PreparedInputResult JetsonNanoTrackPreprocessor::prepareTemplate(const FrameRef &frame, const cv::Point2f &position,
                                                                 int original_size,
                                                                 const ModelInputContract &contract) {
    reset();
    return prepare(frame, position, original_size, contract, true);
}
PreparedInputResult JetsonNanoTrackPreprocessor::prepareSearch(const FrameRef &frame, const cv::Point2f &position,
                                                               int original_size, const ModelInputContract &contract) {
    return prepare(frame, position, original_size, contract, false);
}
PreparedInputResult JetsonNanoTrackPreprocessor::prepare(const FrameRef &frame, const cv::Point2f &position,
                                                         int original_size, const ModelInputContract &contract,
                                                         bool is_template) {
    const auto error{validatePreprocessFrame(frame)};
    if (error != PreprocessError::None) {
        return PreparedInputResult::failure(error, preprocessErrorName(error));
    }
    const int model_size{is_template ? 127 : 255};
    if (!matchesImageContract(contract, 1.0f, model_size)) {
        return PreparedInputResult::failure(PreprocessError::ContractMismatch, "NanoTrack 输入契约不匹配");
    }
    if (!is_template && !state_->mean.valid()) {
        return PreparedInputResult::failure(PreprocessError::ContractMismatch, "必须先成功准备当前目标模板");
    }
    const auto vpi_frame{std::dynamic_pointer_cast<const VpiFrame>(frame)};
    if (!vpi_frame) {
        return PreparedInputResult::failure(PreprocessError::PlatformTypeMismatch, "需要 VpiFrame");
    }
    const cv::Size source_size{frame->width(), frame->height()};
    const auto geometry{calculateNanoCropGeometry(source_size, position, original_size)};
    if (!geometry) {
        return PreparedInputResult::failure(geometry.error, geometry.message);
    }
    try {
        auto storage{state_->inputs[model_size].acquire([&] { return allocateJetsonInput(contract); })};
        if (!storage) {
            return PreparedInputResult::failure(PreprocessError::InputBusy, "上一输入结果尚未释放");
        }
        if (is_template) {
            state_->mean.initialize(frame);
        }
        const auto &g{geometry.value};
        const cv::Size canvas_size{source_size.width + g.left + g.right, source_size.height + g.top + g.bottom};
        state_->canvas.ensure(canvas_size.width, canvas_size.height, VPI_IMAGE_FORMAT_F32);
        if (g.left || g.top || g.right || g.bottom) {
            if (state_->fill_dirty || state_->fill_size != canvas_size) {
                fillGray(state_->fill, canvas_size.width, canvas_size.height, state_->mean.value());
                state_->fill_size = canvas_size;
                state_->fill_dirty = false;
            }
        }
        auto access{vpi_frame->acquireVpiRead()};
        Batch batch{state_->stream};
        const cv::Rect content{g.left, g.top, source_size.width, source_size.height};
        if (g.left || g.top || g.right || g.bottom) {
            fillBands(batch, state_->stream.get(), state_->fill.get(), state_->canvas.get(), canvas_size, content,
                      1.0f);
        }
        convert(state_->stream.get(), access.image(), batch.view(state_->canvas.get(), content), 1.0f);
        batch.sync();
        const cv::Rect crop{g.crop.x + g.left, g.crop.y + g.top, original_size, original_size};
        VPIImage output{batch.view(state_->canvas.get(), crop)};
        if (original_size != model_size) {
            auto &resized{state_->resized[model_size]};
            resized.ensure(model_size, model_size, VPI_IMAGE_FORMAT_F32);
            check(vpiSubmitRescale(state_->stream.get(), VPI_BACKEND_CUDA, output, resized.get(), VPI_INTERP_LINEAR,
                                   VPI_BORDER_CLAMP, 0),
                  "resize NanoTrack crop");
            output = resized.get();
        }
        batch.sync();
        copyPlanes(output, model_size, model_size, *storage);
        PreparedInputRef prepared{std::make_shared<JetsonPreparedModelInput>(std::move(storage), contract)};
        if (is_template) {
            state_->mean.commit();
        }
        return {std::move(prepared)};
    } catch (const std::exception &exception) {
        return PreparedInputResult::failure(PreprocessError::PlatformFailure, exception.what());
    }
}

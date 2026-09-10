#include "jetson_yolo_preprocessor.h"

#include <exception>
#include "frame/vpi_frame.h"
#include "jetson_image_ops.h"
#include "jetson_model_input.h"
#include "preprocess/input_validation.h"
#include <vpi/algo/Rescale.h>

namespace {
using namespace jetson_preprocess;
class JetsonYoloSession final : public YoloFrameSession {
  public:
    JetsonYoloSession(const VpiFrameRef &frame, ModelInputContract contract)
        : contract_{std::move(contract)}, size_{frame->width(), frame->height()} {
        snapshot_.ensure(size_.width, size_.height, VPI_IMAGE_FORMAT_U8);
        // 源租约先创建，Batch 的析构在它之前同步；所有异常路径也遵守此顺序。
        auto access{frame->acquireVpiRead()};
        Batch batch{stream_};
        check(vpiSubmitConvertImageFormat(stream_.get(), VPI_BACKEND_CUDA, access.image(), snapshot_.get(), nullptr),
              "copy frame snapshot");
        batch.sync();
    }
    cv::Size sourceSize() const noexcept override { return size_; }
    PreprocessResult<YoloPreparedRegion> prepare(const cv::Rect &region) override {
        using Result = PreprocessResult<YoloPreparedRegion>;
        const auto resolved{resolveYoloRegion(size_, region)};
        if (!resolved) {
            return Result::failure(resolved.error, resolved.message);
        }
        const cv::Size model{contract_.shape[3], contract_.shape[2]};
        const auto geometry{calculateLetterboxTransform(resolved.value.size(), model)};
        if (!geometry) {
            return Result::failure(geometry.error, geometry.message);
        }
        try {
            auto storage{input_.acquire([&] { return allocateJetsonInput(contract_); })};
            if (!storage) {
                return Result::failure(PreprocessError::InputBusy, "上一输入结果尚未释放");
            }
            canvas_.ensure(model.width, model.height, VPI_IMAGE_FORMAT_F32);
            if (!fill_valid_) {
                fillGray(fill_, model.width, model.height, 114);
                fill_valid_ = true;
            }
            Batch batch{stream_};
            VPIImage content{batch.view(snapshot_.get(), resolved.value)};
            const auto &g{geometry.value};
            if (g.resized != resolved.value.size()) {
                resized_.ensure(g.resized.width, g.resized.height, VPI_IMAGE_FORMAT_U8);
                check(vpiSubmitRescale(stream_.get(), VPI_BACKEND_CUDA, content, resized_.get(), VPI_INTERP_LINEAR,
                                       VPI_BORDER_CLAMP, 0),
                      "resize YOLO region");
                content = resized_.get();
            }
            const cv::Rect destination{g.left, g.top, g.resized.width, g.resized.height};
            fillBands(batch, stream_.get(), fill_.get(), canvas_.get(), model, destination, contract_.scale);
            convert(stream_.get(), content, batch.view(canvas_.get(), destination), contract_.scale);
            batch.sync();
            copyPlanes(canvas_.get(), model.width, model.height, *storage);
            return {YoloPreparedRegion{std::make_shared<JetsonPreparedModelInput>(std::move(storage), contract_), g}};
        } catch (const std::exception &error) {
            return Result::failure(PreprocessError::PlatformFailure, error.what());
        }
    }

  private:
    Stream stream_{};
    Image snapshot_{};
    Image canvas_{};
    Image fill_{};
    Image resized_{};
    bool fill_valid_{false};
    ReusableInput<TensorBuffer> input_{};
    ModelInputContract contract_;
    cv::Size size_;
};
} // namespace

PreprocessResult<std::unique_ptr<YoloFrameSession>>
JetsonYoloPreprocessor::beginFrame(const FrameRef &frame, const ModelInputContract &contract) {
    using Result = PreprocessResult<std::unique_ptr<YoloFrameSession>>;
    const auto error{validatePreprocessFrame(frame)};
    if (error != PreprocessError::None) {
        return Result::failure(error, preprocessErrorName(error));
    }
    if (!matchesImageContract(contract, 1.0f / 255.0f)) {
        return Result::failure(PreprocessError::ContractMismatch, "YOLO 要求灰度三通道 Float32 NCHW 和 1/255 缩放");
    }
    const auto vpi_frame{std::dynamic_pointer_cast<const VpiFrame>(frame)};
    if (!vpi_frame) {
        return Result::failure(PreprocessError::PlatformTypeMismatch, "YOLO 需要 VpiFrame");
    }
    try {
        return {std::make_unique<JetsonYoloSession>(vpi_frame, contract)};
    } catch (const std::exception &exception) {
        return Result::failure(PreprocessError::PlatformFailure, exception.what());
    }
}

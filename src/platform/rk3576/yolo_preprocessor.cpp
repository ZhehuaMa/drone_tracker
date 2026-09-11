#include "yolo_preprocessor.h"

#include <limits>

#include "preprocess/input_validation.h"
#include "preprocess/reusable_input.h"

#include <opencv2/imgproc.hpp>

namespace rk3576 {
namespace {
class Session final : public YoloFrameSession {
public:
    explicit Session(cv::Mat snapshot) : snapshot_{std::move(snapshot)} {}
    cv::Size sourceSize() const noexcept override { return snapshot_.size(); }
    PreprocessResult<YoloPreparedRegion> prepare(const cv::Rect &region) override {
        using Result = PreprocessResult<YoloPreparedRegion>;
        const auto resolved{resolveYoloRegion(sourceSize(), region)};
        if (!resolved) {
            return Result::failure(resolved.error, resolved.message);
        }
        const auto transform{calculateLetterboxTransform(resolved.value.size(), {640, 640})};
        if (!transform) {
            return Result::failure(transform.error, transform.message);
        }
        try {
            auto bytes{cache_.acquire([] { return std::make_shared<std::vector<std::uint8_t>>(640 * 640 * 3); })};
            if (!bytes) {
                return Result::failure(PreprocessError::InputBusy, "Previous input still owns cache lease");
            }
            const auto &lb{transform.value};
            cv::Mat gray(640, 640, CV_8UC1, cv::Scalar{114});
            cv::Mat resized;
            cv::resize(snapshot_(resolved.value), resized, lb.resized, 0, 0, cv::INTER_LINEAR);
            resized.copyTo(gray(cv::Rect{lb.left, lb.top, resized.cols, resized.rows}));
            cv::Mat rgb(640, 640, CV_8UC3, bytes->data());
            cv::cvtColor(gray, rgb, cv::COLOR_GRAY2RGB);
            return {{std::make_shared<ByteInput>(std::move(bytes)), lb}};
        } catch (const std::exception &error) {
            return Result::failure(PreprocessError::PlatformFailure, error.what());
        }
    }

private:
    cv::Mat snapshot_;
    ReusableInput<std::vector<std::uint8_t>> cache_{};
};
} // namespace
PreprocessResult<std::unique_ptr<YoloFrameSession>> YoloPreprocessor::beginFrame(const FrameRef &frame,
                                                                                 const ModelInputContract &contract) {
    using Result = PreprocessResult<std::unique_ptr<YoloFrameSession>>;
    const auto error{validatePreprocessFrame(frame)};
    if (error != PreprocessError::None) {
        return Result::failure(error, "Expected nonempty Gray8 frame");
    }
    if (!matchesYoloContract(contract) || contract.element_type != TensorElementType::Uint8) {
        return Result::failure(PreprocessError::ContractMismatch, "RKNN requires UINT8 NHWC [1,640,640,3]");
    }
    try {
        cv::Mat snapshot;
        {
            const auto view{frame->mapRead()};
            if (!view) {
                return Result::failure(PreprocessError::PlatformFailure, "Frame mapping failed");
            }
            const auto &mat{view->mat()};
            if (view->width() != frame->width() || view->height() != frame->height() ||
                view->pixelFormat() != FramePixelFormat::Gray8 || mat.type() != CV_8UC1 || mat.step[1] != 1 ||
                mat.cols != frame->width() || mat.rows != frame->height() || !mat.data ||
                view->rowStrideBytes() != mat.step[0] || mat.step[0] < static_cast<std::size_t>(mat.cols) ||
                mat.step[0] > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(mat.rows)) {
                return Result::failure(PreprocessError::PlatformTypeMismatch, "Invalid Gray8 mapping geometry/stride");
            }
            snapshot = mat.clone();
        }
        return {std::make_unique<Session>(std::move(snapshot))};
    } catch (const std::exception &error) {
        return Result::failure(PreprocessError::PlatformFailure, error.what());
    }
}
} // namespace rk3576

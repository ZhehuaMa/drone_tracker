#include "nanotrack_preprocessor.h"

#include <algorithm>
#include <limits>

#include "preprocess/input_validation.h"
#include "timing_observer.h"

#include <opencv2/imgproc.hpp>

namespace rk3576 {
void NanoTrackPreprocessor::reset() noexcept { mean_.reset(); }
PreparedInputResult NanoTrackPreprocessor::prepareTemplate(const FrameRef &frame, const cv::Point2f &position,
                                                           int original_size, const ModelInputContract &contract) {
    reset();
    return prepare(frame, position, original_size, contract, true);
}
PreparedInputResult NanoTrackPreprocessor::prepareSearch(const FrameRef &frame, const cv::Point2f &position,
                                                         int original_size, const ModelInputContract &contract) {
    return prepare(frame, position, original_size, contract, false);
}
PreparedInputResult NanoTrackPreprocessor::prepare(const FrameRef &frame, const cv::Point2f &position,
                                                   int original_size, const ModelInputContract &contract,
                                                   bool is_template) {
    timing::Span image_prepare{timing::Stage::ImagePrepare};
    const auto error{validatePreprocessFrame(frame)};
    if (error != PreprocessError::None) {
        image_prepare.finish(false);
        return PreparedInputResult::failure(error, preprocessErrorName(error));
    }
    const int size{is_template ? 127 : 255};
    if (!matchesImageContract(contract, 1.0f, size)) {
        image_prepare.finish(false);
        return PreparedInputResult::failure(PreprocessError::ContractMismatch, "Expected Float32 NCHW NanoTrack input");
    }
    if (!is_template && !mean_.valid()) {
        image_prepare.finish(false);
        return PreparedInputResult::failure(PreprocessError::ContractMismatch, "Current target template is required");
    }
    const auto geometry{calculateNanoCropGeometry({frame->width(), frame->height()}, position, original_size)};
    if (!geometry) {
        image_prepare.finish(false);
        return PreparedInputResult::failure(geometry.error, geometry.message);
    }
    try {
        auto storage{cache_[is_template ? 0 : 1].acquire(
            [size] { return std::make_shared<std::vector<float>>(3 * size * size); })};
        if (!storage) {
            image_prepare.finish(false);
            return PreparedInputResult::failure(PreprocessError::InputBusy, "Previous input still owns cache lease");
        }
        if (is_template) {
            mean_.initialize(frame);
        }
        cv::Mat snapshot;
        {
            const auto view{frame->mapRead()};
            if (!view) {
                image_prepare.finish(false);
                return PreparedInputResult::failure(PreprocessError::PlatformFailure, "Frame mapping failed");
            }
            const auto &mat{view->mat()};
            if (view->width() != frame->width() || view->height() != frame->height() ||
                view->pixelFormat() != FramePixelFormat::Gray8 || mat.type() != CV_8UC1 || mat.step[1] != 1 ||
                mat.cols != frame->width() || mat.rows != frame->height() || !mat.data ||
                view->rowStrideBytes() != mat.step[0] || mat.step[0] < static_cast<std::size_t>(mat.cols) ||
                mat.step[0] > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(mat.rows)) {
                image_prepare.finish(false);
                return PreparedInputResult::failure(PreprocessError::PlatformTypeMismatch, "Invalid Gray8 mapping");
            }
            mat.convertTo(snapshot, CV_32F);
        }
        const auto &g{geometry.value};
        cv::Mat padded;
        cv::copyMakeBorder(snapshot, padded, g.top, g.bottom, g.left, g.right, cv::BORDER_CONSTANT,
                           cv::Scalar{static_cast<double>(mean_.value())});
        const cv::Rect crop{g.crop.x + g.left, g.crop.y + g.top, original_size, original_size};
        cv::Mat plane(size, size, CV_32FC1, storage->data());
        cv::resize(padded(crop), plane, {size, size}, 0, 0, cv::INTER_LINEAR);
        const auto count{static_cast<std::size_t>(size * size)};
        std::copy_n(storage->data(), count, storage->data() + count);
        std::copy_n(storage->data(), count, storage->data() + 2 * count);
        PreparedInputRef prepared{std::make_shared<FloatInput>(std::move(storage), contract)};
        if (is_template) {
            mean_.commit();
        }
        return {std::move(prepared)};
    } catch (const std::exception &exception) {
        image_prepare.finish(false);
        return PreparedInputResult::failure(PreprocessError::PlatformFailure, exception.what());
    }
}
} // namespace rk3576

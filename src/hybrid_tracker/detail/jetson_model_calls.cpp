#include "model_calls.h"

#include <stdexcept>
#include <limits>
#include "trt_engine.h"

namespace tracker_detail {
namespace {
struct TrtOutputStorage final : OutputStorage {
    std::vector<TensorBuffer> buffers{};
};
ModelOutputs wrap(std::vector<TensorBuffer> buffers) {
    auto storage{std::make_shared<TrtOutputStorage>()};
    storage->buffers = std::move(buffers);
    ModelOutputs result{storage, {}};
    for (auto &buffer : storage->buffers) {
        if (!buffer.host_ptr || buffer.dims.nbDims <= 0) {
            throw std::runtime_error("Invalid model output storage");
        }
        std::vector<int> dimensions;
        for (int axis{0}; axis < buffer.dims.nbDims; ++axis) {
            if (buffer.dims.d[axis] <= 0 || buffer.dims.d[axis] > std::numeric_limits<int>::max()) {
                throw std::runtime_error("Invalid model output extent");
            }
            dimensions.push_back(static_cast<int>(buffer.dims.d[axis]));
        }
        result.tensors.emplace_back(buffer.dims.nbDims, dimensions.data(), CV_32F, buffer.host_ptr);
    }
    return result;
}
std::vector<TensorBuffer> checkedInfer(TrtEngine &engine, const PreparedModelInput &input) {
    auto result{engine.inferPrepared(input)};
    if (!result) {
        throw std::runtime_error(std::string{preprocessErrorName(result.error)} + ": " + result.message);
    }
    return std::move(result.value);
}
class JetsonYoloCalls final : public YoloModelCalls {
  public:
    explicit JetsonYoloCalls(const std::string &path) : engine_{path} {}
    ModelInputContract inputContract() const override {
        const auto dims{engine_.getTensorDims(engine_.getPrimaryInputName())};
        return {TensorShape{dims.d, dims.d + dims.nbDims}, TensorElementType::Float32, TensorLayout::Nchw,
                1.0f / 255.0f, 0.0f};
    }
    ModelOutputs infer(const PreparedModelInput &input) override { return wrap(checkedInfer(engine_, input)); }

  private:
    TrtEngine engine_;
};
class JetsonNanoCalls final : public NanoModelCalls {
  public:
    JetsonNanoCalls(const std::string &backbone, const std::string &head) : backbone_{backbone}, head_{head} {}
    void setTemplate(const PreparedModelInput &input) override {
        auto outputs{checkedInfer(backbone_, input)};
        if (outputs.empty()) {
            throw std::runtime_error("Missing template features");
        }
        template_features_ = std::move(outputs[0]);
    }
    ModelOutputs search(const PreparedModelInput &input) override {
        auto features{checkedInfer(backbone_, input)};
        if (features.empty()) {
            throw std::runtime_error("Missing search features");
        }
        return wrap(head_.infer({&template_features_, &features[0]}));
    }

  private:
    TrtEngine backbone_;
    TrtEngine head_;
    TensorBuffer template_features_{};
};
} // namespace
std::unique_ptr<YoloModelCalls> createYoloModelCalls(const std::string &path) {
    return std::make_unique<JetsonYoloCalls>(path);
}
std::unique_ptr<NanoModelCalls> createNanoModelCalls(const std::string &backbone, const std::string &head) {
    return std::make_unique<JetsonNanoCalls>(backbone, head);
}
} // namespace tracker_detail

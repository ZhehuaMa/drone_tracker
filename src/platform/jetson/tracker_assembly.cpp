#include "platform/jetson/tracker_assembly.h"

#include <stdexcept>
#include "hybrid_tracker/model/validation.h"

namespace platform_jetson {
namespace {
void require(bool condition, const char *reason) {
    if (!condition) { throw std::invalid_argument(reason); }
}
void checkSpecs(const std::vector<model::TensorSpec> &specs, bool inputs) {
    for (std::size_t i{0}; i < specs.size(); ++i) {
        const auto &s{specs[i]};
        require(s.index == i && s.type == TensorElementType::Float32 &&
                (s.layout == TensorLayout::Linear || s.layout == TensorLayout::Nchw),
                "Expected native ordered Float32 tensors with continuous layout");
        require(!s.shape.empty(), "Missing tensor shape");
        for (std::size_t axis{0}; axis < s.shape.size(); ++axis) {
            require(s.shape[axis] > 0 || s.shape[axis] == -1, "Invalid tensor extent");
            if (inputs && s.shape[axis] == -1) {
                require(s.minimum.size() == s.shape.size() && s.maximum.size() == s.shape.size() &&
                        s.minimum[axis] > 0 && s.maximum[axis] >= s.minimum[axis], "Invalid dynamic input bounds");
            }
        }
    }
}
void accepts(const model::TensorSpec &spec, const TensorShape &shape) {
    require(spec.shape.size() == shape.size(), "Input rank mismatch");
    for (std::size_t i{0}; i < shape.size(); ++i) {
        require(spec.shape[i] == shape[i] || (spec.shape[i] == -1 &&
                spec.minimum[i] <= shape[i] && spec.maximum[i] >= shape[i]), "Input shape mismatch");
    }
}
bool couldMatch(const TensorShape &actual, const TensorShape &expected) {
    if (actual.size() != expected.size()) { return false; }
    for (std::size_t i{0}; i < actual.size(); ++i) {
        if (actual[i] != -1 && actual[i] != expected[i]) { return false; }
    }
    return true;
}
void checkModel(const model::ModelDescription &description, std::size_t inputs, std::size_t outputs) {
    require(description.inputs.size() == inputs && description.outputs.size() == outputs,
            "Model input/output count mismatch");
    checkSpecs(description.inputs, true);
    checkSpecs(description.outputs, false);
}
} // namespace

PlatformResult<TrackerComponents> assembleTracker(const ModelPaths &paths, const AssemblyDependencies &d) {
    std::string component{"YOLO"};
    std::string path{paths.yolo};
    try {
        const auto load{[&d](const std::string &file) {
            auto result{d.load(file)};
            if (!result) { throw std::runtime_error(model::describe(result.error)); }
            require(static_cast<bool>(result.value), "Loader returned no model");
            return std::move(result.value);
        }};
        TrackerComponents components{};
        auto yolo{load(path)};
        checkModel(yolo->description(), 1, 1);
        const auto &prediction{yolo->description().outputs.front().shape};
        require(prediction.size() == 3 && (prediction[0] == 1 || prediction[0] == -1) &&
                (prediction[1] == -1 || prediction[1] >= 5) &&
                (prediction[1] == -1 || prediction[2] == -1 || prediction[2] >= prediction[1]),
                "Expected YOLO [1, attributes>=5, candidates>=attributes]");
        components.yolo = std::make_unique<model::JetsonYoloBackend>(std::move(yolo), d.adapt);
        components.yolo->inputContract();

        component = "Backbone";
        path = paths.backbone;
        auto backbone{load(path)};
        checkModel(backbone->description(), 1, 1);
        accepts(backbone->description().inputs.front(), {1, 3, 127, 127});
        accepts(backbone->description().inputs.front(), {1, 3, 255, 255});

        component = "Head";
        path = paths.head;
        auto head{load(path)};
        checkModel(head->description(), 2, 2);
        const auto &outputs{head->description().outputs};
        const TensorShape classification{1, 2, 15, 15};
        const TensorShape location{1, 4, 15, 15};
        require((couldMatch(outputs[0].shape, classification) && couldMatch(outputs[1].shape, location)) ||
                (couldMatch(outputs[1].shape, classification) && couldMatch(outputs[0].shape, location)),
                "Expected unique Head classification [1,2,15,15] and location [1,4,15,15]");
        // 仅检查已解析的特征轴；动态输出仍由真实推理边界逐次验证。
        const auto &feature{backbone->description().outputs.front().shape};
        for (const auto &input : head->description().inputs) {
            require(input.shape.size() == feature.size(), "Backbone/Head feature rank mismatch");
            for (std::size_t i{0}; i < feature.size(); ++i) {
                if (feature[i] > 0) {
                    require(input.shape[i] == feature[i] || (input.shape[i] == -1 &&
                            input.minimum[i] <= feature[i] && input.maximum[i] >= feature[i]),
                            "Backbone/Head feature shape mismatch");
                }
            }
        }
        components.nano = std::make_unique<model::JetsonNanoTrackBackend>(
            std::move(backbone), std::move(head), d.adapt);
        component = "YOLO preprocessor";
        path = paths.yolo;
        components.yolo_preprocessor = d.yolo_preprocessor();
        require(static_cast<bool>(components.yolo_preprocessor), "Missing preprocessor");
        component = "NanoTrack preprocessor";
        path = paths.backbone;
        components.nano_preprocessor = d.nano_preprocessor();
        require(static_cast<bool>(components.nano_preprocessor), "Missing preprocessor");
        component = "FrameCompatibility";
        path.clear();
        components.compatibility = d.compatibility();
        require(components.complete(), "Incomplete tracker components");
        return {std::move(components)};
    } catch (const std::exception &error) {
        return PlatformResult<TrackerComponents>::failure({"jetson", component, path, error.what()});
    }
}
} // namespace platform_jetson

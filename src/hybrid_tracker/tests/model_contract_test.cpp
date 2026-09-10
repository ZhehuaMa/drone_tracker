#include "model_test_support.h"

#include <functional>
#include <cstdio>
#include <limits>
#include <sys/wait.h>
#include <unistd.h>
#include "model/jetson/jetson_model_backend.h"
#include "preprocess/reusable_input.h"

namespace {
using model_test::require;
using model_test::tensor;
using namespace model;
class Input final : public PreparedModelInput {
public:
    Input(ModelInputContract contract, std::shared_ptr<const int> lease = {})
        : contract_{std::move(contract)}, lease_{std::move(lease)} {}
    const ModelInputContract &contract() const noexcept override { return contract_; }

private:
    ModelInputContract contract_;
    std::shared_ptr<const int> lease_;
};
PreparedInputRef image(int size) { return std::make_shared<Input>(ModelInputContract{{1, 3, size, size}}); }
class InputStorage final : public TensorStorage {
public:
    InputStorage(PreparedInputRef owner, std::size_t bytes) : owner_{std::move(owner)}, bytes_{bytes} {}
    const float *hostData() const noexcept override { return &placeholder_; }
    std::size_t byteSize() const noexcept override { return bytes_; }

private:
    PreparedInputRef owner_;
    std::size_t bytes_;
    float placeholder_{}; // 模拟执行器不读取图像像素，仅验证契约和所有权。
};
Result<Tensor> adapt(const PreparedInputRef &input, const std::string &identity) {
    if (!input) {
        return Result<Tensor>::failure({ModelError::PlatformTypeMismatch, identity, {}, {}, {}, "null"});
    }
    const auto &c{input->contract()};
    auto bytes{tensorBytes(c.shape, c.element_type, identity, "image")};
    if (!bytes) {
        return Result<Tensor>::failure(bytes.error);
    }
    return {{{c.shape, c.element_type, c.layout, bytes.value, {}}, std::make_shared<InputStorage>(input, bytes.value)}};
}
struct Trace {
    int submits{};
    int calls{};
    bool failure{};
    bool invalid{};
    std::vector<TensorShape> output_shapes{};
    std::function<void(const std::vector<Tensor> &)> consume{};
    std::vector<std::weak_ptr<const TensorStorage>> seen{};
};
class Executor final : public ModelExecutor {
public:
    Executor(ModelDescription description, std::shared_ptr<Trace> trace)
        : description_{std::move(description)}, trace_{std::move(trace)} {}
    const ModelDescription &description() const noexcept override { return description_; }
    ExecutionResult execute(const std::vector<Tensor> &inputs) override {
        ++trace_->calls;
        if (trace_->invalid) {
            return ExecutionResult::failure({ModelError::NotReady, description_.identity, {}, {}, {}, "invalid"});
        }
        if (const auto error{validateInputs(inputs, description_)}) {
            return ExecutionResult::failure(error);
        }
        for (const auto &shape : trace_->output_shapes) {
            auto bytes{tensorBytes(shape, TensorElementType::Float32, description_.identity, "output")};
            if (!bytes) {
                return ExecutionResult::failure(bytes.error);
            }
        }
        ++trace_->submits;
        trace_->seen.clear();
        for (const auto &input : inputs) {
            trace_->seen.push_back(input.storage);
        }
        if (trace_->consume) {
            trace_->consume(inputs);
        }
        if (trace_->failure) {
            return ExecutionResult::failure(
                {ModelError::ExecutionFailure, description_.identity, {}, {}, {}, "injected"});
        }
        std::vector<Tensor> outputs;
        for (const auto &shape : trace_->output_shapes) {
            outputs.push_back(tensor(shape, static_cast<float>(trace_->submits)));
        }
        return {std::move(outputs)};
    }

private:
    ModelDescription description_;
    std::shared_ptr<Trace> trace_;
};
ModelDescription backboneDescription() {
    return {"backbone",
            {{"actual_image", 0, {1, 3, -1, -1}, {1, 3, 127, 127}, {1, 3, 255, 255}}},
            {{"actual_feature", 0, {1, 8, 4, 4}}}};
}
ModelDescription headDescription() {
    return {"head",
            {{"actual_template", 0, {1, 8, 4, 4}}, {"actual_search", 1, {1, 8, 4, 4}}},
            {{"location_first", 0, {1, 4, 15, 15}}, {"classification_second", 1, {1, 2, 15, 15}}}};
}
void checkContracts() {
    auto trace{std::make_shared<Trace>()};
    trace->output_shapes = {{1, 2}};
    Executor executor{headDescription(), trace};
    auto first{tensor({1, 8, 4, 4})};
    auto second{tensor({1, 8, 4, 4})};
    require(!executor.execute({first}), "接受错误输入数量");
    second.descriptor.bytes -= 4;
    auto failure{executor.execute({first, second})};
    require(!failure && failure.error.tensor.find("actual_search") != std::string::npos, "第二输入错误没有张量身份");
    require(!failure.error.expected.empty() && !failure.error.actual.empty(), "缺少预期和实际诊断");
    second = tensor({1, 8, 4, 4});
    second.descriptor.type = TensorElementType::Float16;
    require(!executor.execute({first, second}), "接受不支持的半精度");
    second.descriptor.type = TensorElementType::Int8;
    require(!executor.execute({first, second}), "接受不支持的整数");
    second = tensor({1, 8, 4, 4});
    second.descriptor.layout = TensorLayout::Unsupported;
    require(!executor.execute({first, second}), "接受不支持的布局");
    second = tensor({1, 8, 4, 4});
    second.descriptor.quantization = Quantization{{0.5f}, {0}, {}};
    require(!executor.execute({first, second}), "将整数编码量化误用为浮点像素变换");
    auto unsupported_spec{headDescription()};
    // 故意模拟外部实现返回未知布局值，验证公共校验能够拒绝。
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    unsupported_spec.inputs[1].layout = static_cast<TensorLayout>(99);
    require(static_cast<bool>(validateInputs({first, tensor({1, 8, 4, 4})}, unsupported_spec)),
            "接受未知的模型布局描述");
    require(trace->submits == 0, "非法输入进入提交阶段");
    Executor dynamic{backboneDescription(), trace};
    auto outside{adapt(image(256), "backbone")};
    require(!dynamic.execute({outside.value}) && trace->submits == 0, "动态越界输入已提交");
    trace->output_shapes = {{1, -1, 2}};
    second = tensor({1, 8, 4, 4});
    require(!executor.execute({first, second}) && trace->submits == 0, "未解析输出已提交");
    trace->output_shapes = {{INT32_MAX, INT32_MAX, INT32_MAX}};
    require(!executor.execute({first, second}) && trace->submits == 0, "溢出输出已提交");
    require(!tensorBytes({1, 0}, TensorElementType::Float32, "x", "y"), "接受零尺寸");
    trace->output_shapes = {{1, 2}};
    require(static_cast<bool>(executor.execute({first, second})), "合法双输入失败");
    auto unknown{headDescription()};
    unknown.inputs[1].index = 0;
    require(static_cast<bool>(validateInputs({first, second}, unknown)), "接受错误原生顺序");
}
void checkOwnership() {
    auto trace{std::make_shared<Trace>()};
    trace->output_shapes = {{1, 2}};
    HostTensorView survivor{nullptr};
    std::weak_ptr<const TensorStorage> weak;
    {
        auto executor{std::make_unique<Executor>(headDescription(), trace)};
        auto first{executor->execute({tensor({1, 8, 4, 4}), tensor({1, 8, 4, 4})})};
        survivor = first.value.front().read();
        weak = first.value.front().storage;
        auto second{executor->execute({tensor({1, 8, 4, 4}), tensor({1, 8, 4, 4})})};
        require(second.value.front().read().data()[0] == 2 && survivor.data()[0] == 1, "新执行覆盖旧输出");
    }
    require(!weak.expired() && survivor.data()[0] == 1, "执行器销毁后输出悬空");
    survivor = HostTensorView{nullptr};
    require(weak.expired(), "最后视图释放后存储未回收");
    ReusableInput<int> cache;
    const auto create{[] { return std::make_shared<int>(42); }};
    auto lease{cache.acquire(create)};
    require(static_cast<bool>(lease), "无法获取租约");
    PreparedInputRef prepared{std::make_shared<Input>(ModelInputContract{{1, 3, 127, 127}}, lease)};
    lease.reset();
    auto adapted{adapt(prepared, "lease")};
    prepared.reset();
    trace->consume = [&](const std::vector<Tensor> &) { require(!cache.acquire(create), "执行中输入缓存被复用"); };
    Executor executor{backboneDescription(), trace};
    require(static_cast<bool>(executor.execute({adapted.value})), "租约输入失败");
    require(!cache.acquire(create), "适配所有者尚在但租约被释放");
    adapted.value = {};
    require(static_cast<bool>(cache.acquire(create)), "输入所有者释放后无法复用");
}
void checkNano() {
    auto backbone{std::make_shared<Trace>()};
    auto head{std::make_shared<Trace>()};
    backbone->output_shapes = {{1, 8, 4, 4}};
    head->output_shapes = {{1, 4, 15, 15}, {1, 2, 15, 15}};
    JetsonNanoTrackBackend backend{std::make_unique<Executor>(backboneDescription(), backbone),
                                   std::make_unique<Executor>(headDescription(), head), adapt};
    require(!backend.search(image(255)) && backbone->calls == 0, "无模板仍执行搜索");
    require(static_cast<bool>(backend.setTemplate(image(127))), "模板失败");
    std::shared_ptr<const TensorStorage> original;
    head->consume = [&](const std::vector<Tensor> &inputs) {
        require(inputs[0].read().data()[0] == 1, "模板不是第一个输入");
        require(inputs[1].read().data()[0] > 1, "搜索不是第二个输入");
        original = inputs[0].storage;
    };
    auto result{backend.search(image(255))};
    require(result && result.value.classification.descriptor.shape == TensorShape{1, 2, 15, 15} &&
                result.value.location.descriptor.shape == TensorShape{1, 4, 15, 15},
            "颠倒输出未按角色识别");
    require(!head->seen[0].expired() && head->seen[1].expired(), "特征所有权未按模板/搜索生命周期释放");
    backbone->failure = true;
    require(!backend.search(image(255)), "搜索失败未传播");
    backbone->failure = false;
    head->consume = [&](const std::vector<Tensor> &inputs) {
        require(inputs[0].storage == original, "搜索失败改变模板身份");
    };
    require(static_cast<bool>(backend.search(image(255))), "搜索失败后无法恢复");
    head->failure = true;
    require(!backend.search(image(255)), "Head 失败未传播");
    head->failure = false;
    require(static_cast<bool>(backend.search(image(255))), "Head 失败丢失模板");
    for (const auto &shapes : std::vector<std::vector<TensorShape>>{{{1, 2, 15, 15}, {1, 2, 15, 15}},
                                                                    {{1, 2, 15, 15}},
                                                                    {{1, 2, 15, 15}, {1, 4, 15, 15}, {1, 1}},
                                                                    {{1, 2, 14, 15}, {1, 4, 15, 15}}}) {
        head->output_shapes = shapes;
        auto bad{backend.search(image(255))};
        require(!bad && !bad.value.classification.storage && !bad.value.location.storage, "异常角色暴露部分输出");
    }
    head->output_shapes = {{1, 2, 15, 15}, {1, 4, 15, 15}};
    const int head_calls{head->calls};
    backbone->output_shapes = {{1, 8, 5, 4}};
    require(!backend.search(image(255)) && head->calls == head_calls, "Head 第二输入不匹配仍执行");
    backbone->output_shapes = {{1, 8, 4, 4}};
    head->consume = {};
    backbone->failure = true;
    require(!backend.setTemplate(image(127)), "模板执行失败未传播");
    const int calls{backbone->calls};
    require(!backend.search(image(255)) && backbone->calls == calls, "新模板失败仍复用旧模板");
    backbone->failure = false;
    require(static_cast<bool>(backend.setTemplate(image(127))), "连续目标无法建立模板");
    head->consume = [&](const std::vector<Tensor> &inputs) {
        require(inputs[0].storage != original, "新目标仍使用旧模板");
    };
    require(static_cast<bool>(backend.search(image(255))), "新目标搜索失败");
    head->invalid = true;
    require(backend.search(image(255)).error.code == ModelError::NotReady, "失效执行器未返回未就绪");
    backend.reset();
    backend.reset();
    const int reset_calls{backbone->calls};
    require(!backend.search(image(255)) && backbone->calls == reset_calls, "重置后仍执行搜索");
}
void checkYolo() {
    auto trace{std::make_shared<Trace>()};
    trace->output_shapes = {{1, 5, 10}};
    ModelDescription description{"yolo", {{"actual_image", 0, {1, 3, 64, 64}}}, {{"prediction", 0, {1, 5, 10}}}};
    JetsonYoloBackend backend{std::make_unique<Executor>(description, trace), adapt};
    auto input{std::make_shared<Input>(backend.inputContract())};
    auto output{backend.infer(input)};
    require(output && output.value.attribute_axis == 1 && output.value.candidate_axis == 2, "YOLO 布局错误");
    for (const auto &shape : std::vector<TensorShape>{{2, 5, 10}, {1, 4, 10}, {1, 10}, {1, 10, 5}}) {
        trace->output_shapes = {shape};
        require(!backend.infer(input), "非法 YOLO 输出进入公共解码");
    }
    trace->output_shapes.clear();
    require(!backend.infer(input), "缺失 YOLO 输出未拒绝");
    const int submits{trace->submits};
    require(!backend.infer(image(64)) && trace->submits == submits, "错误像素变换仍执行");
}
void checkFatalPolicy() {
    int descriptors[2]{};
    require(pipe(descriptors) == 0, "pipe failed");
    const pid_t child{fork()};
    require(child >= 0, "fork failed");
    if (child == 0) {
        close(descriptors[0]);
        struct Sentinel {
            int fd;
            ~Sentinel() {
                const char c{'x'};
                const auto written{write(fd, &c, 1)};
                (void)written;
            }
        } sentinel{descriptors[1]};
        require(sentinel.fd >= 0, "无效的析构观察管道");
        terminateDeviceFailure("mock-engine", "injected synchronization failure");
    }
    close(descriptors[1]);
    int status{};
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) != 0,
            "致命故障没有非零退出");
    char byte{};
    require(read(descriptors[0], &byte, 1) == 0, "致命故障执行了栈展开");
    close(descriptors[0]);
}
} // namespace
int main() {
    try {
        checkContracts();
        checkOwnership();
        checkNano();
        checkYolo();
        checkFatalPolicy();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Model contract test failed: %s\n", error.what());
        return 1;
    }
}

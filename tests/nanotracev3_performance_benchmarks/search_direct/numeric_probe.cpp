#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "direct_session.h"
#include "feature_transfer.h"
#include "model/validation.h"
#include "platform/rk3576/nanotrack_preprocessor.h"
#include "platform/rk3576/rknn_nanotrack_backend.h"
#include "search_direct_backend.h"

namespace {
using namespace search_direct;

struct Options {
    std::string template_model{}, search_model{}, head_model{}, output{};
    int iterations{240};
};

void check(bool ok, const std::string &message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

std::string jsonString(const std::string &value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out << '\\' << c;
        } else if (c < 32) {
            out << "\\u00" << std::hex << (c >> 4) << (c & 0xF) << std::dec;
        } else {
            out << c;
        }
    }
    out << '"';
    return out.str();
}

/**
 * @brief 生成固定种子的 Float32 灰度图并复制到三个通道。
 *
 * 数值范围 0～255 且保留小数，用于确认直连路径没有把输入取整或改变前处理。
 * 同一个种子在任何进程、任何次数下都产生完全相同的图像。
 */
PreparedInputRef seededImage(int size, unsigned seed) {
    std::mt19937 generator{seed};
    std::uniform_real_distribution<float> distribution{0.0f, 255.0f};
    const auto plane{static_cast<std::size_t>(size) * size};
    auto values{std::make_shared<std::vector<float>>(plane * 3)};
    for (std::size_t i{0}; i < plane; ++i) {
        const float pixel{distribution(generator)};
        (*values)[i] = pixel;
        (*values)[plane + i] = pixel;
        (*values)[2 * plane + i] = pixel;
    }
    ModelInputContract contract;
    contract.shape = {1, 3, size, size};
    contract.element_type = TensorElementType::Float32;
    contract.layout = TensorLayout::Nchw;
    contract.scale = 1.0f;
    contract.offset = 0.0f;
    return std::make_shared<const rk3576::FloatInput>(values, contract);
}

std::vector<float> flatten(const model::NanoPrediction &prediction) {
    std::vector<float> values;
    for (const auto *tensor : {&prediction.classification, &prediction.location}) {
        const auto view{tensor->read()};
        values.insert(values.end(), view.data(), view.data() + view.size());
    }
    return values;
}

double maximumDifference(const std::vector<float> &a, const std::vector<float> &b) {
    double worst{};
    for (std::size_t i{0}; i < a.size() && i < b.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
    }
    return worst;
}

bool allFinite(const std::vector<float> &values) {
    return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
}

/**
 * @brief 用已通过实机验证的探针顺序单独驱动一组会话。
 *
 * 探针顺序是：head 提交模板 → head 绑定搜索输入 → search 提交图像 → search 绑定输出
 * → 执行 search → 执行 head。它与正式程序"先 search 后 head"的阶段顺序不同，
 * 但同样满足"每次普通提交之后再绑定"这一条局部规则。本类用于把两种顺序直接比对。
 */
class ProbeOrderRunner {
public:
    ProbeOrderRunner(const Options &options, SharedMemoryApi api) {
        auto z{DirectSession::load(options.template_model, api)};
        check(static_cast<bool>(z), "探针顺序：模板会话加载失败");
        auto x{DirectSession::load(options.search_model, api)};
        check(static_cast<bool>(x), "探针顺序：搜索会话加载失败");
        auto head{DirectSession::load(options.head_model, api)};
        check(static_cast<bool>(head), "探针顺序：head 会话加载失败");
        template_ = std::move(z.value);
        search_ = std::move(x.value);
        head_ = std::move(head.value);
        const auto roles{resolveNanoTrackRoles(template_->attributes(), search_->attributes(), head_->attributes())};
        check(static_cast<bool>(roles), "探针顺序：角色识别失败");
        roles_ = roles.value;
        auto contract{matchSharedFeature(search_->nativeOutputs()[0], search_->outputs()[0],
                                         head_->nativeInputs()[roles_.search_input],
                                         head_->inputs()[roles_.search_input], "probe order")};
        check(static_cast<bool>(contract), "探针顺序：共享特征不兼容");
        contract_ = contract.value;
        auto allocated{search_->allocate(contract_.bytes)};
        check(static_cast<bool>(allocated), "探针顺序：共享内存分配失败");
        shared_ = allocated.value;
        auto imported{head_->importFrom(*shared_)};
        check(static_cast<bool>(imported), "探针顺序：共享内存导入失败");
        imported_ = imported.value;
    }
    ~ProbeOrderRunner() noexcept {
        if (imported_) {
            head_->releaseMemory(imported_);
        }
        if (shared_) {
            search_->releaseMemory(shared_);
        }
    }
    ProbeOrderRunner(const ProbeOrderRunner &) = delete;
    ProbeOrderRunner &operator=(const ProbeOrderRunner &) = delete;

    void setTemplate(const PreparedInputRef &input) {
        auto tensor{imageTensor(input, template_->identity(), 127)};
        check(static_cast<bool>(tensor), "探针顺序：模板输入不合契约");
        auto request{submission(tensor.value, template_->inputs()[0])};
        check(static_cast<bool>(template_->submit({request})), "探针顺序：模板提交失败");
        check(static_cast<bool>(template_->run()), "探针顺序：模板执行失败");
        auto outputs{template_->fetch()};
        check(static_cast<bool>(outputs), "探针顺序：模板输出读取失败");
        feature_ = toNchw(outputs.value[0]);
    }

    model::NanoPrediction search(const PreparedInputRef &input) {
        check(static_cast<bool>(feature_.storage), "探针顺序：缺少有效模板");
        // 1. head 先普通提交模板，2. 随后重新绑定 head 的搜索输入。
        auto head_request{submission(feature_, head_->inputs()[roles_.template_input])};
        check(static_cast<bool>(head_->submit({head_request})), "探针顺序：模板特征提交失败");
        check(static_cast<bool>(head_->bind(contract_.consumer, imported_)), "探针顺序：head 绑定失败");
        // 3. search 普通提交图像，4. 随后重新绑定 search 的输出。
        auto tensor{imageTensor(input, search_->identity(), 255)};
        check(static_cast<bool>(tensor), "探针顺序：搜索输入不合契约");
        auto search_request{submission(tensor.value, search_->inputs()[0])};
        check(static_cast<bool>(search_->submit({search_request})), "探针顺序：搜索提交失败");
        check(static_cast<bool>(search_->bind(contract_.producer, shared_)), "探针顺序：搜索绑定失败");
        // 5. 执行 search，6. 执行 head。两次执行之间不读取、复制或转换搜索特征。
        check(static_cast<bool>(search_->run()), "探针顺序：搜索执行失败");
        search_->confirmCompleted();
        check(static_cast<bool>(head_->run()), "探针顺序：head 执行失败");
        auto outputs{head_->fetch()};
        check(static_cast<bool>(outputs), "探针顺序：head 输出读取失败");
        auto prediction{model::nanoPrediction(
            {toNchw(outputs.value[roles_.classification_output]), toNchw(outputs.value[roles_.location_output])},
            head_->identity())};
        check(static_cast<bool>(prediction), "探针顺序：head 输出不合契约");
        return prediction.value;
    }

private:
    std::unique_ptr<DirectSession> template_;
    std::unique_ptr<DirectSession> search_;
    std::unique_ptr<DirectSession> head_;
    rk3576::NanoTrackRoles roles_{};
    SharedFeatureContract contract_{};
    rknn_tensor_mem *shared_{};
    rknn_tensor_mem *imported_{};
    model::Tensor feature_{};
};
} // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "用法: nanotrackv3_search_direct_probe --template-model 路径 --search-model 路径 "
                     "--head-model 路径 [--iterations 240] [--output 文件.json]\n"
                     "在实机上用固定种子的 Float32 输入，逐元素比较三条路径的 head 原始输出：\n"
                     "  1) 正式程序的先 search 后 head 顺序；\n"
                     "  2) 已通过实机验证的探针顺序；\n"
                     "  3) 完全独立的普通 search 会话加普通 head 会话参照。\n"
                     "参照不使用被测共享缓冲区生成自身输入。任一样本不一致即以非零码退出。\n";
        return 0;
    }
    Options options;
    try {
        for (int i{1}; i < argc; ++i) {
            const std::string name{argv[i]};
            check(i + 1 < argc, "选项 " + name + " 缺少参数");
            const std::string value{argv[++i]};
            if (name == "--template-model") {
                options.template_model = value;
            } else if (name == "--search-model") {
                options.search_model = value;
            } else if (name == "--head-model") {
                options.head_model = value;
            } else if (name == "--output") {
                options.output = value;
            } else if (name == "--iterations") {
                options.iterations = std::stoi(value);
            } else {
                check(false, "未知选项 " + name);
            }
        }
        check(!options.template_model.empty() && !options.search_model.empty() && !options.head_model.empty(),
              "需要三份模型路径；使用 --help 查看用法");
        check(options.iterations > 0, "迭代次数必须为正");
    } catch (const std::exception &error) {
        std::cerr << "参数错误: " << error.what() << '\n';
        return 2;
    }

    std::ostringstream json;
    bool passed{false};
    try {
        const auto api{nativeSharedMemoryApi()};
        auto formal{SearchDirectBackend::load(options.template_model, options.search_model, options.head_model, api)};
        check(static_cast<bool>(formal), "正式后端加载失败: " + model::describe(formal.error));
        ProbeOrderRunner probe{options, api};
        // 参照使用完全独立的三个普通会话，不共享被测缓冲区，也不读取其数据。
        auto reference{rk3576::RknnNanoTrackBackend::load(options.template_model, options.search_model,
                                                          options.head_model, api.core,
                                                          {api.create_mem, api.create_mem_from_fd, api.destroy_mem,
                                                           api.set_io_mem, api.mem_sync})};
        check(static_cast<bool>(reference), "独立普通参照加载失败: " + model::describe(reference.error));

        // 固定种子：两个模板、三个搜索输入，搜索按 A/B/A/C 交替，每四次切换模板。
        const std::array<unsigned, 2> template_seeds{11U, 29U};
        const std::array<unsigned, 3> search_seeds{101U, 202U, 303U};
        const std::array<std::size_t, 4> rotation{0, 1, 0, 2};
        std::vector<PreparedInputRef> templates, searches;
        for (const auto seed : template_seeds) {
            templates.push_back(seededImage(127, seed));
        }
        for (const auto seed : search_seeds) {
            searches.push_back(seededImage(255, seed));
        }

        int template_switches{0};
        double worst_formal{}, worst_probe{};
        std::size_t mismatches{}, non_finite{};
        std::set<std::vector<float>> reference_variants;
        std::size_t current_template{templates.size()};
        for (int i{0}; i < options.iterations; ++i) {
            const std::size_t wanted{static_cast<std::size_t>(i / 4) % templates.size()};
            if (wanted != current_template) {
                check(static_cast<bool>(formal.value->setTemplate(templates[wanted])), "正式后端建立模板失败");
                probe.setTemplate(templates[wanted]);
                check(static_cast<bool>(reference.value->setTemplate(templates[wanted])), "参照建立模板失败");
                current_template = wanted;
                ++template_switches;
            }
            const auto index{rotation[static_cast<std::size_t>(i) % rotation.size()]};
            // 参照先执行，确保它的输入与输出完全独立于被测路径。
            auto reference_result{reference.value->search(searches[index])};
            check(static_cast<bool>(reference_result), "参照搜索失败");
            const auto expected{flatten(reference_result.value)};
            auto formal_result{formal.value->search(searches[index])};
            check(static_cast<bool>(formal_result), "正式路径搜索失败: " + model::describe(formal_result.error));
            const auto actual{flatten(formal_result.value)};
            const auto probe_actual{flatten(probe.search(searches[index]))};
            reference_variants.insert(expected);
            if (!allFinite(expected) || !allFinite(actual) || !allFinite(probe_actual)) {
                ++non_finite;
            }
            if (actual != expected || probe_actual != expected) {
                ++mismatches;
            }
            worst_formal = std::max(worst_formal, maximumDifference(actual, expected));
            worst_probe = std::max(worst_probe, maximumDifference(probe_actual, expected));
        }
        passed = mismatches == 0 && non_finite == 0 && reference_variants.size() > 1 && template_switches >= 2;
        json << "{\"schema_version\":1,\"iterations\":" << options.iterations
             << ",\"template_seeds\":[11,29],\"search_seeds\":[101,202,303],\"search_rotation\":\"A/B/A/C\""
             << ",\"template_switches\":" << template_switches << ",\"mismatched_samples\":" << mismatches
             << ",\"non_finite_samples\":" << non_finite
             << ",\"distinct_reference_outputs\":" << reference_variants.size()
             << ",\"max_abs_difference_formal_vs_reference\":" << worst_formal
             << ",\"max_abs_difference_probe_order_vs_reference\":" << worst_probe
             << ",\"comparison\":\"head 分类与位置两项原始输出逐元素比较\""
             << ",\"models\":{\"template\":" << jsonString(options.template_model)
             << ",\"search\":" << jsonString(options.search_model)
             << ",\"head\":" << jsonString(options.head_model) << "},\"passed\":" << (passed ? "true" : "false")
             << "}\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        json << "{\"schema_version\":1,\"passed\":false,\"error\":" << jsonString(error.what()) << "}\n";
        passed = false;
    }
    std::cout << json.str();
    if (!options.output.empty()) {
        std::ofstream out;
        out.exceptions(std::ios::badbit | std::ios::failbit);
        try {
            out.open(options.output, std::ios::binary);
            out << json.str();
        } catch (const std::exception &error) {
            std::cerr << "结果落盘失败: " << error.what() << '\n';
            return 1;
        }
    }
    return passed ? 0 : 1;
}

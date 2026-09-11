#ifndef PREPROCESS_MODEL_INPUT_H
#define PREPROCESS_MODEL_INPUT_H

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "model/tensor.h"

/**
 * @brief 模型输入的完整数值约定，调用方按值保存。
 *
 * 输入为灰度，复制到三个通道。每个值执行 pixel * scale + offset。
 * Jetson YOLO 使用 scale=1/255；RKNN UINT8 YOLO 和 NanoTrack 使用 scale=1，offset 均为零。
 */
struct ModelInputContract {
    TensorShape shape{};
    TensorElementType element_type{TensorElementType::Float32};
    TensorLayout layout{TensorLayout::Nchw};
    float scale{1.0f};
    float offset{0.0f};
};

/** @brief 稳定的失败分类；None 表示成功，InputBusy 表示旧结果仍占用输入缓存。 */
enum class PreprocessError {
    None,
    EmptyFrame,
    UnsupportedFormat,
    InvalidRegion,
    ContractMismatch,
    PlatformTypeMismatch,
    InputBusy,
    PlatformFailure
};

/** @brief 返回错误码的静态名称；借用字符串在进程生命周期内有效。 */
inline const char *preprocessErrorName(PreprocessError error) noexcept {
    switch (error) {
    case PreprocessError::None:
        return "None";
    case PreprocessError::EmptyFrame:
        return "EmptyFrame";
    case PreprocessError::UnsupportedFormat:
        return "UnsupportedFormat";
    case PreprocessError::InvalidRegion:
        return "InvalidRegion";
    case PreprocessError::ContractMismatch:
        return "ContractMismatch";
    case PreprocessError::PlatformTypeMismatch:
        return "PlatformTypeMismatch";
    case PreprocessError::InputBusy:
        return "InputBusy";
    case PreprocessError::PlatformFailure:
        return "PlatformFailure";
    }
    return "Unknown";
}

/**
 * @brief 只读模型输入；结果对象持有存储租约，直到最后一个引用销毁。
 *
 * 租约意味着底层缓存被本结果独占使用，不能被下一次准备覆盖。
 * 消费者必须在同步推理完成后才释放结果。结果可以比预处理器活得更久。
 */
class PreparedModelInput {
  public:
    /** @brief 释放本结果的存储租约，不影响其他缓存的结果。 */
    virtual ~PreparedModelInput() = default;
    /** @brief 借用数值契约；引用只在结果对象存活期间有效。 */
    virtual const ModelInputContract &contract() const noexcept = 0;
};

/** @brief 按值返回操作结果；失败时 value 必须为空值，不得暴露部分输入。 */
template <typename T> struct PreprocessResult {
    T value{};
    PreprocessError error{PreprocessError::None};
    std::string message{};

    /** @brief 只查询成功标记，不转移 value 的所有权。 */
    explicit operator bool() const noexcept { return error == PreprocessError::None; }

    /** @brief 构造失败结果，诊断文本转移到结果中，value 保持空值。 */
    static PreprocessResult failure(PreprocessError code, std::string detail) { return {T{}, code, std::move(detail)}; }
};

/** @brief 共享只读结果；最后一个引用释放后才能复用对应输入缓存。 */
using PreparedInputRef = std::shared_ptr<const PreparedModelInput>;
using PreparedInputResult = PreprocessResult<PreparedInputRef>;

#endif // PREPROCESS_MODEL_INPUT_H

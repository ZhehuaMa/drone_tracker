#ifndef TRT_ENGINE_H
#define TRT_ENGINE_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include "preprocess/model_input.h"
#include "NvInfer.h"
#include <cuda_runtime_api.h>

// Logger for TensorRT info/warning/errors
class Logger : public nvinfer1::ILogger {
    void log(Severity severity, const char* msg) noexcept override;
};

class TensorBuffer {
public:
    TensorBuffer() = default;
    TensorBuffer(const nvinfer1::Dims &dims, size_t element_size);
    ~TensorBuffer();
    TensorBuffer(const TensorBuffer&) = delete;
    TensorBuffer& operator=(const TensorBuffer&) = delete;
    TensorBuffer(TensorBuffer&& other) noexcept;
    TensorBuffer& operator=(TensorBuffer&& other) noexcept;

    void* host_ptr = nullptr;
    void* device_ptr = nullptr;
    size_t size = 0;
    nvinfer1::Dims dims{};
    uint64_t timestamp = 0;
};


class TrtEngine {
public:
    TrtEngine(const std::string& engine_path);
    ~TrtEngine();

    // Run inference on a list of device TensorBuffer inputs
    std::vector<TensorBuffer> infer(const std::vector<const TensorBuffer*>& inputs);

    /** @brief 借用准备输入直到同步推理结束；失败返回分类诊断，不绑定不兼容输入。 */
    PreprocessResult<std::vector<TensorBuffer>> inferPrepared(const PreparedModelInput &input);

    /** @brief 获取指定名称张量的（静态或配置文件）维度 */
    nvinfer1::Dims getTensorDims(const std::string& tensor_name) const;

    /** @brief 获取第一个输入张量的名称 */
    std::string getPrimaryInputName() const;
    
    /** @brief 获取第一个输出张量的名称 */
    std::string getPrimaryOutputName() const;

private:
    void setup();
    void loadEngineFromFile(const std::string& path);

    // TensorRT components
    Logger m_logger;
    std::unique_ptr<nvinfer1::IRuntime> m_runtime;
    std::unique_ptr<nvinfer1::ICudaEngine> m_engine;
    std::unique_ptr<nvinfer1::IExecutionContext> m_context;

    // CUDA stream
    cudaStream_t m_stream{nullptr};
    
    // Buffers management
    std::vector<std::pair<void*, size_t>> m_buffers;
    std::vector<std::string> m_io_tensor_names;
    std::vector<int32_t> m_input_tensor_indices;
    std::vector<int32_t> m_output_tensor_indices;

    std::map<std::string, nvinfer1::Dims> m_io_dims;
};

#endif // TRT_ENGINE_H

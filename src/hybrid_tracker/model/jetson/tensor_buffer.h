#ifndef JETSON_TENSOR_BUFFER_H
#define JETSON_TENSOR_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <NvInfer.h>
#include <cuda_runtime_api.h>

/** @brief Jetson 私有映射存储；预处理与执行器共用，独占释放。 */
class TensorBuffer {
public:
    TensorBuffer() = default;
    TensorBuffer(const nvinfer1::Dims &dims, size_t element_size);
    ~TensorBuffer();
    TensorBuffer(const TensorBuffer &) = delete;
    TensorBuffer &operator=(const TensorBuffer &) = delete;
    TensorBuffer(TensorBuffer &&other) noexcept;
    TensorBuffer &operator=(TensorBuffer &&other) noexcept;

    void *host_ptr{nullptr};
    void *device_ptr{nullptr};
    size_t size{0};
    nvinfer1::Dims dims{};
    uint64_t timestamp{0};
    int device{-1};
};

#endif // JETSON_TENSOR_BUFFER_H

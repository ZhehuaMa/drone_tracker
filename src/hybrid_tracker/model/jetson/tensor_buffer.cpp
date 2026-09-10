#include "model/jetson/tensor_buffer.h"

#include <limits>
#include <stdexcept>

TensorBuffer::TensorBuffer(const nvinfer1::Dims &dims, size_t element_size) : dims{dims} {
    size_t vol{1};
    if (dims.nbDims <= 0 || dims.nbDims > nvinfer1::Dims::MAX_DIMS || element_size == 0) {
        throw std::invalid_argument("Invalid tensor dimensions");
    }
    for (int d{0}; d < dims.nbDims; ++d) {
        if (dims.d[d] <= 0 || vol > std::numeric_limits<size_t>::max() / static_cast<size_t>(dims.d[d])) {
            throw std::invalid_argument("Invalid tensor size");
        }
        vol *= static_cast<size_t>(dims.d[d]);
    }
    if (vol > std::numeric_limits<size_t>::max() / element_size) {
        throw std::invalid_argument("Tensor byte size overflow");
    }
    size = vol * element_size;
    if (cudaGetDevice(&device) != cudaSuccess) {
        throw std::runtime_error("Cannot query storage device");
    }
    if (cudaHostAlloc(&host_ptr, size, cudaHostAllocMapped) != cudaSuccess) {
        throw std::runtime_error("Failed to allocate mapped tensor storage");
    }
    if (cudaHostGetDevicePointer(&device_ptr, host_ptr, 0) != cudaSuccess) {
        cudaFreeHost(host_ptr);
        host_ptr = nullptr;
        throw std::runtime_error("Failed to map tensor device address");
    }
}

TensorBuffer::~TensorBuffer() {
    if (host_ptr) {
        cudaFreeHost(host_ptr);
        host_ptr = nullptr;
        device_ptr = nullptr;
    }
}

TensorBuffer::TensorBuffer(TensorBuffer &&other) noexcept
    : host_ptr{other.host_ptr}, device_ptr{other.device_ptr}, size{other.size}, dims{other.dims},
      timestamp{other.timestamp}, device{other.device} {
    other.host_ptr = nullptr;
    other.device_ptr = nullptr;
    other.size = 0;
}

TensorBuffer &TensorBuffer::operator=(TensorBuffer &&other) noexcept {
    if (this != &other) {
        if (host_ptr) {
            cudaFreeHost(host_ptr);
        }
        host_ptr = other.host_ptr;
        device_ptr = other.device_ptr;
        size = other.size;
        dims = other.dims;
        timestamp = other.timestamp;
        device = other.device;

        other.host_ptr = nullptr;
        other.device_ptr = nullptr;
        other.size = 0;
    }
    return *this;
}

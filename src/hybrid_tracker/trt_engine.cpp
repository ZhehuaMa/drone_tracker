#include "trt_engine.h"
#include <fstream>
#include <iostream>
#include <numeric>

TensorBuffer::TensorBuffer(const nvinfer1::Dims &dims, size_t element_size)
    : dims(dims)
{
    size_t vol = 1;
    for (int d = 0; d < dims.nbDims; ++d) {
        vol *= dims.d[d];
    }
    size = vol * element_size;
    cudaHostAlloc(&host_ptr, size, cudaHostAllocMapped);
    cudaHostGetDevicePointer(&device_ptr, host_ptr, 0);
}

TensorBuffer::~TensorBuffer() {
    if (host_ptr) {
        cudaFreeHost(host_ptr);
        host_ptr = nullptr;
        device_ptr = nullptr;
    }
}

TensorBuffer::TensorBuffer(TensorBuffer&& other) noexcept
    : host_ptr(other.host_ptr), device_ptr(other.device_ptr), size(other.size), dims(other.dims), timestamp(other.timestamp)
{
    other.host_ptr = nullptr;
    other.device_ptr = nullptr;
    other.size = 0;
}

TensorBuffer& TensorBuffer::operator=(TensorBuffer&& other) noexcept {
    if (this != &other) {
        if (host_ptr) {
            cudaFreeHost(host_ptr);
        }
        host_ptr = other.host_ptr;
        device_ptr = other.device_ptr;
        size = other.size;
        dims = other.dims;
        timestamp = other.timestamp;

        other.host_ptr = nullptr;
        other.device_ptr = nullptr;
        other.size = 0;
    }
    return *this;
}

void Logger::log(Severity severity, const char* msg) noexcept {
    // Suppress info-level messages
    if (severity <= Severity::kWARNING) {
        std::cout << msg << std::endl;
    }
}

TrtEngine::TrtEngine(const std::string& engine_path) {
    loadEngineFromFile(engine_path);
    if (!m_engine) {
        throw std::runtime_error("Failed to load TensorRT engine.");
    }
    setup();
}

TrtEngine::~TrtEngine() {
    for (auto& buffer_pair : m_buffers) {
        if (buffer_pair.first) {
            cudaFree(buffer_pair.first);
        }
    }
    if (m_stream) {
        cudaStreamDestroy(m_stream);
    }
}

void TrtEngine::loadEngineFromFile(const std::string& path) {
    std::ifstream engine_file(path, std::ios::binary);
    if (!engine_file) {
        throw std::runtime_error("Could not open engine file: " + path);
    }

    engine_file.seekg(0, std::ifstream::end);
    long int fsize = engine_file.tellg();
    engine_file.seekg(0, std::ifstream::beg);

    std::vector<char> engine_data(fsize);
    engine_file.read(engine_data.data(), fsize);

    m_runtime = std::unique_ptr<nvinfer1::IRuntime>(nvinfer1::createInferRuntime(m_logger));
    m_engine = std::unique_ptr<nvinfer1::ICudaEngine>(
        m_runtime->deserializeCudaEngine(engine_data.data(), fsize)
    );
}

void TrtEngine::setup() {
    m_context = std::unique_ptr<nvinfer1::IExecutionContext>(m_engine->createExecutionContext());
    if (!m_context) {
        throw std::runtime_error("Failed to create execution context.");
    }

    if (cudaStreamCreate(&m_stream) != cudaSuccess) {
        throw std::runtime_error("Failed to create CUDA stream.");
    }

    const int32_t num_tensors = m_engine->getNbIOTensors();
    m_buffers.resize(num_tensors);
    m_io_tensor_names.resize(num_tensors);
    m_io_dims.clear();

    for (int32_t i = 0; i < num_tensors; ++i) {
        const char* name = m_engine->getIOTensorName(i);
        // printf("Tensor %d : %s \n", i, name);
        m_io_tensor_names[i] = std::string(name);
        m_io_dims[name] = m_engine->getTensorShape(name);

        if (m_engine->getTensorIOMode(name) == nvinfer1::TensorIOMode::kINPUT) {
            m_input_tensor_indices.push_back(i);
        } else {
            m_output_tensor_indices.push_back(i);
        }
    }
}

std::vector<TensorBuffer> TrtEngine::infer(const std::vector<TensorBuffer*>& inputs)
{
    if (inputs.size() != m_input_tensor_indices.size()) {
        throw std::runtime_error("Number of inputs does not match engine's expected number of inputs.");
    }

    // --- Set input shapes and allocate/reallocate buffers ---
    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = *inputs[i];
        int32_t binding_index = m_input_tensor_indices[i];
        const char* name = m_io_tensor_names[binding_index].c_str();

        if (!m_context->setInputShape(name, input.dims)) {
             throw std::runtime_error("Failed to set input shape for " + std::string(name));
        }
        m_context->setTensorAddress(name, input.device_ptr);
    }

    // Ensure all input shapes are set before getting output shapes
    if (!m_context->allInputDimensionsSpecified()) {
        throw std::runtime_error("Not all input dimensions have been specified.");
    }

    std::vector<TensorBuffer> outputs;
    for (int32_t binding_index : m_output_tensor_indices) {
        const char* name = m_io_tensor_names[binding_index].c_str();
        TensorBuffer out_mem(m_context->getTensorShape(name), sizeof(float));
        // printf("Output tensor %s memory address: %p\n", name, out_mem.device_ptr);
        m_context->setTensorAddress(name, out_mem.device_ptr);
        outputs.push_back(std::move(out_mem));
    }
    
    // --- Inference ---
    if (!m_context->enqueueV3(m_stream)) {
        throw std::runtime_error("Failed to enqueue inference.");
    }
    
    cudaStreamSynchronize(m_stream);
    return outputs;
}

nvinfer1::Dims TrtEngine::getTensorDims(const std::string& tensor_name) const
{
    try {
        return m_io_dims.at(tensor_name);
    } catch (const std::out_of_range& e) {
        throw std::runtime_error("Tensor name not found: " + tensor_name);
    }
}

std::string TrtEngine::getPrimaryInputName() const
{
    if (!m_input_tensor_indices.empty()) {
        return m_io_tensor_names[m_input_tensor_indices[0]];
    }
    throw std::runtime_error("No input tensors found.");
}

std::string TrtEngine::getPrimaryOutputName() const
{
    if (!m_output_tensor_indices.empty()) {
        return m_io_tensor_names[m_output_tensor_indices[0]];
    }
    throw std::runtime_error("No output tensors found.");
}


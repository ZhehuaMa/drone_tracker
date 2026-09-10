#include "jetson_model_input.h"

#include <limits>
#include <stdexcept>
#include <utility>

JetsonPreparedModelInput::JetsonPreparedModelInput(std::shared_ptr<TensorBuffer> storage, ModelInputContract contract)
    : storage_{std::move(storage)}, contract_{std::move(contract)} {
    if (!storage_) {
        throw std::invalid_argument("Jetson input requires a storage lease");
    }
}

const ModelInputContract &JetsonPreparedModelInput::contract() const noexcept { return contract_; }
const TensorBuffer &JetsonPreparedModelInput::storage() const noexcept { return *storage_; }

std::shared_ptr<TensorBuffer> allocateJetsonInput(const ModelInputContract &contract) {
    nvinfer1::Dims dims{};
    if (contract.shape.empty() || contract.shape.size() > sizeof(dims.d) / sizeof(dims.d[0])) {
        throw std::invalid_argument("Invalid input rank");
    }
    dims.nbDims = static_cast<int>(contract.shape.size());
    std::size_t bytes{sizeof(float)};
    for (int i{0}; i < dims.nbDims; ++i) {
        const int extent{contract.shape[i]};
        if (extent <= 0 || bytes > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(extent)) {
            throw std::invalid_argument("Invalid input extent or allocation overflow");
        }
        bytes *= static_cast<std::size_t>(extent);
        dims.d[i] = extent;
    }
    auto storage{std::make_shared<TensorBuffer>(dims, sizeof(float))};
    if (!storage->host_ptr || !storage->device_ptr || storage->size != bytes) {
        throw std::runtime_error("Failed to allocate Jetson input storage");
    }
    return storage;
}

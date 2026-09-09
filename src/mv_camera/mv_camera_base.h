#pragma once

#include "readerwriterqueue.h"
#include "frame/frame.h"

#include <chrono>
#include <functional>
#include <memory>

#include <vpi/VPI.h>

class NvBuffer;

class MVCameraBase {
public:
    using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>;
    using ImageQueue = moodycamel::BlockingReaderWriterQueue<FrameRef>;
    using CallbackFn = std::function<void(const VPIImagePlanePitchLinear &)>;

    virtual ~MVCameraBase() = default;

    virtual bool run(CallbackFn &&callback=nullptr) = 0;
    virtual bool run_async(CallbackFn &&callback=nullptr) = 0;
    virtual bool stop() = 0;
    virtual bool is_running() const = 0;
    virtual bool get_image_size(int &width, int &height) const = 0;

    ImageQueue& getImageQueue() { return image_queue_; }

    
protected:
    static std::shared_ptr<NvBuffer> create_nv_buffer(uint8_t *y_ptr, uint8_t *uv_ptr, uint32_t w, uint32_t h, uint32_t stride);
    explicit MVCameraBase(int queue_size) : image_queue_(queue_size) {}
    ImageQueue image_queue_;
};

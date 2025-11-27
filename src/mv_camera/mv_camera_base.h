#pragma once 

#include "readerwriterqueue.h"
#include "image_wrapper.h"

#include <functional>
#include <chrono>

class MVCameraBase {
public:
    using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>;
    using ImageQueue = moodycamel::BlockingReaderWriterQueue<std::shared_ptr<VPIImageWrapper>>;
    using CallbackFn = std::function<void(const VPIImagePlanePitchLinear &)>;

    virtual ~MVCameraBase() = default;

    virtual bool run(CallbackFn &&callback=nullptr) = 0;
    virtual bool run_async(CallbackFn &&callback=nullptr) = 0;
    virtual bool stop() = 0;
    virtual bool is_running() const = 0;

    ImageQueue& getImageQueue() { return image_queue_; }
    
protected:
    explicit MVCameraBase(int queue_size) : image_queue_(queue_size) {}
    ImageQueue image_queue_;
};

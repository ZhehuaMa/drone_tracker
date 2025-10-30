#pragma once 

#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>

#include <vpi/VPI.h>

#include "readerwriterqueue.h"
#include "image_wrapper.h"

class MVCamera {
public:
    using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>;
    using ImageQueue = moodycamel::BlockingReaderWriterQueue<std::shared_ptr<VPIImageWrapper>>;
    using CallbackFn = std::function<void(const VPIImagePlanePitchLinear &)>;

    MVCamera(const std::string &device, int width, int height,
                int fps, bool triggerMode,
                int displayWidth, int displayHeight);

    ~MVCamera();

    void run(bool need_save, const std::string &save_path=".", CallbackFn &&callback=nullptr);
    bool run_async(bool need_save, const std::string &save_path=".", CallbackFn &&callback=nullptr);
    void stop();
    ImageQueue& getImageQueue() { return image_queue_; }
    bool is_running() const { return initialized_ && is_running_.load(); }
private:
    std::string devicePath_;
    int fd_ = -1;
    int width_ = 0;
    int height_ = 0;
    int pitch_ = 0;
    int fps_ = 0;
    bool triggerMode_ = false;
    int displayWidth_ = 0;
    int displayHeight_ = 0;

    std::vector<std::pair<void*, size_t>> buffers_;
    std::deque<time_point> requestQueue_;
    std::mutex queueMutex_;
    std::atomic<bool> is_running_{false};
    std::thread triggerThread_;
    std::thread captureThread_;
    bool streaming_ = false;
    bool initialized_ = false;
    
    ImageQueue image_queue_ {10};

    void *uv_ptr_ = nullptr;
    size_t uv_size_ = 0;

    void *jpegenc_ = nullptr;
    VPIStream vpiStream_ = nullptr;
    
    void triggerLoop(double interval_seconds);
    void init();
    void captureLoop(bool need_save, const std::string &save_path, CallbackFn &&callback);
    void cleanup();
};
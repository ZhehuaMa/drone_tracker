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

#include "mv_camera_base.h"

class MVCamera : public MVCameraBase {
public:
    MVCamera(const std::string &device, int width, int height,
             int fps, bool triggerMode,
             int displayWidth, int displayHeight, 
             bool flip, bool mirror);

    virtual ~MVCamera();

    void setup(bool need_save, const std::string &save_path=".");

    virtual void run(CallbackFn &&callback=nullptr) override;
    virtual bool run_async(CallbackFn &&callback=nullptr) override;
    virtual void stop() override;
    virtual bool is_running() const override { return initialized_ && is_running_.load(); }
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

    bool need_save_ = false;
    std::string save_path_ = ".";

    std::vector<std::pair<void*, size_t>> buffers_;
    std::deque<time_point> requestQueue_;
    std::mutex queueMutex_;
    std::atomic<bool> is_running_{false};
    std::thread triggerThread_;
    std::thread captureThread_;
    bool streaming_ = false;
    bool initialized_ = false;

    void *uv_ptr_ = nullptr;
    size_t uv_size_ = 0;

    void *jpegenc_ = nullptr;
    VPIStream vpiStream_ = nullptr;
    
    void triggerLoop(double interval_seconds);
    void init();
    void captureLoop(CallbackFn &&callback);
    void cleanup();
};
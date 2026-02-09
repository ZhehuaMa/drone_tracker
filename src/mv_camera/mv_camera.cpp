#include "mv_camera.h"

#include <iostream>
#include <iomanip>

#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <cuda_runtime.h>

#include <vpi/algo/Rescale.h>
#include <vpi/algo/ConvertImageFormat.h>

#include <NvBuffer.h>
#include <NvJpegEncoder.h>

#include "threadpool.h"
#include "mjpeg_writer.h"

// #include <opencv2/opencv.hpp>
// #include <vpi/OpenCVInterop.hpp>

using namespace kestrel;

// I2C definitions
#define I2C_DEV0 "/dev/i2c-10"
#define I2C_DEV1 "/dev/i2c-9"
#define I2C_ADDR 0x3b
#define TRIGGER_COUNT   0x0418
#define MODEL_NAME   0x0004

// V4L2 Control IDs from Python script
#define TEGRA_CAMERA_CID_BASE (V4L2_CTRL_CLASS_CAMERA | 0x2000)
#define TEGRA_CAMERA_CID_OVERRIDE_ENABLE (TEGRA_CAMERA_CID_BASE + 101)
#define TEGRA_CAMERA_CID_LOW_LATENCY (TEGRA_CAMERA_CID_BASE + 109)
#define TEGRA_CAMERA_CID_PREFERRED_STRIDE (TEGRA_CAMERA_CID_BASE + 110)
#define TEGRA_CAMERA_CID_OVERRIDE_CAP_TIMEOUT_MS (TEGRA_CAMERA_CID_BASE + 111)
#define TEGRA_CAMERA_CID_VI_TIME_OUT_DISABLE (TEGRA_CAMERA_CID_BASE + 120)

#define V4L2_CID_VEYE_MV_BASE (V4L2_CID_USER_BASE + 0x1000)
#define V4L2_CID_VEYE_MV_TRIGGER_MODE (V4L2_CID_VEYE_MV_BASE + 1)
#define V4L2_CID_VEYE_MV_TRIGGER_SRC (V4L2_CID_VEYE_MV_BASE + 2)
#define V4L2_CID_VEYE_MV_SOFT_TRGONE (V4L2_CID_VEYE_MV_BASE + 3)
#define V4L2_CID_VEYE_MV_FRAME_RATE (V4L2_CID_VEYE_MV_BASE + 4)
#define V4L2_CID_VEYE_MV_ROI_X (V4L2_CID_VEYE_MV_BASE + 5)
#define V4L2_CID_VEYE_MV_ROI_Y (V4L2_CID_VEYE_MV_BASE + 6)

#define MEMORY_ALIGNMENT 64

using namespace std::chrono_literals;

static const std::unordered_map<uint16_t, std::pair<int, int>> ModelResolutionMap = {
    { 0x264, {2464, 2056} },  // MV-IMX264M
    { 0x265, {2064, 1544} },  // MV-IMX265M
};

static void fourcc_to_str(int fourcc, char *str) {
    str[0] = fourcc & 0xFF;
    str[1] = (fourcc >> 8) & 0xFF;
    str[2] = (fourcc >> 16) & 0xFF;
    str[3] = (fourcc >> 24) & 0xFF;
    str[4] = '\0';
}

static void set_ctrl(int fd, int id, int value) {
    v4l2_control ctrl{};
    ctrl.id = id;
    ctrl.value = value;
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        perror("VIDIOC_S_CTRL");
    }
}

static int i2c_rd(int fd, uint8_t i2c_addr, uint16_t reg, std::vector<uint8_t> &res) {
    uint8_t req[2] = { uint8_t(reg >> 8), uint8_t(reg & 0xff) };
    struct i2c_rdwr_ioctl_data msgset{};
    struct i2c_msg msgs[2] = {
        {.addr = i2c_addr, .flags = 0, .len = 2, .buf = req},
        {.addr = i2c_addr, .flags = I2C_M_RD, .len = (uint16_t)res.size(), .buf = res.data()}
    };
    msgset.msgs = msgs;
    msgset.nmsgs = 2;
    int err = ioctl(fd, I2C_RDWR, &msgset);
    if (err != (int)msgset.nmsgs) {
        fprintf(stderr, "Read i2c err %d\n", err);
        return -1;
    }
    return 0;
}

void MVCamera::set_image_flip_mirror(bool flip, bool mirror) {
    int i2c_fd = open(i2c_dev, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C device");
        return;
    }
    if (ioctl(i2c_fd, I2C_SLAVE_FORCE, I2C_ADDR) < 0) {
        perror("Failed to set I2C slave address");
        close(i2c_fd);
        return;
    }
    // #define IMAGE_DIR_REG   0x828
    uint8_t val = 0;
    if (flip) val |= 0x02;
    if (mirror) val |= 0x01;
    uint8_t dir_msg[] = {0x08, 0x28, 0, 0, 0, val};
    if (write(i2c_fd, dir_msg, sizeof(dir_msg)) != (ssize_t)sizeof(dir_msg)) {
        perror("Failed to write to I2C device for image dir");
    }
    close(i2c_fd);
}

MVCamera::MVCamera(
        const std::string &device, int width, int height,
        int fps, bool triggerMode,
        int displayWidth, int displayHeight, bool flip, bool mirror)
    : MVCameraBase(10), devicePath_(device), width_(width), height_(height), fps_(fps),
      triggerMode_(triggerMode), displayWidth_(displayWidth), displayHeight_(displayHeight) {
    pitch_ = int(ceil(double(width_) / MEMORY_ALIGNMENT) * MEMORY_ALIGNMENT);
    // std::cout << "Using pitch: " << pitch_ << std::endl;

    jpegenc_ = NvJPEGEncoder::createJPEGEncoder("jpegenc");
    if (vpiStreamCreate(0, &vpiStream_) != VPI_SUCCESS) {
        std::cerr << "Failed to create VPI stream" << std::endl;
        return;
    }

    init();
    set_image_flip_mirror(flip, mirror);
}

MVCamera::~MVCamera() {
    stop();

    if (vpiStream_) { 
        vpiStreamDestroy(vpiStream_); 
        vpiStream_ = nullptr; 
    }
    if (jpegenc_) {
        delete reinterpret_cast<NvJPEGEncoder*>(jpegenc_);
        jpegenc_ = nullptr;
    }
}

void MVCamera::setup(bool need_save, const std::string &save_path, const std::string &filename_ts) {
    need_save_ = need_save;
    save_path_ = save_path;
    filename_ts_ = filename_ts;
}

bool MVCamera::run(CallbackFn &&callback) {
    if (is_running_ || streaming_) {
        // std::cerr << "Camera is already running." << std::endl;
        return false;
    }
    if (!initialized_) {
        init();
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return false;
    }
    is_running_.store(true);
    streaming_ = true;
    if (triggerMode_) {
        triggerThread_ = std::thread(&MVCamera::triggerLoop, this, 1.0 / fps_);
    }
    captureLoop(std::move(callback));
    return true;
}

bool MVCamera::run_async(CallbackFn &&callback) {
    if (is_running_ || streaming_) {
        // std::cerr << "Camera is already running." << std::endl;
        return false;
    }
    if (!initialized_) {
        init();
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return false;
    }
    is_running_.store(true);
    streaming_ = true;
    if (triggerMode_) {
        triggerThread_ = std::thread(&MVCamera::triggerLoop, this, 1.0 / fps_);
    }
    captureThread_ = std::thread(&MVCamera::captureLoop, this, std::move(callback));
    return true;
}

bool MVCamera::stop() {
    if (!initialized_) {
        std::cerr << "Camera not initialized." << std::endl;
        return false;
    }
    if (!is_running_ && !streaming_) {
        // std::cerr << "Camera is not running." << std::endl;
        return false;
    }
    is_running_.store(false);
    if (triggerMode_ && triggerThread_.joinable()) {
        triggerThread_.join();
    }
    std::cout << "Stopping captureThread_..." << std::endl;
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    requestQueue_.clear(); 
    if (streaming_) {
        std::cout << "Stopping stream..." << std::endl;
        set_ctrl(fd_, TEGRA_CAMERA_CID_VI_TIME_OUT_DISABLE, 0);
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }
    cleanup();
    return true;
}

void MVCamera::triggerLoop(double interval_seconds) {
    int i2c_fd = open(i2c_dev, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C device");
        return;
    }
    if (ioctl(i2c_fd, I2C_SLAVE_FORCE, I2C_ADDR) < 0) {
        perror("Failed to set I2C slave address");
        close(i2c_fd);
        return;
    }
    unsigned char trigger_msg[] = {0x04, 0x14, 0, 0, 0, 1};
    std::chrono::duration<double> interval(interval_seconds);
    uint16_t count_lost = 0;
    
    std::vector<uint8_t> response(4);
    while (is_running_) {
        auto start_time = std::chrono::high_resolution_clock::now();

        if (i2c_rd(i2c_fd, I2C_ADDR, TRIGGER_COUNT, response) == 0) {
            uint16_t count_lost_temp = response[1] | (response[0] << 8);
            if (count_lost_temp != count_lost) {
                count_lost = count_lost_temp;
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (!requestQueue_.empty()) {
                    requestQueue_.pop_back();
                }
            }
        }

        std::this_thread::sleep_for(1ms); // ensure at least 1ms gap

        if (write(i2c_fd, trigger_msg, sizeof(trigger_msg)) != (ssize_t)sizeof(trigger_msg)) {
            perror("Failed to write to I2C device for trigger");
            break;
        }
        {
            auto ts = std::chrono::high_resolution_clock::now();
            std::lock_guard<std::mutex> lock(queueMutex_);
            requestQueue_.push_back(ts);
        }
        std::chrono::duration<double> elapsed = 
            std::chrono::high_resolution_clock::now() - start_time;
        if (elapsed < interval) {
            std::this_thread::sleep_for(interval - elapsed);
        }
    }
    close(i2c_fd);
}

uint16_t MVCamera::get_model() {
    int i2c_fd = open(i2c_dev, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C device");
        return 0;
    }
    if (ioctl(i2c_fd, I2C_SLAVE_FORCE, I2C_ADDR) < 0) {
        perror("Failed to set I2C slave address");
        close(i2c_fd);
        return 0;
    }
    std::vector<uint8_t> response(4);
    uint16_t model = 0;
    if (i2c_rd(i2c_fd, I2C_ADDR, MODEL_NAME, response) == 0) {
        model = response[3] | (response[2] << 8);
    }
    close(i2c_fd);
    return model;
}

void MVCamera::init() 
{
    if (initialized_) 
        return;
    fd_ = open(devicePath_.c_str(), O_RDWR);
    if (fd_ < 0) {
        perror("Error opening device");
        return;
    }
    v4l2_capability caps{};
    if (ioctl(fd_, VIDIOC_QUERYCAP, &caps) < 0) {
        perror("Error querying capabilities");
        return;
    }

    // try i2c channel
    i2c_dev = I2C_DEV0;
    auto model_id = get_model();
    if (model_id == 0)
    {
        i2c_dev = I2C_DEV1;
        model_id = get_model();
        if (model_id == 0)
            return;
    }
    
    std::cout << "Device: " << caps.driver << "\nCard: " << caps.card << "\nBus Info: " << caps.bus_info << std::endl;
    std::cout << "Model ID: " << std::hex << model_id << std::dec << std::endl;
    int max_width = 1920;
    int max_height = 1080;
    auto it = ModelResolutionMap.find(model_id);
    if (it != ModelResolutionMap.end()) {
        max_width = it->second.first;
        max_height = it->second.second;
        if (width_ > max_width || height_ > max_height) {
            std::cerr << "Requested resolution " << width_ << "x" << height_ 
                      << " exceeds maximum for model " << std::hex << model_id << std::dec
                      << ": " << max_width << "x" << max_height << std::endl;
            return;
        }
    } else {
        std::cerr << "Unknown model ID: " << std::hex << model_id << std::dec << std::endl;
        return;
    }
    int offset_x = (max_width - width_) / 2;
    int offset_y = (max_height - height_) / 2;
    int timeout_ms = (1000 / fps_) / 2 - 3;

    set_ctrl(fd_, V4L2_CID_VEYE_MV_ROI_X, offset_x);
    set_ctrl(fd_, V4L2_CID_VEYE_MV_ROI_Y, offset_y);
    
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.bytesperline = pitch_; // for U8 with pitch
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        perror("Error: Could not set video format.");
        return;
    }
    char pix_fmt_str[5];
    fourcc_to_str(fmt.fmt.pix.pixelformat, pix_fmt_str);
    std::cout << "Format set to: " << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height 
                << ", PixelFormat: " << pix_fmt_str << std::endl;
    set_ctrl(fd_, TEGRA_CAMERA_CID_OVERRIDE_ENABLE, 1);
    set_ctrl(fd_, TEGRA_CAMERA_CID_LOW_LATENCY, 1);
    set_ctrl(fd_, V4L2_CID_VEYE_MV_TRIGGER_MODE, triggerMode_ ? 1 : 0);
    set_ctrl(fd_, V4L2_CID_VEYE_MV_TRIGGER_SRC, 0);
    set_ctrl(fd_, TEGRA_CAMERA_CID_VI_TIME_OUT_DISABLE, triggerMode_ ? 1 : 0);
    set_ctrl(fd_, V4L2_CID_VEYE_MV_FRAME_RATE, fps_);
    set_ctrl(fd_, TEGRA_CAMERA_CID_OVERRIDE_CAP_TIMEOUT_MS, timeout_ms);
    std::cout << "Set frame rate " << fps_ << std::endl;
    v4l2_requestbuffers req{};
    req.count = 8; // fewer buffers for userptr
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return;
    }
    // Determine buffer size
    size_t bufferSize = pitch_ * height_; // for U8 with pitch
    buffers_.resize(req.count);
    for (unsigned int i = 0; i < req.count; ++i) {
        void *ptr = nullptr;
        cudaError_t cerr = cudaHostAlloc(&ptr, bufferSize, cudaHostAllocMapped);
        if (cerr != cudaSuccess) {
            std::cerr << "cudaHostAlloc failed: " << cudaGetErrorString(cerr) << std::endl;
            return;
        }
        buffers_[i].first = ptr;
        buffers_[i].second = bufferSize;
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;
        buf.index = i;
        buf.m.userptr = reinterpret_cast<unsigned long>(ptr);
        buf.length = bufferSize;
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return;
        }
    }
    
    initialized_ = true;
}

void MVCamera::captureLoop(CallbackFn &&callback) {
    std::unique_ptr<ThreadPool> thread_pool;
    std::unique_ptr<MJPEGWriter> mjpeg_writer;
    if (need_save_) {
        thread_pool = std::make_unique<ThreadPool>(1);
        mjpeg_writer = std::make_unique<MJPEGWriter>();
    }

    VPIImageData inData;
    inData.bufferType = VPI_IMAGE_BUFFER_CUDA_PITCH_LINEAR;
    inData.buffer.pitch.format = VPI_IMAGE_FORMAT_U8;
    inData.buffer.pitch.numPlanes = 1;
    inData.buffer.pitch.planes[0].pixelType = VPI_PIXEL_TYPE_U8;
    inData.buffer.pitch.planes[0].width = width_;
    inData.buffer.pitch.planes[0].height = height_;
    inData.buffer.pitch.planes[0].pitchBytes = pitch_; // for U8

    auto t0 = std::chrono::high_resolution_clock::now();
    
    if (mjpeg_writer) {
        std::string filename_ts = filename_ts_;
        if (filename_ts.empty()) {
            filename_ts = std::to_string(t0.time_since_epoch().count()/1000);
        }
        mjpeg_writer->setup(save_path_ + "/ts_" + filename_ts + ".mjpeg", 
            displayWidth_, displayHeight_, fps_);
    }

    uint64_t request_us = 0;
    while (is_running_) {
        int res = -1;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);
        struct timeval tv {1, 0};
        int r = select(fd_ + 1, &fds, NULL, NULL, &tv);
        if (r == -1) {
            if (errno == EINTR) {
                // Interrupted by signal; loop condition will check is_running_
                continue;
            }
            perror("select");
            break;
        }
        if (r == 0) { std::cerr << "select timeout" << std::endl; continue; }
        if (!FD_ISSET(fd_, &fds)) { std::cerr << "fd not set after select" << std::endl; continue; }
        auto now = std::chrono::high_resolution_clock::now();
        if (triggerMode_) {
            std::chrono::time_point<std::chrono::high_resolution_clock> request_ts{};
            bool got_ts = false;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (!requestQueue_.empty()) {
                    request_ts = requestQueue_.front();
                    requestQueue_.pop_front();
                    // while (!requestQueue_.empty() && requestQueue_.front() < now - std::chrono::seconds(1 / fps_)) {
                    //     requestQueue_.pop_front();
                    // }
                    got_ts = true;
                }
            }
            if (!got_ts) {
                std::this_thread::sleep_for(5ms);
                continue;
            }
            request_us = request_ts.time_since_epoch().count()/1000;
            double latency_ms = std::chrono::duration<double, std::milli>(now - request_ts).count();
            // Adjust timestamp based on latency to better reflect actual capture time
            if (latency_ms < 1000.0/fps_) {
                request_us -=  1000000l/fps_;
            }
            else {
                int times = static_cast<int>(latency_ms / (1000.0/fps_));
                if (times > 2)
                    request_us -=  (1000.0/fps_) * (times-1);
            }
            latency_ms = std::chrono::duration<double, std::milli>(now - request_ts).count();
            std::cout << "Latency: " << std::fixed << std::setprecision(2) << latency_ms << " ms" << std::endl;
        }
        else
        {
            request_us = now.time_since_epoch().count()/1000 - 1500000l/fps_; // estimate timestamp
        }
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;
        if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
            break;
        }
        cudaHostGetDevicePointer(&inData.buffer.pitch.planes[0].data, reinterpret_cast<void*>(buf.m.userptr), 0);
        VPIImage vpiIn = nullptr;
        if (vpiImageCreateWrapper(&inData, NULL, 0, &vpiIn) != VPI_SUCCESS) {
            std::cerr << "Failed to wrap buffer into VPI image " << std::endl;
            break;
        }
        // Submit resize
        VPIImage vpiOut = nullptr;
        VPIImage vpiSave = nullptr;
        const int flags = VPI_BACKEND_CUDA | VPI_BACKEND_CPU;
        VPIStatus status = vpiImageCreate(displayWidth_, displayHeight_, VPI_IMAGE_FORMAT_U8, flags, &vpiOut);
        status = vpiSubmitRescale(vpiStream_, VPI_BACKEND_CUDA, vpiIn, vpiOut, VPI_INTERP_LINEAR, VPI_BORDER_CLAMP, 0);
        if (need_save_) {
            status = vpiImageCreate(displayWidth_, displayHeight_, VPI_IMAGE_FORMAT_U8, flags, &vpiSave);
            status = vpiSubmitConvertImageFormat(vpiStream_, VPI_BACKEND_CUDA, vpiOut, vpiSave, 0);
        }
        if (status == VPI_SUCCESS) {
            vpiStreamSync(vpiStream_);
            auto image_buffer = std::make_shared<VPIImageWrapper>(vpiOut, request_us);
            while (!image_queue_.try_enqueue(std::move(image_buffer))) {
                image_queue_.pop(); 
            }
            if (need_save_) {
                thread_pool->enqueue([image=vpiSave, ts=request_us, video_writer=mjpeg_writer.get(), cb=callback, this]() {
                    VPIImageData outData;
                    if (vpiImageLockData(image, VPI_LOCK_READ, VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &outData) != VPI_SUCCESS) {
                        vpiImageDestroy(image);
                        return;
                    }
                    if (cb) {
                        cb(outData.buffer.pitch.planes[0]);
                    }
                    if (uv_ptr_ == nullptr)
                    {
                        uv_size_ = outData.buffer.pitch.planes[0].pitchBytes * outData.buffer.pitch.planes[0].height / 2;
                        auto res = posix_memalign(&uv_ptr_, MEMORY_ALIGNMENT, uv_size_);
                        // Try to keep pages resident
                        mlock(uv_ptr_, uv_size_);
                        memset(uv_ptr_, 0x80, uv_size_); // set UV to mid-range
                    }
                    auto nv_buffer = create_nv_buffer(
                        reinterpret_cast<uint8_t*>(outData.buffer.pitch.planes[0].data), reinterpret_cast<uint8_t*>(uv_ptr_),
                        outData.buffer.pitch.planes[0].width, outData.buffer.pitch.planes[0].height, outData.buffer.pitch.planes[0].pitchBytes);
                    size_t out_buf_size = outData.buffer.pitch.planes[0].width * outData.buffer.pitch.planes[0].height * 3 / 2;
                    auto out_buf = std::vector<uint8_t>(out_buf_size);
                    uint8_t *ptr = out_buf.data();
                    auto res = reinterpret_cast<NvJPEGEncoder*>(jpegenc_)->encodeFromBuffer(*nv_buffer, JCS_YCbCr, &ptr, out_buf_size, 80);
                    vpiImageUnlock(image);
                    if (res==0 && video_writer)
                    {
                        // auto ts_str = std::to_string(ts);
                        // std::ofstream output_file("ts_" + ts_str + ".jpg", std::ios::binary);
                        // output_file.write(reinterpret_cast<const char*>(out_buf.data()), out_buf_size);
                        // output_file.close();
                        video_writer->writeFrame(out_buf.data(), out_buf_size, ts);
                    }
                    vpiImageDestroy(image);
                });
            }

            auto t1 = std::chrono::high_resolution_clock::now();
            double interval_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            // std::cout << "Frame interval " << static_cast<int>(interval_ms + 0.5) << " ms" << std::endl;
            t0 = t1;
        } 
        else 
        {
            char bufErr[VPI_MAX_STATUS_MESSAGE_LENGTH];
            vpiGetLastStatusMessage(bufErr, sizeof(bufErr));
            std::cerr << "vpiSubmitRescale failed: " << bufErr << std::endl;
            if (vpiOut) vpiImageDestroy(vpiOut);
            if (vpiIn) vpiImageDestroy(vpiIn);
            break;
        }

        if (vpiIn) vpiImageDestroy(vpiIn);
        
        // Re-queue buffer
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            break;
        }
    }
}

void MVCamera::cleanup() {
    if (!initialized_) 
        return; // already stopped
    for (auto &b : buffers_) {
        if (b.first) {
            cudaFreeHost(b.first);
        }
    }
    buffers_.clear();

    if (fd_ >= 0) { 
        close(fd_); 
        fd_ = -1; 
    }
    if (uv_ptr_) {
        // Release UV planes memory
        munlock(uv_ptr_, uv_size_);
        free(uv_ptr_);
        uv_ptr_ = nullptr;
    }

    initialized_ = false;
    std::cout << "Cleanup complete." << std::endl;
}

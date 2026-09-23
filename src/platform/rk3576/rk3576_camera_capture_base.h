#ifndef RK3576_CAMERA_CAPTURE_BASE_H
#define RK3576_CAMERA_CAPTURE_BASE_H

// `CameraSystem`/`CameraFormat`/`CameraSample`/`validateCameraConfig` 与
// `Rk3576CameraCaptureBase` 都与具体传感器型号无关，AR0234/SC233HGS 专属类型
// 分别定义在各自的头文件中。

#include <poll.h>
#include <atomic>
#include <ctime>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "camera_timing.h"
#include "platform/platform_factory.h"

namespace rk3576 {
/** @brief Linux 调用边界；除 clockUs/execute 外，替身通过返回值及 errno 报告错误。 */
class CameraSystem {
public:
    virtual ~CameraSystem() = default;
    virtual int open(const std::string &path, int flags);
    virtual int close(int fd);
    virtual int ioctl(int fd, unsigned long request, void *argument);
    virtual void *map(int fd, std::size_t length, std::uint64_t offset);
    virtual int unmap(void *address, std::size_t length);
    virtual int poll(pollfd *fds, nfds_t count, int timeout_ms);
    virtual int event();
    virtual int signal(int fd);
    virtual std::int64_t clockUs(clockid_t clock);
    /** @brief 控制读回重试之间等待；测试替身可推进单调时钟。 */
    virtual void sleepUs(std::int64_t duration_us);
    /** @brief 执行参数数组，返回标准输出；缺工具、超时和非零退出分别抛出诊断。 */
    virtual std::string execute(const std::vector<std::string> &arguments, int timeout_ms);
};
struct CameraFormat {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t stride{};
    std::uint32_t sizeimage{};
    std::uint32_t buffers{};
};
enum class CameraSampleResult { NoSample, WarmupDiscarded, FrameReady };
struct CameraSample {
    CameraSampleResult result{CameraSampleResult::NoSample};
    FrameRef frame{};
    std::int64_t seconds{};
    std::int64_t microseconds{};
    std::int64_t dequeue_us{};
    std::int64_t copy_us{};
    std::int64_t clock_offset_delta_us{};
    std::uint32_t flags{};
    std::uint32_t sequence{};
    std::string strategy{};
    // 验收可选：驱动持有期内逐项比较已复制的同帧像素，不是两次采集画面对照。
    bool pixels_checked{};
    bool pixels_equal{};
};
/** @brief 校验不支持的选项，任何设备操作前调用。 */
void validateCameraConfig(const FrameSourceConfig &config);

/**
 * @brief 与具体传感器型号无关的 V4L2 采集设备生命周期基类。
 *
 * 负责内存映射缓冲区申请/释放（mmap/REQBUFS/QUERYBUF）、出队入队轮询
 * （QBUF/DQBUF/poll）、停止时的唤醒、单调-实时时钟映射，以及把驱动缓冲区
 * 深拷贝为独立的 Gray8 帧。这些逻辑已随 AR0234 完成实机验证，任何改动都要
 * 重新跑一遍既有 AR0234 测试确认行为不变。
 *
 * 具体传感器只通过下方受保护的虚函数扩展点参与三件事：格式协商（是否需要
 * media-ctl、期望的像素格式）、控制项读写与恢复、软件触发支持与否。
 */
class Rk3576CameraCaptureBase {
public:
    Rk3576CameraCaptureBase(FrameSourceConfig config, std::shared_ptr<CameraSystem> system);
    virtual ~Rk3576CameraCaptureBase() = default;
    Rk3576CameraCaptureBase(const Rk3576CameraCaptureBase &) = delete;
    Rk3576CameraCaptureBase &operator=(const Rk3576CameraCaptureBase &) = delete;

    /** @brief 只设置/查询格式，不申请缓冲或开流。 */
    void prepare();
    void start();
    /** @brief 默认不支持软件触发；AR0234 覆写为发送单次软件命令。 */
    virtual void trigger();
    /** @brief 默认没有待恢复控制项；持有传感器控制项的实现覆写。 */
    virtual std::vector<std::string> unrestoredControls() const;
    /** @brief 根据 result 区分无出队、已归还观察帧和合格帧；错误抛异常，等待可被停止唤醒。 */
    CameraSample next(int timeout_ms, bool compare_pixels = false);
    /** @brief 唤醒 poll，不关闭采集句柄；空字符串表示成功。 */
    std::string wake();
    /** @brief 逆序清理并保留所有错误；调用前生产线程必须已退出采集循环。 */
    std::string finish();
    CameraFormat format() const { return format_; }
    CameraClockPair clockPair() const { return pair_; }
    /** @brief 仅采集线程或 join 后读取；公共帧源另用互斥锁发布快照。 */
    CameraCaptureStatus captureStatus() const { return capture_status_; }
    std::uint64_t sequenceGaps() const { return sequence_gaps_; }
    /** @brief 已复制但未交付队列时调用；仅采集线程可调用。 */
    void discardReadyFrame();

protected:
    virtual bool needsTimestampWarmup() const { return false; }
    // ---- 传感器专属扩展点：由 AR0234/SC233HGS 各自实现 ----
    /** @brief 打开采集节点前的传感器配置（例如 media-ctl 设置格式、读写触发控制项）；不需要时留空实现。 */
    virtual void configureSensor() = 0;
    /** @brief 采集节点应该协商/校验的像素格式，例如 V4L2_PIX_FMT_GREY、V4L2_PIX_FMT_NV12。 */
    virtual std::uint32_t pixelFormat() const = 0;
    /** @brief 格式不匹配错误信息里使用的格式名称（不含尾随空格）。 */
    virtual const char *pixelFormatName() const = 0;
    /** @brief prepare() 重入守卫：是否仍持有传感器专属的打开状态（例如未关闭的传感器句柄）。 */
    virtual bool hasOpenSensorState() const = 0;
    /** @brief finish() 收尾阶段调用，恢复/关闭传感器专属资源；返回累计错误文本，无错误返回空串。 */
    virtual std::string finishSensor() = 0;

    const FrameSourceConfig &config() const noexcept { return config_; }
    CameraSystem &system() const noexcept { return *system_; }
    /** @brief 采集是否已成功 STREAMON 且尚未 STREAMOFF；trigger() 等专属操作据此判断是否处于可发送状态。 */
    bool streaming() const noexcept { return streaming_; }
    /** @brief 结果小于 0 时按 `config().path + " " + operation` 与 errno 组装异常抛出。 */
    void check(int result, const std::string &operation) const;

private:
    struct Mapping { void *address{}; std::size_t length{}; };
    void checkFormat();
    FrameSourceConfig config_{};
    std::shared_ptr<CameraSystem> system_{};
    int fd_{-1};
    int wake_fd_{-1};
    std::mutex wake_mutex_{};
    bool allocated_{};
    bool streaming_{};
    CameraFormat format_{};
    CameraClockPair pair_{};
    std::unique_ptr<CameraTiming> timing_{};
    std::unique_ptr<CameraWarmup> warmup_{};
    CameraCaptureStatus capture_status_{};
    std::atomic<bool> stop_requested_{false};
    std::int64_t stream_started_us_{};
    bool have_sequence_{};
    std::uint32_t previous_sequence_{};
    std::uint64_t sequence_gaps_{};
    bool cleanup_failed_{};
    std::vector<Mapping> mappings_{};
};
} // namespace rk3576
#endif // RK3576_CAMERA_CAPTURE_BASE_H

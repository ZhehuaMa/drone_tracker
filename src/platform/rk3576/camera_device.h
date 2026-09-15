#ifndef RK3576_CAMERA_DEVICE_H
#define RK3576_CAMERA_DEVICE_H

#include <poll.h>
#include <ctime>
#include <mutex>
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
struct CameraSample {
    FrameRef frame{};
    std::int64_t seconds{};
    std::int64_t microseconds{};
    std::int64_t dequeue_us{};
    std::int64_t copy_us{};
    std::uint32_t flags{};
    std::uint32_t sequence{};
    std::string strategy{};
    // 验收可选：驱动持有期内逐项比较已复制的同帧像素，不是两次采集画面对照。
    bool pixels_checked{};
    bool pixels_equal{};
};
/** @brief 校验不支持的选项，任何设备操作前调用。 */
void validateCameraConfig(const FrameSourceConfig &config);
/** @brief 单生产线程拥有设备会话；仅 wake 可由控制线程并发调用。 */
class CameraDevice final {
public:
    CameraDevice(FrameSourceConfig config, std::shared_ptr<CameraSystem> system);
    ~CameraDevice();
    CameraDevice(const CameraDevice &) = delete;
    CameraDevice &operator=(const CameraDevice &) = delete;
    /** @brief 只设置/查询格式，不申请缓冲或开流。 */
    void prepare();
    void start();
    /** @brief 无帧/停止返回空帧；设备错误抛异常。每次等待至多 timeout_ms。 */
    CameraSample next(int timeout_ms, bool compare_pixels = false);
    /** @brief 唤醒 poll，不关闭采集句柄；空字符串表示成功。 */
    std::string wake();
    /** @brief 逆序清理并保留所有错误；调用前生产线程必须已退出采集循环。 */
    std::string finish();
    CameraFormat format() const { return format_; }
    CameraClockPair clockPair() const { return pair_; }
private:
    struct Mapping { void *address{}; std::size_t length{}; };
    void check(int result, const std::string &operation);
    void configureSensor();
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
    std::vector<Mapping> mappings_{};
};
} // namespace rk3576
#endif // RK3576_CAMERA_DEVICE_H

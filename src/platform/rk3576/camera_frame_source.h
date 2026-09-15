#ifndef RK3576_CAMERA_FRAME_SOURCE_H
#define RK3576_CAMERA_FRAME_SOURCE_H

#include <functional>
#include <condition_variable>
#include <limits>
#include <thread>

#include "camera_device.h"

namespace rk3576 {
/** @brief 固定周期计划；错过整周期后跳到未来时刻，不补发。 */
struct CameraTriggerPlan {
    std::int64_t origin_us{};
    double period_us{};
    std::uint64_t index{};
    std::int64_t due() const;
    std::uint64_t skipLate(std::int64_t now);
};
struct CameraTriggerEvent {
    std::int64_t before_us{};
    std::int64_t after_us{};
    bool success{};
    std::string error{};
};
struct CameraDiagnostic {
    FrameSourceStatus source{};
    CameraFormat format{};
    CameraClockPair clock_pair{};
    std::uint64_t driver_dropped{};
    std::int64_t cleanup_us{};
    std::int64_t stop_us{};
    std::vector<std::string> unrestored_controls{};
};
/** @brief 系统调用及线程启动注入点；observe 在生产线程同步执行，异常使本轮失败。 */
struct CameraDependencies {
    std::shared_ptr<CameraSystem> system{};
    std::function<std::thread(std::function<void()>)> launch{};
    std::function<void(const CameraSample &, const CameraDiagnostic &)> observe{};
    bool compare_pixels{};
    std::function<void(const CameraTriggerEvent &)> observe_trigger{};
    std::function<std::int64_t()> trigger_now{};
    // 注入等待仍须使用同一停止谓词；默认以条件变量等待相对剩余量，计划始终为绝对时刻。
    std::function<void(std::condition_variable &, std::unique_lock<std::mutex> &,
                       std::int64_t, const std::function<bool()> &)> trigger_wait{};
    std::uint64_t trigger_limit{std::numeric_limits<std::uint64_t>::max()};
};
/** @brief 单生产者、单消费者相机源；start/析构前消费者静止，stop 可以与消费并发。 */
class Rk3576CameraFrameSource final : public FrameSource {
public:
    explicit Rk3576CameraFrameSource(FrameSourceConfig config, CameraDependencies dependencies = {});
    ~Rk3576CameraFrameSource() override;
    bool start() override;
    bool stop() override;
    /** @brief 仅停止发送并等待在途命令返回，供有限尾帧验收使用。 */
    void stopTriggers();
    bool isRunning() const override;
    FrameSourceStatus status() const override;
    CameraDiagnostic diagnostic() const;
    bool getImageSize(int &width, int &height) const override;
    bool waitFrame(FrameRef &frame, std::chrono::microseconds timeout) override;
    bool tryFrame(FrameRef &frame) override;
    /** @brief 启用录像抛出 std::invalid_argument；禁用为无操作。 */
    void configureRecording(bool enabled, const std::string &, const std::string &) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rk3576
#endif // RK3576_CAMERA_FRAME_SOURCE_H

#ifndef RK3576_CAMERA_FRAME_SOURCE_H
#define RK3576_CAMERA_FRAME_SOURCE_H

#include <functional>
#include <thread>

#include "camera_device.h"

namespace rk3576 {
struct CameraDiagnostic {
    FrameSourceStatus source{};
    CameraFormat format{};
    CameraClockPair clock_pair{};
    std::uint64_t driver_dropped{};
    std::int64_t cleanup_us{};
    std::int64_t stop_us{};
};
/** @brief 系统调用及线程启动注入点；observe 在生产线程同步执行，异常使本轮失败。 */
struct CameraDependencies {
    std::shared_ptr<CameraSystem> system{};
    std::function<std::thread(std::function<void()>)> launch{};
    std::function<void(const CameraSample &, const CameraDiagnostic &)> observe{};
    bool compare_pixels{};
};
/** @brief 单生产者、单消费者相机源；start/析构前消费者静止，stop 可以与消费并发。 */
class Rk3576CameraFrameSource final : public FrameSource {
public:
    explicit Rk3576CameraFrameSource(FrameSourceConfig config, CameraDependencies dependencies = {});
    ~Rk3576CameraFrameSource() override;
    bool start() override;
    bool stop() override;
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

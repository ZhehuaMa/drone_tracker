#ifndef PLATFORM_PLATFORM_FACTORY_H
#define PLATFORM_PLATFORM_FACTORY_H

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "frame/frame.h"
#include "hybrid_tracker/model/model_backend.h"
#include "hybrid_tracker/preprocess/image_preprocessor.h"

/** @brief 创建失败的身份信息；path 是模型文件或采集输入路径。 */
struct PlatformDiagnostic {
    std::string platform{};
    std::string component{};
    std::string path{};
    std::string reason{};
    std::string describe() const { return platform + ": " + component + " [" + path + "]: " + reason; }
};

/** @brief 失败时 value 为空，禁止返回半套资源。 */
template <typename T> struct PlatformResult {
    T value{};
    PlatformDiagnostic error{};
    explicit operator bool() const noexcept { return error.reason.empty(); }
    static PlatformResult failure(PlatformDiagnostic error) { return {T{}, std::move(error)}; }
};

enum class FrameSourceType { Camera, Video };
/** @brief 公共采集配置；尺寸和帧率沿用节点参数的像素、帧/秒单位。 */
struct FrameSourceConfig {
    FrameSourceType type{FrameSourceType::Camera};
    std::string path{};
    int width{};
    int height{};
    int fps{};
    bool trigger_mode{true};
    int output_width{};
    int output_height{};
    bool flip{};
    bool mirror{};
    bool use_history_timestamp{};
};

/** @brief 单消费者帧源；实现直接使用原采集队列，不增加中转线程。 */
class FrameSource {
public:
    virtual ~FrameSource() = default;
    /** @brief 启动原采集线程；重复停止必须安全。 */
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool isRunning() const = 0;
    virtual bool getImageSize(int &width, int &height) const = 0;
    /** @brief 最多等待 timeout 后取一帧；超时返回 false。 */
    virtual bool waitFrame(FrameRef &frame, std::chrono::microseconds timeout) = 0;
    virtual bool tryFrame(FrameRef &frame) = 0;
    /** @brief 启动前配置录像；视频文件输入接受配置但不创建相机录像。 */
    virtual void configureRecording(bool need_save, const std::string &directory, const std::string &prefix) = 0;
};

/** @brief 复制主机像素；返回帧独立于原图和创建器存活。 */
class FrameBuilder {
public:
    virtual ~FrameBuilder() = default;
    virtual PlatformResult<FrameRef> copyFrom(const cv::Mat &image, std::uint64_t timestamp_us) = 0;
};

/** @brief 只读且可并发的真实资源检查，不以创建器实例身份判断兼容性。 */
class FrameCompatibility {
public:
    virtual ~FrameCompatibility() = default;
    /** @brief 返回空原因表示兼容；检查不得修改帧或跟踪状态。 */
    virtual PlatformDiagnostic validate(const FrameRef &frame) const = 0;
};

/** @brief 独占组件集合；不借用工厂资源，缺失任一成员时跟踪器初始化失败。 */
struct TrackerComponents {
    std::unique_ptr<model::YoloBackend> yolo{};
    std::unique_ptr<YoloPreprocessor> yolo_preprocessor{};
    std::unique_ptr<model::NanoTrackBackend> nano{};
    std::unique_ptr<NanoTrackPreprocessor> nano_preprocessor{};
    std::unique_ptr<FrameCompatibility> compatibility{};

    TrackerComponents() = default;
    TrackerComponents(const TrackerComponents &) = delete;
    TrackerComponents &operator=(const TrackerComponents &) = delete;
    TrackerComponents(TrackerComponents &&) noexcept = default;
    TrackerComponents &operator=(TrackerComponents &&) noexcept = default;
    bool complete() const noexcept {
        return yolo && yolo_preprocessor && nano && nano_preprocessor && compatibility;
    }
};

struct ModelPaths {
    std::string yolo{};
    std::string backbone{};
    std::string head{};
};

/** @brief 编译期所选平台的生产入口；创建资源不启动检测或采集线程。 */
class PlatformFactory {
public:
    virtual ~PlatformFactory() = default;
    virtual PlatformResult<std::unique_ptr<FrameSource>> createFrameSource(const FrameSourceConfig &config) = 0;
    virtual PlatformResult<std::unique_ptr<FrameBuilder>> createFrameBuilder() = 0;
    virtual PlatformResult<TrackerComponents> createTrackerComponents(const ModelPaths &paths) = 0;
};

/** @brief 由最终程序链接的平台目标提供实现。 */
std::unique_ptr<PlatformFactory> CreatePlatformFactory();

#endif // PLATFORM_PLATFORM_FACTORY_H

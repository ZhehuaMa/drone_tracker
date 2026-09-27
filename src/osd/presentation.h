#ifndef OSD_PRESENTATION_H
#define OSD_PRESENTATION_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "frame/frame.h"
#include "osd/timing.h"

namespace osd {
using SteadyClock = std::chrono::steady_clock;

/** @brief 独立拥有数值及有效接收时刻；没有值表示未知。 */
template <typename T> struct Measurement {
    std::optional<T> value{};
    SteadyClock::time_point received{};
};
struct Attitude {
    double roll_deg{};
    double pitch_deg{};
    double heading_deg{};
};
struct FlightState {
    bool connected{};
    bool armed{};
    std::string mode{};
};
struct Position {
    double latitude_deg{};
    double longitude_deg{};
};
struct Battery {
    std::optional<double> voltage{}, current_amp{}, percentage{}, consumed_mah{}, cell_voltage{};
};
struct GpsStatus {
    std::optional<int> satellites{};
    /** @brief GPSRAW 原始定位等级；0/1 无定位，2 为二维，3 及以上为三维或增强定位。 */
    std::optional<int> fix_type{};
    std::optional<double> speed_mps{}, course_deg{};
};
/** @brief 显示侧独立快照；无 ROS 消息、借用字符串或设备类型。 */
struct TelemetrySnapshot {
    Measurement<Attitude> attitude{};
    Measurement<FlightState> flight{};
    Measurement<Position> position{};
    Measurement<Battery> battery{};
    Measurement<GpsStatus> gps{};
    Measurement<bool> landed{}, failsafe{};
    Measurement<double> pressure_pa{}, temperature_c{};
    Measurement<int> radar_state{}, battery_warning{};
    std::optional<double> armed_seconds{}, home_distance_m{}, home_relative_deg{}, altitude_m{};
    std::string warning{};
};
/** @brief 原图像素坐标中的左上角与尺寸；不包含算法未提供的 ID/置信度。 */
struct DisplayBox {
    double x{}, y{}, width{}, height{};
    std::string label{};
};
/** @brief 一次消费结果的完整所有权包，状态数值与 TrackerState 一致。 */
struct PresentationPacket {
    FrameRef frame{};
    std::uint64_t round{}, sequence{}, frame_time_us{};
    std::int8_t tracker_state{};
    std::vector<DisplayBox> boxes{};
    SteadyClock::time_point accepted{};
};
enum class DisplayStatus { WaitingInput, WaitingFirst, Live, Stale, Stopped, Eof, InputFailure };
/** @brief 不持有源帧的画面元数据，后端自行保留最后实际显示的纯视频底图。 */
struct PresentationState {
    std::uint64_t round{}, sequence{}, frame_time_us{};
    std::int8_t tracker_state{};
    std::vector<DisplayBox> boxes{};
    DisplayStatus status{DisplayStatus::WaitingInput};
    bool has_video{};
    bool new_video{};
    TelemetrySnapshot telemetry{};
    /** @brief 当前轮内、两秒有效的 ON/OFF 显示提示；空串表示没有有效事件。 */
    std::string trigger{};
};
/**
 * @brief 可注入显示后端。除 interrupt 外，所有调用只来自渲染线程且不持有提交锁。
 * prepareVideo 只能在 mapRead 视图存活期间读取源像素，返回前完成复制且不得保存源帧。
 * present 必须在实际交给设备前调用 authorize；被拒绝时不能切换底图。
 * authorize 成功是提交交接点，此后发生停止按硬件在途提交处理。
 * present 返回 true 表示显示已完成；新底图此时才成为冻结底图。
 * 在途硬件使用完成之前不得复用或释放缓冲区；失败路径同样必须安全收尾。
 */
class DisplayBackend {
public:
    virtual ~DisplayBackend() = default;
    /** @brief 借用当前渲染记录，仅渲染线程设置和使用，下一次渲染前清除。 */
    void setTimingSample(TimingSample *sample) noexcept { timing_sample_ = sample; }
    virtual void initialize() = 0;
    virtual void prepareVideo(const Frame &frame) = 0;
    virtual bool present(const PresentationState &state, const std::function<bool()> &authorize) = 0;
    /** @brief 丢弃尚未显示的准备结果，不得修改最后实际显示底图。 */
    virtual void discardPrepared() noexcept = 0;
    /** @brief 唯一允许并发调用的方法，用于唤醒退出等待。 */
    virtual void interrupt() noexcept = 0;
    virtual void shutdown() noexcept = 0;
protected:
    TimingSample *timing_sample_{};
    const std::function<SteadyClock::time_point()> timing_now_{SteadyClock::now};
};
} // namespace osd
#endif // OSD_PRESENTATION_H

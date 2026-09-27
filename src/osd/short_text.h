#ifndef OSD_SHORT_TEXT_H
#define OSD_SHORT_TEXT_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace osd::short_text {
/** @brief 把显示侧的真实测量转换为参考 HUD 的短文字；缺失值统一显示 --。 */
std::string speedKmh(std::optional<double> metres_per_second);
std::string voltage(std::optional<double> volts);
std::string current(std::optional<double> amps);
std::string milliampHours(std::optional<double> mah);
std::string armedTime(std::optional<double> seconds);
std::string localClock(std::chrono::system_clock::time_point time);
std::string frameNumber(std::optional<std::uint64_t> sequence);
} // namespace osd::short_text

#endif // OSD_SHORT_TEXT_H

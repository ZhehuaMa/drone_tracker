#include "osd/short_text.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value) { throw std::runtime_error(message); }
}
} // namespace
int main() {
    try {
        require(osd::short_text::speedKmh(8.3) == "SPD 030KMH", "speed conversion");
        require(osd::short_text::speedKmh({}) == "SPD --KMH", "missing speed");
        require(osd::short_text::voltage(22.2) == "BAT 22.2V", "voltage format");
        require(osd::short_text::voltage({}) == "BAT --V", "missing voltage");
        require(osd::short_text::current(4.3) == "CUR 04.3A", "current format");
        require(osd::short_text::current({}) == "CUR --A", "missing current");
        require(osd::short_text::current(std::numeric_limits<double>::quiet_NaN()) == "CUR --A",
            "non-finite current");
        require(osd::short_text::milliampHours(410) == "MAH 00410", "mAh format");
        require(osd::short_text::milliampHours({}) == "MAH --", "missing mAh");
        require(osd::short_text::armedTime(75) == "T+01:15", "armed timer");
        require(osd::short_text::armedTime({}) == "T+--:--", "missing armed timer");
        require(osd::short_text::frameNumber(17) == "F000017", "frame sequence");
        require(osd::short_text::frameNumber({}) == "F------", "missing frame sequence");
        setenv("TZ", "UTC", 1);
        tzset();
        require(osd::short_text::localClock(std::chrono::system_clock::time_point{
            std::chrono::seconds{1760000000}}) == "08:53:20.0", "local clock");
        std::cout << "OSD short text contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

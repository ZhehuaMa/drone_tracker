#include "osd/short_text.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value) { throw std::runtime_error(message); }
}
struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};
} // namespace
int main() {
    try {
        require(osd::short_text::speedKmh(8.3) == "SPD 030KMH", "speed conversion");
        require(osd::short_text::speedKmh({}) == "SPD --KMH", "missing speed");
        require(osd::short_text::voltage(22.2) == "BAT 22.2V", "voltage format");
        require(osd::short_text::voltage({}) == "BAT --V", "missing voltage");
        require(osd::short_text::current(4.3) == "CUR 04.3A", "current format");
        require(osd::short_text::current(-3.0) == "CUR 03.0A", "negative current magnitude");
        require(osd::short_text::current({}) == "CUR --A", "missing current");
        require(osd::short_text::current(std::numeric_limits<double>::quiet_NaN()) == "CUR --A",
            "non-finite current");
        require(osd::short_text::milliampHours(410) == "MAH 00410", "mAh format");
        require(osd::short_text::milliampHours({}) == "MAH --", "missing mAh");
        require(osd::short_text::armedTime(75) == "T+01:15", "armed timer");
        require(osd::short_text::armedTime({}) == "T+--:--", "missing armed timer");
        require(osd::short_text::frameNumber(17) == "F000017", "frame sequence");
        require(osd::short_text::frameNumber({}) == "F------", "missing frame sequence");
        using osd::short_text::latitude;
        using osd::short_text::longitude;
        require(latitude(31.2304) == "LAT +31.230400", "north latitude");
        require(longitude(121.4737) == "LON +121.473700", "east longitude");
        require(latitude(-31.2304) == "LAT -31.230400", "south latitude");
        require(longitude(-121.4737) == "LON -121.473700", "west longitude");
        require(latitude(1.2345678) == "LAT +1.234568", "six-place rounding");
        require(latitude(0) == "LAT +0.000000" && longitude(0) == "LON +0.000000", "valid zero");
        require(latitude(-0.0000001) == "LAT +0.000000" && longitude(-0.0) == "LON +0.000000",
            "negative zero normalization");
        require(latitude(-90) == "LAT -90.000000" && latitude(90) == "LAT +90.000000", "latitude limits");
        require(longitude(-180) == "LON -180.000000" && longitude(180) == "LON +180.000000",
            "longitude limits");
        for (const auto value : {std::optional<double>{}, std::optional<double>{91}, {-91},
                 {std::numeric_limits<double>::quiet_NaN()}, {std::numeric_limits<double>::infinity()}}) {
            require(latitude(value) == "LAT --", "invalid latitude accepted");
        }
        for (const auto value : {std::optional<double>{}, std::optional<double>{181}, {-181},
                 {std::numeric_limits<double>::quiet_NaN()}, {-std::numeric_limits<double>::infinity()}}) {
            require(longitude(value) == "LON --", "invalid longitude accepted");
        }
        const std::locale previous{std::locale()};
        std::locale::global(std::locale{previous, std::make_unique<CommaDecimal>().release()});
        require(latitude(31.2304) == "LAT +31.230400", "coordinate depends on process locale");
        std::locale::global(previous);
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

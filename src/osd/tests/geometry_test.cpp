#include "osd/geometry.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value) {
    if (!value) {
        throw std::runtime_error("geometry contract failed");
    }
}
}
int main() {
    try {
        const auto geometry{osd::fitVideo(1920, 1200, 720, 480)};
        const auto &r{geometry.destination};
        require(r.x == 0 && r.y == 14 && r.width == 720 && r.height == 450);
        const auto center{geometry.mapBox({960, 600, 1, 1, {}})};
        require(center && center->x == 360 && center->y == 239);
        const auto edge{geometry.mapBox({-100, -200, 3000, 2000, {}})};
        require(edge && edge->x == r.x && edge->y == r.y && edge->width == r.width && edge->height == r.height);
        require(!geometry.mapBox({2000, 0, 10, 10, {}}));
        require(!geometry.mapBox({0, 0, -10, 10, {}}));
        require(!geometry.mapBox({0, std::numeric_limits<double>::quiet_NaN(), 1, 1, {}}));
        const auto portrait{osd::fitVideo(480, 640, 720, 480)};
        require(portrait.destination.x == 180 && portrait.destination.width == 360);
        const auto wide{osd::fitVideo(1920, 1080, 720, 480)};
        require(wide.destination.height == 404 && wide.destination.y == 38);
        bool rejected{false};
        try { osd::fitVideo(0, 1, 720, 480); } catch (const std::invalid_argument &) { rejected = true; }
        require(rejected);
        std::cout << "OSD geometry tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

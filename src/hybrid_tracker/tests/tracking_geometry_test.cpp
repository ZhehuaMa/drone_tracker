#include "tracking_geometry.h"
#include "model_test_support.h"
#include <iostream>

int main() {
    try {
        const auto box{tracking::filterRectangle({100, 50, 40, 20})};
        model_test::require(box == cv::Rect2f{80, 40, 40, 20}, "filter physical center changed");
        model_test::require(tracking::searchRegion(box, 400, 300, 2, {}) == cv::Rect{60, 30, 80, 40},
            "doubled region shifted physical center");
        const auto fractional{tracking::filterRectangle({100.25, 50.75, 40.5, 19.5})};
        model_test::require(fractional == cv::Rect2f{80, 41, 40.5, 19.5}, "fractional coordinates lost");
        model_test::require(tracking::searchRegion({-5, -8, 20, 10}, 100, 80, 2, {})
            == cv::Rect{0, 0, 40, 20}, "boundary clipping");
        model_test::require(tracking::searchRegion(box, 400, 300, 1, {100, 60})
            == cv::Rect{50, 20, 100, 60}, "minimum region moved center");
        model_test::require(tracking::searchRegion(box, 90, 50, 2, {200, 100})
            == cv::Rect{0, 0, 90, 50}, "oversized minimum region");
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}

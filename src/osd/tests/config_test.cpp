#include "osd/config.h"
#include <iostream>
#include <stdexcept>
int main() {
    if (osd::DisplayConfig{}.render_path != osd::RenderPath::CachedOverlay
        || osd::parseRenderPath("cached-overlay") != osd::RenderPath::CachedOverlay
        || osd::parseRenderPath("legacy") != osd::RenderPath::Legacy) { return 1; }
    for (const std::string value : {"", "Legacy", " cached-overlay", "auto"}) {
        try { osd::parseRenderPath(value); return 1; } catch (const std::invalid_argument &) {}
    }
    std::cout << "OSD render path configuration passed\n";
}

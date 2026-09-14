#ifndef PLATFORM_MODEL_PATHS_H
#define PLATFORM_MODEL_PATHS_H

#include "platform/platform_factory.h"

/** @brief 在加载模型之前检查显式角色；不按文件名推断模型。 */
inline PlatformDiagnostic validateModelPaths(const ModelPaths &p, const std::string &platform) {
    const auto fail{[&](const std::string &role, const std::string &reason) {
        return PlatformDiagnostic{platform, role, {}, reason};
    }};
    if (!p.backbone.empty() && (!p.template_backbone.empty() || !p.search_backbone.empty())) {
        return fail("backbone/template_backbone/search_backbone", "Shared and split paths conflict");
    }
    if (p.yolo.empty()) {
        return fail("yolo", "Missing yolo path");
    }
    if (p.head.empty()) {
        return fail("head", "Missing head path");
    }
    if (platform == "jetson") {
        if (!p.template_backbone.empty() || !p.search_backbone.empty()) {
            return fail("template_backbone/search_backbone", "Jetson requires shared backbone, split mode unsupported");
        }
        if (p.backbone.empty()) {
            return fail("backbone", "Missing shared backbone path");
        }
    } else if (platform == "rk3576") {
        if (!p.backbone.empty()) {
            return fail("backbone", "RK3576 requires template_backbone and search_backbone");
        }
        if (p.template_backbone.empty()) {
            return fail("template_backbone", "Missing template_backbone path");
        }
        if (p.search_backbone.empty()) {
            return fail("search_backbone", "Missing search_backbone path");
        }
    } else {
        return fail("platform", "Unknown platform");
    }
    return {};
}
#endif // PLATFORM_MODEL_PATHS_H

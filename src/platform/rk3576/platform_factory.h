#ifndef RK3576_PLATFORM_FACTORY_H
#define RK3576_PLATFORM_FACTORY_H
#include "tracker_assembly.h"
namespace rk3576 {
/** @brief 独立入口；注入依赖供无设备测试，生产绑定由 CreatePlatformFactory 提供。 */
std::unique_ptr<PlatformFactory> createFactory(AssemblyDependencies dependencies);
} // namespace rk3576
#endif // RK3576_PLATFORM_FACTORY_H

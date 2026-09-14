#ifndef RK3576_PRODUCTION_FACTORY_H
#define RK3576_PRODUCTION_FACTORY_H
#include "platform_factory.h"
#include "rknn_api.h"
namespace rk3576 {
/** @brief 复用正式模型工厂；仅替换 SDK 调用表以注入加载失败。 */
AssemblyDependencies productionDependencies(RknnApi api, std::function<void(const std::string &)> describe = {});
} // namespace rk3576
#endif // RK3576_PRODUCTION_FACTORY_H

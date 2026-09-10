#include "platform/platform_factory.h"
#include "platform/frame_source_ops.h"

#include <type_traits>

// 仅继承公共接口目标的头文件路径，不借用算法或平台实现的传递依赖。
static_assert(std::is_abstract_v<PlatformFactory>);
static_assert(std::is_abstract_v<FrameSource>);
static_assert(std::is_abstract_v<FrameBuilder>);
static_assert(!std::is_copy_constructible_v<TrackerComponents>);

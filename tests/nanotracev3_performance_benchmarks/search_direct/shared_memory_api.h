#ifndef NANOTRACK_SEARCH_DIRECT_SHARED_MEMORY_API_H
#define NANOTRACK_SEARCH_DIRECT_SHARED_MEMORY_API_H

#include "platform/rk3576/rknn_api.h"

namespace search_direct {
/**
 * @brief 仅本测试程序使用的 RKNN 设备内存调用边界。
 *
 * core 是生产平台已有的常规调用集合（加载、查询、普通提交、运行、取回输出、销毁）。
 * 其余成员是共享设备内存所需的调用：分配一块设备内存、按文件描述符导入同一块内存、
 * 指定某个模型输入或输出使用哪块内存、按缓存策略同步，以及释放内存句柄。
 * 生产的 `rk3576::RknnApi` 不因本程序增加成员，生产后端也不会用到这些调用。
 */
struct SharedMemoryApi {
    rk3576::RknnApi core{};
    decltype(&rknn_create_mem) create_mem{};
    decltype(&rknn_create_mem_from_fd) create_mem_from_fd{};
    decltype(&rknn_destroy_mem) destroy_mem{};
    decltype(&rknn_set_io_mem) set_io_mem{};
    decltype(&rknn_mem_sync) mem_sync{};
    /** @brief 是否所有调用都已提供；缺少任意一项都不允许进入设备执行。 */
    bool complete() const noexcept {
        return core.init && core.query && core.inputs_set && core.run && core.wait && core.outputs_get &&
               core.outputs_release && core.destroy && create_mem && create_mem_from_fd && destroy_mem && set_io_mem &&
               mem_sync;
    }
};
/** @brief 返回链接到真实 librknnrt 的调用集合。 */
SharedMemoryApi nativeSharedMemoryApi();
} // namespace search_direct
#endif // NANOTRACK_SEARCH_DIRECT_SHARED_MEMORY_API_H

#ifndef RK3576_RKNN_API_H
#define RK3576_RKNN_API_H

#include <rknn_api.h>

namespace rk3576 {
/** @brief 仅平台内部使用的可注入 C API 边界；测试替换调用，不模拟公共检测器。 */
struct RknnApi {
    decltype(&rknn_init) init{};
    decltype(&rknn_query) query{};
    decltype(&rknn_inputs_set) inputs_set{};
    decltype(&rknn_run) run{};
    decltype(&rknn_wait) wait{};
    decltype(&rknn_outputs_get) outputs_get{};
    decltype(&rknn_outputs_release) outputs_release{};
    decltype(&rknn_destroy) destroy{};
};
struct RknnSharedMemoryApi {
    decltype(&rknn_create_mem) create_mem{};
    decltype(&rknn_create_mem_from_fd) create_mem_from_fd{};
    decltype(&rknn_destroy_mem) destroy_mem{};
    decltype(&rknn_set_io_mem) set_io_mem{};
    decltype(&rknn_mem_sync) mem_sync{};
    bool complete() const noexcept {
        return create_mem && create_mem_from_fd && destroy_mem && set_io_mem && mem_sync;
    }
};
RknnApi nativeRknnApi();
RknnSharedMemoryApi nativeRknnSharedMemoryApi();
} // namespace rk3576
#endif // RK3576_RKNN_API_H

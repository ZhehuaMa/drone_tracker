#include "rknn_api.h"

namespace rk3576 {
RknnApi nativeRknnApi() {
    return {rknn_init, rknn_query,       rknn_inputs_set,      rknn_run,
            rknn_wait, rknn_outputs_get, rknn_outputs_release, rknn_destroy};
}
RknnSharedMemoryApi nativeRknnSharedMemoryApi() {
    return {rknn_create_mem, rknn_create_mem_from_fd, rknn_destroy_mem, rknn_set_io_mem, rknn_mem_sync};
}
} // namespace rk3576

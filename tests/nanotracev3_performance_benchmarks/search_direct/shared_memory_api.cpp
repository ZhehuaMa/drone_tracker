#include "shared_memory_api.h"

namespace search_direct {
SharedMemoryApi nativeSharedMemoryApi() {
    SharedMemoryApi api;
    api.core = rk3576::nativeRknnApi();
    api.create_mem = rknn_create_mem;
    api.create_mem_from_fd = rknn_create_mem_from_fd;
    api.destroy_mem = rknn_destroy_mem;
    api.set_io_mem = rknn_set_io_mem;
    api.mem_sync = rknn_mem_sync;
    return api;
}
} // namespace search_direct

#include "rknn_api.h"

namespace rk3576 {
RknnApi nativeRknnApi() {
    return {rknn_init, rknn_query,       rknn_inputs_set,      rknn_run,
            rknn_wait, rknn_outputs_get, rknn_outputs_release, rknn_destroy};
}
} // namespace rk3576

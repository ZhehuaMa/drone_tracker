#include "rknn_yolo_contract.h"

#include "model/yolo_output_adapter.h"

namespace rk3576 {
model::Result<int> validateYoloSession(const RknnSession &session) {
    using Result = model::Result<int>;
    if (session.inputs().size() != 1 || session.outputs().size() != 6) {
        return Result::failure({model::ModelError::ContractMismatch, session.identity(), "input/output count", "1/6",
                                session.describe(), "Unsupported YOLO structure"});
    }
    const auto &a{session.inputs().front()};
    if (a.n_dims != 4 || a.dims[0] != 1 || a.dims[1] != 640 || a.dims[2] != 640 || a.dims[3] != 3 ||
        a.type != RKNN_TENSOR_INT8 || a.fmt != RKNN_TENSOR_NHWC || a.zp != -128 || a.scale != 1.0f / 255.0f) {
        return Result::failure({model::ModelError::ContractMismatch, session.identity(), "input#0",
                                "INT8 NHWC [1,640,640,3], zp=-128 scale=1/255; mean=0/std=255", session.describe(),
                                "Unsupported YOLO input"});
    }
    for (const auto &output : session.outputs()) {
        if (output.fmt != RKNN_TENSOR_NCHW) {
            return Result::failure({model::ModelError::ContractMismatch, session.identity(),
                                    "output#" + std::to_string(output.index), "NCHW logical output", session.describe(),
                                    "Unsupported YOLO output"});
        }
    }
    return model::yoloSixOutputClasses(session.outputDescriptors(), session.identity());
}
} // namespace rk3576

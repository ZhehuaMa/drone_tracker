#ifndef DRONE_DIAGNOSTICS_PUBLICATION_H
#define DRONE_DIAGNOSTICS_PUBLICATION_H
#include "recorder.h"
#include <utility>

namespace diagnostics {
/** @brief 同一发布边界由 ROS 节点和故障测试共用；正常返回不证明接收端收到。 */
template <typename Publish> void observePublication(Event value, Publish &&publish) {
    value.type = Type::PublishStart;
    emit(value);
    const auto start{monotonicNs()};
    try {
        std::forward<Publish>(publish)();
        value.flags |= Complete;
    } catch (...) {
        value.type = Type::PublishEnd;
        value.reason = Reason::PublishException;
        value.flags |= Critical;
        value.duration_ns = monotonicNs() - start;
        emit(value);
        throw;
    }
    value.type = Type::PublishEnd;
    value.duration_ns = monotonicNs() - start;
    emit(value);
}
} // namespace diagnostics
#endif // DRONE_DIAGNOSTICS_PUBLICATION_H

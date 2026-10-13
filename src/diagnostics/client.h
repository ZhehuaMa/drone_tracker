#ifndef DRONE_DIAGNOSTICS_CLIENT_H
#define DRONE_DIAGNOSTICS_CLIENT_H
#include "recorder.h"
#include <memory>
#include <string>

namespace diagnostics {
/** @brief 启动控制路径拥有共享区；热路径不连接、不分配、不等待存储。 */
class Client final {
public:
    Client() = default;
    ~Client() noexcept;
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    bool prepare(Mode mode, const std::string &endpoint, std::uint64_t run_id = 0, const std::string &identity = "{}");
    bool reconnect() noexcept;
    bool finish(std::chrono::milliseconds budget = std::chrono::milliseconds{2000}) noexcept;
    SharedRegion *region() noexcept { return region_; }

private:
    bool connectConsumer() noexcept;
    int memory_{-1}, notification_{-1}, connection_{-1}, consumer_pidfd_{-1};
    SharedRegion *region_{};
    std::unique_ptr<Recorder> recorder_{};
    std::string endpoint_{}, identity_{};
    std::uint64_t last_connect_ns_{};
};
} // namespace diagnostics
#endif // DRONE_DIAGNOSTICS_CLIENT_H

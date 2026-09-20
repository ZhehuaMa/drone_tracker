#ifndef DRONE_TRACKER_TIMING_OBSERVER_H
#define DRONE_TRACKER_TIMING_OBSERVER_H

#include <cstdint>
#include <exception>

namespace timing {
enum class Stage : std::uint8_t {
    DecodeToResult,
    Decode,
    FrameCopy,
    Preprocess,
    Inference,
    Postprocess,
    Geometry,
    ImagePrepare,
    TemplateBackbone,
    SearchBackbone,
    Head,
    InputPrepare,
    InputsSet,
    Run,
    OutputsGet,
    OutputCopy,
    OutputsRelease,
    Normalize
};
/** @brief 同一线程的借用观察器；回调不得抛异常、分配内存或执行文件 I/O。 */
class Observer {
  public:
    virtual ~Observer() = default;
    virtual std::uint32_t begin(Stage stage) noexcept = 0;
    virtual void end(std::uint32_t id, bool complete) noexcept = 0;
};
inline thread_local Observer *current{nullptr};
/** @brief 只在当前线程启用观察，作用域结束后恢复原观察器。 */
class Observation final {
  public:
    explicit Observation(Observer *observer) noexcept : previous_{current} { current = observer; }
    ~Observation() noexcept { current = previous_; }
    Observation(const Observation &) = delete;
    Observation &operator=(const Observation &) = delete;

  private:
    Observer *previous_{};
};
/** @brief 轻量计时区间；显式 finish(false) 可标记普通返回值表示的失败。 */
class Span final {
  public:
    explicit Span(Stage stage) noexcept : observer_{current}, exceptions_{std::uncaught_exceptions()} {
        if (observer_) {
            id_ = observer_->begin(stage);
        }
    }
    ~Span() noexcept { finish(std::uncaught_exceptions() == exceptions_); }
    void finish(bool complete = true) noexcept {
        if (observer_) {
            observer_->end(id_, complete);
            observer_ = nullptr;
        }
    }
    Span(const Span &) = delete;
    Span &operator=(const Span &) = delete;

  private:
    Observer *observer_{};
    std::uint32_t id_{};
    int exceptions_{};
};
} // namespace timing
#endif // DRONE_TRACKER_TIMING_OBSERVER_H

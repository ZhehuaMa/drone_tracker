#include "timing_recorder.h"
#include <chrono>
#include <stdexcept>
namespace baseline {
namespace {
const std::int64_t process_origin{TimingRecorder::steadyNow()};
} // namespace
std::int64_t TimingRecorder::steadyNow() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
TimingRecorder::TimingRecorder(bool detailed, Clock clock) noexcept
    : detailed_{detailed}, clock_{clock}, origin_{clock == steadyNow ? process_origin : clock()} {}
std::uint32_t TimingRecorder::begin(Stage stage) noexcept {
    if (stage == Stage::Decode && size_ == 0) {
        spans_[0] = {1, 0, Stage::DecodeToResult, now(), 0, false};
        size_ = 1;
        top_ = 1;
    }
    if (!detailed_) {
        return 0;
    }
    if (!top_ || size_ == spans_.size()) {
        error_ = true;
        return 0;
    }
    const auto id{static_cast<std::uint32_t>(size_ + 1)};
    spans_[size_++] = {id, top_, stage, now(), 0, false};
    top_ = id;
    return id;
}
void TimingRecorder::end(std::uint32_t id, bool complete) noexcept {
    if (!id) {
        return;
    }
    if (id != top_ || id > size_) {
        error_ = true;
        return;
    }
    auto &span{spans_[id - 1]};
    span.end = now();
    span.complete = complete;
    if (span.end < span.start) {
        error_ = true;
    }
    top_ = span.parent;
}
void TimingRecorder::finish(bool complete) noexcept {
    if (!active()) {
        return;
    }
    if (top_ != 1) {
        error_ = true;
    }
    while (top_ > 1) {
        end(top_, false);
    }
    end(1, complete && !error_);
}
void TimingRecorder::clear() noexcept {
    size_ = 0;
    top_ = 0;
    error_ = false;
}
std::optional<std::int64_t> TimingRecorder::duration(Stage stage) const {
    std::optional<std::int64_t> value;
    for (std::size_t i{0}; i < size_; ++i) {
        const auto &span{spans_[i]};
        if (span.stage == stage) {
            value = value.value_or(0) + span.end - span.start;
        }
    }
    return value;
}
const char *stageName(Stage stage) {
    constexpr std::array<const char *, 18> names{"decode_to_result",
                                                 "decode",
                                                 "frame_copy",
                                                 "preprocess",
                                                 "inference",
                                                 "postprocess",
                                                 "geometry",
                                                 "image_prepare",
                                                 "template_backbone",
                                                 "search_backbone",
                                                 "head",
                                                 "input_prepare",
                                                 "inputs_set",
                                                 "run",
                                                 "outputs_get",
                                                 "output_copy",
                                                 "outputs_release",
                                                 "normalize"};
    return names.at(static_cast<std::size_t>(stage));
}
const char *modelRole(const TimingRecorder &recorder, const SpanRecord &span) {
    auto id{span.id};
    while (id) {
        const auto &parent{recorder.spans()[id - 1]};
        if (parent.stage == Stage::TemplateBackbone || parent.stage == Stage::SearchBackbone ||
            parent.stage == Stage::Head) {
            return stageName(parent.stage);
        }
        id = parent.parent;
    }
    return "";
}
} // namespace baseline

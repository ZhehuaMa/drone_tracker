#include "replay_runner.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace replay {
int runReplay(const ReplayConfig &config, ReplayReport &report, const ReplayDependencies &d) {
    std::unique_ptr<ReplayReader> reader{};
    std::shared_ptr<HybridTracker> tracker{};
    const auto start{std::chrono::steady_clock::now()};
    std::string reason{"initialization_error"};
    int code{1};
    try {
        reader = std::make_unique<ReplayReader>(d.video(config.video.string()), config.fps, config.max_frames);
        report.input(reader->metadata());
        auto factory{d.factory()};
        if (!factory) {
            throw std::runtime_error("Platform factory returned null");
        }
        auto builder{factory->createFrameBuilder()};
        if (!builder) {
            throw std::runtime_error(builder.error.describe());
        }
        auto components{factory->createTrackerComponents(config.models)};
        if (!components) {
            throw std::runtime_error(components.error.describe());
        }
        report.models(components.value.description);
        tracker = HybridTracker::CreateInstance(config.tracker, std::move(components.value), d.launch);
        if (!tracker || !tracker->init()) {
            throw std::runtime_error("HybridTracker initialization failed");
        }
        ReplayClock clock{config.fps, d.clock};
        std::uint64_t index{};
        std::int64_t previous_submit{};
        DecodedFrame decoded{};
        reason = "runtime_error";
        while (true) {
            if (d.interrupted()) {
                reason = "interrupted";
                break;
            }
            if (!reader->next(decoded)) {
                reason = reader->endReason();
                break;
            }
            FrameRecord record{};
            record.index = index;
            record.timestamp_us = frameTimestamp(index, config.fps);
            record.decode_ms = decoded.decode_ms;
            const auto convert_begin{clock.now()};
            const auto gray{grayImage(decoded.image)};
            auto frame{builder.value->copyFrom(gray, static_cast<std::uint64_t>(record.timestamp_us))};
            if (!frame || !frame.value) {
                throw std::runtime_error(frame.error.describe());
            }
            record.convert_copy_ms = static_cast<double>(clock.now() - convert_begin) / 1e6;
            const auto wait_begin{clock.now()};
            const auto submitted{clock.wait(d.interrupted)};
            if (d.interrupted()) {
                reason = "interrupted";
                break;
            }
            record.wait_ms = static_cast<double>(submitted - wait_begin) / 1e6;
            record.submit_ns = submitted - clock.origin();
            record.interval_ms = index == 0 ? 0 : static_cast<double>(submitted - previous_submit) / 1e6;
            record.lag_ms = std::max(0.0, static_cast<double>(record.submit_ns) / 1e6 -
                                              static_cast<double>(record.timestamp_us - 1000000) / 1000.0);
            previous_submit = submitted;
            FrameContext context{};
            context.frame = std::move(frame.value);
            context.now_us = record.timestamp_us;
            record.output = tracker->update(context);
            const auto update_end{clock.now()};
            record.update_ms = static_cast<double>(update_end - submitted) / 1e6;
            clock.finished();
            record.nano = tracker->lastNanoBox();
            record.nano_score = tracker->lastNanoScore();
            report.frame(record);
            d.check_diagnostics();
            ++index;
        }
        code = reason == "eof" || reason == "frame_limit" ? 0 : reason == "interrupted" ? 130 : 1;
    } catch (const std::exception &error) {
        if (reader && reader->endReason() == "decode_error") {
            reason = "decode_error";
        }
        try {
            report.diagnostic(error.what());
        } catch (const std::exception &) {
        }
    }
    // 析构会 join 检测线程；不能在末尾补交虚拟帧，也不能提前销毁模型或帧。
    tracker.reset();
    try {
        d.check_diagnostics();
        const double elapsed{
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()};
        report.finish(reason, reader ? reader->readCount() : 0, elapsed);
    } catch (const std::exception &) {
        return 1;
    }
    return code;
}
} // namespace replay

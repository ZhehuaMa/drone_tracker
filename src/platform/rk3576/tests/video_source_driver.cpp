#include "../platform_factory.h"
#include "../video_frame_source.h"
#include "platform/frame_source_ops.h"

#include <iostream>
#include <stdexcept>
#include <thread>

/** @brief 无模型视频验收驱动：文件、历史模式(0/1)、消费延时毫秒、轮数。 */
int main(int argc, char **argv) {
    try {
        if (argc != 5) {
            throw std::runtime_error("Usage: video_source_driver FILE HISTORY(0/1) CONSUMER_MS ROUNDS");
        }
        FrameSourceConfig config{};
        config.type = FrameSourceType::Video;
        config.path = argv[1];
        config.use_history_timestamp = std::stoi(argv[2]) != 0;
        const int delay{std::stoi(argv[3])};
        const int rounds{std::stoi(argv[4])};
        if (delay < 0 || rounds <= 0) {
            throw std::runtime_error("Invalid consumer delay or rounds");
        }
        auto factory{rk3576::createFactory({})};
        auto created{factory->createFrameSource(config)};
        if (!created) {
            throw std::runtime_error(created.error.describe());
        }
        auto &source{*created.value};
        auto &video{dynamic_cast<rk3576::Rk3576VideoFrameSource &>(source)};
        for (int round{1}; round <= rounds; ++round) {
            if (!source.start()) {
                throw std::runtime_error(video.diagnostic().reason);
            }
            std::uint64_t consumed{};
            while (true) {
                FrameRef frame{};
                if (!waitForLatestFrame(source, frame, std::chrono::milliseconds{20})) {
                    if (!source.isRunning()) {
                        break;
                    }
                    continue;
                }
                auto view{frame->mapRead()};
                std::uint64_t hash{14695981039346656037ULL};
                for (int row{0}; row < view->height(); ++row) {
                    for (int col{0}; col < view->width(); ++col) {
                        hash = (hash ^ view->mat().at<unsigned char>(row, col)) * 1099511628211ULL;
                    }
                }
                std::cout << "round=" << round << " consumed=" << ++consumed << " timestamp_us="
                          << frame->timestampUs() << " pixel_fnv1a=" << hash << '\n';
                std::this_thread::sleep_for(std::chrono::milliseconds{delay});
            }
            source.stop();
            const auto stats{video.diagnostic()};
            std::cout << "round=" << round << " state=" << static_cast<int>(stats.state)
                      << " read=" << stats.read << " enqueued=" << stats.enqueued
                      << " dropped=" << stats.dropped << " consumed=" << consumed << " reason=" << stats.reason << '\n';
            if (stats.state != rk3576::VideoState::Eof || consumed == 0) {
                throw std::runtime_error("Video round did not finish successfully");
            }
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

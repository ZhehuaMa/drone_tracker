#include "platform/rk3576/mjpeg_video_reader.h"
#include "statistics.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
namespace {
std::int64_t time_ns{};
std::int64_t controlledClock() noexcept { return time_ns; }
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace
int main(int argc, char **argv) {
    using namespace baseline;
    try {
        TimingRecorder timer{true, controlledClock};
        {
            timing::Observation observation{&timer};
            {
                timing::Span decode{Stage::Decode};
                time_ns += 7;
            }
            {
                timing::Span infer{Stage::Inference};
                {
                    timing::Span model{Stage::SearchBackbone};
                    {
                        timing::Span run{Stage::Run};
                        time_ns += 2;
                    }
                    {
                        timing::Span get{Stage::OutputsGet};
                        time_ns += 11;
                    }
                }
            }
            time_ns += 3;
            timer.finish(true);
        }
        require(timer.valid() && timer.duration(Stage::DecodeToResult) == 23, "完整计时");
        require(timer.duration(Stage::Inference) == 13 && timer.duration(Stage::OutputsGet) == 11, "等待归属");
        require(!timer.duration(Stage::Postprocess), "未执行阶段为空");
        require(timer.spans()[4].parent == 4, "SDK 父区间");
        const auto d{distribution({20000000, 30000000})};
        require(d.mean == 25000000 && d.p50 == 20000000 && d.p99 == 30000000, "分位数");
        require(d.standard_deviation == 5000000, "总体标准差");
        require(distribution({}).count == 0, "空样本");
        if (argc == 2) {
            std::atomic<bool> stop{false};
            const auto path{std::filesystem::path(argv[1]) / "420.mov"};
            rk3576::MjpegVideoReader plain{path.string(), stop};
            timer.clear();
            timing::Observation observation{&timer};
            rk3576::MjpegVideoReader observed{path.string(), stop};
            require(timer.active() && timer.duration(Stage::Decode).has_value(), "构造首帧未记录实际解码");
            for (int i{0}; i < 3; ++i) {
                if (i) {
                    timer.clear();
                }
                auto actual{observed.next()};
                timer.finish(true);
                rk3576::VideoReadResult expected;
                {
                    timing::Observation disabled{nullptr};
                    expected = plain.next();
                }
                require(cv::countNonZero(actual.sample.gray != expected.sample.gray) == 0, "观测改变生产亮度");
                require(timer.valid() && timer.duration(Stage::Decode).has_value(), "缓存首帧或后续解码未关联");
            }
            timer.clear();
            rk3576::MjpegVideoReader broken{(std::filesystem::path(argv[1]) / "bad.mov").string(), stop};
            broken.next();
            timer.finish(true);
            timer.clear();
            require(broken.next().status == rk3576::VideoReadStatus::DecodeError, "坏帧未失败");
            timer.finish(false);
            require(!timer.spans()[0].complete && !timer.spans()[1].complete, "坏帧区间未保留失败");
        }
        Config config;
        config.output =
            std::filesystem::temp_directory_path() / ("nanotrack-baseline-test-" + std::to_string(getpid()));
        config.board = "controlled";
        ResultWriter writer{config};
        for (int i{0}; i < 2; ++i) {
            timer.clear();
            timing::Observation observation{&timer};
            {
                timing::Span decode{Stage::Decode};
                time_ns += (i + 2) * 10000000;
            }
            timer.finish(true);
            FrameRecord frame;
            frame.repeat = 1;
            frame.index = i + 1;
            frame.status = "success";
            frame.score = 0.01;
            frame.pts = 9007199254740993LL;
            frame.pts_valid = true;
            frame.error = "逗号,引号\"换行\n内容";
            writer.writeFrame(frame, timer);
        }
        require(writeReports(config.output, "complete", "eof") == 2, "离线有效帧计数");
        const auto summary{readText(config.output / "summary.json")};
        require(summary.find("\"decode_to_result_processing_fps\":40") != std::string::npos, "两帧处理速率");
        std::ifstream csv(config.output / "frames.csv");
        std::vector<std::string> row;
        require(readCsvRecord(csv, row) && row.size() == 24, "表头");
        require(readCsvRecord(csv, row) && row.at(6) == "9007199254740993", "64 位整数保真");
        require(row.at(22) == "逗号,引号\"换行\n内容", "CSV 转义");
        bool rejected{false};
        try {
            ResultWriter duplicate{config};
        } catch (...) {
            rejected = true;
        }
        require(rejected, "输出不覆盖");
        std::filesystem::remove_all(config.output);
        std::cout << "baseline controlled tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

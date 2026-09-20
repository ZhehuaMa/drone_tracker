#include "benchmark_runner.h"
#include "code_identity.h"
#include "model/validation.h"
#include "model_preparation.h"
#include "nano_tracker.h"
#include "platform/rk3576/host_frame.h"
#include "platform/rk3576/mjpeg_video_reader.h"
#include "platform/rk3576/rknn_nanotrack_factory.h"
#include "statistics.h"
#include <atomic>
#include <cmath>
#include <iostream>
#include <jpeglib.h>
#include <libavcodec/version.h>
#include <libavformat/version.h>
#include <memory>
#include <stdexcept>
namespace baseline {
volatile std::sig_atomic_t interrupted{0};
namespace {
void saveText(const std::filesystem::path &path, const std::string &text) {
    std::ofstream output;
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.open(path, std::ios::binary);
    output << text;
    output.close();
}
bool finiteBox(const cv::Rect2f &box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) && std::isfinite(box.height) &&
           box.width > 0 && box.height > 0;
}
} // namespace
int runBenchmark(const Config &config, const ModelLoader &loader) {
    std::unique_ptr<ResultWriter> writer;
    std::string status{"failed"}, reason{"preparation_error"};
    int exit_code{1};
    TimingRecorder recorder{config.detailed};
    std::size_t valid{};
    try {
        writer = std::make_unique<ResultWriter>(config);
        writer->identity["code_commit"] = BASELINE_COMMIT;
        writer->identity["build_flags"] = BASELINE_BUILD_FLAGS;
        writer->identity["compiler"] = __VERSION__;
        writer->identity["opencv"] = CV_VERSION;
        writer->identity["ffmpeg_avcodec"] = AV_STRINGIFY(LIBAVCODEC_VERSION);
        writer->identity["ffmpeg_avformat"] = AV_STRINGIFY(LIBAVFORMAT_VERSION);
        writer->identity["jpeg_version"] = std::to_string(JPEG_LIB_VERSION);
        saveText(config.output / "code_snapshot.txt", BASELINE_SOURCE_SNAPSHOT);
        writer->identity["code_snapshot_sha256"] = sha256(config.output / "code_snapshot.txt");
        writer->identity["executable_sha256"] = sha256("/proc/self/exe");
        writer->identity["video_sha256"] = sha256(config.video);
        {
            std::ifstream mappings("/proc/self/maps");
            std::string line;
            while (std::getline(mappings, line)) {
                const auto offset{line.find('/')};
                if (offset != std::string::npos && line.find("librknnrt.so", offset) != std::string::npos) {
                    const auto library{line.substr(offset)};
                    writer->identity["rknn_library_path"] = library;
                    writer->identity["rknn_library_sha256"] = sha256(library);
                    break;
                }
            }
            if (!writer->identity.count("rknn_library_path")) {
                writer->identity["rknn_library_missing_reason"] = "进程映射中没有可识别的 librknnrt.so 路径";
            }
        }
        for (std::size_t i{0}; i < config.models.size(); ++i) {
            writer->identity["model_" + std::to_string(i) + "_sha256"] = sha256(config.models[i]);
        }
        saveText(config.output / "config.yaml", readText(config.source));
        reason = "model_error";
        auto components{loader ? loader(config)
                               : rk3576::loadNanoTrack(config.models[0].string(), config.models[1].string(),
                                                       config.models[2].string())};
        if (!components) {
            throw std::runtime_error(model::describe(components.error));
        }
        writer->identity["model_attributes_runtime_driver"] = components.value.description;
        writer->diagnostic(components.value.description);
        reason = "precision_unverified";
        verifyPrecision(config, *writer, components.value.floating_interfaces);
        reason = "runtime_version_mismatch";
        if (components.value.description.find("runtime=2.3.2") == std::string::npos ||
            components.value.description.find("driver=0.9.8") == std::string::npos) {
            throw std::runtime_error("实机版本必须为 Runtime 2.3.2 / 驱动 0.9.8");
        }
        NanoTracker tracker{std::move(components.value.backend), std::move(components.value.preprocessor)};
        writer->manifest("running", "measuring");
        std::atomic<bool> stop{false};
        for (int repeat{0}; repeat <= config.repeats; ++repeat) {
            tracker.reset();
            std::unique_ptr<rk3576::MjpegVideoReader> reader;
            const bool warmup{repeat == 0};
            const int limit{warmup ? config.warmup_updates : config.max_updates};
            int updates{};
            for (int index{0};; ++index) {
                if (interrupted) {
                    status = "interrupted";
                    reason = "signal";
                    exit_code = 130;
                    break;
                }
                recorder.clear();
                writer->environment(recorder.now());
                FrameRecord frame;
                frame.repeat = repeat;
                frame.index = index;
                frame.warmup = warmup;
                frame.init = index == 0;
                reason = "read_error";
                rk3576::VideoReadResult decoded;
                bool eof{false};
                {
                    timing::Observation observe{&recorder};
                    try {
                        if (!reader) {
                            reader = std::make_unique<rk3576::MjpegVideoReader>(config.video.string(), stop);
                        }
                        decoded = reader->next();
                        frame.pts = decoded.sample.pts;
                        frame.pts_valid = decoded.sample.has_pts;
                        if (decoded.status == rk3576::VideoReadStatus::Eof) {
                            eof = true;
                        } else if (decoded.status != rk3576::VideoReadStatus::Frame) {
                            reason =
                                decoded.status == rk3576::VideoReadStatus::ReadError ? "read_error" : "decode_error";
                            throw std::runtime_error(decoded.reason);
                        } else {
                            reason = "frame_copy_error";
                            FrameRef input;
                            {
                                timing::Span copy{Stage::FrameCopy};
                                input = rk3576::copyGrayFrame(decoded.sample.gray, 0);
                            }
                            reason = "model_error";
                            if (frame.init) {
                                tracker.init(input, config.bbox);
                                recorder.finish(true);
                                if (!tracker.isInitialized()) {
                                    reason = "model_error";
                                    throw std::runtime_error("模板初始化失败");
                                }
                                frame.bbox = config.bbox;
                                frame.bbox_valid = true;
                            } else {
                                const float score{tracker.update(input, frame.bbox)};
                                recorder.finish(true);
                                if (score < 0 || !std::isfinite(score) || !finiteBox(frame.bbox)) {
                                    reason = "model_error";
                                    throw std::runtime_error("跟踪返回失败或非有限值");
                                }
                                frame.score = score;
                                frame.bbox_valid = true;
                            }
                            frame.status = "success";
                        }
                    } catch (const std::exception &error) {
                        if (recorder.active() && recorder.spans()[0].end == 0) {
                            recorder.finish(false);
                        }
                        recorder.markFailed();
                        frame.error = error.what();
                        if (reason == "read_error") {
                            reason = recorder.active() ? "decode_error" : "read_error";
                        }
                    }
                }
                if (eof) {
                    if (warmup && updates < limit) {
                        reason = "warmup_insufficient";
                        throw std::runtime_error("录像不足预热次数");
                    }
                    reason = "eof";
                    break;
                }
                writer->writeFrame(frame, recorder);
                if (frame.status != "success") {
                    throw std::runtime_error(frame.error);
                }
                if (frame.init) {
                    const auto base{reader->timeBase()};
                    writer->identity["pts_time_base"] = std::to_string(base.first) + "/" + std::to_string(base.second);
                } else {
                    ++updates;
                    if (!warmup) {
                        ++valid;
                    }
                }
                if ((warmup || limit > 0) && updates >= limit) {
                    reason = "frame_limit";
                    break;
                }
            }
            if (status == "interrupted") {
                break;
            }
        }
        if (status != "interrupted") {
            if (!valid) {
                reason = "no_valid_updates";
                throw std::runtime_error("没有有效正式更新");
            }
            status = "complete";
            exit_code = 0;
        }
    } catch (const std::ios_base::failure &error) {
        status = "failed";
        reason = "output_error";
        exit_code = 1;
        std::cerr << reason << ": " << error.what() << '\n';
    } catch (const std::exception &error) {
        status = "failed";
        exit_code = 1;
        std::cerr << reason << ": " << error.what() << '\n';
        if (writer) {
            try {
                writer->diagnostic(error.what());
            } catch (...) {
                std::cerr << "诊断输出也失败\n";
            }
        }
    }
    if (writer) {
        try {
            writer->environment(recorder.now(), true);
            const auto counted{writeReports(config.output, status, reason)};
            if (status == "complete" && counted != valid) {
                throw std::runtime_error("落盘有效帧数不一致");
            }
            writer->manifest(status, reason);
        } catch (const std::exception &error) {
            std::cerr << "output_error: " << error.what() << '\n';
            try {
                writer->manifest("failed", "output_error");
            } catch (...) {
            }
            return 1;
        }
    }
    return exit_code;
}
} // namespace baseline

#include "ar0234_camera_driver_support.h"
#include <linux/videodev2.h>

#include <unistd.h>
#include <csignal>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace rk3576::ar0234_camera_driver {
namespace fs = std::filesystem;
using namespace rk3576;
volatile std::sig_atomic_t interrupted{};
void interrupt(int) { interrupted = 1; }
int number(const std::string &text, bool zero = false) {
    std::size_t end{};
    const auto value{std::stoll(text, &end)};
    if (end != text.size() || value < (zero ? 0 : 1) || value > 1000000000) {
        throw std::invalid_argument("Invalid numeric argument: " + text);
    }
    return static_cast<int>(value);
}
Options parse(int argc, char **argv) {
    Options options{};
    options.config.trigger_mode = false;
    std::set<std::string> supplied{};
    for (int index{1}; index < argc; ++index) {
        const std::string key{argv[index]};
        if (!supplied.insert(key).second) { throw std::invalid_argument("Duplicate argument: " + key); }
        if (key == "--compare-pixels") { options.compare = true; continue; }
        if (++index >= argc) { throw std::invalid_argument("Missing value: " + key); }
        const std::string value{argv[index]};
        if (key == "--video-device") { options.config.path = value; }
        else if (key == "--media-device") { options.config.camera_media_device = value; }
        else if (key == "--sensor-entity") { options.config.camera_sensor_entity = value; }
        else if (key == "--width") { options.config.width = number(value); }
        else if (key == "--height") { options.config.height = number(value); }
        else if (key == "--fps") { options.config.fps = number(value); }
        else if (key == "--mode") {
            if (value != "continuous" && value != "software_trigger") {
                throw std::invalid_argument("--mode must be continuous or software_trigger");
            }
            options.config.trigger_mode = value == "software_trigger";
        }
        else if (key == "--trigger-hz") {
            std::size_t end{};
            options.config.camera_trigger_rate_hz = std::stod(value, &end);
            if (end != value.size()) { throw std::invalid_argument("Invalid --trigger-hz"); }
        }
        else if (key == "--commands") { options.finite_commands = true; options.commands = number(value, true); }
        else if (key == "--tail-ms") { options.tail_ms = number(value, true); }
        else if (key == "--buffers") { options.config.camera_buffer_count = number(value); }
        else if (key == "--frames") { options.frames = number(value); }
        else if (key == "--seconds") { options.seconds = number(value); }
        else if (key == "--rounds") { options.rounds = number(value); }
        else if (key == "--instances") { options.instances = number(value); }
        else if (key == "--slow-ms") { options.slow_ms = number(value, true); }
        else if (key == "--snapshots") { options.snapshots = number(value, true); }
        else if (key == "--output") { options.output = value; }
        else { throw std::invalid_argument("Unknown argument: " + key); }
    }
    validateCameraConfig(options.config);
    if ((options.finite_commands && !options.config.trigger_mode) || options.tail_ms > 1500) {
        throw std::invalid_argument("--commands requires software mode; --tail-ms must be 0..1500");
    }
    if (options.output.empty() || (options.frames == 0 && options.seconds == 0 && !options.finite_commands)) {
        throw std::invalid_argument("--output and --frames, --seconds or --commands are required");
    }
    return options;
}
void checked(std::ostream &stream) {
    stream.flush();
    if (!stream) { throw std::runtime_error("Camera report output write failed"); }
}
std::string quote(const std::string &value) {
    std::string result{"\""};
    for (char character : value) {
        if (character == '"') { result += '"'; }
        result += character;
    }
    return result + '"';
}
std::size_t entries(const fs::path &path) {
    std::size_t result{};
    for (const auto &entry : fs::directory_iterator{path}) { (void)entry; ++result; }
    return result;
}
void resources(std::ostream &out, int instance, int round, const std::string &stage) {
    std::ifstream memory{"/proc/self/statm"};
    std::uint64_t virtual_pages{}, resident_pages{};
    if (!(memory >> virtual_pages >> resident_pages)) { throw std::runtime_error("Cannot read process memory"); }
    const auto pagesize{::sysconf(_SC_PAGESIZE)};
    if (pagesize <= 0) { throw std::runtime_error("Cannot read process page size"); }
    out << instance << ',' << round << ',' << stage << ','
        << std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() << ','
        << resident_pages * pagesize << ',' << virtual_pages * pagesize << ','
        << entries("/proc/self/task") << ',' << entries("/proc/self/fd") << '\n';
    checked(out);
}
void snapshot(const fs::path &path, const FrameRef &frame) {
    std::ofstream out{path, std::ios::binary};
    const auto view{frame->mapRead()};
    if (!view) { throw std::runtime_error("Snapshot map failed"); }
    // PGM P5 是无损的原生八位灰度文件，逐行写有效像素，不包含填充。
    out << "P5\n" << frame->width() << ' ' << frame->height() << "\n255\n";
    for (int row{0}; row < frame->height(); ++row) {
        out.write(reinterpret_cast<const char *>(view->mat().ptr(row)), frame->width());
    }
    checked(out);
    out.close();
    if (!out) { throw std::runtime_error("Snapshot close failed"); }
}
void run(const Options &options, Ar0234CameraDependencies injected) {
    interrupted = 0;
    validateCameraConfig(options.config);
    std::signal(SIGINT, interrupt);
    std::signal(SIGTERM, interrupt);
    if (!fs::create_directory(options.output)) { throw std::runtime_error("Output directory already exists"); }
    std::ofstream config_file{options.output / "config.txt"};
    config_file << "RK3576 独立相机采集；未运行模型或 ROS。\n"
        << "video_device=" << options.config.path << "\nmedia_device=" << options.config.camera_media_device
        << "\nsensor_entity=" << options.config.camera_sensor_entity << "\nwidth=" << options.config.width
        << "\nheight=" << options.config.height << "\nfps=" << options.config.fps
        << "\nbuffers=" << options.config.camera_buffer_count << "\nframes=" << options.frames
        << "\nseconds=" << options.seconds << "\nrounds=" << options.rounds << "\ninstances=" << options.instances
        << "\nslow_ms=" << options.slow_ms << "\nsnapshots=" << options.snapshots
        << "\ncompare_pixels=" << options.compare
        << "\nmode=" << (options.config.trigger_mode ? "software_trigger" : "continuous")
        << "\ntrigger_hz=" << options.config.camera_trigger_rate_hz
        << "\nfinite_commands=" << options.finite_commands << "\ncommands=" << options.commands
        << "\ntail_ms=" << options.tail_ms
        << "\n时间单位为微秒。driver_monotonic 使用驱动单调时间；"
           "dequeue_fallback 只表示出队时间。"
        << "\n时间按每轮固定偏移映射到系统日期时间，未验证曝光同步。"
        << "\nresources.csv 的 rss_bytes 为驻留主机内存；fd_count 包括报告和资源查询自身句柄。"
        << "\nframes.csv 包含所有成功复制并归还的驱动帧，包括被队列丢弃的新帧。\n";
    checked(config_file);
    std::ofstream frames{options.output / "frames.csv"};
    std::ofstream rounds{options.output / "rounds.csv"};
    std::ofstream resource{options.output / "resources.csv"};
    std::ofstream triggers{options.output / "triggers.csv"};
    triggers << "instance,round,before_us,after_us,success,error\n";
    checked(triggers);
    frames << "instance,round,sequence,raw_seconds,raw_microseconds,flags,time_source,strategy,dequeue_us,"
              "timestamp_us,offset_us,pair_span_us,width,height,stride,sizeimage,buffers,copy_us,"
              "pixels_checked,pixels_equal,read,enqueued,queue_dropped,driver_dropped,clock_offset_delta_us\n";
    rounds << "instance,round,consumed,read,enqueued,queue_dropped,driver_dropped,elapsed_us,round_capture_fps,"
              "round_consume_fps,stop_us,"
              "cleanup_us,state,reason,mode,requested_trigger_hz,actual_trigger_hz,trigger_success,trigger_errors,"
              "trigger_skipped,frames_at_send_stop,tail_frames,unconfirmed_at_send_stop,unconfirmed_at_close,"
              "command_frame_difference,tool_timeout,controls_restored,unrestored_controls\n";
    resource << "instance,round,stage,monotonic_us,rss_bytes,virtual_bytes,threads,fd_count\n";
    checked(frames); checked(rounds); checked(resource);
    resources(resource, 0, 0, "baseline");
    for (int instance{0}; instance < options.instances && !interrupted; ++instance) {
        int active_round{};
        int saved{};
        FactoryOptions factory_options{};
        factory_options.enable_camera = true;
        factory_options.camera = injected;
        factory_options.camera.compare_pixels = options.compare;
        if (options.finite_commands) { factory_options.camera.trigger_limit = options.commands; }
        factory_options.camera.observe_trigger = [&](const Ar0234TriggerEvent &event) {
            triggers << instance << ',' << active_round << ',' << event.before_us << ',' << event.after_us
                << ',' << event.success << ',' << quote(event.error) << '\n';
            checked(triggers);
        };
        factory_options.camera.observe = [&](const CameraSample &sample, const Ar0234CameraDiagnostic &status) {
            const auto &format{status.format};
            frames << instance << ',' << active_round << ',' << sample.sequence << ',' << sample.seconds << ','
                << sample.microseconds << ',' << sample.flags << ','
                << ((sample.flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK) == V4L2_BUF_FLAG_TSTAMP_SRC_SOE
                    ? "SOE" : "EOF_or_unspecified") << ','
                << sample.strategy << ',' << sample.dequeue_us << ',' << sample.frame->timestampUs() << ','
                << status.clock_pair.offset_us << ',' << status.clock_pair.span_us << ',' << format.width << ','
                << format.height << ',' << format.stride << ',' << format.sizeimage << ',' << format.buffers << ','
                << sample.copy_us << ',' << sample.pixels_checked << ',' << sample.pixels_equal << ','
                << status.source.read << ',' << status.source.enqueued << ',' << status.source.dropped << ','
                << status.driver_dropped << ',' << sample.clock_offset_delta_us << '\n';
            checked(frames);
            if (saved < options.snapshots) {
                snapshot(options.output / ("frame-" + std::to_string(instance) + "-" + std::to_string(active_round)
                    + "-" + std::to_string(saved++) + ".pgm"), sample.frame);
            }
        };
        auto factory{createFactory({}, std::move(factory_options))};
        auto result{factory->createFrameSource(options.config)};
        if (!result) { throw std::runtime_error(result.error.describe()); }
        auto &source{*result.value};
        auto *camera{dynamic_cast<Ar0234CameraFrameSource *>(&source)};
        if (!camera) { throw std::runtime_error("Factory returned unexpected source"); }
        resources(resource, instance, 0, "created");
        for (active_round = 0; active_round < options.rounds && !interrupted; ++active_round) {
            saved = 0;
            const auto begin{std::chrono::steady_clock::now()};
            if (!source.start()) { throw std::runtime_error(source.status().reason); }
            std::uint64_t consumed{};
            auto last_resource{begin};
            bool sending_stopped{};
            bool tool_timeout{};
            auto send_stopped_at{begin};
            std::uint64_t frames_at_send_stop{};
            std::uint64_t unconfirmed_at_send_stop{};
            const double budget_seconds{options.seconds ? static_cast<double>(options.seconds)
                : (options.finite_commands ? options.commands / options.config.camera_trigger_rate_hz
                    + 5.0 + options.tail_ms / 1000.0 : 0.0)};
            while (!interrupted) {
                const auto snapshot{source.status()};
                if (options.finite_commands && !sending_stopped && snapshot.trigger_success >= options.commands) {
                    camera->stopTriggers();
                    sending_stopped = true;
                    send_stopped_at = std::chrono::steady_clock::now();
                    const auto stopped_snapshot{source.status()};
                    frames_at_send_stop = stopped_snapshot.read;
                    unconfirmed_at_send_stop = stopped_snapshot.trigger_success > stopped_snapshot.read
                        ? stopped_snapshot.trigger_success - stopped_snapshot.read : 0;
                }
                if (sending_stopped && std::chrono::steady_clock::now() - send_stopped_at
                    >= std::chrono::milliseconds{options.tail_ms}) { break; }
                if (options.finite_commands && std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - begin).count() >= budget_seconds) {
                    tool_timeout = true;
                    break;
                }
                if (options.frames && consumed >= options.frames) { break; }
                if (options.seconds
                    && std::chrono::steady_clock::now() - begin >= std::chrono::seconds{options.seconds}) {
                    break;
                }
                FrameRef frame{};
                if (source.waitFrame(frame, std::chrono::milliseconds{100})) { ++consumed; }
                if (!source.isRunning()) { break; }
                const auto now{std::chrono::steady_clock::now()};
                if (now - last_resource >= std::chrono::seconds{1}) {
                    resources(resource, instance, active_round, "running"); last_resource = now;
                }
                // 慢消费也定期响应 SIGINT，并在每轮时长达到时停止。
                const auto delayed_until{now + std::chrono::milliseconds{options.slow_ms}};
                while (!interrupted && std::chrono::steady_clock::now() < delayed_until) {
                    if (!source.isRunning()) { break; }
                    const auto sample_time{std::chrono::steady_clock::now()};
                    if (options.finite_commands && (std::chrono::duration<double>(sample_time - begin).count()
                        >= budget_seconds || source.status().trigger_success >= options.commands)) { break; }
                    if (sample_time - last_resource >= std::chrono::seconds{1}) {
                        resources(resource, instance, active_round, "slow_consumer");
                        last_resource = sample_time;
                    }
                    if (options.seconds && std::chrono::steady_clock::now() - begin
                        >= std::chrono::seconds{options.seconds}) { break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds{10});
                }
            }
            if (!sending_stopped) {
                camera->stopTriggers();
                const auto stopped_snapshot{source.status()};
                frames_at_send_stop = stopped_snapshot.read;
                unconfirmed_at_send_stop = stopped_snapshot.trigger_success > stopped_snapshot.read
                    ? stopped_snapshot.trigger_success - stopped_snapshot.read : 0;
            }
            const bool stopped{source.stop()};
            const auto status{camera->diagnostic()};
            const auto elapsed{std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - begin).count()};
            rounds << instance << ',' << active_round << ',' << consumed << ',' << status.source.read << ','
                << status.source.enqueued << ',' << status.source.dropped << ',' << status.driver_dropped << ','
                << elapsed << ',' << (elapsed > 0 ? status.source.read * 1000000.0 / elapsed : 0.0) << ','
                << (elapsed > 0 ? consumed * 1000000.0 / elapsed : 0.0) << ','
                << status.stop_us << ',' << status.cleanup_us << ','
                << static_cast<int>(status.source.state) << ',' << quote(status.source.reason) << ','
                << (options.config.trigger_mode ? "software_trigger" : "continuous") << ','
                << status.source.requested_trigger_hz << ',' << status.source.actual_trigger_hz << ','
                << status.source.trigger_success << ',' << status.source.trigger_errors << ','
                << status.source.trigger_skipped << ',' << frames_at_send_stop << ','
                << status.source.read - frames_at_send_stop << ',' << unconfirmed_at_send_stop << ','
                << (status.source.trigger_success > status.source.read
                    ? status.source.trigger_success - status.source.read : 0) << ','
                << (static_cast<std::int64_t>(status.source.trigger_success)
                    - static_cast<std::int64_t>(status.source.read)) << ','
                << tool_timeout << ',' << status.source.controls_restored << ',';
            std::string unrestored{};
            for (const auto &name : status.unrestored_controls) { unrestored += name + " "; }
            rounds << quote(unrestored) << '\n';
            checked(rounds);
            resources(resource, instance, active_round, "stopped");
            if (tool_timeout) { throw std::runtime_error("Finite trigger tool timeout"); }
            if (!stopped) { throw std::runtime_error(status.source.reason); }
            if (status.stop_us > 1000000) { throw std::runtime_error("Camera stop exceeded 1 second"); }
            if (consumed == 0 && !interrupted && !(options.finite_commands && options.commands == 0)) {
                throw std::runtime_error("No frames consumed");
            }
        }
        result.value.reset();
        resources(resource, instance, active_round, "destroyed");
    }
    checked(frames); checked(rounds); checked(resource);
    for (auto *stream : {&config_file, &frames, &rounds, &resource, &triggers}) {
        stream->close();
        if (!*stream) { throw std::runtime_error("Report close failed"); }
    }
    if (interrupted) { throw std::runtime_error("Interrupted by signal; stopped and released camera"); }
}
} // namespace rk3576::ar0234_camera_driver

#include "../replay_runner.h"
#include "platform/rk3576/platform_factory.h"
#include "platform/rk3576/host_frame.h"
#include "hybrid_tracker/tests/model_test_support.h"
#include "platform/rk3576/nanotrack_preprocessor.h"
#include "platform/rk3576/yolo_preprocessor.h"
#include <atomic>
#include <csignal>
#include <sys/resource.h>
#include <condition_variable>
#include <future>
#include <iostream>
#include <limits>
#include <thread>
#include <unistd.h>
#include <opencv2/videoio.hpp>
namespace {
using model_test::require;
using namespace replay;
std::filesystem::path root{};
std::string read(const std::filesystem::path &path) {
    std::ifstream in{path};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}
void write(const std::filesystem::path &path, const std::string &s) {
    std::ofstream out{path};
    out << s;
}
template <typename F> void rejects(F f, const char *why) {
    bool rejected{};
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, why);
}
std::string replace(std::string s, const std::string &from, const std::string &to) {
    const auto pos{s.find(from)};
    require(pos != std::string::npos, "测试替换字段不存在");
    s.replace(pos, from.size(), to);
    return s;
}
ReplayConfig config() {
    auto c{readConfig(root / "config.yaml")};
    c.directory = root / "run";
    c.tracker.target_class_id = 0;
    c.tracker.yolo_interval = 1;
    c.tracker.lost_max_count = 2;
    return c;
}
void configurations() {
    const auto original{read(REPLAY_EXAMPLE)};
    write(root / "config.yaml", original);
    const auto c{config()};
    require(c.fps == 30 && c.tracker.dcf.model_sz == 96 && c.tracker.kf_fx == 800 && c.models.backbone.empty() &&
                c.tracker.yolo_engine_path.empty(),
            "配置映射不完整");
    cv::FileStorage snapshot{".json", cv::FileStorage::WRITE | cv::FileStorage::MEMORY | cv::FileStorage::FORMAT_JSON};
    writeConfig(snapshot, c);
    const auto saved{snapshot.releaseAndGetString()};
    require(saved.find("dcf_eps") != std::string::npos && saved.find("kf_history_len") != std::string::npos,
            "配置快照缺参数");
    const auto cwd{std::filesystem::current_path()};
    std::filesystem::current_path(root);
    const auto relative{readConfig("config.yaml")};
    std::filesystem::current_path(cwd);
    require(relative.models.yolo == c.models.yolo, "路径依赖工作目录");
    const std::vector<std::string> invalid{
        replace(original, "schema_version: 1", "schema_version: 2"),
        replace(original, "  fps: 30.0", ""),
        replace(original, "  fps: 30.0", "  fps: 30.0\n  fps: 25.0"),
        replace(original, "  fps: 30.0", "  fps: .Nan"),
        replace(original, "  fps: 30.0", "  fps: .Inf"),
        replace(original, "  fps: 30.0", "  fps: 0"),
        replace(original, "  fps: 30.0", "  fps: \"30\""),
        replace(original, "  fps: 30.0", "  fps: 30.0\n  mystery: 1"),
        replace(original, "max_frames: 0", "max_frames: 4294967296"),
        replace(original, "yolo_interval: 4", "yolo_interval: 999999999999999999999999999"),
        replace(original, "enable_dcf: 1", "enable_dcf: true"),
        replace(original, "enable_dcf: 1", "enable_dcf: 2"),
        replace(original, "enable_dcf: 1", "enable_dcf: 1.0"),
        replace(original, "dcf_eps: 0.00001", "dcf_eps: 1.e-100"),
        original + "\n---\nschema_version: 1\n",
        original + "\n---\n",
        replace(original, "max_frames: 0", "max_frames: 0x100000000"),
        replace(original, "schema_version: 1", "schema_version: 1\ninclude: file.yaml")};
    for (const auto &text : invalid) {
        write(root / "bad.yaml", text);
        rejects([&] { readConfig(root / "bad.yaml"); }, "非法配置未拒绝");
    }
    std::filesystem::create_directories(c.directory);
    write(root / "exists.yaml", replace(original, "runs/fp16-001", c.directory.string()));
    rejects([&] { readConfig(root / "exists.yaml"); }, "已有输出目录未拒绝");
    std::filesystem::remove(c.directory);
    const char *args[]{"replay", "unused", "--fps", "60"};
    rejects([&] { commandConfig(1, args); }, "无参数未拒绝");
    rejects([&] { commandConfig(4, args); }, "额外参数未拒绝");
}
class Source final : public VideoSource {
public:
    Source(int frames, std::uint64_t declared = 0, double fps = 30)
        : remaining_{frames}, declared_{declared}, fps_{fps} {}
    bool read(cv::Mat &image) override {
        if (remaining_-- <= 0) {
            return false;
        }
        image = cv::Mat{80, 100, CV_8UC3, cv::Scalar::all(42)};
        return true;
    }
    VideoMetadata metadata() const override { return {0, 0, fps_, declared_, "controlled"}; }

private:
    int remaining_{};
    std::uint64_t declared_{};
    double fps_{};
};
class SequenceSource final : public VideoSource {
public:
    explicit SequenceSource(std::vector<cv::Mat> images) : images_{std::move(images)} {}
    bool read(cv::Mat &image) override {
        if (next_ == images_.size()) {
            return false;
        }
        image = images_[next_++];
        return true;
    }
    VideoMetadata metadata() const override { return {0, 0, 0, images_.size(), "sequence"}; }

private:
    std::vector<cv::Mat> images_{};
    std::size_t next_{};
};
void readersAndClock() {
    const auto video_path{root / "synthetic.avi"};
    {
        cv::VideoWriter writer{video_path.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30, {100, 80}};
        require(writer.isOpened(), "无法创建可控录像");
        for (int i{0}; i < 3; ++i) {
            writer.write(cv::Mat{80, 100, CV_8UC3, cv::Scalar::all(i * 40)});
        }
    }
    ReplayReader actual{openVideo(video_path.string()), 30, 0};
    DecodedFrame actual_frame{};
    int actual_count{};
    while (actual.next(actual_frame)) {
        ++actual_count;
    }
    require(actual_count == 3 && actual.endReason() == "eof" && !actual.metadata().backend.empty(),
            "真实 OpenCV 解码入口丢首帧或缺元数据");
    for (const auto &entry :
         std::vector<std::pair<std::uint64_t, std::string>>{{3, "eof"}, {5, "decode_error"}, {0, "end_unverified"}}) {
        ReplayReader reader{std::make_unique<Source>(3, entry.first), 30, 0};
        DecodedFrame frame{};
        int count{};
        while (reader.next(frame)) {
            ++count;
            require(grayImage(frame.image).type() == CV_8UC1, "未转换灰度");
        }
        require(count == 3 && reader.readCount() == 3 && reader.endReason() == entry.second, "首帧或结束分类错误");
    }
    ReplayReader limited{std::make_unique<Source>(3, 3), 30, 1};
    DecodedFrame frame{};
    require(limited.next(frame) && !limited.next(frame) && limited.endReason() == "frame_limit", "帧上限错误");
    rejects([] { ReplayReader r{std::make_unique<Source>(3, 3, 25), 30, 0}; }, "fps 冲突未拒绝");
    require(frameTimestamp(0, 30) == 1000000 && frameTimestamp(1, 30) == 1033333 && frameTimestamp(2, 30) == 1066667 &&
                frameTimestamp(1, 29.97) == 1033367,
            "时间戳公式错误");
    require(frameTimestamp(9000000000ULL, 1000000) == 9001000000LL, "长序列时间丢精度");
    rejects([] { frameTimestamp(std::numeric_limits<std::uint64_t>::max(), 30); }, "时间溢出未拒绝");
    std::int64_t time{};
    ReplayClock clock{30, {[&] { return time; }, [&](std::int64_t target) { time = std::max(time, target); }}};
    require(clock.wait() == 0, "首帧调度错误");
    time = 60000000;
    clock.finished();
    require(clock.wait() >= 93333334, "慢更新后追赶发送");
    time += 100000000;
    clock.finished();
    require(clock.wait() >= 226666668, "积压调度错误");
    time = 0;
    ReplayClock slow{0.1, {[&] { return time; }, [&](std::int64_t target) { time = std::max(time, target); }}};
    slow.finished();
    require(slow.wait([&] { return time >= 50000000; }) == 50000000, "低帧率等待未及时响应中断");
    ReplayReader grayscale{
        std::make_unique<SequenceSource>(std::vector<cv::Mat>{cv::Mat{3, 4, CV_8UC1, cv::Scalar{42}}}), 30, 0};
    require(grayscale.next(frame) && frame.image.type() == CV_8UC1 && frame.image.at<unsigned char>(0, 0) == 42,
            "灰度首帧丢失");
    for (int type : {CV_8UC4, CV_32FC1}) {
        rejects(
            [&] {
                ReplayReader r{
                    std::make_unique<SequenceSource>(std::vector<cv::Mat>{cv::Mat{3, 4, type, cv::Scalar::all(0)}}), 30,
                    0};
            },
            "读取器未拒绝非法首帧格式");
    }
    ReplayReader changed{std::make_unique<SequenceSource>(std::vector<cv::Mat>{
                             cv::Mat{3, 4, CV_8UC1, cv::Scalar::all(0)}, cv::Mat{5, 4, CV_8UC1, cv::Scalar::all(0)}}),
                         30, 0};
    require(changed.next(frame), "合法首帧未保留");
    rejects([&] { changed.next(frame); }, "中途尺寸变化未拒绝");
    require(changed.endReason() == "decode_error", "尺寸变化结束分类错误");
    rejects([] { grayImage(cv::Mat{3, 3, CV_32FC1, cv::Scalar::all(0)}); }, "浮点图未拒绝");
    rejects([] { grayImage(cv::Mat{3, 3, CV_8UC4, cv::Scalar::all(0)}); }, "四通道未拒绝");
}
class Storage final : public model::TensorStorage {
public:
    std::vector<float> values{};
    const float *hostData() const noexcept override { return values.data(); }
    std::size_t byteSize() const noexcept override { return values.size() * sizeof(float); }
};
struct Control {
    std::atomic<int> frame{};
    std::atomic<int> active{};
    std::atomic<int> calls{};
    std::atomic<int> destroyed{};
    std::atomic<int> templates{};
    std::mutex mutex{};
    std::condition_variable cv{};
    bool block{};
    bool entered{};
    bool release{};
};
class Yolo final : public model::YoloBackend {
public:
    explicit Yolo(std::shared_ptr<Control> c) : c_{std::move(c)} {}
    ~Yolo() override {
        require(c_->active == 0, "仍在执行时释放模型");
        ++c_->destroyed;
    }
    ModelInputContract inputContract() const override { return rk3576::yoloContract(); }
    model::Result<int> classCount() const override { return {1}; }
    model::Result<model::YoloPrediction> infer(const PreparedInputRef &) override {
        ++c_->active;
        ++c_->calls;
        {
            std::unique_lock<std::mutex> lock{c_->mutex};
            c_->entered = true;
            c_->cv.notify_all();
            c_->cv.wait(lock, [&] { return !c_->block || c_->release; });
        }
        auto storage{std::make_shared<Storage>()};
        storage->values.resize(50, 0);
        storage->values[0] = 320;
        storage->values[10] = 320;
        storage->values[20] = 100;
        storage->values[30] = 100;
        storage->values[40] = c_->frame < 15 || c_->frame >= 65 ? 0.99f : 0;
        --c_->active;
        model::Tensor tensor{{{1, 5, 10}, TensorElementType::Float32, TensorLayout::Linear, 200, {}}, storage};
        return {{std::move(tensor), 1, 2}};
    }

private:
    std::shared_ptr<Control> c_;
};
class Nano final : public model::NanoTrackBackend {
public:
    explicit Nano(std::shared_ptr<Control> c) : c_{std::move(c)} {}
    model::Status setTemplate(const PreparedInputRef &) override {
        ++c_->templates;
        return {true};
    }
    model::Result<model::NanoPrediction> search(const PreparedInputRef &) override {
        return {{model_test::tensor({1, 2, 15, 15}, 1), model_test::tensor({1, 4, 15, 15}, 10)}};
    }
    void reset() noexcept override {}

private:
    std::shared_ptr<Control> c_;
};
rk3576::AssemblyDependencies assembly(const std::shared_ptr<Control> &c) {
    rk3576::AssemblyDependencies d{};
    d.yolo = [c](const std::string &) {
        return PlatformResult<rk3576::YoloComponents>{{std::make_unique<Yolo>(c), "controlled"}};
    };
    d.nano = [c](const ModelPaths &) {
        return PlatformResult<rk3576::NanoTrackComponents>{
            {std::make_unique<Nano>(c), std::make_unique<rk3576::NanoTrackPreprocessor>(), "controlled"}};
    };
    return d;
}
class ForeignFrame final : public Frame {
public:
    explicit ForeignFrame(FrameRef frame) : frame_{std::move(frame)} {}
    int width() const noexcept override { return frame_->width(); }
    int height() const noexcept override { return frame_->height(); }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::uint64_t timestampUs() const noexcept override { return frame_->timestampUs(); }
    std::unique_ptr<FrameReadView> mapRead() const override { return frame_->mapRead(); }

private:
    FrameRef frame_;
};
void compatibility() {
    auto control{std::make_shared<Control>()};
    auto factory{rk3576::createFactory(assembly(control))};
    auto components{factory->createTrackerComponents(config().models)};
    auto tracker{HybridTracker::CreateInstance(config().tracker, std::move(components.value))};
    require(tracker->init(), "兼容性测试初始化失败");
    FrameContext context{};
    context.now_us = 1000000;
    context.frame = rk3576::copyHostFrame(cv::Mat{80, 100, CV_8UC3, cv::Scalar::all(0)}, 9000000);
    const auto color{tracker->update(context)};
    require(!color.consumed && color.state == TrackerState::Detecting, "彩色帧改变状态");
    context.frame =
        std::make_shared<ForeignFrame>(rk3576::copyGrayFrame(cv::Mat{80, 100, CV_8UC1, cv::Scalar::all(0)}, 9000000));
    require(!tracker->update(context).consumed, "伪装 Gray8 的外平台帧被接受");
    context.frame = rk3576::createFactory({})
                        ->createFrameBuilder()
                        .value->copyFrom(cv::Mat{80, 100, CV_8UC1, cv::Scalar{42}}, 1000000)
                        .value;
    require(tracker->update(context).consumed, "拒绝帧污染时间，或跨创建器灰度被拒绝");
    require(!tracker->update(context).consumed && tracker->lastNanoBox().width <= 0 && tracker->lastNanoScore() < 0,
            "重复帧未拒绝，或残留上一帧诊断");
}
void pipeline(bool dcf) {
    auto c{config()};
    c.tracker.enable_dcf = dcf;
    c.directory = root / (dcf ? "dcf-on" : "dcf-off");
    c.tracker.max_position_jump = 10000;
    c.tracker.max_size_jump_ratio = 100;
    auto control{std::make_shared<Control>()};
    ReplayDependencies d{};
    d.factory = [control] { return rk3576::createFactory(assembly(control)); };
    d.video = [](const std::string &) { return std::make_unique<Source>(100, 100); };
    std::int64_t time{};
    d.clock = {[&] { return time; },
               [&](std::int64_t target) {
                   time = std::max(time, target);
                   // 仅测试时钟让后台线程取得 CPU；生产回放不等待检测结果。
                   std::this_thread::sleep_for(std::chrono::milliseconds{15});
                   ++control->frame;
               }};
    ReplayReport report{c};
    require(runReplay(c, report, d) == 0, "完整回放失败");
    cv::FileStorage result{(c.directory / "run.json").string(), cv::FileStorage::READ};
    require(result["input"]["submitted_frames"].string() == "100", "提交帧数错误");
    require(result["input"]["accepted_frames"].string() == "100", "有效帧计数错误");
    bool found{}, tracking{}, lost{}, recovered{};
    for (const auto &event : result["state_transitions"]) {
        const int state{static_cast<int>(event["state"])};
        if (state == 1) {
            if (lost) {
                recovered = true;
            }
            found = true;
        }
        if (state == 2) {
            tracking = true;
        }
        if (state == 3) {
            lost = true;
        }
    }
    require(found && tracking && lost && recovered, "自动捕获/跟踪/丢失恢复未覆盖");
    require(control->destroyed == 1 && control->active == 0 && control->templates > 1, "模型生命周期不完整");
}
void finalWaitAndStartup() {
    auto c{config()};
    c.directory = root / "wait";
    c.max_frames = 1;
    auto control{std::make_shared<Control>()};
    control->block = true;
    ReplayDependencies d{};
    d.factory = [control] { return rk3576::createFactory(assembly(control)); };
    d.video = [](const std::string &) { return std::make_unique<Source>(3, 3); };
    d.check_diagnostics = [control] {
        std::unique_lock<std::mutex> lock{control->mutex};
        if (!control->cv.wait_for(lock, std::chrono::seconds{5}, [&] { return control->entered; })) {
            throw std::runtime_error("Expected running detector before final frame shutdown");
        }
    };
    ReplayReport report{c};
    auto run{std::async(std::launch::async, [&] { return runReplay(c, report, d); })};
    {
        std::unique_lock<std::mutex> lock{control->mutex};
        require(control->cv.wait_for(lock, std::chrono::seconds{5}, [&] { return control->entered; }),
                "末帧检测未开始");
    }
    require(run.wait_for(std::chrono::milliseconds{30}) == std::future_status::timeout && control->destroyed == 0,
            "未等待末帧检测就退出");
    {
        std::lock_guard<std::mutex> lock{control->mutex};
        control->release = true;
    }
    control->cv.notify_all();
    require(run.get() == 0 && control->destroyed == 1, "末帧释放失败");
    cv::FileStorage result{(c.directory / "run.json").string(), cv::FileStorage::READ};
    require(result["input"]["submitted_frames"].string() == "1", "末尾增加虚拟帧");
    d.check_diagnostics = [] {};
    c.directory = root / "startup";
    d.launch = [](std::function<void()>) -> std::thread { throw std::runtime_error("injected thread start failure"); };
    ReplayReport failed{c};
    require(runReplay(c, failed, d) == 1, "线程启动失败仍成功");
    c.directory = root / "interrupt";
    d.launch = launchDetectionThread;
    d.interrupted = [] { return true; };
    ReplayReport stopped{c};
    require(runReplay(c, stopped, d) == 130, "中断退出码错误");
}
void reports() {
    auto c{config()};
    c.directory = root / "reports";
    ReplayReport report{c};
    require(read(c.directory / "run.json").find("running") != std::string::npos, "未写入 running 状态");
    FrameRecord r{};
    r.index = 9007199254740993ULL;
    r.timestamp_us = 9007199254740993LL;
    r.output.info = "逗号,双引号\"和\n换行";
    r.nano_score = std::numeric_limits<float>::quiet_NaN();
    report.models(std::string(12000, 'x'));
    report.frame(r);
    report.finish("eof", 1, 5);
    const auto csv{read(c.directory / "frames.csv")};
    require(csv.find("9007199254740993,9007199254740993,9007199254740993") != std::string::npos, "整数精度丢失");
    require(csv.find("0,,,,,0,,,,,0,,,,,,") != std::string::npos, "无效框/分数未留空");
    require(csv.find(csvEscape(r.output.info)) != std::string::npos, "CSV 转义失败");
    cv::FileStorage parsed{(c.directory / "run.json").string(), cv::FileStorage::READ};
    require(parsed["input"]["accepted_frames"].string() == "0" && parsed["input"]["submitted_frames"].string() == "1",
            "consumed=false 计数错误");
    require(read(c.directory / "run.json").find("9007199254740993") != std::string::npos, "JSON 整数丢精度");
    write(root / "hash", "abc");
    require(sha256(root / "hash") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 错误");
    c.directory = root / "csv-failure";
    ReplayReport csv_failed{c};
    struct rlimit old_limit{};
    require(::getrlimit(RLIMIT_FSIZE, &old_limit) == 0, "无法取得文件大小测试限制");
    auto limited{old_limit};
    limited.rlim_cur = std::filesystem::file_size(c.directory / "frames.csv") + 2;
    const auto handler{std::signal(SIGXFSZ, SIG_IGN)};
    require(::setrlimit(RLIMIT_FSIZE, &limited) == 0, "无法设置输出失败注入");
    bool failed_write{};
    try {
        csv_failed.frame(r);
    } catch (const std::exception &) {
        failed_write = true;
    }
    ::setrlimit(RLIMIT_FSIZE, &old_limit);
    std::signal(SIGXFSZ, handler);
    require(failed_write, "CSV 写失败未暴露");
    c.directory = root / "write-failure";
    ReplayReport failed{c};
    std::filesystem::create_directory(c.directory / "run.json.tmp");
    rejects([&] { failed.finish("eof", 0, 0); }, "汇总写入失败未暴露");
    require(read(c.directory / "run.json").find("running") != std::string::npos, "失败覆盖为成功状态");
}
} // namespace
int main() {
    try {
        cv::setNumThreads(1);
        root = std::filesystem::temp_directory_path() / ("tracker-replay-test-" + std::to_string(::getpid()));
        std::filesystem::create_directories(root);
        configurations();
        readersAndClock();
        reports();
        compatibility();
        pipeline(false);
        pipeline(true);
        finalWaitAndStartup();
        std::filesystem::remove_all(root);
        std::cout << "replay tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " artifacts=" << root << '\n';
        return 1;
    }
}

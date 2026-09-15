#include "../production_factory.h"
#include "../host_frame.h"
#include "platform/model_paths.h"
#include "hybrid_tracker.h"
#include "rknn_test_support.h"
#include <iostream>
namespace {
using namespace rknn_test;
Sdk fixtures() {
    Model y{};
    y.inputs = {attribute(0, {1, 640, 640, 3}, RKNN_TENSOR_INT8, RKNN_TENSOR_NHWC)};
    y.inputs[0].scale = 1.0f / 255.0f;
    y.inputs[0].zp = -128;
    for (std::uint32_t side : {80U, 40U, 20U}) {
        y.outputs.push_back(attribute(static_cast<std::uint32_t>(y.outputs.size()), {1, 64, side, side}));
        y.outputs.push_back(attribute(static_cast<std::uint32_t>(y.outputs.size()), {1, 4, side, side}));
    }
    Model z{};
    z.inputs = {attribute(0, {1, 3, 127, 127})};
    z.outputs = {attribute(0, {1, 96, 8, 8})};
    Model x{};
    x.inputs = {attribute(0, {1, 3, 255, 255})};
    x.outputs = {attribute(0, {1, 96, 16, 16})};
    Model h{};
    h.inputs = {attribute(0, {1, 96, 8, 8}), attribute(1, {1, 96, 16, 16})};
    h.outputs = {attribute(0, {1, 2, 15, 15}), attribute(1, {1, 4, 15, 15})};
    return {{y, z, x, h}};
}
void paths() {
    const ModelPaths old{"y", "b", "h"};
    const ModelPaths split{"y", "", "h", "z", "x"};
    require(validateModelPaths(old, "jetson").reason.empty(), "合法三字段聚合初始化被破坏");
    require(validateModelPaths(split, "rk3576").reason.empty(), "合法四角色被拒绝");
    require(!validateModelPaths(old, "rk3576").reason.empty(), "RK3576 接受共享模式");
    require(!validateModelPaths(split, "jetson").reason.empty(), "Jetson 接受拆分模式");
    auto mixed{split};
    mixed.backbone = "b";
    require(!validateModelPaths(mixed, "jetson").reason.empty() && !validateModelPaths(mixed, "rk3576").reason.empty(),
            "混填未拒绝");
    for (int i{0}; i < 4; ++i) {
        auto missing{split};
        std::array<std::string *, 4> fields{&missing.yolo, &missing.template_backbone, &missing.search_backbone,
                                            &missing.head};
        fields[i]->clear();
        require(!validateModelPaths(missing, "rk3576").reason.empty(), "缺少角色未拒绝");
    }
}
void failures() {
    const ModelPaths paths{"y", "", "h", "z", "x"};
    const std::array<std::string, 4> roles{"yolo", "template_backbone", "search_backbone", "head"};
    const std::array<std::string, 4> files{"y", "z", "x", "h"};
    for (int failure{0}; failure < 4; ++failure) {
        auto state{fixtures()};
        sdk = &state;
        state.models[failure].query_status = -7;
        auto result{rk3576::assembleTracker(paths, rk3576::productionDependencies(api()))};
        require(!result && !result.value.complete(), "失败仍返回组件");
        require(result.error.component == roles[failure] && result.error.path == files[failure], "失败角色或路径丢失");
        for (int i{0}; i < 4; ++i) {
            require(state.models[i].destroys == (i <= failure ? 1 : 0), "会话未恰好释放一次");
            require(state.models[i].runs == 0, "装配期间启动推断");
        }
    }
    auto state{fixtures()};
    sdk = &state;
    auto result{rk3576::assembleTracker(paths, rk3576::productionDependencies(api()))};
    require(result && result.value.complete() && !result.value.description.empty(), "完整装配或属性缺失");
    TrackerSettings settings{};
    settings.target_class_id = 4;
    int starts{};
    auto tracker{HybridTracker::CreateInstance(settings, std::move(result.value), [&](std::function<void()> work) {
        ++starts;
        return launchDetectionThread(std::move(work));
    })};
    require(!tracker->init() && starts == 0, "类别越界后仍启动线程");
    tracker.reset();
    for (const auto &m : state.models) {
        require(m.destroys == 1, "越界未释放会话");
    }
}
void frames() {
    auto production{CreatePlatformFactory()};
    FrameSourceConfig camera{};
    camera.path = "/dev/camera-must-not-be-opened";
    auto rejected{production->createFrameSource(camera)};
    require(!rejected && rejected.error.reason == "Camera frame source unsupported",
            "正式 ROS 工厂意外开放相机或尝试访问设备");
    std::unique_ptr<FrameReadView> view{};
    {
        auto factory{rk3576::createFactory({})};
        auto builder{factory->createFrameBuilder()};
        cv::Mat parent{10, 20, CV_8UC3, cv::Scalar{12, 34, 56}};
        const auto roi{parent(cv::Rect{1, 2, 8, 6})};
        require(!roi.isContinuous(), "未覆盖非连续图像");
        auto frame{builder.value->copyFrom(roi, 12345)};
        require(frame && frame.value->pixelFormat() == FramePixelFormat::Bgr8 && frame.value->timestampUs() == 12345,
                "像素格式或时间错误");
        require(!rk3576::createFrameCompatibility()->validate(frame.value).reason.empty(), "彩色被跟踪接受");
        view = frame.value->mapRead();
        parent.setTo(0);
        for (auto type : {FrameSourceType::Camera, FrameSourceType::Video}) {
            FrameSourceConfig c{};
            c.type = type;
            auto invalid{factory->createFrameSource(c)};
            require(!invalid && invalid.error.platform == "rk3576", "相机或空视频路径伪成功");
        }
        FrameSourceConfig video{};
        video.type = FrameSourceType::Video;
        video.path = std::string{VIDEO_FIXTURES} + "/420.mov";
        video.output_width = 16;
        video.output_height = 16;
        video.fps = 1;
        video.flip = true;
        video.mirror = true;
        video.use_history_timestamp = true;
        auto source{factory->createFrameSource(video)};
        require(source && !source.value->isRunning(), "合法视频创建失败或提前启动");
        int width{}, height{};
        require(source.value->getImageSize(width, height) && width == 641 && height == 479, "视频尺寸被相机配置改变");
        source.value->configureRecording(true, "/invalid-recording-directory", "unused");
        require(source.value->start(), "真实工厂视频启动失败");
        FrameRef first{};
        require(source.value->waitFrame(first, std::chrono::seconds{1})
            && first->timestampUs() == 1700000000000000, "工厂首帧或时间错误");
        source.value->stop();
        video.path += ".missing";
        auto bad{factory->createFrameSource(video)};
        require(!bad && bad.error.path == video.path && !bad.error.reason.empty(), "失败路径或原因丢失");
    }
    require(view->mat().at<cv::Vec3b>(0, 0) == cv::Vec3b(12, 34, 56), "视图跨工厂生命周期失败");
    auto a{rk3576::createFactory({})};
    auto b{rk3576::createFactory({})};
    auto gray{a->createFrameBuilder().value->copyFrom(cv::Mat{8, 8, CV_8UC1, cv::Scalar{42}}, 10)};
    require(rk3576::createFrameCompatibility()->validate(gray.value).reason.empty(), "跨创建器灰度拒绝");
}
} // namespace
int main() {
    try {
        paths();
        failures();
        frames();
        std::cout << "rk3576 assembly passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

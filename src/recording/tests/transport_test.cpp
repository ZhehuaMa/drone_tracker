#include "recording/recording_manager.h"

#include <cstring>
#include <iostream>
#include <thread>

#include <rosbag2_cpp/reader.hpp>

namespace {
using namespace std::chrono_literals;
void require(bool value, const char *message) { if (!value) { throw std::runtime_error(message); } }
template<typename Predicate> void until(Predicate predicate, const char *message) {
    const auto end{std::chrono::steady_clock::now() + 5s};
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > end) { throw std::runtime_error(message); }
        std::this_thread::sleep_for(5ms);
    }
}
template<typename Message> Message message() {
    Message result{};
    if constexpr (recording::HasHeader<Message>::value) { result.header.stamp.sec = 10;
    result.header.frame_id = "source"; }
    if constexpr (std::is_same_v<Message, std_msgs::msg::String>) { result.data = "AIM"; }
    if constexpr (std::is_same_v<Message, quadrotor_msgs::msg::ControlCommand>) {
        result.expected_execution_time.sec = 11; result.rotor_thrusts = {1.0, 2.0, 3.0, 4.0}; result.armed = true;
    }
    if constexpr (std::is_same_v<Message, quadrotor_msgs::msg::LowLevelFeedback>) {
        result.motor_speeds = {400, 500}; result.thrust_mapping_coeffs = {1, 2, 3};
    }
    if constexpr (std::is_same_v<Message, sensor_msgs::msg::Image>) {
        result.width = 16; result.height = 16; result.step = 48; result.encoding = "bgr8"; result.data.resize(768, 77);
    }
    if constexpr (std::is_same_v<Message, std_msgs::msg::Float32MultiArray>) {
        result.data = {1, 2, 3}; result.layout.dim.resize(1); result.layout.dim[0].label = "motor";
    }
    return result;
}
using PayloadCheck = std::function<bool(const rosbag2_storage::SerializedBagMessage &)>;
struct Source { rclcpp::PublisherBase::SharedPtr publisher{}; std::function<void()> publish{}; };
template<typename Message> Source source(rclcpp::Node &node, const recording::TopicDescriptor &entry,
    std::map<std::string, PayloadCheck> &expected) {
    rclcpp::QoS qos{10};
    if (entry.delivery == recording::Delivery::BestEffort) { qos.best_effort(); }
    if (entry.delivery == recording::Delivery::Retained) { qos.transient_local(); }
    const auto value{message<Message>()};
    expected[entry.name] = [value](const rosbag2_storage::SerializedBagMessage &item) {
        rclcpp::SerializedMessage raw{*item.serialized_data}; rclcpp::Serialization<Message> serializer{};
        Message decoded{}; serializer.deserialize_message(&raw, &decoded); return decoded == value;
    };
    auto publisher{node.create_publisher<Message>(entry.name, qos)};
    return {publisher, [publisher, value] { publisher->publish(value); }};
}
void transport(const std::filesystem::path &root) {
    rclcpp::NodeOptions options{};
    options.arguments({"--ros-args", "-r", "/fpv/odom:=/recording_test/odom"});
    auto node{std::make_shared<rclcpp::Node>("recording_manager_test", options)};
    node->declare_parameter<std::string>("save_path", root.string());
    recording::declareParameters(*node);
    node->set_parameter(rclcpp::Parameter{"bag.bench_profile", true});
    node->set_parameter(rclcpp::Parameter{"bag.topics.body_imu.name", "/recording_test/imu"});
    auto config{recording::readConfig(*node)};
    std::vector<BagSession::Status> results{};
    recording::RecordingManager manager{*node, config, {}, [&](const auto &status) { results.push_back(status); }};
    manager.startExecutor();
    require(!manager.capture(), "preview created session");
    auto publisher_node{std::make_shared<rclcpp::Node>("recording_catalog_source")};
    std::map<std::string, PayloadCheck> expected{};
    std::vector<Source> sources{};
    for (const auto &entry : config.topics) {
        if (entry.source != recording::SourceKind::Subscription) { continue; }
        if (entry.id == "radar_target") { continue; }
#define SOURCE(stable_id, package, type, file, name, time, kind, hz, protect, qos, depth) \
        if (entry.id == #stable_id) { sources.push_back(source<package::msg::type>(*publisher_node, entry, expected)); continue; }
        DRONE_RECORDING_TOPICS(SOURCE)
#undef SOURCE
    }
    // Retained mode is received and cached while idle, before any session exists.
    for (auto &value : sources) {
        until([&] { return value.publisher->get_subscription_count() > 0; }, "catalog publisher did not connect");
        if (std::string{value.publisher->get_topic_name()} == "/fpv/gimbal_mode") { value.publish(); }
    }
    std::this_thread::sleep_for(100ms);
    std_msgs::msg::Header on{}; on.stamp.sec = 9; on.frame_id = "ON";
    manager.observeTrigger(on, 9000000001); manager.observeTrigger(on, 9000000002);
    manager.open(1, 9000000003);
    auto old{manager.capture()};
    require(old != nullptr, "session did not open");
    for (const auto &entry : config.topics) {
        if (entry.id == "radar_target") {
            sources.push_back(source<nav_msgs::msg::Odometry>(*publisher_node, entry, expected));
            until([&] { return sources.back().publisher->get_subscription_count() > 0; }, "late radar did not connect");
        }
    }
    for (auto &value : sources) { value.publish(); }
    until([&] {
        const auto status{old->bag->snapshot()};
        for (const auto &entry : config.topics) {
            if (entry.source == recording::SourceKind::Subscription && status.topics.at(entry.name).accepted == 0) { return false; }
        }
        return true;
    }, "typed catalog roundtrip missing an entry");
    auto bbox{std::make_shared<vision_msgs::msg::Detection2D>(message<vision_msgs::msg::Detection2D>())};
    auto los{std::make_shared<geometry_msgs::msg::TwistStamped>(message<geometry_msgs::msg::TwistStamped>())};
    manager.submitObservationPair(1, bbox, los, false);
    std_msgs::msg::Header off{}; off.stamp.sec = 12; off.frame_id = "OFF";
    manager.observeTrigger(off, 12000000001); manager.submitObservationPair(1, bbox, los, true);
    manager.seal("user_off"); manager.close();
    require(results.size() == 1 && results.back().phase == "complete", "catalog bag incomplete");
    require(results.back().topics.at("/fpv/tracker_trigger").written == 3, "identical real ON occurrences deduplicated");
    require(results.back().topics.at("/fpv/gimbal_mode").snapshots == 1, "initial mode snapshot missing");
    auto oversized_mode{publisher_node->create_publisher<std_msgs::msg::String>(
        "/fpv/gimbal_mode", rclcpp::QoS{1}.reliable().transient_local())};
    until([&] { return oversized_mode->get_subscription_count() > 0; }, "oversized mode publisher did not connect");
    std_msgs::msg::String oversized{}; oversized.data.resize(70000, 'x'); oversized_mode->publish(oversized);
    std::this_thread::sleep_for(100ms);
    manager.open(2, 13000000000);
    auto next{manager.capture()};
    require(next->bag->snapshot().topics.at("/fpv/gimbal_mode").snapshots == 0,
        "oversized latest mode retained an unbounded or stale snapshot");
    const auto &imu{config.topics.front()};
    manager.submit(old, imu, std::make_shared<sensor_msgs::msg::Imu>(), 14, 14);
    require(next->bag->snapshot().topics.at(imu.name).accepted == 0, "old handle entered replacement session");
    manager.seal("eof"); manager.close(); manager.stopExecutor();
    require(results.size() == 2 && results.back().phase == "complete", "second session close failed");
    std::size_t directories{}; std::set<std::string> seen{};
    for (const auto &directory : std::filesystem::directory_iterator{root}) {
        ++directories; require(std::filesystem::exists(directory.path() / "session.json"), "summary missing");
        rosbag2_cpp::Reader reader{}; reader.open(directory.path().string());
        while (reader.has_next()) {
            const auto item{reader.read_next()}; const auto found{expected.find(item->topic_name)};
            if (found == expected.end()) { continue; }
            require(found->second(*item), ("typed payload changed: " + item->topic_name).c_str());
            seen.insert(item->topic_name);
            const auto descriptor{std::find_if(config.topics.begin(), config.topics.end(), [&](const auto &entry) {
                return entry.name == item->topic_name;
            })};
            if (descriptor->timestamp == recording::TimestampRule::Header) { require(item->time_stamp == 10000000000LL, "source timestamp changed"); }
        }
    }
    require(directories == 2 && seen.size() == expected.size(), "catalog bag readback missing typed payload");
}
void configuration() {
    rclcpp::Node node{"recording_config_test"};
    node.declare_parameter<std::string>("save_path", "."); recording::declareParameters(node);
    auto rejects{[&] {
        try { (void)recording::readConfig(node); } catch (const std::exception &) { return true; }
        return false;
    }};
    node.set_parameter(rclcpp::Parameter{"bag.topics.altitude.name", "/mavros/imu/data"});
    require(rejects(), "duplicate resolved names accepted");
    node.set_parameter(rclcpp::Parameter{"bag.topics.altitude.name", "/fpv/altitude"});
    node.set_parameter(rclcpp::Parameter{"bag.topics.failsafe.max_hz", 10.0});
    require(rejects(), "protected event sampling accepted");
    node.set_parameter(rclcpp::Parameter{"bag.topics.failsafe.max_hz", 0.0});
    node.set_parameter(rclcpp::Parameter{"bag.topics.gimbal.max_hz", -1.0});
    require(rejects(), "negative frequency accepted");
    node.set_parameter(rclcpp::Parameter{"bag.topics.gimbal.max_hz", 100.0});
    node.set_parameter(rclcpp::Parameter{"bag.topics.trigger.name", "/different_trigger"});
    require(rejects(), "direct input silently relabeled");
}
void incompatibleDelivery(const std::filesystem::path &root) {
    rclcpp::Node node{"recording_incompatible_test"};
    node.declare_parameter<std::string>("save_path", root.string()); recording::declareParameters(node);
    BagSession::Status result{};
    recording::RecordingManager manager{node, recording::readConfig(node), {}, [&](const auto &status) { result = status; }};
    manager.startExecutor();
    auto publisher{node.create_publisher<mavros_msgs::msg::State>("/mavros/state", rclcpp::QoS{1}.best_effort())};
    manager.open(1, 10000000000);
    const auto handle{manager.capture()};
    until([&] { return handle->bag->snapshot().topics.at("/mavros/state").incompatible_qos > 0; },
        "supported incompatible-delivery event was hidden");
    manager.close(); manager.stopExecutor();
    require(result.phase == "incomplete", "incompatible delivery claimed a complete session");
}

void policyAdmission(const std::filesystem::path &root) {
    rclcpp::Node node{"recording_policy_admission_test"};
    node.declare_parameter<std::string>("save_path", root.string());
    recording::declareParameters(node);
    auto config{recording::readConfig(node)};
    config.observations_max_hz = 1e-300;
    for (auto &entry : config.topics) {
        if (entry.id == "body_imu" || entry.id == "tracker_image") { entry.max_hz = 0; }
        if (entry.id == "tracker_image") { entry.max_messages = 1; }
    }
    auto image{std::make_shared<sensor_msgs::msg::Image>(message<sensor_msgs::msg::Image>())};
    config.image_burst_bytes = recording::retainedPayload(*image);
    config.image_bytes_per_second = 1;
    auto descriptor{[&](const std::string &id) -> const recording::TopicDescriptor & {
        for (const auto &entry : config.topics) { if (entry.id == id) { return entry; } }
        throw std::logic_error("missing test catalog entry");
    }};
    std::mutex mutex{};
    std::condition_variable changed{};
    bool opened{}, released{};
    BagSession::Status result{};
    recording::RecordingManager manager{node, config, [&](const char *stage) {
        if (std::string{stage} != "open") { return; }
        std::unique_lock<std::mutex> lock{mutex};
        opened = true;
        changed.notify_all();
        changed.wait(lock, [&] { return released; });
    }, [&](const auto &status) { result = status; }};
    struct Release {
        std::function<void()> action{};
        ~Release() { action(); }
    } release{[&] {
        { std::lock_guard<std::mutex> lock{mutex}; released = true; }
        changed.notify_all();
    }};
    manager.open(1, 9000000000LL);
    const auto handle{manager.capture()};
    {
        std::unique_lock<std::mutex> lock{mutex};
        require(changed.wait_for(lock, 2s, [&] { return opened; }), "policy writer did not reach gate");
    }
    auto bbox{std::make_shared<vision_msgs::msg::Detection2D>(message<vision_msgs::msg::Detection2D>())};
    auto los{std::make_shared<geometry_msgs::msg::TwistStamped>(message<geometry_msgs::msg::TwistStamped>())};
    manager.submitObservationPair(1, bbox, los, false);
    manager.submitObservationPair(1, bbox, los, false);
    auto invalid_bbox{std::make_shared<vision_msgs::msg::Detection2D>()};
    auto invalid_los{std::make_shared<geometry_msgs::msg::TwistStamped>()};
    invalid_bbox->header.stamp.sec = 12;
    invalid_los->header.stamp = invalid_bbox->header.stamp;
    manager.submitObservationPair(1, invalid_bbox, invalid_los, true);
    for (int index{}; index < 3; ++index) {
        auto imu{std::make_shared<sensor_msgs::msg::Imu>()};
        imu->header.frame_id = std::to_string(index);
        if (index == 1) { imu->header.stamp.sec = -1; }
        if (index == 2) { imu->header.stamp.sec = 3; imu->header.stamp.nanosec = 1000000000; }
        manager.submit(handle, descriptor("body_imu"), imu, (20LL + index) * 1000000000, index);
        auto event{std::make_shared<std_msgs::msg::Bool>()};
        event->data = index == 1;
        manager.submit(handle, descriptor("failsafe"), event, index + 1, index * 1000000);
    }
    manager.submit(handle, descriptor("tracker_image"), image, 1, 0);
    manager.submit(handle, descriptor("tracker_image"), image, 2, 0);
    auto oversized{std::make_shared<sensor_msgs::msg::Image>(*image)};
    oversized->data.resize(2000);
    manager.submit(handle, descriptor("tracker_image"), oversized, 3, 0);
    // Refill, then force admission failure. Failed admission must not consume the allowance.
    manager.submit(handle, descriptor("tracker_image"), image, 4, 10000000000000LL);
    release.action();
    until([&] { return handle->bag->snapshot().topics.at(descriptor("tracker_image").name).written == 1; },
        "policy image did not finish writing");
    until([&] { return handle->bag->snapshot().queued == 0; }, "policy writer did not release retained budget");
    manager.submit(handle, descriptor("tracker_image"), image, 5, 10000000000000LL);
    manager.close();
    manager.stopExecutor();
    const auto &first{result.topics.at(descriptor("target_bbox").name)};
    const auto &second{result.topics.at(descriptor("target_los").name)};
    require(first.accepted == 2 && second.accepted == 2 && first.policy_skipped == 1
        && second.policy_skipped == 1 && first.written == second.written,
        "pair sampling or final OFF bypass was not joint");
    const auto &imu{result.topics.at(descriptor("body_imu").name)};
    require(imu.written == 3 && imu.timestamp_fallback == 3 && imu.fallback_reasons[1] == 1
        && imu.fallback_reasons[2] == 1 && imu.fallback_reasons[3] == 1,
        "fallback counters lost their reasons");
    const auto &images{result.topics.at(descriptor("tracker_image").name)};
    require(images.attempted == 5 && images.accepted == 2 && images.written == 2
        && images.byte_limited == 1 && images.oversized == 1 && images.dropped == 1,
        "image policy outcomes or admission-charged tokens were conflated");
    require(result.topics.at(descriptor("failsafe").name).written == 3,
        "brief protected transitions were sampled");
    std::size_t fallback_payloads{};
    for (const auto &directory : std::filesystem::directory_iterator{root}) {
        rosbag2_cpp::Reader reader{};
        reader.open(directory.path().string());
        while (reader.has_next()) {
            const auto item{reader.read_next()};
            if (item->topic_name != descriptor("body_imu").name) { continue; }
            rclcpp::SerializedMessage raw{*item->serialized_data};
            rclcpp::Serialization<sensor_msgs::msg::Imu> serializer{};
            sensor_msgs::msg::Imu decoded{};
            serializer.deserialize_message(&raw, &decoded);
            const int index{std::stoi(decoded.header.frame_id)};
            require(item->time_stamp == (20LL + index) * 1000000000,
                "invalid source stamp did not index with captured receipt time");
            require((index == 0 && decoded.header.stamp.sec == 0 && decoded.header.stamp.nanosec == 0)
                || (index == 1 && decoded.header.stamp.sec == -1)
                || (index == 2 && decoded.header.stamp.nanosec == 1000000000),
                "timestamp fallback rewrote the original payload");
            ++fallback_payloads;
        }
    }
    require(fallback_payloads == 3 && result.phase == "incomplete", "unexpected image gaps were hidden");
}
} // namespace
int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    const auto root{std::filesystem::temp_directory_path() / ("recording-transport-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()))};
    try {
        configuration(); transport(root); incompatibleDelivery(root / "incompatible");
        policyAdmission(root / "policy");
        std::filesystem::remove_all(root);
    }
    catch (const std::exception &error) { std::cerr << error.what() << " evidence=" << root << '\n';
    rclcpp::shutdown(); return 1; }
    rclcpp::shutdown(); return 0;
}

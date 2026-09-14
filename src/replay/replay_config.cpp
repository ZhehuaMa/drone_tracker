#include "replay_config.h"

#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace replay {
namespace {
void fail(const std::string &field, const std::string &reason) { throw std::invalid_argument(field + ": " + reason); }
// OpenCV 的整数解析可能先收窄为 int；必须在进入 FileStorage 前检查原始整数。
void checkIntegerTokens(const std::string &text) {
    std::string token{};
    std::size_t line_number{1}, column{}, token_line{1}, token_column{1};
    const auto check{[&token](std::size_t source_line, std::size_t source_column) {
        if (token.empty()) {
            return;
        }
        if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0 || token.rfind("-0x", 0) == 0 ||
            token.rfind("+0x", 0) == 0) {
            fail("configuration line " + std::to_string(source_line) + ":" + std::to_string(source_column) + " integer " +
                     token,
                 "use decimal integer notation");
        }
        const std::size_t start{token[0] == '-' || token[0] == '+' ? 1U : 0U};
        if (start < token.size() && token.find_first_not_of("0123456789", start) == std::string::npos) {
            long long value{};
            const char *begin{token.data() + (token[0] == '+' ? 1 : 0)};
            const auto result{std::from_chars(begin, token.data() + token.size(), value)};
            if (result.ec != std::errc{} || value < std::numeric_limits<int>::min() ||
                value > std::numeric_limits<int>::max()) {
                fail("configuration line " + std::to_string(source_line) + ":" + std::to_string(source_column) +
                         " integer " + token,
                     "outside signed 32-bit range");
            }
        }
        token.clear();
    }};
    char quote{};
    bool escaped{false};
    bool comment{false};
    for (char ch : text) {
        if (ch == '\n') {
            ++line_number;
            column = 0;
        } else {
            ++column;
        }
        if (comment) {
            if (ch == '\n') {
                comment = false;
            }
            continue;
        }
        if (quote) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\' && quote == '"') {
                escaped = true;
            } else if (ch == quote) {
                quote = 0;
            }
            continue;
        }
        if (ch == '#' || ch == '\'' || ch == '"') {
            check(token_line, token_column);
            if (ch == '#') {
                comment = true;
            } else {
                quote = ch;
            }
        } else if (std::isspace(static_cast<unsigned char>(ch)) ||
                   std::string{":,{}[]"}.find(ch) != std::string::npos) {
            check(token_line, token_column);
        } else {
            if (token.empty()) {
                token_line = line_number;
                token_column = column;
            }
            token += ch;
        }
    }
    check(token_line, token_column);
}
class Map {
public:
    Map(cv::FileNode node, std::string field) : node_{node}, field_{std::move(field)} {
        if (!node_.isMap()) {
            fail(field_, "expected mapping");
        }
        for (auto it{node_.begin()}; it != node_.end(); ++it) {
            const auto key{(*it).name()};
            if (!keys_.insert(key).second) {
                fail(field_ + "." + key, "duplicate key");
            }
        }
    }
    cv::FileNode take(const std::string &key) {
        if (keys_.erase(key) != 1) {
            fail(field_ + "." + key, "required field missing");
        }
        return node_[key];
    }
    double number(const std::string &key, double minimum, double maximum, bool strict = false) {
        const auto node{take(key)};
        if (!node.isInt() && !node.isReal()) {
            fail(field_ + "." + key, "expected number");
        }
        const double value{static_cast<double>(node)};
        if (!std::isfinite(value) || value < minimum || value > maximum || (strict && value == minimum)) {
            fail(field_ + "." + key, "non-finite or outside permitted range");
        }
        return value;
    }
    int integer(const std::string &key, int minimum, int maximum = std::numeric_limits<int>::max()) {
        const auto node{take(key)};
        if (!node.isInt()) {
            fail(field_ + "." + key, "expected integer (boolean uses 0/1)");
        }
        const int value{static_cast<int>(node)};
        if (value < minimum || value > maximum) {
            fail(field_ + "." + key, "integer outside permitted range");
        }
        return value;
    }
    std::string string(const std::string &key) {
        const auto node{take(key)};
        if (!node.isString() || node.string().empty()) {
            fail(field_ + "." + key, "expected nonempty path string");
        }
        if (node.string().find('\0') != std::string::npos) {
            fail(field_ + "." + key, "NUL in path");
        }
        return node.string();
    }
    void finish() const {
        if (!keys_.empty()) {
            fail(field_ + "." + *keys_.begin(), "unknown field");
        }
    }

private:
    cv::FileNode node_;
    std::string field_;
    std::set<std::string> keys_{};
};
} // namespace
ReplayConfig readConfig(const std::filesystem::path &path) {
    ReplayConfig c{};
    c.file = std::filesystem::absolute(path).lexically_normal();
    std::ifstream input{c.file};
    if (!input) {
        fail(c.file.string(), "cannot read configuration");
    }
    std::ostringstream text{};
    text << input.rdbuf();
    if (input.bad()) {
        fail(c.file.string(), "configuration read failed");
    }
    c.source = text.str();
    checkIntegerTokens(c.source);
    // 包括空的后续文档；FileStorage::root(1).empty() 无法区分它与不存在。
    std::istringstream lines{c.source};
    std::string line{};
    bool content_seen{false};
    bool document_seen{false};
    while (std::getline(lines, line)) {
        const auto first{line.find_first_not_of(" \t\r")};
        if (first == std::string::npos || line[first] == '#' || line[first] == '%') {
            continue;
        }
        if (line.compare(first, 3, "---") == 0) {
            if (content_seen || document_seen) {
                fail(c.file.string(), "only one YAML document permitted");
            }
            document_seen = true;
        } else {
            content_seen = true;
        }
    }
    try {
        cv::FileStorage fs{c.source, cv::FileStorage::READ | cv::FileStorage::MEMORY | cv::FileStorage::FORMAT_YAML};
        if (!fs.isOpened()) {
            fail(c.file.string(), "cannot parse YAML");
        }
        if (!fs.root(1).empty()) {
            fail(c.file.string(), "only one YAML document permitted");
        }
        Map root{fs.root(), "configuration"};
        if (root.integer("schema_version", 1, 1) != 1) {
            fail("schema_version", "expected 1");
        }
        const auto resolve{[&](const std::string &value) {
            return (c.file.parent_path() / std::filesystem::path{value}).lexically_normal();
        }};
        Map models{root.take("models"), "models"};
        c.models.yolo = resolve(models.string("yolo")).string();
        c.models.template_backbone = resolve(models.string("template_backbone")).string();
        c.models.search_backbone = resolve(models.string("search_backbone")).string();
        c.models.head = resolve(models.string("head")).string();
        models.finish();
        Map in{root.take("input"), "input"};
        c.video = resolve(in.string("video"));
        c.fps = in.number("fps", 0, 1000000, true);
        c.max_frames = in.integer("max_frames", 0);
        in.finish();
        constexpr double largest{std::numeric_limits<double>::max()};
        constexpr double largest_float{std::numeric_limits<float>::max()};
        Map camera{root.take("camera"), "camera"};
        auto &t{c.tracker};
        t.kf_fx = camera.number("fx", 0, largest, true);
        t.kf_fy = camera.number("fy", 0, largest, true);
        t.kf_cx0 = camera.number("cx", -largest, largest);
        t.kf_cy0 = camera.number("cy", -largest, largest);
        camera.finish();
        Map tracker{root.take("tracker"), "tracker"};
        t.target_class_id = tracker.integer("yolo_target_class_id", 0);
        const auto real{[&](const char *key, double low, double high, bool strict = false) {
            const double value{tracker.number(key, low, high, strict)};
            const float result{static_cast<float>(value)};
            if (strict && low == 0 && result == 0) {
                fail(key, "underflows Float32");
            }
            return result;
        }};
        t.yolo_conf_threshold = real("yolo_conf_threshold", 0, 1);
        t.yolo_nms_threshold = real("yolo_nms_threshold", 0, 1);
        t.yolo_interval = tracker.integer("yolo_interval", 1);
        t.yolo_recall_relax_ratio = real("yolo_recall_relax_ratio", 0, 1, true);
        t.iou_reset_threshold = real("iou_reset_threshold", 0, 1);
        t.iou_calibrate_threshold = real("iou_calibrate_threshold", 0, 1);
        t.max_position_jump = real("max_position_jump", 0, largest_float, true);
        t.max_size_jump_ratio = real("max_size_jump_ratio", 1, largest_float);
        t.max_box_area_ratio = real("max_box_area_ratio", 0, 1, true);
        t.roi_scale_factor = real("tracking_roi_scale_factor", 1, largest_float);
        t.lost_max_count = tracker.integer("lost_max_count", 1);
        t.frame_buffer_size = tracker.integer("frame_buffer_size", 1);
        t.jump_tolerance = tracker.integer("jump_tolerance", 1);
        t.enable_dcf = tracker.integer("enable_dcf", 0, 1) != 0;
        t.dcf.fixed_template = tracker.integer("dcf_fixed_template", 0, 1) != 0;
        t.dcf.learning_rate = real("dcf_learning_rate", 0, 1);
        t.dcf.lambda_reg = real("dcf_lambda", 0, largest_float, true);
        t.dcf.psr_thresh = real("dcf_psr_thresh", 0, largest_float);
        t.dcf_max_div_factor = real("dcf_max_div_factor", 0, largest_float, true);
        t.dcf.model_sz = tracker.integer("dcf_model_sz", 2);
        t.dcf.pad = real("dcf_pad", 0, largest_float);
        t.dcf.sigma_factor = real("dcf_sigma_factor", 0, largest_float, true);
        t.dcf.eps = real("dcf_eps", 0, largest_float, true);
        t.kf_r_pos = tracker.number("kf_r_pos", 0, largest, true);
        t.kf_r_size = tracker.number("kf_r_size", 0, largest, true);
        t.kf_q_a = tracker.number("kf_q_a", 0, largest);
        t.kf_q_size = tracker.number("kf_q_size", 0, largest);
        t.kf_history_len = tracker.integer("kf_history_len", 1);
        tracker.finish();
        Map out{root.take("output"), "output"};
        c.directory = resolve(out.string("directory"));
        out.finish();
        root.finish();
        if (std::filesystem::exists(c.directory) || std::filesystem::is_symlink(c.directory)) {
            fail("output.directory", "already exists");
        }
        // 旧字段不参与模型加载，消除默认路径造成的第二配置来源。
        t.yolo_engine_path.clear();
        t.nanotrack_backbone_path.clear();
        t.nanotrack_head_path.clear();
        return c;
    } catch (const cv::Exception &error) {
        fail(c.file.string(), error.what());
    }
    throw std::invalid_argument("Unreachable configuration parser state");
}
void writeConfig(cv::FileStorage &out, const ReplayConfig &c) {
    const auto &t{c.tracker};
    out << "schema_version" << 1 << "file" << c.file.string();
    out << "models" << "{" << "yolo" << c.models.yolo << "template_backbone" << c.models.template_backbone
        << "search_backbone" << c.models.search_backbone << "head" << c.models.head << "}";
    out << "input" << "{" << "video" << c.video.string() << "fps" << c.fps << "max_frames" << c.max_frames << "}";
    out << "camera" << "{" << "fx" << t.kf_fx << "fy" << t.kf_fy << "cx" << t.kf_cx0 << "cy" << t.kf_cy0 << "}";
    out << "tracker" << "{" << "yolo_target_class_id" << t.target_class_id;
    out << "yolo_conf_threshold" << t.yolo_conf_threshold;
    out << "yolo_nms_threshold" << t.yolo_nms_threshold;
    out << "yolo_interval" << t.yolo_interval;
    out << "yolo_recall_relax_ratio" << t.yolo_recall_relax_ratio;
    out << "iou_reset_threshold" << t.iou_reset_threshold;
    out << "iou_calibrate_threshold" << t.iou_calibrate_threshold;
    out << "max_position_jump" << t.max_position_jump;
    out << "max_size_jump_ratio" << t.max_size_jump_ratio;
    out << "max_box_area_ratio" << t.max_box_area_ratio;
    out << "tracking_roi_scale_factor" << t.roi_scale_factor;
    out << "lost_max_count" << t.lost_max_count;
    out << "frame_buffer_size" << t.frame_buffer_size;
    out << "jump_tolerance" << t.jump_tolerance;
    out << "enable_dcf" << static_cast<int>(t.enable_dcf);
    out << "dcf_fixed_template" << static_cast<int>(t.dcf.fixed_template);
    out << "dcf_learning_rate" << t.dcf.learning_rate;
    out << "dcf_lambda" << t.dcf.lambda_reg;
    out << "dcf_psr_thresh" << t.dcf.psr_thresh;
    out << "dcf_max_div_factor" << t.dcf_max_div_factor;
    out << "dcf_model_sz" << t.dcf.model_sz;
    out << "dcf_pad" << t.dcf.pad;
    out << "dcf_sigma_factor" << t.dcf.sigma_factor;
    out << "dcf_eps" << t.dcf.eps;
    out << "kf_r_pos" << t.kf_r_pos;
    out << "kf_r_size" << t.kf_r_size;
    out << "kf_q_a" << t.kf_q_a;
    out << "kf_q_size" << t.kf_q_size;
    out << "kf_history_len" << t.kf_history_len;
    out << "}" << "output" << "{" << "directory" << c.directory.string() << "}";
}
ReplayConfig commandConfig(int argc, const char *const *argv) {
    if (argc != 2) {
        throw std::invalid_argument("Usage: rk3576_tracker_replay CONFIG.yaml (exactly one configuration)");
    }
    return readConfig(argv[1]);
}
} // namespace replay

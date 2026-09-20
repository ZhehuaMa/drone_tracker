#include "model_preparation.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace baseline {
void verifyPrecision(const Config &config, ResultWriter &writer, bool floating_interfaces) {
    writer.identity["precision_status"] = "precision_unverified";
    writer.identity["precision_evidence_sha256"] = sha256(config.evidence);
    std::filesystem::copy_file(config.evidence, config.output / "precision_evidence.yaml");
    if (!floating_interfaces) {
        throw std::runtime_error("precision_unverified: 模型含整数接口");
    }
    cv::FileStorage evidence(config.evidence.string(), cv::FileStorage::READ);
    if (!evidence.isOpened() || int(evidence["schema_version"]) != 1) {
        throw std::runtime_error("precision_unverified: 证据格式非法");
    }
    const auto models{evidence["models"]};
    if (!models.isSeq() || models.size() != 3) {
        throw std::runtime_error("precision_unverified: 需要三份模型证据");
    }
    const std::array<std::string, 3> roles{"template_backbone", "search_backbone", "head"};
    std::set<std::string> seen;
    for (const auto &entry : models) {
        const std::string role{entry["role"].string()};
        const auto match{std::find(roles.begin(), roles.end(), role)};
        if (match == roles.end() || !seen.insert(role).second) {
            throw std::runtime_error("precision_unverified: 角色非法");
        }
        const auto index{static_cast<std::size_t>(match - roles.begin())};
        const auto hash{sha256(config.models[index])};
        writer.identity[role + "_sha256"] = hash;
        if (entry["sha256"].string() != hash || entry["precision"].string() != "FP16") {
            throw std::runtime_error("precision_unverified: 模型指纹不符或非纯 FP16");
        }
        std::filesystem::path record{entry["export_record"].string()};
        if (record.empty()) {
            throw std::runtime_error("precision_unverified: 导出记录缺失");
        }
        if (record.is_relative()) {
            record = config.evidence.parent_path() / record;
        }
        const auto record_hash{sha256(record)};
        if (entry["export_record_sha256"].string() != record_hash) {
            throw std::runtime_error("precision_unverified: 导出记录指纹不符");
        }
        cv::FileStorage exported(record.string(), cv::FileStorage::READ);
        if (!exported.isOpened() || exported["model_sha256"].string() != hash ||
            exported["precision"].string() != "FP16" || exported["role"].string() != role ||
            exported["status"].string() != "success" || exported["source_record"].string().empty()) {
            throw std::runtime_error("precision_unverified: 导出记录与清单矛盾或缺少来源");
        }
        std::filesystem::path source{exported["source_record"].string()};
        if (source.is_relative()) {
            source = record.parent_path() / source;
        }
        if (sha256(source) != exported["source_record_sha256"].string()) {
            throw std::runtime_error("precision_unverified: 原始导出证据指纹不符");
        }
        std::filesystem::copy_file(record, config.output / (role + "_export.yaml"));
        std::filesystem::copy_file(source, config.output / (role + "_source_record.txt"));
        writer.identity[role + "_export_sha256"] = record_hash;
    }
    writer.identity["precision_status"] = "verified_export_evidence";
}
} // namespace baseline

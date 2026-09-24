#include "storage.h"

#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using namespace rk3576;
using namespace rk3576::recording;
void require(bool value, const char *reason) { if (!value) { throw std::runtime_error(reason); } }
std::string fileName(int fd) {
    std::array<char, 4096> text{};
    const auto count{::readlink(("/proc/self/fd/" + std::to_string(fd)).c_str(), text.data(), text.size() - 1)};
    return count > 0 ? std::filesystem::path{std::string{text.data(), static_cast<std::size_t>(count)}}.filename().string() : "?";
}
struct FaultIo final : FileIo {
    std::string mode{};
    bool armed{}, interrupted{};
    std::vector<std::string> trace{};
    ssize_t write(int fd, const void *data, std::size_t size) override {
        trace.push_back("write:" + fileName(fd));
        if (armed && mode == "eintr" && !interrupted) { interrupted = true; errno = EINTR; return -1; }
        if (armed && mode == "eio") { errno = EIO; return -1; }
        if (armed && mode == "enospc") { errno = ENOSPC; return -1; }
        if (armed && mode == "zero") { errno = EIO; return 0; }
        return FileIo::write(fd, data, armed && mode == "short" ? std::min<std::size_t>(17, size) : size);
    }
    int dataSync(int fd) override {
        const auto name{fileName(fd)};
        trace.push_back("sync:" + name);
        if (armed && ((mode == "video_sync" && name.find(".ts") != std::string::npos)
            || (mode == "frames_sync" && name.find(".frames.jsonl") != std::string::npos)
            || (mode == "commit_sync" && name == "commits.jsonl"))) { errno = EIO; return -1; }
        return FileIo::dataSync(fd);
    }
    int directorySync(int fd) override {
        trace.push_back("dir:" + fileName(fd));
        if (armed && mode == "dir_sync") { errno = EIO; return -1; }
        return FileIo::directorySync(fd);
    }
    int availableBytes(int fd, std::uint64_t &bytes) override {
        if (armed && mode == "free_space") { bytes = 0; return 0; }
        return FileIo::availableBytes(fd, bytes);
    }
};
EncodedFrame fixture(std::int64_t pts) {
    EncodedFrame frame{};
    frame.identity.round = 1; frame.identity.sequence = static_cast<unsigned>(pts / 1000000);
    frame.identity.timestamp_us = 1000000 + pts;
    frame.pts_us = pts; frame.idr = true;
    // 仅用于封装/文件次序测试的 Annex-B 边界样本，不作为可解码画质证据。
    frame.bytes = {0,0,0,1,0x67,0x42,0xc0,0x32,0x8d,0x8d,0x50,0x3c,1,0x2e,0x4d,0x90,
                   0,0,3,0,0x10,0,0,7,0x88,0xf0,0x80,0x42,0xa0,0,0,0,1,0x68,0xce,0x31,0xb2,
                   0,0,0,1,0x65,0x88,0x84,0x21,0xa0};
    return frame;
}
void tickAfterInterval(Storage &storage) {
    std::this_thread::sleep_for(std::chrono::milliseconds{510}); storage.tick();
}
CameraRecordingConfig config(const std::filesystem::path &root) {
    CameraRecordingConfig out{}; out.enabled = true; out.directory = root.string(); out.sync_interval_ms = 500;
    out.min_free_bytes = 1; out.max_total_bytes = 32 * 1024 * 1024; out.segment_seconds = 10;
    return out;
}
void successful(const std::filesystem::path &root, const std::string &mode) {
    RecordingSlots slots{1920, 1200, 1};
    auto io{std::make_shared<FaultIo>()};
    Storage storage{config(root / mode), {1920, 1200}, 1, "{\"test\":true}", slots.header(), io};
    io->mode = mode; io->armed = true;
    storage.writeFrame(fixture(0));
    tickAfterInterval(storage);
    require(slots.header().synced.load() == 1, "First sync watermark missing");
    io->trace.clear();
    storage.writeFrame(fixture(1000000));
    tickAfterInterval(storage);
    const auto &trace{io->trace};
    auto position{[&](const std::string &name) {
        const auto found{std::find(trace.begin(), trace.end(), name)};
        require(found != trace.end(), "Missing sync trace operation"); return found - trace.begin();
    }};
    require(position("sync:000001.ts") < position("write:000001.frames.jsonl")
        && position("sync:000001.frames.jsonl") < position("write:commits.jsonl")
        && position("write:commits.jsonl") < position("sync:commits.jsonl"), "Commit preceded video or time records");
    storage.writeFrame(fixture(10000000));
    storage.finish("{\"failed\":false}");
    require(slots.header().synced.load() == 3, "Final sync frame count mismatch");
    require(std::filesystem::exists(storage.directory() + "/000002.ts"), "IDR segment not created");
    require(std::filesystem::exists(storage.directory() + "/complete.json"), "Normal completion missing");
}
void failure(const std::filesystem::path &root, const std::string &mode) {
    RecordingSlots slots{1920, 1200, 1};
    auto io{std::make_shared<FaultIo>()};
    Storage storage{config(root / mode), {1920, 1200}, 1, "{}", slots.header(), io};
    storage.writeFrame(fixture(0)); tickAfterInterval(storage);
    require(slots.header().synced.load() == 1, "Fault test baseline not committed");
    io->mode = mode; io->armed = true;
    bool rejected{};
    try { storage.writeFrame(fixture(mode == "dir_sync" ? 10000000 : 1000000)); tickAfterInterval(storage); }
    catch (const std::exception &) { rejected = true; }
    require(rejected, "Injected storage fault ignored");
    require(slots.header().synced.load() == 1, "Failure advanced sync watermark");
    require(!std::filesystem::exists(storage.directory() + "/complete.json"), "Failed session marked complete");
}
void zeroFrames(const std::filesystem::path &root) {
    for (const bool failed : {false, true}) {
        RecordingSlots slots{4, 2, 1};
        Storage storage{config(root / (failed ? "warmup-failed" : "cancelled")), {4, 2}, 1, "{}", slots.header()};
        storage.finish(failed ? "{\"failed\":true}" : "{\"failed\":false}");
        require(!std::filesystem::exists(storage.directory() + "/000001.ts"), "Zero-frame session created video");
        require(slots.header().synced.load() == 0, "Zero-frame sync became known");
    }
}
void evidenceProtection(const std::filesystem::path &root) {
    {
        RecordingSlots slots{4, 2, 1};
        Storage storage{config(root / "collision"), {4, 2}, 1, "{}", slots.header()};
        const auto existing{std::filesystem::path{storage.directory()} / "000001.ts"};
        { std::ofstream file{existing}; file << "existing-evidence"; }
        bool rejected{};
        try { storage.writeFrame(fixture(0)); } catch (const std::exception &) { rejected = true; }
        std::ifstream file{existing}; std::string text{}; file >> text;
        require(rejected && text == "existing-evidence", "File collision overwrote original evidence");
    }
    for (const bool free_space : {false, true}) {
        RecordingSlots slots{4, 2, 1};
        auto limits{config(root / (free_space ? "free-space-limit" : "quota-limit"))};
        if (free_space) { limits.min_free_bytes = UINT64_MAX; }
        else { limits.max_total_bytes = 1; }
        bool rejected{};
        try { Storage storage{limits, {4, 2}, 1, "{}", slots.header()}; }
        catch (const std::exception &) { rejected = true; }
        require(rejected && slots.header().synced.load() == 0, "Startup space protection failed");
    }
    {
        RecordingSlots slots{4, 2, 1};
        auto limits{config(root / "external-usage")}; limits.max_total_bytes = 1024 * 1024;
        Storage storage{limits, {4, 2}, 1, "{}", slots.header()};
        const auto external{std::filesystem::path{limits.directory} / "external-evidence"};
        { std::ofstream file{external}; file << "retain"; }
        // 稀疏测试文件仅改变逻辑配额，不写满生产盘。
        std::filesystem::resize_file(external, 2 * 1024 * 1024);
        std::this_thread::sleep_for(std::chrono::milliseconds{1010});
        bool rejected{};
        try { storage.writeFrame(fixture(0)); } catch (const std::exception &) { rejected = true; }
        require(rejected && std::filesystem::file_size(external) == 2 * 1024 * 1024,
            "External quota growth ignored or historical file removed");
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2) { throw std::invalid_argument("storage_test requires a fresh ext4 test directory"); }
        const std::filesystem::path root{argv[1]};
        require(std::filesystem::create_directory(root), "Test root must be new; history is not removed");
        for (const std::string mode : {"normal", "short", "eintr"}) { successful(root, mode); }
        for (const std::string mode : {"eio", "enospc", "zero", "video_sync", "frames_sync", "commit_sync",
                                      "dir_sync", "free_space"}) {
            failure(root, mode);
        }
        zeroFrames(root);
        evidenceProtection(root);
        std::cout << "Storage ordering, short write, EINTR, EIO, ENOSPC, sync failure and zero-frame tests passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}

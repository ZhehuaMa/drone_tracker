#ifndef RK3576_RECORDING_STORAGE_H
#define RK3576_RECORDING_STORAGE_H

#include <memory>
#include <string>
#include <sys/types.h>
#include "encoder.h"

namespace rk3576::recording {
/** @brief 写入/同步可注入边界；测试以 errno 模拟磁盘错误，不制造真实故障。 */
class FileIo {
public:
    virtual ~FileIo() = default;
    virtual ssize_t write(int fd, const void *data, std::size_t size);
    virtual int dataSync(int fd);
    virtual int directorySync(int fd);
    virtual int availableBytes(int fd, std::uint64_t &bytes);
};
/** @brief 单一存储线程拥有所有文件和封装状态，不在析构时伪造完成标记。 */
class Storage final {
public:
    Storage(CameraRecordingConfig config, CameraFormat format, std::uint64_t round,
            std::string manifest, RecordingSlots::Header &status, std::shared_ptr<FileIo> io = {});
    ~Storage();
    void writeFrame(const EncodedFrame &frame);
    void writeEvent(const RecordingSlots::Event &event);
    void tick();
    void finish(const std::string &camera_summary);
    const std::string &directory() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rk3576::recording
#endif // RK3576_RECORDING_STORAGE_H

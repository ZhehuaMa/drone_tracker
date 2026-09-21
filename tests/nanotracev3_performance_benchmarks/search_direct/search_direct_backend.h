#ifndef NANOTRACK_SEARCH_DIRECT_BACKEND_H
#define NANOTRACK_SEARCH_DIRECT_BACKEND_H

#include <map>
#include <memory>
#include <string>

#include "direct_session.h"
#include "model/model_backend.h"
#include "platform/rk3576/rknn_nanotrack_factory.h"

namespace search_direct {
/**
 * @brief 仅用于性能对照的 NanoTrackV3 后端：搜索特征通过共享设备内存交给 head。
 *
 * 与生产后端的唯一差别是搜索特征的交接方式。模板特征仍然取回主机、每帧普通提交；
 * head 的两项输出仍按普通接口取回为 Float32。搜索特征不经过主机读取、复制或转换。
 *
 * 调用顺序固定为：提交搜索图像 → 绑定搜索输出 → 执行搜索 → 提交模板特征 →
 * 绑定 head 搜索输入 → 执行 head → 取回 head 输出。每次普通提交之后都重新绑定，
 * 因为实机验证表明旧绑定不会跨普通提交保持有效。
 */
class SearchDirectBackend final : public model::NanoTrackBackend {
public:
    static model::Result<std::unique_ptr<SearchDirectBackend>> load(const std::string &template_path,
                                                                    const std::string &search_path,
                                                                    const std::string &head_path,
                                                                    SharedMemoryApi api);
    SearchDirectBackend(std::unique_ptr<DirectSession> template_session, std::unique_ptr<DirectSession> search_session,
                        std::unique_ptr<DirectSession> head_session, rk3576::NanoTrackRoles roles,
                        SharedFeatureContract contract);
    ~SearchDirectBackend() noexcept override;
    SearchDirectBackend(const SearchDirectBackend &) = delete;
    SearchDirectBackend &operator=(const SearchDirectBackend &) = delete;

    model::Status setTemplate(const PreparedInputRef &input) override;
    model::Result<model::NanoPrediction> search(const PreparedInputRef &input) override;
    void reset() noexcept override;

    std::string describe() const;
    bool floatingInterfaces() const noexcept;
    /** @brief 本次执行路径的身份字段，写入运行记录用于追溯与对照。 */
    const std::map<std::string, std::string> &identity() const noexcept { return identity_; }
    /** @brief 仅供受控测试在消费者完成后读取共享特征做诊断；正式计时运行不调用。 */
    model::Result<std::vector<float>> readSharedFeatureForDiagnostics();

private:
    bool ready() const noexcept;
    model::Status runTemplateBackbone(const PreparedInputRef &input, model::Tensor &feature);
    void releaseShared() noexcept;

    std::unique_ptr<DirectSession> template_session_;
    std::unique_ptr<DirectSession> search_session_;
    std::unique_ptr<DirectSession> head_session_;
    rk3576::NanoTrackRoles roles_{};
    SharedFeatureContract contract_{};
    rknn_tensor_mem *shared_{};
    rknn_tensor_mem *imported_{};
    model::Tensor template_feature_{};
    std::map<std::string, std::string> identity_{};
};

/** @brief 组装本测试程序使用的组件，注入共用运行器；不改变生产工厂的装配路径。 */
model::Result<rk3576::NanoTrackComponents> loadSearchDirect(const std::string &template_path,
                                                            const std::string &search_path,
                                                            const std::string &head_path, SharedMemoryApi api,
                                                            std::map<std::string, std::string> *identity = nullptr);
model::Result<rk3576::NanoTrackComponents> loadSearchDirect(const std::string &template_path,
                                                            const std::string &search_path,
                                                            const std::string &head_path,
                                                            std::map<std::string, std::string> *identity = nullptr);
} // namespace search_direct
#endif // NANOTRACK_SEARCH_DIRECT_BACKEND_H

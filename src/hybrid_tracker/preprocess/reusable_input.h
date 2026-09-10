#ifndef PREPROCESS_REUSABLE_INPUT_H
#define PREPROCESS_REUSABLE_INPUT_H

#include <memory>
#include <utility>

/**
 * @brief 内部缓存：只有缓存自身持有存储时才允许重新写入。
 *
 * 调用方必须串行调用 acquire；返回的共享引用是租约，可跨线程消费或释放。
 * 缓存销毁不使已有租约失效。生产缓冲区和测试存储使用同一复用规则。
 */
template <typename Storage> class ReusableInput {
  public:
    /** @brief 借用工厂，首次申请时创建存储；占用时返回空引用而不调用工厂。 */
    template <typename Factory> std::shared_ptr<Storage> acquire(Factory &&factory) {
        if (storage_ && storage_.use_count() != 1) {
            return {};
        }
        if (!storage_) {
            storage_ = std::forward<Factory>(factory)();
        }
        return storage_;
    }

  private:
    std::shared_ptr<Storage> storage_{};
};

#endif // PREPROCESS_REUSABLE_INPUT_H

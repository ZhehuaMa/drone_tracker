#ifndef FRAME_VPI_FRAME_H
#define FRAME_VPI_FRAME_H

#include <cstdint>
#include <memory>
#include <mutex>

#include <opencv2/core/mat.hpp>
#include <vpi/VPI.h>

#include "frame/frame.h"

struct VpiFrameState;

/**
 * @brief VPI 原生图像的作用域只读租约。
 *
 * 租约保持底层帧资源有效，并与 FrameReadView 共用同一个逐帧访问互斥量。
 * 调用方向任意 VPI stream 提交涉及 image() 的操作后，必须在释放本租约前调用
 * vpiStreamSync() 完成同步。租约不转移 VPIImage 的所有权。
 */
class VpiFrameReadAccess final {
public:
  VpiFrameReadAccess(const VpiFrameReadAccess &) = delete;
  VpiFrameReadAccess &operator=(const VpiFrameReadAccess &) = delete;
  VpiFrameReadAccess(VpiFrameReadAccess &&) noexcept = default;
  VpiFrameReadAccess &operator=(VpiFrameReadAccess &&) noexcept = default;

  /** @brief 释放读取租约；调用方必须已同步所有使用该图像的 VPI stream。 */
  ~VpiFrameReadAccess() = default;

  /** @brief 返回租约范围内借用的 VPIImage；所有权仍属于 VpiFrame。 */
  VPIImage image() const noexcept;

private:
  friend class VpiFrame;

  VpiFrameReadAccess(std::shared_ptr<VpiFrameState> state,
                     std::unique_lock<std::mutex> access_lock,
                     VPIImage image) noexcept;

  std::shared_ptr<VpiFrameState> state_;
  std::unique_lock<std::mutex> access_lock_;
  VPIImage image_{nullptr};
};

/**
 * @brief Jetson VPI/CUDA 平台的 Frame 实现。
 *
 * 对象不可复制。共享所有权必须通过返回的 shared_ptr 表达，最后一个帧、主机
 * 视图或 VPI 读取租约释放后，底层 VPIImage 恰好销毁一次。
 */
class VpiFrame final : public Frame {
public:
  VpiFrame() = delete;
  VpiFrame(const VpiFrame &) = delete;
  VpiFrame &operator=(const VpiFrame &) = delete;
  VpiFrame(VpiFrame &&) = delete;
  VpiFrame &operator=(VpiFrame &&) = delete;

  /** @brief 释放本对象共享的 VPI 图像所有权。 */
  ~VpiFrame() override = default;

  /**
   * @brief 接管一个已有 VPIImage。
   * @param image 待接管图像；调用后无论成功或失败，调用方都不得再访问或销毁它。
   * @param timestamp_us 图像采集时间戳，单位为微秒。
   * @return 成功时返回共享拥有图像的帧；空句柄、查询失败或不支持格式时返回
   * nullptr。
   */
  static std::shared_ptr<const VpiFrame>
  takeOwnership(VPIImage image, std::uint64_t timestamp_us) noexcept;

  /**
   * @brief 从 OpenCV 图像复制创建 VPI 帧。
   * @param mat 被复制的输入图像；调用返回后调用方仍拥有原图。
   * @param timestamp_us 图像采集时间戳，单位为微秒。
   * @return 成功时返回独立共享帧；仅支持 CV_8UC1 和 CV_8UC3，失败返回 nullptr。
   */
  static std::shared_ptr<const VpiFrame>
  copyFrom(const cv::Mat &mat, std::uint64_t timestamp_us) noexcept;

  /** @brief 返回图像宽度；不转移 VPI 资源所有权。 */
  int width() const noexcept override;

  /** @brief 返回图像高度；不转移 VPI 资源所有权。 */
  int height() const noexcept override;

  /** @brief 返回与 VPI 图像对应的通用像素格式。 */
  FramePixelFormat pixelFormat() const noexcept override;

  /** @brief 返回采集时间戳，单位为微秒。 */
  std::uint64_t timestampUs() const noexcept override;

  /**
   * @brief 创建 VPI 主机只读映射。
   * @return 成功时返回持有 VPI 锁及帧资源的独占视图；失败返回 nullptr。
   */
  std::unique_ptr<FrameReadView> mapRead() const override;

  /**
   * @brief 获取 VPI 原生图像的作用域只读租约。
   *
   * 返回值共享底层所有权并持有逐帧互斥量。使用 image() 提交的所有 VPI stream
   * 操作必须在租约析构前同步完成。
   */
  VpiFrameReadAccess acquireVpiRead() const;

private:
  explicit VpiFrame(std::shared_ptr<VpiFrameState> state) noexcept;

  std::shared_ptr<VpiFrameState> state_;
};

using VpiFrameRef = std::shared_ptr<const VpiFrame>;

#endif // FRAME_VPI_FRAME_H

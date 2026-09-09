#ifndef FRAME_FRAME_H
#define FRAME_FRAME_H

#include <cstddef>
#include <cstdint>
#include <memory>

#include <opencv2/core/mat.hpp>

/**
 * @brief 通用帧支持的原生像素格式。
 *
 * 该枚举只描述当前已经验证的八位图像格式，不包含任何平台 SDK 类型。
 */
enum class FramePixelFormat : std::uint8_t { Gray8, Bgr8 };

/**
 * @brief CPU 可读帧数据的只读借用视图。
 *
 * 视图对象拥有底层平台映射或锁定的生命周期。调用方只能在本对象存活期间
 * 读取 mat() 返回的数据，不得保存该 cv::Mat 后让它逃逸到视图生命周期之外。
 */
class FrameReadView {
public:
  /** @brief 释放视图，并自动结束底层平台映射或锁定。 */
  virtual ~FrameReadView() = default;

  /** @brief 返回视图宽度；返回值是借用数据的元数据，不转移所有权。 */
  virtual int width() const noexcept = 0;

  /** @brief 返回视图高度；返回值是借用数据的元数据，不转移所有权。 */
  virtual int height() const noexcept = 0;

  /** @brief 返回视图原生像素格式；返回值不转移任何平台资源。 */
  virtual FramePixelFormat pixelFormat() const noexcept = 0;

  /** @brief 返回每行实际占用的字节数，包括可能存在的行填充。 */
  virtual std::size_t rowStrideBytes() const noexcept = 0;

  /**
   * @brief 返回只读 OpenCV 图像头。
   * @return 对视图内部 cv::Mat 的只读借用，仅在本视图存活期间有效。
   */
  virtual const cv::Mat &mat() const noexcept = 0;
};

/**
 * @brief 跨模块传递的只读帧契约。
 *
 * 帧对象拥有或共享底层平台资源。所有元数据均可只读访问，像素内容只能通过
 * mapRead() 返回的受生命周期保护视图读取。
 */
class Frame {
public:
  /** @brief 释放此帧引用持有的平台资源所有权。 */
  virtual ~Frame() = default;

  /** @brief 返回图像宽度；不转移帧或像素数据所有权。 */
  virtual int width() const noexcept = 0;

  /** @brief 返回图像高度；不转移帧或像素数据所有权。 */
  virtual int height() const noexcept = 0;

  /** @brief 返回帧的原生像素格式；不转移平台资源所有权。 */
  virtual FramePixelFormat pixelFormat() const noexcept = 0;

  /**
   * @brief 返回帧的采集时间戳，单位为微秒。
   *
   * 该值是采集时刻的唯一权威来源，与处理或实时外推时刻分开保存。
   */
  virtual std::uint64_t timestampUs() const noexcept = 0;

  /**
   * @brief 创建 CPU 可读的只读借用视图。
   * @return 成功时返回独占拥有映射生命周期的视图；映射失败时返回 nullptr，
   *         且不会暴露无效数据地址。
   */
  virtual std::unique_ptr<FrameReadView> mapRead() const = 0;
};

/**
 * @brief 共享只读帧引用。
 *
 * 队列或消费者复制该引用表示共享底层帧所有权；最后一个引用释放后平台资源
 * 才会被回收。const 限定阻止公共消费者修改已发布帧。
 */
using FrameRef = std::shared_ptr<const Frame>;

#endif // FRAME_FRAME_H

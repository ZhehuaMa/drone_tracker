#ifndef DETECTION_THREAD_H
#define DETECTION_THREAD_H

#include <functional>
#include <thread>
#include <utility>

/** @brief 检测线程启动能力；测试可注入启动异常，生产默认创建普通工作线程。 */
using DetectionThreadLauncher = std::function<std::thread(std::function<void()>)>;
/** @brief 启动任务并转移线程所有权；调用方负责停止并等待线程结束。 */
inline std::thread launchDetectionThread(std::function<void()> work) { return std::thread{std::move(work)}; }

#endif // DETECTION_THREAD_H

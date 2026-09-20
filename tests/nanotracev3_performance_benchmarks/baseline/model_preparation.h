#ifndef NANOTRACK_BASELINE_MODEL_PREPARATION_H
#define NANOTRACK_BASELINE_MODEL_PREPARATION_H
#include "result_writer.h"
namespace baseline {
/** @brief 检查清单与模型指纹、精度声明及导出记录的绑定；不重新转换模型。 */
void verifyPrecision(const Config &config, ResultWriter &writer, bool floating_interfaces);
} // namespace baseline
#endif // NANOTRACK_BASELINE_MODEL_PREPARATION_H

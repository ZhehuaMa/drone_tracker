#ifndef NANOTRACK_BASELINE_STATISTICS_H
#define NANOTRACK_BASELINE_STATISTICS_H
#include "result_writer.h"
#include <istream>
#include <map>
#include <vector>
namespace baseline {
using CsvRow = std::map<std::string, std::string>;
bool readCsvRecord(std::istream &input, std::vector<std::string> &row);
struct Distribution {
    std::size_t count{};
    double mean{}, standard_deviation{};
    std::int64_t minimum{}, maximum{}, p50{}, p90{}, p95{}, p99{};
};
Distribution distribution(std::vector<std::int64_t> samples);
/**
 * @brief 把落盘 JSON 转成 OpenCV FileStorage 能解析的等价文本。
 *
 * OpenCV 4.5 的 JSON 读取器不接受标准 `null`、`true`、`false` 字面量，也不支持
 * `\uXXXX` 转义。这里只在内存副本中替换它们：`null` 变成字符串 `"__null__"`，
 * 布尔变成 1/0，`\uXXXX` 变成可见文本 `<u+XXXX>`。其余字符串内容保持原样。
 */
std::string jsonForOpenCv(const std::string &raw);
/** @brief 只读取落盘原始记录生成报告，返回成功正式更新数。 */
std::size_t writeReports(const std::filesystem::path &directory, const std::string &status, const std::string &reason,
                         const std::string &variant = {}, const std::map<std::string, std::string> &identity = {});
} // namespace baseline
#endif // NANOTRACK_BASELINE_STATISTICS_H

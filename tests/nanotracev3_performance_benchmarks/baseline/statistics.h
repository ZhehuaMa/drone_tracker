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
/** @brief 只读取落盘原始记录生成报告，返回成功正式更新数。 */
std::size_t writeReports(const std::filesystem::path &directory, const std::string &status, const std::string &reason);
} // namespace baseline
#endif // NANOTRACK_BASELINE_STATISTICS_H

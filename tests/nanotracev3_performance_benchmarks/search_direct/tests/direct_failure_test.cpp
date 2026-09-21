#include <algorithm>
#include <cerrno>
#include <iostream>
#include <memory>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "direct_test_support.h"
#include "platform/rk3576/nanotrack_preprocessor.h"
#include "search_direct_backend.h"

namespace {
using namespace search_direct;
using namespace search_direct_test;

PreparedInputRef image(int size, float seed) {
    auto values{std::make_shared<std::vector<float>>(static_cast<std::size_t>(3 * size * size), seed)};
    ModelInputContract contract;
    contract.shape = {1, 3, size, size};
    contract.element_type = TensorElementType::Float32;
    contract.layout = TensorLayout::Nchw;
    contract.scale = 1.0f;
    contract.offset = 0.0f;
    return std::make_shared<const rk3576::FloatInput>(values, contract);
}

/** @brief 在一次独立的设备替身上装配后端；调用方负责在作用域内使用。 */
model::Result<std::unique_ptr<SearchDirectBackend>> build() {
    return SearchDirectBackend::load("z", "x", "head", fakeApi());
}

void checkAllocationFailures() {
    {
        auto fake{nanoTrackModels()};
        device = &fake;
        fake.models[1].create_mem_fails = true;
        require(!build(), "共享内存分配失败必须拒绝装配");
        require(fake.live_handles == 0, "分配失败后仍有内存句柄");
        device = nullptr;
    }
    {
        auto fake{nanoTrackModels()};
        device = &fake;
        fake.models[2].import_fails = true;
        require(!build(), "共享内存导入失败必须拒绝装配");
        require(fake.live_handles == 0, "导入失败后原始内存未释放");
        device = nullptr;
    }
    {
        auto fake{nanoTrackModels()};
        device = &fake;
        fake.models[1].native_query_status = -1;
        const auto failed{build()};
        require(!failed, "原生属性查询失败必须拒绝装配");
        require(fake.live_handles == 0, "属性查询失败后仍有内存句柄");
        device = nullptr;
    }
    std::cout << "分配、导入与属性查询失败处理通过\n";
}

void checkRuntimeFailures() {
    struct Scenario {
        const char *name;
        std::size_t model;
        int FakeModel::*field;
        bool head_should_run;
    };
    const std::vector<Scenario> scenarios{
        {"search_bind", 1, &FakeModel::bind_status, false}, {"search_run", 1, &FakeModel::run_status, false},
        {"head_bind", 2, &FakeModel::bind_status, false},   {"head_run", 2, &FakeModel::run_status, true},
        {"head_get", 2, &FakeModel::get_status, true},      {"head_release", 2, &FakeModel::release_status, true},
    };
    for (const auto &scenario : scenarios) {
        auto fake{nanoTrackModels()};
        device = &fake;
        {
            auto loaded{build()};
            require(static_cast<bool>(loaded), "后端装配失败");
            auto &backend{*loaded.value};
            require(static_cast<bool>(backend.setTemplate(image(127, 3.0f))), "模板建立失败");
            require(static_cast<bool>(backend.search(image(255, 11.0f))), "首次搜索应当成功");
            const int head_runs_before{fake.models[2].runs};
            fake.models[scenario.model].*scenario.field = -1;
            auto failed{backend.search(image(255, 22.0f))};
            require(!failed, std::string("注入失败后仍然返回成功: ") + scenario.name);
            require(!failed.value.classification.storage && !failed.value.location.storage,
                    std::string("失败结果携带了旧预测: ") + scenario.name);
            require(!failed.error.model.empty() && !failed.error.message.empty(),
                    std::string("失败诊断缺少模型身份或原因: ") + scenario.name);
            require((fake.models[2].runs > head_runs_before) == scenario.head_should_run,
                    std::string("失败后 head 执行与否不符合预期: ") + scenario.name);
            // 失效的后端不得继续执行，也不得返回任何预测。
            require(!backend.search(image(255, 33.0f)), std::string("失效后端仍可继续搜索: ") + scenario.name);
        }
        require(fake.live_handles == 0, std::string("失败路径泄漏内存句柄: ") + scenario.name);
        device = nullptr;
    }
    std::cout << "绑定、运行与输出失败处理通过\n";
}

/** @brief 在子进程中执行会触发致命诊断的场景，确认进程终止而不是继续使用设备存储。 */
void runFatalScenario(const std::string &name) {
    auto fake{nanoTrackModels()};
    device = &fake;
    auto loaded{build()};
    require(static_cast<bool>(loaded), "后端装配失败");
    auto &backend{*loaded.value};
    require(static_cast<bool>(backend.setTemplate(image(127, 3.0f))), "模板建立失败");
    if (name == "unconfirmed_stop") {
        // 运行失败且 wait 也无法确认设备停止：必须终止进程，不得继续释放存储。
        fake.models[1].run_status = -1;
        fake.models[1].wait_status = -1;
        (void)backend.search(image(255, 11.0f));
    } else if (name == "release_failure") {
        fake.models[1].destroy_mem_status = -1;
        loaded.value.reset();
    }
    device = nullptr;
    std::cerr << "致命场景 " << name << " 没有终止进程\n";
    _exit(0);
}

void checkFatalScenarios() {
    for (const std::string name : {"unconfirmed_stop", "release_failure"}) {
        const pid_t child{fork()};
        require(child >= 0, "无法启动子进程");
        if (child == 0) {
            runFatalScenario(name);
            _exit(0);
        }
        int status{};
        pid_t waited{};
        do {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        require(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE,
                "致命路径没有按诊断策略终止: " + name);
    }
    std::cout << "停止不确定与释放失败的致命路径通过\n";
}

void checkNoDoubleRelease() {
    auto fake{nanoTrackModels()};
    device = &fake;
    {
        auto loaded{build()};
        require(static_cast<bool>(loaded), "后端装配失败");
        require(static_cast<bool>(loaded.value->setTemplate(image(127, 3.0f))), "模板建立失败");
        require(static_cast<bool>(loaded.value->search(image(255, 11.0f))), "搜索失败");
    }
    require(fake.live_handles == 0, "正常销毁后仍有内存句柄");
    require(fake.models[0].destroys == 1 && fake.models[1].destroys == 1 && fake.models[2].destroys == 1,
            "会话销毁次数错误");
    device = nullptr;
    std::cout << "正常销毁没有重复释放\n";
}
} // namespace

int main() {
    try {
        checkAllocationFailures();
        checkRuntimeFailures();
        checkNoDoubleRelease();
        checkFatalScenarios();
        std::cout << "search_direct 受控资源与失败测试通过\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

from pathlib import Path
root=Path(__file__).parent/'source'
p=root/'src/hybrid_tracker/yolo_detector.cpp';s=p.read_text().replace('#include <fstream>','#include <fstream>\n#include <cstdlib>')
s=s.replace('m_worker_thread = cpu_affinity::start', 'if (std::getenv("DRONE_FIXED_SCHEDULE")) { return true; }\n        m_worker_thread = cpu_affinity::start')
s=s.replace('bool YoloV8Detector::get_result(YoloResult& result)\n{', '''bool YoloV8Detector::get_result(YoloResult& result)
{
    if (std::getenv("DRONE_FIXED_SCHEDULE") && diagnostics::current_update % 40 == 20) {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (m_new_request && m_next_request.frame_id == 1) {
            float x{}, y{}, width{}, height{}, score{};
            std::sscanf(std::getenv("DRONE_FIXED_BOX"), "%f,%f,%f,%f,%f", &x, &y, &width, &height, &score);
            m_last_result = {m_next_request.frame_id, true, {{x, y, width, height}, score, 0},
                m_next_request.request_id, m_next_request.source_update_id, m_next_request.identity, diagnostics::Reason::None};
            m_result_ready = true;
            m_new_request = false;
        }
    }''');p.write_text(s)
p=root/'src/diagnostics/recorder.h';s=p.read_text().replace('inline void emit(Event event) noexcept {','inline void (*fixed_hook)(const Event &) noexcept{};\ninline void emit(Event event) noexcept {\n    if (fixed_hook) { fixed_hook(event); }');p.write_text(s)
p=root/'CMakeLists.txt';s=p.read_text().replace('ament_package()', '''add_executable(fixed_workload_probe src/fixed_workload_probe.cpp)
target_link_libraries(fixed_workload_probe PRIVATE hybrid_tracker platform_rk3576 OpenSSL::Crypto)
install(TARGETS fixed_workload_probe DESTINATION lib/${PROJECT_NAME})
ament_package()''');p.write_text(s)

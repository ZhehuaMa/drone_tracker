cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nanotrackv3_search_direct LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    option(DRONE_TRACKER_BUILD_TESTS "Build controlled search-direct tests" ON)
    enable_testing()
    set(REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
    include(${REPOSITORY_ROOT}/src/platform/rk3576/dependencies.cmake)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc dnn video imgcodecs videoio)
    add_subdirectory(${REPOSITORY_ROOT}/src/hybrid_tracker common)
    add_subdirectory(${REPOSITORY_ROOT}/src/platform/rk3576 rk3576)
    # 共用运行器、配置与记录来自 baseline；独立入口自行引入，不重复添加目标。
    add_subdirectory(${REPOSITORY_ROOT}/tests/nanotracev3_performance_benchmarks/baseline baseline)
endif()
get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
find_package(JPEG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SEARCH_DIRECT_AV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)

add_library(nanotrack_search_direct_backend STATIC
    shared_memory_api.cpp
    native_attributes.cpp
    feature_transfer.cpp
    direct_session.cpp
    search_direct_backend.cpp)
target_include_directories(nanotrack_search_direct_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE "${RKNN_INCLUDE_DIR}")
target_link_libraries(nanotrack_search_direct_backend PUBLIC nanotrack_baseline_support rk3576_yolo_sdk)

add_executable(nanotrackv3_search_direct main.cpp)
target_include_directories(nanotrackv3_search_direct PRIVATE "${RKNN_INCLUDE_DIR}")
target_link_libraries(nanotrackv3_search_direct PRIVATE nanotrack_search_direct_backend nanotrack_baseline_runner)
install(TARGETS nanotrackv3_search_direct DESTINATION lib/drone_tracker)

add_executable(nanotrackv3_search_direct_probe numeric_probe.cpp)
target_include_directories(nanotrackv3_search_direct_probe PRIVATE "${RKNN_INCLUDE_DIR}")
target_link_libraries(nanotrackv3_search_direct_probe PRIVATE nanotrack_search_direct_backend)
install(TARGETS nanotrackv3_search_direct_probe DESTINATION lib/drone_tracker)

add_executable(nanotrackv3_benchmark_compare compare_runs.cpp)
target_link_libraries(nanotrackv3_benchmark_compare PRIVATE nanotrack_baseline_support)
install(TARGETS nanotrackv3_benchmark_compare DESTINATION lib/drone_tracker)

if(DRONE_TRACKER_BUILD_TESTS)
    foreach(name direct_backend direct_failure)
        add_executable(search_direct_${name}_tests tests/${name}_test.cpp)
        target_include_directories(search_direct_${name}_tests PRIVATE "${RKNN_INCLUDE_DIR}")
        target_link_libraries(search_direct_${name}_tests PRIVATE nanotrack_search_direct_backend)
        add_test(NAME search_direct_${name}_tests COMMAND search_direct_${name}_tests)
    endforeach()
    add_test(NAME search_direct_help COMMAND nanotrackv3_search_direct --help)
    add_test(NAME search_direct_probe_help COMMAND nanotrackv3_search_direct_probe --help)
    add_test(NAME search_direct_probe_missing_models COMMAND nanotrackv3_search_direct_probe --iterations 1)
    set_tests_properties(search_direct_probe_missing_models PROPERTIES WILL_FAIL TRUE)
    add_test(NAME search_direct_missing_config COMMAND nanotrackv3_search_direct)
    set_tests_properties(search_direct_missing_config PROPERTIES WILL_FAIL TRUE)

    set(SEARCH_DIRECT_FIXTURES "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    add_executable(search_direct_video_fixtures ${REPOSITORY_ROOT}/src/platform/rk3576/tests/video_fixture_generator.cpp)
    target_link_libraries(search_direct_video_fixtures PRIVATE rk3576_video JPEG::JPEG PkgConfig::SEARCH_DIRECT_AV)
    add_test(NAME search_direct_video_fixtures COMMAND search_direct_video_fixtures "${SEARCH_DIRECT_FIXTURES}")
    set_tests_properties(search_direct_video_fixtures PROPERTIES FIXTURES_SETUP search_direct_video)

    add_executable(search_direct_runner_tests tests/direct_runner_test.cpp)
    target_include_directories(search_direct_runner_tests PRIVATE "${RKNN_INCLUDE_DIR}")
    target_link_libraries(search_direct_runner_tests PRIVATE nanotrack_search_direct_backend nanotrack_baseline_runner)
    add_dependencies(search_direct_runner_tests baseline_verify_records nanotrackv3_benchmark_compare)
    add_test(NAME search_direct_runner_tests COMMAND search_direct_runner_tests "${SEARCH_DIRECT_FIXTURES}"
        $<TARGET_FILE:baseline_verify_records> $<TARGET_FILE:nanotrackv3_benchmark_compare>)
    set_tests_properties(search_direct_runner_tests PROPERTIES FIXTURES_REQUIRED search_direct_video TIMEOUT 120)
endif()

cmake_minimum_required(VERSION 3.10)

# 单独配置本目录可构建完整公共算法，不发现 ROS 或硬件 SDK。
option(DRONE_TRACKER_PREPROCESS_TESTS_ONLY "Build platform-independent algorithm and contract tests" OFF)
if(DRONE_TRACKER_PREPROCESS_TESTS_ONLY)
    project(preprocess_common LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc dnn video)
endif()
find_package(Threads REQUIRED)

add_library(platform_interfaces INTERFACE)
target_include_directories(platform_interfaces INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})

add_library(model_common STATIC model/validation.cpp model/jetson/jetson_model_backend.cpp)
target_include_directories(model_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_library(preprocess_geometry STATIC preprocess/preprocess_geometry.cpp)
target_include_directories(preprocess_geometry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(preprocess_geometry PUBLIC ${OpenCV_LIBS})

add_library(hybrid_tracker STATIC hybrid_tracker.cpp yolo_detector.cpp nano_tracker.cpp dcf_tracker.cpp rotation_kf.cpp)
target_link_libraries(hybrid_tracker PUBLIC platform_interfaces model_common preprocess_geometry ${OpenCV_LIBS} Threads::Threads)

# 装配顺序使用真实代码，模型加载及预处理创建可由测试替换；本目标不使用 SDK。
add_library(jetson_tracker_assembly STATIC ../platform/jetson/tracker_assembly.cpp)
target_link_libraries(jetson_tracker_assembly PUBLIC platform_interfaces model_common)

option(DRONE_TRACKER_BUILD_TESTS "Build hybrid tracker unit tests" OFF)
if(DRONE_TRACKER_PREPROCESS_TESTS_ONLY OR DRONE_TRACKER_BUILD_TESTS)
    enable_testing()
    add_library(platform_headers_check OBJECT tests/platform_headers_test.cpp)
    target_link_libraries(platform_headers_check PRIVATE platform_interfaces)
    foreach(test_name model_contract preprocess_common preprocess_contract hybrid_tracker platform_contract)
        if(test_name STREQUAL "preprocess_common")
            set(test_source tests/preprocess_geometry_test.cpp)
        elseif(test_name STREQUAL "hybrid_tracker")
            set(test_source tests/test_main.cpp)
        else()
            set(test_source tests/${test_name}_test.cpp)
        endif()
        add_executable(${test_name}_tests ${test_source})
        target_link_libraries(${test_name}_tests PRIVATE hybrid_tracker jetson_tracker_assembly)
        add_test(NAME ${test_name}_tests COMMAND ${test_name}_tests)
    endforeach()
    add_test(NAME platform_selection_tests COMMAND ${CMAKE_COMMAND}
        -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../..
        -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/platform_selection
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform_selection_test.cmake)
endif()

if(NOT DRONE_TRACKER_PREPROCESS_TESTS_ONLY AND DRONE_TRACKER_BUILD_TESTS)
    add_executable(hybrid_tracker_stress tests/stress_main.cpp)
    target_link_libraries(hybrid_tracker_stress PRIVATE hybrid_tracker platform_jetson)
    add_executable(replay_harness tests/replay_harness.cpp)
    target_link_libraries(replay_harness PRIVATE hybrid_tracker platform_jetson)
    add_executable(vpi_race_repro tests/race_repro.cpp)
    target_link_libraries(vpi_race_repro PRIVATE ${OpenCV_LIBS} vpi CUDA::cudart Threads::Threads)
endif()

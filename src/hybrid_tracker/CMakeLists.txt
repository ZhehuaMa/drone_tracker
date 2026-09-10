cmake_minimum_required(VERSION 3.10)

# 单独配置本目录即可运行公共预处理测试，无需发现任何硬件 SDK。
option(DRONE_TRACKER_PREPROCESS_TESTS_ONLY "Build only platform-independent preprocessing tests" OFF)
if(DRONE_TRACKER_PREPROCESS_TESTS_ONLY)
    project(preprocess_common LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    enable_testing()
    add_library(model_common STATIC model/validation.cpp model/jetson/jetson_model_backend.cpp)
    target_include_directories(model_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    add_library(preprocess_geometry STATIC preprocess/preprocess_geometry.cpp)
    target_include_directories(preprocess_geometry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # 公共几何和真实算法测试仅依赖 OpenCV 与线程库。
    find_package(OpenCV REQUIRED COMPONENTS core imgproc dnn)
    find_package(Threads REQUIRED)
    target_include_directories(preprocess_geometry SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
    add_executable(preprocess_common_tests tests/preprocess_geometry_test.cpp)
    target_include_directories(preprocess_common_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(preprocess_common_tests PRIVATE preprocess_geometry)
    add_test(NAME preprocess_common_tests COMMAND preprocess_common_tests)
    add_executable(preprocess_contract_tests tests/preprocess_contract_test.cpp nano_tracker.cpp yolo_detector.cpp)
    target_include_directories(preprocess_contract_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(preprocess_contract_tests PRIVATE preprocess_geometry model_common ${OpenCV_LIBS} Threads::Threads)
    add_test(NAME preprocess_contract_tests COMMAND preprocess_contract_tests)
    add_executable(model_contract_tests tests/model_contract_test.cpp)
    target_link_libraries(model_contract_tests PRIVATE model_common)
    add_test(NAME model_contract_tests COMMAND model_contract_tests)
    return()
endif()

set(CMAKE_CUDA_ARCHITECTURES 87)
set(CUDA_ARCHITECTURES "87")

project(hybrid_tracker LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Find required packages
find_package(CUDAToolkit REQUIRED)
find_package(OpenCV REQUIRED)
find_package(VPI REQUIRED)

# message("CUDA Arch: ${CMAKE_CUDA_ARCHITECTURES_NATIVE}")
# message("Platform: ${CMAKE_LIBRARY_ARCHITECTURE}")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CUDAToolkit_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${TensorRT_INCLUDE_DIRS}
    /usr/include/${CMAKE_LIBRARY_ARCHITECTURE}
)

# Link directories
link_directories(
    ${TensorRT_LIBRARIES}
    ${CUDA_TOOLKIT_ROOT_DIR}/lib64
    /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
)

# Library source files
set(LIB_SOURCES
    hybrid_tracker.cpp
    yolo_detector.cpp
    dcf_tracker.cpp
    rotation_kf.cpp
    nano_tracker.cpp
    model/jetson/tensor_buffer.cpp
    model/jetson/jetson_storage.cpp
    model/jetson/tensor_rt_model_executor.cpp
    model/jetson/jetson_model_factory.cpp
    preprocess/jetson/jetson_model_input.cpp
    preprocess/jetson/jetson_image_ops.cpp
    preprocess/jetson/jetson_yolo_preprocessor.cpp
    preprocess/jetson/jetson_nanotrack_preprocessor.cpp
)

set(LIB_HEADERS
    hybrid_tracker.h
    dcf_tracker.h
    rotation_kf.h
    rotation_history.h
)

# 几何实现只有一个编译目标，公共测试不链接平台算法库。
add_library(preprocess_geometry STATIC preprocess/preprocess_geometry.cpp)
target_include_directories(preprocess_geometry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})

add_library(model_common STATIC model/validation.cpp model/jetson/jetson_model_backend.cpp)
target_include_directories(model_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Create shared library
add_library(${PROJECT_NAME} STATIC ${LIB_SOURCES})

target_link_libraries(${PROJECT_NAME} PUBLIC
    drone_frame
    model_common
    preprocess_geometry
    ${OpenCV_LIBS}
    nvinfer
    CUDA::cudart
    vpi
    pthread
)

# 单元测试（DCF/KF/切片，纯主机端，不需要 GPU 与 TRT 引擎）
option(DRONE_TRACKER_BUILD_TESTS "Build hybrid tracker unit tests" OFF)
if(DRONE_TRACKER_BUILD_TESTS)
    add_executable(model_contract_tests tests/model_contract_test.cpp)
    target_link_libraries(model_contract_tests PRIVATE model_common)
    add_test(NAME model_contract_tests COMMAND model_contract_tests)
    add_executable(preprocess_common_tests tests/preprocess_geometry_test.cpp)
    target_include_directories(preprocess_common_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(preprocess_common_tests PRIVATE preprocess_geometry)
    add_executable(preprocess_contract_tests tests/preprocess_contract_test.cpp nano_tracker.cpp yolo_detector.cpp)
    target_include_directories(preprocess_contract_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(preprocess_contract_tests PRIVATE preprocess_geometry model_common ${OpenCV_LIBS} pthread)
    add_executable(hybrid_tracker_tests tests/test_main.cpp)
    target_link_libraries(hybrid_tracker_tests PRIVATE ${PROJECT_NAME})
    # 压力测试：需要真实 TRT 引擎（<pkg>/models），验证 VPI/内存稳定性
    add_executable(hybrid_tracker_stress tests/stress_main.cpp)
    target_link_libraries(hybrid_tracker_stress PRIVATE ${PROJECT_NAME})
    # 竞态复现：并发流提交 vs 主机锁（纯 VPI，无引擎）
    add_executable(vpi_race_repro tests/race_repro.cpp)
    target_link_libraries(vpi_race_repro PRIVATE ${OpenCV_LIBS} vpi CUDA::cudart pthread)
    # 离线回放：用录制视频直接驱动完整 HybridTracker，输出 bbox CSV 与稳定性统计
    add_executable(replay_harness tests/replay_harness.cpp)
    target_link_libraries(replay_harness PRIVATE ${PROJECT_NAME})
endif()

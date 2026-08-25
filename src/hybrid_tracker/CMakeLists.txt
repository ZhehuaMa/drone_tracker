cmake_minimum_required(VERSION 3.10)

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
    mv_camera
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
    trt_engine.cpp
)

set(LIB_HEADERS
    hybrid_tracker.h
    dcf_tracker.h
    rotation_kf.h
    rotation_history.h
)

# Create shared library
add_library(${PROJECT_NAME} STATIC ${LIB_SOURCES})

target_link_libraries(${PROJECT_NAME} PUBLIC
    ${OpenCV_LIBS}
    nvinfer
    CUDA::cudart
    vpi
    pthread
)

# 单元测试（DCF/KF/切片，纯主机端，不需要 GPU 与 TRT 引擎）
option(DRONE_TRACKER_BUILD_TESTS "Build hybrid tracker unit tests" OFF)
if(DRONE_TRACKER_BUILD_TESTS)
    add_executable(hybrid_tracker_tests tests/test_main.cpp)
    target_link_libraries(hybrid_tracker_tests PRIVATE ${PROJECT_NAME})
    # 压力测试：需要真实 TRT 引擎（<pkg>/models），验证 VPI/内存稳定性
    add_executable(hybrid_tracker_stress tests/stress_main.cpp)
    target_link_libraries(hybrid_tracker_stress PRIVATE ${PROJECT_NAME})
endif()


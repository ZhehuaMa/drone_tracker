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
    simple_kf.cpp
    nano_tracker.cpp
    trt_engine.cpp
)

set(LIB_HEADERS
    hybrid_tracker.h
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


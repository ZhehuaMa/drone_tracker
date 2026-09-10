cmake_minimum_required(VERSION 3.10)

# 必须先拒绝未实现平台，不能让 CUDA/SDK 发现掩盖平台错误。
set(DRONE_TRACKER_PLATFORM "jetson" CACHE STRING "Production platform")
set_property(CACHE DRONE_TRACKER_PLATFORM PROPERTY STRINGS jetson)
if(NOT DRONE_TRACKER_PLATFORM STREQUAL "jetson")
    if(DRONE_TRACKER_PLATFORM STREQUAL "rk3576")
        message(FATAL_ERROR "DRONE_TRACKER_PLATFORM='${DRONE_TRACKER_PLATFORM}' backend not implemented; supported: jetson")
    else()
        message(FATAL_ERROR "DRONE_TRACKER_PLATFORM='${DRONE_TRACKER_PLATFORM}' unsupported; supported: jetson")
    endif()
endif()

set(CMAKE_CUDA_ARCHITECTURES 87)
set(CUDA_ARCHITECTURES "87")

project(drone_tracker LANGUAGES CXX CUDA)

add_compile_definitions(ROS_PACKAGE_NAME="${PROJECT_NAME}")

set(CMAKE_BUILD_TYPE Release)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Find required packages
find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(vision_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(std_msgs REQUIRED)
find_package(mavros_msgs REQUIRED)
find_package(rosbag2_cpp REQUIRED)

find_package(CUDAToolkit REQUIRED)
find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
option(DRONE_TRACKER_BUILD_TESTS "Build hybrid tracker unit tests" OFF)
if(DRONE_TRACKER_BUILD_TESTS)
    enable_testing()
endif()
find_package(VPI REQUIRED)

add_library(drone_frame STATIC
    src/frame/vpi_frame.cpp
)
target_include_directories(drone_frame PUBLIC
    src
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(drone_frame PUBLIC
    ${OpenCV_LIBS}
    vpi
)

add_subdirectory(src/mv_camera)
add_subdirectory(src/hybrid_tracker)
add_subdirectory(src/platform)

# Executable
add_executable(drone_tracker_node
    src/drone_tracker_node.cpp
)

ament_target_dependencies(drone_tracker_node
    ament_index_cpp
    rclcpp
    sensor_msgs
    nav_msgs
    vision_msgs
    cv_bridge
    std_msgs
    mavros_msgs
    rosbag2_cpp
)

target_link_libraries(drone_tracker_node
    hybrid_tracker
    platform_jetson
)

install(TARGETS
  drone_tracker_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  models
  DESTINATION share/${PROJECT_NAME}/
)

install(DIRECTORY
  launch
  DESTINATION share/${PROJECT_NAME}/
)

install(DIRECTORY
  parameters
  DESTINATION share/${PROJECT_NAME}/
)

ament_package()

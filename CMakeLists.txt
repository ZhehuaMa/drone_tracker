cmake_minimum_required(VERSION 3.10)

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
find_package(cv_bridge REQUIRED)
find_package(std_msgs REQUIRED)
find_package(mavros_msgs REQUIRED)

find_package(CUDAToolkit REQUIRED)
find_package(OpenCV REQUIRED)
find_package(VPI REQUIRED)
find_package(Eigen3 REQUIRED)

# Include directories
include_directories(
    src/mv_camera
    src/hybrid_tracker
    ${EIGEN3_INCLUDE_DIR}
    src
)

add_subdirectory(src/mv_camera)
add_subdirectory(src/hybrid_tracker)

# Executable
add_executable(drone_tracker_node
    src/drone_tracker_node.cpp
    src/drone_ekf.cpp
)

ament_target_dependencies(drone_tracker_node
    ament_index_cpp
    rclcpp
    sensor_msgs
    vision_msgs
    cv_bridge
    std_msgs
    mavros_msgs
)

target_link_libraries(drone_tracker_node
    hybrid_tracker
    mv_camera
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

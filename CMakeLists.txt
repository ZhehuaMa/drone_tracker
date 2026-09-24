cmake_minimum_required(VERSION 3.10)

# 在语言及 SDK 发现之前校验平台。
set(DRONE_TRACKER_PLATFORM "jetson" CACHE STRING "Production platform")
set_property(CACHE DRONE_TRACKER_PLATFORM PROPERTY STRINGS jetson rk3576)
if(NOT DRONE_TRACKER_PLATFORM MATCHES "^(jetson|rk3576)$")
    message(FATAL_ERROR "DRONE_TRACKER_PLATFORM='${DRONE_TRACKER_PLATFORM}' unsupported; supported: jetson/rk3576")
endif()
option(DRONE_TRACKER_BUILD_OSD "Build RK3576 HDMI OSD backend" OFF)
option(DRONE_TRACKER_BUILD_RECORDING "Build independent RK3576 recording validation tools" OFF)
if(DRONE_TRACKER_BUILD_RECORDING AND NOT DRONE_TRACKER_PLATFORM STREQUAL "rk3576")
    message(FATAL_ERROR "DRONE_TRACKER_BUILD_RECORDING requires rk3576")
endif()
option(DRONE_TRACKER_OSD_LOW_LEVEL_FEEDBACK "Build optional quadrotor telemetry adapter" OFF)
if(DRONE_TRACKER_BUILD_OSD AND NOT DRONE_TRACKER_PLATFORM STREQUAL "rk3576")
    message(FATAL_ERROR "DRONE_TRACKER_BUILD_OSD requires rk3576")
endif()
if(DRONE_TRACKER_OSD_LOW_LEVEL_FEEDBACK AND NOT DRONE_TRACKER_BUILD_OSD)
    message(FATAL_ERROR "DRONE_TRACKER_OSD_LOW_LEVEL_FEEDBACK requires DRONE_TRACKER_BUILD_OSD")
endif()
project(drone_tracker LANGUAGES CXX)
if(DRONE_TRACKER_PLATFORM STREQUAL "jetson")
    set(CMAKE_CUDA_ARCHITECTURES 87)
    set(CUDA_ARCHITECTURES "87")
    enable_language(CUDA)
else()
    include(src/platform/rk3576/dependencies.cmake)
endif()

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

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
option(DRONE_TRACKER_BUILD_TESTS "Build hybrid tracker unit tests" OFF)
if(DRONE_TRACKER_BUILD_TESTS)
    enable_testing()
endif()
if(DRONE_TRACKER_PLATFORM STREQUAL "jetson")
    find_package(CUDAToolkit REQUIRED)
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
endif()
add_subdirectory(src/hybrid_tracker)
add_subdirectory(src/platform)
add_subdirectory(src/osd)
if(DRONE_TRACKER_PLATFORM STREQUAL "rk3576")
    add_subdirectory(tests/nanotracev3_performance_benchmarks/baseline)
    add_subdirectory(tests/nanotracev3_performance_benchmarks/search_direct)
endif()

# Executable
add_library(drone_tracker_ros STATIC src/drone_tracker_node.cpp)
target_include_directories(drone_tracker_ros PUBLIC src)
if(DRONE_TRACKER_PLATFORM STREQUAL "rk3576")
    target_compile_definitions(drone_tracker_ros PUBLIC DRONE_TRACKER_RK3576)
endif()
add_executable(drone_tracker_node src/drone_tracker_main.cpp)
target_link_libraries(drone_tracker_node drone_tracker_ros platform_${DRONE_TRACKER_PLATFORM})
if(DRONE_TRACKER_BUILD_RECORDING)
    add_executable(recording_validation_node src/tests/recording_validation_node.cpp)
    target_link_libraries(recording_validation_node drone_tracker_ros platform_${DRONE_TRACKER_PLATFORM})
endif()

set(DRONE_TRACKER_ROS_DEPENDENCIES
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

ament_target_dependencies(drone_tracker_ros ${DRONE_TRACKER_ROS_DEPENDENCIES})
target_link_libraries(drone_tracker_ros hybrid_tracker)
target_link_libraries(drone_tracker_ros drone_osd_telemetry)
if(DRONE_TRACKER_PLATFORM STREQUAL "rk3576")
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_link_libraries(drone_tracker_ros OpenSSL::Crypto)
endif()
if(DRONE_TRACKER_BUILD_OSD)
    target_compile_definitions(drone_tracker_ros PRIVATE DRONE_TRACKER_HAS_OSD)
    target_link_libraries(drone_tracker_ros drone_osd_rk3576)
endif()

install(TARGETS
  drone_tracker_node
  DESTINATION lib/${PROJECT_NAME}
)
install(FILES src/osd/FONT_LICENSE.txt DESTINATION share/${PROJECT_NAME}/licenses)

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

if(DRONE_TRACKER_BUILD_TESTS AND DRONE_TRACKER_PLATFORM STREQUAL "rk3576")
    add_executable(cpu_affinity_tests src/platform/rk3576/tests/cpu_affinity_test.cpp)
    target_link_libraries(cpu_affinity_tests PRIVATE cpu_affinity)
    add_test(NAME cpu_affinity_tests COMMAND cpu_affinity_tests)
    add_test(NAME deployment_cpu_policy_tests COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/deployment/tests/test_cpu_policy.py ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ros_video_tests src/tests/ros_video_test.cpp)
    target_link_libraries(ros_video_tests drone_tracker_ros rk3576_video)
    set(ROS_VIDEO_FIXTURES "${CMAKE_CURRENT_BINARY_DIR}/ros-video-fixtures")
    target_compile_definitions(ros_video_tests PRIVATE ROS_VIDEO_FIXTURES="${ROS_VIDEO_FIXTURES}")
    add_executable(ros_video_fixture_generator src/platform/rk3576/tests/video_fixture_generator.cpp)
    target_link_libraries(ros_video_fixture_generator rk3576_video)
    add_test(NAME ros_video_generate_fixtures COMMAND ros_video_fixture_generator "${ROS_VIDEO_FIXTURES}")
    set_tests_properties(ros_video_generate_fixtures PROPERTIES FIXTURES_SETUP ros_video_files)
    add_test(NAME ros_video_tests COMMAND ros_video_tests)
    add_test(NAME ros_initialization_tests COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:drone_tracker_node> -DOSD_BUILT=${DRONE_TRACKER_BUILD_OSD}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/src/tests/ros_initialization_test.cmake)
    # 只编译公共 ROS 配置策略，不链接 Jetson SDK，也不声称实机验证。
    add_library(drone_tracker_jetson_config STATIC src/drone_tracker_node.cpp)
    target_include_directories(drone_tracker_jetson_config PUBLIC src)
    ament_target_dependencies(drone_tracker_jetson_config ${DRONE_TRACKER_ROS_DEPENDENCIES})
    target_link_libraries(drone_tracker_jetson_config hybrid_tracker)
    target_link_libraries(drone_tracker_jetson_config drone_osd_telemetry)
    add_executable(jetson_config_tests src/tests/jetson_config_test.cpp)
    target_link_libraries(jetson_config_tests drone_tracker_jetson_config)
    add_test(NAME jetson_config_tests COMMAND jetson_config_tests)
    set_tests_properties(ros_video_tests PROPERTIES FIXTURES_REQUIRED ros_video_files TIMEOUT 60 ENVIRONMENT "ROS_DOMAIN_ID=187;ROS_LOCALHOST_ONLY=1")
endif()

ament_package()

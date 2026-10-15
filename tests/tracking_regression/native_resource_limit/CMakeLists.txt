cmake_minimum_required(VERSION 3.16)
project(tracking_regression_native_resource_limit LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "This retained OS resource-limit check must run natively")
endif()
get_filename_component(tracker_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core)
find_package(yaml-cpp REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VIDEO_AV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
find_program(TRACKING_REGRESSION_FFMPEG NAMES ffmpeg REQUIRED)
add_executable(tracking_regression_native_resource_limit
    ../tests/realtime_source_test.cpp
    ../realtime_source.cpp
    "${tracker_root}/src/platform/rk3576/host_frame.cpp"
    "${tracker_root}/src/diagnostics/recorder.cpp")
target_include_directories(tracking_regression_native_resource_limit PRIVATE
    .. "${tracker_root}/src" "${tracker_root}/src/hybrid_tracker" ${OpenCV_INCLUDE_DIRS})
target_compile_definitions(tracking_regression_native_resource_limit PRIVATE
    TRACKING_REGRESSION_FFMPEG="${TRACKING_REGRESSION_FFMPEG}" DRONE_TRACKER_ENABLE_DIAGNOSTICS)
target_link_libraries(tracking_regression_native_resource_limit
    ${OpenCV_LIBS} yaml-cpp OpenSSL::Crypto PkgConfig::VIDEO_AV pthread)
ament_target_dependencies(tracking_regression_native_resource_limit rclcpp sensor_msgs)
enable_testing()
add_test(NAME tracking_regression_native_resource_limit
    COMMAND tracking_regression_native_resource_limit --resource-limit-only)
set_tests_properties(tracking_regression_native_resource_limit PROPERTIES TIMEOUT 90)

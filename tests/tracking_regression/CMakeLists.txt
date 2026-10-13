find_program(TRACKING_REGRESSION_FFMPEG NAMES ffmpeg)
find_program(TRACKING_REGRESSION_FFPROBE NAMES ffprobe)
foreach(tool TRACKING_REGRESSION_FFMPEG TRACKING_REGRESSION_FFPROBE)
    if(NOT ${tool} OR NOT EXISTS "${${tool}}")
        message(FATAL_ERROR "FFmpeg CLI test prerequisite missing: ${tool}. See tests/tracking_regression/README.md; run prepare_test_tools.sh and run_sdk_tests.sh.")
    endif()
    execute_process(COMMAND "${${tool}}" -version RESULT_VARIABLE cli_status OUTPUT_QUIET ERROR_QUIET)
    if(NOT cli_status EQUAL 0)
        message(FATAL_ERROR "FFmpeg test CLI cannot run on this architecture: ${${tool}}")
    endif()
endforeach()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_library(tracking_regression_source STATIC realtime_source.cpp)
target_include_directories(tracking_regression_source PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tracking_regression_source rk3576_video yaml-cpp OpenSSL::Crypto)
ament_target_dependencies(tracking_regression_source rclcpp sensor_msgs)
add_executable(rk3576_tracking_regression main.cpp)
target_link_libraries(rk3576_tracking_regression PRIVATE tracking_regression_source drone_tracker_ros platform_rk3576)
install(TARGETS rk3576_tracking_regression DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION share/${PROJECT_NAME}/tracking_regression
    PATTERN "__pycache__" EXCLUDE PATTERN "*.cpp" EXCLUDE PATTERN "*.h" EXCLUDE)
add_test(NAME tracking_regression_python
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p test_*.py)
set_tests_properties(tracking_regression_python PROPERTIES TIMEOUT 30
    ENVIRONMENT "DRONE_TRACKER_TEST_PROGRAM=$<TARGET_FILE:rk3576_tracking_regression>")
add_executable(tracking_regression_source_tests tests/realtime_source_test.cpp)
target_link_libraries(tracking_regression_source_tests PRIVATE tracking_regression_source drone_diagnostics)
target_compile_definitions(tracking_regression_source_tests PRIVATE
    TRACKING_REGRESSION_FFMPEG="${TRACKING_REGRESSION_FFMPEG}")
add_test(NAME tracking_regression_source_tests COMMAND tracking_regression_source_tests)
set_tests_properties(tracking_regression_source_tests PROPERTIES TIMEOUT 90)
add_executable(tracking_regression_round_tests tests/node_round_test.cpp)
target_link_libraries(tracking_regression_round_tests PRIVATE drone_tracker_ros yaml-cpp)
add_test(NAME tracking_regression_round_tests COMMAND tracking_regression_round_tests)
set_tests_properties(tracking_regression_round_tests PROPERTIES TIMEOUT 30
    ENVIRONMENT "ROS_DOMAIN_ID=188;ROS_LOCALHOST_ONLY=1")
add_test(NAME tracking_regression_preflight
    COMMAND ${CMAKE_COMMAND} -DREGRESSION_PROGRAM=$<TARGET_FILE:rk3576_tracking_regression>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/preflight_test.cmake)

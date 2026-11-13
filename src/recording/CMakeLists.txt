find_package(OpenSSL REQUIRED COMPONENTS Crypto)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE RECORDING_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
add_library(drone_recording STATIC topic_catalog.cpp config.cpp recording_manager.cpp)
target_compile_definitions(drone_recording PRIVATE DRONE_RECORDING_SOURCE_REVISION="${RECORDING_REVISION}")
target_compile_options(drone_recording PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(drone_recording PUBLIC ..)
ament_target_dependencies(drone_recording PUBLIC rclcpp sensor_msgs geometry_msgs nav_msgs
    vision_msgs std_msgs mavros_msgs quadrotor_msgs rosbag2_cpp)
target_link_libraries(drone_recording PUBLIC cpu_affinity OpenSSL::Crypto)
if(DRONE_TRACKER_BUILD_TESTS)
    add_executable(recording_catalog_policy_tests tests/catalog_policy_test.cpp)
    target_link_libraries(recording_catalog_policy_tests PRIVATE drone_recording)
    add_test(NAME recording_catalog_policy_tests COMMAND recording_catalog_policy_tests)
    add_executable(recording_writer_tests tests/writer_test.cpp)
    target_link_libraries(recording_writer_tests PRIVATE drone_recording)
    add_executable(recording_transport_tests tests/transport_test.cpp)
    target_link_libraries(recording_transport_tests PRIVATE drone_recording)
    add_executable(recording_affinity_tests tests/affinity_test.cpp)
    target_link_libraries(recording_affinity_tests PRIVATE drone_recording)
    set(RECORDING_AFFINITY_PROGRAM "$<TARGET_FILE:recording_affinity_tests>")
    set(RECORDING_TRANSPORT_PROGRAM "$<TARGET_FILE:recording_transport_tests>")
    if(CMAKE_CROSSCOMPILING)
        include(ExternalProject)
        set(RECORDING_NATIVE_DIR "${CMAKE_BINARY_DIR}/recording-native")
        ExternalProject_Add(recording_native
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/native"
            BINARY_DIR "${RECORDING_NATIVE_DIR}"
            CMAKE_ARGS -DDRONE_REPO_ROOT=${PROJECT_SOURCE_DIR}
                -DQUADROTOR_SOURCE=$ENV{DRONE_QUADROTOR_SOURCE}
                -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/c++ -DCMAKE_C_COMPILER=/usr/bin/cc
            INSTALL_COMMAND "" BUILD_ALWAYS TRUE
            BUILD_BYPRODUCTS "${RECORDING_NATIVE_DIR}/recording_transport_native_tests")
        set(RECORDING_TRANSPORT_PROGRAM "${RECORDING_NATIVE_DIR}/recording_transport_native_tests")
        set(RECORDING_AFFINITY_PROGRAM "${RECORDING_NATIVE_DIR}/recording_affinity_native_tests")
    endif()
    add_test(NAME recording_transport_tests COMMAND ${RECORDING_TRANSPORT_PROGRAM})
    add_test(NAME recording_affinity_tests COMMAND ${RECORDING_AFFINITY_PROGRAM})
    set_tests_properties(recording_affinity_tests PROPERTIES TIMEOUT 45
        ENVIRONMENT "ROS_DOMAIN_ID=${DRONE_TEST_ROS_DOMAIN_ID};ROS_LOCALHOST_ONLY=1")
    if(CMAKE_CROSSCOMPILING)
        add_test(NAME recording_writer_tests COMMAND "${RECORDING_NATIVE_DIR}/recording_writer_native_tests")
        set_tests_properties(recording_writer_tests PROPERTIES TIMEOUT 45
            ENVIRONMENT "AMENT_PREFIX_PATH=${RECORDING_NATIVE_DIR}/message-install:/opt/ros/humble;LD_LIBRARY_PATH=${RECORDING_NATIVE_DIR}/message-install/lib:/opt/ros/humble/lib;ROS_DOMAIN_ID=${DRONE_TEST_ROS_DOMAIN_ID};ROS_LOCALHOST_ONLY=1")
    else()
        add_test(NAME recording_writer_tests COMMAND recording_writer_tests)
    endif()
    set_tests_properties(recording_transport_tests PROPERTIES TIMEOUT 45
        ENVIRONMENT "ROS_DOMAIN_ID=${DRONE_TEST_ROS_DOMAIN_ID};ROS_LOCALHOST_ONLY=1")
    if(CMAKE_CROSSCOMPILING)
        set_property(TEST recording_affinity_tests APPEND PROPERTY ENVIRONMENT
            "AMENT_PREFIX_PATH=${RECORDING_NATIVE_DIR}/message-install:/opt/ros/humble"
            "LD_LIBRARY_PATH=${RECORDING_NATIVE_DIR}/message-install/lib:/opt/ros/humble/lib")
        set_property(TEST recording_transport_tests APPEND PROPERTY ENVIRONMENT
            "AMENT_PREFIX_PATH=${RECORDING_NATIVE_DIR}/message-install:/opt/ros/humble"
            "LD_LIBRARY_PATH=${RECORDING_NATIVE_DIR}/message-install/lib:/opt/ros/humble/lib")
    endif()
endif()

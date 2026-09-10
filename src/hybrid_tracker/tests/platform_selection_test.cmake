foreach(platform rk3576 other)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${REPO_ROOT}" -B "${TEST_ROOT}/${platform}"
        -DDRONE_TRACKER_PLATFORM=${platform} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    set(log "${output}\n${error}")
    if(result EQUAL 0 OR NOT log MATCHES "DRONE_TRACKER_PLATFORM='${platform}'" OR
       NOT log MATCHES "supported: jetson" OR log MATCHES "Failed to find nvcc|Could not find.*CUDA")
        message(FATAL_ERROR "平台选择未提前失败: ${log}")
    endif()
endforeach()

foreach(clock use_sim_time use_history_timestamp)
    execute_process(COMMAND "${PROGRAM}" --ros-args -p ${clock}:=true
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
    if(NOT result EQUAL 1 OR NOT "${output}${error}" MATCHES "${clock} must be false")
        message(FATAL_ERROR "初始化错误未以诊断和退出码 1 返回: ${result} ${output}${error}")
    endif()
endforeach()
execute_process(COMMAND "${PROGRAM}" --ros-args -p osd.enabled:=true
    -p osd.drm_device:=/nonexistent-drone-osd-test-device
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
if(OSD_BUILT)
    set(expected "OSD open /nonexistent-drone-osd-test-device")
else()
    set(expected "osd.enabled requires DRONE_TRACKER_BUILD_OSD=ON")
endif()
if(NOT result EQUAL 1 OR NOT "${output}${error}" MATCHES "${expected}")
    message(FATAL_ERROR "OSD 初始化失败诊断不符: ${result} ${output}${error}")
endif()

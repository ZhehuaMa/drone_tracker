cmake_minimum_required(VERSION 3.16)
# 使用实际子进程核对退出状态，避免只检查结构体中的数字。
set(failure_cases "预处理比例30%" "恢复中位数相等" "恢复中位数增加" "恢复P95增加"
                  "预处理刚超过20%" "恢复P95刚增加")
set(success_cases "通过正例" "预处理恰好20%" "恢复中位数刚降低" "恢复P95相等")
foreach(sample IN LISTS failure_cases success_cases)
    execute_process(COMMAND "${TEST_PROGRAM}" --case "${sample}"
                    RESULT_VARIABLE exit_code OUTPUT_VARIABLE report ERROR_VARIABLE errors)
    if(sample IN_LIST failure_cases)
        if(NOT exit_code STREQUAL "1" OR NOT report MATCHES "^指定录像重建恢复对照：失败"
           OR NOT report MATCHES "状态：性能门槛未通过" OR report MATCHES "恢复对照：通过")
            message(FATAL_ERROR "${sample} 未正确失败：退出码=${exit_code}\n${report}\n${errors}")
        endif()
    else()
        if(NOT exit_code STREQUAL "0" OR NOT report MATCHES "^指定录像重建恢复对照：通过"
           OR NOT report MATCHES "状态：通过")
            message(FATAL_ERROR "${sample} 未正确通过：退出码=${exit_code}\n${report}\n${errors}")
        endif()
    endif()
    message(STATUS "${sample}：实际退出码=${exit_code}\n${report}")
endforeach()

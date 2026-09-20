cmake_minimum_required(VERSION 3.16)
foreach(required REPOSITORY_ROOT GIT_EXECUTABLE IDENTITY_TEMPLATE IDENTITY_OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "源码身份缺少参数: ${required}")
    endif()
endforeach()

function(identity_git output)
    execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGN}
        WORKING_DIRECTORY "${REPOSITORY_ROOT}"
        OUTPUT_VARIABLE value ERROR_VARIABLE error RESULT_VARIABLE result)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "源码身份收集失败 (${ARGN}): ${error}")
    endif()
    set(${output} "${value}" PARENT_SCOPE)
endfunction()

identity_git(BASELINE_COMMIT rev-parse --verify HEAD)
string(STRIP "${BASELINE_COMMIT}" BASELINE_COMMIT)
identity_git(BASELINE_SOURCE_SNAPSHOT diff --no-ext-diff --no-textconv HEAD --)
identity_git(BASELINE_UNTRACKED -c core.quotepath=false ls-files --others --exclude-standard)
string(REPLACE "\n" ";" BASELINE_UNTRACKED "${BASELINE_UNTRACKED}")
foreach(path IN LISTS BASELINE_UNTRACKED)
    if(path MATCHES "^(src/|tests/nanotracev3_performance_benchmarks/baseline/)"
       AND path MATCHES "[.](cpp|h|yaml|txt|in|cmake)$")
        file(READ "${REPOSITORY_ROOT}/${path}" content)
        string(APPEND BASELINE_SOURCE_SNAPSHOT "\n新增文件 ${path}\n${content}\n")
    endif()
endforeach()
# 保持快照中的 C++ 原始字符串可安全嵌入；内容不变时不触碰输出时间戳。
string(SHA256 BASELINE_SNAPSHOT_HASH "${BASELINE_SOURCE_SNAPSHOT}")
string(SUBSTRING "${BASELINE_SNAPSHOT_HASH}" 0 12 BASELINE_SNAPSHOT_SUFFIX)
set(BASELINE_SNAPSHOT_DELIMITER "snap${BASELINE_SNAPSHOT_SUFFIX}")
configure_file("${IDENTITY_TEMPLATE}" "${IDENTITY_OUTPUT}" @ONLY)

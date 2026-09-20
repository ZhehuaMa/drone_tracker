# 由仓库和独立入口共用；身份检查在使用生成头文件的目标编译前执行。
set(BASELINE_IDENTITY_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")
function(baseline_code_identity target repository_root)
    find_package(Git REQUIRED)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type)
    add_custom_target(${target}_identity
        COMMAND ${CMAKE_COMMAND}
            "-DREPOSITORY_ROOT=${repository_root}"
            "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
            "-DIDENTITY_TEMPLATE=${BASELINE_IDENTITY_MODULE_DIR}/code_identity.h.in"
            "-DIDENTITY_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/code_identity.h"
            "-DBASELINE_BUILD_FLAGS=${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${build_type}}; ${CMAKE_CXX_COMPILER}; C++17"
            -P "${BASELINE_IDENTITY_MODULE_DIR}/generate_code_identity.cmake"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/code_identity.h"
        COMMENT "检查 NanoTrack 基准源码身份"
        VERBATIM)
    add_dependencies(${target} ${target}_identity)
endfunction()

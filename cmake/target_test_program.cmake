include_guard(GLOBAL)

# ARM children must not fall back to an older host-registered binfmt emulator.
# Native builds, including the explicit ARM64 image, retain their normal binary path.
function(drone_target_test_program target output_variable)
    if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
        set(launcher "${CMAKE_CURRENT_BINARY_DIR}/target-launchers/${target}")
        set(command "")
        foreach(argument IN LISTS CMAKE_CROSSCOMPILING_EMULATOR)
            string(REPLACE "'" "'\\''" escaped "${argument}")
            string(APPEND command " '${escaped}'")
        endforeach()
        file(GENERATE OUTPUT "${launcher}"
            CONTENT "#!/usr/bin/env bash\nset -e\nexport QEMU_CPU=cortex-a72\nexec${command} '$<TARGET_FILE:${target}>' \"$@\"\n"
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
        set(${output_variable} "${launcher}" PARENT_SCOPE)
    else()
        set(${output_variable} "$<TARGET_FILE:${target}>" PARENT_SCOPE)
    endif()
endfunction()

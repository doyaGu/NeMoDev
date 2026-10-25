# Exercise the actual Runtime install rules without building the editor or SDK.
if (NOT SOURCE_DIRECTORY OR NOT BINARY_DIRECTORY)
    message(FATAL_ERROR "SOURCE_DIRECTORY and BINARY_DIRECTORY are required")
endif ()
get_filename_component(SOURCE_DIRECTORY "${SOURCE_DIRECTORY}" ABSOLUTE)
get_filename_component(BINARY_DIRECTORY "${BINARY_DIRECTORY}" ABSOLUTE)
set(_project "${BINARY_DIRECTORY}/project")
set(_build "${BINARY_DIRECTORY}/build")
set(_runtime "${BINARY_DIRECTORY}/runtime")
file(MAKE_DIRECTORY "${_project}")
file(WRITE "${_project}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.25)\n"
        "project(NeMoDevReleaseMaterials NONE)\n"
        "set(VIRTOOLSDEV_SOURCE_DIR [==[${SOURCE_DIRECTORY}]==])\n"
        "include([==[${SOURCE_DIRECTORY}/cmake/ThirdPartyNotices.cmake]==])\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_project}" -B "${_build}"
        COMMAND_ERROR_IS_FATAL ANY OUTPUT_QUIET)

function(install_notices)
    execute_process(COMMAND "${CMAKE_COMMAND}" --install "${_build}"
            --prefix "${_runtime}" --component Runtime --config Release
            COMMAND_ERROR_IS_FATAL ANY OUTPUT_QUIET)
endfunction()

function(check_notices expect_failure)
    execute_process(COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIRECTORY=${SOURCE_DIRECTORY}" "-DRUNTIME_DIRECTORY=${_runtime}"
            -P "${SOURCE_DIRECTORY}/tests/CheckLicenseDeployment.cmake"
            RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if (expect_failure)
        if (_result EQUAL 0)
            message(FATAL_ERROR "Incomplete or changed runtime notices were accepted")
        endif ()
    elseif (NOT _result EQUAL 0)
        message(FATAL_ERROR "Notice verification failed: ${_output}${_error}")
    endif ()
endfunction()

install_notices()
check_notices(FALSE)
file(REMOVE "${_runtime}/licenses/VirtoolsScriptDeobfuscation-CC-BY-NC-SA-4.0.txt")
check_notices(TRUE)
install_notices()
file(APPEND "${_runtime}/licenses/DearImGui-MIT.txt" "Changed notice\n")
check_notices(TRUE)
install_notices()
check_notices(FALSE)
message(STATUS "Runtime notice installation, missing-file and changed-file checks passed")

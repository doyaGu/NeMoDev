# Configure the real standalone entry point without its engine sources. Check
# the actionable error for absent, empty and partially populated submodules.
if(NOT HTML_SOURCE_DIRECTORY OR NOT TEST_BINARY_DIRECTORY)
    message(FATAL_ERROR "HTML_SOURCE_DIRECTORY and TEST_BINARY_DIRECTORY are required")
endif()
foreach(_case missing empty incomplete)
    set(_fixture "${TEST_BINARY_DIRECTORY}/${_case}")
    file(MAKE_DIRECTORY "${_fixture}/html")
    configure_file("${HTML_SOURCE_DIRECTORY}/CMakeLists.txt" "${_fixture}/html/CMakeLists.txt" COPYONLY)
    file(COPY "${HTML_SOURCE_DIRECTORY}/detail" DESTINATION "${_fixture}/html")
    if(NOT _case STREQUAL missing)
        file(MAKE_DIRECTORY "${_fixture}/external/litehtml")
    endif()
    if(_case STREQUAL incomplete)
        file(WRITE "${_fixture}/external/litehtml/CMakeLists.txt" "# Incomplete source tree\n")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_fixture}/html" -B "${_fixture}/build"
        -DNEMODEV_HTML_BUILD_TESTS=OFF
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    set(_diagnostic "${_output}${_error}")
    if(_result EQUAL 0 OR NOT _diagnostic MATCHES "litehtml sources are missing or incomplete"
            OR NOT _diagnostic MATCHES "git submodule update --init --recursive external/litehtml"
            OR _diagnostic MATCHES "no longer applies")
        message(FATAL_ERROR "Incorrect ${_case} submodule diagnostic:\n${_diagnostic}")
    endif()
endforeach()
message(STATUS "Missing, empty and incomplete litehtml submodules report the initialization command")

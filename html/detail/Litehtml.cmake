# Keep narrow CSS/layout corrections private to the pinned engine's build copy.
# The source submodule and the application's public HTML interface stay unchanged.
find_package(Git REQUIRED)
set(_html_engine_source "${CMAKE_CURRENT_SOURCE_DIR}/../external/litehtml")
set(_html_engine_copy "${CMAKE_CURRENT_BINARY_DIR}/litehtml-source")
set(_html_engine_input "${CMAKE_CURRENT_BINARY_DIR}/litehtml-patch-input")
file(GLOB_RECURSE _html_engine_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
    RELATIVE "${_html_engine_source}" "${_html_engine_source}/*")
list(FILTER _html_engine_files EXCLUDE REGEX "^\\.git(/|$)")
foreach(_file IN LISTS _html_engine_files)
    configure_file("${_html_engine_source}/${_file}" "${_html_engine_input}/${_file}" COPYONLY)
endforeach()
foreach(_patch litehtml-render-tree.patch litehtml-table-sizing.patch)
    set(_html_engine_patch "${CMAKE_CURRENT_SOURCE_DIR}/detail/${_patch}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_html_engine_patch}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR}"
        "${GIT_EXECUTABLE}" apply --no-index --verbose --ignore-space-change --whitespace=nowarn "${_html_engine_patch}"
        WORKING_DIRECTORY "${_html_engine_input}"
        RESULT_VARIABLE _html_patch_result ERROR_VARIABLE _html_patch_error OUTPUT_VARIABLE _html_patch_output)
    if(NOT _html_patch_result EQUAL 0 OR "${_html_patch_output}${_html_patch_error}" MATCHES "Skipped patch")
        message(FATAL_ERROR "The litehtml fix ${_patch} no longer applies: ${_html_patch_error}")
    endif()
endforeach()
# Preserve compiled-source timestamps when the resulting contents are unchanged.
foreach(_file IN LISTS _html_engine_files)
    configure_file("${_html_engine_input}/${_file}" "${_html_engine_copy}/${_file}" COPYONLY)
endforeach()
add_subdirectory("${_html_engine_copy}" "${CMAKE_CURRENT_BINARY_DIR}/litehtml" EXCLUDE_FROM_ALL)

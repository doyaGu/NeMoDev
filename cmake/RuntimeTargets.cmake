# Runtime loading and CTest setup owned by Virtools Dev.

# CK2.dll and VxMath.dll loaded on first use: the program starts without the
# runtime (a test then skips, the editor resolves the runtime at startup).
function(virtoolsdev_enable_delay_load_runtime target_name)
    if (MSVC AND NOT VIRTOOLSDEV_BALLANCED)
        target_link_options(${target_name} PRIVATE /DELAYLOAD:CK2.dll /DELAYLOAD:VxMath.dll)
        target_link_libraries(${target_name} PRIVATE delayimp)
    endif ()
endfunction()

if (VIRTOOLSDEV_BUILD_TESTS AND NOT VIRTOOLSDEV_BALLANCED AND NOT (VIRTOOLS_RUNTIME_PATH AND EXISTS "${VIRTOOLS_RUNTIME_PATH}/CK2.dll"))
    message(WARNING "VIRTOOLS_RUNTIME_PATH does not contain CK2.dll; tests that load Virtools DLLs may fail at runtime.")
endif ()

# The directory of a runtime's plugin folders (VirtoolsPluginRoot): the
# runtime's, or its parent's when only the parent has RenderEngines
# (Ballance: CK2.dll in Bin\).
function(virtoolsdev_plugin_root runtime_directory result)
    set(_root "${runtime_directory}")
    if (NOT IS_DIRECTORY "${runtime_directory}/RenderEngines")
        get_filename_component(_parent "${runtime_directory}" DIRECTORY)
        if (IS_DIRECTORY "${_parent}/RenderEngines")
            set(_root "${_parent}")
        endif ()
    endif ()
    set(${result} "${_root}" PARENT_SCOPE)
endfunction()

# Register an editor regression with the selected runtime environment.
function(virtoolsdev_add_test test_name test_target)
    add_test(NAME ${test_name} COMMAND ${test_target} ${ARGN})
    if(VIRTOOLSDEV_BALLANCED)
        set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
        return()
    endif()
    if (VIRTOOLS_RUNTIME_PATH AND EXISTS "${VIRTOOLS_RUNTIME_PATH}/CK2.dll")
        virtoolsdev_plugin_root("${VIRTOOLS_RUNTIME_PATH}" _root)
        set(_path "${VIRTOOLS_RUNTIME_PATH};${_root}/RenderEngines")
        string(REPLACE ";" "\\;" _path "${_path}")
        set_tests_properties(${test_name} PROPERTIES
                ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH};PATH=${_path}")
    endif ()
endfunction()

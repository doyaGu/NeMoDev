# Tests use the same source-built Ballanced runtime as the editor.
function(virtoolsdev_add_test test_name test_target)
    add_test(NAME ${test_name} COMMAND ${test_target} ${ARGN})
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
endfunction()

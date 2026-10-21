cmake_minimum_required(VERSION 3.25)

# NemoDev: a headless core (CK runtime, document, model, commands)
# and a Dear ImGui SDL3/GPU front end. Core tests link the core; UI tests
# build the editor itself with Dear ImGui Test Engine.
#
# Includes are written from this directory: "core/document/Document.h",
# "ui/app/EditorApp.h". tests/CheckLayering.cmake (CTest VirtoolsDevLayering)
# checks that, and that core includes no ui or ImGui header and only core
# includes a core/*/detail header.
project(NemoDev LANGUAGES C CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
if(APPLE)
    set(CMAKE_BUILD_RPATH "@loader_path")
    set(CMAKE_INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(CMAKE_BUILD_RPATH "$ORIGIN")
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type (Debug/Release)" FORCE)
endif ()

set(VIRTOOLSDEV_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# Images, interface data and fonts are included; optional help is supplied locally.
set(VIRTOOLSDEV_RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources" CACHE PATH
        "Virtools resources: images.txt, images/, data/ and fonts/")
get_filename_component(VIRTOOLSDEV_RESOURCES_DIR "${VIRTOOLSDEV_RESOURCES_DIR}" ABSOLUTE)
foreach(_resource images.txt data/interface.nmo)
    if(NOT EXISTS "${VIRTOOLSDEV_RESOURCES_DIR}/${_resource}")
        message(FATAL_ERROR "Missing local resource: ${VIRTOOLSDEV_RESOURCES_DIR}/${_resource}. "
                "Prepare resources as described in README.md, or set VIRTOOLSDEV_RESOURCES_DIR.")
    endif()
endforeach()
set(_help_archive "${VIRTOOLSDEV_RESOURCES_DIR}/data/documentation.chm")
if(NOT EXISTS "${_help_archive}")
    set(_help_archive "${CMAKE_CURRENT_SOURCE_DIR}/local-resources/data/documentation.chm")
endif()
set(VIRTOOLSDEV_HAS_HELP OFF)
if(EXISTS "${_help_archive}")
    set(VIRTOOLSDEV_HAS_HELP ON)
else()
    message(STATUS "CHM documentation not found; local help will not be deployed.")
endif()
# The editor and its tests share runtime/Bin for every configuration.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/runtime/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PROJECT_BINARY_DIR}/runtime/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PROJECT_BINARY_DIR}/runtime/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${PROJECT_BINARY_DIR}/runtime/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${PROJECT_BINARY_DIR}/runtime/Bin")
include(cmake/Ballanced.cmake)
include(cmake/ThirdPartyNotices.cmake)

option(VIRTOOLSDEV_BUILD_TESTS "Build NemoDev tests" OFF)
# Original regression compositions are provided locally, never redistributed.
set(VIRTOOLSDEV_SAMPLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/local-samples" CACHE PATH
        "The directory whose data/ holds the samples the tests open")
# Dear ImGui (submodule, docking branch) with the SDL3 and SDL GPU backends.
set(VIRTOOLSDEV_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
# Dear ImGui Test Engine (submodule; its own license, imgui_test_engine/LICENSE.txt):
# VirtoolsDevUiTests.exe, the editor driven through its UI (tests/ui).
set(VIRTOOLSDEV_IMGUI_TEST_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui_test_engine")
set(_ui_tests_default OFF)
if (VIRTOOLSDEV_BUILD_TESTS AND EXISTS "${VIRTOOLSDEV_IMGUI_TEST_ENGINE_DIR}/imgui_test_engine/imgui_te_engine.cpp")
    set(_ui_tests_default ON)
endif ()
option(VIRTOOLSDEV_BUILD_UI_TESTS "Build VirtoolsDevUiTests (Dear ImGui Test Engine)" ${_ui_tests_default})

if (NOT EXISTS "${VIRTOOLSDEV_IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui not found at ${VIRTOOLSDEV_IMGUI_DIR}. "
            "From the NemoDev repository root, run: git submodule update --init --recursive external/imgui")
endif ()
if (VIRTOOLSDEV_BUILD_UI_TESTS AND NOT EXISTS "${VIRTOOLSDEV_IMGUI_TEST_ENGINE_DIR}/imgui_test_engine/imgui_te_engine.cpp")
    message(FATAL_ERROR "Dear ImGui Test Engine not found at ${VIRTOOLSDEV_IMGUI_TEST_ENGINE_DIR} "
            "From the NemoDev repository root, run: git submodule update --init --recursive external/imgui_test_engine; "
            "or configure with -DVIRTOOLSDEV_BUILD_UI_TESTS=OFF.")
endif ()

include(cmake/RuntimeTargets.cmake)

# The images listed in images.txt are embedded in the editor. Interface data
# and the local help archive are copied beside the program.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} -c "import PIL" RESULT_VARIABLE _pillow_result)
if(NOT _pillow_result EQUAL 0)
    message(FATAL_ERROR "Resource compilation requires Pillow for ${Python3_EXECUTABLE}")
endif()
file(GLOB_RECURSE _resource_images CONFIGURE_DEPENDS "${VIRTOOLSDEV_RESOURCES_DIR}/images/*")
set(_compiled_images "${PROJECT_BINARY_DIR}/generated/ResourceImages.cpp")
add_custom_command(OUTPUT "${_compiled_images}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/GenerateResources.py"
        --root "${VIRTOOLSDEV_RESOURCES_DIR}" --output "${_compiled_images}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/GenerateResources.py"
        "${VIRTOOLSDEV_RESOURCES_DIR}/images.txt" ${_resource_images}
    VERBATIM)
add_library(VirtoolsDevResources STATIC "${_compiled_images}")
target_include_directories(VirtoolsDevResources PUBLIC "${VIRTOOLSDEV_SOURCE_DIR}")
target_compile_features(VirtoolsDevResources PUBLIC cxx_std_17)
# As core/runtime/Resources.h names them.
set(VIRTOOLSDEV_DATA_FILES "${VIRTOOLSDEV_RESOURCES_DIR}/data/interface.nmo")
set(_runtime_data "${PROJECT_BINARY_DIR}/runtime/Bin/data")
add_custom_target(VirtoolsDevData ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_runtime_data}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VIRTOOLSDEV_DATA_FILES} "${_runtime_data}"
    DEPENDS ${VIRTOOLSDEV_DATA_FILES}
    VERBATIM)
if(VIRTOOLSDEV_HAS_HELP)
    find_program(VIRTOOLSDEV_SEVEN_ZIP NAMES 7zz 7z 7za HINTS "C:/Program Files/7-Zip" REQUIRED)
    set(_help_output "${PROJECT_BINARY_DIR}/generated/help")
    add_custom_command(OUTPUT "${_help_output}/help.ndh" "${_help_output}/VirtoolsDevHelp.html"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/ExtractHelp.py"
            --archive "${_help_archive}"
            --seven-zip "${VIRTOOLSDEV_SEVEN_ZIP}" --output "${_help_output}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/ExtractHelp.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/HelpContent.py" "${_help_archive}"
        VERBATIM)
    add_custom_target(VirtoolsDevHelp
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_runtime_data}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_help_archive}" "${_runtime_data}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_help_output}" "${_runtime_data}/help"
        DEPENDS "${_help_output}/help.ndh" "${_help_archive}"
        VERBATIM)
    add_dependencies(VirtoolsDevData VirtoolsDevHelp)
endif()
add_dependencies(VirtoolsDevRuntime VirtoolsDevData)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/application.rc"
        "${PROJECT_BINARY_DIR}/generated/application.rc" @ONLY)
function(virtoolsdev_add_resources target)
    add_dependencies(${target} VirtoolsDevData)
    target_link_libraries(${target} PRIVATE VirtoolsDevResources)
    if(WIN32)
        target_sources(${target} PRIVATE "${PROJECT_BINARY_DIR}/generated/application.rc")
    endif()
endfunction()
add_subdirectory(core)
set(NEMODEV_HTML_BUILD_TESTS "${VIRTOOLSDEV_BUILD_TESTS}")
add_subdirectory(html)

if (VIRTOOLSDEV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

add_subdirectory(ui)

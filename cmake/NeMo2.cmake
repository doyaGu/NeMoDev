# NeMo2 owns engine assembly; NeMoDev owns application staging and packaging.
set(VIRTOOLS_SDK_PATH "" CACHE PATH "NeMo2 source checkout (required)")
if(NOT VIRTOOLS_SDK_PATH)
    message(FATAL_ERROR "Set VIRTOOLS_SDK_PATH to the NeMo2 source checkout.")
endif()
get_filename_component(VIRTOOLS_SDK_PATH "${VIRTOOLS_SDK_PATH}" ABSOLUTE)
if(NOT EXISTS "${VIRTOOLS_SDK_PATH}/cmake/NeMo2ComponentRegistry.cmake")
    message(FATAL_ERROR "VIRTOOLS_SDK_PATH must name a NeMo2 source checkout.")
endif()
foreach(_component VxMath CK2 RenderEngine Managers Plugins BuildingBlocks)
    if(NOT EXISTS "${VIRTOOLS_SDK_PATH}/Source/${_component}/CMakeLists.txt")
        message(FATAL_ERROR "VIRTOOLS_SDK_PATH must name a complete NeMo2 checkout: missing Source/${_component}.\n"
            "From the NeMo2 checkout, run: git submodule update --init --recursive")
    endif()
endforeach()
find_package(SDL3 3.4.20 CONFIG REQUIRED)
if(NOT TARGET SDL3::SDL3-shared)
    message(FATAL_ERROR "The NeMo2 runtime requires shared SDL3. "
        "Build and install SDL3 with SDL_SHARED=ON and SDL_STATIC=OFF, "
        "then set CMAKE_PREFIX_PATH to that installation prefix.")
endif()
install(IMPORTED_RUNTIME_ARTIFACTS SDL3::SDL3-shared
    RUNTIME DESTINATION Bin COMPONENT Runtime
    LIBRARY DESTINATION Bin COMPONENT Runtime)
function(virtoolsdev_add_nemo2)
    # SDK options belong to its subdirectories, not the editor and HTML tests.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(NEMO_BUILD_STATIC OFF)
    set(NEMO_BUILD_TESTS OFF)
    set(VXMATH_BUILD_PERF OFF)
    set(CKRE_BUILD_TOOLS OFF)
    set(CKRE_BUILD_SDL_GPU_RASTERIZER ON)
    set(CKBB_BUILD_ALL_MODULES ON)
    set(CKMANAGERS_BUILD_ALL ON)
    # Reuse prepared local fonts; the SDK can also populate its build cache.
    if(NOT DEFINED VXMATH_FONT_DIR AND EXISTS "${VIRTOOLSDEV_RESOURCES_DIR}/fonts/NotoSans-Regular.ttf")
        set(VXMATH_FONT_DIR "${VIRTOOLSDEV_RESOURCES_DIR}/fonts" CACHE PATH "Verified Noto font directory")
    endif()
    set(BUILD_TESTING OFF)
    add_subdirectory("${VIRTOOLS_SDK_PATH}" "NeMo2")
endfunction()
virtoolsdev_add_nemo2()
get_property(_runtime_manifest GLOBAL PROPERTY NEMO_RUNTIME_COMPONENT_MANIFEST)
if(NOT EXISTS "${_runtime_manifest}")
    message(FATAL_ERROR "NeMo2 did not publish its configured runtime components.")
endif()
include("${_runtime_manifest}")
foreach(_required CK2 VxMath CK2_3D CKSdlGpuRasterizer SdlInputManager SdlSoundManager ParameterOperations)
    if(NOT _required IN_LIST NEMO_RUNTIME_TARGETS)
        message(FATAL_ERROR "NeMoDev requires the NeMo2 runtime component ${_required}.")
    endif()
endforeach()
set(VIRTOOLS_SDK_INCLUDE_DIR "${VIRTOOLS_SDK_PATH}/Source/CK2/include")
set(VIRTOOLS_RUNTIME_PATH "${PROJECT_BINARY_DIR}/runtime/Bin")
file(STRINGS "${VIRTOOLS_SDK_INCLUDE_DIR}/CKDefines.h" _version_definitions
    REGEX "^#define (CKVERSION|DEVVERSION) 0x[0-9A-Fa-f]+")
foreach(_definition IN LISTS _version_definitions)
    string(REGEX REPLACE "^#define ([A-Z]+) (0x[0-9A-Fa-f]+).*" "\\1;\\2" _parts "${_definition}")
    list(GET _parts 0 _name)
    list(GET _parts 1 ${_name})
endforeach()
if(NOT CKVERSION OR NOT DEVVERSION)
    message(FATAL_ERROR "NeMo2 SDK must declare CKVERSION and DEVVERSION")
endif()
file(WRITE "${PROJECT_BINARY_DIR}/NeMo2Runtime.txt" "NeMo2SDK\n${CKVERSION}\n${DEVVERSION}\n")
install(FILES "${PROJECT_BINARY_DIR}/NeMo2Runtime.txt" DESTINATION Bin COMPONENT Runtime)

# All targets retain their component's install ownership. Stage Runtime only,
# after building its modules, for both the editor and the real-runtime tests.
# This is a development deployment, independent of DESTDIR used for packaging.
foreach(_target IN LISTS NEMO_RUNTIME_TARGETS)
    get_target_property(_type ${_target} TYPE)
    if(NOT _type STREQUAL "SHARED_LIBRARY" AND NOT _type STREQUAL "MODULE_LIBRARY")
        message(FATAL_ERROR "NeMoDev requires a shared NeMo2 runtime: ${_target} is ${_type}.")
    endif()
    if(APPLE)
        set_property(TARGET ${_target} PROPERTY INSTALL_RPATH "@loader_path/../Bin")
        # Stage must not rewrite the SDK build outputs in place on every install.
        set_property(TARGET ${_target} PROPERTY BUILD_WITH_INSTALL_RPATH TRUE)
    elseif(UNIX)
        set_property(TARGET ${_target} APPEND PROPERTY INSTALL_RPATH "$ORIGIN/../Bin")
    endif()
endforeach()
add_custom_target(VirtoolsDevRuntime ALL
    COMMAND ${CMAKE_COMMAND} -E env --unset=DESTDIR ${CMAKE_COMMAND}
        --install "${PROJECT_BINARY_DIR}" --prefix "${PROJECT_BINARY_DIR}/runtime" --component Runtime --config $<CONFIG>
    COMMAND ${CMAKE_COMMAND} -E env --unset=DESTDIR ${CMAKE_COMMAND}
        "-DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}/runtime"
        -P "${VIRTOOLSDEV_SOURCE_DIR}/cmake/InstallApplicationData.cmake"
    DEPENDS ${NEMO_RUNTIME_TARGETS}
    VERBATIM)

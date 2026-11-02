# Modules use only the public editor headers and the matching host's exports.
# They must not link a second ImGui implementation or native editor internals.
function(nemodev_add_module target)
    cmake_parse_arguments(MODULE "" "HOST" "SOURCES" ${ARGN})
    if(NOT MODULE_SOURCES OR MODULE_UNPARSED_ARGUMENTS OR MODULE_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "nemodev_add_module requires SOURCES <files> and optional HOST <drawing target>")
    endif()
    if(MODULE_HOST AND NOT TARGET "${MODULE_HOST}")
        message(FATAL_ERROR "Unknown module host: ${MODULE_HOST}")
    endif()
    add_library(${target} MODULE ${MODULE_SOURCES})
    target_include_directories(${target} PRIVATE "${VIRTOOLSDEV_SOURCE_DIR}" "${VIRTOOLSDEV_IMGUI_DIR}")
    target_compile_features(${target} PRIVATE cxx_std_17)
    if(MODULE_HOST)
        target_link_libraries(${target} PRIVATE ${MODULE_HOST})
    endif()
    set_target_properties(${target} PROPERTIES SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(WIN32 AND MODULE_HOST)
        target_compile_definitions(${target} PRIVATE "IMGUI_API=__declspec(dllimport)")
    endif()
endfunction()

# Installation is explicit: regression fixtures never enter the product.
function(nemodev_install_module target)
    install(TARGETS ${target} LIBRARY DESTINATION Bin/modules COMPONENT Application
            RUNTIME DESTINATION Bin/modules COMPONENT Application)
endfunction()

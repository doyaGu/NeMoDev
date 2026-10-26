cmake_minimum_required(VERSION 3.25)

if(NOT APPLE OR NOT IS_DIRECTORY "${APP}/Contents/MacOS")
    message(FATAL_ERROR "APP must name a macOS application bundle")
endif()

# Plugins are loaded by CK rather than linked by the executable. Keep their
# category directories while BundleUtilities fixes the complete dependency tree.
function(gp_item_default_embedded_path_override item result)
    if(item MATCHES "/Contents/PlugIns/([^/]+)$")
        set(${result} "@executable_path/../PlugIns" PARENT_SCOPE)
    elseif(item MATCHES "/Contents/PlugIns/([^/]+)/[^/]+$")
        set(${result} "@executable_path/../PlugIns/${CMAKE_MATCH_1}" PARENT_SCOPE)
    else()
        set(${result} "@executable_path/../Frameworks" PARENT_SCOPE)
    endif()
endfunction()

file(GLOB_RECURSE _plugins "${APP}/Contents/PlugIns/*.dylib" "${APP}/Contents/PlugIns/*.so")
set(BU_CHMOD_BUNDLE_ITEMS ON)
include(BundleUtilities)
fixup_bundle("${APP}" "${_plugins}" "${APP}/Contents/Frameworks")

# Virtools Dev's include rules (CTest VirtoolsDevLayering):
#  - a project header is included with its path from the repository's root
#    ("core/document/Document.h"), not by its name alone;
#  - core includes no ui header and no ImGui or D3D header (the tests link
#    the core alone);
#  - a core/<module>/detail header is the core's own: ui and the tests do
#    not include it;
#  - the test engine's headers (imgui_te_*) and tests/ headers are the UI
#    tests' (tests/ui): the editor itself includes neither.
# cmake -DVIRTOOLSDEV_SOURCE_DIR=<repository root> -P CheckLayering.cmake

if (NOT VIRTOOLSDEV_SOURCE_DIR)
    message(FATAL_ERROR "VIRTOOLSDEV_SOURCE_DIR not set")
endif ()
get_filename_component(VIRTOOLSDEV_SOURCE_DIR "${VIRTOOLSDEV_SOURCE_DIR}" ABSOLUTE)

file(GLOB_RECURSE _headers RELATIVE "${VIRTOOLSDEV_SOURCE_DIR}"
        "${VIRTOOLSDEV_SOURCE_DIR}/core/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/ui/*.h")
set(_header_names "")
foreach (_header IN LISTS _headers)
    get_filename_component(_name "${_header}" NAME)
    list(APPEND _header_names "${_name}")
endforeach ()

file(GLOB_RECURSE _sources RELATIVE "${VIRTOOLSDEV_SOURCE_DIR}"
        "${VIRTOOLSDEV_SOURCE_DIR}/core/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/core/*.cpp"
        "${VIRTOOLSDEV_SOURCE_DIR}/ui/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/ui/*.cpp"
        "${VIRTOOLSDEV_SOURCE_DIR}/tests/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/tests/*.cpp")

set(_errors "")
foreach (_source IN LISTS _sources)
    file(STRINGS "${VIRTOOLSDEV_SOURCE_DIR}/${_source}" _lines REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<]")
    foreach (_line IN LISTS _lines)
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[\"<]([^\">]+)[\">].*$" "\\1" _path "${_line}")
        get_filename_component(_name "${_path}" NAME)
        if (_path STREQUAL _name)
            list(FIND _header_names "${_name}" _found)
            if (NOT _found EQUAL -1)
                list(APPEND _errors "${_source}: #include \"${_path}\" without its path")
            endif ()
        endif ()
        if (_source MATCHES "^core/")
            if (_path MATCHES "^ui/" OR _path MATCHES "^imgui" OR _path MATCHES "^backends/"
                    OR _path MATCHES "^misc/cpp/" OR _path MATCHES "^d3d")
                list(APPEND _errors "${_source}: core includes ${_path}")
            endif ()
        elseif (_path MATCHES "^core/[^/]+/detail/")
            list(APPEND _errors "${_source}: includes the core's private ${_path}")
        endif ()
        if (NOT _source MATCHES "^tests/ui/" AND (_path MATCHES "^imgui_te_" OR _path MATCHES "^tests/"))
            list(APPEND _errors "${_source}: only the UI tests include ${_path}")
        endif ()
    endforeach ()
endforeach ()

# The editor owns its InterfaceChunk implementation. This must also configure
# in a checkout where the former submodule does not exist.
file(GLOB _build_helpers "${VIRTOOLSDEV_SOURCE_DIR}/cmake/*.cmake")
set(_build_files "${VIRTOOLSDEV_SOURCE_DIR}/CMakeLists.txt"
        "${VIRTOOLSDEV_SOURCE_DIR}/core/CMakeLists.txt"
        "${VIRTOOLSDEV_SOURCE_DIR}/ui/CMakeLists.txt"
        "${VIRTOOLSDEV_SOURCE_DIR}/tests/CMakeLists.txt" ${_build_helpers})
foreach (_build_file IN LISTS _build_files)
    file(READ "${_build_file}" _contents)
    if (_contents MATCHES "VirtoolsLoader::|VIRTOOLSDEV_LIBRARY_DIR|external/VirtoolsScriptDeobfuscation")
        list(APPEND _errors "${_build_file}: external InterfaceChunk build dependency")
    endif ()
endforeach ()
file(READ "${VIRTOOLSDEV_SOURCE_DIR}/.gitmodules" _submodules)
if (_submodules MATCHES "VirtoolsScriptDeobfuscation")
    list(APPEND _errors ".gitmodules: external InterfaceChunk submodule")
endif ()

if (_errors)
    list(JOIN _errors "\n  " _report)
    message(FATAL_ERROR "Include rules broken:\n  ${_report}")
endif ()
list(LENGTH _sources _count)
message(STATUS "Include rules hold in ${_count} files")

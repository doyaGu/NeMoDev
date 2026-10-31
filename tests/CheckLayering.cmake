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
        "${VIRTOOLSDEV_SOURCE_DIR}/core/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/ui/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/html/*.h"
        "${VIRTOOLSDEV_SOURCE_DIR}/editor/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/application/*.h"
        "${VIRTOOLSDEV_SOURCE_DIR}/workspace/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/features/*.h")
set(_header_names "")
foreach (_header IN LISTS _headers)
    get_filename_component(_name "${_header}" NAME)
    list(APPEND _header_names "${_name}")
endforeach ()

file(GLOB_RECURSE _sources RELATIVE "${VIRTOOLSDEV_SOURCE_DIR}"
        "${VIRTOOLSDEV_SOURCE_DIR}/core/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/core/*.cpp"
        "${VIRTOOLSDEV_SOURCE_DIR}/ui/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/ui/*.cpp"
        "${VIRTOOLSDEV_SOURCE_DIR}/html/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/html/*.cpp"
        "${VIRTOOLSDEV_SOURCE_DIR}/tests/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/tests/*.cpp" "${VIRTOOLSDEV_SOURCE_DIR}/tests/*.c"
        "${VIRTOOLSDEV_SOURCE_DIR}/editor/*.h"
        "${VIRTOOLSDEV_SOURCE_DIR}/application/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/application/*.cpp"
        "${VIRTOOLSDEV_SOURCE_DIR}/workspace/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/workspace/*.cpp"
        "${VIRTOOLSDEV_SOURCE_DIR}/features/*.h" "${VIRTOOLSDEV_SOURCE_DIR}/features/*.cpp"
        "${VIRTOOLSDEV_SOURCE_DIR}/core/*.mm" "${VIRTOOLSDEV_SOURCE_DIR}/ui/*.mm"
        "${VIRTOOLSDEV_SOURCE_DIR}/html/*.mm" "${VIRTOOLSDEV_SOURCE_DIR}/tests/*.mm"
        "${VIRTOOLSDEV_SOURCE_DIR}/application/*.mm" "${VIRTOOLSDEV_SOURCE_DIR}/workspace/*.mm"
        "${VIRTOOLSDEV_SOURCE_DIR}/features/*.mm")

set(_errors "")
foreach (_source IN LISTS _sources)
    file(STRINGS "${VIRTOOLSDEV_SOURCE_DIR}/${_source}" _lines REGEX "^[ \t]*#[ \t]*(include|import)[ \t]*[\"<]")
    foreach (_line IN LISTS _lines)
        string(REGEX REPLACE "^[ \t]*#[ \t]*(include|import)[ \t]*[\"<]([^\">]+)[\">].*$" "\\2" _path "${_line}")
        get_filename_component(_name "${_path}" NAME)
        if (_path STREQUAL _name)
            list(FIND _header_names "${_name}" _found)
            if (NOT _found EQUAL -1)
                list(APPEND _errors "${_source}: #include \"${_path}\" without its path")
            endif ()
        endif ()
        if(_source MATCHES "^editor/" AND NOT _source STREQUAL "editor/ImGuiDrawing.h")
            if(NOT _path MATCHES "^(stdint\\.h|editor/(Identity|Editor)\\.h)$")
                list(APPEND _errors "${_source}: public editor interface includes ${_path}")
            endif()
        endif()
        if(_source MATCHES "^application/" AND _path MATCHES "^(core/|ui/|features/|workspace/|CK|Vx|SDL|imgui|backends/)" AND
                NOT (_source STREQUAL "application/EditorApplication.cpp" AND
                     _path MATCHES "^core/(document/(Document|Edit)|runtime/TextEncoding)\\.h$") AND
                NOT (_source STREQUAL "application/ObjectSelection.cpp" AND
                     _path STREQUAL "core/document/Document.h") AND
                NOT (_source STREQUAL "application/EventLog.h" AND
                     _path STREQUAL "core/runtime/Log.h") AND
                NOT (_source STREQUAL "application/DocumentFiles.h" AND
                     _path MATCHES "^core/(document/(Document|DocumentSaver|LinkValidation)|runtime/CkRuntimeHost)\\.h$") AND
                NOT (_source STREQUAL "application/DocumentFiles.cpp" AND
                     _path MATCHES "^core/(document/(ToolCommands|ExportCommands|FileCommands|OutputFiles)|runtime/TextEncoding)\\.h$") AND
                NOT (_source STREQUAL "application/OutputCommands.cpp" AND
                     _path STREQUAL "core/document/FileCommands.h") AND
                NOT (_source STREQUAL "application/ExecutionCommands.cpp" AND
                     _path MATCHES "^core/(document/Document|graph/(Debugger|RuntimeProfile)|runtime/TextEncoding)\\.h$") AND
                NOT (_source STREQUAL "application/DocumentCommands.cpp" AND
                     _path MATCHES "^core/(document/FileCommands|runtime/TextEncoding)\\.h$"))
            list(APPEND _errors "${_source}: editor commands include ${_path}")
        endif()
        if((_source MATCHES "^workspace/" OR _source MATCHES "^features/objects/ObjectViews") AND
                _path MATCHES "^(core/|ui/|CK|Vx|SDL)")
            list(APPEND _errors "${_source}: object views include native or legacy editor state ${_path}")
        endif()
        if(_source MATCHES "^features/scene/" AND NOT _source MATCHES "^features/scene/Scene(Commands|Queries)\\.(h|cpp)$" AND
                (_path MATCHES "^(core/|CK|Vx|SDL)" OR
                 (_path MATCHES "^ui/" AND NOT _path MATCHES "^ui/controls/(DevBitmaps|DevControls|DevTheme|TypeAhead)\\.h$")))
            list(APPEND _errors "${_source}: scene view includes native or legacy editor state ${_path}")
        endif()
        if(_source MATCHES "^features/runtime/" AND
                (_path MATCHES "^(core/|CK|Vx|SDL)" OR
                 (_path MATCHES "^ui/" AND NOT _path MATCHES "^ui/controls/(DevBitmaps|DevControls|DevTheme)\\.h$")))
            list(APPEND _errors "${_source}: runtime view includes native or legacy editor state ${_path}")
        endif()
        if(_source MATCHES "^features/environment/" AND NOT _source MATCHES "^features/environment/EnvironmentCommands\\.(h|cpp)$" AND
                (_path MATCHES "^(core/|CK|Vx|SDL)" OR
                 (_path MATCHES "^ui/" AND NOT _path MATCHES "^ui/controls/(DevBitmaps|DevControls|DevTheme|TypeAhead)\\.h$")))
            list(APPEND _errors "${_source}: environment view includes native or legacy editor state ${_path}")
        endif()
        if(_source MATCHES "^features/diagnostics/" AND NOT _source MATCHES "^features/diagnostics/DiagnosticCommands\\.(h|cpp)$" AND
                (_path MATCHES "^(core/|CK|Vx|SDL)" OR
                 (_path MATCHES "^ui/" AND NOT _path MATCHES "^ui/controls/(DevControls|DevTheme|TypeAhead)\\.h$")))
            list(APPEND _errors "${_source}: diagnostic view includes native or legacy editor state ${_path}")
        endif()
        if(_source MATCHES "^features/values/" AND NOT _source MATCHES "^features/values/ParameterCommands\\.(h|cpp)$" AND
                _path MATCHES "^(CK|Vx|SDL|ui/app/EditorState|core/(document|runtime|graph|level|layout))")
            list(APPEND _errors "${_source}: value editor includes native state ${_path}")
        endif()
        if(_source MATCHES "^features/defaults/" AND _path MATCHES "^(core/|ui/|CK|Vx|SDL)")
            list(APPEND _errors "${_source}: default workspace includes native or legacy editor state ${_path}")
        endif()
        if(_source MATCHES "^tests/modules/" AND _path MATCHES "^(core/|ui/|application/|workspace/|features/|CK|Vx|SDL|imgui_internal)")
            list(APPEND _errors "${_source}: external module includes private editor code ${_path}")
        endif()
        if(_source MATCHES "^features/resources/" AND NOT _source MATCHES "^features/resources/(BuildingBlockQueries|DataResourceCommands)\\.(h|cpp)$" AND
                (_path MATCHES "^(core/|CK|Vx|SDL)" OR
                 (_path MATCHES "^ui/" AND NOT _path MATCHES "^ui/controls/(DevBitmaps|DevControls|DevTheme|TypeAhead)\\.h$")))
            list(APPEND _errors "${_source}: resource view includes native or legacy editor state ${_path}")
        endif()
        if (_source MATCHES "^core/")
            if (_path MATCHES "^ui/" OR _path MATCHES "^html/" OR _path MATCHES "^imgui" OR _path MATCHES "^backends/"
                    OR _path MATCHES "^misc/cpp/" OR _path MATCHES "^d3d")
                list(APPEND _errors "${_source}: core includes ${_path}")
            endif ()
        elseif (_path MATCHES "^core/[^/]+/detail/")
            list(APPEND _errors "${_source}: includes the core's private ${_path}")
        endif ()
        if (_source MATCHES "^html/" AND (_path MATCHES "^core/|^ui/|^imgui|^SDL|^CK|^Vx|^windows\\.h|^d3d"))
            list(APPEND _errors "${_source}: HTML rendering includes application or platform header ${_path}")
        endif ()
        if (NOT _source MATCHES "^html/" AND (_path MATCHES "^html/detail/|^litehtml"))
            list(APPEND _errors "${_source}: includes HTML rendering internals ${_path}")
        endif ()
        if (_source STREQUAL "html/HtmlDocument.h" AND _path MATCHES "^litehtml|^html/detail/")
            list(APPEND _errors "${_source}: public HTML interface exposes ${_path}")
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

# NeMoDev: the Dear ImGui front end on VirtoolsDevCore. Optional UI regressions
# build the same editor with Dear ImGui Test Engine and the tests of tests/ui.

set(_imgui_sources
        ${VIRTOOLSDEV_IMGUI_DIR}/imgui.cpp
        ${VIRTOOLSDEV_IMGUI_DIR}/imgui_draw.cpp
        ${VIRTOOLSDEV_IMGUI_DIR}/imgui_tables.cpp
        ${VIRTOOLSDEV_IMGUI_DIR}/imgui_widgets.cpp
        ${VIRTOOLSDEV_IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
        ${VIRTOOLSDEV_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${VIRTOOLSDEV_IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp)

add_library(VirtoolsDevImGui STATIC ${_imgui_sources})
target_include_directories(VirtoolsDevImGui PUBLIC ${VIRTOOLSDEV_IMGUI_DIR} ${VIRTOOLSDEV_IMGUI_DIR}/backends)
target_compile_features(VirtoolsDevImGui PUBLIC cxx_std_17)
target_link_libraries(VirtoolsDevImGui PUBLIC SDL3::SDL3)
set_target_properties(VirtoolsDevImGui PROPERTIES FOLDER "VirtoolsDev")

set(_editor_sources
        # The program: window, main loop, menus, state, preferences, capture.
        app/Main.cpp
        app/CommandLine.cpp
        app/EditorApp.cpp
        app/FileMenu.cpp
        app/StatusBar.cpp
        app/DuplicateNames.cpp
        app/Preferences.cpp
        app/DesktopFiles.cpp
        help/HelpBook.cpp
        help/HelpWindow.cpp
        render/UiRenderer.cpp
        render/ImageFiles.cpp
        render/HtmlRenderer.cpp
        # Dev's look and the shared widgets.
        controls/DevTheme.cpp
        controls/DevControls.cpp
        controls/DevIcons.cpp
        controls/DevBitmaps.cpp
        controls/FileDialogs.cpp
        controls/ValueEditor.cpp
        controls/ParameterBox.cpp
        controls/TypeAhead.cpp
        controls/BehaviorInfoDialog.cpp
        # The Schematic.
        schematic/SchematicCanvas.cpp
        schematic/SchematicInput.cpp
        schematic/SchematicDialogs.cpp
        schematic/SchematicMenus.cpp
        schematic/SchematicLinking.cpp
        schematic/SchematicPrint.cpp
        schematic/SchematicRenderer.cpp
        schematic/Snapshots.cpp
        # The 3D Layout.
        layout/LayoutWindow.cpp
        layout/LayoutInput.cpp
        layout/LayoutMenu.cpp
        layout/LayoutDrop.cpp
        layout/LayoutExplorer.cpp
        layout/LayoutSnapshot.cpp
        layout/LayoutFullScreen.cpp
        # Docked panels and managers.
        panels/EditorPanels.cpp
        panels/LevelManager.cpp
        panels/DebuggerPanel.cpp
        panels/ManagerWindows.cpp
        panels/HierarchyManager.cpp
        # The Setups.
        setup/SetupWindows.cpp
        setup/SetupRenderPages.cpp
        setup/SetupScenePages.cpp
        setup/SetupObjectPages.cpp
        # The Resources, Editors, Options and Help windows.
        tools/ToolWindows.cpp
        tools/GeneralPreferences.cpp
        tools/DataResources.cpp)

if(WIN32)
    list(APPEND _editor_sources print/PrinterWindows.cpp)
elseif(APPLE)
    enable_language(OBJCXX)
    list(APPEND _editor_sources print/PrinterMac.mm print/PrintDocument.cpp)
    set_source_files_properties(print/PrinterMac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTKPRINT REQUIRED IMPORTED_TARGET gtk+-unix-print-3.0 gio-unix-2.0)
    list(APPEND _editor_sources print/PrinterLinux.cpp print/PrintDocument.cpp)
endif()
if(NOT WIN32)
    find_path(VIRTOOLSDEV_HARU_INCLUDE_DIR hpdf.h REQUIRED)
    find_library(VIRTOOLSDEV_HARU_LIBRARY NAMES hpdf libhpdf REQUIRED)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT VIRTOOLSDEV_HARU_LIBRARY MATCHES "\\.a$")
        # Preserve the SONAME links, rebased beside the real shared library.
        install(CODE "file(INSTALL \"${VIRTOOLSDEV_HARU_LIBRARY}\"
                DESTINATION \"\${CMAKE_INSTALL_PREFIX}/Bin\" FOLLOW_SYMLINK_CHAIN)"
                COMPONENT Runtime)
    endif()
endif()

# The editor as target, on the Dear ImGui library imgui, with the sources after it.
function(virtoolsdev_add_editor target imgui)
    add_executable(${target} WIN32 ${_editor_sources} ${ARGN})
    # LinkRouting.h and SchematicGrid.h: the canvas routes links on Dev's grid.
    target_link_libraries(${target} PRIVATE VirtoolsDevCore NeMoDevHtml ${imgui} SDL3::SDL3 stb)
    add_dependencies(${target} VirtoolsDevRuntime)
    if(WIN32)
        target_link_libraries(${target} PRIVATE comdlg32 shell32 ole32)
    else()
        target_include_directories(${target} PRIVATE "${VIRTOOLSDEV_HARU_INCLUDE_DIR}")
        target_link_libraries(${target} PRIVATE "${VIRTOOLSDEV_HARU_LIBRARY}")
        if(APPLE)
            target_link_libraries(${target} PRIVATE "-framework AppKit")
        else()
            target_link_libraries(${target} PRIVATE PkgConfig::GTKPRINT)
        endif()
    endif()
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    # Dev's bitmaps and icons, interface.nmo and optional local help.
    virtoolsdev_add_resources(${target})
    set_target_properties(${target} PROPERTIES FOLDER "VirtoolsDev"
            VS_DEBUGGER_ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
endfunction()

virtoolsdev_add_editor(VirtoolsDev VirtoolsDevImGui)
target_compile_definitions(VirtoolsDev PRIVATE VIRTOOLSDEV_PRODUCTION)
set_target_properties(VirtoolsDev PROPERTIES OUTPUT_NAME NeMoDev)
install(TARGETS VirtoolsDev RUNTIME DESTINATION Bin COMPONENT Application)
install(SCRIPT "${VIRTOOLSDEV_SOURCE_DIR}/cmake/InstallApplicationData.cmake" COMPONENT Application)
install(FILES ${VIRTOOLSDEV_DATA_FILES} DESTINATION data COMPONENT Application)
if(VIRTOOLSDEV_HAS_HELP)
    install(FILES "${_help_archive}" DESTINATION data COMPONENT Application)
    install(DIRECTORY "${_help_output}/" DESTINATION data/help COMPONENT Application)
endif()

if(VIRTOOLSDEV_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME NeMoDevInstalledRuntime COMMAND ${Python3_EXECUTABLE}
            "${VIRTOOLSDEV_SOURCE_DIR}/tests/InstalledRuntimeTests.py"
            --build "${PROJECT_BINARY_DIR}" --cmake "${CMAKE_COMMAND}" --config "$<CONFIG>"
            --runtime-check "$<TARGET_FILE:NeMoDevRuntimeLayoutTests>"
            --binary-name "$<TARGET_FILE_NAME:VirtoolsDev>" --haru-library "${VIRTOOLSDEV_HARU_LIBRARY}")
    set_tests_properties(NeMoDevInstalledRuntime PROPERTIES RUN_SERIAL TRUE TIMEOUT 180 LABELS self-contained)
endif()

if (VIRTOOLSDEV_BUILD_UI_TESTS)
    # Dear ImGui with the test engine's hooks (IMGUI_ENABLE_TEST_ENGINE, from
    # imgui_te_imconfig.h) and the engine, its coroutine on a std::thread.
    set(_engine_dir "${VIRTOOLSDEV_IMGUI_TEST_ENGINE_DIR}/imgui_test_engine")
    add_library(VirtoolsDevImGuiTestEngine STATIC ${_imgui_sources}
            ${_engine_dir}/imgui_capture_tool.cpp
            ${_engine_dir}/imgui_te_context.cpp
            ${_engine_dir}/imgui_te_coroutine.cpp
            ${_engine_dir}/imgui_te_engine.cpp
            ${_engine_dir}/imgui_te_exporters.cpp
            ${_engine_dir}/imgui_te_perftool.cpp
            ${_engine_dir}/imgui_te_ui.cpp
            ${_engine_dir}/imgui_te_utils.cpp)
    target_include_directories(VirtoolsDevImGuiTestEngine PUBLIC ${VIRTOOLSDEV_IMGUI_DIR}
            ${VIRTOOLSDEV_IMGUI_DIR}/backends ${VIRTOOLSDEV_IMGUI_TEST_ENGINE_DIR} ${_engine_dir})
    target_compile_definitions(VirtoolsDevImGuiTestEngine PUBLIC IMGUI_USER_CONFIG="imgui_te_imconfig.h"
            IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)
    target_compile_definitions(VirtoolsDevImGuiTestEngine PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_features(VirtoolsDevImGuiTestEngine PUBLIC cxx_std_17)
    target_link_libraries(VirtoolsDevImGuiTestEngine PUBLIC SDL3::SDL3)
    set_target_properties(VirtoolsDevImGuiTestEngine PROPERTIES FOLDER "VirtoolsDev")

    virtoolsdev_add_editor(VirtoolsDevUiTests VirtoolsDevImGuiTestEngine
            app/EditorStartup.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/UiTestRun.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/UiTests.h
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/UiTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/AppTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/FileMenuTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/SchematicTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/LayoutTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/LevelTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/SetupTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/HierarchyTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/BlocksTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/AttributesTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/CreateTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/SoundTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/CharacterTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/DebuggerTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/ToolsTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/PrintTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/PlayerTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/HelpTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/HtmlTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/DesktopTests.cpp)
    if(WIN32)
        target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/WindowsDialogTests.cpp)
    endif()
    target_compile_definitions(VirtoolsDevUiTests PRIVATE
            VIRTOOLSDEV_DIALOG_DRIVER="${VIRTOOLSDEV_SOURCE_DIR}/tests/NativeDialogDriver.py")
    target_link_libraries(VirtoolsDevUiTests PRIVATE ${CMAKE_DL_LIBS})
    target_compile_definitions(VirtoolsDevUiTests PRIVATE VIRTOOLSDEV_UI_TESTS)
    # A console program with the editor's SDL main and results on stdout.
    set_target_properties(VirtoolsDevUiTests PROPERTIES WIN32_EXECUTABLE FALSE)
endif ()

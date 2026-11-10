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
        native/EditorSession.cpp
        native/EditorProfileStorage.cpp
        app/FileInputs.cpp
        app/OutputInputs.cpp
        app/EnvironmentInputs.cpp
        app/MessageDialogs.cpp
        app/StatusBar.cpp
        app/DuplicateNames.cpp
        app/DesktopFiles.cpp
        help/HelpBook.cpp
        help/HelpWindow.cpp
        render/UiRenderer.cpp
        render/HtmlRenderer.cpp
        # Dev's look and the shared widgets.
        controls/DevTheme.cpp
        controls/UiAppearance.cpp
        controls/DevControls.cpp
        controls/DevIcons.cpp
        controls/DevBitmaps.cpp
        controls/FileDialogs.cpp
        controls/ValueEditor.cpp
        controls/ParameterBox.cpp
        controls/ParameterDialog.cpp
        native/LayoutRenderer.cpp
        native/LayoutDocument.cpp
        native/ParameterCopies.cpp
        native/ParameterDialogs.cpp
        controls/ObjectArrayInput.cpp
        controls/TypeAhead.cpp
        controls/BehaviorInfoFields.cpp
        # The Schematic.
        schematic/SchematicCanvas.cpp
        schematic/SchematicViews.cpp schematic/SchematicTools.cpp
        schematic/SchematicInput.cpp
        schematic/SchematicDialogs.cpp
        schematic/SchematicParameterDialog.cpp
        schematic/SchematicMenus.cpp
        schematic/SchematicLinking.cpp
        schematic/SchematicPrint.cpp
        schematic/SchematicRenderer.cpp
        schematic/Snapshots.cpp
        # The 3D Layout.
        layout/LayoutWindow.cpp
        layout/LayoutWindows.cpp layout/LayoutControls.cpp
        layout/LayoutInput.cpp
        layout/LayoutMenu.cpp
        layout/LayoutDrop.cpp
        layout/LayoutExplorer.cpp
        layout/LayoutSnapshot.cpp
        ../features/layout/SnapshotData.cpp
        layout/LayoutFullScreen.cpp
        # Docked panels and managers.
        panels/EditorPanels.cpp
        panels/EventLogView.cpp
        ../features/scene/SceneViewData.cpp
        ../features/arrays/ArrayTable.cpp
        ../features/values/ValueEditors.cpp
        ../features/values/SpecializedValueEditors.cpp
        ../features/values/SelectionValueEditors.cpp
        ../features/scene/SceneTreeView.cpp
        ../features/scene/AttributeView.cpp
        ../features/resources/BuildingBlockViews.cpp
        ../features/resources/DataResourceViews.cpp
        ../features/defaults/DefaultWorkspace.cpp
        ../features/runtime/RuntimeViews.cpp
        ../features/runtime/DebuggerView.cpp
        ../features/runtime/ProfilerView.cpp
        ../features/runtime/ParameterDebuggerView.cpp
        ../features/environment/EnvironmentViews.cpp
        ../features/environment/PluginView.cpp
        ../features/environment/PathView.cpp
        ../features/environment/LinkOptionsView.cpp
        ../features/diagnostics/DiagnosticViews.cpp
        # The Setups.
        setup/SetupWindows.cpp
        setup/SetupLayout.cpp
        ../features/setup/SetupViewData.cpp
        setup/SetupRenderPages.cpp
        setup/SetupScenePages.cpp
        setup/SetupObjectPages.cpp
        # The Resources, Editors, Options and Help windows.
        ../features/preferences/PreferencesView.cpp
        ../features/layout/LayoutSceneData.cpp
        ../features/schematic/SchematicDiagnosticViews.cpp)

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
find_package(Threads REQUIRED)
function(virtoolsdev_add_editor target imgui)
    add_library(${target}Workspace STATIC "${VIRTOOLSDEV_SOURCE_DIR}/workspace/EditorWorkspace.cpp" "${VIRTOOLSDEV_SOURCE_DIR}/workspace/WorkspaceState.cpp" "${VIRTOOLSDEV_SOURCE_DIR}/workspace/ToolMenus.cpp")
    target_link_libraries(${target}Workspace PRIVATE NeMoDevEditorApplication ${imgui})
    target_include_directories(${target}Workspace PUBLIC "${VIRTOOLSDEV_SOURCE_DIR}")
    add_library(${target}ObjectViews STATIC "${VIRTOOLSDEV_SOURCE_DIR}/features/objects/ObjectViews.cpp")
    target_link_libraries(${target}ObjectViews PRIVATE NeMoDevEditorApplication ${imgui})
    target_include_directories(${target}ObjectViews PUBLIC "${VIRTOOLSDEV_SOURCE_DIR}")
    add_executable(${target} WIN32 ${_editor_sources} ${ARGN})
    # LinkRouting.h and SchematicGrid.h: the canvas routes links on Dev's grid.
    target_link_libraries(${target} PRIVATE ${target}Workspace ${target}ObjectViews NeMoDevObjectCommands NeMoDevEditorApplication VirtoolsDevCore NeMoDevHtml ${imgui} SDL3::SDL3 stb Threads::Threads)
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
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/ModuleTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/ViewRoutingTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/WorkspaceTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/ValueEditTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/ArrayEditTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/AppearanceTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/ArchitectureBaselineTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/GpuViewportTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/LayoutViewportTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/ViewportBaselineTests.cpp
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
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/ToolsTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/EnvironmentTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/DiagnosticTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/PreferencesTests.cpp
            ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/FrameQueryTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/PrintTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/PlayerTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/LayoutDropTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/HelpTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/WorkspaceLayoutTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/HtmlTests.cpp)
    target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/DesktopTests.cpp)
    if(WIN32)
        target_sources(VirtoolsDevUiTests PRIVATE ${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/WindowsDialogTests.cpp)
    endif()
    target_compile_definitions(VirtoolsDevUiTests PRIVATE
            VIRTOOLSDEV_DIALOG_DRIVER="${VIRTOOLSDEV_SOURCE_DIR}/tests/NativeDialogDriver.py")
    target_link_libraries(VirtoolsDevUiTests PRIVATE ${CMAKE_DL_LIBS})
    target_compile_definitions(VirtoolsDevUiTests PRIVATE VIRTOOLSDEV_UI_TESTS)
    target_include_directories(VirtoolsDevUiTests PRIVATE "${VIRTOOLS_SDK_PATH}/Source/RenderEngine/include")
    # A console program with the editor's SDL main and results on stdout.
    if(APPLE)
        target_sources(VirtoolsDevUiTests PRIVATE "${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/UiTestStartupMac.mm")
        set_source_files_properties("${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/UiTestStartupMac.mm"
                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_sources(VirtoolsDevUiTests PRIVATE "${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/ViewportSchedulingMac.mm")
        set_source_files_properties("${VIRTOOLSDEV_SOURCE_DIR}/tests/ui/ViewportSchedulingMac.mm"
                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(VirtoolsDevUiTests PRIVATE "-framework Foundation")
    endif()
    set_target_properties(VirtoolsDevUiTests PROPERTIES WIN32_EXECUTABLE FALSE)
endif ()

# Modules use the running editor's ImGui implementation. They never link a
# second copy. The module import library is specific to its matching host on Windows.
foreach(_host IN ITEMS VirtoolsDev VirtoolsDevUiTests)
    if(TARGET ${_host})
        set_target_properties(${_host} PROPERTIES ENABLE_EXPORTS TRUE)
    endif()
endforeach()
if(WIN32)
    target_compile_definitions(VirtoolsDevImGui PRIVATE "IMGUI_API=__declspec(dllexport)")
    if(TARGET VirtoolsDevImGuiTestEngine)
        target_compile_definitions(VirtoolsDevImGuiTestEngine PRIVATE "IMGUI_API=__declspec(dllexport)")
    endif()
endif()
option(VIRTOOLSDEV_BUILD_EXAMPLES "Build and install public editor module examples" OFF)
if(VIRTOOLSDEV_BUILD_EXAMPLES OR VIRTOOLSDEV_BUILD_TESTS)
    nemodev_add_module(NeMoDevDocumentInfo HOST VirtoolsDev
            SOURCES "${VIRTOOLSDEV_SOURCE_DIR}/examples/DocumentInfo/DocumentInfo.cpp")
    if(NOT VIRTOOLSDEV_BUILD_EXAMPLES)
        set_target_properties(NeMoDevDocumentInfo PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endif()
if(VIRTOOLSDEV_BUILD_EXAMPLES)
    if(APPLE)
        set(_module_output "$<TARGET_FILE_DIR:VirtoolsDev>/../PlugIns/EditorModules")
    else()
        set(_module_output "$<TARGET_FILE_DIR:VirtoolsDev>/modules")
    endif()
    set_target_properties(NeMoDevDocumentInfo PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${_module_output}" RUNTIME_OUTPUT_DIRECTORY "${_module_output}")
    nemodev_install_module(NeMoDevDocumentInfo)
endif()
if(VIRTOOLSDEV_BUILD_TESTS)
    add_executable(NeMoDevEditorTests ../tests/EditorApplicationTests.cpp ../tests/EditorToolTests.cpp ../tests/EditorAppearanceTests.cpp ../tests/DocumentCommandTests.cpp ../tests/ObjectCommandTests.cpp ../tests/ResourceCommandTests.cpp ../tests/ExecutionCommandTests.cpp ../tests/DocumentTransitionTests.cpp ../tests/OutputCommandTests.cpp ../tests/WorkspaceTests.cpp ../tests/WorkspacePersistenceTests.cpp ../tests/EventLogTests.cpp ../tests/SceneCommandTests.cpp ../tests/BuildingBlockQueryTests.cpp ../tests/DataResourceTests.cpp ../tests/ArrayEditTests.cpp ../tests/SetupEditTests.cpp ../tests/SchematicEditTests.cpp ../tests/ParameterEditTests.cpp ../tests/SpecializedParameterEditTests.cpp ../tests/SelectionParameterEditTests.cpp ../features/resources/DataResourceViews.cpp)
    target_link_libraries(NeMoDevEditorTests PRIVATE VirtoolsDevWorkspace NeMoDevObjectCommands VirtoolsDevCore NeMoDevEditorApplication "$<LINK_LIBRARY:WHOLE_ARCHIVE,VirtoolsDevImGui>")
    target_sources(NeMoDevEditorTests PRIVATE ../tests/RuntimeProjectionTests.cpp)
    target_sources(NeMoDevEditorTests PRIVATE ../tests/MessageCommandTests.cpp)
    target_sources(NeMoDevEditorTests PRIVATE ../tests/EnvironmentCommandTests.cpp)
    target_sources(NeMoDevEditorTests PRIVATE ../tests/DiagnosticCommandTests.cpp ../tests/PreferencesCommandTests.cpp ../tests/ProfileCommandTests.cpp)
    target_compile_definitions(NeMoDevEditorTests PRIVATE VIRTOOLSDEV_PREFERENCES_FIXTURES="${VIRTOOLSDEV_SOURCE_DIR}/tests/fixtures/preferences")
    target_sources(NeMoDevEditorTests PRIVATE ../tests/LayoutPresentationTests.cpp ../tests/CanvasCommandTests.cpp ../tests/GestureCommandTests.cpp ../tests/GraphEditingTests.cpp ../tests/SchematicAuthoringTests.cpp ../tests/SchematicProjectionTests.cpp ../tests/SchematicScriptCommandTests.cpp ../tests/GraphFileCommandTests.cpp ../features/layout/LayoutSceneData.cpp ../features/layout/SnapshotData.cpp)
    target_sources(NeMoDevEditorTests PRIVATE ../tests/LayoutAuthoringTests.cpp ../tests/SetupProjectionTests.cpp ../tests/SetupPageTests.cpp ../features/setup/SetupViewData.cpp ../features/scene/SceneViewData.cpp)
    target_sources(NeMoDevEditorTests PRIVATE ../tests/SchematicPickingTests.cpp controls/DevTheme.cpp)
    target_sources(NeMoDevEditorTests PRIVATE ../tests/LayoutViewportTests.cpp)
    set_target_properties(NeMoDevEditorTests PROPERTIES ENABLE_EXPORTS TRUE)
    nemodev_add_module(NeMoDevEditorModule HOST NeMoDevEditorTests SOURCES ../tests/modules/ObjectModule.cpp)
    add_library(NeMoDevEnvironmentPlugin MODULE ../tests/EnvironmentPlugin.cpp)
    target_link_libraries(NeMoDevEnvironmentPlugin PRIVATE VirtoolsDevCore)
    set_target_properties(NeMoDevEnvironmentPlugin PROPERTIES SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}")
    nemodev_add_module(NeMoDevGestureModule SOURCES ../tests/modules/GestureModule.cpp)
    nemodev_add_module(NeMoDevToolModule SOURCES ../tests/modules/ToolModule.cpp)
    add_executable(NeMoDevUiAppearanceTests ../tests/UiAppearanceTests.cpp
            controls/UiAppearance.cpp controls/DevTheme.cpp controls/DevBitmaps.cpp render/UiRenderer.cpp setup/SetupLayout.cpp)
    target_include_directories(NeMoDevUiAppearanceTests PRIVATE "${VIRTOOLSDEV_SOURCE_DIR}")
    target_link_libraries(NeMoDevUiAppearanceTests PRIVATE NeMoDevEditorApplication VirtoolsDevCore VirtoolsDevImGui SDL3::SDL3)
    virtoolsdev_add_resources(NeMoDevUiAppearanceTests)
    add_test(NAME NeMoDevUiAppearance COMMAND NeMoDevUiAppearanceTests)
    set_tests_properties(NeMoDevUiAppearance PROPERTIES LABELS self-contained)
    nemodev_add_module(NeMoDevAppearanceModule SOURCES ../tests/modules/AppearanceModule.cpp)
    if(TARGET VirtoolsDevUiTests)
        add_test(NAME VirtoolsDevUi.DocumentViews.failed_new_keeps_both_viewports_and_setup COMMAND VirtoolsDevUiTests
                --ui-tests "^documentviews$" --ui-results "${PROJECT_BINARY_DIR}/virtoolsdev-ui/document-views.xml")
        set_tests_properties(VirtoolsDevUi.DocumentViews.failed_new_keeps_both_viewports_and_setup
                PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 60)
        add_test(NAME VirtoolsDevUi.PlayerState.unavailable_fullscreen_keeps_both_viewports COMMAND VirtoolsDevUiTests
                --ui-tests unavailable_fullscreen_keeps_both_viewports
                --ui-results "${PROJECT_BINARY_DIR}/virtoolsdev-ui/player-unavailable-fullscreen.xml")
        set_tests_properties(VirtoolsDevUi.PlayerState.unavailable_fullscreen_keeps_both_viewports
                PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 60)
        add_test(NAME VirtoolsDevUi.SetupGrid.large_grid_layers_have_matching_cells COMMAND VirtoolsDevUiTests
                --editor-module "$<TARGET_FILE:NeMoDevAppearanceModule>"
                --ui-tests grid_reflow_multilayer_strokes_cancel_and_undo
                --ui-results "${PROJECT_BINARY_DIR}/virtoolsdev-ui/setup-large-grid.xml")
        set_tests_properties(VirtoolsDevUi.SetupGrid.large_grid_layers_have_matching_cells
                PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 45)
        add_test(NAME VirtoolsDevUi.FileMenu.save_callback_document_replacement_releases_manager COMMAND VirtoolsDevUiTests
                --ui-tests save_callback_keeps_document_open
                --ui-results "${PROJECT_BINARY_DIR}/virtoolsdev-ui/save-callback-lifetime.xml")
        set_tests_properties(VirtoolsDevUi.FileMenu.save_callback_document_replacement_releases_manager
                PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 45)
        foreach(_case gpu_viewport_close_after_draw_keeps_pending_image
                gpu_viewport_context_stop_after_draw_keeps_pending_image)
            add_test(NAME VirtoolsDevUi.GpuLifetime.${_case} COMMAND VirtoolsDevUiTests
                    --ui-tests "${_case}"
                    --ui-results "${PROJECT_BINARY_DIR}/virtoolsdev-ui/gpu-lifetime-${_case}.xml")
            set_tests_properties(VirtoolsDevUi.GpuLifetime.${_case}
                    PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 60)
        endforeach()
        foreach(_case
                curve_layout_resize_cancel_and_undo time_layout_resize_cancel_and_undo
                dependencies_layout_modes_cancel_and_undo lod_layout_flags_proportions_cancel_and_undo
                key_layout_capture_resize_cancel_and_undo script_layout_filter_resize_cancel_and_undo
                animation_layout_resize_cancel_and_undo attribute_layout_category_resize_cancel_and_undo
                message_layout_resize_deferred_creation_and_undo synchro_layout_users_resize_cancel_and_undo
                nested_specialized_layout_batch_and_undo retained_specialized_provider_layout
                curve_layout_drag_reflow_does_not_resample
                vector3_layout_resize_cancel_and_undo matrix_layout_resize_cancel_and_undo
                vector2_layout_resize_cancel_and_undo quaternion_layout_resize_cancel_and_undo
                box_layout_resize_cancel_and_undo angle_layout_turns_cancel_and_undo
                euler_layout_resize_cancel_and_undo rect_layout_modes_cancel_and_undo
                color_layout_channels_picker_cancel_and_undo flags_layout_unknown_bits_cancel_and_undo
                nested_compound_layout_batch_and_undo retained_compound_provider_layout
                angle_layout_drag_reflow_does_not_resample
                basic_integer_layout_cancel_and_undo basic_real_layout_cancel_and_undo
                basic_percent_layout_cancel_and_undo boolean_enum_layout_cancel_and_undo
                string_multiline_layout_cancel_and_undo object_filter_layout_cancel_and_undo
                object_array_table_layout_cancel_and_undo)
            add_test(NAME VirtoolsDevUi.ValueLayout.${_case} COMMAND VirtoolsDevUiTests
                    --ui-tests "${_case}" --editor-module "$<TARGET_FILE:NeMoDevAppearanceModule>"
                    --editor-module "$<TARGET_FILE:NeMoDevUiObjectModule>"
                    --ui-results "${PROJECT_BINARY_DIR}/virtoolsdev-ui/value-layout-${_case}.xml")
            set_tests_properties(VirtoolsDevUi.ValueLayout.${_case}
                    PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 60)
        endforeach()
    endif()
    add_dependencies(NeMoDevEditorTests NeMoDevGestureModule NeMoDevToolModule NeMoDevAppearanceModule)
    add_test(NAME NeMoDevEditor COMMAND NeMoDevEditorTests "$<TARGET_FILE:NeMoDevEditorModule>" "$<TARGET_FILE:NeMoDevEnvironmentPlugin>" "$<TARGET_FILE:NeMoDevGestureModule>" "$<TARGET_FILE:NeMoDevToolModule>" "$<TARGET_FILE:NeMoDevAppearanceModule>")
    set_tests_properties(NeMoDevEditor PROPERTIES LABELS self-contained ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    add_executable(NeMoDevNativeTaskCommandTests ../tests/NativeTaskCommandTests.cpp)
    add_executable(NeMoDevDocumentViewTests ../tests/DocumentViewTests.cpp native/EditorSession.cpp controls/FileDialogs.cpp)
    target_link_libraries(NeMoDevDocumentViewTests PRIVATE VirtoolsDevWorkspace NeMoDevEditorApplication
            NeMoDevObjectCommands VirtoolsDevCore VirtoolsDevImGui Threads::Threads)
    nemodev_add_module(NeMoDevDocumentViewModule SOURCES ../tests/modules/DocumentViewModule.cpp)
    add_dependencies(NeMoDevDocumentViewTests NeMoDevDocumentViewModule)
    foreach(_case RetainedInstances DispatchFailure DetachFailure CloseRefusal RecoveryRetry SuccessfulReplacement
            CommittedCallbackFailure ThreadChecks ModuleOrder CameraRestart CameraDetach QueuedReplacement)
        add_test(NAME NeMoDevDocumentView.${_case} COMMAND NeMoDevDocumentViewTests ${_case})
        set_tests_properties(NeMoDevDocumentView.${_case} PROPERTIES LABELS self-contained
                ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    endforeach()
    set_property(TEST NeMoDevDocumentView.CameraRestart NeMoDevDocumentView.CameraDetach APPEND PROPERTY LABELS gpu)
    add_test(NAME NeMoDevDocumentView.ExternalProvider COMMAND NeMoDevDocumentViewTests ExternalProvider
            "$<TARGET_FILE:NeMoDevDocumentViewModule>")
    set_tests_properties(NeMoDevDocumentView.ExternalProvider PROPERTIES LABELS self-contained
            ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    target_link_libraries(NeMoDevNativeTaskCommandTests PRIVATE NeMoDevEditorApplication NeMoDevObjectCommands
            VirtoolsDevCore VirtoolsDevImGui Threads::Threads)
    foreach(_case FileInput OutputInput ResourceInput PreparationFailure ExpiredFileInput ExpiredOutputInput)
        add_test(NAME NeMoDevNativeTaskCommand.${_case} COMMAND NeMoDevNativeTaskCommandTests ${_case})
        set_tests_properties(NeMoDevNativeTaskCommand.${_case} PROPERTIES LABELS self-contained
                ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    endforeach()
    add_executable(NeMoDevEditorOwnershipTests ../tests/EditorOwnershipTests.cpp)
    target_link_libraries(NeMoDevEditorOwnershipTests PRIVATE NeMoDevEditorApplication NeMoDevObjectCommands
            VirtoolsDevCore VirtoolsDevImGui Threads::Threads)
    foreach(_case CapturedConfiguration CapturedAppearance DraftConfiguration DraftAppearance FailedConfigurationKeepsDraft PreviewConfigurationCancel
            PendingPreviewConfiguration PendingPreviewClose DraftScopeLifetime DocumentReplacement StopCleanupOrder
            QueryConfigurationFailure DeliveryAcrossConfiguration QueryScopeLifetime)
        add_test(NAME NeMoDevEditorOwnership.${_case} COMMAND NeMoDevEditorOwnershipTests ${_case})
        set_tests_properties(NeMoDevEditorOwnership.${_case} PROPERTIES LABELS self-contained
                ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    endforeach()
    add_executable(NeMoDevModuleLifecycleTests ../tests/ModuleLifecycleTests.cpp)
    nemodev_add_module(NeMoDevLifecycleModule SOURCES ../tests/modules/LifecycleModule.cpp)
    target_link_libraries(NeMoDevModuleLifecycleTests PRIVATE NeMoDevEditorApplication NeMoDevObjectCommands VirtoolsDevCore VirtoolsDevImGui Threads::Threads)
    foreach(_case Order Disabled StartRefusal StartException UpdateException ReentrantCalls ThreadChecks StopException StableAllocations
            StopBeforeRelease UnstartedRelease UpdateOnly OptionalPresent OptionalAbsent OptionalCycle BorrowedControllerLifetime)
        add_test(NAME NeMoDevModuleLifecycle.${_case} COMMAND NeMoDevModuleLifecycleTests ${_case})
        set_tests_properties(NeMoDevModuleLifecycle.${_case} PROPERTIES LABELS self-contained ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    endforeach()
    if(TARGET VirtoolsDevUiTests)
        add_dependencies(VirtoolsDevUiTests NeMoDevLifecycleModule)
        foreach(_case DisabledExecution ExternalModule DisabledExternal StartRefusal StartException)
            add_test(NAME VirtoolsDevUi.ModuleLifecycle.${_case} COMMAND ${Python3_EXECUTABLE}
                    "${VIRTOOLSDEV_SOURCE_DIR}/tests/ModuleLifecycleStartupTests.py"
                    --binary "$<TARGET_FILE:VirtoolsDevUiTests>" --module "$<TARGET_FILE:NeMoDevLifecycleModule>"
                    --case ${_case} --results "${PROJECT_BINARY_DIR}/virtoolsdev-ui/module-lifecycle")
            set_tests_properties(VirtoolsDevUi.ModuleLifecycle.${_case} PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 90)
        endforeach()
    endif()
    add_executable(NeMoDevWorkspaceCloseTests ../tests/WorkspaceCloseTests.cpp)
    target_link_libraries(NeMoDevWorkspaceCloseTests PRIVATE VirtoolsDevWorkspace NeMoDevEditorApplication
            VirtoolsDevCore "$<LINK_LIBRARY:WHOLE_ARCHIVE,VirtoolsDevImGui>")
    nemodev_add_module(NeMoDevWorkspaceCloseModule SOURCES ../tests/modules/WorkspaceCloseModule.cpp)
    add_executable(NeMoDevFilePresentationTests ../tests/FilePresentationTests.cpp native/EditorSession.cpp controls/FileDialogs.cpp)
    target_link_libraries(NeMoDevFilePresentationTests PRIVATE VirtoolsDevWorkspace NeMoDevObjectCommands NeMoDevEditorApplication
            VirtoolsDevCore SDL3::SDL3 Threads::Threads "$<LINK_LIBRARY:WHOLE_ARCHIVE,VirtoolsDevImGui>")
    nemodev_add_module(NeMoDevFileNoticeModule SOURCES ../tests/modules/FileNoticeModule.cpp)
    add_dependencies(NeMoDevFilePresentationTests NeMoDevFileNoticeModule)
    foreach(_case Loaded EmptyScript LayoutProjection Merge Replacement ProviderChanged DocumentChanged NoticeFailure FailedOpen SaveReport CancelledReport PlayerReport)
        add_test(NAME NeMoDevFilePresentation.${_case} COMMAND NeMoDevFilePresentationTests ${_case})
        set_tests_properties(NeMoDevFilePresentation.${_case} PROPERTIES LABELS self-contained ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    endforeach()
    foreach(_case External DisabledModule DisabledViews)
        add_test(NAME NeMoDevFilePresentation.${_case} COMMAND NeMoDevFilePresentationTests ${_case} "$<TARGET_FILE:NeMoDevFileNoticeModule>")
        set_tests_properties(NeMoDevFilePresentation.${_case} PROPERTIES LABELS self-contained ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    endforeach()
    add_test(NAME NeMoDevFilePresentation.Import COMMAND NeMoDevFilePresentationTests Import "${VIRTOOLSDEV_SOURCE_DIR}/resources/data/interface.nmo")
    set_tests_properties(NeMoDevFilePresentation.Import PROPERTIES LABELS self-contained ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    add_test(NAME NeMoDevFilePresentation.Nmo COMMAND NeMoDevFilePresentationTests Nmo "${VIRTOOLSDEV_SOURCE_DIR}/resources/data/interface.nmo")
    set_tests_properties(NeMoDevFilePresentation.Nmo PROPERTIES LABELS self-contained ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    add_executable(NeMoDevProfileStorageTests ../tests/ProfileStorageTests.cpp native/EditorProfileStorage.cpp)
    target_link_libraries(NeMoDevProfileStorageTests PRIVATE VirtoolsDevWorkspace NeMoDevEditorApplication
            VirtoolsDevCore SDL3::SDL3 "$<LINK_LIBRARY:WHOLE_ARCHIVE,VirtoolsDevImGui>")
    foreach(_case LegacyUser LegacyApplication Isolated LoadFailure LegacyLayout LegacyLayoutInvalid
            PreferenceRefusal PreferenceException ModuleRefusal ModuleException WorkspaceRefusal WorkspaceException
            PreparationRefusal PreparationException AutosaveRetry DelayedLayout RelativePath SessionPreferences
            BusySave StartupFailure RestoreFailure Restart)
        add_test(NAME NeMoDevProfileStorage.${_case} COMMAND NeMoDevProfileStorageTests ${_case})
        set_tests_properties(NeMoDevProfileStorage.${_case} PROPERTIES LABELS self-contained)
    endforeach()
    add_dependencies(NeMoDevProfileStorageTests NeMoDevWorkspaceCloseModule)
    add_test(NAME NeMoDevProfileStorage.Discovery COMMAND NeMoDevProfileStorageTests Discovery
            "$<TARGET_FILE:NeMoDevWorkspaceCloseModule>")
    set_tests_properties(NeMoDevProfileStorage.Discovery PROPERTIES LABELS self-contained)
    add_dependencies(NeMoDevWorkspaceCloseTests NeMoDevWorkspaceCloseModule)
    foreach(_case ReplacementFailure ReplacementRefusal MultipleReplacementFailure WriterRefusal WriterException
            SuccessfulReplacement NormalClose CloseCleanupExceptions ReplacementCleanupExceptions ForcedClose
            WrongThread DocumentPreserved ReporterException)
        add_test(NAME NeMoDevWorkspaceClose.${_case} COMMAND NeMoDevWorkspaceCloseTests ${_case})
        set_tests_properties(NeMoDevWorkspaceClose.${_case} PROPERTIES LABELS self-contained
                ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    endforeach()
    add_test(NAME NeMoDevWorkspaceClose.ExternalProvider COMMAND NeMoDevWorkspaceCloseTests ExternalProvider
            "$<TARGET_FILE:NeMoDevWorkspaceCloseModule>")
    set_tests_properties(NeMoDevWorkspaceClose.ExternalProvider PROPERTIES LABELS self-contained)
    add_executable(NeMoDevEditorQueryTests ../tests/EditorQueryTests.cpp)
    target_link_libraries(NeMoDevEditorQueryTests PRIVATE NeMoDevEditorApplication VirtoolsDevCore VirtoolsDevImGui Threads::Threads)
    foreach(_case DeliveryLifetime RecordKeys CachedAllocations TimeSettings IdentifierSpans)
        add_test(NAME NeMoDevEditorQuery.${_case} COMMAND NeMoDevEditorQueryTests ${_case})
        set_tests_properties(NeMoDevEditorQuery.${_case} PROPERTIES LABELS self-contained
                ENVIRONMENT "VIRTOOLS_PATH=${VIRTOOLS_RUNTIME_PATH}")
    endforeach()
    if(TARGET VirtoolsDevUiTests)
        nemodev_add_module(NeMoDevUiDocumentInfo HOST VirtoolsDevUiTests
                SOURCES "${VIRTOOLSDEV_SOURCE_DIR}/examples/DocumentInfo/DocumentInfo.cpp")
        target_include_directories(NeMoDevUiDocumentInfo PRIVATE "${VIRTOOLSDEV_IMGUI_TEST_ENGINE_DIR}/imgui_test_engine")
        target_compile_definitions(NeMoDevUiDocumentInfo PRIVATE IMGUI_USER_CONFIG="imgui_te_imconfig.h")
        set_target_properties(NeMoDevUiDocumentInfo PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/example-modules/ui"
                RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/example-modules/ui")
        nemodev_add_module(NeMoDevUiObjectModule HOST VirtoolsDevUiTests SOURCES ../tests/modules/ObjectModule.cpp)
        target_include_directories(NeMoDevUiObjectModule PRIVATE "${VIRTOOLSDEV_IMGUI_TEST_ENGINE_DIR}/imgui_test_engine")
        target_compile_definitions(NeMoDevUiObjectModule PRIVATE IMGUI_USER_CONFIG="imgui_te_imconfig.h")
        nemodev_add_module(NeMoDevViewRoutingModule HOST VirtoolsDevUiTests SOURCES ../tests/modules/ViewRoutingModule.cpp)
        target_include_directories(NeMoDevViewRoutingModule PRIVATE "${VIRTOOLSDEV_IMGUI_TEST_ENGINE_DIR}/imgui_test_engine")
        target_compile_definitions(NeMoDevViewRoutingModule PRIVATE IMGUI_USER_CONFIG="imgui_te_imconfig.h")
        add_test(NAME VirtoolsDevUi.modules COMMAND VirtoolsDevUiTests --ui-tests "^modules$"
                --editor-module "$<TARGET_FILE:NeMoDevUiObjectModule>"
                --editor-module-dir "$<TARGET_FILE_DIR:NeMoDevUiDocumentInfo>"
                --ui-results "${PROJECT_BINARY_DIR}/virtoolsdev-ui/modules.xml")
        set_tests_properties(VirtoolsDevUi.modules PROPERTIES LABELS self-contained)
        foreach(_case schematic_replaced_view_receives_script_show layout_foreign_source_keeps_active_camera
                schematic_replaced_instances_keep_navigation_targets layout_replaced_instances_use_selected_command_provider
                schematic_replaced_view_receives_parameter_navigation layout_invalid_sources_cannot_query_edit_or_capture
                layout_default_gesture_keeps_initial_view_after_focus_change level_navigation_uses_replaced_schematic)
            set(_routing_sample)
            if(_case STREQUAL "level_navigation_uses_replaced_schematic")
                set(_routing_sample "${VIRTOOLSDEV_SAMPLE_ROOT}/data/BBSamples/Interface/2D Flow Layout.cmo")
            endif()
            add_test(NAME VirtoolsDevUi.ViewRouting.${_case} COMMAND VirtoolsDevUiTests
                    --ui-tests "${_case}" --editor-module "$<TARGET_FILE:NeMoDevUiObjectModule>"
                    --editor-module "$<TARGET_FILE:NeMoDevViewRoutingModule>"
                    --ui-results "${PROJECT_BINARY_DIR}/virtoolsdev-ui/view-routing-${_case}.xml" ${_routing_sample})
            set_tests_properties(VirtoolsDevUi.ViewRouting.${_case}
                    PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 60)
            if(_routing_sample)
                set_tests_properties(VirtoolsDevUi.ViewRouting.${_case} PROPERTIES LABELS sample SKIP_RETURN_CODE 77)
            endif()
        endforeach()
        foreach(_case schematic_explorer_reflow_keeps_navigation_and_cancel
                layout_explorer_reflow_preserves_locked_selection_and_cancel
                snapshot_grid_reflow_keeps_slots_and_explicit_readback
                report_scroll_reflow_preserves_document_history
                debugger_reflow_keeps_input_and_runtime_commands memory_list_reflow_keeps_sort_and_close
                invalid_links_reflow_keeps_refresh_and_close schematic_object_filter_reflow_cancel_and_undo
                profiler_reflow_keeps_details_sort_and_close message_explorer_reflow_keeps_selection_and_lists)
            add_test(NAME VirtoolsDevUi.WorkspaceLayout.${_case} COMMAND VirtoolsDevUiTests
                    --ui-tests "${_case}" --editor-module "$<TARGET_FILE:NeMoDevAppearanceModule>"
                    --editor-module "$<TARGET_FILE:NeMoDevUiObjectModule>"
                    --ui-results "${PROJECT_BINARY_DIR}/virtoolsdev-ui/workspace-layout-${_case}.xml")
            set_tests_properties(VirtoolsDevUi.WorkspaceLayout.${_case}
                    PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 60)
        endforeach()
        foreach(_flow files help)
            add_test(NAME VirtoolsDevUi.StartupFlows.${_flow} COMMAND ${Python3_EXECUTABLE}
                    "${VIRTOOLSDEV_SOURCE_DIR}/tests/FileHelpStartupTests.py"
                    --binary "$<TARGET_FILE:VirtoolsDevUiTests>" --module "$<TARGET_FILE:NeMoDevUiObjectModule>"
                    --help-dir "${_runtime_data}/help" --mode "${_flow}"
                    --results "${PROJECT_BINARY_DIR}/startup-flows-${_flow}")
            set_tests_properties(VirtoolsDevUi.StartupFlows.${_flow}
                    PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 300)
        endforeach()
        add_test(NAME VirtoolsDevUi.appearance COMMAND ${Python3_EXECUTABLE}
                "${VIRTOOLSDEV_SOURCE_DIR}/tests/AppearanceStartupTests.py"
                --binary "$<TARGET_FILE:VirtoolsDevUiTests>" --module "$<TARGET_FILE:NeMoDevAppearanceModule>"
                --help-dir "${_runtime_data}/help"
                --results "${PROJECT_BINARY_DIR}/appearance-startup")
        set_tests_properties(VirtoolsDevUi.appearance PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 300)
        add_test(NAME VirtoolsDevUi.moduleStartup COMMAND ${Python3_EXECUTABLE}
                "${VIRTOOLSDEV_SOURCE_DIR}/tests/ModuleStartupTests.py"
                --binary "$<TARGET_FILE:VirtoolsDevUiTests>" --module "$<TARGET_FILE:NeMoDevUiDocumentInfo>"
                --results "${PROJECT_BINARY_DIR}/module-startup")
        set_tests_properties(VirtoolsDevUi.moduleStartup PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 180)
        add_test(NAME VirtoolsDevUi.profileStartup COMMAND ${Python3_EXECUTABLE}
                "${VIRTOOLSDEV_SOURCE_DIR}/tests/ProfileStartupTests.py"
                --binary "$<TARGET_FILE:VirtoolsDevUiTests>" --module "$<TARGET_FILE:NeMoDevUiDocumentInfo>"
                --providers "$<TARGET_FILE:NeMoDevUiObjectModule>" --replacement "$<TARGET_FILE:NeMoDevProfileReplacement>"
                --results "${PROJECT_BINARY_DIR}/profile-startup")
        set_tests_properties(VirtoolsDevUi.profileStartup PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 300)
        add_test(NAME VirtoolsDevUi.workspaceStartup COMMAND ${Python3_EXECUTABLE}
                "${VIRTOOLSDEV_SOURCE_DIR}/tests/WorkspaceStartupTests.py"
                --document "${VIRTOOLSDEV_SOURCE_DIR}/resources/data/interface.nmo"
                --binary "$<TARGET_FILE:VirtoolsDevUiTests>" --results "${PROJECT_BINARY_DIR}/workspace-startup")
        set_tests_properties(VirtoolsDevUi.workspaceStartup PROPERTIES LABELS self-contained RUN_SERIAL TRUE TIMEOUT 300)
    endif()
endif()

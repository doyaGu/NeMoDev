# VirtoolsDevCore: no ImGui or D3D; the editor and the tests link it.

set(VIRTOOLS_DEV_CORE_SOURCES
        # InterfaceChunk persistence, reconstruction and Schematic geometry.
        interface/detail/InterfaceBitmap.cpp
        interface/InterfaceChunkFormat.cpp
        interface/detail/InterfaceChunkReader.cpp
        interface/detail/InterfaceChunkSections.cpp
        interface/InterfaceData.cpp
        interface/InterfaceDataSerialization.cpp
        schematic/BehaviorDisplayGeometry.cpp
        schematic/detail/GraphBuilder.cpp
        schematic/InterfaceDataReconstruction.cpp
        interface/InterfaceDataValidation.cpp
        schematic/LayoutCalculator.cpp
        schematic/LinkEndpointGeometry.cpp
        schematic/LinkRouting.cpp
        schematic/detail/OrthogonalRouter.cpp
        schematic/ParameterDisplayGeometry.cpp
        runtime/VirtoolsRuntimeLayout.cpp
        # The CK runtime and text encodings.
        runtime/CkArrays.cpp
        runtime/CkRuntimeHost.cpp
        runtime/Resources.cpp
        runtime/SdkCompat.cpp
        runtime/TextEncoding.cpp
        # The composition: opening, Scripts, undo, saving and the file tools.
        document/Document.cpp
        document/ScriptSession.cpp
        document/UndoHistory.cpp
        document/Edit.cpp
        document/DocumentSaver.cpp
        document/LinkValidation.cpp
        document/ObjectPictures.cpp
        document/SpriteTextPersistence.cpp
        document/FileCommands.cpp
        document/ToolCommands.cpp
        # The Schematic's display model and display-only edits.
        schematic/SchematicModel.cpp
        schematic/SchematicHitTest.cpp
        schematic/SchematicSearch.cpp
        schematic/SchematicEdits.cpp
        # Behavior graphs: topology, values, Building Blocks, Scripts, the debugger.
        graph/RuntimeGraph.cpp
        graph/TopologyCommands.cpp
        graph/OperationCommands.cpp
        graph/LinkCommands.cpp
        graph/TopologyPrimitives.cpp
        graph/TopologyPaste.cpp
        graph/GroupCommands.cpp
        graph/ExportCommands.cpp
        graph/ReplaceCommands.cpp
        graph/ShortcutCommands.cpp
        graph/BehaviorCommands.cpp
        graph/ValueCommands.cpp
        graph/ScriptCommands.cpp
        graph/NmsCommands.cpp
        graph/Debugger.cpp
        # The level: Level Manager, objects, curves, scenes, hierarchy.
        level/LevelCommands.cpp
        level/CurveCommands.cpp
        level/CreateCommands.cpp
        level/SceneCommands.cpp
        level/ObjectEditCommands.cpp
        level/ObjectProperties.cpp
        level/SetupCommands.cpp
        level/LevelTree.cpp
        level/HierarchyTree.cpp
        # The 3D Layout.
        layout/LayoutView.cpp
        layout/ViewProjection.cpp
        layout/GestureMath.cpp
        layout/LayoutOverlay.cpp
        layout/LayoutPick.cpp
        layout/LayoutTools.cpp
        layout/LayoutExplorer.cpp
        layout/LayoutInterface.cpp
        layout/SnapshotBuffer.cpp
        # Building Block prototypes, parameter types and their UIs.
        catalog/BuildingBlockCatalog.cpp
        catalog/ParameterCatalog.cpp
        catalog/ParameterUIs.cpp)

add_library(VirtoolsDevCore STATIC ${VIRTOOLS_DEV_CORE_SOURCES})
add_dependencies(VirtoolsDevCore VirtoolsDevData)
target_include_directories(VirtoolsDevCore PUBLIC ${VIRTOOLSDEV_SOURCE_DIR})
target_link_libraries(VirtoolsDevCore PUBLIC CK2 VxMath)
target_compile_definitions(VirtoolsDevCore PRIVATE VIRTOOLS_CK2_FILENAME="$<TARGET_FILE_NAME:CK2>")
if(WIN32)
    target_link_libraries(VirtoolsDevCore PRIVATE version advapi32)
endif()
target_compile_features(VirtoolsDevCore PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(VirtoolsDevCore PUBLIC /utf-8)
endif()
target_compile_definitions(VirtoolsDevCore PUBLIC NOMINMAX PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(VirtoolsDevCore PROPERTIES FOLDER "VirtoolsDev")

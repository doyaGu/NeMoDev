add_library(NeMoDevEditorApplication STATIC Preferences.cpp EditorApplication.cpp EditorModules.cpp EventLog.cpp MessageCommands.cpp KeyBinding.cpp RuntimeOperation.cpp DocumentFiles.cpp DocumentCommands.cpp ObjectSelection.cpp ExecutionCommands.cpp OutputCommands.cpp)
target_include_directories(NeMoDevEditorApplication PUBLIC "${VIRTOOLSDEV_SOURCE_DIR}")
target_compile_features(NeMoDevEditorApplication PUBLIC cxx_std_17)
target_link_libraries(NeMoDevEditorApplication PRIVATE VirtoolsDevCore nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})

add_library(NeMoDevObjectCommands STATIC ../features/objects/ObjectCommands.cpp ../features/objects/SelectionCommands.cpp ../features/scene/SceneCommands.cpp ../features/scene/SceneQueries.cpp ../features/resources/BuildingBlockQueries.cpp ../features/resources/DataResourceCommands.cpp ../features/values/ParameterCommands.cpp ../features/arrays/ArrayCommands.cpp ../features/setup/SetupCommands.cpp ../features/schematic/SchematicCommands.cpp)
target_include_directories(NeMoDevObjectCommands PUBLIC "${VIRTOOLSDEV_SOURCE_DIR}")
target_link_libraries(NeMoDevObjectCommands PRIVATE VirtoolsDevCore PUBLIC NeMoDevEditorApplication)
target_sources(NeMoDevObjectCommands PRIVATE ../features/environment/EnvironmentCommands.cpp)
target_sources(NeMoDevObjectCommands PRIVATE ../features/diagnostics/DiagnosticCommands.cpp ../features/preferences/PreferencesCommands.cpp)
target_sources(NeMoDevObjectCommands PRIVATE ../features/layout/LayoutCommands.cpp ../features/layout/LayoutGestures.cpp ../features/layout/LayoutPresentation.cpp ../features/layout/SnapshotCommands.cpp)
target_sources(NeMoDevObjectCommands PRIVATE ../features/layout/LayoutAuthoring.cpp)
target_sources(NeMoDevObjectCommands PRIVATE ../features/schematic/SchematicGraphCommands.cpp ../features/schematic/SchematicGeometry.cpp ../features/schematic/SchematicConnections.cpp ../features/schematic/SchematicCopying.cpp)

target_sources(NeMoDevObjectCommands PRIVATE ../features/schematic/SchematicAuthoring.cpp ../features/schematic/SchematicParameterEdits.cpp ../features/schematic/SchematicScriptCommands.cpp ../features/schematic/SchematicModelCommands.cpp ../features/schematic/SchematicModelData.cpp)

if(WIN32)
    target_link_libraries(NeMoDevEditorApplication PRIVATE advapi32)
endif()

target_sources(NeMoDevObjectCommands PRIVATE ../features/setup/SetupQueries.cpp ../features/setup/SetupRenderQueries.cpp ../features/setup/SetupObjectQueries.cpp ../features/setup/SetupSceneQueries.cpp ../features/setup/SetupActions.cpp)

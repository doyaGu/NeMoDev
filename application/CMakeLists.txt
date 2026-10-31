add_library(NeMoDevEditorApplication STATIC EditorApplication.cpp EventLog.cpp KeyBinding.cpp RuntimeOperation.cpp DocumentFiles.cpp DocumentCommands.cpp ObjectSelection.cpp ExecutionCommands.cpp OutputCommands.cpp)
target_include_directories(NeMoDevEditorApplication PUBLIC "${VIRTOOLSDEV_SOURCE_DIR}")
target_compile_features(NeMoDevEditorApplication PUBLIC cxx_std_17)
target_link_libraries(NeMoDevEditorApplication PRIVATE VirtoolsDevCore ${CMAKE_DL_LIBS})

add_library(NeMoDevObjectCommands STATIC ../features/objects/ObjectCommands.cpp ../features/objects/SelectionCommands.cpp ../features/scene/SceneCommands.cpp ../features/scene/SceneQueries.cpp ../features/resources/BuildingBlockQueries.cpp ../features/resources/DataResourceCommands.cpp ../features/values/ParameterCommands.cpp ../features/arrays/ArrayCommands.cpp ../features/setup/SetupCommands.cpp ../features/schematic/SchematicCommands.cpp)
target_include_directories(NeMoDevObjectCommands PUBLIC "${VIRTOOLSDEV_SOURCE_DIR}")
target_link_libraries(NeMoDevObjectCommands PRIVATE VirtoolsDevCore PUBLIC NeMoDevEditorApplication)

# Virtools Dev's tests: they link the core only.
add_executable(NeMoDevHelpTests HelpBookTests.cpp ../ui/help/HelpBook.cpp)
target_include_directories(NeMoDevHelpTests PRIVATE "${VIRTOOLSDEV_SOURCE_DIR}")
target_compile_features(NeMoDevHelpTests PRIVATE cxx_std_17)
set(_help_test ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/HelpContentTests.py"
        --reader $<TARGET_FILE:NeMoDevHelpTests>)
if(VIRTOOLSDEV_HAS_HELP)
    list(APPEND _help_test --content "${_help_output}")
    add_dependencies(NeMoDevHelpTests VirtoolsDevHelp)
endif()
add_test(NAME NeMoDevHelpContent COMMAND ${_help_test})
set(_production_check ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/CheckProductionBinary.py"
        --binary $<TARGET_FILE:VirtoolsDev>)
if(VIRTOOLSDEV_BUILD_UI_TESTS)
    list(APPEND _production_check --test-binary $<TARGET_FILE:VirtoolsDevUiTests>)
endif()
add_test(NAME NeMoDevProductionIsolation COMMAND ${_production_check})
add_test(NAME NeMoDevCTestResults COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/CTestResultsTests.py")
if(UNIX)
    add_test(NAME NeMoDevPackaging COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/PackagingTests.py")
endif()
add_executable(NeMoDevCommandLineTests CommandLineTests.cpp ../ui/app/CommandLine.cpp)
target_link_libraries(NeMoDevCommandLineTests PRIVATE VirtoolsDevCore)
virtoolsdev_add_test(NeMoDevCommandLine NeMoDevCommandLineTests)

add_executable(NeMoDevRuntimeLayoutTests RuntimeLayoutTests.cpp)
target_link_libraries(NeMoDevRuntimeLayoutTests PRIVATE VirtoolsDevCore)
target_compile_definitions(NeMoDevRuntimeLayoutTests PRIVATE VIRTOOLS_CK2_FILENAME="$<TARGET_FILE_NAME:CK2>")
virtoolsdev_add_test(NeMoDevRuntimeLayout NeMoDevRuntimeLayoutTests)
add_test(NAME NeMoDevRuntimeStartup COMMAND NeMoDevRuntimeLayoutTests --runtime auto)
set_tests_properties(NeMoDevRuntimeStartup PROPERTIES ENVIRONMENT "VIRTOOLS_PATH=;SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
add_test(NAME NeMoDevApplicationData COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIRECTORY=${VIRTOOLSDEV_SOURCE_DIR}"
        "-DBINARY_DIRECTORY=${PROJECT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/ApplicationDataTests.cmake")

add_test(NAME VirtoolsDevLicenseDeployment COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIRECTORY=${VIRTOOLSDEV_SOURCE_DIR}"
        "-DRUNTIME_DIRECTORY=${PROJECT_BINARY_DIR}/runtime"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckLicenseDeployment.cmake")

add_test(NAME VirtoolsDevFontDeployment COMMAND ${CMAKE_COMMAND}
        "-DFONT_DIRECTORY=${PROJECT_BINARY_DIR}/runtime/data/fonts"
        "-DFONT_MANIFEST=${VIRTOOLS_SDK_PATH}/Source/VxMath/fonts/sources.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckFontDeployment.cmake")

add_executable(VirtoolsDevFileDialogTests VirtoolsDevFileDialogTests.cpp
    ../ui/controls/FileDialogs.cpp ../ui/app/Preferences.cpp)
target_link_libraries(VirtoolsDevFileDialogTests PRIVATE VirtoolsDevCore)
target_include_directories(VirtoolsDevFileDialogTests PRIVATE ${VIRTOOLSDEV_IMGUI_DIR})
target_compile_definitions(VirtoolsDevFileDialogTests PRIVATE SDL_STATIC_LIB)
virtoolsdev_add_test(VirtoolsDevFileDialogs VirtoolsDevFileDialogTests)

add_executable(VirtoolsDevPortableTests VirtoolsDevPortableTests.cpp)
target_link_libraries(VirtoolsDevPortableTests PRIVATE VirtoolsDevCore)
virtoolsdev_add_resources(VirtoolsDevPortableTests)
virtoolsdev_add_test(VirtoolsDevPortable VirtoolsDevPortableTests)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # This checks VxMath's primitive bitmap/font APIs without a CK runtime.
    # Supply their asset fixture through the SDK's standard XDG search path.
    set_tests_properties(VirtoolsDevPortable PROPERTIES ENVIRONMENT_MODIFICATION
            "XDG_DATA_DIRS=path_list_prepend:${PROJECT_BINARY_DIR}/runtime/data")
endif()

add_executable(SchematicModelTests SchematicModelTests.cpp)
target_link_libraries(SchematicModelTests PRIVATE VirtoolsDevCore)
target_compile_definitions(SchematicModelTests PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(SchematicModelTests PROPERTIES FOLDER "VirtoolsDev")
virtoolsdev_add_test(SchematicModelTests SchematicModelTests)

add_executable(VirtoolsDevTests
        VirtoolsDevTests.cpp
        GraphTestSupport.cpp
        ObjectEditFailureTests.cpp
        SaveFailureTests.cpp
        InitialConditionsTests.cpp
        ParameterValueFailureTests.cpp
        VirtoolsDevEditTests.cpp
        VirtoolsDevTopologyTests.cpp
        VirtoolsDevManagerTests.cpp
        VirtoolsDevFileTests.cpp
        VirtoolsDevLayoutTests.cpp
        VirtoolsDevTestSupport.h
        VirtoolsDevCases.def)
target_link_libraries(VirtoolsDevTests PRIVATE VirtoolsDevCore)
target_compile_definitions(VirtoolsDevTests PRIVATE _CRT_SECURE_NO_WARNINGS
        VIRTOOLSDEV_RESOURCES_DIR="${VIRTOOLSDEV_RESOURCES_DIR}"
        VIRTOOLSDEV_HAS_HELP=$<BOOL:${VIRTOOLSDEV_HAS_HELP}>)
# The Resources case: the images in the program, the data files beside it.
virtoolsdev_add_resources(VirtoolsDevTests)
set_target_properties(VirtoolsDevTests PROPERTIES FOLDER "VirtoolsDev")
# The default suite, each case alone in its process (VirtoolsDevTests --case,
# the cases of VirtoolsDevCases.def): a failure names its case, none depends
# on another.
set(_dev_cases_file "${CMAKE_CURRENT_SOURCE_DIR}/VirtoolsDevCases.def")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_dev_cases_file}")
file(STRINGS "${_dev_cases_file}" _dev_case_lines REGEX "^VIRTOOLSDEV_CASE\\(")
foreach (_dev_case_line IN LISTS _dev_case_lines)
    string(REGEX REPLACE "^VIRTOOLSDEV_CASE\\(([A-Za-z0-9_]+)\\).*$" "\\1" _dev_case "${_dev_case_line}")
    virtoolsdev_add_test(VirtoolsDev.${_dev_case} VirtoolsDevTests --case ${_dev_case})
endforeach ()
# These tests supply their own inputs and can run from a public checkout.
# Original-composition regressions below retain their separate fixtures.
get_property(_self_contained_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_self_contained_tests} PROPERTIES LABELS self-contained)
# The samples of VIRTOOLSDEV_SAMPLE_ROOT/data. Edits a copy in memory;
# outputs go to the build tree, never into data/.
set(_dev_data "${VIRTOOLSDEV_SAMPLE_ROOT}/data")
set(_dev_command_sample "${_dev_data}/BBSamples/Interface/2D Flow Layout.cmo")
if (EXISTS "${_dev_command_sample}")
    virtoolsdev_add_test(VirtoolsDevSampleCommands VirtoolsDevTests "${_dev_command_sample}"
            "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip")
    # The random edits of a sample whose pasted Change Value If writes a local
    # and whose other Script reads a deleted local through a shortcut.
    set(_dev_frustum_sample "${_dev_data}/BBSamples/Collisions/Frustum Object Intersection.cmo")
    virtoolsdev_add_test(VirtoolsDevSampleFrustum VirtoolsDevTests "${_dev_frustum_sample}"
            "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip/frustum")
    # Create menu objects and a scene setting survive save and reload.
    virtoolsdev_add_test(VirtoolsDevCreateSave VirtoolsDevTests --create-save "${_dev_command_sample}"
            "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip/create")
    # Separate processes: the check cannot inherit any alignment kept in memory.
    set(_sprite_text_output "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip/spritetext")
    virtoolsdev_add_test(VirtoolsDevSpriteTextSave VirtoolsDevTests --sprite-text-save "${_dev_command_sample}"
            "${_sprite_text_output}")
    virtoolsdev_add_test(VirtoolsDevSpriteTextReopen VirtoolsDevTests --sprite-text-check "${_sprite_text_output}/aligned.cmo"
            "${_sprite_text_output}/reopened")
    set_tests_properties(VirtoolsDevSpriteTextSave PROPERTIES FIXTURES_SETUP SpriteTextFile)
    set_tests_properties(VirtoolsDevSpriteTextReopen PROPERTIES FIXTURES_REQUIRED SpriteTextFile)
    # File > New Composition and the two Merge commands, saved and reloaded.
    virtoolsdev_add_test(VirtoolsDevNewMerge VirtoolsDevTests --new-merge "${_dev_command_sample}"
            "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip/merge")
    # Data Resources, Import File (As), resource files and the Options tools.
    virtoolsdev_add_test(VirtoolsDevTools VirtoolsDevTests --tools "${_dev_command_sample}"
            "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip/tools")
    # The 3D Layout view: a render context, Dev's views, the frames copied back.
    # A sample in the this repository (3D entities, a camera, a light, a
    # curve), so a clone runs them; a missing sample skips its tests
    # (VirtoolsDevTests returns 77), not another sample in its place.
    set(_dev_layout_sample "${_dev_data}/BBSamples/3D Transformations/Add Control Point.cmo")
    virtoolsdev_add_test(VirtoolsDevLayout VirtoolsDevTests --layout "${_dev_layout_sample}"
            "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip/layout")
    # Its checks that need no render context: they run where no render device is.
    virtoolsdev_add_test(VirtoolsDevLayoutObjects VirtoolsDevTests --layout-objects "${_dev_layout_sample}"
            "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip/layoutobjects")
    # The 3D Layout's interface data: selection group, selection and lock saved and read back.
    virtoolsdev_add_test(VirtoolsDevLayoutInterface VirtoolsDevTests --layout-interface "${_dev_layout_sample}"
            "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip/layoutinterface")
    # The File menu: Save Version's names, Export to Player, Create Web Page.
    virtoolsdev_add_test(VirtoolsDevFileMenu VirtoolsDevTests --file-menu "${_dev_layout_sample}"
            "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip/filemenu")
    # The objects' pictures (Snapshot Buffer images dropped on a Setup or a 3D Layout object).
    virtoolsdev_add_test(VirtoolsDevObjectPictures VirtoolsDevTests --object-pictures "${_dev_layout_sample}"
            "${PROJECT_BINARY_DIR}/virtoolsdev-roundtrip/pictures")
    # Dev's link validation after loading (a Level linked to a 3D Entity input).
    set(_dev_links_sample "${_dev_data}/BBSamples/Narratives/Attach Script.cmo")
    virtoolsdev_add_test(VirtoolsDevLinks VirtoolsDevTests --links "${_dev_links_sample}")
    # Dev's Duplicate Names Found: the file merged into itself with each choice.
    virtoolsdev_add_test(VirtoolsDevDuplicates VirtoolsDevTests --duplicates "${_dev_command_sample}")
endif ()
# The list's paths are relative to the sample root.
set(_schematic_sample_list "${_dev_data}/verification/interfacechunk_samples.txt")
if (EXISTS "${_schematic_sample_list}")
    virtoolsdev_add_test(SchematicSampleModels SchematicModelTests --sample-list "${_schematic_sample_list}")
    set_tests_properties(SchematicSampleModels PROPERTIES WORKING_DIRECTORY "${VIRTOOLSDEV_SAMPLE_ROOT}")
endif ()
# The UI tests (VirtoolsDevUiTests, ui/CMakeLists.txt): the editor on its new
# composition, then on samples of the this repository: the Schematic and
# the Script Debugger on 2D Flow Layout, the 3D Layout, the Level Manager, the
# Setups, the Hierarchy Manager, the Building Blocks, the Attribute Manager,
# the objects made in the editor and the Editors and Options windows on Add
# Control Point (3D entities, a camera, a light, a curve, a Script); the Sound and Array Setups on SoundIsland, the Character Setup on
# Character Controller (samples not in the this repository: skipped
# without them). Each runs one category: ^name$ (the filter matches a
# substring of a test's name or category otherwise: "level" would take
# "open_from_level_manager" of "setup").
if (VIRTOOLSDEV_BUILD_UI_TESTS)
    set(_dev_ui_results "${PROJECT_BINARY_DIR}/virtoolsdev-ui")
    virtoolsdev_add_test(VirtoolsDevUi.html VirtoolsDevUiTests --ui-tests "^html$"
            --ui-results "${_dev_ui_results}/html.xml")
    if(VIRTOOLSDEV_HAS_HELP)
        virtoolsdev_add_test(VirtoolsDevUi.help VirtoolsDevUiTests --ui-tests "^help$"
                --ui-results "${_dev_ui_results}/help.xml")
    endif()
    set(_dev_ui_entities "${_dev_data}/BBSamples/3D Transformations/Add Control Point.cmo")
    virtoolsdev_add_test(VirtoolsDevUi.app VirtoolsDevUiTests --ui-tests "^app$" --ui-results "${_dev_ui_results}/app.xml")
    virtoolsdev_add_test(VirtoolsDevUi.filemenu VirtoolsDevUiTests --ui-tests "^filemenu$" --ui-results "${_dev_ui_results}/filemenu.xml")
    # Temporarily removes the build runtime's version marker to test export failure.
    set_tests_properties(VirtoolsDevUi.filemenu PROPERTIES RUN_SERIAL TRUE)
    set_tests_properties(VirtoolsDevUi.html VirtoolsDevUi.app VirtoolsDevUi.filemenu
            PROPERTIES LABELS self-contained)
    foreach (_category schematic debugger)
        virtoolsdev_add_test(VirtoolsDevUi.${_category} VirtoolsDevUiTests --ui-tests "^${_category}$"
                --ui-results "${_dev_ui_results}/${_category}.xml" "${_dev_command_sample}")
    endforeach ()
    foreach (_category layout level setup propertyedits hierarchy blocks attributes create tools)
        virtoolsdev_add_test(VirtoolsDevUi.${_category} VirtoolsDevUiTests --ui-tests "^${_category}$"
                --ui-results "${_dev_ui_results}/${_category}.xml" "${_dev_ui_entities}")
    endforeach ()
    # These scenarios deliberately move desktop focus and enter fullscreen.
    # Bare Xvfb has no window manager and cannot verify desktop activation.
    option(VIRTOOLSDEV_TEST_DESKTOP "Register tests that operate native desktop windows and dialogs" OFF)
    if(VIRTOOLSDEV_TEST_DESKTOP)
        virtoolsdev_add_test(VirtoolsDevUi.player VirtoolsDevUiTests --ui-tests "^player$" --ui-show
                --ui-results "${_dev_ui_results}/player.xml" "${_dev_ui_entities}")
        set_tests_properties(VirtoolsDevUi.player PROPERTIES RUN_SERIAL TRUE LABELS desktop TIMEOUT 180)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(_desktop_categories desktopfiles desktopprint)
            if(VIRTOOLSDEV_HAS_HELP)
                list(APPEND _desktop_categories desktophelp)
            endif()
            foreach(_category IN LISTS _desktop_categories)
                virtoolsdev_add_test(VirtoolsDevUi.${_category} VirtoolsDevUiTests --ui-tests "^${_category}$" --ui-show --open-scripts
                        --ui-results "${_dev_ui_results}/${_category}.xml" "${_dev_command_sample}")
                set_tests_properties(VirtoolsDevUi.${_category} PROPERTIES RUN_SERIAL TRUE LABELS desktop TIMEOUT 180)
            endforeach()
        endif()
    endif()
    virtoolsdev_add_test(VirtoolsDevUi.sound VirtoolsDevUiTests --ui-tests "^sound$"
            --ui-results "${_dev_ui_results}/sound.xml" "${_dev_data}/Demo/SoundIsland/SoundIsland.cmo")
    virtoolsdev_add_test(VirtoolsDevUi.character VirtoolsDevUiTests --ui-tests "^character$"
            --ui-results "${_dev_ui_results}/character.xml" "${_dev_data}/BBSamples/Characters/Character Controller.cmo")
    set(_dev_utf8_sample "${PROJECT_BINARY_DIR}/virtoolsdev-ui/中文组合.cmo")
    if(EXISTS "${_dev_command_sample}")
        configure_file("${_dev_command_sample}" "${_dev_utf8_sample}" COPYONLY)
    endif()
    virtoolsdev_add_test(VirtoolsDevUi.printCancel VirtoolsDevUiTests --ui-tests "cancel_pages" --open-scripts
            --tool "print:$<TARGET_FILE_DIR:VirtoolsDevUiTests>/test-print-cancel" "${_dev_utf8_sample}")
    virtoolsdev_add_test(VirtoolsDevUi.printPages VirtoolsDevUiTests --ui-tests "four_pages" --open-scripts
            --tool "print:$<TARGET_FILE_DIR:VirtoolsDevUiTests>/test-print-pages" "${_dev_command_sample}")
endif ()
# Skipped, not failed, without the runtime or a sample (every test above).
get_property(_dev_tests DIRECTORY PROPERTY TESTS)
if (_dev_tests)
    set_tests_properties(${_dev_tests} PROPERTIES SKIP_RETURN_CODE 77)
endif ()

# The source tree's include rules (no runtime needed).
add_test(NAME VirtoolsDevLayering
        COMMAND ${CMAKE_COMMAND} -DVIRTOOLSDEV_SOURCE_DIR=${VIRTOOLSDEV_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckLayering.cmake)
set_tests_properties(VirtoolsDevLayering PROPERTIES LABELS self-contained)

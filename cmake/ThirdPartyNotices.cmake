# Ship the editor's license scope and third-party notices with the runtime.
# This module also works in the standalone notice installation test.
install(FILES
        "${VIRTOOLSDEV_SOURCE_DIR}/LICENSE"
        "${VIRTOOLSDEV_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION . COMPONENT Runtime)
install(DIRECTORY "${VIRTOOLSDEV_SOURCE_DIR}/licenses/"
        DESTINATION licenses COMPONENT Runtime
        FILES_MATCHING PATTERN "*.txt" PATTERN "*.json")
install(FILES "${VIRTOOLSDEV_SOURCE_DIR}/core/interface/origin.json"
        DESTINATION licenses COMPONENT Runtime
        RENAME VirtoolsScriptDeobfuscation-origin.json)
install(FILES
        "${VIRTOOLSDEV_SOURCE_DIR}/resources/fonts/OFL.txt"
        "${VIRTOOLSDEV_SOURCE_DIR}/resources/fonts/OFL-NotoSans.txt"
        "${VIRTOOLSDEV_SOURCE_DIR}/resources/fonts/sources.json"
        DESTINATION licenses/fonts COMPONENT Runtime)

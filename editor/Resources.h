#ifndef NEMODEV_EDITOR_RESOURCES_H
#define NEMODEV_EDITOR_RESOURCES_H

#include "editor/Editor.h"

// ImGui owns the drag bytes until delivery or cancellation. No document
// objects or catalog indices are carried across views.
#define EDITOR_BUILDING_BLOCK_PAYLOAD "SCHEMATIC_BB"
typedef struct EditorBuildingBlock {
    uint32_t first, second;
} EditorBuildingBlock;
// Followed immediately by count EditorBuildingBlock values in catalog order.
// source names the row dragged; replacement uses it, insertion uses the list.
typedef struct EditorBuildingBlockDragHeader {
    EditorBuildingBlock source;
    uint32_t count;
} EditorBuildingBlockDragHeader;

#define EDITOR_RESOURCE_FILE_PAYLOAD "SCHEMATIC_RESOURCE_FILE"
// Followed by count records, each containing EditorResourceFileHeader and
// pathSize UTF-8 bytes (without a terminator). source is the dragged row.
typedef struct EditorResourceDragHeader {
    uint32_t source, count;
} EditorResourceDragHeader;
typedef struct EditorResourceFileHeader {
    uint32_t classId, pathSize;
} EditorResourceFileHeader;

#endif

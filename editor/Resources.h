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

#endif

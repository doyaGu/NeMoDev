#ifndef NEMODEV_EDITOR_SCHEMATIC_H
#define NEMODEV_EDITOR_SCHEMATIC_H

// script.navigate uses scope.view to address one script.schematic instance.
// target identifies its Script. An omitted kind shows the Script header.
// Indexed elements require the Script revision and document topology revision
// at which they were read; pins additionally require the endpoint object.
// A stale position fails without changing that view's navigation.
typedef enum EditorSchematicKind {
    EDITOR_SCHEMATIC_NONE,
    EDITOR_SCHEMATIC_SCRIPT,
    EDITOR_SCHEMATIC_BEHAVIOR,
    EDITOR_SCHEMATIC_PIN,
    EDITOR_SCHEMATIC_LINK,
    EDITOR_SCHEMATIC_CONTROL_POINT,
    EDITOR_SCHEMATIC_OPERATION,
    EDITOR_SCHEMATIC_LOCAL_PARAMETER,
    EDITOR_SCHEMATIC_SHORTCUT,
    EDITOR_SCHEMATIC_COMMENT,
    EDITOR_SCHEMATIC_START
} EditorSchematicKind;

typedef enum EditorSchematicPin {
    EDITOR_SCHEMATIC_NO_PIN,
    EDITOR_SCHEMATIC_BEHAVIOR_INPUT,
    EDITOR_SCHEMATIC_BEHAVIOR_OUTPUT,
    EDITOR_SCHEMATIC_PARAMETER_INPUT,
    EDITOR_SCHEMATIC_PARAMETER_OUTPUT,
    EDITOR_SCHEMATIC_TARGET
} EditorSchematicPin;

// schematic.model copies this owner category for drawing; consumers do not
// need the native class registry to choose the Script header's icon.
typedef enum EditorSchematicOwnerKind {
    EDITOR_SCHEMATIC_OWNER_OBJECT,
    EDITOR_SCHEMATIC_OWNER_LEVEL,
    EDITOR_SCHEMATIC_OWNER_PLACE,
    EDITOR_SCHEMATIC_OWNER_SCENE,
    EDITOR_SCHEMATIC_OWNER_SCRIPT,
    EDITOR_SCHEMATIC_OWNER_ENTITY2D,
    EDITOR_SCHEMATIC_OWNER_ENTITY3D,
    EDITOR_SCHEMATIC_OWNER_MATERIAL
} EditorSchematicOwnerKind;
#endif

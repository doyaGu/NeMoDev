#pragma once
#include "editor/Editor.h"
namespace vdev {
uint32_t DrawTimeValue(void *, const EditorHost *, const EditorValueInput *);
uint32_t DrawKeyValue(void *, const EditorHost *, const EditorValueInput *);
uint32_t DrawScriptValue(void *, const EditorHost *, const EditorValueInput *);
uint32_t DrawAnimationValue(void *, const EditorHost *, const EditorValueInput *);
uint32_t DrawMessageValue(void *, const EditorHost *, const EditorValueInput *);
uint32_t DrawAttributeValue(void *, const EditorHost *, const EditorValueInput *);
uint32_t DrawSynchroValue(void *, const EditorHost *, const EditorValueInput *);
} // namespace vdev

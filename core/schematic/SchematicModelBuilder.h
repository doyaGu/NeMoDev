#pragma once
#include "core/schematic/SchematicModel.h"
class CKContext;
class InterfaceData;
namespace vdev {
class ScriptSession;
SchematicModel BuildSchematicModel(const ScriptSession &, CKContext *, const TextMeasure & = {});
SchematicModel BuildSchematicModel(const ScriptSession &, CKContext *, const TextMeasure &, bool open);
SchematicModel BuildSchematicModel(const InterfaceData &, uint32_t script, CKContext *, const TextMeasure &, bool open);
} // namespace vdev

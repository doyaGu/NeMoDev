#pragma once
#include "application/EditorApplication.h"
#include "core/schematic/SchematicModel.h"
namespace vdev {
// Owned drawing data. Pages contain at most count elements; decoding is atomic.
EditorField SchematicModelFields(const SchematicModel &, size_t first, size_t count = 128);
size_t SchematicModelSize(const SchematicModel &);
bool ReadSchematicModel(const EditorField &, SchematicModel &);
} // namespace vdev

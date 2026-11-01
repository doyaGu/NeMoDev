#pragma once
#include "application/EditorApplication.h"
#include "core/schematic/SchematicModel.h"
#include "core/schematic/ElementAddress.h"
namespace vdev {
// Owned drawing data. Pages contain at most count elements; decoding is atomic.
EditorField SchematicModelFields(const SchematicModel &, size_t first, size_t count = 128);
size_t SchematicModelSize(const SchematicModel &);
bool ReadSchematicModel(const EditorField &, SchematicModel &);
// Match live runtime references to this copied model's retained positions.
// Missing, failed or newer results produce no runtime marks.
SchematicActivity ReadSchematicActivity(const EditorResponse &execution, const EditorResponse &activity,
                                        const ElementAddress &script,
                                        const std::map<ElementRef, ElementAddress> &positions);
} // namespace vdev

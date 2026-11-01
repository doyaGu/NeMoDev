#pragma once
#include "workspace/EditorClient.h"

namespace vdev {
bool RegisterSchematicDiagnosticViews(EditorApplication &, std::string &error);
bool ReadSchematicRows(const EditorClient &, const char *query, std::vector<EditorField> &, uint64_t &revision, const std::vector<EditorValue> &arguments = {});
}

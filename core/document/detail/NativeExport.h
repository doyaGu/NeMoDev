#pragma once

#include <string>
#include <vector>
#include "CKAll.h"

namespace vdev {
class Document;

enum class NativeExportKind { Objects, Behaviors };

// Native NMO/NMS output only. Display chunks and top-level flags are prepared
// and restored within this operation. A failed write, reload or publication
// preserves the destination; exports never change the document's save point.
bool SaveNativeExport(Document &document, const std::vector<CK_ID> &objects, const std::string &path,
                      std::string *error, NativeExportKind kind, bool hideRepresentation);
} // namespace vdev

#pragma once

#include <string>
#include <vector>
#include "CKAll.h"
#include "core/document/DocumentSaver.h"

namespace vdev {
enum class NativeExportKind { Objects, Behaviors };

class Document;
class SaveFileOperations;
struct BehaviorDescription;
struct NativeExportResult {
    SaveReport report;
    std::vector<std::string> written;
};
// Optional DSC and NMS are prepared before either destination is replaced.
// On publication failure the previous DSC is restored; any output left by a
// failed restoration is listed in written, with a recovery path in report.
void ExportNativeObjects(Document &document, const std::vector<CK_ID> &objects, const std::string &path,
                         NativeExportKind kind, bool hideRepresentation, const BehaviorDescription *description,
                         NativeExportResult &result, SaveFileOperations *operations = nullptr);

} // namespace vdev

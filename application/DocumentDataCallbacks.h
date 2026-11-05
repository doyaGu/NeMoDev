#pragma once

#include <memory>
#include <mutex>
#include <string>
#include "editor/Editor.h"

namespace vdev {
struct DocumentDataRecord;
struct DocumentDataType;
// File tasks may finish after the requesting view closes. Stop waits for a
// running conversion before module state/code is released; old registries can
// then only preserve opaque data, never enter unloaded module code.
class DocumentDataCallbacks {
public:
    void Close();
    bool Convert(const EditorDocumentDataDesc &, const DocumentDataRecord &,
                 const DocumentDataRecord *, DocumentDataRecord &, std::string &);
private:
    std::mutex m_Mutex;
    bool m_Closed = false;
};
EditorDocumentData BorrowDocumentData(const DocumentDataRecord &);
bool CopyDocumentData(const EditorDocumentData &, DocumentDataRecord &);
DocumentDataType MakeDocumentDataType(const EditorDocumentDataDesc &,
                                     const std::shared_ptr<DocumentDataCallbacks> &);
} // namespace vdev

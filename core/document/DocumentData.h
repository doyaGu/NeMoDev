#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/document/ObjectPictures.h"
#include "core/layout/LayoutInterface.h"
#include "editor/Identity.h"

namespace vdev {
class Document;


// Bytes belong to the document, not to the module that understands them.
// Native IDs must never be embedded in bytes: objects are ordered reference
// slots, remapped by CKStateChunk even when the module is unavailable.
struct DocumentDataRecord {
    std::string id;
    uint32_t version = 0;
    ObjectRef owner{}; // zero: composition-wide; otherwise per-object data
    std::vector<unsigned char> bytes;
    std::vector<ObjectRef> objects;
};
bool operator==(const DocumentDataRecord &, const DocumentDataRecord &);
using DocumentDataKey = std::pair<std::string, ObjectRef>;
using DocumentDataRecords = std::map<DocumentDataKey, DocumentDataRecord>;

struct DocumentDataState {
    LayoutLists layout;
    ObjectPictures pictures;
    DocumentDataRecords records;
    bool layoutChanged = false, picturesChanged = false, recordsChanged = false;
};

struct DocumentDataLoad {
    enum Mode { Open, Import, Merge } mode = Open;
    // Only managers brought by this load; never reread the current document's
    // older saved chunks over its unsaved changes.
    std::vector<CK_ID> managers, objects;
};

struct DocumentDataType {
    std::string id;
    uint32_t version = 1;
    std::function<bool(const DocumentDataRecord &, DocumentDataRecord &, std::string &)> upgrade;
    std::function<bool(const DocumentDataRecord &, const DocumentDataRecord &,
                       DocumentDataRecord &, std::string &)> merge;
    // Native encodings keep existing files readable by older editors. They
    // participate in the same registration and file operations as module data.
    std::function<bool(Document &, DocumentDataState &, const DocumentDataLoad &, std::string &)> read;
    std::function<bool(Document &, DocumentDataState &, const LayoutInterfaceData *, std::string &)> write;
};

class DocumentDataRegistry {
public:
    bool Register(DocumentDataType type, std::string &error);
    const DocumentDataType *Find(const std::string &) const;
    const std::vector<DocumentDataType> &Types() const { return m_Types; }
private:
    std::vector<DocumentDataType> m_Types;
};
std::shared_ptr<const DocumentDataRegistry> DefaultDocumentDataRegistry();

bool ReadDocumentData(Document &, const DocumentDataLoad &, std::string &error);
bool WriteDocumentData(Document &, const LayoutInterfaceData *, std::string &error);
// No registration is required for reading/preserving unknown data. Mutations
// require a registered, current schema and join the caller's existing Edit.
const DocumentDataRecord *FindDocumentData(const Document &, const std::string &, ObjectRef owner = {});
EditResult SetDocumentData(Document &, DocumentDataRecord record);
EditResult RemoveDocumentData(Document &, const std::string &, ObjectRef owner = {});
void RebindDocumentData(Document &);
void ForgetDocumentData(Document &);
DocumentDataRecords CaptureDocumentDataForSave(const Document &);
bool VerifyDocumentData(const DocumentDataRecords &, CKContext *, CKFile &, std::string &error);
} // namespace vdev

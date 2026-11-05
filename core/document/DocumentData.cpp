#include "core/document/DocumentData.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/runtime/SdkCompat.h"

namespace vdev {
namespace {
const CKGUID DATA_GUID(0x4E4D4441, 0x44415441);
constexpr CKDWORD RECORD = 0x4E4D4452;
constexpr uint32_t FormatVersion = 1, MaxBytes = 16 * 1024 * 1024, MaxObjects = 65536;
constexpr size_t MaxRecords = 4096, MaxDocumentBytes = 64 * 1024 * 1024;
using Chunk = std::unique_ptr<CKStateChunk, decltype(&DeleteCKStateChunk)>;

bool Identifier(const std::string &id) {
    return !id.empty() && id.size() <= 256 && id.find('.') != std::string::npos &&
           id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == std::string::npos;
}
DocumentDataKey Key(const DocumentDataRecord &record) { return {record.id, record.owner}; }
bool SavedObject(const Document &document, CK_ID id) {
    auto *object = document.Context()->GetObject(id);
    return object && document.IsLive(id) && !(object->GetObjectFlags() & CK_OBJECT_NOTTOBESAVED);
}
std::vector<CKInterfaceObjectManager *> Managers(Document &document, const std::vector<CK_ID> *ids = nullptr) {
    std::vector<CKInterfaceObjectManager *> result;
    const auto add = [&](CKObject *object) {
        auto *manager = InterfaceObjectManagerOf(object);
        if (manager && manager->GetGuid() == DATA_GUID) result.push_back(manager);
    };
    if (ids) for (auto id : *ids) add(document.Context()->GetObject(id));
    else {
        const auto &objects = document.Context()->GetObjectListByType(CKCID_INTERFACEOBJECTMANAGER, TRUE);
        for (int i = 0; i < objects.Size(); ++i) add(objects[i]);
    }
    return result;
}

// Check every span before calling SDK reads. File lengths are not allocation
// requests, and an invalid reference table cannot turn into ordinary bytes.
class Reader {
public:
    explicit Reader(CKStateChunk &chunk) : c(chunk) {
        c.StartRead();
        const auto bytes = c.SeekIdentifierAndReturnSize(RECORD);
        if (bytes < 0 || !bytes) throw std::runtime_error("Missing module data record");
        end = c.GetCurrentPos() + bytes / 4;
        if (end > c.GetDataSize() / 4) throw std::runtime_error("Truncated module data record");
    }
    uint32_t Word() { Need(1); return c.ReadDword(); }
    CK_ID Object() { Need(1); return c.ReadObjectID(); }
    std::vector<unsigned char> Bytes(uint32_t limit) {
        const auto count = Word();
        if (count > limit) throw std::runtime_error("Module data exceeds the size limit");
        const auto words = (count + 3) / 4;
        Need(words);
        auto *p = static_cast<unsigned char *>(c.LockReadBuffer());
        std::vector<unsigned char> result(p, p + count);
        c.Goto(c.GetCurrentPos() + static_cast<int>(words));
        return result;
    }
    void Complete() { if (c.GetCurrentPos() != end) throw std::runtime_error("Unexpected module data fields"); }
private:
    void Need(uint32_t words) {
        if (c.GetCurrentPos() < 0 || c.GetCurrentPos() > end || words > uint32_t(end - c.GetCurrentPos()))
            throw std::runtime_error("Truncated module data record");
    }
    CKStateChunk &c;
    int end = 0;
};

bool ValidRecord(const Document &document, const DocumentDataRecord &record, bool existing = false) {
    if (!Identifier(record.id) || !record.version || record.bytes.size() > MaxBytes || record.objects.size() > MaxObjects)
        return false;
    const auto valid = [&](ObjectRef object) {
        return object == ObjectRef{} || document.Resolve(object, CKCID_OBJECT,
            existing ? Document::ObjectScope::Existing : Document::ObjectScope::Live) != nullptr;
    };
    return valid(record.owner) && std::all_of(record.objects.begin(), record.objects.end(), valid);
}
void Budget(const DocumentDataRecords &records) {
    size_t bytes = 0;
    if (records.size() > MaxRecords) throw std::runtime_error("Too many module data records");
    for (const auto &entry : records) {
        bytes += entry.second.bytes.size() + entry.second.objects.size() * sizeof(ObjectRef) + entry.first.first.size();
        if (bytes > MaxDocumentBytes) throw std::runtime_error("Module document data exceeds the size limit");
    }
}
DocumentDataRecord Decode(CKStateChunk &chunk,
                          const std::function<ObjectRef(CK_ID)> &reference) {
    Reader reader(chunk);
    if (reader.Word() != FormatVersion) throw std::runtime_error("Unsupported module data format");
    DocumentDataRecord record;
    const auto id = reader.Bytes(256);
    record.id.assign(id.begin(), id.end());
    record.version = reader.Word();
    const auto objectOwned = reader.Word();
    const auto owner = reader.Object();
    record.owner = reference(owner);
    if (objectOwned > 1 || bool(owner) != bool(objectOwned) || (objectOwned && !record.owner))
        throw std::runtime_error("Missing or invalid module data owner");
    record.bytes = reader.Bytes(MaxBytes);
    const auto count = reader.Word();
    if (count > MaxObjects) throw std::runtime_error("Too many module object references");
    record.objects.reserve(count);
    for (uint32_t i = 0; i < count; ++i) record.objects.push_back(reference(reader.Object()));
    reader.Complete();
    if (!Identifier(record.id) || !record.version) throw std::runtime_error("Invalid module data record");
    return record;
}
Chunk Encode(const Document &document, const DocumentDataRecord &record) {
    Chunk chunk(CreateCKStateChunk(1, nullptr), DeleteCKStateChunk);
    if (!chunk) throw std::runtime_error("Cannot create module data chunk");
    chunk->StartWrite();
    chunk->WriteIdentifier(RECORD);
    chunk->WriteDword(FormatVersion);
    chunk->WriteBuffer(int(record.id.size()), const_cast<char *>(record.id.data()));
    chunk->WriteDword(record.version);
    chunk->WriteDword(record.owner ? 1 : 0);
    chunk->WriteObjectID(document.NativeId(record.owner));
    chunk->WriteBuffer(int(record.bytes.size()), const_cast<unsigned char *>(record.bytes.data()));
    chunk->WriteDword(CKDWORD(record.objects.size()));
    for (auto object : record.objects) {
        const auto id = document.NativeId(object);
        chunk->WriteObjectID(SavedObject(document, id) ? id : 0);
    }
    chunk->CloseChunk();
    return chunk;
}
bool Upgrade(Document &document, DocumentDataRecord &record, std::string &error) {
    auto *type = document.DataTypes()->Find(record.id);
    // Unknown and newer schemas stay byte-for-byte intact. No callback means
    // the registered module cannot yet understand this older record either.
    if (!type || record.version >= type->version || !type->upgrade) return true;
    auto upgraded = record;
    if (!type->upgrade(record, upgraded, error)) {
        if (error.empty()) error = "Upgrading module document data failed: " + record.id;
        return false;
    }
    if (Key(upgraded) != Key(record) || upgraded.version != type->version || !ValidRecord(document, upgraded, true)) {
        error = "Invalid upgraded module data: " + record.id;
        return false;
    }
    record = std::move(upgraded);
    return true;
}
} // namespace

bool operator==(const DocumentDataRecord &a, const DocumentDataRecord &b) {
    return a.id == b.id && a.version == b.version && a.owner == b.owner && a.bytes == b.bytes && a.objects == b.objects;
}
bool DocumentDataRegistry::Register(DocumentDataType type, std::string &error) {
    if (!Identifier(type.id) || !type.version || Find(type.id) || bool(type.read) != bool(type.write)) {
        error = "Invalid or duplicate document data type: " + type.id;
        return false;
    }
    m_Types.push_back(std::move(type));
    return true;
}
const DocumentDataType *DocumentDataRegistry::Find(const std::string &id) const {
    for (const auto &type : m_Types) if (type.id == id) return &type;
    return nullptr;
}

struct DocumentDataAccess {
    static DocumentDataState &Data(Document &document) { return document.m_Data; }
};

std::shared_ptr<const DocumentDataRegistry> DefaultDocumentDataRegistry() {
    static const auto registry = [] {
        auto result = std::make_shared<DocumentDataRegistry>();
        std::string error;
        DocumentDataType layout;
        layout.id = "layout.display";
        layout.read = [](Document &doc, DocumentDataState &state, const DocumentDataLoad &load, std::string &) {
            if (load.mode == DocumentDataLoad::Open)
                state.layout = ReadLayoutLists(doc.Context(), ReadLayoutInterfaceData(doc.Context()));
            return true;
        };
        layout.write = [](Document &doc, DocumentDataState &state, const LayoutInterfaceData *view, std::string &) {
            if (view || state.layoutChanged) {
                auto data = view ? *view : ReadLayoutInterfaceData(doc.Context());
                const auto lists = state.layout;
                if (view || data.present || !lists.Empty()) {
                    WriteLayoutLists(doc.Context(), lists, [&](CK_ID id) { return SavedObject(doc, id); }, data);
                    WriteLayoutInterfaceData(doc, data);
                }
                if (state.layout == lists) state.layoutChanged = false;
            }
            return true;
        };
        if (!result->Register(std::move(layout), error)) throw std::logic_error(error);
        DocumentDataType pictures;
        pictures.id = "object.pictures";
        pictures.read = [](Document &doc, DocumentDataState &state, const DocumentDataLoad &load, std::string &) {
            if (load.mode == DocumentDataLoad::Open) state.pictures = ReadObjectPictures(doc);
            else if (load.mode == DocumentDataLoad::Merge) {
                const std::set<CK_ID> incoming(load.objects.begin(), load.objects.end());
                for (auto &entry : ReadObjectPictures(doc)) if (incoming.count(entry.first)) {
                    state.pictures.emplace(entry.first, std::move(entry.second));
                    state.picturesChanged = true;
                }
            }
            return true;
        };
        pictures.write = [](Document &doc, DocumentDataState &state, const LayoutInterfaceData *, std::string &) {
            if (state.picturesChanged) {
                const auto copy = state.pictures;
                WriteObjectPictures(doc, copy);
                if (state.pictures == copy) state.picturesChanged = false;
            }
            return true;
        };
        if (!result->Register(std::move(pictures), error)) throw std::logic_error(error);
        return result;
    }();
    return registry;
}

bool ReadDocumentData(Document &document, const DocumentDataLoad &load, std::string &error) {
    try {
        auto next = document.Data();
        const auto before = next.records;
        DocumentDataRecords incoming;
        const auto managers = Managers(document, load.mode == DocumentDataLoad::Open ? nullptr : &load.managers);
        size_t chunks = 0;
        for (auto *manager : managers) {
            const auto count = manager->GetChunkCount();
            if (count < 0 || (chunks += size_t(count)) > MaxRecords)
                throw std::runtime_error("Too many module data chunks");
            for (int i = 0; i < manager->GetChunkCount(); ++i) {
                auto *chunk = manager->GetChunk(i);
                if (!chunk) throw std::runtime_error("Missing module data chunk");
                auto record = Decode(*chunk, [&](CK_ID id) { return document.Reference(id); });
                const auto original = record;
                if (!Upgrade(document, record, error)) return false;
                if (!(record == original)) next.recordsChanged = true;
                if (!incoming.emplace(Key(record), std::move(record)).second)
                    throw std::runtime_error("Duplicate module data in file");
                Budget(incoming);
            }
        }
        if (load.mode == DocumentDataLoad::Open) next.records = std::move(incoming);
        else for (auto &entry : incoming) {
            auto found = next.records.find(entry.first);
            if (found == next.records.end()) next.records.emplace(entry.first, std::move(entry.second));
            else if (!(found->second == entry.second)) {
                const auto *type = document.DataTypes()->Find(entry.first.first);
                if (!type || !type->merge || found->second.version != type->version || entry.second.version != type->version) {
                    error = "Conflicting module document data: " + entry.first.first;
                    return false;
                }
                auto merged = found->second;
                if (!type->merge(found->second, entry.second, merged, error)) {
                    if (error.empty()) error = "Merging module document data failed: " + entry.first.first;
                    return false;
                }
                if (Key(merged) != entry.first || merged.version != type->version || !ValidRecord(document, merged, true)) {
                    error = "Invalid merged module data: " + entry.first.first;
                    return false;
                }
                found->second = std::move(merged);
            }
        }
        Budget(next.records);
        for (const auto &type : document.DataTypes()->Types())
            if (type.read && !type.read(document, next, load, error)) return false;
        if (load.mode != DocumentDataLoad::Open && (!managers.empty() || next.records != before)) next.recordsChanged = true;
        const bool upgraded = load.mode == DocumentDataLoad::Open && next.recordsChanged;
        DocumentDataAccess::Data(document) = std::move(next);
        if (upgraded) document.MarkTopologyChanged();
        return true;
    } catch (const std::exception &e) { error = e.what(); }
    catch (...) { error = "Reading module document data raised an exception"; }
    return false;
}

bool WriteDocumentData(Document &document, const LayoutInterfaceData *view, std::string &error) {
    try {
        auto &state = DocumentDataAccess::Data(document);
        // Prepare all generic chunks before changing any native manager.
        const auto records = state.records;
        std::vector<Chunk> chunks;
        const bool writeRecords = state.recordsChanged || !records.empty() || !Managers(document).empty();
        if (writeRecords) {
            Budget(records);
            for (const auto &entry : records) {
                if (entry.second.owner && !SavedObject(document, document.NativeId(entry.second.owner))) continue;
                chunks.push_back(Encode(document, entry.second));
            }
        }
        for (const auto &type : document.DataTypes()->Types())
            if (type.write && !type.write(document, state, view, error)) return false;
        if (!writeRecords) return true;
        auto managers = Managers(document);
        if (chunks.empty()) {
            for (auto *manager : managers) document.Context()->DestroyObject(manager);
            if (state.records == records) state.recordsChanged = false;
            return true;
        }
        if (!chunks.empty() && managers.empty()) {
            auto *manager = InterfaceObjectManagerOf(document.Context()->CreateObject(CKCID_INTERFACEOBJECTMANAGER));
            if (!manager) throw std::runtime_error("Cannot create module data manager");
            manager->SetGuid(DATA_GUID);
            document.Include(manager->GetID());
            managers.push_back(manager);
        }
        for (auto *manager : managers)
            while (manager->GetChunkCount()) manager->RemoveStateChunk(manager->GetChunk(0));
        for (auto &chunk : chunks) managers.front()->AddStateChunk(chunk.release());
        if (state.records == records) state.recordsChanged = false;
        return true;
    } catch (const std::exception &e) { error = e.what(); }
    catch (...) { error = "Writing module document data raised an exception"; }
    return false;
}

const DocumentDataRecord *FindDocumentData(const Document &document, const std::string &id, ObjectRef owner) {
    const auto found = document.Data().records.find({id, owner});
    return found == document.Data().records.end() ? nullptr : &found->second;
}

class DocumentDataAction : public Action {
public:
    DocumentDataAction(DocumentDataKey key, std::optional<DocumentDataRecord> before,
                       std::optional<DocumentDataRecord> after)
        : m_Key(std::move(key)), m_Before(std::move(before)), m_After(std::move(after)) {}
    bool Redo(Document &doc) override { return Apply(doc, m_After); }
    bool Undo(Document &doc) override { return Apply(doc, m_Before); }
    void RetainObjects(const Document &doc, std::set<CK_ID> &objects) const override {
        const auto retain = [&](const std::optional<DocumentDataRecord> &record) {
            if (!record) return;
            if (auto id = doc.NativeId(record->owner, Document::ObjectScope::Existing)) objects.insert(id);
            for (auto ref : record->objects)
                if (auto id = doc.NativeId(ref, Document::ObjectScope::Existing)) objects.insert(id);
        };
        retain(m_Before); retain(m_After);
    }
private:
    bool Apply(Document &doc, const std::optional<DocumentDataRecord> &record) {
        if (record && !ValidRecord(doc, *record, true)) return false;
        if (record) {
            // Own every allocation before touching the live record. A failed
            // copy cannot leave half of its bytes/references installed.
            auto copy = *record;
            doc.m_Data.records.insert_or_assign(m_Key, std::move(copy));
        } else doc.m_Data.records.erase(m_Key);
        doc.m_Data.recordsChanged = true;
        return true;
    }
    DocumentDataKey m_Key;
    std::optional<DocumentDataRecord> m_Before, m_After;
};

EditResult SetDocumentData(Document &document, DocumentDataRecord record) {
    const auto *type = document.DataTypes()->Find(record.id);
    if (!document.IsOpen() || !type || type->read || record.version != type->version || !ValidRecord(document, record))
        return EditResult::Refused("Invalid module document data");
    const auto *before = FindDocumentData(document, record.id, record.owner);
    if (before && before->version != type->version) return EditResult::Refused("Unsupported module data version");
    if (before && *before == record) return EditResult::NoChange();
    try {
        size_t bytes = record.bytes.size() + record.objects.size() * sizeof(ObjectRef) + record.id.size();
        const auto key = Key(record);
        size_t count = 1;
        for (const auto &entry : document.Data().records) if (entry.first != key) {
            bytes += entry.second.bytes.size() + entry.second.objects.size() * sizeof(ObjectRef) + entry.first.first.size();
            ++count;
        }
        if (bytes > MaxDocumentBytes || count > MaxRecords)
            return EditResult::Refused("Module document data exceeds the size limit");
    } catch (const std::exception &e) { return EditResult::Refused(e.what()); }
    Edit edit(document, "Module data");
    auto key = Key(record);
    if (!edit.Do(std::make_unique<DocumentDataAction>(std::move(key),
            before ? std::optional<DocumentDataRecord>(*before) : std::nullopt, std::move(record))))
        return edit.Fail("Cannot set module document data");
    return edit.Commit();
}
EditResult RemoveDocumentData(Document &document, const std::string &id, ObjectRef owner) {
    const auto *type = document.DataTypes()->Find(id);
    if (!document.IsOpen() || !type || type->read || (owner && !document.Resolve(owner)))
        return EditResult::Refused("Invalid module document data");
    const auto *before = FindDocumentData(document, id, owner);
    if (!before) return EditResult::NoChange();
    if (before->version != type->version) return EditResult::Refused("Unsupported module data version");
    Edit edit(document, "Remove module data");
    if (!edit.Do(std::make_unique<DocumentDataAction>(DocumentDataKey{id, owner}, *before, std::nullopt)))
        return edit.Fail("Cannot remove module document data");
    return edit.Commit();
}
void RebindDocumentData(Document &document) {
    auto &state = DocumentDataAccess::Data(document);
    DocumentDataRecords rebound;
    for (auto &entry : state.records) {
        auto &record = entry.second;
        if (record.owner) record.owner.document = document.Token();
        for (auto &object : record.objects) if (object) object.document = document.Token();
        rebound.emplace(Key(record), std::move(record));
    }
    state.records = std::move(rebound);
}
void ForgetDocumentData(Document &document) {
    auto &state = DocumentDataAccess::Data(document);
    for (auto it = state.records.begin(); it != state.records.end();) {
        if (it->second.owner && !document.Resolve(it->second.owner, CKCID_OBJECT, Document::ObjectScope::Existing)) {
            it = state.records.erase(it);
            state.recordsChanged = true;
        } else {
            for (auto &object : it->second.objects)
                if (object && !document.Resolve(object, CKCID_OBJECT, Document::ObjectScope::Existing)) {
                    object = {};
                    state.recordsChanged = true;
                }
            ++it;
        }
    }
}
DocumentDataRecords CaptureDocumentDataForSave(const Document &document) {
    DocumentDataRecords result;
    for (const auto &entry : document.Data().records) {
        auto record = entry.second;
        if (record.owner && !SavedObject(document, document.NativeId(record.owner))) continue;
        for (auto &object : record.objects) if (!SavedObject(document, document.NativeId(object))) object = {};
        result.emplace(Key(record), std::move(record));
    }
    return result;
}
bool VerifyDocumentData(const DocumentDataRecords &expected, CKContext *context, CKFile &file, std::string &error) {
    try {
        std::map<CK_ID, CK_ID> original;
        std::vector<CKInterfaceObjectManager *> managers;
        for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
            const auto &entry = file.m_FileObjects[i];
            if (!entry.ObjPtr || context->GetObject(entry.CreatedObject) != entry.ObjPtr) continue;
            original.emplace(entry.CreatedObject, entry.Object);
            auto *manager = InterfaceObjectManagerOf(entry.ObjPtr);
            if (manager && manager->GetGuid() == DATA_GUID) managers.push_back(manager);
        }
        DocumentDataRecords actual;
        std::map<CK_ID, ObjectRef> references;
        for (const auto &entry : expected) {
            if (entry.second.owner) references.emplace(entry.second.owner.object, entry.second.owner);
            for (auto ref : entry.second.objects) if (ref) references.emplace(ref.object, ref);
        }
        const auto reference = [&](CK_ID id) -> ObjectRef {
            if (!id) return {};
            const auto found = original.find(id);
            if (found == original.end()) throw std::runtime_error("Unknown saved module object reference");
            const auto ref = references.find(found->second);
            if (ref == references.end()) throw std::runtime_error("Unexpected saved module object reference");
            return ref->second;
        };
        for (auto *manager : managers) for (int i = 0; i < manager->GetChunkCount(); ++i) {
            if (!manager->GetChunk(i)) throw std::runtime_error("Missing saved module data chunk");
            auto record = Decode(*manager->GetChunk(i), reference);
            if (!actual.emplace(Key(record), std::move(record)).second)
                throw std::runtime_error("Duplicate saved module data record");
        }
        if (actual == expected) return true;
        error = "Module document data or object references changed in the saved file";
    } catch (const std::exception &e) { error = e.what(); }
    catch (...) { error = "Verifying module document data raised an exception"; }
    return false;
}

} // namespace vdev

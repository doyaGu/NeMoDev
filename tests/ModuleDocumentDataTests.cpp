#include "VirtoolsDevTestSupport.h"

#include <SDL3/SDL_timer.h>
#include "core/document/DocumentData.h"
#include "core/layout/LayoutCommands.h"

namespace vdevtest {
namespace {
struct Files {
    std::filesystem::path path = std::filesystem::current_path() /
        ("module-document-data-" + std::to_string(SDL_GetTicksNS()));
    Files() { CHECK(std::filesystem::create_directory(path)); }
    ~Files() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    std::string File(const char *name) const { return (path / name).u8string(); }
};
std::shared_ptr<DocumentDataRegistry> Types(uint32_t version = 1) {
    auto types = std::make_shared<DocumentDataRegistry>(*DefaultDocumentDataRegistry());
    DocumentDataType type; type.id = "test.document-data"; type.version = version;
    std::string error;
    CHECK(types->Register(std::move(type), error));
    return types;
}
DocumentDataRecord Record(Document &doc, unsigned char byte, ObjectRef owner = {}, uint32_t version = 1) {
    return {"test.document-data", version, owner, {0, byte, 255, 0}, {}};
}
CK_ID Frame(Document &document, const char *name) {
    const auto created = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(created.ok);
    document.Context()->GetObject(created.created)->SetName(const_cast<CKSTRING>(name));
    return created.created;
}
const DocumentDataRecord &Read(const Document &doc, ObjectRef owner = {}) {
    const auto *record = FindDocumentData(doc, "test.document-data", owner);
    CHECK(record);
    return *record;
}
void Save(Document &doc, const std::string &path) {
    SaveReport report;
    if (!SaveDocumentAs(doc, path, {}, report)) {
        for (const auto &error : report.errors) std::cerr << error << '\n';
        CHECK(false);
    }
    CHECK(report.saved && report.documentSaved);
}
CK_ID Named(Document &doc, const char *name) {
    auto *object = doc.Context()->GetObjectByName(const_cast<CKSTRING>(name));
    CHECK(object);
    return object->GetID();
}
std::string Bytes(const std::string &path) {
    std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
class DataSaveCallback : public CKBaseManager {
public:
    explicit DataSaveCallback(CKContext *context)
        : CKBaseManager(context, CKGUID(0x1f2934bb, 0x671e8f93), "Module data save regression") {}
    CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PreSave | CKMANAGER_FUNC_PostSave; }
    CKERROR PreSave() override {
        if (!corrupt) return CK_OK;
        corrupt = false;
        const auto &objects = m_Context->GetObjectListByType(CKCID_INTERFACEOBJECTMANAGER, TRUE);
        for (int i = 0; i < objects.Size(); ++i) {
            auto *manager = InterfaceObjectManagerOf(objects[i]);
            if (!manager || manager->GetGuid() != CKGUID(0x4E4D4441, 0x44415441)) continue;
            CHECK(manager->GetChunkCount() == 1);
            auto *chunk = manager->GetChunk(0);
            chunk->StartRead(); CHECK(chunk->SeekIdentifier(0x4E4D4452));
            chunk->ReadDword(); // envelope version
            const auto idBytes = chunk->ReadDword();
            chunk->Goto(chunk->GetCurrentPos() + int((idBytes + 3) / 4));
            chunk->ReadDword(); chunk->ReadDword(); chunk->ReadObjectID();
            CHECK(chunk->ReadDword() == 4);
            // Corrupt valid file content without changing document-owned bytes.
            *static_cast<unsigned char *>(chunk->LockReadBuffer()) ^= 0x10;
            corrupted = true;
        }
        return CK_OK;
    }
    CKERROR PostSave() override {
        auto run = std::move(after); after = {};
        if (run) run();
        return CK_OK;
    }
    bool corrupt = false, corrupted = false;
    std::function<void()> after;
};
} // namespace

void ModuleDocumentEdits(CkRuntimeHost &host) {
    Document doc(host, Types());
    CHECK(doc.New(nullptr));
    auto first = Record(doc, 1);
    CHECK(SetDocumentData(doc, first).ok && doc.History().UndoCount() == 1);
    CHECK(SetDocumentData(doc, first).Unchanged() && doc.History().UndoCount() == 1);
    {
        Edit edit(doc, "Combined module edit");
        CHECK(SetDocumentData(doc, Record(doc, 2)).ok);
        CHECK(SetDocumentData(doc, Record(doc, 3)).ok);
        CHECK(edit.Commit().ok);
    }
    CHECK(Read(doc).bytes[1] == 3 && doc.History().UndoCount() == 2);
    CHECK(doc.History().Undo() && Read(doc) == first);
    const auto redos = doc.History().RedoLabel();
    {
        Edit cancelled(doc, "Cancelled module edit");
        CHECK(SetDocumentData(doc, Record(doc, 4)).ok);
        CHECK(RemoveDocumentData(doc, first.id).ok);
    }
    CHECK(Read(doc) == first && doc.History().RedoLabel() == redos);
    auto invalid = first; invalid.objects.push_back({{999}, 1, 1});
    CHECK(!SetDocumentData(doc, invalid).ok);
    invalid = first; invalid.owner = {{doc.Token().value}, 1, UINT64_MAX};
    CHECK(!SetDocumentData(doc, invalid).ok);
    invalid = first; invalid.id = "missing.data";
    CHECK(!SetDocumentData(doc, invalid).ok);
    CHECK(doc.History().Redo() && Read(doc).bytes[1] == 3);
    CHECK(RemoveDocumentData(doc, first.id).ok && !FindDocumentData(doc, first.id));
    CHECK(doc.History().Undo() && Read(doc).bytes[1] == 3);
    CHECK(doc.DataTypes()->Find("layout.display") && doc.DataTypes()->Find("object.pictures"));
    std::string error;
    auto types = Types(); DocumentDataType duplicate; duplicate.id = first.id;
    CHECK(!types->Register(duplicate, error));
}

void ModuleDocumentRoundTrip(CkRuntimeHost &host) {
    Files files;
    DocumentDataRecord expected;
    {
        Document doc(host, Types()); CHECK(doc.New(nullptr));
        const auto object = Frame(doc, "Module reference");
        auto data = Record(doc, 17); data.objects = {{}, doc.Reference(object), {}, doc.Reference(object)};
        CHECK(SetDocumentData(doc, data).ok);
        auto owned = Record(doc, 31, doc.Reference(object)); owned.bytes.clear();
        CHECK(SetDocumentData(doc, owned).ok);
        CHECK(SetFrameRepresentation(doc, {object}, 2).ok);
        Save(doc, files.File("known.cmo"));
        expected = data;
    }
    {
        Document unknown(host); CHECK(unknown.Open(files.File("known.cmo"), nullptr));
        auto actual = Read(unknown);
        const auto object = Named(unknown, "Module reference");
        CHECK(actual.bytes == expected.bytes && actual.version == 1 && actual.objects.size() == 4);
        CHECK(actual.objects[0] == ObjectRef{} && actual.objects[2] == ObjectRef{});
        CHECK(unknown.NativeId(actual.objects[1]) == object && actual.objects[1] == actual.objects[3]);
        CHECK(Read(unknown, unknown.Reference(object)).bytes.empty());
        CHECK(unknown.Layout().representations.at(object) == 2);
        CHECK(!SetDocumentData(unknown, actual).ok && !RemoveDocumentData(unknown, actual.id).ok);
        Save(unknown, files.File("unknown.cmo"));
    }
    {
        Document doc(host, Types()); CHECK(doc.New(nullptr));
        Frame(doc, "Force imported IDs to change");
        const auto history = doc.History().UndoCount();
        std::vector<CK_ID> added;
        CHECK(doc.Import(files.File("unknown.cmo"), Document::ImportMode::File, nullptr, &added));
        const auto object = Named(doc, "Module reference");
        CHECK(Read(doc).bytes == expected.bytes && doc.NativeId(Read(doc).objects[1]) == object);
        CHECK(Read(doc, doc.Reference(object)).bytes.empty());
        CHECK(doc.History().UndoCount() == history); // Import keeps its original semantics.
        Save(doc, files.File("imported.cmo"));
        auto prepared = doc.PrepareOpen(files.File("imported.cmo"), nullptr);
        CHECK(prepared); prepared->Commit();
        CHECK(Read(doc).objects[1].document == doc.Token() && doc.Resolve(Read(doc).objects[1]));
        CHECK(doc.Resolve(Read(doc, doc.Reference(Named(doc, "Module reference"))).owner));
    }
}

void ModuleDocumentVersions(CkRuntimeHost &host) {
    Files files;
    { Document doc(host, Types()); CHECK(doc.New(nullptr)); CHECK(SetDocumentData(doc, Record(doc, 1)).ok); Save(doc, files.File("v1.cmo")); }
    int upgrades = 0, fault = 0;
    auto types = std::make_shared<DocumentDataRegistry>(*DefaultDocumentDataRegistry());
    DocumentDataType type; type.id = "test.document-data"; type.version = 2;
    type.upgrade = [&](const auto &old, auto &out, auto &) {
        ++upgrades;
        if (fault == 1) return false;
        if (fault == 2) throw std::runtime_error("Injected upgrade failure");
        out = old; out.version = 2; out.bytes.push_back(42);
        if (fault == 3) out.owner = {{999}, 1, 1};
        return true;
    };
    std::string error; CHECK(types->Register(std::move(type), error));
    Document doc(host, types); CHECK(doc.New(nullptr));
    const auto object = Frame(doc, "Keep on failed upgrade");
    const auto reference = doc.Reference(object);
    CHECK(SetDocumentData(doc, Record(doc, 9, {}, 2)).ok);
    CHECK(doc.History().Undo());
    const auto state = doc.Data().records;
    const auto undo = doc.History().UndoCount();
    const auto redo = doc.History().RedoLabel();
    for (fault = 1; fault <= 3; ++fault) {
        CHECK(!doc.PrepareOpen(files.File("v1.cmo"), &error));
        CHECK(doc.Resolve(reference) && doc.Data().records == state);
        CHECK(doc.History().UndoCount() == undo && doc.History().RedoLabel() == redo);
    }
    fault = 0;
    auto prepared = doc.PrepareOpen(files.File("v1.cmo"), &error);
    CHECK(prepared); prepared->Commit();
    CHECK(Read(doc).version == 2 && Read(doc).bytes.back() == 42 && doc.Dirty());
    Save(doc, files.File("v2.cmo"));
    const auto calls = upgrades;
    prepared = doc.PrepareOpen(files.File("v2.cmo"), &error); CHECK(prepared); prepared->Commit();
    CHECK(upgrades == calls && !doc.Dirty());
    doc.Close();
    {
        Document older(host, Types()); CHECK(older.Open(files.File("v2.cmo"), &error));
        CHECK(Read(older).version == 2);
        CHECK(!SetDocumentData(older, Record(older, 3)).ok);
        CHECK(!RemoveDocumentData(older, "test.document-data").ok);
        Save(older, files.File("future.cmo"));
    }
}

void ModuleDocumentLoadRecovery(CkRuntimeHost &host) {
    Files files;
    {
        Document incoming(host, Types()); CHECK(incoming.New(nullptr));
        const auto object = Frame(incoming, "Incoming module owner");
        auto data = Record(incoming, 2); data.objects.push_back(incoming.Reference(object));
        CHECK(SetDocumentData(incoming, data).ok);
        CHECK(SetDocumentData(incoming, Record(incoming, 7, incoming.Reference(object))).ok);
        Save(incoming, files.File("incoming.cmo"));
    }
    int fault = 0, merges = 0;
    auto types = std::make_shared<DocumentDataRegistry>(*DefaultDocumentDataRegistry());
    DocumentDataType type; type.id = "test.document-data";
    type.merge = [&](const auto &current, const auto &incoming, auto &out, auto &) {
        ++merges;
        if (fault == 1) return false;
        if (fault == 2) throw std::runtime_error("Injected merge failure");
        out = current;
        out.bytes.insert(out.bytes.end(), incoming.bytes.begin(), incoming.bytes.end());
        out.objects = incoming.objects;
        if (fault == 3) out.objects = {{{999}, 1, 1}};
        return true;
    };
    std::string error; CHECK(types->Register(std::move(type), error));
    Document doc(host, types); CHECK(doc.New(nullptr));
    auto object = Frame(doc, "Existing module owner");
    const auto reference = doc.Reference(object);
    CHECK(SetDocumentData(doc, Record(doc, 1)).ok);
    CHECK(SetDocumentData(doc, Record(doc, 3)).ok);
    CHECK(doc.History().Undo()); // Preserve both Undo and Redo on failure.
    const auto state = doc.Data().records;
    const auto objects = doc.LoadedObjects();
    const auto revision = doc.ChangeRevision(), topology = doc.TopologyRevision();
    const auto undo = doc.History().UndoCount();
    const auto redo = doc.History().RedoLabel();
    for (fault = 1; fault <= 3; ++fault) for (int merge = 0; merge < 2; ++merge) {
        std::vector<CK_ID> output{object};
        const auto ok = merge ? doc.Merge(files.File("incoming.cmo"), false, &error, &output)
                              : doc.Import(files.File("incoming.cmo"), Document::ImportMode::File, &error, &output);
        CHECK(!ok && !error.empty() && output == std::vector<CK_ID>{object});
        CHECK(doc.Resolve(reference) && doc.Data().records == state && doc.LoadedObjects() == objects);
        CHECK(doc.History().UndoCount() == undo && doc.History().RedoLabel() == redo);
        CHECK(doc.ChangeRevision() == revision && doc.TopologyRevision() == topology);
        CHECK(!doc.Context()->GetObjectByName(const_cast<CKSTRING>("Incoming module owner")));
    }
    fault = 0;
    CHECK(doc.Merge(files.File("incoming.cmo"), false, &error));
    CHECK(merges == 7 && Read(doc).bytes.size() == 8);
    const auto incoming = Named(doc, "Incoming module owner");
    CHECK(doc.NativeId(Read(doc).objects.front()) == incoming);
    CHECK(Read(doc, doc.Reference(incoming)).bytes[1] == 7);
    CHECK(doc.History().UndoCount() == undo && doc.History().RedoLabel() == redo);
    CHECK(doc.History().Redo() && Read(doc).bytes[1] == 3);
    Save(doc, files.File("merged.cmo"));
    doc.Close();
    // Unknown conflicts reject without dropping either source's bytes.
    Document unknown(host); CHECK(unknown.Open(files.File("merged.cmo"), &error));
    const auto unknownBefore = unknown.Data().records;
    CHECK(!unknown.Merge(files.File("incoming.cmo"), false, &error));
    CHECK(unknown.Data().records == unknownBefore);
}

void ModuleDocumentFileFailures(CkRuntimeHost &host) {
    Files files;
    Document doc(host, Types()); CHECK(doc.New(nullptr));
    const auto object = Frame(doc, "Lifetime reference");
    auto record = Record(doc, 19); record.objects.push_back(doc.Reference(object));
    CHECK(SetDocumentData(doc, record).ok);
    const auto path = files.File("protected.cmo"); Save(doc, path);
    const auto original = Bytes(path);
    CHECK(SetDocumentData(doc, Record(doc, 20)).ok);
    SaveReport report;
    CHECK(!SaveDocumentAs(doc, path, {}, report));
    CHECK(Bytes(path) == original && Read(doc).bytes[1] == 20 && doc.Dirty());
    CHECK(!SaveDocumentAs(doc, files.File("missing/out.cmo"), {}, report));
    Save(doc, files.File("retry.cmo"));
    CHECK(doc.History().Undo() && Read(doc).objects.front() == record.objects.front());
    CHECK(doc.History().Redo() && Read(doc).objects.empty());
    CHECK(SetDocumentData(doc, record).ok);
    CHECK(DestroyLevelObjects(doc, {object}).ok);
    CHECK(Read(doc).objects.front() == ObjectRef{});
    const auto replacement = Frame(doc, "New lifetime");
    CHECK(!Read(doc).objects.front() && doc.IsLive(replacement));
    Save(doc, files.File("deleted.cmo"));
    doc.Close();
    Document empty(host, Types()); CHECK(empty.New(nullptr));
    Save(empty, files.File("empty.cmo"));
    CHECK(empty.Data().records.empty());
    // Only registered schemas: no extra native manager or empty data chunk.
    CHECK(empty.Context()->GetObjectListByType(CKCID_INTERFACEOBJECTMANAGER, TRUE).Size() == 0);
}

void ModuleDocumentSerializationFailures(CkRuntimeHost &host) {
    Files files;
    Document doc(host, Types()); CHECK(doc.New(nullptr));
    CHECK(SetDocumentData(doc, Record(doc, 1)).ok);
    const auto target = files.File("original.cmo"); Save(doc, target);
    const auto original = Bytes(target);
    CHECK(SetDocumentData(doc, Record(doc, 2)).ok);
    auto *callback = new DataSaveCallback(doc.Context());
    CHECK(doc.Context()->RegisterNewManager(callback) == CK_OK);
    callback->corrupt = true;
    SaveOptions overwrite; overwrite.allowOverwrite = true;
    SaveReport report;
    CHECK(!SaveDocumentAs(doc, target, overwrite, report));
    CHECK(callback->corrupted && !report.errors.empty());
    CHECK(Bytes(target) == original && Read(doc).bytes[1] == 2 && doc.Dirty());
    // A failed native writer must not poison the next save's cached chunks.
    Save(doc, files.File("retry.cmo"));
    callback->after = [&] { CHECK(SetDocumentData(doc, Record(doc, 3)).ok); };
    CHECK(SaveDocumentAs(doc, files.File("callback.cmo"), {}, report));
    CHECK(report.saved && !report.documentSaved && Read(doc).bytes[1] == 3 && doc.Dirty());
    auto prepared = doc.PrepareOpen(files.File("callback.cmo"), nullptr);
    CHECK(prepared && Read(doc).bytes[1] == 3);
    prepared->Commit();
    CHECK(Read(doc).bytes[1] == 2 && !doc.Dirty());
    CHECK(RemoveDocumentData(doc, "test.document-data").ok);
    Save(doc, files.File("removed.cmo"));
    CHECK(doc.Context()->GetObjectListByType(CKCID_INTERFACEOBJECTMANAGER, TRUE).Size() == 0);
    CHECK(doc.History().Undo() && Read(doc).bytes[1] == 2);
    Save(doc, files.File("restored.cmo"));
}

void ModuleDocumentInvalidFiles(CkRuntimeHost &host) {
    Files files;
    const std::string id = "test.document-data";
    for (int fault = 0; fault < 8; ++fault) {
        Document source(host); CHECK(source.New(nullptr));
        auto *manager = InterfaceObjectManagerOf(source.Context()->CreateObject(CKCID_INTERFACEOBJECTMANAGER));
        CHECK(manager); manager->SetGuid(CKGUID(0x4E4D4441, 0x44415441));
        for (int copy = 0; copy < (fault == 7 ? 2 : 1); ++copy) {
            auto *chunk = CreateCKStateChunk(1, nullptr); CHECK(chunk);
            chunk->StartWrite(); chunk->WriteIdentifier(0x4E4D4452);
            chunk->WriteDword(fault == 0 ? 999 : 1);
            if (fault == 1) chunk->WriteDword(UINT32_MAX);
            else {
                chunk->WriteBuffer(int(id.size()), const_cast<char *>(id.data()));
                chunk->WriteDword(fault == 2 ? 0 : 1);
                chunk->WriteDword(fault == 3 ? 1 : 0); chunk->WriteObjectID(0);
                if (fault == 4) chunk->WriteDword(UINT32_MAX);
                else {
                    chunk->WriteBuffer(0, nullptr);
                    chunk->WriteDword(fault == 5 ? UINT32_MAX : fault == 6 ? 1 : 0);
                }
            }
            chunk->CloseChunk(); manager->AddStateChunk(chunk);
        }
        auto *objects = CreateCKObjectArray();
        objects->InsertRear(source.Context()->GetCurrentLevel()->GetID()); objects->InsertRear(manager->GetID());
        const auto path = files.File(("invalid-" + std::to_string(fault) + ".cmo").c_str());
        CHECK(source.Context()->Save(const_cast<CKSTRING>(path.c_str()), objects, CK_STATESAVE_ALL) == CK_OK);
        DeleteCKObjectArray(objects); source.Close();
        Document doc(host, Types()); CHECK(doc.New(nullptr));
        const auto object = Frame(doc, "Keep after invalid data");
        const auto reference = doc.Reference(object);
        CHECK(SetDocumentData(doc, Record(doc, 5)).ok);
        CHECK(SetDocumentData(doc, Record(doc, 6)).ok && doc.History().Undo());
        const auto before = doc.Data().records;
        const auto undo = doc.History().UndoCount(); const auto redo = doc.History().RedoLabel();
        std::string error;
        CHECK(!doc.PrepareOpen(path, &error) && !error.empty());
        CHECK(!doc.Import(path, Document::ImportMode::File, &error) && !error.empty());
        CHECK(!doc.Merge(path, false, &error) && !error.empty());
        CHECK(doc.Resolve(reference) && doc.Data().records == before);
        CHECK(doc.History().UndoCount() == undo && doc.History().RedoLabel() == redo);
        CHECK(doc.History().Redo() && Read(doc).bytes[1] == 6);
    }
}
} // namespace vdevtest

#include "core/document/detail/NativeExport.h"

#include <map>
#include <set>
#include <sstream>

#include "core/document/Document.h"
#include "core/document/detail/PendingSaveFile.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/SdkCompat.h"
#include "core/schematic/InterfaceDataReconstruction.h"

namespace vdev {
namespace {

// Prepared chunks/flags live outside the protected writer. Restoration must
// also run when a save callback throws a structured exception on Windows.
class ExportSelection {
public:
    explicit ExportSelection(Document &document) : document(document), context(document.Context()),
                                                  generation(document.Host().Generation()), objects(CreateCKObjectArray()) {}
    ~ExportSelection() {
        const bool live = document.Context() == context && document.Host().Generation() == generation;
        for (const auto &entry : flags)
            if (live && context->GetObject(entry.first) == entry.second.first)
                entry.second.first->SetFlags(static_cast<CK_BEHAVIOR_FLAGS>(entry.second.second));
        for (const auto &entry : interfaces) {
            if (live && context->GetObject(entry.first) == entry.second.first)
                entry.second.first->SetInterfaceChunk(entry.second.second);
            else DeleteCKStateChunk(entry.second.second);
        }
        DeleteCKObjectArray(objects);
    }
    ExportSelection(const ExportSelection &) = delete;
    ExportSelection &operator=(const ExportSelection &) = delete;

    bool Prepare(const std::vector<CK_ID> &ids, NativeExportKind kind, bool hidden, std::string *error) {
        std::set<CKBehavior *> displays;
        std::set<CK_ID> listed;
        for (CK_ID id : ids) {
            CKObject *object = context->GetObject(id);
            if (!object || !listed.insert(id).second) continue;
            CKBehavior *behavior = CKBehavior::Cast(object);
            if (kind == NativeExportKind::Behaviors && !behavior) continue;
            objects->InsertRear(id);
            if (kind == NativeExportKind::Behaviors) {
                flags.emplace(id, std::make_pair(behavior, behavior->GetFlags()));
                behavior->SetFlags(static_cast<CK_BEHAVIOR_FLAGS>(behavior->GetFlags() | CKBEHAVIOR_TOPMOST));
                displays.insert(behavior);
            } else if (behavior) {
                if (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) displays.insert(behavior);
            } else if (CKBeObject *owner = CKBeObject::Cast(object)) {
                for (int i = 0; i < owner->GetScriptCount(); ++i)
                    if (CKBehavior *script = owner->GetScript(i)) displays.insert(script);
            }
        }
        if (!objects->GetCount()) {
            if (error) *error = "Select objects to save";
            return false;
        }
        for (CKBehavior *behavior : displays)
            if (!PrepareDisplay(behavior, hidden, error)) return false;
        return true;
    }
    CKObjectArray *Objects() const { return objects; }

private:
    bool PrepareDisplay(CKBehavior *behavior, bool hidden, std::string *error) {
        const bool script = (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) != 0;
        if (script)
            if (ScriptSession *session = document.ExistingSession(behavior->GetID()))
                if (session->Dirty()) {
                    InterfaceData data = session->Data();
                    CKStateChunk *chunk = GenerateInterfaceChunk(data, behavior);
                    if (!chunk) return DisplayError(error);
                    behavior->SetInterfaceChunk(chunk);
                }
        if (!hidden || behavior->GetType() == CKBEHAVIORTYPE_BASE) return true;
        InterfaceData data;
        ScriptSession *session = script ? document.Session(behavior->GetID()) : nullptr;
        if (session) data = session->Data();
        else {
            ReconstructInterfaceData(data, behavior);
            // Layout calculates a Script header's height even for a graph.
            // A graph's own chunk stores its root rectangle, not that header.
            data.header = Header();
        }
        data.observers.clear();
        data.rootBehavior.flags |= 0x20000; // UI_FLAG_HIDDEN, in the output only
        CKStateChunk *original = behavior->GetInterfaceChunk();
        CKStateChunk *retained = original ? CreateCKStateChunk(original) : nullptr;
        if (original && !retained) return DisplayError(error);
        interfaces.emplace(behavior->GetID(), std::make_pair(behavior, retained));
        CKStateChunk *chunk = GenerateInterfaceChunk(data, behavior);
        if (!chunk) return DisplayError(error);
        behavior->SetInterfaceChunk(chunk);
        return true;
    }
    static bool DisplayError(std::string *error) {
        if (error) *error = "Cannot prepare the exported InterfaceChunk";
        return false;
    }
    Document &document;
    CKContext *context;
    std::uint64_t generation;
    CKObjectArray *objects;
    std::map<CK_ID, std::pair<CKBehavior *, CKDWORD>> flags;
    std::map<CK_ID, std::pair<CKBehavior *, CKStateChunk *>> interfaces;
};

// CKFile's normal epilogue and destructor change these fields. A throwing
// reader/save callback can skip that epilogue; keep the guard outside SEH.
class ExportSaveState {
public:
    explicit ExportSaveState(CKContext *context)
        : context(context), saving(context->m_Saving), manager(context->m_CurrentManager),
          callback(context->m_UserLoadCallBack), callbackData(context->m_UserLoadCallBackArgs),
          general(context->m_GeneralLoadMode), objects3d(context->m_3DObjectsLoadMode),
          meshes(context->m_MeshLoadMode), materials(context->m_MatTexturesLoadMode) {}
    ~ExportSaveState() {
        context->m_Saving = saving;
        context->m_CurrentManager = manager;
        context->SetUserLoadCallback(callback, callbackData);
        context->SetAutomaticLoadMode(general, objects3d, meshes, materials);
    }
    ExportSaveState(const ExportSaveState &) = delete;
    ExportSaveState &operator=(const ExportSaveState &) = delete;

private:
    CKContext *context;
    CKBOOL saving;
    CKBaseManager *manager;
    CK_USERLOADCALLBACK callback;
    void *callbackData;
    CK_LOADMODE general, objects3d, meshes, materials;
};

bool VerifyExport(Document &document, const std::string &path, const std::map<CK_ID, CK_CLASSID> &expected,
                  SaveReport &report) {
    CKContext *context = nullptr;
    if (CreateCKContext(&context, nullptr) != CK_OK || !context) {
        report.errors.push_back("Cannot create the export verification context");
        return false;
    }
    CKFile *file = nullptr;
    CKObjectArray *objects = CreateCKObjectArray();
    CKERROR loaded = CKERR_INVALIDFILE;
    bool verified = false;
    const bool completed = GuardedCall([&] {
        context->SetInterfaceMode(TRUE, nullptr, nullptr);
        file = context->CreateCKFile();
        loaded = file->Load(const_cast<CKSTRING>(path.c_str()), objects, CK_LOAD_DEFAULT);
        if (loaded != CK_OK) return;
        std::map<CK_ID, CK_CLASSID> actual;
        for (int i = 0; i < file->m_FileObjects.Size(); ++i) {
            const auto &entry = file->m_FileObjects[i];
            if (!entry.ObjPtr) continue; // unresolved external references are allowed
            if (context->GetObject(entry.CreatedObject) != entry.ObjPtr) return;
            actual.emplace(entry.Object, entry.ObjPtr->GetClassID());
        }
        for (const auto &entry : expected) {
            const auto found = actual.find(entry.first);
            if (found == actual.end() || found->second != entry.second) return;
        }
        verified = true;
    }, &document.Host(), "Verify native export");
    const bool fileClosed = GuardedCall([&] { if (file) context->DeleteCKFile(file); },
                                       &document.Host(), "Close export verification file");
    DeleteCKObjectArray(objects);
    const bool contextClosed = GuardedCall([&] {
        context->SetInterfaceMode(FALSE, nullptr, nullptr);
        CKCloseContext(context);
    }, &document.Host(), "Close export verification context");
    if (!completed || loaded != CK_OK || !verified || !fileClosed || !contextClosed)
        report.errors.push_back("The exported file could not be verified; the destination was not replaced");
    return completed && loaded == CK_OK && verified && fileClosed && contextClosed;
}

bool WriteExport(Document &document, CKObjectArray *objects, PendingSaveFile &pending, SaveReport &report) {
    const std::string native = pending.Path().string();
    CKContext *context = document.Context();
    std::map<CK_ID, CK_CLASSID> expected;
    for (objects->Reset(); !objects->EndOfList(); objects->Next())
        if (CKObject *object = context->GetObject(objects->GetDataId())) expected.emplace(object->GetID(), object->GetClassID());
    CKERROR saved = CKERR_INVALIDFILE;
    bool completed = false, closed = false;
    {
        ExportSaveState state(context);
        CKFile *file = nullptr;
        completed = GuardedCall([&] {
            file = context->CreateCKFile();
            saved = file->StartSave(const_cast<CKSTRING>(native.c_str()));
            if (saved != CK_OK) return;
            objects->Reset();
            file->SaveObjects(objects);
            saved = file->EndSave();
        }, &document.Host(), "Write native export");
        closed = GuardedCall([&] { if (file) context->DeleteCKFile(file); }, &document.Host(), "Close native export file");
    }
    if (!completed || !closed) {
        report.errors.push_back("Native export raised an exception; the destination was not replaced");
        return false;
    }
    if (saved != CK_OK) {
        report.errors.push_back("Native export failed: " + std::to_string(saved));
        return false;
    }
    return VerifyExport(document, native, expected, report) && pending.Publish(true);
}

} // namespace

bool SaveNativeExport(Document &document, const std::vector<CK_ID> &objects, const std::string &path,
                      std::string *error, NativeExportKind kind, bool hideRepresentation) {
    if (!document.Context()) {
        if (error) *error = "The runtime is not started";
        return false;
    }
    ExportSelection selection(document);
    bool prepared = false;
    if (!GuardedCall([&] { prepared = selection.Prepare(objects, kind, hideRepresentation, error); },
                     &document.Host(), "Prepare native export")) {
        if (error) *error = "Preparing the export raised an exception";
        return false;
    }
    if (!prepared) return false;
    SaveReport report;
    bool saved = false;
    try {
        PendingSaveFile pending(std::filesystem::u8path(path), report);
        if (pending.Create()) saved = WriteExport(document, selection.Objects(), pending, report);
    } catch (const std::system_error &failure) {
        report.errors.push_back("Cannot prepare the export path: " + std::string(failure.what()));
    }
    if (error && !saved) {
        std::ostringstream message;
        for (const auto &item : report.errors) { if (message.tellp() > 0) message << "; "; message << item; }
        for (const auto &item : report.notes) { if (message.tellp() > 0) message << "; "; message << item; }
        *error = message.str();
    }
    for (const auto &note : report.notes) document.Host().Log(LogLevel::Warning, note);
    return saved;
}

} // namespace vdev

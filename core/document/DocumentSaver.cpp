#include "core/document/DocumentSaver.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/SdkCompat.h"
#include "core/interface/InterfaceDataDiff.h"
#include "core/interface/InterfaceDataValidation.h"
#include "core/document/detail/ProjectParameterTypes.h"
#include "core/document/Document.h"
#include "core/document/ObjectPictures.h"
#include "core/document/SpriteTextPersistence.h"

namespace vdev {

namespace {

namespace fs = std::filesystem;

struct Replacement {
    CK_ID id = 0;
    CKBehavior *script = nullptr;
    CKStateChunk *chunk = nullptr;
    size_t behaviorCount = 0;
    size_t linkCount = 0;
};

size_t CountLinks(const InterfaceData &data) {
    size_t count = data.rootBehavior.links.size();
    for (const auto &record : data.behaviors) count += record.links.size();
    return count;
}

std::string NameOf(CKObject *object) {
    return object && object->GetName() ? object->GetName() : std::string();
}

} // namespace

// The sets behind Document's queries, which only the saver reads.
struct DocumentSaverAccess {
    static const std::set<CK_ID> &Graveyard(const Document &document) { return document.m_Graveyard; }
};

namespace {

// Graveyard objects plus everything they own (IOs, parameters, links,
// operations and sub-behaviors), which the load array lists separately.
std::set<CK_ID> ExcludedObjects(CKContext *context, const std::set<CK_ID> &graveyard) {
    std::set<CK_ID> excluded;
    std::vector<CKObject *> pending;
    for (CK_ID id : graveyard)
        if (CKObject *object = context->GetObject(id)) pending.push_back(object);
    while (!pending.empty()) {
        CKObject *object = pending.back();
        pending.pop_back();
        if (!object || !excluded.insert(object->GetID()).second) continue;
        if (CKBehavior *behavior = CKBehavior::Cast(object)) {
            for (int i = 0; i < behavior->GetInputCount(); ++i) pending.push_back(behavior->GetInput(i));
            for (int i = 0; i < behavior->GetOutputCount(); ++i) pending.push_back(behavior->GetOutput(i));
            for (int i = 0; i < behavior->GetInputParameterCount(); ++i)
                pending.push_back(behavior->GetInputParameter(i));
            for (int i = 0; i < behavior->GetOutputParameterCount(); ++i)
                pending.push_back(behavior->GetOutputParameter(i));
            for (int i = 0; i < behavior->GetLocalParameterCount(); ++i)
                pending.push_back(behavior->GetLocalParameter(i));
            if (behavior->GetTargetParameter()) pending.push_back(behavior->GetTargetParameter());
            for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) pending.push_back(behavior->GetSubBehavior(i));
            for (int i = 0; i < behavior->GetSubBehaviorLinkCount(); ++i)
                pending.push_back(behavior->GetSubBehaviorLink(i));
            for (int i = 0; i < behavior->GetParameterOperationCount(); ++i)
                pending.push_back(behavior->GetParameterOperation(i));
        } else if (CKParameterOperation *operation = CKParameterOperation::Cast(object)) {
            pending.push_back(operation->GetInParameter1());
            pending.push_back(operation->GetInParameter2());
            pending.push_back(operation->GetOutParameter());
        }
    }
    return excluded;
}

// Object parameters may refer to objects that Building Blocks or managers
// create while loading: never in the file, recreated with new IDs by every
// load (Find Path.cmo FindPathGroup, flagged CK_OBJECT_NOTTOBESAVED; Vertex
// Render.cmo MLMaterial and IK Position.cmo IKPosChain, not flagged). Such a
// reference is recorded as this marker on both sides. A reference to an
// object loaded from the source file or created by the editor must survive
// the save exactly: the reloaded value then maps to a saved ID.
constexpr CK_ID RUNTIME_ONLY_OBJECT = 0xFFFFFFFF;

void MarkRuntimeOnlyReferences(Document &document, ProjectParameterTypes::Snapshot &snapshot) {
    std::set<CK_ID> documentObjects(document.LoadedObjects().begin(), document.LoadedObjects().end());
    documentObjects.insert(document.CreatedObjects().begin(), document.CreatedObjects().end());
    CKContext *context = document.Context();
    for (auto &entry : snapshot) {
        ProjectParameterTypes::State &state = entry.second;
        if (!state.hasObjectValue || !state.objectValue) continue;
        CKObject *object = context->GetObject(state.objectValue);
        const bool runtimeOnly = !documentObjects.count(state.objectValue) ||
                                 (object && (object->GetObjectFlags() & CK_OBJECT_NOTTOBESAVED));
        if (runtimeOnly) state.objectValue = RUNTIME_ONLY_OBJECT;
    }
}

// Snapshot of the reloaded file keyed by saved IDs (CKFileObject::Object).
bool CaptureSaved(CKFile &file, ProjectParameterTypes::Snapshot &result, std::ostream &errors) {
    ProjectParameterTypes::Snapshot runtime;
    if (!ProjectParameterTypes::Capture(file, false, runtime, errors)) return false;
    std::map<CK_ID, CK_ID> saved;
    for (int i = 0; i < file.m_FileObjects.Size(); ++i)
        if (file.m_FileObjects[i].ObjPtr) saved[file.m_FileObjects[i].ObjPtr->GetID()] = file.m_FileObjects[i].Object;
    result.clear();
    for (auto entry : runtime) {
        auto key = saved.find(entry.first);
        if (key == saved.end()) {
            errors << "Parameter has no saved ID: " << entry.second.name << '\n';
            return false;
        }
        ProjectParameterTypes::State &state = entry.second;
        // A <This> parameter takes its Script's owner, and the owner's class as
        // its type, when the file loads (none for a Script no object holds:
        // Attach Script.cmo's): value and type not compared.
        if (CKParameterLocal *local = CKParameterLocal::Cast(file.m_Context->GetObject(entry.first)))
            if (local->IsMyselfParameter()) {
                state.hasObjectValue = false;
                state.objectValue = 0;
                state.guid = CKGUID();
            }
        if (state.hasObjectValue && state.objectValue) {
            auto value = saved.find(state.objectValue);
            // Not a saved object: created by the reload itself. The comparison
            // fails unless the original also referred to a runtime object.
            state.objectValue = value != saved.end() ? value->second : RUNTIME_ONLY_OBJECT;
        }
        result.emplace(key->second, state);
    }
    return true;
}

// A file for the player (CKFILE_FORVIEWER) has no InterfaceChunk to check and
// is loaded as a player loads it, outside interface mode.
bool Verify(Document &document, const std::string &path, const ProjectParameterTypes::Snapshot &expected,
            const std::vector<Replacement> &replacements, const std::map<CK_ID, int> &alignments,
            bool forViewer, SaveReport &report) {
    CKContext *verification = nullptr;
    const CKERROR created = CreateCKContext(&verification, nullptr);
    if (created != CK_OK || !verification) {
        report.errors.push_back("Cannot create the verification CKContext");
        return false;
    }
    if (!forViewer) verification->SetInterfaceMode(TRUE, nullptr, nullptr);
    CKFile *file = verification->CreateCKFile();
    CKObjectArray *objects = CreateCKObjectArray();
    CKERROR loaded = CKERR_INVALIDFILE;
    const bool completed = GuardedCall([&] {
                    loaded = file->Load(const_cast<CKSTRING>(path.c_str()), objects, CK_LOAD_DEFAULT);
                    if (loaded == CK_OK && !forViewer) RestoreSpriteTextAlignments(*file);
                },
                &document.Host(), "verification CKFile::Load");
    bool ok = completed && loaded == CK_OK;
    if (!completed) report.errors.push_back("The saved file raised an exception while loading in a new CKContext");
    else if (!ok) report.errors.push_back("The saved file does not load in a new CKContext: " + std::to_string(loaded));
    // Objects the load destroyed again (Scripts of a level scene) leave stale
    // pointers in the file's object list.
    for (int i = 0; ok && i < file->m_FileObjects.Size(); ++i) {
        const CKFileObject &entry = file->m_FileObjects[i];
        if (entry.ObjPtr && verification->GetObject(entry.CreatedObject) != entry.ObjPtr) {
            CKObject *original = document.Context()->GetObject(entry.Object);
            report.errors.push_back("Objects are lost when the saved file loads, first: " + NameOf(original) +
                                    " (class " + std::to_string(entry.ObjectCid) + ")");
            ok = false;
        }
    }

    if (ok) {
        std::ostringstream errors;
        ProjectParameterTypes::Snapshot actual;
        if (!CaptureSaved(*file, actual, errors) ||
            !ProjectParameterTypes::Compare(expected, actual, errors)) {
            report.errors.push_back("Parameter types, object references or enum values changed: " + errors.str());
            ok = false;
            if (std::getenv("SCHEMATIC_SAVE_TRACE"))
                for (const auto &item : expected) {
                    const auto found = actual.find(item.first);
                    if (found == actual.end() || (item.second.objectValue == found->second.objectValue &&
                                                  item.second.guid == found->second.guid))
                        continue;
                    CKParameterLocal *local = CKParameterLocal::Cast(document.Context()->GetObject(item.first));
                    std::fprintf(stderr, "differs: %s id=%u object %u -> %u, myself %d, owner %u\n", item.second.name.c_str(),
                                 (unsigned) item.first, (unsigned) item.second.objectValue, (unsigned) found->second.objectValue,
                                 local && local->IsMyselfParameter() ? 1 : 0,
                                 local && local->GetOwner() ? (unsigned) local->GetOwner()->GetID() : 0u);
                    break;
                }
            if (std::getenv("SCHEMATIC_SAVE_TRACE"))
                for (int i = 0; i < file->m_FileObjects.Size(); ++i) {
                    const auto &entry = file->m_FileObjects[i];
                    if (entry.ObjPtr) continue;
                    CKObject *original = document.Context()->GetObject(entry.Object);
                    CKObject *owner = nullptr;
                    if (CKParameter *parameter = CKParameter::Cast(original)) owner = parameter->GetOwner();
                    if (CKParameterIn *input = CKParameterIn::Cast(original)) owner = input->GetOwner();
                    std::fprintf(stderr, "not loaded %u class %d '%s' owner %u '%s' graveyard %d/%d\n",
                                 static_cast<unsigned>(entry.Object), entry.ObjectCid,
                                 original && original->GetName() ? original->GetName() : "",
                                 owner ? static_cast<unsigned>(owner->GetID()) : 0u,
                                 owner && owner->GetName() ? owner->GetName() : "",
                                 static_cast<int>(document.IsDiscarded(entry.Object)),
                                 owner ? static_cast<int>(document.IsDiscarded(owner->GetID())) : -1);
                }
        }
        report.parametersVerified = static_cast<int>(actual.size());
    }
    if (ok && !forViewer) {
        if (CaptureSpriteTextAlignments(*file) != alignments) {
            report.errors.push_back("Sprite Text alignments changed when the saved file loaded in a new CKContext");
            ok = false;
        } else {
            report.spriteTextsVerified = static_cast<int>(alignments.size());
        }
    }
    if (ok && !forViewer) {
        // CKFileObject::Object is the saved ID, i.e. the runtime ID at save time.
        std::map<CK_ID, CKObject *> bySavedId;
        for (int i = 0; i < file->m_FileObjects.Size(); ++i)
            if (file->m_FileObjects[i].ObjPtr) bySavedId[file->m_FileObjects[i].Object] = file->m_FileObjects[i].ObjPtr;
        for (const auto &replacement : replacements) {
            auto found = bySavedId.find(replacement.id);
            CKBehavior *script = found != bySavedId.end() ? CKBehavior::Cast(found->second) : nullptr;
            InterfaceData reloaded;
            CKERROR extracted = CKERR_NOTFOUND;
            if (script)
                GuardedCall([&] { extracted = ExtractInterfaceData(script, reloaded); }, &document.Host(),
                            "verification ExtractInterfaceData");
            if (extracted != CK_OK || reloaded.behaviors.size() != replacement.behaviorCount ||
                CountLinks(reloaded) != replacement.linkCount) {
                report.errors.push_back("Rewritten InterfaceChunk does not reload: " + NameOf(replacement.script));
                ok = false;
                continue;
            }
            ++report.scriptsVerified;
        }
    }
    verification->DeleteCKFile(file);
    DeleteCKObjectArray(objects);
    if (!forViewer) verification->SetInterfaceMode(FALSE, nullptr, nullptr);
    CKCloseContext(verification);
    return ok;
}

} // namespace

bool SaveDocumentAs(Document &document, const std::string &path, const SaveOptions &options,
                    SaveReport &report) {
    report = SaveReport();
    CKContext *context = document.Context();
    if (!document.IsOpen() || !context) {
        report.errors.push_back("No composition is open");
        return false;
    }
    std::error_code ec;
    const fs::path output = fs::absolute(path, ec);
    const fs::path source = fs::absolute(document.SourcePath(), ec);
    if (output == source || fs::equivalent(output, source, ec)) {
        report.errors.push_back("The loaded source file is never overwritten; choose a new file");
        return false;
    }
    if (fs::exists(output, ec) && !options.allowOverwrite) {
        report.errors.push_back("The output file already exists");
        return false;
    }

    // 1. Serialize and reload every edited Script before installing anything.
    std::vector<Replacement> replacements;
    auto discard = [&] {
        for (auto &replacement : replacements) DeleteCKStateChunk(replacement.chunk);
        replacements.clear();
    };
    // Without Save Script Snapshot every Script with a snapshot is written
    // again without it (its session made now if needed).
    if (!options.saveScriptSnapshots)
        for (const ScriptInfo &info : document.Scripts())
            if (info.hasInterfaceChunk && !document.IsDiscarded(info.id)) document.Session(info.id);
    for (const auto &entry : document.Sessions()) {
        ScriptSession &session = *entry.second;
        if (session.ReadOnly()) continue;
        const bool dropSnapshot = !options.saveScriptSnapshots && session.Data().header.snapshot != nullptr;
        if (!session.Dirty() && !dropSnapshot) continue;
        CKBehavior *script = CKBehavior::Cast(context->GetObject(entry.first));
        if (!script || document.IsDiscarded(entry.first)) continue;
        const std::string name = NameOf(script);
        InterfaceData written = session.Data();
        if (dropSnapshot) written.header.SetSnapshot(nullptr);
        const auto issues = ValidateInterfaceData(written, script, InterfaceDataValidationStage::BeforeSaveToChunk);
        if (!issues.empty()) {
            for (const auto &issue : issues)
                report.errors.push_back(name + ": behavior " + std::to_string(issue.behaviorId) + " link " +
                                        std::to_string(issue.linkIndex) + " " + issue.field + ": " + issue.reason);
            discard();
            return false;
        }
        CKStateChunk *chunk = GenerateInterfaceChunk(written, script);
        if (!chunk) {
            report.errors.push_back(name + ": InterfaceChunk generation failed");
            discard();
            return false;
        }
        InterfaceData reloaded;
        const CKERROR error = reloaded.LoadFromChunk(script, chunk);
        InterfaceDataDiff::Config config;
        // Parameter::id of rebuilt shortcuts is not stored in display tables.
        config.ignoreSyntheticParameterIds = session.Origin() == SessionOrigin::Reconstructed;
        const auto diff = InterfaceDataDiff(config).Compare(written, reloaded);
        if (error != CK_OK || !diff.identical) {
            if (std::getenv("SCHEMATIC_SAVE_TRACE")) std::fprintf(stderr, "%s\n", diff.GetDetailedReport().c_str());
            DeleteCKStateChunk(chunk);
            report.errors.push_back(name + ": generated InterfaceChunk does not reload identically");
            discard();
            return false;
        }
        replacements.push_back({entry.first, script, chunk, written.behaviors.size(), CountLinks(written)});
    }

    // 2. Install. CKBehavior owns installed chunks.
    for (auto &replacement : replacements) replacement.script->SetInterfaceChunk(replacement.chunk);
    report.chunksWritten = static_cast<int>(replacements.size());

    // 3. Save the loaded and created objects, minus soft-deleted ones.
    WriteSpriteTextAlignments(document);
    const std::set<CK_ID> excluded = ExcludedObjects(context, DocumentSaverAccess::Graveyard(document));
    CKObjectArray *objects = CreateCKObjectArray();
    std::set<CK_ID> listed;
    auto list = [&](CK_ID id) {
        if (!excluded.count(id) && context->GetObject(id) && listed.insert(id).second) objects->InsertRear(id);
    };
    // The current level first: loading merges the file's level into the
    // level of the context, so it is not among the loaded objects; without
    // it the scenes (settings, objects, initial conditions) are not saved.
    if (CKLevel *level = context->GetCurrentLevel()) list(level->GetID());
    for (CK_ID id : document.LoadedObjects()) list(id);
    for (CK_ID id : document.CreatedObjects()) list(id);
    const std::string outputName = output.string();
    const CK_FILE_WRITEMODE writeMode = context->GetFileWriteMode();
    const bool forViewer = ((writeMode | options.writeMode) & CKFILE_FORVIEWER) != 0;
    context->SetFileWriteMode(static_cast<CK_FILE_WRITEMODE>(writeMode | options.writeMode));
    CKFile *file = context->CreateCKFile();
    CKERROR saved = CKERR_INVALIDFILE;
    ProjectParameterTypes::Snapshot expected;
    std::map<CK_ID, int> alignments;
    std::ostringstream captureErrors;
    bool captured = false;
    // The Building Blocks' and managers' save callbacks run here: one that
    // raises fails the save instead of ending the editor with the work unsaved.
    const bool completed = GuardedCall(
        [&] {
            saved = file->StartSave(const_cast<CKSTRING>(outputName.c_str()));
            if (saved != CK_OK) return;
            objects->Reset();
            file->SaveObjects(objects);
            alignments = CaptureSpriteTextAlignments(*file);
            // The saving CKFile lists every written object (with dependencies)
            // until EndSave; saved IDs are the current runtime IDs.
            captured = ProjectParameterTypes::Capture(*file, false, expected, captureErrors);
            // <This> values and types come from the owners at load (CaptureSaved): not compared.
            for (auto &item : expected)
                if (CKParameterLocal *local = CKParameterLocal::Cast(context->GetObject(item.first)))
                    if (local->IsMyselfParameter()) {
                        item.second.hasObjectValue = false;
                        item.second.objectValue = 0;
                        item.second.guid = CKGUID();
                    }
            MarkRuntimeOnlyReferences(document, expected);
            saved = file->EndSave();
        },
        &document.Host(), "CKFile::Save");
    GuardedCall([&] { context->DeleteCKFile(file); }, &document.Host(), "CKContext::DeleteCKFile");
    DeleteCKObjectArray(objects);
    context->SetFileWriteMode(writeMode);
    if (!completed) {
        report.errors.push_back("CKFile save raised an exception (the file may be incomplete)");
        return false;
    }
    if (saved != CK_OK) {
        report.errors.push_back("CKFile save failed: " + std::to_string(saved));
        return false;
    }
    // A composition without Scripts (File > New) may have no parameters to compare.
    if (!captured || (expected.empty() && !document.Scripts().empty())) {
        report.errors.push_back("Cannot capture saved parameter types: " + captureErrors.str());
        return false;
    }
    report.saved = true;

    // 4. Independent reload.
    if (!Verify(document, outputName, expected, replacements, alignments, forViewer, report)) {
        report.notes.push_back("The file was written but failed verification; do not use it");
        return false;
    }
    if (options.markSaved) document.MarkSaved();
    report.notes.push_back("Native Dev load/save/reopen has not been performed");
    return true;
}

bool SaveComposition(Document &document, const std::string &path, const CompositionData &data,
                     const SaveOptions &options, SaveReport &report) {
    if (data.layout) WriteLayoutInterfaceData(document, *data.layout);
    if (data.pictures) WriteObjectPictures(document, *data.pictures);
    return SaveDocumentAs(document, path, options, report);
}

} // namespace vdev

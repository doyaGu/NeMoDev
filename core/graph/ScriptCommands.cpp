#include "core/graph/ScriptCommands.h"

#include <memory>
#include <string>

#include "core/runtime/CkRuntimeHost.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/document/ScriptSession.h"
#include "core/graph/detail/TopologyInternal.h"
#include "core/document/Edit.h"

namespace vdev {

namespace {

CK_ID Mapped(const std::map<CK_ID, CK_ID> &map, CK_ID id) {
    auto found = map.find(id);
    return found != map.end() ? found->second : id;
}

// A Script given to its owner (at the end of its Scripts) or taken from it.
bool Attach(Document &document, CK_ID scriptId, CK_ID ownerId) {
    CKBehavior *script = CKBehavior::Cast(document.Context()->GetObject(scriptId));
    CKBeObject *owner = CKBeObject::Cast(document.Context()->GetObject(ownerId));
    if (!script || !owner) return false;
    for (int i = 0; i < owner->GetScriptCount(); ++i)
        if (owner->GetScript(i) == script) return true;
    if (AddScriptLast(owner, script) != CK_OK) return false;
    for (int i = 0; i < owner->GetScriptCount(); ++i)
        if (owner->GetScript(i) == script) return true;
    return false;
}

bool Detach(Document &document, CK_ID scriptId, CK_ID ownerId) {
    CKBehavior *script = CKBehavior::Cast(document.Context()->GetObject(scriptId));
    CKBeObject *owner = CKBeObject::Cast(document.Context()->GetObject(ownerId));
    if (!script || !owner) return false;
    bool present = false;
    for (int i = 0; i < owner->GetScriptCount(); ++i)
        if (owner->GetScript(i) == script) present = true;
    if (present) owner->RemoveScript(scriptId);
    for (int i = 0; i < owner->GetScriptCount(); ++i)
        if (owner->GetScript(i) == script) return false;
    return true;
}

} // namespace

void RemapInterfaceData(InterfaceData &data, const std::map<CK_ID, CK_ID> &map) {
    auto record = [&](BehaviorData &behavior) {
        behavior.id = Mapped(map, behavior.id);
        behavior.parentId = Mapped(map, behavior.parentId);
        for (auto &link : behavior.links) {
            link.id = Mapped(map, link.id);
            link.start.id = Mapped(map, link.start.id);
            link.end.id = Mapped(map, link.end.id);
        }
        for (auto &operation : behavior.operations) operation.id = Mapped(map, operation.id);
        for (auto &local : behavior.localParams)
            if (local.id) local.id = Mapped(map, local.id);
        for (auto &shortcut : behavior.sharedParams) {
            shortcut.id = Mapped(map, shortcut.id);
            shortcut.sourceId = Mapped(map, shortcut.sourceId);
        }
    };
    record(data.rootBehavior);
    for (auto &behavior : data.behaviors) record(behavior);
}

EditResult DeleteScript(Document &document, CK_ID scriptId) {
    CKBehavior *script = CKBehavior::Cast(document.Context()->GetObject(scriptId));
    CKBeObject *owner = script ? script->GetOwner() : nullptr;
    if (!script || !(script->GetType() & CKBEHAVIORTYPE_SCRIPT) || !owner) return EditResult::Refused("Select a Script");
    const CK_ID ownerId = owner->GetID();
    Edit edit(document, "Delete Script");
    if (!edit.Do([&document, scriptId, ownerId] { return Detach(document, scriptId, ownerId); },
                 [&document, scriptId, ownerId] { return Attach(document, scriptId, ownerId); }))
        return edit.Fail("The Script could not be taken from its owner");
    edit.Bury(scriptId);
    return edit.Commit();
}

std::vector<EditResult> PasteScriptsOn(Document &document, const std::vector<CK_ID> &scripts,
                                       const std::vector<CK_ID> &owners, bool markCopies) {
    EditGroup group(document, "Paste Script");
    std::vector<EditResult> results;
    for (CK_ID owner : owners) results.push_back(PasteScripts(document, scripts, owner, markCopies));
    return results;
}

EditResult PasteScripts(Document &document, const std::vector<CK_ID> &scripts, CK_ID ownerId, bool markCopies) {
    CKContext *context = document.Context();
    CKBeObject *owner = CKBeObject::Cast(context->GetObject(ownerId));
    if (!owner) {
        return EditResult::Refused("Select an object to receive the Scripts");
    }
    if (!context->GetCurrentLevel()) {
        return EditResult::Refused("CKContext::CopyObjects needs a current level");
    }
    XObjectArray originals;
    for (CK_ID id : scripts)
        if (CKBehavior *script = CKBehavior::Cast(context->GetObject(id)))
            if (script->GetType() & CKBEHAVIORTYPE_SCRIPT) originals.PushBack(id);
    if (!originals.Size()) {
        return EditResult::Refused("Copy Scripts first (Script header: Copy)");
    }
    XObjectArray copies;
    GuardedCall([&] { copies = context->CopyObjects(originals, CKGetDefaultClassDependencies(CK_DEPENDENCIES_COPY)); },
                &document.Host(), "CKContext::CopyObjects");
    if (copies.Size() != originals.Size()) {
        return EditResult::Refused("CKContext::CopyObjects failed");
    }
    // The copies join the composition with the step; one the owner does not
    // take is never part of it.
    Edit edit(document, "Paste Script");
    std::vector<CK_ID> attached;
    std::string refusal;
    for (int i = 0; i < copies.Size(); ++i) {
        CKBehavior *original = CKBehavior::Cast(context->GetObject(originals[i]));
        CKBehavior *copy = CKBehavior::Cast(context->GetObject(copies[i]));
        if (!copy) continue;
        if (markCopies && original && original->GetOwner() == owner) {
            const std::string name = std::string(original->GetName() ? original->GetName() : "") + ".Copy";
            copy->SetName(const_cast<CKSTRING>(name.c_str()));
        }
        const CK_ID copyId = copy->GetID();
        if (!edit.Do([&document, copyId, ownerId] { return Attach(document, copyId, ownerId); },
                     [&document, copyId, ownerId] { return Detach(document, copyId, ownerId); })) {
            edit.Created(copyId); // made, and never part of the composition
            edit.Bury(copyId);
            refusal = std::string("The owner's class does not accept ") + (copy->GetName() ? copy->GetName() : "");
            continue;
        }
        edit.Created(copyId);
        CopyScriptDisplay(document, original, copy);
        attached.push_back(copyId);
    }
    if (attached.empty()) return edit.Fail(refusal);
    return edit.Commit(std::to_string(attached.size()) + " Script(s)", attached.front());
}

void CopyScriptDisplay(Document &document, CKBehavior *original, CKBehavior *copy) {
    if (!original || !copy) return;
    std::map<CK_ID, CK_ID> map;
    ScriptSession *session = document.Session(original->GetID());
    if (!session || !detail::MapBehaviorCopy(original, copy, map)) return;
    InterfaceData data = session->Data();
    data.observers.clear();
    RemapInterfaceData(data, map);
    // CopyObjects may share the original's chunk with the copy: never let CK
    // free a chunk the original still uses.
    CKStateChunk *previous = copy->GetInterfaceChunk();
    if (CKStateChunk *chunk = GenerateInterfaceChunk(data, copy)) {
        if (previous && previous == original->GetInterfaceChunk()) {
            struct ChunkAccess : CKBehavior {
                static void Detach(CKBehavior *behavior) { static_cast<ChunkAccess *>(behavior)->m_InterfaceChunk = nullptr; }
            };
            ChunkAccess::Detach(copy);
        }
        copy->SetInterfaceChunk(chunk);
    }
}

bool SetScriptSnapshot(Document &document, CK_ID script, void *bitmap) {
    ScriptSession *session = document.Session(script);
    if (!session || !(session->Data().rootBehavior.id == script)) return false;
    return EditScriptData(document, script, "Load Snapshot for Script",
                          [bitmap](InterfaceData &data) { data.header.SetSnapshot(bitmap); });
}

} // namespace vdev

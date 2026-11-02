#include "core/graph/ScriptCommands.h"

#include <memory>
#include <string>
#include <algorithm>
#include <cstring>
#include <SDL3/SDL.h>
#include "core/runtime/ImageFiles.h"
#include "core/runtime/TextEncoding.h"

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

class ScriptAttachmentAction final : public Action {
public:
    ScriptAttachmentAction(Document &document, CK_ID script, CK_ID owner, bool attach)
        : m_Script(document.Reference(script)), m_Owner(document.Reference(owner)), m_Attach(attach) {}
    bool Redo(Document &document) override { return Apply(document, m_Attach); }
    bool Undo(Document &document) override { return Apply(document, !m_Attach); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        for (auto ref : {m_Script, m_Owner})
            if (const auto id = document.NativeId(ref, Document::ObjectScope::Existing))
                objects.insert(id);
    }

private:
    bool Apply(Document &document, bool attach) {
        const auto script = document.NativeId(m_Script, Document::ObjectScope::Existing);
        const auto owner = document.NativeId(m_Owner, Document::ObjectScope::Existing);
        return script && owner && (attach ? Attach(document, script, owner) : Detach(document, script, owner));
    }
    ObjectRef m_Script{}, m_Owner{};
    bool m_Attach;
};

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
    if (!edit.Do(std::make_unique<ScriptAttachmentAction>(document, scriptId, ownerId, false)))
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
        if (!edit.Do(std::make_unique<ScriptAttachmentAction>(document, copyId, ownerId, true))) {
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
    ScriptSession detached;
    // A retained clipboard source may already be absent from Scripts(),
    // without ever having had a Schematic session opened before deletion.
    if (!session && detached.Open(document.Context(), original->GetID())) session = &detached;
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
                          [bitmap, owner = document.BitmapOwner(bitmap)](InterfaceData &data) {
                              data.header.SetSnapshot(bitmap, owner);
                          });
}

void *LoadSnapshotBitmap(const std::wstring &path, int maxWidth, int maxHeight, std::string *error) {
    std::vector<unsigned char> source;
    int width = 0, height = 0;
    if (maxWidth <= 0 || maxHeight <= 0 || !ReadImageRgba(WideToUtf8(path), source, width, height)) {
        if (error)
            *error = "Cannot decode the snapshot image";
        return nullptr;
    }
    const double scale = std::min(1.0, std::min(double(maxWidth) / width, double(maxHeight) / height));
    const int w = std::max(1, int(width * scale + 0.5)), h = std::max(1, int(height * scale + 0.5));
    SDL_Surface *input = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, source.data(), width * 4);
    SDL_Surface *output = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_BGRA32);
    if (!input || !output || !SDL_BlitSurfaceScaled(input, nullptr, output, nullptr, SDL_SCALEMODE_LINEAR)) {
        SDL_DestroySurface(input);
        SDL_DestroySurface(output);
        if (error)
            *error = "Cannot scale the snapshot image";
        return nullptr;
    }
    std::vector<unsigned char> bgra(static_cast<size_t>(w) * h * 4);
    for (int row = 0; row < h; ++row)
        std::memcpy(bgra.data() + static_cast<size_t>(row) * w * 4,
                    static_cast<unsigned char *>(output->pixels) + static_cast<size_t>(row) * output->pitch,
                    static_cast<size_t>(w) * 4);
    for (size_t i = 3; i < bgra.size(); i += 4)
        bgra[i] = 255;
    SDL_DestroySurface(input);
    SDL_DestroySurface(output);
    void *bitmap = [&] {
        VxImageDescEx image;
        image.Width = w;
        image.Height = h;
        image.BitsPerPixel = 32;
        image.BytesPerLine = w * 4;
        image.RedMask = 0xff0000;
        image.GreenMask = 0xff00;
        image.BlueMask = 0xff;
        image.AlphaMask = 0xff000000;
        image.Image = bgra.data();
        return VxCreateBitmap(image);
    }();
    if (!bitmap && error)
        *error = "Cannot create the snapshot bitmap";
    return bitmap;
}
EditResult LoadScriptSnapshot(Document &document, CK_ID script, const std::string &path) {
    auto *session = document.Session(script);
    if (!session || session->ReadOnly())
        return EditResult::Refused("The Script is read-only");
    std::string error;
    auto *bitmap = LoadSnapshotBitmap(Utf8ToWide(path), 48, 36, &error);
    if (!bitmap)
        return EditResult::Error(error);
    auto owner = document.KeepBitmap(bitmap);
    return SetScriptSnapshot(document, script, bitmap) ? EditResult::Applied() : EditResult::NoChange();
}

} // namespace vdev

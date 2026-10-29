#include "core/level/ObjectEditCommands.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "core/runtime/CkRuntimeHost.h"
#include "core/level/LevelCommands.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/level/ObjectProperties.h"

namespace vdev {

struct ObjectState {
    std::unique_ptr<CKStateChunk, decltype(&DeleteCKStateChunk)> chunk{nullptr, &DeleteCKStateChunk};
    UnkeptValues unkept;
    std::vector<std::pair<ObjectPropertyCheck, std::string>> properties;
    int slots = -1;
};

struct ObjectStateEntry {
    CKObjectHandle object{};
    ObjectState before, after;
    bool changed = false;
};

static int SlotCountOf(CKObject *object) {
    if (CKTexture *texture = CKTexture::Cast(object))
        return (texture->GetSlotCount() << 16) | (std::max(0, texture->GetCurrentSlot()) & 0xFFFF);
    if (CKSprite *sprite = CKSprite::Cast(object))
        return (sprite->GetSlotCount() << 16) | (std::max(0, sprite->GetCurrentSlot()) & 0xFFFF);
    return -1;
}

static bool RestoreSlotCount(CKObject *object, int captured) {
    if (captured < 0 || SlotCountOf(object) == captured) return true;
    const int count = captured >> 16, current = captured & 0xFFFF;
    if (CKTexture *texture = CKTexture::Cast(object)) {
        if (texture->GetSlotCount() != count) texture->SetSlotCount(count);
        if (count > 0) texture->SetCurrentSlot(std::min(current, count - 1));
    } else if (CKSprite *sprite = CKSprite::Cast(object)) {
        if (sprite->GetSlotCount() != count) sprite->SetSlotCount(count);
        if (count > 0) sprite->SetCurrentSlot(std::min(current, count - 1));
    }
    return SlotCountOf(object) == captured;
}

static void RefreshObject(CKObject *object) {
    if (CKCurvePoint *point = CKCurvePoint::Cast(object)) {
        point->NotifyUpdate();
        if (CKCurve *curve = point->GetCurve()) curve->Update();
    } else if (CKCurve *curve = CKCurve::Cast(object)) {
        curve->Update();
    }
}

static bool CaptureState(CKContext *context, CKObjectHandle handle, ObjectState &state,
                         const CkRuntimeHost *host) {
    bool captured = false;
    return GuardedCall([&] {
        CKObject *object = context ? context->GetObjectByHandle(handle) : nullptr;
        if (!object) return;
        state.chunk.reset(CKSaveObjectState(object));
        if (!state.chunk || context->GetObjectByHandle(handle) != object) return;
        state.slots = SlotCountOf(object);
        state.unkept = ReadUnkeptValues(object);
        for (const auto &property : ObjectPropertyChecks())
            if (CKIsChildClassOf(object, property.cid))
                state.properties.emplace_back(property, property.text(object));
        captured = context->GetObjectByHandle(handle) == object;
    }, host, "Capture object state") && captured;
}

static bool CompareStates(ObjectState &a, ObjectState &b, bool &same, const CkRuntimeHost *host) {
    same = false;
    bool compared = false;
    return GuardedCall([&] {
        if (!a.chunk || !b.chunk) return;
        const int size = a.chunk->GetDataSize(), otherSize = b.chunk->GetDataSize();
        if (size < 0 || otherSize < 0) return;
        a.chunk->StartRead();
        b.chunk->StartRead();
        const void *x = size ? a.chunk->LockReadBuffer() : nullptr;
        const void *y = otherSize ? b.chunk->LockReadBuffer() : nullptr;
        if ((size && !x) || (otherSize && !y)) return;
        same = size == otherSize && a.slots == b.slots && a.unkept.text == b.unkept.text &&
               (!size || std::memcmp(x, y, static_cast<size_t>(size)) == 0);
        compared = true;
    }, host, "Compare object states") && compared;
}

static bool ReadState(CKContext *context, ObjectStateEntry &entry, ObjectState &state, const CkRuntimeHost *host) {
    bool read = false;
    return GuardedCall([&] {
        CKObject *object = context ? context->GetObjectByHandle(entry.object) : nullptr;
        if (!object || !state.chunk) return;
        const CKERROR error = CKReadObjectState(object, state.chunk.get());
        if (context->GetObjectByHandle(entry.object) != object) return;
        for (const auto &restore : state.unkept.restore) {
            restore(object);
            if (context->GetObjectByHandle(entry.object) != object) return;
        }
        const bool slots = RestoreSlotCount(object, state.slots);
        read = error == CK_OK && slots && context->GetObjectByHandle(entry.object) == object;
    }, host, "Restore object state") && read;
}

// Curves depend on their points: refresh after every native state is back.
static bool RefreshAndCheck(CKContext *context, std::vector<ObjectStateEntry> &entries, bool before,
                            const CkRuntimeHost *host) {
    bool ok = true;
    for (ObjectStateEntry &entry : entries) {
        bool refreshed = false;
        const bool called = GuardedCall([&] {
            CKObject *object = context ? context->GetObjectByHandle(entry.object) : nullptr;
            if (!object) return;
            RefreshObject(object);
            refreshed = context->GetObjectByHandle(entry.object) == object;
        }, host, "Refresh object state");
        ok = called && refreshed && ok;
    }
    for (ObjectStateEntry &entry : entries) {
        bool checked = false;
        const bool called = GuardedCall([&] {
            CKObject *object = context ? context->GetObjectByHandle(entry.object) : nullptr;
            if (!object) return;
            const ObjectState &expected = before ? entry.before : entry.after;
            if (SlotCountOf(object) != expected.slots) return;
            for (const auto &property : expected.properties)
                if (!property.first.same(property.second, property.first.text(object))) return;
            checked = context->GetObjectByHandle(entry.object) == object;
        }, host, "Check restored object properties");
        ok = called && checked && ok;
    }
    return ok;
}

static bool RestoreStates(CKContext *context, std::vector<ObjectStateEntry> &entries, bool before,
                          const CkRuntimeHost *host) {
    if (!context || std::any_of(entries.begin(), entries.end(), [&](const ObjectStateEntry &entry) {
            return !context->GetObjectByHandle(entry.object);
        })) return false;
    bool ok = true;
    for (size_t i = entries.size(); i-- > 0;) {
        ObjectStateEntry &entry = entries[i];
        ok = ReadState(context, entry, before ? entry.before : entry.after, host) && ok;
    }
    return RefreshAndCheck(context, entries, before, host) && ok;
}

class ObjectStateAction : public Action {
public:
    explicit ObjectStateAction(std::vector<ObjectStateEntry> entries) : m_Entries(std::move(entries)) {}
    bool Redo(Document &document) override { return Read(document, false); }
    bool Undo(Document &document) override { return Read(document, true); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        for (const auto &entry : m_Entries)
            if (document.Context()->GetObjectByHandle(entry.object)) objects.insert(entry.object.Id);
    }

private:
    bool Read(Document &document, bool undo) {
        if (RestoreStates(document.Context(), m_Entries, undo, &document.Host())) return true;
        // A failing reader may already have written. This action owns
        // compensation for all objects in its part of the undo step.
        if (!RestoreStates(document.Context(), m_Entries, !undo, &document.Host()))
            document.Host().Log(LogLevel::Error, "Object state recovery was incomplete");
        return false;
    }
    std::vector<ObjectStateEntry> m_Entries;
};

struct ObjectsGesture::State {
    CKContext *context = nullptr;
    DocumentToken document{};
    std::vector<ObjectStateEntry> entries;

    bool BelongsTo(Document &owner) const {
        return context == owner.Context() && (!document.value || document == owner.Token());
    }
};

ObjectsGesture::ObjectsGesture() = default;
ObjectsGesture::~ObjectsGesture() = default;
bool ObjectsGesture::Active() const { return m_State != nullptr; }

bool ObjectsGesture::Begin(CKContext *context, const std::vector<CK_ID> &objects) {
    // View teardown discards materials after it stopped writing and the
    // document is unavailable.
    if (!context && objects.empty()) { m_State.reset(); return false; }
    if (Active() || !context) return false;
    auto state = std::make_unique<State>();
    state->context = context;
    for (CK_ID id : objects) {
        if (!id || std::any_of(state->entries.begin(), state->entries.end(),
                              [id](const ObjectStateEntry &entry) { return entry.object.Id == id; })) continue;
        ObjectStateEntry entry;
        entry.object = context->GetObjectHandle(id);
        if (!CaptureState(context, entry.object, entry.before, nullptr)) return false;
        state->entries.push_back(std::move(entry));
    }
    if (state->entries.empty()) return false;
    m_State = std::move(state);
    return true;
}

bool ObjectsGesture::Begin(Document &document, const std::vector<CK_ID> &objects) {
    if (!Begin(document.Context(), objects)) return false;
    m_State->document = document.Token();
    return true;
}

std::vector<CK_ID> ObjectsGesture::Objects() const {
    std::vector<CK_ID> objects;
    if (m_State) for (const auto &entry : m_State->entries) objects.push_back(entry.object.Id);
    return objects;
}

bool ObjectsGesture::Cancel(CKContext *context) {
    if (!m_State) return true;
    std::unique_ptr<State> state = std::move(m_State);
    return context && context == state->context && RestoreStates(context, state->entries, true, nullptr);
}

bool ObjectsGesture::Cancel(Document &document) {
    if (!m_State) return true;
    std::unique_ptr<State> state = std::move(m_State);
    if (!state->BelongsTo(document)) return false;
    const bool restored = RestoreStates(document.Context(), state->entries, true, &document.Host());
    if (!restored) document.InvalidateHistory();
    document.RefreshScripts();
    return restored;
}

EditResult ObjectsGesture::Commit(Document &document, const std::string &label, bool markChanged) {
    if (!m_State) return EditResult::Error(label + ": no object edit is open");
    if (!m_State->BelongsTo(document)) {
        m_State.reset();
        return EditResult::Error(label + ": the document has changed");
    }
    bool same = true;
    std::string error;
    for (ObjectStateEntry &entry : m_State->entries) {
        if (!CaptureState(document.Context(), entry.object, entry.after, &document.Host())) {
            error = "Cannot capture the object's state after editing";
            break;
        }
        bool unchanged = false;
        if (!CompareStates(entry.before, entry.after, unchanged, &document.Host())) {
            error = "Cannot compare the object's states";
            break;
        }
        same = same && unchanged;
        entry.changed = !unchanged;
    }
    if (!error.empty()) {
        const bool restored = Cancel(document);
        if (!restored) error += " (recovery was incomplete: the undo history is cleared)";
        document.Host().Log(LogLevel::Error, label + ": " + error);
        return EditResult::Error(label + ": " + error);
    }
    if (same) {
        m_State.reset();
        return EditResult::NoChange();
    }
    Edit edit(document, label, markChanged ? Edit::Marks::Composition : Edit::Marks::Scripts);
    auto &entries = m_State->entries;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                [](const ObjectStateEntry &entry) { return !entry.changed; }), entries.end());
    edit.Done(std::make_unique<ObjectStateAction>(std::move(m_State->entries)));
    m_State.reset(); // Edit now owns all recovery and history materials.
    return edit.Commit();
}

EditResult EditObjectState(Document &document, CK_ID id, const std::string &label,
                           const std::function<void(CKObject *)> &change) {
    return EditObjectsState(document, {id}, label, change);
}

EditResult EditObjectsState(Document &document, const std::vector<CK_ID> &ids,
                            const std::string &label, const std::function<void(CKObject *)> &change) {
    if (ids.empty()) return EditResult::NoChange();
    std::vector<CK_ID> live;
    for (CK_ID id : ids) if (document.IsLive(id)) live.push_back(id);
    if (live.empty()) return EditResult::Refused("The object is gone");
    ObjectsGesture gesture;
    if (!gesture.Begin(document, live)) return EditResult::Error("Cannot capture the object's state");
    for (CK_ID id : gesture.Objects()) {
        CKObject *object = document.Context()->GetObject(id);
        if (!object || !GuardedCall([&] { change(object); }, &document.Host(), label.c_str())) {
            const bool restored = gesture.Cancel(document);
            return EditResult::Error(label + (restored ? " failed" : " failed and recovery was incomplete: the undo history is cleared"));
        }
    }
    return gesture.Commit(document, label);
}

} // namespace vdev

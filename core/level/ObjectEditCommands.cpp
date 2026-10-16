#include "core/level/ObjectEditCommands.h"

#include <cstring>
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

#include "core/runtime/CkRuntimeHost.h"
#include "core/level/LevelCommands.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/level/ObjectProperties.h"

namespace vdev {

namespace {

// The image slots of the states CaptureObjectState took (see SlotCountOf),
// until they are taken or the state is deleted.
std::map<const CKStateChunk *, int> &CapturedSlotCounts() {
    static std::map<const CKStateChunk *, int> counts;
    return counts;
}

int TakeSlotCount(const CKStateChunk *state) {
    auto &counts = CapturedSlotCounts();
    auto found = counts.find(state);
    if (found == counts.end()) return -1;
    const int count = found->second;
    counts.erase(found);
    return count;
}

// The values the states CaptureObjectState took do not keep (ObjectProperties'
// inState), until the state is deleted: read back with it.
std::map<const CKStateChunk *, UnkeptValues> &CapturedUnkept() {
    static std::map<const CKStateChunk *, UnkeptValues> values;
    return values;
}

const UnkeptValues *UnkeptOf(const CKStateChunk *state) {
    auto found = CapturedUnkept().find(state);
    return found == CapturedUnkept().end() ? nullptr : &found->second;
}

struct ChunkDeleter {
    void operator()(CKStateChunk *chunk) const { ReleaseObjectState(chunk); }
};
using Chunk = std::unique_ptr<CKStateChunk, ChunkDeleter>;

bool SameState(CKStateChunk *a, CKStateChunk *b) {
    if (!a || !b) return false;
    const UnkeptValues *x0 = UnkeptOf(a), *y0 = UnkeptOf(b);
    if ((x0 ? x0->text : std::string()) != (y0 ? y0->text : std::string())) return false;
    const int size = a->GetDataSize();
    if (size != b->GetDataSize()) return false;
    a->StartRead();
    b->StartRead();
    const void *x = a->LockReadBuffer(), *y = b->LockReadBuffer();
    return x && y && std::memcmp(x, y, static_cast<size_t>(size)) == 0;
}

// Objects whose shape depends on others are brought up to date after a
// state is read back (a curve follows its control points).
void Refresh(CKObject *object) {
    if (CKCurvePoint *point = CKCurvePoint::Cast(object)) {
        point->NotifyUpdate();
        if (CKCurve *curve = point->GetCurve()) curve->Update();
    } else if (CKCurve *curve = CKCurve::Cast(object)) {
        curve->Update();
    }
}

// Read the native state and restore the values that its chunk does not keep.
CKERROR ReadStateBack(CKObject *object, CKStateChunk *state) {
    const CKERROR error = CKReadObjectState(object, state);
    if (const UnkeptValues *unkept = UnkeptOf(state))
        for (const auto &restore : unkept->restore) restore(object);
    return error;
}

// Image slots of textures and sprites: reading an older state back adds the
// slots it holds but does not remove later ones (Add Slot / Add Texture
// would not be undone) and sets the current slot before the slots exist, so
// the count and the current slot at capture time are kept with each state
// (count << 16 | current) and set again after reading it.
int SlotCountOf(CKObject *object) {
    if (CKTexture *texture = CKTexture::Cast(object))
        return (texture->GetSlotCount() << 16) | (std::max(0, texture->GetCurrentSlot()) & 0xFFFF);
    if (CKSprite *sprite = CKSprite::Cast(object))
        return (sprite->GetSlotCount() << 16) | (std::max(0, sprite->GetCurrentSlot()) & 0xFFFF);
    return -1;
}

void RestoreSlotCount(CKObject *object, int captured) {
    if (captured < 0 || SlotCountOf(object) == captured) return;
    const int count = captured >> 16, current = captured & 0xFFFF;
    if (CKTexture *texture = CKTexture::Cast(object)) {
        if (texture->GetSlotCount() != count) texture->SetSlotCount(count);
        if (count > 0) texture->SetCurrentSlot(std::min(current, count - 1));
    } else if (CKSprite *sprite = CKSprite::Cast(object)) {
        if (sprite->GetSlotCount() != count) sprite->SetSlotCount(count);
        if (count > 0) sprite->SetCurrentSlot(std::min(current, count - 1));
    }
}

// The states of one or more objects before and after an edit (an Edit's
// action, done already when it is recorded).
class ObjectStateAction : public Action {
public:
    struct Entry {
        CK_ID object = 0;
        Chunk before, after;
        int slotsBefore = -1, slotsAfter = -1; // image slots (textures, sprites)
    };
    explicit ObjectStateAction(std::vector<Entry> entries) : m_Entries(std::move(entries)) {}
    bool Redo(Document &document) override { return Read(document, false); }
    bool Undo(Document &document) override { return Read(document, true); }

private:
    bool Read(Document &document, bool undo) {
        bool ok = true;
        for (Entry &entry : m_Entries) {
            CKObject *object = document.Context()->GetObject(entry.object);
            CKStateChunk *state = undo ? entry.before.get() : entry.after.get();
            if (!object || !state) {
                ok = false;
                continue;
            }
            bool read = false;
            GuardedCall([&] { read = ReadStateBack(object, state) == CK_OK; }, &document.Host(), "CKReadObjectState");
            ok = ok && read;
            GuardedCall([&] { RestoreSlotCount(object, undo ? entry.slotsBefore : entry.slotsAfter); }, &document.Host(),
                        "Image slots");
        }
        for (Entry &entry : m_Entries)
            if (CKObject *object = document.Context()->GetObject(entry.object))
                GuardedCall([&] { Refresh(object); }, &document.Host(), "Refresh");
        return ok;
    }
    std::vector<Entry> m_Entries;
};

// One undo step of the object states changed (the 3D Layout's own cameras
// leave the composition unmodified).
bool Record(Document &document, const std::string &label, std::vector<ObjectStateAction::Entry> changed,
            Edit::Marks marks = Edit::Marks::Composition) {
    Edit edit(document, label, marks);
    edit.Done(std::make_unique<ObjectStateAction>(std::move(changed)));
    return edit.Commit().ok;
}

} // namespace

CKStateChunk *CaptureObjectState(CKContext *context, CK_ID id) {
    CKObject *object = context ? context->GetObject(id) : nullptr;
    CKStateChunk *state = object ? CKSaveObjectState(object) : nullptr;
    if (!state) return nullptr;
    // A state at the address of one deleted without ReleaseObjectState does
    // not take its count.
    if (SlotCountOf(object) >= 0) CapturedSlotCounts()[state] = SlotCountOf(object);
    else CapturedSlotCounts().erase(state);
    UnkeptValues unkept = ReadUnkeptValues(object);
    if (!unkept.restore.empty()) CapturedUnkept()[state] = std::move(unkept);
    else CapturedUnkept().erase(state);
    return state;
}

void ReleaseObjectState(CKStateChunk *state) {
    if (!state) return;
    TakeSlotCount(state);
    CapturedUnkept().erase(state);
    DeleteCKStateChunk(state);
}

bool CommitObjectEdit(Document &document, CK_ID id, CKStateChunk *before,
                      const std::string &label) {
    Chunk previous(before);
    Chunk after(CaptureObjectState(document.Context(), id));
    const int slotsBefore = TakeSlotCount(previous.get()), slotsAfter = TakeSlotCount(after.get());
    if (!previous || !after || SameState(previous.get(), after.get())) return false;
    std::vector<ObjectStateAction::Entry> entries(1);
    entries[0].object = id;
    entries[0].slotsBefore = slotsBefore;
    entries[0].slotsAfter = slotsAfter;
    entries[0].before = std::move(previous);
    entries[0].after = std::move(after);
    return Record(document, label, std::move(entries));
}

EditResult EditObjectState(Document &document, CK_ID id, const std::string &label,
                           const std::function<void(CKObject *)> &change) {
    CKObject *object = document.Context()->GetObject(id);
    if (!object) {
        return EditResult::Refused("The object is gone");
    }
    CKStateChunk *before = CaptureObjectState(document.Context(), id);
    if (!before) return EditResult::Error("Cannot capture the object's state");
    if (!GuardedCall([&] { change(object); }, &document.Host(), label.c_str())) {
        if (before) {
            ReadStateBack(object, before);
            ReleaseObjectState(before);
        }
        return EditResult::Error(label + " raised an exception");
    }
    if (!CommitObjectEdit(document, id, before, label)) return EditResult::NoChange();
    return EditResult::Applied();
}

EditResult EditObjectsState(Document &document, const std::vector<CK_ID> &ids,
                            const std::string &label, const std::function<void(CKObject *)> &change) {
    std::vector<ObjectStateAction::Entry> entries;
    for (CK_ID id : ids) {
        if (!document.Context()->GetObject(id)) continue;
        ObjectStateAction::Entry entry;
        entry.object = id;
        entry.before.reset(CaptureObjectState(document.Context(), id));
        entry.slotsBefore = TakeSlotCount(entry.before.get());
        if (entry.before) entries.push_back(std::move(entry));
    }
    if (entries.empty()) {
        return EditResult::Refused("The objects are gone");
    }
    bool raised = false;
    for (ObjectStateAction::Entry &entry : entries)
        if (CKObject *object = document.Context()->GetObject(entry.object))
            if (!GuardedCall([&] { change(object); }, &document.Host(), label.c_str())) raised = true;
    if (raised) {
        for (ObjectStateAction::Entry &entry : entries)
            if (CKObject *object = document.Context()->GetObject(entry.object)) ReadStateBack(object, entry.before.get());
        return EditResult::Error(label + " raised an exception");
    }
    std::vector<ObjectStateAction::Entry> changed;
    for (ObjectStateAction::Entry &entry : entries) {
        entry.after.reset(CaptureObjectState(document.Context(), entry.object));
        entry.slotsAfter = TakeSlotCount(entry.after.get());
        if (entry.after && !SameState(entry.before.get(), entry.after.get())) changed.push_back(std::move(entry));
    }
    if (changed.empty()) return EditResult::NoChange();
    return Record(document, label, std::move(changed)) ? EditResult::Applied()
                                                       : EditResult::Error(label + ": the edit has ended");
}

ObjectsGesture::~ObjectsGesture() {
    Clear();
}

void ObjectsGesture::Clear() {
    for (auto &entry : m_Before) ReleaseObjectState(entry.second);
    m_Before.clear();
}

void ObjectsGesture::Begin(CKContext *context, const std::vector<CK_ID> &objects) {
    Clear();
    for (CK_ID id : objects) {
        if (!id || std::any_of(m_Before.begin(), m_Before.end(), [&](const auto &entry) { return entry.first == id; }))
            continue;
        if (CKStateChunk *state = CaptureObjectState(context, id)) m_Before.emplace_back(id, state);
    }
}

std::vector<CK_ID> ObjectsGesture::Objects() const {
    std::vector<CK_ID> objects;
    for (const auto &entry : m_Before) objects.push_back(entry.first);
    return objects;
}

void ObjectsGesture::Cancel(CKContext *context) {
    for (auto &entry : m_Before)
        if (CKObject *object = context ? context->GetObject(entry.first) : nullptr) {
            ReadStateBack(object, entry.second);
            Refresh(object);
        }
    Clear();
}

bool ObjectsGesture::Commit(Document &document, const std::string &label, bool markChanged) {
    std::vector<ObjectStateAction::Entry> changed;
    for (auto &entry : m_Before) {
        ObjectStateAction::Entry item;
        item.object = entry.first;
        item.before.reset(entry.second);
        item.slotsBefore = TakeSlotCount(entry.second);
        entry.second = nullptr;
        item.after.reset(CaptureObjectState(document.Context(), entry.first));
        item.slotsAfter = TakeSlotCount(item.after.get());
        if (item.after && !SameState(item.before.get(), item.after.get())) changed.push_back(std::move(item));
    }
    m_Before.clear();
    if (changed.empty()) return false;
    return Record(document, label, std::move(changed), markChanged ? Edit::Marks::Composition : Edit::Marks::Scripts);
}

} // namespace vdev

#pragma once

// The Level commands' undo step (LevelCommands.cpp, CurveCommands.cpp,
// CreateCommands.cpp): an action and its inverse over the document, as one
// Edit.

#include <memory>
#include <set>
#include <string>

#include "CKAll.h"

#include "core/runtime/CkRuntimeHost.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/document/EditResult.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/UndoHistory.h"

namespace vdev {
namespace level {

using DocumentFn = std::function<bool(Document &)>;

// Applies redo now as one undo step. created: the objects it made, which
// join the composition with the objects they own (in the graveyard while the
// step is undone); buried: the objects it removed, the other way round (the
// actions themselves leave the graveyard alone). A redo that fails may have
// done part of its work: undoAction takes it back (when that fails too, the
// history is cleared and the objects made are kept, Edit).
inline EditResult Apply(Document &document, const std::string &label, const DocumentFn &redo,
                        const DocumentFn &undoAction, const std::set<CK_ID> &created = {}, CK_ID result = 0,
                        const std::set<CK_ID> &buried = {}) {
    Edit edit(document, label);
    const auto inverse = [&document, undoAction] { return undoAction(document); };
    if (!edit.Do([&document, redo] { return redo(document); }, inverse)) {
        for (CK_ID id : created) edit.Created(id);
        edit.Done([] { return true; }, inverse);
        return edit.Fail(label + " failed");
    }
    std::set<CK_ID> kept = created;
    for (CK_ID id : CollectOwnedObjects(document.Context(), created)) kept.insert(id);
    for (CK_ID id : kept) edit.Created(id);
    for (CK_ID id : buried) edit.Bury(id);
    return edit.Commit(std::string(), result);
}

inline std::vector<ObjectRef> RecordObjects(const Document &document, const std::vector<CK_ID> &ids) {
    return document.References(ids);
}
inline void KeepObjects(const Document &document, std::set<CK_ID> &objects, ObjectRef ref) {
    if (const auto id = document.NativeId(ref, Document::ObjectScope::Existing))
        objects.insert(id);
}
inline void KeepObjects(const Document &document, std::set<CK_ID> &objects, const std::vector<ObjectRef> &refs) {
    for (auto ref : refs)
        KeepObjects(document, objects, ref);
}
inline bool ResolveObjects(Document &document, const std::vector<ObjectRef> &refs, std::vector<CK_ID> &ids) {
    ids.clear();
    for (auto ref : refs) {
        const auto id = document.NativeId(ref, Document::ObjectScope::Existing);
        if (ref != ObjectRef{} && !id)
            return false;
        ids.push_back(id);
    }
    return true;
}

inline EditResult Apply(Document &document, const std::string &label, std::unique_ptr<Action> action,
                        const std::set<CK_ID> &created = {}, CK_ID result = 0, const std::set<CK_ID> &buried = {}) {
    Edit edit(document, label);
    if (!edit.Do(std::move(action))) {
        for (auto id : created)
            edit.Created(id);
        edit.Done(std::move(action));
        return edit.Fail(label + " failed");
    }
    std::set<CK_ID> kept = created;
    for (auto id : CollectOwnedObjects(document.Context(), created))
        kept.insert(id);
    for (auto id : kept)
        edit.Created(id);
    for (auto id : buried)
        edit.Bury(id);
    return edit.Commit({}, result);
}

template <typename T> T *Get(Document &document, CK_ID id) {
    return T::Cast(document.Context()->GetObject(id));
}
template <typename T> T *Get(Document &document, ObjectRef ref) {
    return T::Cast(document.Resolve(ref, CKCID_OBJECT, Document::ObjectScope::Existing));
}

enum class LevelCollection { Objects, Places };
class LevelObjectAction final : public Action {
public:
    LevelObjectAction(Document &document, CK_ID level, CK_ID object,
                      LevelCollection collection = LevelCollection::Objects, bool listed = true)
        : m_Level(document.Reference(level)), m_Object(document.Reference(object)), m_Collection(collection),
          m_Listed(listed) {}
    bool Redo(Document &document) override { return Apply(document, true); }
    bool Undo(Document &document) override { return Apply(document, false); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Level);
        KeepObjects(document, objects, m_Object);
    }

private:
    bool Apply(Document &document, bool add) {
        auto *level = Get<CKLevel>(document, m_Level);
        auto *object = document.Resolve(m_Object, CKCID_OBJECT, Document::ObjectScope::Existing);
        if (!level || !object)
            return false;
        if (!m_Listed)
            return true;
        if (m_Collection == LevelCollection::Places) {
            auto *place = CKPlace::Cast(object);
            return place && (add ? level->AddPlace(place) : level->RemovePlace(place)) == CK_OK;
        }
        return (add ? level->AddObject(object) : level->RemoveObject(object)) == CK_OK;
    }
    ObjectRef m_Level{}, m_Object{};
    LevelCollection m_Collection;
    bool m_Listed;
};

} // namespace level
} // namespace vdev

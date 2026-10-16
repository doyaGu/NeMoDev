#pragma once

// The Level commands' undo step (LevelCommands.cpp, CurveCommands.cpp,
// CreateCommands.cpp): an action and its inverse over the document, as one
// Edit.

#include <functional>
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

template <typename T> T *Get(Document &document, CK_ID id) {
    return T::Cast(document.Context()->GetObject(id));
}

} // namespace level
} // namespace vdev

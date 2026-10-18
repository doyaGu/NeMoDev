#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CKAll.h"
#include "core/document/EditResult.h"

namespace vdev {

class Document;

// Undoable edits of an object's own state (Setup panels): the object's state
// chunk before and after the edit (CKSaveObjectState / CKReadObjectState,
// the mechanism of initial conditions) is the undo payload, so any setter
// of any class is one undo step without a command per property.

// Applies change to the object as one undo step.
EditResult EditObjectState(Document &document, CK_ID object, const std::string &label,
                           const std::function<void(CKObject *)> &change);

// Applies change to each object (one undo step for all of them); objects
// left unchanged are not recorded.
EditResult EditObjectsState(Document &document, const std::vector<CK_ID> &objects,
                            const std::string &label, const std::function<void(CKObject *)> &change);

// A gesture editing objects live (the 3D Layout's tools drag them): their
// states when it begins, read back by Cancel or recorded by Commit as one
// undo step. markChanged false: the step does not mark the composition
// modified (the 3D Layout's own cameras).
class ObjectsGesture {
public:
    ObjectsGesture();
    ~ObjectsGesture();
    ObjectsGesture(const ObjectsGesture &) = delete;
    ObjectsGesture &operator=(const ObjectsGesture &) = delete;

    // Capture every object before allowing writes. Finish with Commit or
    // Cancel before closing the view or document.
    bool Begin(CKContext *context, const std::vector<CK_ID> &objects);
    bool Begin(Document &document, const std::vector<CK_ID> &objects);
    bool Active() const;
    std::vector<CK_ID> Objects() const;
    bool Cancel(CKContext *context);
    bool Cancel(Document &document);
    // NoChange requires complete capture and comparison. On failure restore
    // the original values; failed recovery invalidates the document history.
    EditResult Commit(Document &document, const std::string &label, bool markChanged = true);

private:
    struct State;
    std::unique_ptr<State> m_State;
};

} // namespace vdev

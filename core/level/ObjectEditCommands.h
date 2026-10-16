#pragma once

#include <functional>
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

// The object's current state; null when it is gone. The caller owns it
// (ReleaseObjectState) unless it is handed to CommitObjectEdit.
CKStateChunk *CaptureObjectState(CKContext *context, CK_ID object);
// Deletes a state CaptureObjectState took (and what it kept with it).
void ReleaseObjectState(CKStateChunk *state);

// Records the change since `before` (taken by CaptureObjectState) as one
// undo step; takes ownership of before. False (and nothing recorded) when
// the object is gone or nothing changed.
bool CommitObjectEdit(Document &document, CK_ID object, CKStateChunk *before,
                      const std::string &label);

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
    ObjectsGesture() = default;
    ~ObjectsGesture();
    ObjectsGesture(const ObjectsGesture &) = delete;
    ObjectsGesture &operator=(const ObjectsGesture &) = delete;

    void Begin(CKContext *context, const std::vector<CK_ID> &objects);
    bool Active() const { return !m_Before.empty(); }
    std::vector<CK_ID> Objects() const;
    void Cancel(CKContext *context);
    // False when nothing changed (then nothing is recorded).
    bool Commit(Document &document, const std::string &label, bool markChanged = true);

private:
    void Clear();
    std::vector<std::pair<CK_ID, CKStateChunk *>> m_Before;
};

} // namespace vdev

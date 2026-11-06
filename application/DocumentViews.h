#pragma once

#include "editor/Identity.h"

namespace vdev {
// The editing session owns replacement; the workspace retains its instances
// until replacement succeeds. These calls run between native tasks on the UI
// thread. Failed detachment must remain restorable in the original document.
class DocumentViews {
  public:
    virtual ~DocumentViews() = default;
    virtual void DetachDocument(bool replaceContext) = 0;
    virtual void RestoreDocument() = 0;
    virtual void DocumentReplaced(DocumentToken document, uint64_t revision, bool open) = 0;
    virtual bool DocumentInteractionActive() const = 0;
};
} // namespace vdev

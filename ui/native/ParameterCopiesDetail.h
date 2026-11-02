#pragma once
// Included only by the native parameter adapters.
#include "ui/controls/ParameterCopies.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/document/Document.h"
#include "workspace/EditorWorkspace.h"
#include <set>

namespace vdev {
struct ParameterCopies::Data {
    CkRuntimeHost &host;
    EditorWorkspace &workspace;
    std::function<Document *()> document;
    std::function<ParameterDependencies()> copyOptions;
    std::function<void()> clearSelection;
    std::set<ObjectRef> owned;
    CKParameter *Parameter(ObjectRef reference) const {
        auto *doc = document();
        return doc ? CKParameter::Cast(doc->Resolve(reference, CKCID_PARAMETER, Document::ObjectScope::Existing)) : nullptr;
    }
};
} // namespace vdev

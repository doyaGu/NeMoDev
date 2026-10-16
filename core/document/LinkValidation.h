#pragma once

#include <string>
#include <vector>

#include "CKAll.h"

namespace vdev {

class Document;

// A stored link Dev treats as invalid after loading (Dev.exe
// CUIKLink_ResolveAndValidateEndpoints, from ValidateAndFinalizeLinks and
// CUIKBehaviorWindow_LoadInterfaceData): an endpoint that designates no CK
// object, a behavior link without its CKBehaviorLink between the two IOs,
// and with Test Parameter Link after Loading a parameter link whose types
// are not compatible (CKParameterManager::IsTypeCompatible; a link through
// an undisplayed conversion operation is not tested).
struct InvalidLink {
    CK_ID script = 0;
    CK_ID graph = 0; // the record storing the link
    int slot = -1;   // its index in the record's links
    CK_ID link = 0;  // the stored link id
    bool behaviorLink = false;
    std::string reason;
};

// Every Script of the composition (sessions are opened for the ones with a chunk).
std::vector<InvalidLink> FindInvalidLinks(Document &document, bool testParameterTypes);

// Remove Invalid Link after Loading: the stored records go (not CK objects),
// as part of loading (no undo step); each Script changed is saved again.
// Returns how many were removed.
int RemoveInvalidLinks(Document &document, const std::vector<InvalidLink> &links);

} // namespace vdev

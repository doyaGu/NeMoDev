#pragma once

#include "CKAll.h"

namespace vdev {

// Editor insertion operations use CK2's array ownership and indexed methods.
// No internal arrays or native object layouts are exposed to callers.
bool InsertBehaviorIo(CKBehavior *behavior, int slot, CKBehaviorIO *io, bool input);
bool InsertInputParameter(CKBehavior *behavior, int slot, CKParameterIn *in);
bool InsertOutputParameter(CKBehavior *behavior, int slot, CKParameterOut *out);
bool InsertLocalParameter(CKBehavior *behavior, int slot, CKParameterLocal *local);

// Ballanced keeps equal-priority behavior and Script ordering stable.

// A sub-behavior added to a graph, among its equal priorities at position
// of the order before (the end when position is out of range).
bool AddSubBehaviorAt(CKBehavior *graph, CKBehavior *child, int position);
// A script added to an object, after its equal priorities.
CKERROR AddScriptLast(CKBeObject *owner, CKBehavior *script);
// CKBehavior::SetPriority, the equal priorities of its parent graph (of its
// owner's scripts, for a script) in their order before.
void SetPriorityKeepingTies(CKBehavior *behavior, int priority);

} // namespace vdev

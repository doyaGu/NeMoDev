#include "core/runtime/CkArrays.h"

namespace vdev {
bool InsertBehaviorIo(CKBehavior *behavior, int slot, CKBehaviorIO *io, bool input) {
    return behavior && (input ? behavior->InsertInput(slot, io) : behavior->InsertOutput(slot, io)) == CK_OK;
}

bool InsertInputParameter(CKBehavior *behavior, int slot, CKParameterIn *parameter) {
    if (!behavior || !parameter || slot < 0 || slot > behavior->GetInputParameterCount()) return false;
    const int from = behavior->GetInputParameterCount();
    behavior->AddInputParameter(parameter);
    return behavior->GetInputParameterCount() == from + 1 && behavior->MoveInputParameter(from, slot) == CK_OK;
}

bool InsertOutputParameter(CKBehavior *behavior, int slot, CKParameterOut *parameter) {
    if (!behavior || !parameter || slot < 0 || slot > behavior->GetOutputParameterCount()) return false;
    const int from = behavior->GetOutputParameterCount();
    behavior->AddOutputParameter(parameter);
    return behavior->GetOutputParameterCount() == from + 1 && behavior->MoveOutputParameter(from, slot) == CK_OK;
}

bool InsertLocalParameter(CKBehavior *behavior, int slot, CKParameterLocal *parameter) {
    if (!behavior || !parameter || slot < 0 || slot > behavior->GetLocalParameterCount()) return false;
    const int from = behavior->GetLocalParameterCount();
    behavior->AddLocalParameter(parameter);
    return behavior->GetLocalParameterCount() == from + 1 && behavior->MoveLocalParameter(from, slot) == CK_OK;
}

bool AddSubBehaviorAt(CKBehavior *graph, CKBehavior *child, int position) {
    return graph && graph->AddSubBehaviorAt(child, position) == CK_OK;
}
CKERROR AddScriptLast(CKBeObject *owner, CKBehavior *script) {
    return owner && script ? owner->AddScript(script) : CKERR_INVALIDPARAMETER;
}
void SetPriorityKeepingTies(CKBehavior *behavior, int priority) {
    if (behavior) behavior->SetPriority(priority);
}
} // namespace vdev

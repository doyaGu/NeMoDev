#include "core/runtime/CkArrays.h"

#include <cstring>
#include <vector>


namespace vdev {

namespace {

// The arrays CK keeps protected.
struct BehaviorArrays : CKBehavior {
    using CKBehavior::m_InputArray;
    using CKBehavior::m_OutputArray;
    using CKBehavior::m_InParameter;
    using CKBehavior::m_OutParameter;
    using CKBehavior::m_LocalParameter;
    using CKBehavior::m_GraphData;
};

struct BeObjectScripts : CKBeObject {
    using CKBeObject::m_ScriptArray;
};

// The last entry of array moved to slot (the ones from slot on move up).
void MoveLastTo(XSObjectPointerArray &array, int slot) {
    const int size = array.Size();
    if (size <= 0 || slot < 0 || slot >= size - 1) return;
    CKObject **begin = array.Begin();
    CKObject *last = begin[size - 1];
    std::memmove(begin + slot + 1, begin + slot, static_cast<size_t>(size - 1 - slot) * sizeof(CKObject *));
    begin[slot] = last;
}

// A parameter added by CK2 (which sets its owner), then moved to slot.
template <class Parameter>
bool InsertParameter(CKBehavior *behavior, int slot, Parameter *parameter, int count,
                     void (CKBehavior::*add)(Parameter *), XSObjectPointerArray CKBehavior::*array) {
    if (!behavior || !parameter || slot < 0 || slot > count) return false;
    XSObjectPointerArray &entries = behavior->*array;
    CKBehavior *owner = CKBehavior::Cast(parameter->GetOwner());
    if (parameter->GetCKContext() != behavior->GetCKContext() || entries.GetPosition(parameter) >= 0 ||
        (owner && owner != behavior && (owner->*array).GetPosition(parameter) >= 0))
        return false;
    (behavior->*add)(parameter);
    if (entries.Size() != count + 1 || entries[count] != parameter) return false;
    MoveLastTo(behavior->*array, slot);
    return true;
}

// A graph's sub-behaviors or an object's scripts, where CK keeps them.
struct PriorityList {
    CKObject **entries = nullptr;
    int count = 0;
};

PriorityList SubBehaviorList(CKBehavior *graph) {
    BehaviorGraphData *data = graph ? graph->*&BehaviorArrays::m_GraphData : nullptr;
    if (!data) return {};
    return {data->m_SubBehaviors.Begin(), graph->GetSubBehaviorCount()};
}

PriorityList ScriptList(CKBeObject *owner) {
    XObjectPointerArray *scripts = owner ? owner->*&BeObjectScripts::m_ScriptArray : nullptr;
    if (!scripts) return {};
    return {scripts->Begin(), owner->GetScriptCount()};
}

std::vector<CKObject *> Snapshot(const PriorityList &list) {
    return list.entries ? std::vector<CKObject *>(list.entries, list.entries + list.count) : std::vector<CKObject *>();
}

// Each place of CK's order takes the first of ties not placed yet with the
// priority CK put there.
bool OrderTies(const PriorityList &list, const std::vector<CKObject *> &ties) {
    if (!list.entries || list.count != static_cast<int>(ties.size())) return false;
    for (int start = 0; start < list.count;) {
        const int priority = static_cast<CKBehavior *>(list.entries[start])->GetPriority();
        int end = start + 1;
        while (end < list.count && static_cast<CKBehavior *>(list.entries[end])->GetPriority() == priority) ++end;
        int next = start;
        for (CKObject *entry : ties)
            if (static_cast<CKBehavior *>(entry)->GetPriority() == priority) {
                if (next == end) return false;
                list.entries[next++] = entry;
            }
        if (next != end) return false;
        start = end;
    }
    return true;
}

} // namespace

bool InsertBehaviorIo(CKBehavior *behavior, int slot, CKBehaviorIO *io, bool input) {
    if (!behavior || !io || io->GetType() != (input ? CK_BEHAVIORIO_IN : CK_BEHAVIORIO_OUT)) return false;
    XSObjectPointerArray CKBehavior::*member = input ? &BehaviorArrays::m_InputArray : &BehaviorArrays::m_OutputArray;
    XSObjectPointerArray &array = behavior->*member;
    if (slot < 0 || slot > array.Size()) return false;
    CKBehavior *owner = io->GetOwner();
    if (io->GetCKContext() != behavior->GetCKContext() || array.GetPosition(io) >= 0 ||
        (owner && owner != behavior && (owner->*member).GetPosition(io) >= 0))
        return false;
    array.Insert(slot, io);
    io->SetOwner(behavior);
    return true;
}

bool InsertInputParameter(CKBehavior *behavior, int slot, CKParameterIn *in) {
    return InsertParameter(behavior, slot, in, behavior ? behavior->GetInputParameterCount() : 0,
                           &CKBehavior::AddInputParameter, &BehaviorArrays::m_InParameter);
}

bool InsertOutputParameter(CKBehavior *behavior, int slot, CKParameterOut *out) {
    return InsertParameter(behavior, slot, out, behavior ? behavior->GetOutputParameterCount() : 0,
                           &CKBehavior::AddOutputParameter, &BehaviorArrays::m_OutParameter);
}

bool InsertLocalParameter(CKBehavior *behavior, int slot, CKParameterLocal *local) {
    return InsertParameter(behavior, slot, local, behavior ? behavior->GetLocalParameterCount() : 0,
                           &CKBehavior::AddLocalParameter, &BehaviorArrays::m_LocalParameter);
}

bool AddSubBehaviorAt(CKBehavior *graph, CKBehavior *child, int position) {
    if (!graph || !child || graph->GetCKContext() != child->GetCKContext()) return false;
    for (int i = 0; i < graph->GetSubBehaviorCount(); ++i)
        if (graph->GetSubBehavior(i) == child) return false;
    std::vector<CKObject *> ties = Snapshot(SubBehaviorList(graph));
    if (position < 0 || position > static_cast<int>(ties.size())) position = static_cast<int>(ties.size());
    ties.insert(ties.begin() + position, child);
    if (graph->AddSubBehavior(child) != CK_OK) return false;
    return OrderTies(SubBehaviorList(graph), ties);
}

CKERROR AddScriptLast(CKBeObject *owner, CKBehavior *script) {
    if (!owner || !script) return CKERR_INVALIDPARAMETER;
    for (int i = 0; i < owner->GetScriptCount(); ++i)
        if (owner->GetScript(i) == script) return CK_OK;
    std::vector<CKObject *> ties = Snapshot(ScriptList(owner));
    ties.push_back(script);
    const CKERROR added = owner->AddScript(script);
    if (added != CK_OK) return added;
    if (!OrderTies(ScriptList(owner), ties)) return CKERR_INVALIDOPERATION;
    return CK_OK;
}

void SetPriorityKeepingTies(CKBehavior *behavior, int priority) {
    if (!behavior) return;
    CKBehavior *parent = behavior->GetParent();
    CKBeObject *owner = !parent && (behavior->GetFlags() & CKBEHAVIOR_SCRIPT) ? behavior->GetOwner() : nullptr;
    auto list = [&] { return parent ? SubBehaviorList(parent) : ScriptList(owner); };
    const std::vector<CKObject *> ties = Snapshot(list());
    behavior->SetPriority(priority);
    OrderTies(list(), ties);
}

} // namespace vdev

#include "core/document/ScriptSession.h"

#include "core/runtime/CkRuntimeHost.h"
#include "core/schematic/InterfaceDataReconstruction.h"

namespace vdev {

namespace {

void RefreshRecord(CKContext *context, BehaviorData &record) {
    CKBehavior *behavior = CKBehavior::Cast(context->GetObject(record.id));
    record.UpdateRuntimeState(behavior);
    CKBehavior *parent = behavior ? behavior->GetParent() : nullptr;
    record.parentId = parent ? parent->GetID() : 0;
}

} // namespace

bool ScriptSession::Open(CKContext *context, CK_ID scriptId) {
    m_ScriptId = scriptId;
    m_LoadMessage.clear();
    m_Data.Clear();
    CKBehavior *script = CKBehavior::Cast(context->GetObject(scriptId));
    if (!script) {
        m_Origin = SessionOrigin::LoadFailed;
        m_LoadMessage = "Script object not found";
        return false;
    }

    // No chunk, or one cleared (no data, as tools that strip Scripts leave
    // it): the layout is rebuilt from the runtime graph.
    CKStateChunk *chunk = script->GetInterfaceChunk();
    std::string unused;
    if (chunk && chunk->GetDataSize() <= 0) unused = "InterfaceChunk cleared";
    if (chunk && unused.empty()) {
        CKERROR result = CKERR_INVALIDPARAMETER;
        const bool completed = GuardedCall([&] { result = m_Data.LoadFromChunk(script, chunk); }, nullptr,
                                           "InterfaceData::LoadFromChunk");
        if (completed && result == CK_OK) {
            m_Origin = SessionOrigin::LoadedChunk;
            RefreshRuntimeState(context);
            m_SavedRevision = m_Revision;
            return true;
        }
        // Unreadable (damaged, partly cleared, a layout this library does
        // not parse): rebuilt as well rather than left read-only. The chunk
        // stays on the Script until the rebuilt layout is edited and saved.
        unused = "InterfaceChunk unreadable (" + std::to_string(result) + ") at phase '" + m_Data.GetLastLoadPhase() +
                 "', dword " + std::to_string(m_Data.GetLastLoadDwordOffset());
        m_Data.observers.clear();
        m_Data.Clear();
    }
    const std::uint64_t outside = m_ChangesOutsideUndo;
    const bool rebuilt = Reconstruct(context);
    // Not a change: an unedited Script keeps its chunk (or its absence); a
    // Script made after saving and undone leaves the composition unmodified.
    m_SavedRevision = m_Revision;
    m_ChangesOutsideUndo = outside;
    if (rebuilt && !unused.empty()) m_LoadMessage = unused + "; layout rebuilt from the runtime graph";
    return rebuilt;
}

bool ScriptSession::Reconstruct(CKContext *context) {
    CKBehavior *script = CKBehavior::Cast(context->GetObject(m_ScriptId));
    InterfaceDataReconstructionOptions options;
    options.linkRouting = InterfaceDataLinkRouting::Straight;
    if (!GuardedCall([&] { ReconstructInterfaceData(m_Data, script, options); }, nullptr, "ReconstructInterfaceData")) {
        m_Data.Clear();
        m_Origin = SessionOrigin::LoadFailed;
        m_LoadMessage = "Layout reconstruction failed";
        return false;
    }
    m_Origin = SessionOrigin::Reconstructed;
    m_LoadMessage.clear();
    RefreshRuntimeState(context);
    Touch();
    return true;
}

void ScriptSession::Assign(const InterfaceData &data) {
    m_Data = data;
    m_Data.observers.clear();
}

void ScriptSession::RefreshRuntimeState(CKContext *context) {
    RefreshRecord(context, m_Data.rootBehavior);
    for (auto &record : m_Data.behaviors) RefreshRecord(context, record);
}

} // namespace vdev

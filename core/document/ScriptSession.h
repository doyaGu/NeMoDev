#pragma once

#include <cstdint>
#include <string>

#include "core/interface/InterfaceData.h"

namespace vdev {

enum class SessionOrigin {
    LoadedChunk,   // the Script's saved InterfaceChunk was parsed
    Reconstructed, // layout rebuilt from the CK runtime graph: no chunk, or
                   // one cleared or unreadable (LoadMessage says why)
    LoadFailed,    // no Script, or the rebuild itself failed; read-only
};

// Display data of one Script. Untouched sessions are never serialized, so
// the original InterfaceChunk bytes stay attached to the CKBehavior (also
// a cleared or unreadable one, until the rebuilt layout is edited).
class ScriptSession {
public:
    // False when the session is read-only (LoadFailed). A Script whose
    // InterfaceChunk is missing, cleared (no data) or cannot be parsed gets
    // its layout rebuilt from the runtime graph, editable as any other.
    bool Open(CKContext *context, CK_ID scriptId);
    // Replaces the layout with a runtime reconstruction. False (and the
    // session read-only) when the reconstruction raised.
    bool Reconstruct(CKContext *context);

    CK_ID ScriptId() const { return m_ScriptId; }
    SessionOrigin Origin() const { return m_Origin; }
    // Why the saved layout was not used (cleared, unreadable) or why the
    // session is read-only; empty otherwise.
    const std::string &LoadMessage() const { return m_LoadMessage; }
    bool ReadOnly() const { return m_Origin == SessionOrigin::LoadFailed; }

    InterfaceData &Data() { return m_Data; }
    const InterfaceData &Data() const { return m_Data; }

    // Replaces the display data, e.g. from an undo snapshot. Observers are
    // never copied: snapshots are plain values.
    void Assign(const InterfaceData &data);
    // Runtime-only fields (parent ids, port counts, Target state) are not in
    // the chunk; refresh them after loading or after a CK topology change.
    void RefreshRuntimeState(CKContext *context);

    // Open/closed in the Schematic: view state only. Dev 2.5 shows loaded
    // Scripts open even when the root record carries UI flag 0x200 (2D
    // Text.cmo), so the stored flag is kept unchanged and not interpreted.
    bool IsOpen() const { return m_Open; }
    void SetOpen(bool open) {
        m_Open = open;
        ++m_ViewRevision;
    }
    std::uint64_t ViewRevision() const { return m_ViewRevision; }

    std::uint64_t Revision() const { return m_Revision; }
    // A change outside undo (display reconstructed, invalid links removed,
    // automatic resizing): the Script stays modified until it is saved.
    void Touch() {
        ++m_Revision;
        ++m_ChangesOutsideUndo;
    }
    // A change an undo step makes, or a live edit that ends as one (or is
    // put back): undoing back to the save point leaves it unmodified.
    void TouchUndoable() { ++m_Revision; }
    std::uint64_t ChangesOutsideUndo() const { return m_ChangesOutsideUndo; }
    bool Dirty() const { return m_Revision != m_SavedRevision; }
    void MarkSaved() { m_SavedRevision = m_Revision; }

private:
    CK_ID m_ScriptId = 0;
    SessionOrigin m_Origin = SessionOrigin::LoadedChunk;
    std::string m_LoadMessage;
    InterfaceData m_Data;
    std::uint64_t m_Revision = 0;
    std::uint64_t m_SavedRevision = 0;
    std::uint64_t m_ChangesOutsideUndo = 0;
    bool m_Open = true;
    std::uint64_t m_ViewRevision = 0;
};

} // namespace vdev

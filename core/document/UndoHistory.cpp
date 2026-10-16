#include "core/document/UndoHistory.h"

#include "core/document/Document.h"
#include "core/document/Edit.h"

namespace vdev {

UndoHistory::UndoHistory(Document &document, size_t limit) : m_Document(document), m_Limit(limit) {}

UndoHistory::~UndoHistory() = default;

void UndoHistory::SetLimit(size_t limit) {
    m_Limit = limit;
    Evict();
}

std::string UndoHistory::UndoLabel() const {
    return m_Undo.empty() ? std::string() : m_Undo.back()->Label();
}

std::string UndoHistory::RedoLabel() const {
    return m_Redo.empty() ? std::string() : m_Redo.back()->Label();
}

void UndoHistory::Push(std::unique_ptr<UndoStep> step) {
    if (!step) return;
    // The save point ahead (undone since): the new edit replaces it.
    for (const auto &undone : m_Redo)
        if (undone.get() == m_SaveTop) m_SaveReachable = false;
    m_Redo.clear();
    m_Undo.push_back(std::move(step));
    Evict();
}

void UndoHistory::Evict() {
    while (m_Undo.size() > m_Limit) {
        // The save point is the evicted step, or before all steps.
        if (!m_SaveTop || m_Undo.front().get() == m_SaveTop) m_SaveReachable = false;
        m_Undo.pop_front();
    }
}

void UndoHistory::MarkSavePoint() {
    m_SaveTop = Top();
    m_SaveReachable = !m_Document.Dirty(); // modified since it was saved: no step is the saved one
    m_SaveOutside = m_Document.ChangesOutsideUndo();
}

void UndoHistory::ReachSavePoint() {
    if (m_SaveReachable && Top() == m_SaveTop && m_Document.ChangesOutsideUndo() == m_SaveOutside) m_Document.MarkSaved();
}

bool UndoHistory::Undo() {
    if (m_Document.EditOpen() || m_Undo.empty()) return false;
    std::unique_ptr<UndoStep> step = std::move(m_Undo.back());
    m_Undo.pop_back();
    if (!step->Undo(m_Document)) {
        Clear(); // the composition may be partly restored: no step matches it any longer
        return false;
    }
    m_Redo.push_back(std::move(step));
    ReachSavePoint();
    return true;
}

bool UndoHistory::Redo() {
    if (m_Document.EditOpen() || m_Redo.empty()) return false;
    std::unique_ptr<UndoStep> step = std::move(m_Redo.back());
    m_Redo.pop_back();
    if (!step->Redo(m_Document)) {
        Clear();
        return false;
    }
    m_Undo.push_back(std::move(step));
    ReachSavePoint();
    return true;
}

void UndoHistory::Clear() {
    m_Undo.clear();
    m_Redo.clear();
    m_SaveTop = nullptr;
    m_SaveReachable = false;
}

} // namespace vdev

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

namespace vdev {

class Document;
class UndoStep;

// The document's undo history (Document::History): the undo
// steps its edits recorded (Edit). Dev's default undo depth is 50 (UI
// reference section 14.3).
class UndoHistory {
public:
    ~UndoHistory();
    UndoHistory(const UndoHistory &) = delete;
    UndoHistory &operator=(const UndoHistory &) = delete;

    // The depth (tests use deeper or shallower histories).
    void SetLimit(size_t limit);

    // A step that fails clears the history (the composition may be partly
    // restored, the other steps no longer match it). False while an edit is
    // open (Document::EditOpen): its step is not in the history yet.
    bool Undo();
    bool Redo();
    void Clear();
    // The composition as it was saved or opened: Undo / Redo coming back to
    // this step leave the document unmodified (Document::MarkSaved) unless
    // it was changed outside undo since (Document::ChangesOutsideUndo). The
    // step is lost once it is evicted, cleared or replaced by a new edit.
    void MarkSavePoint();

    bool CanUndo() const { return !m_Undo.empty(); }
    bool CanRedo() const { return !m_Redo.empty(); }
    std::string UndoLabel() const;
    std::string RedoLabel() const;
    size_t UndoCount() const { return m_Undo.size(); }

private:
    // Only the document has one (a second history would not see its edits);
    // only an edit adds to it.
    friend class Document;
    friend class Edit;
    explicit UndoHistory(Document &document, size_t limit = 50);
    // A new step (applied already): the steps undone are dropped.
    void Push(std::unique_ptr<UndoStep> step);
    const UndoStep *Top() const { return m_Undo.empty() ? nullptr : m_Undo.back().get(); }
    void Evict();
    void ReachSavePoint();

    Document &m_Document;
    size_t m_Limit;
    std::deque<std::unique_ptr<UndoStep>> m_Undo;
    std::deque<std::unique_ptr<UndoStep>> m_Redo;
    // The save point: the step on top then (null: none), whether it can
    // still be reached, the changes outside undo then.
    const UndoStep *m_SaveTop = nullptr;
    bool m_SaveReachable = false;
    std::uint64_t m_SaveOutside = 0;
};

} // namespace vdev

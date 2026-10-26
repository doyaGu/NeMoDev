#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "CKAll.h"
#include "core/interface/InterfaceData.h"

#include "core/document/EditResult.h"
#include "core/document/UndoHistory.h"

namespace vdev {

class Document;
class UndoStep;

// A part of an undo step that is done and taken back on its own. False: it
// failed; the step takes back the actions before it.
class Action {
public:
    virtual ~Action() = default;
    virtual bool Redo(Document &document) = 0;
    virtual bool Undo(Document &document) = 0;
};

// An undo step being built. Its actions are done as they
// are recorded; Commit puts the step in the history, and an edit that ends
// without it takes back what it did. One begun while another is open is
// nested in it: its step joins that edit's on Commit, and goes back with it.
// Meanwhile the history is neither undone nor redone (Document::EditOpen).
//
// The step's bookkeeping is the edit's: the objects it made are saved with
// the composition and wait in the graveyard while it is undone, the ones it
// removed the other way round; the Scripts are listed again and the
// composition is marked modified by undo (Marks) each time it is applied,
// undone or redone; the display data of the Scripts it shows is put back
// after the runtime is. A step that fails clears the history (UndoHistory) and
// leaves its objects out of the graveyard: they may still be in the
// composition, and are kept and saved rather than lost. An edit that cannot
// take back what it did clears the history the same way (InvalidateHistory),
// and the edits it is nested in record nothing. An edit taken back whole
// leaves the composition unmodified when it was so at its start and nothing
// changed outside undo meanwhile (as undoing back to the save point).
class Edit {
public:
    // What applying, undoing or redoing the step marks modified by undo.
    enum class Marks {
        Composition, // the composition (Document::MarkUndoableChange) and the Scripts it shows or touches
        Scripts,     // only the Scripts it shows or touches: display edits, and none for the 3D Layout's own cameras
    };

    Edit(Document &document, std::string label, Marks marks = Marks::Composition);
    // Not committed: takes back what it did (Cancel).
    ~Edit();
    Edit(const Edit &) = delete;
    Edit &operator=(const Edit &) = delete;

    Document &Composition() const { return *m_Document; }
    CKContext *Context() const;
    const std::string &Label() const { return m_Label; }
    bool Nested() const { return m_Parent != nullptr; }
    // Not committed, cancelled or failed yet.
    bool Open() const { return m_Document && !m_Finished; }
    // Nothing recorded so far.
    bool Empty() const;

    // Runs redo now and records the pair; false (and nothing recorded) when
    // redo fails or raises a structured exception (then the edit fails as an
    // error, Fail).
    bool Do(std::function<bool()> redo, std::function<bool()> undo);
    // Runs the action's Redo now and records it; false as above.
    bool Do(std::unique_ptr<Action> action);
    // Records a pair whose redo was performed already.
    void Done(std::function<bool()> redo, std::function<bool()> undo);
    // Records an action performed already.
    void Done(std::unique_ptr<Action> action);

    // The Script's display data, to edit in place (the Schematic shows it as
    // it changes): the data before is kept from the first call, Commit records
    // the change. Null when the Script has no session or it is read-only.
    InterfaceData *ScriptData(CK_ID script);
    // Puts data in place of the Script's display data and refreshes its
    // runtime fields. False as ScriptData.
    bool ShowScript(CK_ID script, const InterfaceData &data);
    // The Script shows what the edit changes in the runtime (a parameter's
    // value): its session is marked modified with the step.
    void Touches(CK_ID script);
    // An object the edit made: saved with the composition (with the objects it
    // owns), in the graveyard while the step is undone.
    void Created(CK_ID id);
    // An object the edit removed: in the graveyard (kept for undo, never
    // saved) while the step is applied.
    void Bury(CK_ID id);

    // Ends the edit: its step goes into the history (nested: into the edit it
    // is in). With nothing recorded nothing is (NoChange).
    EditResult Commit(const std::string &message = std::string(), CK_ID created = 0);
    // Ends the edit taking back what it did: Refused (message), or Error when
    // the runtime raised in Do or what was done could not be taken back.
    EditResult Fail(const std::string &message);
    // Fail for an edit with nothing to do (NoChange).
    EditResult Unchanged(const std::string &message);
    // Ends the edit taking back what it did (a gesture's Escape). False when
    // that failed (the history is cleared).
    bool Cancel();

private:
    friend class Document;
    // The document closed with the edit open: it ends with nothing to take
    // back (the objects are gone) and nothing recorded.
    void Abandon();
    bool Rollback();
    void Finish();

    Document *m_Document; // null once the document closed (Abandon)
    Edit *m_Parent;
    std::string m_Label;
    std::unique_ptr<UndoStep> m_Step;
    std::map<CK_ID, size_t> m_Shown; // Script -> its display change in m_Step
    bool m_Finished = false;
    bool m_Raised = false; // a step raised a structured exception (Do)
    bool m_Broken = false; // a nested edit could not be taken back
    // At the start (not nested): unmodified, and the changes outside undo so far.
    bool m_CleanAtStart = false;
    std::uint64_t m_OutsideAtStart = 0;
};

// An edit committed with whatever was done when it goes out of scope:
// several commands as one undo step where each may be refused on its own (it
// reports its refusal; the others stay). Use an Edit where it is all or
// nothing.
class EditGroup {
public:
    EditGroup(Document &document, std::string label) : m_Edit(document, std::move(label)) {}
    ~EditGroup() { m_Edit.Commit(); }
    EditGroup(const EditGroup &) = delete;
    EditGroup &operator=(const EditGroup &) = delete;

private:
    Edit m_Edit;
};

// One undo step in the history: what an Edit recorded.
class UndoStep {
public:
    UndoStep(std::string label, Edit::Marks marks) : m_Label(std::move(label)), m_Marks(marks) {}
    const std::string &Label() const { return m_Label; }
    // False: it failed (the history is cleared).
    bool Redo(Document &document) { return Run(document, false); }
    bool Undo(Document &document) { return Run(document, true); }

private:
    friend class Edit;
    friend class NestedStep;
    friend class UndoHistory;
    // Objects required to replay this step, including its nested edits.
    void RetainObjects(std::set<CK_ID> &objects) const;
    // A Script's display before and after the step.
    struct Display {
        CK_ID script = 0;
        InterfaceData before, after;
    };
    bool Run(Document &document, bool undo, bool mark = true);
    // The actions in order (undo: back), taking back the ones done when one fails.
    bool RunActions(Document &document, bool undo);
    void Show(Document &document, bool undo) const;
    // What applying it marks, the Scripts listed again first (MarkAll: with
    // the steps nested in it).
    void Mark(Document &document) const;
    void MarkAll(Document &document) const;
    bool Empty() const;

    std::string m_Label;
    Edit::Marks m_Marks;
    std::vector<std::unique_ptr<Action>> m_Actions;
    std::vector<Display> m_Displays;
    std::set<CK_ID> m_Touched;
    std::set<CK_ID> m_Created;
    std::set<CK_ID> m_Buried;
};

// Applies mutate to a copy of the Script's display data and records the
// change as one undo step (an Edit marking the Script only). Returns false
// for read-only sessions.
bool EditScriptData(Document &document, CK_ID script, const std::string &label,
                    const std::function<void(InterfaceData &)> &mutate);

} // namespace vdev

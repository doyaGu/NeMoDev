#include "core/document/Edit.h"

#include <algorithm>

#include "core/document/Document.h"
#include "core/graph/RuntimeGraph.h"
#include "core/runtime/CkRuntimeHost.h"

namespace vdev {

namespace {

// A pair of functions (Edit::Do, Done); a structured exception is a failure.
class FunctionAction : public Action {
public:
    FunctionAction(std::function<bool()> redo, std::function<bool()> undo)
        : m_Redo(std::move(redo)), m_Undo(std::move(undo)) {}
    bool Redo(Document &document) override { return Call(document, m_Redo, "redo"); }
    bool Undo(Document &document) override { return Call(document, m_Undo, "undo"); }

private:
    static bool Call(Document &document, const std::function<bool()> &fn, const char *what) {
        bool ok = false;
        return GuardedCall([&] { ok = fn(); }, &document.Host(), what) && ok;
    }
    std::function<bool()> m_Redo, m_Undo;
};

} // namespace

// A nested edit's step in the step of the edit it was in.
class NestedStep : public Action {
public:
    explicit NestedStep(std::unique_ptr<UndoStep> step) : step(std::move(step)) {}
    bool Redo(Document &document) override { return step->Redo(document); }
    bool Undo(Document &document) override { return step->Undo(document); }
    // Taken back with the edit it is in: as Undo, marking nothing (the edit changed nothing).
    bool TakeBack(Document &document) { return step->Run(document, true, false); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override { step->RetainObjects(document, objects); }
    std::unique_ptr<UndoStep> step;
};

// ---------------------------------------------------------------------------
// UndoStep

bool UndoStep::RunActions(Document &document, bool undo) {
    if (!undo) {
        for (size_t i = 0; i < m_Actions.size(); ++i)
            if (!m_Actions[i]->Redo(document)) {
                while (i-- > 0) m_Actions[i]->Undo(document);
                return false;
            }
        return true;
    }
    for (size_t i = m_Actions.size(); i-- > 0;)
        if (!m_Actions[i]->Undo(document)) {
            for (size_t j = i + 1; j < m_Actions.size(); ++j) m_Actions[j]->Redo(document);
            return false;
        }
    return true;
}

bool UndoStep::Run(Document &document, bool undo, bool mark) {
    const auto valid = [&](ObjectRef ref) { return document.Resolve(ref, CKCID_OBJECT, Document::ObjectScope::Existing) != nullptr; };
    const bool validTargets = std::all_of(m_Created.begin(), m_Created.end(), valid) &&
        std::all_of(m_Buried.begin(), m_Buried.end(), valid) && std::all_of(m_Touched.begin(), m_Touched.end(), valid) &&
        std::all_of(m_Displays.begin(), m_Displays.end(), [&](const Display &display) { return valid(display.script); });
    const bool ok = validTargets && RunActions(document, undo);
    // A step that failed may have left its objects where they were: they
    // stay out of the graveyard (listed and saved) rather than vanish while
    // still in the composition.
    for (ObjectRef object : m_Created) document.SetDiscarded(object, ok && undo);
    for (ObjectRef object : m_Buried) document.SetDiscarded(object, ok && !undo);
    if (!m_Created.empty() || !m_Buried.empty()) document.EditorObjectsChanged();
    document.RefreshScripts();
    // The display after the runtime: its runtime fields are read from the graph as it is now.
    if (ok) Show(document, undo);
    if (mark) Mark(document);
    return ok;
}

void UndoStep::Show(Document &document, bool undo) const {
    for (const Display &display : m_Displays) {
        ScriptSession *session = document.Session(document.NativeId(display.script, Document::ObjectScope::Existing));
        if (!session) continue; // the Script is not in the composition (the step took it away)
        session->Restore(undo ? display.before : display.after);
        session->TouchUndoable();
    }
}

void UndoStep::Mark(Document &document) const {
    if (m_Marks == Edit::Marks::Composition) document.MarkUndoableChange();
    for (ObjectRef script : m_Touched)
        if (ScriptSession *session = document.ExistingSession(document.NativeId(script, Document::ObjectScope::Existing))) session->TouchUndoable();
}

void UndoStep::MarkAll(Document &document) const {
    Mark(document);
    for (const auto &action : m_Actions)
        if (const auto *nested = dynamic_cast<const NestedStep *>(action.get())) nested->step->MarkAll(document);
}

bool UndoStep::Empty() const {
    return m_Actions.empty() && m_Displays.empty() && m_Touched.empty() && m_Created.empty() && m_Buried.empty();
}

void UndoStep::RetainObjects(const Document &document, std::set<CK_ID> &objects) const {
    const auto keep = [&](ObjectRef ref) {
        if (const auto id = document.NativeId(ref, Document::ObjectScope::Existing)) objects.insert(id);
    };
    for (ObjectRef ref : m_Created) keep(ref);
    for (ObjectRef ref : m_Buried) keep(ref);
    for (ObjectRef ref : m_Touched) keep(ref);
    for (const Display &display : m_Displays) keep(display.script);
    for (const auto &action : m_Actions) action->RetainObjects(document, objects);
}

// ---------------------------------------------------------------------------
// Edit

Edit::Edit(Document &document, std::string label, Marks marks)
    : m_Document(&document), m_Parent(document.m_OpenEdit), m_Label(label),
      m_Step(std::make_unique<UndoStep>(std::move(label), marks)) {
    document.m_OpenEdit = this;
    if (!m_Parent) {
        m_CleanAtStart = !document.Dirty();
        m_OutsideAtStart = document.ChangesOutsideUndo();
    }
}

Edit::~Edit() {
    if (Open()) Cancel();
}

CKContext *Edit::Context() const {
    return m_Document ? m_Document->Context() : nullptr;
}

bool Edit::Empty() const {
    return !m_Step || m_Step->Empty();
}

bool Edit::Do(std::function<bool()> redo, std::function<bool()> undo) {
    if (!Open()) return false;
    bool ok = false;
    if (!GuardedCall([&] { ok = redo(); }, &m_Document->Host(), m_Label.c_str())) {
        ok = false;
        m_Raised = true; // the edit fails as an error (Fail), not a refusal
    }
    if (ok) Done(std::move(redo), std::move(undo));
    return ok;
}

bool Edit::Do(std::unique_ptr<Action> action) {
    if (!Open() || !action) return false;
    bool ok = false;
    if (!GuardedCall([&] { ok = action->Redo(*m_Document); }, &m_Document->Host(), m_Label.c_str())) {
        ok = false;
        m_Raised = true;
    }
    if (ok) Done(std::move(action));
    return ok;
}

void Edit::Done(std::function<bool()> redo, std::function<bool()> undo) {
    Done(std::make_unique<FunctionAction>(std::move(redo), std::move(undo)));
}

void Edit::Done(std::unique_ptr<Action> action) {
    if (Open() && action) m_Step->m_Actions.push_back(std::move(action));
}

InterfaceData *Edit::ScriptData(CK_ID script) {
    if (!Open()) return nullptr;
    ScriptSession *session = m_Document->Session(script);
    if (!session || session->ReadOnly()) return nullptr;
    if (!m_Shown.count(script)) {
        UndoStep::Display display;
        display.script = m_Document->Reference(script);
        display.before = session->Data();
        display.before.observers.clear();
        m_Shown[script] = m_Step->m_Displays.size();
        m_Step->m_Displays.push_back(std::move(display));
    }
    session->TouchUndoable();
    return &session->m_Data;
}

bool Edit::ShowScript(CK_ID script, const InterfaceData &data) {
    if (data.rootBehavior.id != script) return false;
    if (!ScriptData(script)) return false;
    ScriptSession *session = m_Document->ExistingSession(script);
    session->Restore(data);
    return true;
}

void Edit::Touches(CK_ID script) {
    if (Open())
        if (const auto ref = m_Document->Reference(script)) m_Step->m_Touched.insert(ref);
}

void Edit::Created(CK_ID id) {
    if (!Open() || !id) return;
    const auto ref = m_Document->Reference(id);
    if (!ref) return;
    m_Step->m_Created.insert(ref);
    m_Document->Include(id);
    for (CK_ID owned : CollectOwnedObjects(Context(), {id})) m_Document->Include(owned);
    m_Document->EditorObjectsChanged();
}

void Edit::Bury(CK_ID id) {
    if (!Open() || !id) return;
    const auto ref = m_Document->Reference(id);
    if (!ref) return;
    m_Step->m_Buried.insert(ref);
    m_Document->SetDiscarded(ref, true);
    m_Document->EditorObjectsChanged();
}

EditResult Edit::Commit(const std::string &message, CK_ID created) {
    if (!Open()) return EditResult::Error(m_Label + ": the edit has ended");
    if (m_Broken) {
        // A nested edit could not be taken back: the steps no longer match the composition.
        Rollback();
        return EditResult::Error(m_Label + " failed and could not be undone: the undo history is cleared");
    }
    if (m_Step->Empty()) {
        Finish();
        m_Step.reset();
        m_Document->CollectGraveyard();
        return EditResult::NoChange();
    }
    Document &document = *m_Document;
    for (UndoStep::Display &display : m_Step->m_Displays) {
        const ScriptSession *session = document.ExistingSession(document.NativeId(display.script, Document::ObjectScope::Existing));
        display.after = session ? session->Data() : display.before;
        display.after.observers.clear();
    }
    document.RefreshScripts();
    std::unique_ptr<UndoStep> step = std::move(m_Step);
    Edit *parent = m_Parent;
    Finish();
    if (parent) {
        // Marked with the edit it is in (taken back with it, it changed nothing).
        parent->Done(std::make_unique<NestedStep>(std::move(step)));
    } else {
        step->MarkAll(document);
        document.History().Push(std::move(step));
    }
    return EditResult::Applied(message, created);
}

EditResult Edit::Fail(const std::string &message) {
    if (Open() && !Rollback())
        return EditResult::Error(message + " (what was done could not be taken back: the undo history is cleared)");
    return m_Raised ? EditResult::Error(message + " (the runtime raised an exception)") : EditResult::Refused(message);
}

EditResult Edit::Unchanged(const std::string &message) {
    EditResult result = Fail(message);
    if (result.status == EditStatus::Refused) result.status = EditStatus::NoChange;
    return result;
}

bool Edit::Cancel() {
    return !Open() || Rollback();
}

bool Edit::Rollback() {
    Document &document = *m_Document;
    UndoStep &step = *m_Step;
    bool restored = true;
    for (size_t i = step.m_Actions.size(); i-- > 0;) {
        Action &action = *step.m_Actions[i];
        auto *nested = dynamic_cast<NestedStep *>(&action);
        if (!(nested ? nested->TakeBack(document) : action.Undo(document))) restored = false;
    }
    for (ObjectRef ref : step.m_Buried) document.SetDiscarded(ref, false);
    document.RefreshScripts();
    step.Show(document, true);
    if (!restored) {
        // Partly taken back: the steps recorded no longer match the
        // composition, and the objects made may be in it (kept, saved).
        document.Host().Log(LogLevel::Error, m_Label + " could not be taken back: the undo history is cleared");
        document.InvalidateHistory();
    }
    if (!restored || m_Broken) {
        for (Edit *outer = m_Parent; outer; outer = outer->m_Parent) outer->m_Broken = true;
        Finish();
        m_Step.reset();
        document.RequestGraveyardCollection();
        return false;
    }
    // Never part of the composition once taken back: kept out of the saved file.
    for (ObjectRef ref : step.m_Created) document.SetDiscarded(ref, true);
    if (!step.m_Created.empty() || !step.m_Buried.empty()) document.EditorObjectsChanged();
    // Shown back as it was (the sessions marked), unmodified as it was.
    if (!m_Parent && m_CleanAtStart && document.ChangesOutsideUndo() == m_OutsideAtStart) document.MarkSaved();
    Finish();
    m_Step.reset(); // Release actions and state chunks before their objects.
    document.RequestGraveyardCollection();
    return true;
}

void Edit::Finish() {
    m_Finished = true;
    if (!m_Document) return;
    if (m_Document->m_OpenEdit == this) {
        m_Document->m_OpenEdit = m_Parent;
        return;
    }
    // Ended before an edit begun in it (two gestures overlapping): that one
    // is now in this one's parent.
    for (Edit *inner = m_Document->m_OpenEdit; inner; inner = inner->m_Parent)
        if (inner->m_Parent == this) {
            inner->m_Parent = m_Parent;
            break;
        }
}

void Edit::Abandon() {
    m_Finished = true;
    m_Step.reset(); // its state chunks go while the runtime is there
    m_Document = nullptr;
}

bool EditScriptData(Document &document, CK_ID script, const std::string &label,
                    const std::function<void(InterfaceData &)> &mutate) {
    ScriptSession *session = document.Session(script);
    if (!session || session->ReadOnly()) return false;
    InterfaceData after = session->Data();
    after.observers.clear();
    mutate(after);
    Edit edit(document, label, Edit::Marks::Scripts);
    return edit.ShowScript(script, after) && edit.Commit().ok;
}

} // namespace vdev

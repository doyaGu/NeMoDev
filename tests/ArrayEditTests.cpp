#include "application/EditorApplication.h"
#include "core/document/Clipboard.h"
#include "core/level/ArrayCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "features/arrays/ArrayCommands.h"
#include "editor/ImGuiDrawing.h"
#include "workspace/EditorClient.h"
#include <limits>
#include <cstring>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string Cell(CKDataArray *a, int r, int c) {
    int size = a->GetElementStringValue(r, c, nullptr, 0);
    std::string s(std::max(size, 1), '\0');
    a->GetElementStringValue(r, c, s.data(), int(s.size()));
    s.resize(std::strlen(s.c_str()));
    return s;
}
CKParameter *CellParameter(CKDataArray *a, int r, int c) {
    return reinterpret_cast<CKParameter *>(*a->GetElement(r, c));
}
CK_PARAMETERSTRINGFUNCTION originalString = nullptr;
unsigned strings = 0;
CKERROR FailSecondString(CKParameter *p, char *value, CKBOOL set) {
    auto result = originalString(p, value, set);
    if (set && ++strings == 2)
        return CKERR_INVALIDOPERATION;
    return result;
}
} // namespace
void TestArrayEdits(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    auto *a = CKDataArray::Cast(doc.Context()->CreateObject(CKCID_DATAARRAY, "Table editing"));
    Check(a, "create array");
    doc.Include(a->GetID());
    a->InsertColumn(-1, CKARRAYTYPE_STRING, "Name");
    a->InsertColumn(-1, CKARRAYTYPE_INT, "Number");
    a->InsertColumn(-1, CKARRAYTYPE_PARAMETER, "Value", CKPGUID_FLOAT);
    a->InsertColumn(-1, CKARRAYTYPE_OBJECT, "Object");
    for (int r = 0; r < 3; ++r) {
        a->AddRow();
        std::string s = "row" + std::to_string(r);
        a->SetElementStringValue(r, 0, s.data());
    }
    const auto owned = doc.Reference(CellParameter(a, 0, 2)->GetID());
    const auto second = doc.Reference(CellParameter(a, 1, 2)->GetID());
    EditorApplication app([&] { return &doc; }, [] { return true; });
    Clipboard clipboard;
    ArrayCommands commands([&] { return &doc; }, clipboard);
    const auto paint = [](void *, const EditorHost *, const EditorValueInput *) -> uint32_t { return 0; };
    const EditorValueEditorDesc editors[] = {
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.string"),
         EDITOR_TEXT("test.array-values"), EDITOR_TEXT("Text"), paint, nullptr, EDITOR_TEXT_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.array-column"),
         EDITOR_TEXT("test.array-values"), EDITOR_TEXT("Column"), paint, nullptr, EDITOR_RECORD}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.array-values");
    module.editors = editors;
    module.editorCount = 2;
    Check(commands.Register(app, error) && app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error),
          error.c_str());
    app.Attach(doc.Token(), doc.TopologyRevision(), true, true);
    const auto owner = app.OpenScope();
    const auto scope = app.Scope(owner);
    const auto target = doc.Reference(a->GetID());
    const auto sync = [&] { app.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    const auto execute = [&](const char *id, std::vector<EditorField> fields) {
        fields.insert(fields.begin(), {{"target", EDITOR_OBJECT, target},
                                       {"revision", EDITOR_INTEGER_VALUE, {}, {}, doc.TopologyRevision()}});
        const auto values = BorrowEditorFields(fields);
        const auto result = app.Execute(id, {scope, values.data(), uint32_t(values.values.size())});
        app.EndFrame();
        sync();
        return result.status;
    };
    const auto cells = [&](std::vector<uint64_t> rows, int col, EditorEdit &edit) {
        EditorValue selected{EditorLiteral("rows"), EDITOR_INTEGER_LIST};
        selected.integers = {rows.data(), uint32_t(rows.size())};
        const EditorValue values[] = {ObjectArgument("target", target), selected, IntegerArgument("column", col)};
        return app.BeginEdit("array.cells", {scope, values, 3}, edit);
    };
    const auto text = [&](EditorEdit edit, const std::string &value) {
        const EditorValue v{EditorLiteral("value"), EDITOR_TEXT_VALUE, {}, EditorText(value)};
        return app.UpdateEdit(edit, v);
    };
    auto count = doc.History().UndoCount();
    EditorEdit edit = 0;
    Check(cells({0}, 0, edit) == EDITOR_OK, "begin table cell");
    for (int i = 0; i < 40; ++i)
        Check(text(edit, "draft" + std::to_string(i)) == EDITOR_OK, "update table draft");
    Check(Cell(a, 0, 0) == "row0" && doc.History().UndoCount() == count, "draft changed native cell or history");
    Check(app.FinishEdit(edit, false) == EDITOR_CANCELLED, "cancel table cell");
    Check(Cell(a, 0, 0) == "row0", "cancel failed to restore cell");
    Check(cells({0}, 0, edit) == EDITOR_OK && text(edit, "accepted") == EDITOR_OK &&
              app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "accept table cell");
    app.EndFrame();
    sync();
    Check(Cell(a, 0, 0) == "accepted" && doc.History().UndoCount() == count + 1,
          "cell interaction did not make exactly one undo step");
    Check(doc.History().Undo() && Cell(a, 0, 0) == "row0" && doc.Reference(CellParameter(a, 0, 2)->GetID()) == owned,
          "cell Undo replaced parameter identity");
    sync();
    Check(cells({0}, 0, edit) == EDITOR_OK && text(edit, "row0") == EDITOR_OK &&
              app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "accept unchanged cell");
    app.EndFrame();
    sync();
    Check(doc.History().CanRedo() && doc.History().UndoCount() == count, "unchanged cell discarded Redo");
    Check(doc.History().Redo() && Cell(a, 0, 0) == "accepted", "cell Redo");
    sync();
    Check(cells({0, 2}, 0, edit) == EDITOR_OK && text(edit, "batch") == EDITOR_OK &&
              app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "accept batch cells");
    app.EndFrame();
    sync();
    Check(Cell(a, 0, 0) == "batch" && Cell(a, 2, 0) == "batch" && doc.History().UndoCount() == count + 2,
          "batch is not one undo");
    Check(doc.History().Undo(), "undo batch");
    sync();
    Check(execute("array.key-set", {{"column", EDITOR_INTEGER_VALUE, {}, {}, 0}}) == EDITOR_OK, "set key column");
    const auto before = doc.History().UndoCount();
    Check(cells({0, 1}, 0, edit) == EDITOR_OK && text(edit, "unique") == EDITOR_OK &&
              app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "queue duplicate key batch");
    app.EndFrame();
    sync();
    Check(Cell(a, 0, 0) == "accepted" && Cell(a, 1, 0) == "row1" && doc.History().UndoCount() == before,
          "rejected batch partially changed cells or history");
    Check(execute("array.key-set", {{"column", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(-1)}}) == EDITOR_OK,
          "clear key column");
    Check(cells({0}, 2, edit) == EDITOR_OK && text(edit, "4.5") == EDITOR_OK &&
              app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "parameter cell text");
    app.EndFrame();
    sync();
    float value = 0;
    CellParameter(a, 0, 2)->GetValue(&value);
    Check(value == 4.5f && doc.Reference(CellParameter(a, 0, 2)->GetID()) == owned,
          "native parameter cell setter result or identity");
    Check(doc.History().Undo(), "undo parameter cell");
    sync();
    CellParameter(a, 0, 2)->GetValue(&value);
    Check(value == 0 && doc.Reference(CellParameter(a, 0, 2)->GetID()) == owned, "parameter undo changed identity");
    // A string function can write and then report failure. The whole batch,
    // including prior cells and the failing parameter, must be restored.
    auto *type = doc.Context()->GetParameterManager()->GetParameterTypeDescription(CellParameter(a, 0, 2)->GetType());
    originalString = type->StringFunction;
    type->StringFunction = FailSecondString;
    strings = 0;
    const auto failed = SetArrayCells(doc, a->GetID(), {0, 1}, 2, "9.5");
    type->StringFunction = originalString;
    Check(!failed.ok, "failing parameter batch accepted");
    CellParameter(a, 0, 2)->GetValue(&value);
    Check(value == 0, "first parameter survived failed batch");
    CellParameter(a, 1, 2)->GetValue(&value);
    Check(value == 0 && doc.Reference(CellParameter(a, 1, 2)->GetID()) == second, "failing parameter was not restored");
    sync();
    // Position metadata is captured, so a reordered table cannot receive an
    // old draft. A cancelled draft never changes selection or redo history.
    Check(cells({0}, 0, edit) == EDITOR_OK && text(edit, "wrong row") == EDITOR_OK, "stale cell draft");
    Check(MoveArrayRow(doc, a->GetID(), 0, 1).ok, "reorder while draft is open");
    sync();
    Check(app.FinishEdit(edit, true) == EDITOR_STALE && Cell(a, 0, 0) == "row1" && Cell(a, 1, 0) == "accepted",
          "stale cell addressed another row");
    Check(doc.History().Undo() && doc.Reference(CellParameter(a, 0, 2)->GetID()) == owned, "reorder undo identities");
    sync();
    Check(execute("array.rows-remove", {{"rows", EDITOR_INTEGER_LIST, {}, {}, 0, {}, {0, 2}}}) == EDITOR_OK &&
              a->GetRowCount() == 1,
          "remove rows command");
    Check(doc.Resolve(owned) == nullptr && doc.Resolve(owned, CKCID_PARAMETER, Document::ObjectScope::Existing),
          "deleted parameter was not buried");
    Check(doc.History().Undo() && a->GetRowCount() == 3 && doc.Reference(CellParameter(a, 0, 2)->GetID()) == owned,
          "row Undo replaced original parameter");
    sync();
    Check(execute("array.column-remove", {{"column", EDITOR_INTEGER_VALUE, {}, {}, 2}}) == EDITOR_OK &&
              a->GetColumnCount() == 3,
          "delete parameter column");
    Check(doc.History().Undo() && a->GetColumnCount() == 4 && doc.Reference(CellParameter(a, 0, 2)->GetID()) == owned,
          "column Undo replaced original parameter");
    sync();
    // Column properties share a record draft; cancelling a type change does
    // not create parameters, clear cells or leave an undo entry.
    const EditorValue colArgs[] = {ObjectArgument("target", target), IntegerArgument("column", 0)};
    Check(app.BeginEdit("array.column", {scope, colArgs, 2}, edit) == EDITOR_OK, "begin column record");
    EditorField state;
    EditorValueEditorDesc renderer{};
    Check(app.EditState(edit, state, renderer), "read column draft");
    state.fields[0].text = "Renamed";
    state.fields[1].integer = 2;

    std::vector<EditorField> columnValue{state};
    const auto updated = BorrowEditorFields(columnValue);
    Check(app.UpdateEdit(edit, updated.values.front()) == EDITOR_OK && app.FinishEdit(edit, false) == EDITOR_CANCELLED,
          "cancel column record");
    Check(std::string(a->GetColumnName(0)) == "Name" && a->GetColumnType(0) == CKARRAYTYPE_STRING,
          "column cancel changed native table");
    Check(app.BeginEdit("array.column", {scope, colArgs, 2}, edit) == EDITOR_OK &&
              app.UpdateEdit(edit, updated.values.front()) == EDITOR_OK && app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "accept column record");
    app.EndFrame();
    sync();
    Check(std::string(a->GetColumnName(0)) == "Renamed" && a->GetColumnType(0) == CKARRAYTYPE_FLOAT,
          "column edit not applied");
    Check(doc.History().Undo() && Cell(a, 0, 0) == "accepted" &&
              doc.Reference(CellParameter(a, 0, 2)->GetID()) == owned,
          "column type Undo lost cells/identity");
    sync();
    // The parameter clipboard retains a removed source cell. Pasting its
    // value must work while the source waits in Undo's graveyard.
    Check(SetArrayCells(doc, a->GetID(), {0}, 2, "7.25").ok, "prepare copied cell");
    sync();
    Check(execute("array.copy", {{"rows", EDITOR_INTEGER_LIST, {}, {}, 0, {}, {0}},
                                 {"column", EDITOR_INTEGER_VALUE, {}, {}, 2}}) == EDITOR_OK,
          "copy parameter cell command");
    Check(execute("array.rows-remove", {{"rows", EDITOR_INTEGER_LIST, {}, {}, 0, {}, {0}}}) == EDITOR_OK,
          "remove copied row");
    Check(execute("array.paste", {{"rows", EDITOR_INTEGER_LIST, {}, {}, 0, {}, {0}},
                                  {"column", EDITOR_INTEGER_VALUE, {}, {}, 2}}) == EDITOR_OK,
          "paste retained cell value");
    CellParameter(a, 0, 2)->GetValue(&value);
    Check(value == 7.25f, "retained cell value was not pasted");
    Check(doc.History().Undo() && doc.History().Undo() && doc.Reference(CellParameter(a, 0, 2)->GetID()) == owned &&
              doc.History().Undo(),
          "undo copied cell and row");
    clipboard = Clipboard{};
    sync();
    std::string longText(4096, 'x');
    Check(cells({0}, 0, edit) == EDITOR_OK && text(edit, longText) == EDITOR_OK &&
              app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "long cell draft");
    app.EndFrame();
    sync();
    Check(Cell(a, 0, 0) == longText, "cell text truncated");
    Check(cells({0}, 0, edit) == EDITOR_OK && text(edit, "closed") == EDITOR_OK, "owner close draft");
    app.CloseScope(owner);
    Check(app.UpdateEdit(edit, "late") == EDITOR_STALE && Cell(a, 0, 0) == longText, "closed table draft wrote a cell");
    // New columns use the same record flow and keep the chosen insertion
    // position. Missing parameter types are rejected before a draft closes.
    const auto another = app.OpenScope();
    const auto anotherScope = app.Scope(another);
    const EditorValue insertArgs[] = {ObjectArgument("target", target), IntegerArgument("column", 0)};
    Check(app.BeginEdit("array.new-column", {anotherScope, insertArgs, 2}, edit) == EDITOR_OK, "begin inserted column");
    Check(app.EditState(edit, state, renderer), "insert column draft");
    state.fields[0].text = "Inserted";
    state.fields[1].integer = 5;
    columnValue = {state};
    auto invalid = BorrowEditorFields(columnValue);
    Check(app.UpdateEdit(edit, invalid.values.front()) == EDITOR_OK && app.FinishEdit(edit, true) == EDITOR_INVALID,
          "missing parameter type accepted");
    state.fields[1].integer = 1;
    // Record members are addressed by name, independent of provider ordering.
    std::swap(state.fields[0], state.fields[2]);
    columnValue = {state};
    auto reordered = BorrowEditorFields(columnValue);
    Check(app.UpdateEdit(edit, reordered.values.front()) == EDITOR_OK && app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "insert reordered column record");
    app.EndFrame();
    sync();
    Check(a->GetColumnCount() == 5 && std::string(a->GetColumnName(0)) == "Inserted" && Cell(a, 0, 1) == longText,
          "column insertion lost destination or values");
    Check(doc.History().Undo() && a->GetColumnCount() == 4 && doc.Reference(CellParameter(a, 0, 2)->GetID()) == owned,
          "insert column Undo identities");
    sync();
    auto *object = doc.Context()->CreateObject(CKCID_BEOBJECT, "Named cell object");
    doc.Include(object->GetID());
    Check(SetArrayCells(doc, a->GetID(), {0}, 3, "Named cell object").ok && a->GetElementObject(0, 3) == object,
          "native object cell text result");
    Check(doc.History().Undo() && !a->GetElementObject(0, 3), "undo object cell");
    sync();
    const auto nativeCount = doc.Context()->GetObjectListByType(CKCID_PARAMETER, TRUE).Size();
    Check(RemoveArrayColumn(doc, a->GetID(), 2).ok, "remove column for repeated replay");
    for (int i = 0; i < 12; ++i)
        Check(doc.History().Undo() && doc.Reference(CellParameter(a, 0, 2)->GetID()) == owned && doc.History().Redo(),
              "replay replaced parameter identity");
    Check(doc.History().Undo() && doc.Context()->GetObjectListByType(CKCID_PARAMETER, TRUE).Size() == nativeCount,
          "replaying table leaked placeholder objects");
    sync();
    app.Stop();
    doc.History().Clear();
    doc.History().SetLimit(1);
    const auto retired = doc.Reference(CellParameter(a, 0, 2)->GetID());
    Check(RemoveArrayRows(doc, a->GetID(), {0}).ok &&
              doc.Resolve(retired, CKCID_PARAMETER, Document::ObjectScope::Existing),
          "history retain deleted cell");
    Check(InsertArrayRow(doc, a->GetID()).ok && !doc.Resolve(retired, CKCID_PARAMETER, Document::ObjectScope::Existing),
          "history eviction leaked cell parameter");
}

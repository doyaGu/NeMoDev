#include "features/arrays/ArrayTable.h"
#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"
#include <algorithm>
#include <iterator>

namespace vdev {
namespace {
const EditorField *Field(const std::vector<EditorField> &fields, const char *key) {
    for (const auto &f : fields)
        if (f.key == key)
            return &f;
    return nullptr;
}
int Number(const std::vector<EditorField> &fields, const char *key) {
    const auto *v = Field(fields, key);
    return v ? int(int64_t(v->integer)) : 0;
}
std::string Text(const std::vector<EditorField> &fields, const char *key) {
    const auto *v = Field(fields, key);
    return v ? v->text : std::string();
}
std::string Text(EditorString s) { return {s.data ? s.data : "", s.size}; }
void CancelInput(unsigned id) {
    if (!id || !ImGui::GetCurrentContext())
        return;
    auto &g = *ImGui::GetCurrentContext();
    if (g.ActiveId == id)
        ImGui::ClearActiveID();
    // InputText keeps one final write after deactivation. A cancelled draft
    // must also drop that write before the same cell is drawn again.
    if (g.InputTextDeactivatedState.ID == id)
        g.InputTextDeactivatedState.ID = 0;
}
uint32_t DrawColumn(void *, const EditorHost *host, const EditorValueInput *input) {
    std::vector<EditorField> owned;
    if (CopyEditorFields(owned, &input->value, 1) != EDITOR_OK || owned.front().fields.size() != 3)
        return EDITOR_INPUT_INVALID;
    const auto &record = owned.front().fields;
    const char *const keys[] = {"name", "type", "parameter"};
    const EditorValueType types[] = {EDITOR_TEXT_VALUE, EDITOR_INTEGER_VALUE, EDITOR_INTEGER_VALUE};
    size_t indices[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto field = std::find_if(record.begin(), record.end(),
                                        [&](const auto &v) { return v.key == keys[i] && v.type == types[i]; });
        if (field == record.end())
            return EDITOR_INPUT_INVALID;
        indices[i] = size_t(field - record.begin());
    }
    uint32_t actions = 0;
    const char *const names[] = {"Name", "Type", "Parameter"};
    for (size_t i = 0; i < 3; ++i) {
        if (i == 2 && owned.front().fields[indices[1]].integer != 5)
            continue;
        auto &value = owned.front().fields[indices[i]];
        std::vector<EditorField> metadata;
        if (i == 1) {
            metadata = {{"labels", EDITOR_TEXT_LIST}, {"values", EDITOR_INTEGER_LIST}};
            metadata[0].texts = {"Integer", "Float", "String", "Object", "Parameter"};
            metadata[1].integers = {1, 2, 3, 4, 5};
        }
        if (i == 2) {
            std::vector<EditorField> copied;
            if (CopyEditorFields(copied, input->fields, input->count) != EDITOR_OK)
                return EDITOR_INPUT_INVALID;
            const auto *labels = Field(copied, "parameter-labels"), *values = Field(copied, "parameter-types");
            if (!labels || !values)
                return EDITOR_INPUT_INVALID;
            metadata = {*labels, *values};
            metadata[0].key = "labels";
            metadata[1].key = "values";
        }
        struct Update {
            std::vector<EditorField> *owned;
            size_t index;
            const EditorValueInput *input;
        } update{&owned, indices[i], input};
        auto write = [](void *data, const EditorValue *v, uint32_t count) -> EditorStatus {
            auto &u = *static_cast<Update *>(data);
            std::vector<EditorField> copied;
            if (count != 1 || CopyEditorFields(copied, v, count) != EDITOR_OK ||
                copied.front().type != u.owned->front().fields[u.index].type)
                return EDITOR_INVALID;
            auto next = *u.owned;
            copied.front().key = next.front().fields[u.index].key;
            next.front().fields[u.index] = std::move(copied.front());
            const auto fields = BorrowEditorFields(next);
            auto status = u.input->update.write(u.input->update.data, fields.data(), 1);
            if (status == EDITOR_OK)
                *u.owned = std::move(next);
            return status;
        };
        const std::vector<EditorField> childValue{value};
        const auto borrowed = BorrowEditorFields(childValue), fields = BorrowEditorFields(metadata);
        auto child = *input;
        child.label = EditorLiteral(names[i]);
        child.value = borrowed.values.front();
        child.update = {&update, write};
        child.fields = fields.data();
        child.count = uint32_t(fields.values.size());
        child.style = EDITOR_VALUE_INLINE;
        child.focus = input->focus && i == 0;
        child.choicesQuery = {};
        child.drawMembers = nullptr;
        child.membersData = nullptr;
        ImGui::SetNextItemWidth(240);
        const auto action =
            host->drawValue(host->data, EditorLiteral(i == 0 ? "value.string" : "value.enumeration"), &child);
        actions |= i == 0 ? action : action & ~uint32_t(EDITOR_INPUT_ACCEPT);
    }
    return actions;
}
} // namespace
bool RegisterArrayValueEditors(EditorApplication &app, std::string &error) {
    const EditorValueEditorDesc editor{sizeof(EditorValueEditorDesc),
                                       EDITOR_IMGUI_VERSION,
                                       EDITOR_TEXT("value.array-column"),
                                       EDITOR_TEXT("builtin.array-values"),
                                       EDITOR_TEXT("Array Column"),
                                       DrawColumn,
                                       nullptr,
                                       EDITOR_RECORD};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("arrays.values");
    module.editors = &editor;
    module.editorCount = 1;
    return app.Register(module, error);
}
void ArrayTable::Cancel(const EditorClient &client) {
    CancelInput(activeInput);
    activeInput = 0;
    client.CancelEdit(cell);
    client.CancelEdit(dialog);
    popup.clear();
    openPopup = openDelete = false;
    deleteColumn = -1;
    editRow = editColumn = -1;
}
void ArrayTable::Draw(const EditorClient &client, ObjectRef object) {
    if (target != object) {
        Cancel(client);
        *this = ArrayTable{};
        target = object;
    }
    if (selectionOperation) {
        const auto completion = client.Completion(selectionOperation);
        if (completion.status != EDITOR_QUEUED) {
            if (completion.status == EDITOR_OK)
                selected = std::move(selectionAfter);
            selectionOperation = 0;
            selectionAfter.clear();
        }
    }
    const auto table = client.Query("array.table", {ObjectArgument("target", target)});
    if (table.status != EDITOR_OK) {
        Cancel(client);
        ImGui::TextDisabled("Array is unavailable");
        return;
    }
    const auto current = EditorClient::Integer(table, "revision");
    if (current != revision) {
        Cancel(client);
        revision = current;
    }
    const int rowCount = int(EditorClient::Integer(table, "rows")),
              key = int(int64_t(EditorClient::Integer(table, "key")));
    const auto *schema = EditorClient::Find(table, "columns");
    if (!schema)
        return;
    const auto &columns = schema->fields;
    const int columnCount = int(columns.size());
    for (auto it = selected.begin(); it != selected.end();)
        it = *it < 0 || *it >= rowCount ? selected.erase(it) : std::next(it);
    auto rowsArgument = [&] {
        EditorValue v{EditorLiteral("rows"), EDITOR_INTEGER_LIST};
        return v;
    };
    auto command = [&](const char *id, std::vector<EditorValue> values = {}) {
        client.CancelEdit(cell);
        editRow = editColumn = -1;
        std::vector<uint64_t> rows(selected.begin(), selected.end());
        auto selectedRows = rowsArgument();
        selectedRows.integers = {rows.data(), uint32_t(rows.size())};
        values.insert(values.begin(), {ObjectArgument("target", target), IntegerArgument("revision", revision)});
        if (std::string(id) == "array.rows-remove" || std::string(id) == "array.rows-duplicate" ||
            std::string(id) == "array.copy" || std::string(id) == "array.paste" ||
            std::string(id) == "array.paste-shortcuts" || std::string(id) == "array.remove-shortcuts")
            values.push_back(selectedRows);
        return client.Command(id, values);
    };
    auto beginColumn = [&](int c, const char *title, bool insert = false) {
        Cancel(client);
        if (client.BeginEdit(insert ? "array.new-column" : "array.column",
                             {ObjectArgument("target", target), IntegerArgument("column", uint64_t(int64_t(c)))},
                             dialog) == EDITOR_OK) {
            popup = title;
            openPopup = true;
        }
    };
    auto beginMultiple = [&](int c) {
        Cancel(client);
        std::vector<uint64_t> rows(selected.begin(), selected.end());
        auto v = rowsArgument();
        v.integers = {rows.data(), uint32_t(rows.size())};
        if (client.BeginEdit("array.cells", {ObjectArgument("target", target), v, IntegerArgument("column", c)},
                             dialog) == EDITOR_OK) {
            popup = "Edit Multiple";
            openPopup = true;
        }
    };
    auto dialogWindow = [&](const char *title) {
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if (!dialog) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::SetNextItemWidth(260);
        const auto action =
            client.Host()->drawEdit(client.Host()->data, dialog, EditorLiteral("##value"), ImGui::IsWindowAppearing());
        if (action & EDITOR_INPUT_ACTIVE)
            activeInput = ImGui::GetActiveID();
        const bool cancel = (action & EDITOR_INPUT_CANCEL) || ImGui::IsKeyPressed(ImGuiKey_Escape);
        bool accept = ImGui::Button("OK", ImVec2(80, 0)) || (action & EDITOR_INPUT_ACCEPT);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)) || cancel) {
            client.CancelEdit(dialog);
            popup.clear();
            ImGui::CloseCurrentPopup();
        } else if (accept && !(action & EDITOR_INPUT_INVALID)) {
            const auto result = client.Host()->finishEdit(client.Host()->data, dialog, 1, nullptr);
            if (result == EDITOR_QUEUED || result == EDITOR_NO_CHANGE || result == EDITOR_STALE) {
                dialog = 0;
                popup.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    };
    auto rowClicked = [&](int row, bool right) {
        const auto &io = ImGui::GetIO();
        if (right) {
            if (!selected.count(row))
                selected = {row};
        } else if (io.KeyShift && anchor >= 0) {
            selected.clear();
            for (int r = std::min(row, anchor); r <= std::max(row, anchor); ++r)
                selected.insert(r);
        } else if (io.KeyCtrl) {
            if (!selected.erase(row))
                selected.insert(row);
            anchor = row;
        } else {
            selected = {row};
            anchor = row;
        }
    };
    auto followSelection = [&](const EditorResponse &result, std::set<int> after) {
        if (result.status == EDITOR_OK)
            selected = std::move(after);
        else if (result.status == EDITOR_QUEUED) {
            selectionOperation = result.operation;
            selectionAfter = std::move(after);
        }
    };
    auto removeRows = [&] {
        if (!selected.empty())
            followSelection(command("array.rows-remove"), {});
    };
    auto duplicateRows = [&] {
        if (!selected.empty())
            command("array.rows-duplicate");
    };
    auto moveRow = [&](int step) {
        if (selected.empty())
            return;
        const int row = *selected.begin();
        const auto result = command("array.row-move",
                                    {IntegerArgument("index", row), IntegerArgument("step", uint64_t(int64_t(step)))});
        followSelection(result, {row + step});
    };
    auto cellCommand = [&](const char *id, int c) {
        if (c >= 0)
            command(id, {IntegerArgument("column", c)});
    };
    auto rowMenu = [&](int row, int c, bool shortcut) {
        if (ImGui::MenuItem("Insert Row"))
            command("array.row-insert", {IntegerArgument("index", row)});
        if (ImGui::MenuItem("Delete Row", "Delete"))
            removeRows();
        if (ImGui::MenuItem("Duplicate Row", "D"))
            duplicateRows();
        const int first = selected.empty() ? row : *selected.begin();
        if (first > 0 && ImGui::MenuItem("Move Row Up", "-"))
            moveRow(-1);
        if (first < rowCount - 1 && ImGui::MenuItem("Move Row Down", "+"))
            moveRow(1);
        if (c < 0)
            return;
        ImGui::Separator();
        if (selected.size() > 1 && c != key) {
            if (ImGui::MenuItem("Edit Multiple"))
                beginMultiple(c);
        } else if (ImGui::MenuItem("Edit")) {
            focusRow = row;
            focusColumn = c;
        }
        if (Number(columns[c].fields, "type") == 5) {
            if (ImGui::MenuItem("Copy", "Ctrl+C"))
                cellCommand("array.copy", c);
            const auto options =
                client.Query("array.paste-options", {ObjectArgument("target", target), IntegerArgument("column", c)});
            if (EditorClient::Integer(options, "paste") && ImGui::MenuItem("Paste", "Ctrl+V"))
                cellCommand("array.paste", c);
            if (EditorClient::Integer(options, "shortcuts") && ImGui::MenuItem("Paste As Shortcut"))
                cellCommand("array.paste-shortcuts", c);
            if (shortcut && ImGui::MenuItem("Remove Shortcut"))
                cellCommand("array.remove-shortcuts", c);
        }
    };
    if (ImGui::Button("Add Column"))
        beginColumn(-1, "New Column", true);
    ImGui::SameLine();
    ImGui::BeginDisabled(!columnCount);
    if (ImGui::Button("Add Row"))
        command("array.row-insert", {IntegerArgument("index", uint64_t(-1))});
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("   %d column(s), %d row(s)", columnCount, rowCount);
    if (openPopup) {
        openPopup = false;
        ImGui::OpenPopup(popup.c_str());
    }
    if (openDelete) {
        openDelete = false;
        ImGui::OpenPopup("Delete Column");
    }
    dialogWindow("New Column");
    dialogWindow("Edit Column Properties");
    dialogWindow("Insert Column");
    dialogWindow("Edit Multiple");
    // The delete confirmation is outside the changing header ID stack.
    if (ImGui::BeginPopupModal("Delete Column", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (deleteColumn < 0) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::TextUnformatted("Are you sure you want to delete this column ?");
        if (ImGui::Button("Yes", ImVec2(80, 0))) {
            command("array.column-remove", {IntegerArgument("column", deleteColumn)});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!columnCount) {
        ImGui::TextDisabled("The array has no column: Add Column first.");
        return;
    }
    if (!ImGui::BeginTable("array", columnCount + 1,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                           ImVec2(0, 0)))
        return;
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40);
    std::vector<std::string> headers;
    for (int c = 0; c < columnCount; ++c) {
        headers.push_back(std::to_string(c) + " : " + Text(columns[c].fields, "name") + (c == key ? " (key)" : ""));
        ImGui::TableSetupColumn(headers.back().c_str(), ImGuiTableColumnFlags_WidthFixed, 130);
    }
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableNextColumn();
    ImGui::TableHeader("");
    for (int c = 0; c < columnCount; ++c) {
        ImGui::TableNextColumn();
        ImGui::PushID(c);
        ImGui::TableHeader(headers[c].c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Text(columns[c].fields, "type-name").c_str());
        bool edit = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left), insert = false;
        if (ImGui::BeginPopupContextItem("columnmenu")) {
            if (ImGui::MenuItem("Sort Ascending", nullptr, false, Number(columns[c].fields, "sortable")))
                command("array.sort", {IntegerArgument("column", c), IntegerArgument("ascending", 1)});
            if (ImGui::MenuItem("Sort Descending", nullptr, false, Number(columns[c].fields, "sortable")))
                command("array.sort", {IntegerArgument("column", c), IntegerArgument("ascending", 0)});
            ImGui::Separator();
            if (ImGui::MenuItem("Edit Column Properties"))
                edit = true;
            if (ImGui::MenuItem(c == key ? "Remove Key Column" : "Make Key Column"))
                command("array.key-set", {IntegerArgument("column", uint64_t(int64_t(c == key ? -1 : c)))});
            if (ImGui::MenuItem("Delete Column")) {
                Cancel(client);
                deleteColumn = c;
                openDelete = true;
            }
            if (c > 0 && ImGui::MenuItem("Move Column Left"))
                command("array.column-move", {IntegerArgument("column", c), IntegerArgument("step", uint64_t(-1))});
            if (c < columnCount - 1 && ImGui::MenuItem("Move Column Right"))
                command("array.column-move", {IntegerArgument("column", c), IntegerArgument("step", 1)});
            if (ImGui::MenuItem("Insert Column"))
                insert = true;
            ImGui::EndPopup();
        }
        // Dialog IDs belong to the whole table, independent of a changing column.
        ImGui::PopID();
        if (edit)
            beginColumn(c, "Edit Column Properties");
        if (insert)
            beginColumn(c, "Insert Column", true);
    }
    bool cellDrawn = false;
    ImGuiListClipper clipper;
    clipper.Begin(rowCount);
    while (clipper.Step())
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            ImGui::PushID(r);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(r).c_str(), selected.count(r) != 0))
                rowClicked(r, false);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                rowClicked(r, true);
            if (ImGui::BeginPopupContextItem("rowmenu")) {
                rowMenu(r, -1, false);
                ImGui::EndPopup();
            }
            for (int c = 0; c < columnCount; ++c) {
                ImGui::TableNextColumn();
                ImGui::PushID(c);
                ImGui::SetNextItemWidth(-1);
                if (focusRow == r && focusColumn == c) {
                    ImGui::SetKeyboardFocusHere();
                    focusRow = focusColumn = -1;
                }
                const uint64_t row = uint64_t(r);
                EditorValue rows{EditorLiteral("rows"), EDITOR_INTEGER_LIST};
                rows.integers = {&row, 1};
                if (cell && editRow == r && editColumn == c &&
                    !ImGui::IsRectVisible({ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()})) {
                    // Cancel before drawing: a clipped active input may emit
                    // deactivation, which would otherwise accept its draft.
                    client.CancelEdit(cell);
                    editRow = editColumn = -1;
                    CancelInput(activeInput);
                    activeInput = 0;
                }
                EditorEdit value = (editRow == r && editColumn == c) ? cell : 0;
                const auto action = client.DrawProperty(
                    "array.cells", {ObjectArgument("target", target), rows, IntegerArgument("column", c)}, value,
                    "##cell");
                if (action & EDITOR_INPUT_ACTIVE)
                    activeInput = ImGui::GetActiveID();
                if (value) {
                    cell = value;
                    editRow = r;
                    editColumn = c;
                    cellDrawn = ImGui::IsItemVisible();
                } else if (editRow == r && editColumn == c) {
                    cell = 0;
                    editRow = editColumn = -1;
                }
                if (ImGui::IsItemActivated()) {
                    column = c;
                    if (!selected.count(r))
                        rowClicked(r, true);
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    rowClicked(r, true);
                    column = c;
                }
                if (ImGui::BeginPopupContextItem("cellmenu")) {
                    // Only fetch shortcut flags while the context menu is open.
                    const auto page =
                        client.Query("array.table", {ObjectArgument("target", target), IntegerArgument("first", r),
                                                     IntegerArgument("count", 1)});
                    bool shortcut = false;
                    if (const auto *cells = EditorClient::Find(page, "cells"))
                        if (!cells->fields.empty())
                            if (const auto *flags = Field(cells->fields.front().fields, "shortcuts"))
                                if (size_t(c) < flags->integers.size())
                                    shortcut = flags->integers[c] != 0;
                    rowMenu(r, c, shortcut);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
    ImGui::EndTable();
    if (cell && !cellDrawn) {
        client.CancelEdit(cell);
        editRow = editColumn = -1;
        CancelInput(activeInput);
        activeInput = 0;
    }
    if (rowCount && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        int row = selected.empty() ? 0 : *selected.begin();
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            selected = {std::max(0, row - 1)};
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            selected = {std::min(rowCount - 1, row + 1)};
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            column = std::max(0, column - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            column = std::min(columnCount - 1, column + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
            ImGui::IsKeyPressed(ImGuiKey_F2)) {
            if (selected.empty())
                selected = {row};
            focusRow = row;
            focusColumn = std::clamp(column, 0, columnCount - 1);
        }
        if (!selected.empty()) {
            const auto &io = ImGui::GetIO();
            if (io.KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_C))
                    cellCommand("array.copy", column);
                if (ImGui::IsKeyPressed(ImGuiKey_V))
                    cellCommand("array.paste", column);
            } else {
                if (ImGui::IsKeyPressed(ImGuiKey_Delete))
                    removeRows();
                if (ImGui::IsKeyPressed(ImGuiKey_D))
                    duplicateRows();
                if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
                    moveRow(-1);
                if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
                    moveRow(1);
            }
        }
    }
}
} // namespace vdev

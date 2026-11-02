#include "ui/schematic/SchematicViews.h"
#include "ui/panels/EditorPanels.h"
#include "ui/app/DesktopFiles.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/DevBitmaps.h"
#include "editor/ImGuiDrawing.h"

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include "ui/schematic/SchematicCanvas.h"
#include "features/schematic/SchematicPosition.h"
#include "workspace/EditorWorkspace.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

bool EditorPanels::Register(EditorApplication &editor, std::string &error) {
    EditorViewDesc view{};
    view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("schematic.explorer"); view.provider = EDITOR_TEXT("builtin.schematic-explorer");
    view.title = EDITOR_TEXT("Schematic Explorer"); view.area = EDITOR_DOCUMENT;
    view.singleInstance = 1; view.documentBound = 1;
    view.data = this;
    view.create = [](void *data, const EditorHost *, EditorView) -> void * {
        auto &panel = *static_cast<EditorPanels *>(data);
        panel.FocusExplorer(); return data;
    };
    view.closing = [](void *data, const EditorHost *, EditorView) {
        static_cast<EditorPanels *>(data)->CloseExplorer(false);
    };
    // The document search survives this singleton window for F3 navigation.
    // Closing releases its drafts; DocumentClosed clears the saved results.
    view.destroy = [](void *) {};
    view.draw = [](void *data, const EditorHost *, EditorView, EditorViewFrame *frame) {
        EditorPrepareViewWindow(*frame);
        bool open = true;
        static_cast<EditorPanels *>(data)->ExplorerPanel(&open);
        frame->close = !open;
    };
    EditorCommandDesc commands[3]{};
    const char *ids[] = {"schematic.open-explorer", "schematic.explorer-next", "schematic.explorer-previous"};
    const char *titles[] = {"Schematic Explorer", "Next Explorer Result", "Previous Explorer Result"};
    for (unsigned i = 0; i < 3; ++i) {
        auto &command = commands[i];
        command.size = sizeof(command);
        command.id = EditorLiteral(ids[i]);
        command.title = EditorLiteral(titles[i]);
        command.provider = EDITOR_TEXT("builtin.schematic-explorer");
        command.data = this;
        command.area = EDITOR_WORKSPACE;
        command.effect = EDITOR_SESSION_CHANGE;
        command.state = [](void *data, const EditorHost *, const EditorRequest *r) {
            auto &panel = *static_cast<EditorPanels *>(data);
            const auto level = panel.m_Workspace.Role("main.level");
            return !r->scope.view && level && panel.m_Workspace.Focused() == level ? EDITOR_REFUSED : EDITOR_OK;
        };
    }
    commands[0].run = [](void *data, const EditorHost *, const EditorRequest *, EditorSink) {
        auto &panel = *static_cast<EditorPanels *>(data);
        if (!panel.m_Workspace.OpenRole("main.schematic-explorer")) return EDITOR_REFUSED;
        panel.FocusExplorer();
        return EDITOR_OK;
    };
    commands[1].run = [](void *data, const EditorHost *, const EditorRequest *, EditorSink) {
        static_cast<EditorPanels *>(data)->FindNext(1);
        return EDITOR_OK;
    };
    commands[2].run = [](void *data, const EditorHost *, const EditorRequest *, EditorSink) {
        static_cast<EditorPanels *>(data)->FindNext(-1);
        return EDITOR_OK;
    };
    const EditorBindingDesc bindings[] = {{commands[0].id, EDITOR_TEXT("Ctrl+F")},
                                          {commands[0].id, EDITOR_TEXT("Ctrl+Shift+F")},
                                          {commands[1].id, EDITOR_TEXT("F3")},
                                          {commands[2].id, EDITOR_TEXT("Shift+F3")}};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("schematic.explorer"); module.views = &view; module.viewCount = 1;
    module.commands = commands;
    module.commandCount = 3;
    module.bindings = bindings;
    module.bindingCount = 4;
    return editor.Register(module, error);
}

void EditorPanels::DocumentClosed() {
    m_ExplorerBox.Clear();
    m_FilterValue.Clear();
    m_ReplaceValue.Clear();
    m_Doc = {};
    m_Pending.clear();
    m_ParameterTypes.clear();
    m_TypesRevision = 0;
    m_ExplorerShown = false;
    m_OpenExplorerValue = false;
}

void EditorPanels::FindNext(int direction) {
    if (m_Doc.results.empty()) {
        Client().Log(EDITOR_LOG_INFO, "No Explorer results (Ctrl+F)");
        return;
    }
    const int count = static_cast<int>(m_Doc.results.size());
    m_Doc.currentResult = m_Doc.currentResult < 0 ? (direction > 0 ? 0 : count - 1)
                                          : ((m_Doc.currentResult + direction) % count + count) % count;
    const SearchResult &result = m_Doc.results[m_Doc.currentResult];
    m_Schematics.Show(result.address, result.focusReference, true, m_Doc.explorerView);
}

void EditorPanels::ShowScript(uint32_t script, const ElementRef &ref, uint32_t focusBehavior, bool focus) {
    if (auto *canvas = m_Schematics.Find(m_Doc.explorerView ? m_Doc.explorerView : m_Schematics.CurrentId()))
        canvas->ShowScript(script, ref, focusBehavior, focus);
}
void EditorPanels::RefreshStack() { m_Schematics.RefreshStacks(); }

SchematicNavigation &EditorPanels::ExplorerNavigation() {
    auto *canvas = m_Schematics.Find(m_Doc.explorerView);
    return canvas ? canvas->Navigation() : m_Schematics.Current().Navigation();
}

EditorClient EditorPanels::Client() const {
    return EditorClient(m_Workspace.Application().Host(),
                        m_Doc.explorerView ? m_Doc.explorerView : m_Schematics.CurrentId());
}
bool EditorPanels::ReadSearch(bool tree, std::vector<SearchResult> &out) {
    const auto inspected = Client().Inspect(m_Doc.explorerView);
    const auto *visible = EditorClient::Find(inspected, "scripts");
    if (inspected.status != EDITOR_OK || !visible)
        return false;
    auto *value = tree ? nullptr : ExplorerValue(false);
    std::vector<EditorField> args{
        {"text", EDITOR_TEXT_VALUE, {}, tree ? "" : m_SearchText},
        {"id", EDITOR_INTEGER_VALUE, {}, {}, tree ? 0 : uint64_t(std::max(0, std::atoi(m_SearchIdText.c_str())))},
        {"kinds", EDITOR_INTEGER_VALUE, {}, {}, tree ? uint64_t(SearchScripts | SearchBehaviors) : m_SearchKinds},
        {"all", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(m_SearchAllScripts)},
        {"scripts", EDITOR_OBJECT_LIST},
        {"filter", EDITOR_INTEGER_VALUE, {}, {}, tree ? 0 : uint64_t(m_SearchFilter)},
        {"type", EDITOR_INTEGER_VALUE, {}, {}, m_SearchType},
        {"derived", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(m_SearchDerived)},
        {"value", EDITOR_OBJECT, value ? value->WorkReference() : ObjectRef{}},
        {"first", EDITOR_INTEGER_VALUE}};
    args[4].objects = visible->objects;
    std::vector<SearchResult> rows;
    uint64_t first = 0, total = 0, revision = 0, topology = 0, scripts = 0;
    do {
        args.back().integer = first;
        const auto values = BorrowEditorFields(args);
        const auto page = Client().Query("schematic.search", {values.data(), values.data() + values.size()});
        const auto *items = EditorClient::Find(page, "rows");
        if (page.status != EDITOR_OK || !items)
            return false;
        const auto count = EditorClient::Integer(page, "total"), next = EditorClient::Integer(page, "next");
        if (next < first || next > count || next - first != items->fields.size() || (next == first && first < count))
            return false;
        if (!first) {
            total = count;
            revision = EditorClient::Integer(page, "document-revision");
            topology = EditorClient::Integer(page, "topology");
            scripts = EditorClient::Integer(page, "scripts-revision");
        } else if (total != count || revision != EditorClient::Integer(page, "document-revision") ||
                   topology != EditorClient::Integer(page, "topology") ||
                   scripts != EditorClient::Integer(page, "scripts-revision"))
            return false;
        for (const auto &item : items->fields) {
            EditorResponse r;
            r.fields = item.fields;
            const auto *address = EditorClient::Find(r, "address"), *focus = EditorClient::Find(r, "focus-target"),
                       *object = EditorClient::Find(r, "object");
            SearchResult row;
            if (!address || !focus || !object || !ReadSchematicPosition(*address, row.address))
                return false;
            row.script = row.address.script.object;
            row.ref = row.address.element;
            row.focusReference = focus->object;
            row.focus = focus->object.object;
            row.objectReference = object->object;
            row.object = object->object.object;
            row.label = EditorClient::Text(r, "label");
            row.kind = EditorClient::Text(r, "kind");
            row.detail = EditorClient::Text(r, "detail");
            row.scriptName = EditorClient::Text(r, "script-name");
            row.objectName = EditorClient::Text(r, "object-name");
            row.objectClass = int32_t(EditorClient::Integer(r, "class"));
            row.classIcon = int(EditorClient::Integer(r, "class-icon"));
            rows.push_back(std::move(row));
        }
        first = next;
    } while (first < total);
    out = std::move(rows);
    return true;
}
void EditorPanels::Command(const char *id, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    const auto result = Client().Command(id, {values.data(), values.data() + values.size()});
    if (result.status == EDITOR_QUEUED)
        m_Pending.emplace_back(result.operation, std::string(id) == "schematic.replace-search-values");
    else if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE)
        Client().Log(EDITOR_LOG_WARNING,
                    result.message.empty() ? "The Schematic target changed; select it again" : result.message);
}
void EditorPanels::Rename(const SearchResult &result) {
    Command("object.rename",
            {{"target", EDITOR_OBJECT, result.objectReference}, {"name", EDITOR_TEXT_VALUE, {}, m_ExplorerRenameText}});
    m_Doc.resultsKey.clear();
}
void EditorPanels::Poll() {
    for (auto it = m_Pending.begin(); it != m_Pending.end();) {
        const auto result = Client().Completion(it->first);
        if (result.status == EDITOR_QUEUED) {
            ++it;
            continue;
        }
        if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE)
            Client().Log(EDITOR_LOG_WARNING,
                        result.message.empty() ? "The Schematic target changed; select it again" : result.message);
        else if (it->second)
            m_FilterValue.CopyDraft(m_ReplaceValue);
        m_Doc.resultsKey.clear();
        it = m_Pending.erase(it);
    }
}

void EditorPanels::RunSearch() {
    if (!Client().Scope().document.value) {
        m_Doc.results.clear();
        return;
    }
    const auto metadata = Client().Query("schematic.stack");
    if (metadata.status != EDITOR_OK)
        return;
    auto *value = ExplorerValue(false);
    std::string key = m_SearchText + '\x1f' + m_SearchIdText + '\x1f' + std::to_string(m_SearchKinds) + ":" +
                      std::to_string(m_SearchAllScripts) + ":" + std::to_string(m_SearchFilter) + ":" +
                      std::to_string(m_SearchType) + ":" + std::to_string(m_SearchDerived) + ":" +
                      std::to_string(Client().Scope().document.value) + ":" +
                      std::to_string(EditorClient::Integer(metadata, "topology")) + ":" +
                      std::to_string(EditorClient::Integer(metadata, "scripts-revision")) + ":" +
                      std::to_string(EditorClient::Integer(metadata, "document-revision"));
    if (!m_SearchAllScripts)
        for (auto id : ExplorerNavigation().visibleScripts)
            key += "," + std::to_string(id);
    if (m_SearchFilter == 2 && value)
        key += '\x1f' + value->Text(0);
    if (const auto *rows = EditorClient::Find(metadata, "rows"))
        for (const auto &row : rows->fields)
            for (const auto &f : row.fields)
                if (f.key == "revision")
                    key += ":" + std::to_string(f.integer);
    if (key == m_Doc.resultsKey)
        return;
    std::vector<SearchResult> results;
    if (!ReadSearch(false, results))
        return;
    m_Doc.results = std::move(results);
    m_Doc.resultsKey = std::move(key);
    m_Doc.currentResult = -1;
    m_Doc.chosen.clear();
    m_Doc.anchor = -1;
}

// The Parameter Filter's value (Edit Value) or replace value (Edit Replace
// Value): parameters of the Type chosen the Explorer keeps (dynamic, not
// saved; Dev.exe sub_5B95A0 sets their type).
ParameterBox *EditorPanels::ExplorerValue(bool replace) {
    if (!Client().Scope().document.value)
        return nullptr;
    const auto revision = m_Workspace.Application().Revision();
    if (m_ParameterTypes.empty() || m_TypesRevision != revision) {
        const auto types = Client().Query("schematic.parameter-types");
        if (types.status == EDITOR_OK)
            if (const auto *items = EditorClient::Find(types, "types")) {
                m_ParameterTypes = items->fields;
                m_TypesRevision = revision;
            }
    }
    if (!m_SearchType)
        for (const auto &row : m_ParameterTypes) {
            EditorResponse r;
            r.fields = row.fields;
            const auto type = EditorClient::Integer(r, "type");
            if (!m_SearchType || EditorClient::Text(r, "name") == "Float")
                m_SearchType = type;
            if (EditorClient::Text(r, "name") == "Float")
                break;
        }
    auto &box = replace ? m_ReplaceValue : m_FilterValue;
    return box.AddDefault(m_SearchType, replace ? "Replace Value" : "Filter Value") ? &box : nullptr;
}

// The rows chosen: the Schematic's selection (Dev.exe sub_5B6B40), those of
// the Script of the row clicked last (a selection is in one Script).
void EditorPanels::ExplorerSelect() {
    if (m_Doc.anchor < 0 || m_Doc.anchor >= int(m_Doc.results.size()))
        return;
    const auto script = m_Doc.results[size_t(m_Doc.anchor)].script;
    std::vector<ElementAddress> selected;
    for (auto row : m_Doc.chosen)
        if (row >= 0 && row < int(m_Doc.results.size())) {
            const auto &result = m_Doc.results[size_t(row)];
            if (result.script == script && result.ref.kind != ElementKind::Script)
                selected.push_back(result.address);
        }
    ExplorerNavigation().selection.Assign(selected);
    if (selected.empty())
        ExplorerNavigation().selection.script = script;
}

// Dev's tree view (sub_5B79E0): the Scripts and their behaviors; a click
// shows one.
void EditorPanels::ExplorerTree(const ImVec2 &min, const ImVec2 &max) {
    ImGui::SetCursorScreenPos(min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vi::Color(7));
    ImGui::PushStyleColor(ImGuiCol_Text, vi::Color(0));
    ImGui::BeginChild("##tree", {max.x - min.x, max.y - min.y}, ImGuiChildFlags_Borders);
    std::vector<SearchResult> rows;
    ReadSearch(true, rows);
    std::function<void(const SearchResult &)> draw = [&](const SearchResult &row) {
        ImGui::PushID(int(row.object));
        const bool leaf = std::none_of(rows.begin(), rows.end(), [&](const SearchResult &other) {
            return other.ref.kind != ElementKind::Script && other.address.owner == row.objectReference;
        });
        const bool selected = m_Doc.treeTarget && m_Doc.treeTarget->objectReference == row.objectReference;
        const bool open = ImGui::TreeNodeEx("##node",
                                            ImGuiTreeNodeFlags_SpanAvailWidth | (leaf ? ImGuiTreeNodeFlags_Leaf : 0) |
                                                (selected ? ImGuiTreeNodeFlags_Selected : 0),
                                            "%s", row.label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_Doc.treeTarget = row;
            m_Schematics.Show(row.address, row.focusReference, true, m_Doc.explorerView);
        }
        if (open) {
            for (const auto &child : rows)
                if (child.ref.kind != ElementKind::Script && child.address.owner == row.objectReference)
                    draw(child);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    for (const auto &row : rows)
        if (row.ref.kind == ElementKind::Script)
            draw(row);
    if (m_Doc.treeTarget && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        m_ExplorerRename = -2;
        m_ExplorerRenameText = m_Doc.treeTarget->objectName;
        ImGui::OpenPopup("Rename###explorertreerename");
    }
    if (ImGui::BeginPopupModal("Rename###explorertreerename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool entered = ImGui::InputText("##name", &m_ExplorerRenameText, ImGuiInputTextFlags_EnterReturnsTrue);
        if ((entered || ImGui::Button("OK", {80, 0})) && m_Doc.treeTarget) {
            Rename(*m_Doc.treeTarget);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {80, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

// Dev's Schematic Explorer (dialog 732 "Schematic Filter Box", Dev.exe
// sub_5B4F40 / sub_5B5040 / sub_5B5A80): the Item List (Item, Script, Type;
// or the tree), Show in List (the kinds, CK ID and Text Filters, Parse All
// / Only Visible Items, the entities chosen), Selection Actions (All, None,
// Invert, GoTo, Delete), the Parameter Filter (No Filter, Filter Type,
// Filter Value: Type, Derived, Edit Value, Edit Replace Value, Replace
// Value); Select keeps the selection made, Cancel gives the Schematic's back.
void EditorPanels::CloseExplorer(bool keep) {
    if (!m_ExplorerShown) return;
        if (!keep && m_Schematics.Find(m_Doc.explorerView)) {
            m_Doc.selectionBefore.Validate([&](const ElementAddress &address) {
                EditorField elements{"elements", EDITOR_RECORD};
                elements.fields = {SchematicPosition(address, "0")};
                const std::vector<EditorField> owned{elements};
                const auto values = BorrowEditorFields(owned);
                const auto result =
                    EditorClient(m_Workspace.Application().Host(), m_Doc.explorerView)
                        .Query("schematic.validate-positions", {values.data(), values.data() + values.size()});
                const auto *valid = EditorClient::Find(result, "elements");
                return result.status == EDITOR_OK && valid && valid->fields.size() == 1;
            });
            ExplorerNavigation().selection.Assign(m_Doc.selectionBefore.Addresses());
        }
    const EditorValue settings[] = {
        IntegerArgument("Schematic Explorer\\Show In List", m_SearchKinds),
        IntegerArgument("Schematic Explorer\\Parse All Items", m_SearchAllScripts),
        IntegerArgument("Schematic Explorer\\Tree View", m_SearchTree),
        IntegerArgument("Schematic Explorer\\Parameter Filter", m_SearchFilter),
        IntegerArgument("Schematic Explorer\\Derived", m_SearchDerived)};
    const EditorValue value{EDITOR_TEXT("value"), EDITOR_RECORD, {}, {}, 0, {}, {}, {}, 0, {}, {settings, 5}};
    EditorClient(m_Workspace.Application().Host(), 0).Command("preferences.set-explorer-options", {value});
    m_ExplorerBox.Clear(); m_FilterValue.Clear(); m_ReplaceValue.Clear();
    m_ExplorerShown = false;
}
void EditorPanels::ExplorerPanel(bool *open) {
    if (m_ExplorerShown && !m_Schematics.Find(m_Doc.explorerView)) {
        m_ExplorerBox.Clear();
        m_FilterValue.Clear();
        m_ReplaceValue.Clear();
        m_ExplorerShown = false;
        *open = false;
        return;
    }
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 units(345.0f, 279.0f);
    constexpr float PAD = 4.5f;
    ImVec2 o;
    m_Preferences.Refresh(Client());
    const auto &prefs = m_Preferences;
    if (!m_ExplorerShown) {
        m_ExplorerShown = true;
        m_Doc.explorerView = m_Schematics.CurrentId();
        m_Doc.selectionBefore = ExplorerNavigation().selection;
        // The settings it had when it closed last.
        m_SearchKinds = static_cast<unsigned>(prefs.Int("Schematic Explorer\\Show In List"));
        if (!m_SearchKinds) m_SearchKinds = SearchScripts;
        m_SearchAllScripts = prefs.Bool("Schematic Explorer\\Parse All Items");
        m_SearchTree = prefs.Bool("Schematic Explorer\\Tree View");
        m_SearchFilter = std::max(0, std::min(2, prefs.Int("Schematic Explorer\\Parameter Filter")));
        m_SearchDerived = prefs.Bool("Schematic Explorer\\Derived");
    }
    auto close = [&](bool keep) { CloseExplorer(keep); *open = false; };
    if (!vi::BeginWindow("Schematic Explorer", "Schematic Explorer", vi::TemplateClient(units, s, PAD), s, o)) {
        vi::EndWindow();
        return;
    }
    const vi::TemplateLayout l = vi::LayoutTemplate(o, units, s, PAD);
    vi::TemplatePanel(l);
    RunSearch();
    Poll();
    auto text = [&](float x, float y, float w, float h, const char *label, bool bold = false, vi::Align align = vi::LEFT) {
        vi::Text(l.Min(x, y), l.Max(x, y, w, h), label, bold, align);
    };

    // The Item List, or the tree.
    text(0, 2, 65, 10, "Item List", true);
    if (vi::Button("treeview", l.Min(150, 2), l.Max(150, 2, 80, 10), m_SearchTree ? "Toggle To List View" : "Toggle To Tree View"))
        m_SearchTree = !m_SearchTree;
    const int count = static_cast<int>(m_Doc.results.size());
    if (m_SearchTree) {
        ExplorerTree(l.Min(0, 13), l.Max(0, 13, 230, 210));
    } else {
        static const std::vector<vi::ListColumn> COLUMNS{{"Item", 0.0f}, {"Script", 110.0f}, {"Type", 70.0f}};
        bool twice = false;
        const int clicked = vi::ListView(
            "##items", l.Min(0, 13), l.Max(0, 13, 230, 210), s, COLUMNS, count,
            [&](int r, int c) -> std::string {
                const SearchResult &result = m_Doc.results[r];
                if (c == 0)
                    return result.label;
                if (c == 2)
                    return result.kind;
                return result.scriptName;
            },
            [&](int r) { return m_Doc.chosen.count(r) != 0; }, &twice, nullptr, false, nullptr,
            // Documentation "Schematic Explorer": "Icons in front of an item is
            // the element type" (Dev's class icons).
            [&](int r) -> ImTextureID {
                const auto icon = m_Doc.results[r].classIcon;
                return icon ? DevIconImage(icon) : ImTextureID();
            });
        if (clicked >= 0) {
            const ImGuiIO &io = ImGui::GetIO();
            if (io.KeyShift && m_Doc.anchor >= 0) {
                m_Doc.chosen.clear();
                for (int r = std::min(clicked, m_Doc.anchor); r <= std::max(clicked, m_Doc.anchor); ++r) m_Doc.chosen.insert(r);
            } else if (io.KeyCtrl) {
                if (!m_Doc.chosen.erase(clicked)) m_Doc.chosen.insert(clicked);
                m_Doc.anchor = clicked;
            } else {
                m_Doc.chosen = {clicked};
                m_Doc.anchor = clicked;
            }
            m_Doc.currentResult = clicked;
            ExplorerSelect();
            if (twice) {
                const SearchResult &result = m_Doc.results[clicked];
                m_Schematics.Show(result.address, result.focusReference, true, m_Doc.explorerView);
            }
        }
        // Its menu (Dev.exe sub_5B8520): Edit, the first row chosen's dialog
        // (sub_5B8690); F2 renames it (the list's label edit).
        const ImVec2 listMin = l.Min(0, 13), listMax = l.Max(0, 13, 230, 210);
        const bool overList = ImGui::IsMouseHoveringRect(listMin, listMax) && ImGui::IsWindowHovered();
        if (overList && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) ImGui::OpenPopup("explorermenu");
        const int first = m_Doc.chosen.empty() ? -1 : *m_Doc.chosen.begin();
        if (ImGui::BeginPopup("explorermenu")) {
            if (ImGui::MenuItem("Edit", nullptr, false, first >= 0 && first < count)) {
                const SearchResult &result = m_Doc.results[first];
                m_Schematics.Show(result.address, result.focusReference, true, m_Doc.explorerView);
                auto values = SchematicPosition(result.address).fields;
                if (result.focusReference)
                    values.push_back({"focus-target", EDITOR_OBJECT, result.focusReference});
                Command("script.edit-element", values);
                m_Schematics.Focus(m_Doc.explorerView);
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_F2, false) && first >= 0 && first < count && m_Doc.results[first].object &&
            m_Doc.results[first].ref.kind != ElementKind::Comment) {
            m_ExplorerRename = first;
            m_ExplorerRenameText = m_Doc.results[first].objectName;
            ImGui::OpenPopup("Rename###explorerrename");
        }
        if (ImGui::BeginPopupModal("Rename###explorerrename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool entered = ImGui::InputText("##name", &m_ExplorerRenameText, ImGuiInputTextFlags_EnterReturnsTrue);
            if ((entered || ImGui::Button("OK", ImVec2(80, 0))) && m_ExplorerRename >= 0 && m_ExplorerRename < count) {
                const SearchResult &result = m_Doc.results[m_ExplorerRename];
                Rename(result);
                m_Doc.resultsKey.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // Show in List.
    text(233, 2, 91, 10, "Show In List", true);
    vi::Frame(l.Min(233, 13), l.Max(233, 13, 112, 210));
    struct Kind {
        const char *label;
        unsigned bit;
    };
    static const Kind KINDS[] = {{"Scripts", SearchScripts},
                                 {"Behavior Graphs", SearchGraphs},
                                 {"Building Blocks", SearchBuildingBlocks},
                                 {"Comments", SearchComments},
                                 {"Parameter Operations", SearchOperations},
                                 {"Shortcuts", SearchShortcuts},
                                 {"Local Parameters", SearchLocalParameters},
                                 {"Parameter Inputs", SearchParameterInputs},
                                 {"Parameter Outputs", SearchParameterOutputs},
                                 {"Behavior Inputs", SearchBehaviorInputs},
                                 {"Behavior Outputs", SearchBehaviorOutputs}};
    for (int k = 0; k < 11; ++k) {
        bool on = (m_SearchKinds & KINDS[k].bit) != 0;
        const float y = 14.0f + 11.0f * k;
        if (vi::CheckButton(KINDS[k].label, l.Min(237, y), l.Max(237, y, 104, 10), on, KINDS[k].label, !m_SearchTree))
            m_SearchKinds = on ? (m_SearchKinds | KINDS[k].bit) : (m_SearchKinds & ~KINDS[k].bit);
    }
    text(237, 137, 60, 10, "Filter uint32_t");
    vi::Edit("##ckid", l.Min(299, 137), l.Max(299, 137, 43, 12), m_SearchIdText);
    text(237, 149, 105, 10, "Text Filter");
    if (m_FocusSearch) {
        ImGui::SetKeyboardFocusHere();
        m_FocusSearch = false;
    }
    vi::Edit("##text", l.Min(237, 158), l.Max(237, 158, 105, 12), m_SearchText, vi::LEFT);
    // The button names what it changes to (Dev.exe sub_5B5A80 1453).
    if (vi::Button("parse", l.Min(249, 197), l.Max(249, 197, 80, 10),
                   m_SearchAllScripts ? "Parse Only Visible Items" : "Parse All Items"))
        m_SearchAllScripts = !m_SearchAllScripts;
    char chosen[64];
    std::snprintf(chosen, sizeof(chosen), m_Doc.chosen.size() == 1 ? "%d Entity Selected" : "%d Entities Selected",
                  static_cast<int>(m_Doc.chosen.size()));
    text(237, 211, 105, 10, chosen, false, vi::CENTER);

    // Selection Actions.
    text(1, 226, 102, 8, "Selection Actions", true);
    vi::Frame(l.Min(0, 235), l.Max(0, 235, 102, 43));
    const bool listing = !m_SearchTree;
    if (vi::Button("All", l.Min(3, 239), l.Max(3, 239, 46, 10), "All", listing)) {
        for (int r = 0; r < count; ++r) m_Doc.chosen.insert(r);
        if (m_Doc.anchor < 0 && count) m_Doc.anchor = 0;
        ExplorerSelect();
    }
    if (vi::Button("None", l.Min(3, 251), l.Max(3, 251, 46, 10), "None", listing)) {
        m_Doc.chosen.clear();
        ExplorerNavigation().selection.Clear();
    }
    if (vi::Button("Invert", l.Min(3, 264), l.Max(3, 264, 46, 10), "Invert", listing)) {
        std::set<int> inverted;
        for (int r = 0; r < count; ++r)
            if (!m_Doc.chosen.count(r)) inverted.insert(r);
        m_Doc.chosen = inverted;
        if (!inverted.empty()) m_Doc.anchor = *inverted.begin();
        ExplorerSelect();
    }
    // In the tree: the node chosen.
    const auto *treeNode = m_Doc.treeTarget ? &*m_Doc.treeTarget : nullptr;
    if (vi::Button("GoTo", l.Min(53, 239), l.Max(53, 239, 46, 10), "GoTo",
                   listing ? !m_Doc.chosen.empty() : treeNode != nullptr)) {
        if (listing) {
            const auto &row = m_Doc.results[size_t(*m_Doc.chosen.begin())];
            m_Doc.currentResult = *m_Doc.chosen.begin();
            m_Schematics.Show(row.address, row.focusReference, true, m_Doc.explorerView);
            ExplorerSelect();
        } else if (treeNode)
            m_Schematics.Show(treeNode->address, treeNode->focusReference, true, m_Doc.explorerView);
    }
    if (vi::Button("Delete", l.Min(53, 251), l.Max(53, 251, 46, 10), "Delete",
                   listing ? !m_Doc.chosen.empty() : treeNode != nullptr)) {
        EditorField elements{"elements", EDITOR_RECORD};
        const auto add = [&](const SearchResult &row) {
            elements.fields.push_back(SchematicPosition(row.address, std::to_string(elements.fields.size()).c_str()));
        };
        if (listing)
            for (int row : m_Doc.chosen)
                add(m_Doc.results[size_t(row)]);
        else if (treeNode)
            add(*treeNode);
        Command("schematic.delete-search-results", {elements});
        ExplorerNavigation().selection.Clear();
        m_Doc.chosen.clear();
        m_Doc.treeTarget.reset();
        m_Doc.resultsKey.clear();
    }

    // The Parameter Filter (Dev.exe sub_5B80C0): the Type with Filter Type,
    // the values too with Filter Value.
    text(109, 226, 236, 8, "Parameter Filter", true);
    vi::Frame(l.Min(109, 235), l.Max(109, 235, 236, 43));
    static const char *const MODES[] = {"No Filter", "Filter Type", "Filter Value"};
    const int mode = vi::Combo("##filtermode", l.Min(115, 236), l.Max(115, 236, 93, 12), MODES[m_SearchFilter],
                               [] { return std::vector<std::string>{"No Filter", "Filter Type", "Filter Value"}; }, m_SearchFilter);
    if (mode >= 0) m_SearchFilter = mode;
    const bool typed = m_SearchFilter >= 1 && listing, valued = m_SearchFilter == 2 && listing;
    text(212, 238, 22, 10, "Type:");
    auto *value = ExplorerValue(false), *replace = ExplorerValue(true);
    std::string currentType;
    std::vector<std::string> names;
    for (const auto &row : m_ParameterTypes) {
        EditorResponse r;
        r.fields = row.fields;
        auto name = EditorClient::Text(r, "name");
        names.push_back(name);
        if (EditorClient::Integer(r, "type") == m_SearchType)
            currentType = name;
    }
    ImGui::BeginDisabled(!typed);
    const auto chosenType =
        vi::Combo("##type", l.Min(237, 236), l.Max(237, 236, 105, 12), currentType, [&] { return names; }, -1);
    if (chosenType >= 0 && size_t(chosenType) < m_ParameterTypes.size()) {
        EditorResponse r;
        r.fields = m_ParameterTypes[size_t(chosenType)].fields;
        m_SearchType = EditorClient::Integer(r, "type");
        value = ExplorerValue(false);
        replace = ExplorerValue(true);
    }
    ImGui::EndDisabled();
    vi::CheckButton("Derived", l.Min(290, 251), l.Max(290, 251, 52, 10), m_SearchDerived, "Derived", typed);
    if (vi::Button("editvalue", l.Min(115, 251), l.Max(115, 251, 75, 10), "Edit Value", valued && value)) {
        m_ExplorerBox.Clear();
        if (m_ExplorerBox.AddDraft(*value, "Filter Value")) {
            m_ExplorerEditReplace = false;
            m_OpenExplorerValue = true;
        }
    }
    text(196, 251, 85, 10, value && valued ? AnsiToUtf8(value->Text(0)).c_str() : "");
    if (vi::Button("editreplace", l.Min(115, 264), l.Max(115, 264, 75, 10), "Edit Replace Value", valued && replace)) {
        m_ExplorerBox.Clear();
        if (m_ExplorerBox.AddDraft(*replace, "Replace Value")) {
            m_ExplorerEditReplace = true;
            m_OpenExplorerValue = true;
        }
    }
    text(196, 264, 85, 10, replace && valued ? AnsiToUtf8(replace->Text(0)).c_str() : "");
    // Replace Value (Dev.exe sub_5B9600): every parameter listed takes the
    // replace value, which the filter then looks for.
    if (vi::Button("replacevalue", l.Min(285, 264), l.Max(285, 264, 57, 10), "Replace Value", valued && replace && value)) {
        EditorField elements{"elements", EDITOR_RECORD};
        for (const auto &row : m_Doc.results)
            if (row.objectReference)
                elements.fields.push_back(
                    SchematicPosition(row.address, std::to_string(elements.fields.size()).c_str()));
        Command("schematic.replace-search-values", {elements, {"value", EDITOR_OBJECT, replace->WorkReference()}});
    }
    // Edit Value / Edit Replace Value: Dev's Edit Parameter on the value.
    const char *dialog = "Edit Parameter###ExplorerValue";
    if (m_OpenExplorerValue) {
        ImGui::OpenPopup(dialog);
        m_OpenExplorerValue = false;
    }
    const char *typeName = currentType.c_str();
    const DialogAction action = EditParameterValueDialog(dialog, "Edit Parameter", m_ExplorerBox, typeName);
    if (action == DialogAction::Ok)
        if (auto *target = m_ExplorerEditReplace ? replace : value)
            target->CopyDraft(m_ExplorerBox);
    if (action != DialogAction::None) {
        m_ExplorerBox.Clear();
        m_Doc.resultsKey.clear();
    }

    if (vi::DialogButton("Select", ImVec2(l.right - 130.0f * s, l.buttonsY), s, ImGuiKey_Enter)) close(true);
    else if (vi::DialogButton("Cancel", ImVec2(l.right - 60.0f * s, l.buttonsY), s, ImGuiKey_Escape)) close(false);
    vi::EndWindow();
}

} // namespace vdev

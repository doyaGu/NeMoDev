#include "ui/panels/EditorPanels.h"
#include "ui/app/DesktopFiles.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/DevBitmaps.h"

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
#include "core/schematic/SchematicEdits.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/graph/ValueCommands.h"
#include "core/graph/TopologyCommands.h"
#include "ui/controls/ValueEditor.h"

namespace vdev {

namespace {

std::string LowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

void EditorPanels::DocumentClosed() {
    m_ExplorerBox.Clear();
    m_Doc = DocumentState();
}

void EditorPanels::FindNext(int direction) {
    if (m_Doc.results.empty()) {
        m_State.Log(LogLevel::Info, "No Explorer results (Ctrl+F)");
        return;
    }
    const int count = static_cast<int>(m_Doc.results.size());
    m_Doc.currentResult = m_Doc.currentResult < 0 ? (direction > 0 ? 0 : count - 1)
                                          : ((m_Doc.currentResult + direction) % count + count) % count;
    const SearchResult &result = m_Doc.results[m_Doc.currentResult];
    ShowScript(result.script, result.ref, result.focus);
}

void EditorPanels::ShowScript(CK_ID script, const ElementRef &ref, CK_ID focusBehavior, bool focus) {
    if (!m_State.HasDocument()) return;
    const ScriptInfo *info = m_State.document->FindScript(script);
    if (!info) return;
    // Showing a Script explicitly brings it back from Hide Selected / Other.
    if (focus) m_State.hiddenScripts.erase(script);
    m_State.stackScript = script;
    CKContext *context = m_State.document->Context();
    CKLevel *level = context ? context->GetCurrentLevel() : nullptr;
    CKScene *scene = m_State.view.sceneFilter && level ? level->GetCurrentScene() : nullptr;
    m_State.visibleScripts.clear();
    // Scripts created at run time (or of objects so created) only with Show
    // Dynamic Objects (the one asked for always).
    const bool showDynamic = m_State.preferences.Bool("3D Layout\\Show Dynamic Objects");
    for (const auto &other : m_State.document->Scripts()) {
        const bool sameOwner = info->ownerId ? other.ownerId == info->ownerId : other.id == script;
        if (!m_State.view.showAllScripts && !sameOwner) continue;
        if (m_State.hiddenScripts.count(other.id)) continue;
        if (!showDynamic && other.id != script) {
            CKObject *each = context->GetObject(other.id);
            CKObject *owner = context->GetObject(other.ownerId);
            if ((each && each->IsDynamic()) || (owner && owner->IsDynamic())) continue;
        }
        if (scene) {
            // Scene filter: Scripts whose owner is in the active scene (the
            // Level's own Scripts always).
            CKObject *owner = context->GetObject(other.ownerId);
            if (owner && owner != level && !scene->IsObjectHere(owner)) {
                // Dev.exe sub_4C81C0 ([Schematic]<Dialog>#0) for the one asked for.
                if (other.id == script && focus)
                    m_State.Log(LogLevel::Warning, "Script is not in current scene. Remove Schematic scene filter to show it");
                continue;
            }
        }
        m_State.visibleScripts.push_back(other.id);
    }
    // Dev stacks Scripts by their stored Header::scriptIndex (2D Text.cmo);
    // Scripts without a saved InterfaceChunk follow in document order.
    auto order = [&](CK_ID id) -> unsigned long long {
        ScriptSession *session = m_State.document->Session(id);
        return session && session->Origin() == SessionOrigin::LoadedChunk ? session->Data().header.scriptIndex
                                                                           : ~0ull;
    };
    std::stable_sort(m_State.visibleScripts.begin(), m_State.visibleScripts.end(),
                     [&](CK_ID a, CK_ID b) { return order(a) < order(b); });
    if (!m_State.scriptOrder.empty()) {
        // A Sort By ... order; Scripts it does not name keep their place at the end.
        auto rank = [&](CK_ID id) {
            auto found = std::find(m_State.scriptOrder.begin(), m_State.scriptOrder.end(), id);
            return static_cast<size_t>(found - m_State.scriptOrder.begin());
        };
        std::stable_sort(m_State.visibleScripts.begin(), m_State.visibleScripts.end(),
                         [&](CK_ID a, CK_ID b) { return rank(a) < rank(b); });
    }
    if (!focus) return;
    m_State.focus.pending = true;
    m_State.focus.script = script;
    m_State.focus.ref = ref.IsValid() ? ref : ElementRef{ElementKind::Script, script, script};
    m_State.focus.behavior = focusBehavior;
}

void EditorPanels::RefreshStack() {
    if (!m_State.HasDocument()) return;
    const Document &document = *m_State.document;
    // The Script the stack was made for; gone, one still shown; else the first.
    CK_ID anchor = document.FindScript(m_State.stackScript) ? m_State.stackScript : 0;
    for (CK_ID id : m_State.visibleScripts)
        if (!anchor && document.FindScript(id)) anchor = id;
    if (!anchor && !document.Scripts().empty()) anchor = document.Scripts().front().id;
    if (anchor) {
        ShowScript(anchor, ElementRef(), 0, false);
    } else {
        m_State.visibleScripts.clear();
        m_State.stackScript = 0;
    }
}

// ScriptsPanel (Dev's Level Manager): LevelManager.cpp.

void EditorPanels::BuildingBlockItem(size_t index) {
    const BuildingBlockEntry &entry = m_State.catalog.Entries()[index];
    ImGui::PushID(static_cast<int>(index));
    const std::string name = AnsiToUtf8(entry.name);
    // Documentation "Building Blocks Resource": several selected with Shift
    // or Ctrl + click, dragged together; a double click opens the help.
    const bool selected = m_BlockSelection.count(index) != 0;
    if (ImGui::Selectable(name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
        const ImGuiIO &io = ImGui::GetIO();
        auto from = std::find(m_BlockShown.begin(), m_BlockShown.end(), m_BlockAnchor);
        auto to = std::find(m_BlockShown.begin(), m_BlockShown.end(), index);
        if (io.KeyShift && from != m_BlockShown.end() && to != m_BlockShown.end()) {
            // Shift: the rows from the one clicked before.
            if (to < from) std::swap(from, to);
            if (!io.KeyCtrl) m_BlockSelection.clear();
            for (auto it = from; it <= to; ++it) m_BlockSelection.insert(*it);
        } else {
            if (io.KeyCtrl && selected) m_BlockSelection.erase(index);
            else if (io.KeyCtrl) m_BlockSelection.insert(index);
            else m_BlockSelection = {index};
            m_BlockAnchor = index;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) m_Canvas.OpenHtmlHelpOf(entry);
    }
    // Its help (Dev's resources: the help file, 1121) with F1 or the menu.
    if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        // This item handles F1 before the application's general reference
        // shortcut runs at the end of the frame.
        ImGui::SetKeyOwner(ImGuiKey_F1, ImGui::GetItemID(), ImGuiInputFlags_LockThisFrame);
        m_Canvas.OpenHtmlHelpOf(entry);
    }
    if (ImGui::BeginPopupContextItem("bbmenu")) {
        if (ImGui::MenuItem("Html Help", "F1", false, HelpAvailable())) m_Canvas.OpenHtmlHelpOf(entry);
        ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropSource()) {
        if (ImGui::GetDragDropPayload() == nullptr || !ImGui::GetDragDropPayload()->IsDataType(BUILDING_BLOCK_PAYLOAD) ||
            m_State.draggedBlocks.empty()) {
            m_State.draggedBlocks.clear();
            if (selected)
                for (size_t each : m_BlockSelection) m_State.draggedBlocks.push_back(m_State.catalog.Entries()[each].guid);
            else
                m_State.draggedBlocks.push_back(entry.guid);
        }
        ImGui::SetDragDropPayload(BUILDING_BLOCK_PAYLOAD, &entry.guid, sizeof(CKGUID));
        if (m_State.draggedBlocks.size() > 1) ImGui::Text("%d Building Blocks", static_cast<int>(m_State.draggedBlocks.size()));
        else ImGui::Text("%s", name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(name.c_str());
        ImGui::TextDisabled("%s", AnsiToUtf8(entry.category).c_str());
        // Dev's columns (Dev.exe sub_55E890): Apply to, T, Version, Author.
        if (!entry.compatibleClassName.empty())
            ImGui::TextDisabled("Apply to: %s%s", AnsiToUtf8(entry.compatibleClassName).c_str(),
                                entry.targetable ? "   (T: can use a target)" : "");
        ImGui::TextDisabled("Version: 0x%08X   Author: %s", static_cast<unsigned>(entry.version), AnsiToUtf8(entry.author).c_str());
        if (HelpAvailable()) ImGui::TextDisabled("F1: Html Help");
        if (!entry.description.empty()) {
            ImGui::Separator();
            ImGui::PushTextWrapPos(420.0f);
            ImGui::TextUnformatted(AnsiToUtf8(entry.description).c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTooltip();
    }
    ImGui::PopID();
}

void EditorPanels::DrawCategory(const std::string &name, const std::string &path, const BuildingBlockCategory &node) {
    const std::string label = AnsiToUtf8(name);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (path == m_BlockCategory) flags |= ImGuiTreeNodeFlags_Selected;
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_BlockCategory = path;
    if (!open) return;
    for (const auto &child : node.children) DrawCategory(child.first, path + "/" + child.first, child.second);
    ImGui::TreePop();
}

void EditorPanels::BuildingBlocksPanel(bool *open) {
    if (!BeginDevWindow("Building Blocks", open)) {
        ImGui::End();
        return;
    }
    const auto &entries = m_State.catalog.Entries();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter Building Blocks", &m_BlockFilter);
    ImGui::TextDisabled("%zu prototypes - drag into the Schematic", entries.size());
    ImGui::Separator();
    // Documentation "Building Blocks Resource": the tree of categories in
    // the left pane, the columns Behavior Name, Apply to, T, Description
    // (and Version, Author: Dev.exe sub_55E890).
    const float treeWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x * 0.3f);
    ImGui::BeginChild("categories", ImVec2(treeWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    for (const auto &child : m_State.catalog.CategoryTree().children) DrawCategory(child.first, child.first, child.second);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("blocks", ImVec2(0, 0), ImGuiChildFlags_Borders);
    // The rows: those the filter finds (name or category), else the
    // category selected's and its sub-categories'.
    m_BlockShown.clear();
    if (!m_BlockFilter.empty()) {
        const std::string filter = LowerCopy(m_BlockFilter);
        for (size_t i = 0; i < entries.size(); ++i)
            if (LowerCopy(AnsiToUtf8(entries[i].name)).find(filter) != std::string::npos ||
                LowerCopy(AnsiToUtf8(entries[i].category)).find(filter) != std::string::npos)
                m_BlockShown.push_back(i);
    } else if (!m_BlockCategory.empty()) {
        const BuildingBlockCategory *node = &m_State.catalog.CategoryTree();
        size_t start = 0;
        while (node && start <= m_BlockCategory.size()) {
            const size_t slash = m_BlockCategory.find('/', start);
            const std::string part = m_BlockCategory.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            auto found = node->children.find(part);
            node = found != node->children.end() ? &found->second : nullptr;
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        std::function<void(const BuildingBlockCategory &)> collect = [&](const BuildingBlockCategory &each) {
            m_BlockShown.insert(m_BlockShown.end(), each.entries.begin(), each.entries.end());
            for (const auto &child : each.children) collect(child.second);
        };
        if (node) collect(*node);
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("blocklist", 6, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Behavior Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 2.0f);
        ImGui::TableSetupColumn("Apply to", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("T", ImGuiTableColumnFlags_WidthFixed, 16.0f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 4.0f);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableHeadersRow();
        // Sorted by the column clicked (the name at first).
        if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs())
            if (specs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs &sort = specs->Specs[0];
                auto key = [&](size_t i) -> std::string {
                    const BuildingBlockEntry &e = entries[i];
                    switch (sort.ColumnIndex) {
                    case 1: return LowerCopy(e.compatibleClassName);
                    case 2: return e.targetable ? "1" : "0";
                    case 3: return LowerCopy(e.description);
                    case 4: {
                        char version[16];
                        std::snprintf(version, sizeof(version), "%08X", static_cast<unsigned>(e.version));
                        return std::string(version);
                    }
                    case 5: return LowerCopy(e.author);
                    default: return LowerCopy(e.name);
                    }
                };
                std::stable_sort(m_BlockShown.begin(), m_BlockShown.end(), [&](size_t a, size_t b) {
                    return sort.SortDirection == ImGuiSortDirection_Descending ? key(b) < key(a) : key(a) < key(b);
                });
            }
        // Every row submitted (no clipper): a row dragged keeps its drag source.
        for (size_t row = 0; row < m_BlockShown.size(); ++row) {
            const size_t index = m_BlockShown[row];
            const BuildingBlockEntry &entry = entries[index];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            BuildingBlockItem(index);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(AnsiToUtf8(entry.compatibleClassName).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.targetable ? "T" : "");
            ImGui::TableNextColumn();
            const std::string description = AnsiToUtf8(entry.description);
            ImGui::TextUnformatted(description.substr(0, description.find('\n')).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", static_cast<unsigned>(entry.version));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(AnsiToUtf8(entry.author).c_str());
        }
        ImGui::EndTable();
    }
    if (m_BlockShown.empty())
        ImGui::TextDisabled(m_BlockFilter.empty() ? "Select a category" : "No Building Block found");
    ImGui::EndChild();
    ImGui::End();
}

void EditorPanels::RunSearch() {
    if (!m_State.HasDocument()) {
        m_Doc.results.clear();
        return;
    }
    SearchQuery query;
    query.listAll = true;
    query.text = m_SearchText;
    query.id = static_cast<CK_ID>(std::max(0, std::atoi(m_SearchIdText.c_str())));
    query.kinds = m_SearchKinds;
    query.onlyScripts = !m_SearchAllScripts;
    query.scripts = m_State.visibleScripts;
    query.filter = static_cast<SearchFilter>(m_SearchFilter);
    query.type = m_SearchType;
    query.derived = m_SearchDerived;
    if (CKParameter *value = ExplorerValue(false)) query.value = value->GetID();
    // The same query on the same composition keeps the results (and the rows chosen).
    const Document &document = *m_State.document;
    std::string key = query.text + '\x1f' + m_SearchIdText + '\x1f' + std::to_string(query.kinds) + '\x1f' +
                      std::to_string(query.onlyScripts) + std::to_string(m_SearchFilter) + std::to_string(query.type) +
                      std::to_string(query.derived) + '\x1f' + std::to_string(document.TopologyRevision()) + '\x1f' +
                      std::to_string(document.ScriptsRevision());
    if (query.onlyScripts)
        for (CK_ID script : query.scripts) key += ',' + std::to_string(script);
    if (m_SearchFilter == 2)
        if (CKParameter *value = ExplorerValue(false)) key += '\x1f' + ParameterText(value);
    if (key == m_Doc.resultsKey) return;
    m_Doc.resultsKey = key;
    m_Doc.results = SearchDocument(*m_State.document, query);
    m_Doc.currentResult = -1;
    m_Doc.chosen.clear();
    m_Doc.anchor = -1;
}

// The Parameter Filter's value (Edit Value) or replace value (Edit Replace
// Value): parameters of the Type chosen the Explorer keeps (dynamic, not
// saved; Dev.exe sub_5B95A0 sets their type).
CKParameter *EditorPanels::ExplorerValue(bool replace) {
    if (!m_State.HasDocument()) return nullptr;
    CKContext *context = m_State.document->Context();
    CKParameterManager *manager = context->GetParameterManager();
    if (!manager) return nullptr;
    if (m_SearchType < 0) m_SearchType = manager->ParameterGuidToType(CKPGUID_FLOAT);
    CK_ID &id = replace ? m_Doc.replaceValue : m_Doc.filterValue;
    CKParameter *parameter = CKParameter::Cast(context->GetObject(id));
    if (!parameter) {
        parameter = context->CreateCKParameterOut(const_cast<CKSTRING>(replace ? "Replace Value" : "Filter Value"),
                                                  manager->ParameterTypeToGuid(m_SearchType), TRUE);
        id = parameter ? parameter->GetID() : 0;
    }
    if (parameter && parameter->GetType() != m_SearchType) parameter->SetType(m_SearchType);
    return parameter;
}

// The rows chosen: the Schematic's selection (Dev.exe sub_5B6B40), those of
// the Script of the row clicked last (a selection is in one Script).
void EditorPanels::ExplorerSelect() {
    if (m_Doc.anchor < 0 || m_Doc.anchor >= static_cast<int>(m_Doc.results.size())) return;
    const CK_ID script = m_Doc.results[m_Doc.anchor].script;
    m_State.selection.Clear();
    for (int row : m_Doc.chosen) {
        const SearchResult &result = m_Doc.results[row];
        if (result.script == script && result.ref.kind != ElementKind::Script) m_State.selection.Add(script, result.ref);
    }
    if (m_State.selection.Empty()) m_State.selection.script = script;
}

// Dev's tree view (sub_5B79E0): the Scripts and their behaviors; a click
// shows one.
void EditorPanels::ExplorerTree(const ImVec2 &min, const ImVec2 &max) {
    ImGui::SetCursorScreenPos(min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vi::Color(7));
    ImGui::PushStyleColor(ImGuiCol_Text, vi::Color(0));
    ImGui::BeginChild("##tree", ImVec2(max.x - min.x, max.y - min.y), ImGuiChildFlags_Borders);
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
    std::function<void(CK_ID, CKBehavior *)> node = [&](CK_ID script, CKBehavior *behavior) {
        if (!behavior) return;
        ImGui::PushID(static_cast<int>(behavior->GetID()));
        const bool leaf = behavior->GetSubBehaviorCount() == 0;
        const bool chosen = m_Doc.treeBehavior == behavior->GetID();
        const bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_SpanAvailWidth | (leaf ? ImGuiTreeNodeFlags_Leaf : 0) |
                                                          (chosen ? ImGuiTreeNodeFlags_Selected : 0),
                                            "%s", AnsiToUtf8(behavior->GetName() ? behavior->GetName() : "").c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_Doc.treeScript = script;
            m_Doc.treeBehavior = behavior->GetID();
            CKBehavior *parent = behavior->GetParent();
            const bool isScript = behavior->GetID() == script;
            ShowScript(script, isScript ? ElementRef() : ElementRef{ElementKind::Behavior, parent ? parent->GetID() : 0, behavior->GetID()},
                       parent ? parent->GetID() : 0);
        }
        if (open) {
            for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) node(script, behavior->GetSubBehavior(i));
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    if (context)
        for (const ScriptInfo &info : m_State.document->Scripts())
            if (m_SearchAllScripts ||
                std::find(m_State.visibleScripts.begin(), m_State.visibleScripts.end(), info.id) != m_State.visibleScripts.end())
                node(info.id, CKBehavior::Cast(context->GetObject(info.id)));
    // F2: the node chosen renamed.
    CKObject *chosenNode = context ? context->GetObject(m_Doc.treeBehavior) : nullptr;
    if (chosenNode && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        m_ExplorerRename = -2;
        m_ExplorerRenameText = AnsiToUtf8(chosenNode->GetName() ? chosenNode->GetName() : "");
        ImGui::OpenPopup("Rename###explorertreerename");
    }
    if (ImGui::BeginPopupModal("Rename###explorertreerename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool entered = ImGui::InputText("##name", &m_ExplorerRenameText, ImGuiInputTextFlags_EnterReturnsTrue);
        if ((entered || ImGui::Button("OK", ImVec2(80, 0))) && chosenNode) {
            Report(RenameObject(*m_State.document, m_Doc.treeScript, m_Doc.treeBehavior, Utf8ToAnsi(m_ExplorerRenameText)),
                   "Rename");
            m_Doc.resultsKey.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
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
void EditorPanels::ExplorerPanel(bool *open) {
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 units(345.0f, 279.0f);
    constexpr float PAD = 4.5f;
    ImVec2 o;
    Preferences &prefs = m_State.preferences;
    if (!m_ExplorerShown) {
        m_ExplorerShown = true;
        m_Doc.selectionBefore = m_State.selection;
        // The settings it had when it closed last.
        m_SearchKinds = static_cast<unsigned>(prefs.Int("Schematic Explorer\\Show In List"));
        if (!m_SearchKinds) m_SearchKinds = SearchScripts;
        m_SearchAllScripts = prefs.Bool("Schematic Explorer\\Parse All Items");
        m_SearchTree = prefs.Bool("Schematic Explorer\\Tree View");
        m_SearchFilter = std::max(0, std::min(2, prefs.Int("Schematic Explorer\\Parameter Filter")));
        m_SearchDerived = prefs.Bool("Schematic Explorer\\Derived");
    }
    auto close = [&](bool keep) {
        if (!keep) m_State.selection = m_Doc.selectionBefore;
        prefs.SetInt("Schematic Explorer\\Show In List", static_cast<int>(m_SearchKinds));
        prefs.SetBool("Schematic Explorer\\Parse All Items", m_SearchAllScripts);
        prefs.SetBool("Schematic Explorer\\Tree View", m_SearchTree);
        prefs.SetInt("Schematic Explorer\\Parameter Filter", m_SearchFilter);
        prefs.SetBool("Schematic Explorer\\Derived", m_SearchDerived);
        m_ExplorerShown = false;
        *open = false;
    };
    if (!vi::BeginWindow("Schematic Explorer", "Schematic Explorer", vi::TemplateClient(units, s, PAD), s, o)) {
        vi::EndWindow();
        return;
    }
    const vi::TemplateLayout l = vi::LayoutTemplate(o, units, s, PAD);
    vi::TemplatePanel(l);
    RunSearch();
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
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
                if (c == 0) return result.label;
                if (c == 2) return result.kind;
                const ScriptInfo *script = m_State.document->FindScript(result.script);
                return script ? AnsiToUtf8(script->name) : std::string();
            },
            [&](int r) { return m_Doc.chosen.count(r) != 0; }, &twice, nullptr, false, nullptr,
            // Documentation "Schematic Explorer": "Icons in front of an item is
            // the element type" (Dev's class icons).
            [&](int r) -> ImTextureID {
                CKObject *object = context && m_Doc.results[r].object ? context->GetObject(m_Doc.results[r].object) : nullptr;
                return object ? DevIconImage(DevClassIconId(object->GetClassID())) : ImTextureID();
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
                ShowScript(result.script, result.ref, result.focus);
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
                ShowScript(result.script, result.ref, result.focus);
                m_Canvas.EditElement(result.script, result.ref);
                m_State.events.Post(FocusSchematic{});
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_F2, false) && first >= 0 && first < count && m_Doc.results[first].object &&
            m_Doc.results[first].ref.kind != ElementKind::Comment) {
            m_ExplorerRename = first;
            CKObject *object = context ? context->GetObject(m_Doc.results[first].object) : nullptr;
            m_ExplorerRenameText = AnsiToUtf8(object && object->GetName() ? object->GetName() : "");
            ImGui::OpenPopup("Rename###explorerrename");
        }
        if (ImGui::BeginPopupModal("Rename###explorerrename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool entered = ImGui::InputText("##name", &m_ExplorerRenameText, ImGuiInputTextFlags_EnterReturnsTrue);
            if ((entered || ImGui::Button("OK", ImVec2(80, 0))) && m_ExplorerRename >= 0 && m_ExplorerRename < count) {
                const SearchResult &result = m_Doc.results[m_ExplorerRename];
                Report(RenameObject(*m_State.document, result.script, result.object, Utf8ToAnsi(m_ExplorerRenameText)), "Rename");
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
    text(237, 137, 60, 10, "Filter CK_ID");
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
        m_State.selection.Clear();
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
    CKBehavior *treeNode = m_State.HasDocument() ? CKBehavior::Cast(m_State.document->Context()->GetObject(m_Doc.treeBehavior)) : nullptr;
    if (vi::Button("GoTo", l.Min(53, 239), l.Max(53, 239, 46, 10), "GoTo", listing ? !m_Doc.chosen.empty() : treeNode != nullptr)) {
        if (listing) {
            const SearchResult &result = m_Doc.results[*m_Doc.chosen.begin()];
            m_Doc.currentResult = *m_Doc.chosen.begin();
            ShowScript(result.script, result.ref, result.focus);
            ExplorerSelect();
        } else if (treeNode) {
            CKBehavior *parent = treeNode->GetParent();
            const bool isScript = treeNode->GetID() == m_Doc.treeScript;
            ShowScript(m_Doc.treeScript,
                       isScript ? ElementRef() : ElementRef{ElementKind::Behavior, parent ? parent->GetID() : 0, treeNode->GetID()},
                       parent ? parent->GetID() : 0);
        }
    }
    // Delete: the rows chosen (the tree's node), each Script's elements one
    // command, Scripts with them; one undo step.
    if (vi::Button("Delete", l.Min(53, 251), l.Max(53, 251, 46, 10), "Delete", listing ? !m_Doc.chosen.empty() : treeNode != nullptr)) {
        std::map<CK_ID, std::vector<ElementRef>> elements;
        std::vector<CK_ID> scripts;
        if (listing) {
            for (int r : m_Doc.chosen) {
                if (m_Doc.results[r].ref.kind == ElementKind::Script) scripts.push_back(m_Doc.results[r].script);
                else elements[m_Doc.results[r].script].push_back(m_Doc.results[r].ref);
            }
        } else if (treeNode) {
            CKBehavior *parent = treeNode->GetParent();
            if (treeNode->GetID() == m_Doc.treeScript) scripts.push_back(m_Doc.treeScript);
            else elements[m_Doc.treeScript].push_back(ElementRef{ElementKind::Behavior, parent ? parent->GetID() : 0, treeNode->GetID()});
        }
        EditGroup group(*m_State.document, "Delete");
        for (const auto &entry : elements) {
            if (std::find(scripts.begin(), scripts.end(), entry.first) != scripts.end()) continue;
            const EditResult result = DeleteElements(*m_State.document, entry.first, entry.second);
            if (!result.ok && !result.Unchanged()) m_State.Log(LogLevel::Warning, "Delete: " + AnsiToUtf8(result.message));
        }
        for (CK_ID script : scripts) Report(DeleteScript(*m_State.document, script), "Delete Script");
        m_State.selection.Clear();
        m_Doc.chosen.clear();
        m_Doc.treeBehavior = m_Doc.treeScript = 0;
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
    CKParameter *value = ExplorerValue(false);
    CKParameter *replace = ExplorerValue(true);
    if (manager) {
        const char *typeName = m_SearchType >= 0 ? manager->ParameterTypeToName(m_SearchType) : nullptr;
        if (!typed) ImGui::BeginDisabled();
        std::vector<int> types;
        const int chosenType = vi::Combo(
            "##type", l.Min(237, 236), l.Max(237, 236, 105, 12), typeName ? AnsiToUtf8(typeName) : std::string(),
            [&] {
                std::vector<std::string> names;
                for (int t = 0; t < manager->GetParameterTypesCount(); ++t) {
                    const char *name = manager->IsParameterTypeToBeShown(t) ? manager->ParameterTypeToName(t) : nullptr;
                    if (name && *name) names.push_back(AnsiToUtf8(name));
                }
                return names;
            },
            -1);
        if (chosenType >= 0)
            for (int t = 0, shown = 0; t < manager->GetParameterTypesCount(); ++t) {
                const char *name = manager->IsParameterTypeToBeShown(t) ? manager->ParameterTypeToName(t) : nullptr;
                if (!name || !*name) continue;
                if (shown++ == chosenType) {
                    m_SearchType = t;
                    value = ExplorerValue(false);
                    replace = ExplorerValue(true);
                }
            }
        if (!typed) ImGui::EndDisabled();
    }
    vi::CheckButton("Derived", l.Min(290, 251), l.Max(290, 251, 52, 10), m_SearchDerived, "Derived", typed);
    if (vi::Button("editvalue", l.Min(115, 251), l.Max(115, 251, 75, 10), "Edit Value", valued && value)) {
        m_ExplorerBox.Clear();
        if (m_ExplorerBox.Add(value, "Filter Value")) {
            m_ExplorerEditReplace = false;
            m_OpenExplorerValue = true;
        }
    }
    text(196, 251, 85, 10, value && valued ? AnsiToUtf8(ParameterText(value)).c_str() : "");
    if (vi::Button("editreplace", l.Min(115, 264), l.Max(115, 264, 75, 10), "Edit Replace Value", valued && replace)) {
        m_ExplorerBox.Clear();
        if (m_ExplorerBox.Add(replace, "Replace Value")) {
            m_ExplorerEditReplace = true;
            m_OpenExplorerValue = true;
        }
    }
    text(196, 264, 85, 10, replace && valued ? AnsiToUtf8(ParameterText(replace)).c_str() : "");
    // Replace Value (Dev.exe sub_5B9600): every parameter listed takes the
    // replace value, which the filter then looks for.
    if (vi::Button("replacevalue", l.Min(285, 264), l.Max(285, 264, 57, 10), "Replace Value", valued && replace && value)) {
        std::vector<std::pair<CK_ID, CK_ID>> parameters;
        for (const SearchResult &result : m_Doc.results)
            if (result.object) parameters.emplace_back(result.script, result.object);
        const EditResult result = ReplaceParameterValues(*m_State.document, parameters, replace->GetID());
        if (!result.ok && !result.Unchanged()) m_State.Log(LogLevel::Warning, "Replace Value: " + AnsiToUtf8(result.message));
        value->CopyValue(replace, FALSE);
    }
    // Edit Value / Edit Replace Value: Dev's Edit Parameter on the value.
    const char *dialog = "Edit Parameter###ExplorerValue";
    if (m_OpenExplorerValue) {
        ImGui::OpenPopup(dialog);
        m_OpenExplorerValue = false;
    }
    const char *typeName = manager && m_SearchType >= 0 ? manager->ParameterTypeToName(m_SearchType) : nullptr;
    const DialogAction action = EditParameterValueDialog(dialog, "Edit Parameter", m_ExplorerBox, typeName);
    if (action == DialogAction::Ok)
        if (CKParameter *target = m_ExplorerEditReplace ? replace : value)
            if (CKParameter *work = m_ExplorerBox.Work(0)) target->CopyValue(work, FALSE);
    if (action != DialogAction::None) m_ExplorerBox.Clear();

    if (vi::DialogButton("Select", ImVec2(l.right - 130.0f * s, l.buttonsY), s, ImGuiKey_Enter)) close(true);
    else if (vi::DialogButton("Cancel", ImVec2(l.right - 60.0f * s, l.buttonsY), s, ImGuiKey_Escape)) close(false);
    vi::EndWindow();
}

// Dev's Event Log (dialog 572, Dev.exe sub_59BEB0, sub_59C350, sub_59C570):
// a floating window made with the status bar, its bottom-right corner on the
// status bar's top-right one (sub_4229D0); 354 x 286 dialog units, resizable;
// the text of the lines over its whole client but the buttons Clear (empties
// it, "Event Log Cleared") and Close.
void EditorPanels::LogPanel(bool *open) {
    const float s = ImGui::GetFontSize() / 13.0f;
    const float title = vi::TitleHeight(s);
    if (!m_LogPlaced) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y),
                                ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(354.0f * 1.5f * s + 2.0f, 286.0f * 1.625f * s + title + 2.0f), ImGuiCond_Always);
        // The status bar takes its room from the work area a frame after it shows.
        m_LogPlaced = ImGui::GetFrameCount() > 2;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f * s, title + 80.0f * s), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, vi::Color(4));
    ImGui::PushStyleColor(ImGuiCol_Border, vi::Color(0));
    const bool visible = ImGui::Begin("Event Log", nullptr,
                                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                          ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!visible) {
        ImGui::End();
        return;
    }
    // The dark title bar, as Dev's dialogs; moved by it.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    dl->AddRectFilled(ImVec2(at.x + 1.0f, at.y + 1.0f), ImVec2(at.x + size.x - 1.0f, at.y + 1.0f + title), vi::Color(2),
                      6.0f * s, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(at.x + 1.0f, at.y + 1.0f + title), ImVec2(at.x + size.x - 1.0f, at.y + 1.0f + title),
                IM_COL32(188, 188, 188, 255));
    vi::Text(ImVec2(at.x + 9.0f * s, at.y + 1.0f + (title - ImGui::GetFontSize()) * 0.5f),
             ImVec2(at.x + size.x - 6.0f * s, at.y + title), "Event Log", true, vi::LEFT, vi::Color(9));
    ImGui::SetCursorScreenPos(at);
    ImGui::InvisibleButton("##caption", ImVec2(size.x, title + 1.0f));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(at.x + delta.x, at.y + delta.y));
    }
    // The text (a read-only VIEdit, one line per entry), the buttons below.
    const ImVec2 client(at.x + 1.0f, at.y + 2.0f + title);
    const ImVec2 clientMax(at.x + size.x - 1.0f, at.y + size.y - 1.0f);
    const float buttons = clientMax.y - 25.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(client.x + 1.0f, client.y + 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vi::Color(7));
    ImGui::PushStyleColor(ImGuiCol_Text, vi::Color(0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f * s, 2.0f * s));
    ImGui::BeginChild("lines", ImVec2(clientMax.x - client.x - 2.0f, buttons - client.y - 3.0f * s),
                      ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_State.log.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const LogEntry &entry = m_State.log[i];
            if (entry.repeats > 1) ImGui::Text("%s  (x%d)", entry.text.c_str(), entry.repeats);
            else ImGui::TextUnformatted(entry.text.c_str());
        }
    }
    if (m_LastLogSize != m_State.log.size()) ImGui::SetScrollHereY(1.0f);
    m_LastLogSize = m_State.log.size();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const float by = clientMax.y - 22.0f * s;
    if (vi::DialogButton("Close", ImVec2(clientMax.x - 70.0f * s, by), s, focused ? ImGuiKey_Escape : ImGuiKey_None))
        *open = false;
    if (vi::DialogButton("Clear", ImVec2(clientMax.x - 140.0f * s, by), s)) {
        m_State.log.clear();
        m_State.Log(LogLevel::Info, "Event Log Cleared");
    }
    ImGui::End();
}

} // namespace vdev

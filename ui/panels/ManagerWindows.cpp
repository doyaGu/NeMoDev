// Dev's Attribute Manager, Add Attribute dialog and Hierarchy Manager;
// layout after the Dev 2.5 screenshots the user gave.

#include "ui/panels/ManagerWindows.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>

#include "imgui.h"
#include "imgui_internal.h"

#include "CKAll.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/AttributeQueries.h"
#include "core/level/ObjectQueries.h"
#include "core/catalog/ParameterCatalog.h"
#include "ui/controls/TypeAhead.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {


void ManagerWindows::DocumentClosed() {
    m_ValueBox.Clear();
    if (m_State.workspace) m_State.workspace->CloseOwner(m_NameOwner);
    m_Doc = DocumentState();
    m_Selected = -1;
    m_MoreTypes.clear();
    m_Chosen.clear();
    m_TypeCategory.clear();
    m_RenameType = -1;
    m_RenameCategory.clear();
    m_DeleteCategory.clear();
    m_OpenValue = m_ConfirmRemoveUser = false;
    m_HierarchyPickRows.clear();
    m_HierarchyPickFrame = -1;
#ifdef VIRTOOLSDEV_UI_TESTS
    m_HierarchyShownRows.clear();
#endif
}

void ManagerWindows::Draw(ImGuiID dock, bool openDialog) {
    m_Dock = dock;
    if (!m_State.HasDocument()) {
        m_Doc.openAddAttribute = false;
        m_Selected = -1;
        m_Doc.hierarchySelection.clear();
        m_Doc.hierarchyOpen.clear();
        m_Doc.hierarchyStarted = false;
        return;
    }
    if (m_State.showAttributeManager) AttributeManager(&m_State.showAttributeManager);
    if (m_State.showHierarchy) HierarchyManager(&m_State.showHierarchy);
    if (!m_State.showHierarchy && m_NameOwner && m_State.workspace) {
        m_State.workspace->CloseOwner(m_NameOwner);
        m_Doc.hierarchyRename = 0;
    }
    if (openDialog || !m_Doc.openAddAttribute) AddAttributeDialog();
}

// ---------------------------------------------------------------- Add Attribute

// The category tree: categories open, attributes selectable (Ctrl adds to
// the selection when multiple).
void ManagerWindows::AttributeTree(const char *id, const std::vector<CK_ID> &targets, bool multiple, float height) {
    const auto catalog = ReadAttributeCatalog(*m_State.document, targets);
    if (!catalog.available) return;
    if (ImGui::BeginChild(id, ImVec2(300, height), ImGuiChildFlags_Borders)) {
        for (const auto &category : catalog.categories) {
            if (category.types.empty() && !targets.empty()) continue;
            const std::string label = category.name.empty() ? std::string("(No category)") : AnsiToUtf8(category.name);
            if (!ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) continue;
            for (CKAttributeType type : category.types) {
                const auto &attribute = *catalog.Find(type);
                const auto &name = attribute.name;
                ImGui::PushID(type);
                const bool chosen = m_Chosen.count(type) != 0;
                if (ImGui::Selectable(AnsiToUtf8(name).c_str(), chosen, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (!multiple || !ImGui::GetIO().KeyCtrl) m_Chosen.clear();
                    if (chosen && multiple && ImGui::GetIO().KeyCtrl) m_Chosen.erase(type);
                    else m_Chosen.insert(type);
                }
                if (ImGui::IsItemHovered()) {
                    const auto parameter = AnsiToUtf8(attribute.parameterName);
                    const auto cls = AnsiToUtf8(attribute.compatibleName);
                    ImGui::SetTooltip("Type: %s\nApply to: %s", parameter.empty() ? "None" : parameter.c_str(), cls.c_str());
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
}

// Dev "More>>": Create New Category / Create New Attribute Type.
void ManagerWindows::CreatePanel() {
    const auto catalog = ReadAttributeCatalog(*m_State.document);
    if (!catalog.available) return;
    auto types = ListParameterTypes(*m_State.document);
    std::stable_sort(types.begin(), types.end(), [](const auto &a, const auto &b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Create New Category");
    ImGui::Separator();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("##categoryname", m_CategoryName, sizeof(m_CategoryName));
    ImGui::SameLine();
    if (ImGui::Button("Create##category")) {
        const EditResult result = CreateAttributeCategory(*m_State.document, Utf8ToAnsi(m_CategoryName));
        if (result.ok) {
            m_State.Log(LogLevel::Info, std::string("Created attribute category ") + m_CategoryName);
            m_TypeCategory = Utf8ToAnsi(m_CategoryName);
            m_CategoryName[0] = 0;
        } else {
            m_State.Log(LogLevel::Warning, "Create New Category: " + AnsiToUtf8(result.message));
        }
    }
    ImGui::Dummy(ImVec2(1, 10));
    ImGui::TextUnformatted("Create New Attribute Type");
    ImGui::Separator();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("##typename", m_TypeName, sizeof(m_TypeName));
    ImGui::TextUnformatted("Parameter Type");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(240);
    const auto selected = std::find_if(types.begin(), types.end(), [&](const auto &type) { return type.guid == m_TypeParameter; });
    if (selected == types.end()) m_TypeParameter = CKGUID(0, 0);
    const auto shown = selected == types.end() ? std::string("None") : AnsiToUtf8(selected->name);
    if (ImGui::BeginCombo("##typeparameter", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        TypeAhead typeAhead;
        if (typeAhead.Selectable("None", m_TypeParameter == CKGUID(0, 0))) m_TypeParameter = CKGUID(0, 0);
        for (const auto &type : types) {
            if (typeAhead.Selectable(AnsiToUtf8(type.name).c_str(), m_TypeParameter == type.guid)) m_TypeParameter = type.guid;
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted("Category");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(240);
    if (ImGui::BeginCombo("##typecategory", m_TypeCategory.empty() ? "(No category)" : AnsiToUtf8(m_TypeCategory).c_str())) {
        TypeAhead typeAhead;
        if (typeAhead.Selectable("(No category)", m_TypeCategory.empty())) m_TypeCategory.clear();
        for (const auto &category : catalog.categories) {
            if (category.name.empty()) continue;
            if (typeAhead.Selectable(AnsiToUtf8(category.name).c_str(), m_TypeCategory == category.name)) m_TypeCategory = category.name;
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Create##type", ImVec2(80, 0))) {
        const EditResult result = CreateAttributeType(*m_State.document, Utf8ToAnsi(m_TypeName),
                                                      m_TypeParameter, m_TypeCategory);
        if (result.ok) {
            m_State.Log(LogLevel::Info, std::string("Created attribute type ") + m_TypeName);
            m_Chosen = {static_cast<CKAttributeType>(result.created)};
            m_Selected = static_cast<CKAttributeType>(result.created);
            m_TypeName[0] = 0;
        } else {
            m_State.Log(LogLevel::Warning, "Create New Attribute Type: " + AnsiToUtf8(result.message));
        }
    }
    ImGui::EndGroup();
}

void ManagerWindows::AddAttributeDialog() {
    if (m_Doc.openAddAttribute) {
        m_Doc.openAddAttribute = false;
        m_Chosen.clear();
        m_More = m_Doc.addAttributeMore;
        m_Doc.addAttributeMore = false;
        ImGui::OpenPopup("Add Attribute");
    }
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Add Attribute", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const std::vector<CK_ID> &targets = m_Doc.addAttributeTargets;
    ImGui::BeginGroup();
    AttributeTree("tree", targets, true, 360);
    ImGui::BeginDisabled(targets.empty() || m_Chosen.empty());
    if (ImGui::Button("Add Selected", ImVec2(120, 0))) {
        int added = 0;
        const auto catalog = ReadAttributeCatalog(*m_State.document);
        for (CKAttributeType type : m_Chosen) {
            std::vector<std::pair<std::string, bool>> each;
            const EditResult result = AddAttribute(*m_State.document, targets, type, &each);
            const auto *attribute = catalog.Find(type);
            m_State.LogAttributeAdds(attribute ? attribute->name.c_str() : "", each, result.ok);
            if (result.ok) ++added;
            else if (each.empty()) m_State.Log(LogLevel::Warning, "Add Attribute: " + AnsiToUtf8(result.message));
        }
        if (added) {
            m_State.Log(LogLevel::Info, "Added " + std::to_string(added) + " attribute(s)");
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ImGui::Button(m_More ? "<< Less" : "More >>", ImVec2(80, 0))) m_More = !m_More;
    ImGui::EndGroup();
    if (m_More) {
        ImGui::SameLine();
        CreatePanel();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- Attribute Manager

// Dev's Attribute Manager (Dev.exe sub_55AE50): the toolbar 21 high (Create
// Attribute, Copy Selected Item Value, Paste Value On Selected Items,
// Remove All User Attributes, the close button at the right), a free strip
// 6 wide at the left, then the attribute types and, right of a split bar 2
// wide, the objects having the selected one.
void ManagerWindows::AttributeManager(bool *open) {
    if (m_Dock) ImGui::SetNextWindowDockID(m_Dock, ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = BeginDevWindow("Attribute Manager", open);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }
    DevTabClose(open);
    const auto catalog = ReadAttributeCatalog(*m_State.document);
    if (!catalog.available) {
        ImGui::End();
        return;
    }
    const auto liveRows = FilterObjects(*m_State.document, {m_Doc.attributeRows.begin(), m_Doc.attributeRows.end()}, CKCID_BEOBJECT);
    if (liveRows.size() != m_Doc.attributeRows.size()) m_Doc.attributeRows = {liveRows.begin(), liveRows.end()};
    if (m_Selected >= 0 && !catalog.Find(m_Selected)) m_Selected = -1;
    for (auto it = m_MoreTypes.begin(); it != m_MoreTypes.end();)
        it = catalog.Find(*it) && *it != m_Selected ? std::next(it) : m_MoreTypes.erase(it);
    if (m_Selected < 0) m_MoreTypes.clear();
    // The types whose objects are listed: the one clicked last first.
    std::vector<CKAttributeType> listed;
    if (m_Selected >= 0) listed.push_back(m_Selected);
    listed.insert(listed.end(), m_MoreTypes.begin(), m_MoreTypes.end());
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    auto at = [&](float x, float y) { return ImVec2(IM_ROUND(origin.x + x * s), IM_ROUND(origin.y + y * s)); };

    // Copy: the selected rows' attribute parameters (sub_55C450); Paste: the
    // one copied on the selected rows' (sub_55C4B0, its messages on the
    // status bar). Ctrl+C / Ctrl+V do the same in the window.
    auto copy = [&] {
        if (m_Doc.attributeRows.empty() || m_Selected < 0) return;
        std::vector<CK_ID> values;
        for (const auto &row : ReadAttributeObjects(*m_State.document, {m_Selected}, {m_Doc.attributeRows.begin(), m_Doc.attributeRows.end()}))
            if (row.parameter && m_Doc.attributeRows.count(row.object)) values.push_back(row.parameter);
        m_State.clipboard.SetParameters(*m_State.document, std::move(values));
    };
    auto paste = [&] {
        if (!m_State.clipboard.BelongsTo(*m_State.document)) return;
        const auto copied = FilterObjects(*m_State.document, m_State.clipboard.parameters, CKCID_PARAMETER, ObjectQueryScope::Existing);
        if (copied.empty()) {
            m_State.Log(LogLevel::Warning, "Nothing to paste!");
            return;
        }
        if (copied.size() > 1) {
            m_State.Log(LogLevel::Warning, "Can't paste multiple values, please select a single parameter!");
            return;
        }
        if (m_Selected < 0 || m_Doc.attributeRows.empty()) return;
        const EditResult result =
            PasteAttributeValue(*m_State.document, copied.front(),
                                std::vector<CK_ID>(m_Doc.attributeRows.begin(), m_Doc.attributeRows.end()), m_Selected);
        if ((!result.ok && !result.Unchanged()) || (result.ok && !result.message.empty()))
            m_State.Log(LogLevel::Warning, AnsiToUtf8(result.message));
    };

    // ------------------------------------------------------------- toolbar
    dl->AddRectFilled(origin, ImVec2(origin.x + available.x, at(0, 21).y), vi::Color(2));
    if (ImTextureID cap = DevBitmap(834)) dl->AddImage(cap, at(0, 0), at(6, 21));
    static const int COPY[5] = {1057, 1058, 1059, 1060, 1057};
    static const int PASTE[5] = {1061, 1062, 1063, 1064, 1061};
    static const int CLOSE[5] = {613, 613, 614, 614, 613};
    if (vi::ToolbarButton("create", at(15, 3), at(95, 18), "Create Attribute", s)) RequestAddAttribute({}, true);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create Attribute");
    if (DevBitmapButton("copy", at(101, 1), COPY, false, true, s, "Copy Selected Item Value")) copy();
    if (DevBitmapButton("paste", at(121, 1), PASTE, false, true, s, "Paste Value On Selected Items")) paste();
    if (vi::ToolbarButton("removeuser", at(141, 3), at(271, 18), "Remove All User Attributes", s))
        m_ConfirmRemoveUser = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove All User Attributes");
    if (DevBitmapButton("close", ImVec2(origin.x + available.x - 25.0f * s, origin.y + 4.0f * s), CLOSE, false, true, s,
                        nullptr))
        *open = false;
    // The free space at the left (ZC 1).
    dl->AddRectFilled(at(0, 21), ImVec2(at(6, 0).x, origin.y + available.y), vi::Color(1));
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copy();
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) paste();
    }
    if (m_ConfirmRemoveUser) {
        ImGui::OpenPopup("Remove All User Attributes");
        m_ConfirmRemoveUser = false;
    }
    if (ImGui::BeginPopupModal("Remove All User Attributes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Dev.exe sub_55D800 (1589); the undo history goes with them here.
        ImGui::TextUnformatted("Are you sure you want to remove all user attributes ?\n"
                               "Undo stack will be cleared.");
        if (ImGui::Button("OK", ImVec2(80, 0))) {
            const EditResult result = RemoveUserAttributes(*m_State.document);
            m_Selected = -1;
            m_State.Log(result.ok ? LogLevel::Info : LogLevel::Warning,
                        result.ok ? std::string("Removed the user attributes") : "Remove All User Attributes: " + AnsiToUtf8(result.message));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // Left: categories and attribute types (Type, Apply to).
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX;
    const float left = at(6, 0).x, top = at(0, 21).y;
    const float split = IM_ROUND(2.0f * s);
    // Dev.exe sub_55AE50: the types' list half the width at first, the split
    // bar dragged (VISplitBar) moves it.
    const float room = origin.x + available.x - left;
    if (m_AttributeSplit <= 0.0f) m_AttributeSplit = IM_ROUND(room * 0.5f);
    m_AttributeSplit = std::clamp(m_AttributeSplit, 40.0f * s, std::max(40.0f * s, room - 40.0f * s));
    const float half = IM_ROUND(m_AttributeSplit);
    ImGui::SetCursorScreenPos(ImVec2(left + half, top));
    ImGui::InvisibleButton("##split", ImVec2(split, std::max(1.0f, origin.y + available.y - top)));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) m_AttributeSplit += ImGui::GetIO().MouseDelta.x;
    dl->AddRectFilled(ImVec2(left + half, top), ImVec2(left + half + split, origin.y + available.y), vi::Color(1));
    ImGui::SetCursorScreenPos(ImVec2(left, top));
    if (ImGui::BeginTable("attributetypes", 3, flags, ImVec2(half, origin.y + available.y - top))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        // Dev's MultiTree columns: 250, 100, 100.
        ImGui::TableSetupColumn("Attributes", ImGuiTableColumnFlags_WidthFixed, 250.0f * s);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f * s);
        ImGui::TableSetupColumn("Apply to", ImGuiTableColumnFlags_WidthFixed, 100.0f * s);
        ImGui::TableHeadersRow();
        // Rename in place: the name typed is taken with Enter or when the
        // edit is left, Escape keeps the name.
        auto renameField = [&](const std::function<EditResult(const std::string &)> &rename) {
            ImGui::SetNextItemWidth(-1);
            if (m_RenameFocus) {
                ImGui::SetKeyboardFocusHere();
                m_RenameFocus = false;
            }
            const bool entered = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            if (entered || (ImGui::IsItemDeactivated() && !escaped)) {
                const EditResult result = rename(Utf8ToAnsi(m_RenameBuffer));
                if (!result.ok && !result.Unchanged()) m_State.Log(LogLevel::Warning, AnsiToUtf8(result.message));
                m_RenameType = -1;
                m_RenameCategory.clear();
            } else if (escaped || (!ImGui::IsItemActive() && !ImGui::IsItemActivated() && !m_RenameFocus &&
                                   ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered())) {
                m_RenameType = -1;
                m_RenameCategory.clear();
            }
        };
        auto startRename = [&](const std::string &text) {
            std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", AnsiToUtf8(text).c_str());
            m_RenameFocus = true;
        };
        for (const auto &category : catalog.categories) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string label = category.name.empty() ? std::string("(No category)") : AnsiToUtf8(category.name);
            const bool renaming = !category.name.empty() && m_RenameCategory == category.name;
            ImGui::PushID(label.c_str());
            const bool openNode =
                ImGui::TreeNodeEx("##category", ImGuiTreeNodeFlags_SpanFullWidth, "%s", renaming ? "" : label.c_str());
            // A category's menu (Dev.exe sub_55D200): Rename and Delete when
            // its flags allow them.
            const CKDWORD categoryFlags = category.flags;
            if (!category.name.empty() && (categoryFlags & (CK_ATTRIBUT_CAN_MODIFY | CK_ATTRIBUT_CAN_DELETE)) &&
                ImGui::BeginPopupContextItem("categorymenu")) {
                if ((categoryFlags & CK_ATTRIBUT_CAN_MODIFY) && ImGui::MenuItem("Rename")) {
                    m_RenameCategory = category.name;
                    startRename(category.name);
                }
                if ((categoryFlags & CK_ATTRIBUT_CAN_DELETE) && ImGui::MenuItem("Delete")) {
                    if (category.types.empty()) {
                        const EditResult result = DeleteAttributeCategory(*m_State.document, category.name);
                        if (!result.ok) m_State.Log(LogLevel::Warning, AnsiToUtf8(result.message));
                    } else {
                        m_DeleteCategory = category.name;
                    }
                }
                ImGui::EndPopup();
            }
            if (renaming) {
                ImGui::SameLine();
                const std::string old = category.name;
                renameField([&](const std::string &name) { return RenameAttributeCategory(*m_State.document, old, name); });
            }
            ImGui::PopID();
            if (!openNode) continue;
            for (CKAttributeType type : category.types) {
                ImGui::PushID(type);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const auto &attribute = *catalog.Find(type);
                const auto &name = attribute.name;
                const CKDWORD typeFlags = attribute.flags;
                const bool user = (typeFlags & CK_ATTRIBUT_USER) != 0;
                if (m_RenameType == type) {
                    renameField([&](const std::string &text) { return RenameAttributeType(*m_State.document, type, text); });
                } else {
                    if (user) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 160, 255));
                    ImGui::TreeNodeEx("##attr", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                                    ImGuiTreeNodeFlags_SpanFullWidth |
                                                    (m_Selected == type || m_MoreTypes.count(type) ? ImGuiTreeNodeFlags_Selected : 0),
                                      "%s", AnsiToUtf8(name).c_str());
                    if (user) ImGui::PopStyleColor();
                    // Dragged onto objects (3D Layout, Level Manager, Setups).
                    if (ImGui::BeginDragDropSource()) {
                        const AttributePayload payload{type, 0};
                        ImGui::SetDragDropPayload(ATTRIBUTE_PAYLOAD, &payload, sizeof(payload));
                        ImGui::TextUnformatted(AnsiToUtf8(name).c_str());
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl && m_Selected >= 0) {
                        // Ctrl + click: one type more (or less).
                        if (type == m_Selected) {
                            m_Selected = m_MoreTypes.empty() ? -1 : *m_MoreTypes.begin();
                            m_MoreTypes.erase(m_Selected);
                        } else if (m_MoreTypes.count(type)) {
                            m_MoreTypes.erase(type);
                        } else {
                            m_MoreTypes.insert(m_Selected);
                            m_Selected = type;
                        }
                        m_Doc.attributeRows.clear();
                    } else if ((ImGui::IsItemClicked() || ImGui::IsItemClicked(ImGuiMouseButton_Right)) &&
                               (m_Selected != type || !m_MoreTypes.empty())) {
                        if (!(ImGui::IsItemClicked(ImGuiMouseButton_Right) && m_MoreTypes.count(type))) {
                            m_Selected = type;
                            m_MoreTypes.clear();
                            m_Doc.attributeRows.clear();
                        }
                    }
                    // A type's menu: Rename, Delete (its flags).
                    if ((typeFlags & (CK_ATTRIBUT_CAN_MODIFY | CK_ATTRIBUT_CAN_DELETE)) && ImGui::BeginPopupContextItem("typemenu")) {
                        if ((typeFlags & CK_ATTRIBUT_CAN_MODIFY) && ImGui::MenuItem("Rename")) {
                            m_RenameType = type;
                            startRename(name);
                        }
                        if ((typeFlags & CK_ATTRIBUT_CAN_DELETE) && ImGui::MenuItem("Delete")) {
                            const EditResult result = DeleteAttributeType(*m_State.document, type);
                            if (!result.ok) m_State.Log(LogLevel::Warning, AnsiToUtf8(result.message));
                            if (m_Selected == type) m_Selected = -1;
                            m_MoreTypes.erase(type);
                        }
                        ImGui::EndPopup();
                    }
                }
                ImGui::TableNextColumn();
                const auto parameter = AnsiToUtf8(attribute.parameterName);
                ImGui::TextUnformatted(parameter.empty() ? "" : parameter.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(AnsiToUtf8(attribute.compatibleName).c_str());
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::EndTable();
    }
    // Delete Category with attributes in it: Dev asks first (sub_55D480).
    if (!m_DeleteCategory.empty()) ImGui::OpenPopup("Delete Category");
    if (ImGui::BeginPopupModal("Delete Category", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This will move every attribute to -No Category- !");
        if (ImGui::Button("OK", ImVec2(80, 0))) {
            const EditResult result = DeleteAttributeCategory(*m_State.document, m_DeleteCategory);
            if (!result.ok) m_State.Log(LogLevel::Warning, AnsiToUtf8(result.message));
            m_DeleteCategory.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            m_DeleteCategory.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // The objects' commands (Dev.exe sub_55D080's menu, sub_55C2F0's keys).
    std::vector<CK_ID> chosen(m_Doc.attributeRows.begin(), m_Doc.attributeRows.end());
    auto removeFromObjects = [&] {
        if (m_Selected < 0 || chosen.empty()) return;
        // Each type listed taken off the objects chosen (those that have it).
        EditGroup group(*m_State.document, "Remove Attribute from Objects");
        for (CKAttributeType type : listed) {
            std::vector<CK_ID> having;
            for (const auto &row : ReadAttributeObjects(*m_State.document, {type}, chosen))
                if (std::find(chosen.begin(), chosen.end(), row.object) != chosen.end()) having.push_back(row.object);
            if (having.empty()) continue;
            const EditResult result = RemoveAttributeFromObjects(*m_State.document, having, type);
            if (!result.ok && !result.Unchanged()) m_State.Log(LogLevel::Warning, AnsiToUtf8(result.message));
        }
        m_Doc.attributeRows.clear();
    };
    auto showSetup = [&] {
        if (!chosen.empty()) m_State.events.Post(ShowObjectSetup{m_State.document->Reference(chosen.front())});
    };
    // Right: the objects with the selected attribute, selected with a click
    // (Ctrl: added or taken off); values are editable.
    ImGui::SetCursorScreenPos(ImVec2(left + half + split, top));
    if (ImGui::BeginTable("attributeobjects", 3, flags,
                          ImVec2(origin.x + available.x - (left + half + split), origin.y + available.y - top))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        // Dev's columns: 100, 200, 350.
        ImGui::TableSetupColumn("Object Name", ImGuiTableColumnFlags_WidthFixed, 100.0f * s);
        ImGui::TableSetupColumn("Attribute Name", ImGuiTableColumnFlags_WidthFixed, 200.0f * s);
        ImGui::TableSetupColumn("Attribute Value", ImGuiTableColumnFlags_WidthFixed, 350.0f * s);
        ImGui::TableHeadersRow();
        const auto rows = ReadAttributeObjects(*m_State.document, listed);
        for (const auto &row : rows) {
            const CKAttributeType shownType = row.type;
            const auto *attribute = catalog.Find(shownType);
            if (!attribute) continue;
            const auto &attributeName = attribute->name;
            const CK_ID id = row.object;
            ImGui::PushID(shownType);
            ImGui::PushID(static_cast<int>(id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool selected = m_Doc.attributeRows.count(id) != 0;
            if (ImGui::Selectable(((row.name.empty() ? std::string("<Unnamed>") : AnsiToUtf8(row.name)) + "###row").c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (!ImGui::GetIO().KeyCtrl) m_Doc.attributeRows = {id};
                else if (selected) m_Doc.attributeRows.erase(id);
                else m_Doc.attributeRows.insert(id);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) m_State.events.Post(ShowObjectSetup{m_State.document->Reference(id)});
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) {
                m_Doc.attributeRows = {id};
                chosen = {id};
            }
            // Dragged with this object's value.
            if (ImGui::BeginDragDropSource()) {
                const AttributePayload payload{shownType, row.parameter};
                ImGui::SetDragDropPayload(ATTRIBUTE_PAYLOAD, &payload, sizeof(payload));
                ImGui::Text("%s (%s)", AnsiToUtf8(attributeName).c_str(), (row.name.empty() ? std::string("<Unnamed>") : AnsiToUtf8(row.name)).c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem("objectmenu")) {
                if (ImGui::MenuItem("Copy Attribute Parameter Value", "Ctrl+C")) copy();
                if (ImGui::MenuItem("Paste Attribute Parameter Value", "Ctrl+V")) paste();
                if (ImGui::MenuItem("Remove Attribute from Objects", "Del")) removeFromObjects();
                ImGui::Separator();
                if (ImGui::MenuItem("Show Setup", "Enter")) showSetup();
                if (ImGui::MenuItem("Show Object In Level Manager", "(double-click on object name)"))
                    m_State.events.Post(ShowInLevelManager{m_State.document->Reference(chosen.empty() ? id : chosen.front())});
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(AnsiToUtf8(attributeName).c_str());
            ImGui::TableNextColumn();
            if (row.parameter) {
                const bool editing = m_Doc.valueObject == id && m_Doc.valueType == shownType;
                const std::string shown = editing ? m_ValueText : AnsiToUtf8(row.value);
                char buffer[512];
                strncpy(buffer, shown.c_str(), sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = 0;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##value", buffer, sizeof(buffer))) {
                    m_Doc.valueObject = id;
                    m_Doc.valueType = shownType;
                    m_ValueText = buffer;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    const EditResult result =
                        SetAttributeText(*m_State.document, id, shownType, Utf8ToAnsi(m_ValueText));
                    if (!result.ok && !result.Unchanged())
                        m_State.Log(LogLevel::Warning, "Edit Attribute: " + AnsiToUtf8(result.message));
                    m_Doc.valueObject = 0;
                }
                // Documentation: "Double-clicking on the attribute value
                // will open the edit dialog box".
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ImGui::ClearActiveID();
                    m_Doc.valueObject = 0;
                    m_ValueType = row.parameterName;
                    m_ValueBox.Clear();
                    if (m_ValueBox.Add(row.parameter, attributeName)) m_OpenValue = true;
                }
            }
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) removeFromObjects();
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) showSetup();
        // Ctrl+A: every object listed.
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && m_Selected >= 0) {
            m_Doc.attributeRows.clear();
            for (const auto &row : ReadAttributeObjects(*m_State.document, listed)) m_Doc.attributeRows.insert(row.object);
        }
    }
    // Edit Parameter on a value (OK: the box's undo steps, one "Edit Attribute").
    const char *valueDialog = "Edit Parameter###AttributeManagerValue";
    if (m_OpenValue) {
        ImGui::OpenPopup(valueDialog);
        m_OpenValue = false;
    }
    const DialogAction valueAction = EditParameterValueDialog(valueDialog, "Edit Parameter", m_ValueBox, AnsiToUtf8(m_ValueType).c_str());
    if (valueAction == DialogAction::Ok) {
        EditGroup group(*m_State.document, "Edit Attribute");
        m_ValueBox.Apply(0);
    }
    if (valueAction != DialogAction::None) m_ValueBox.Clear();
    ImGui::End();
}

} // namespace vdev

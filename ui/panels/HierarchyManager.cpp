// Dev's Hierarchy Manager (Dev.exe sub_563FC0 makes its toolbar and tree,
// sub_565180 fills it; documentation "Hierarchy Manager").
#include "ui/panels/ManagerWindows.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"

#include "CKAll.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "core/level/HierarchyTree.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/SceneCommands.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

// Dev's colors (InterfaceControls.dll's table): the toolbar 65, the header
// 176, the rows 200 with white lines, a selected row 128.
constexpr ImU32 TOOLBAR = IM_COL32(65, 65, 65, 255);
constexpr ImU32 HEADER = IM_COL32(176, 176, 176, 255);
constexpr ImU32 ROWS = IM_COL32(200, 200, 200, 255);
constexpr ImU32 LINE = IM_COL32(255, 255, 255, 255);
constexpr ImU32 SELECTED = IM_COL32(128, 128, 128, 255);
constexpr ImU32 INK = IM_COL32(0, 0, 0, 255);
constexpr const char *PAYLOAD = "SCHEMATIC_HIERARCHY";

// A root's key in the open set (objects use their id).
CK_ID RootKey(int root) { return 0xFFFFFF00u + static_cast<CK_ID>(root); }

CK_ID KeyOf(const HierarchyItem &item) { return item.object ? item.object : RootKey(item.root); }

} // namespace

void ManagerWindows::HierarchyDrop(CK_ID parent, int background) {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(PAYLOAD)) {
        // Dev moves every selected entity dragged with the one grabbed.
        const CK_ID grabbed = *static_cast<const CK_ID *>(payload->Data);
        std::vector<CK_ID> moved;
        if (m_Doc.hierarchySelection.count(grabbed)) moved.assign(m_Doc.hierarchySelection.begin(), m_Doc.hierarchySelection.end());
        else moved.push_back(grabbed);
        for (const EditResult &result : SetHierarchyParents(*m_State.document, moved, parent, background))
            if (!result.ok && !result.Unchanged())
                m_State.Log(LogLevel::Warning, "Hierarchy Manager: " + AnsiToUtf8(result.message));
    }
    ImGui::EndDragDropTarget();
}

// Dev's layout (Dev.exe sub_563FC0): the toolbar 21 high (Expand Selected,
// Collapse Selected, Show/Hide Meshes, Materials, Textures, the close
// button at the right), then the tree: a header, rows 18 high, the
// Hierarchy column and the Render Priority column 70 wide; the indent 13,
// the arrow (resvis.dll icons 217 / 218), the class icon, the name.
void ManagerWindows::HierarchyManager(bool *open) {
    const ImGuiID dock = hierarchyDock ? hierarchyDock : m_Dock;
    if (dock) ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible =
        BeginDevWindow("Hierarchy Manager", open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    m_HierarchyPickRows.clear();
    m_HierarchyShownRows.clear();
    m_State.objectPick["Hierarchy Manager"] = [this](float x, float y, CK_CLASSID cid) -> CK_ID {
        if (ImGui::GetFrameCount() - m_HierarchyPickFrame > 2 || !m_State.HasDocument()) return 0;
        for (const PickRow &row : m_HierarchyPickRows) {
            if (x < row.x0 || y < row.y0 || x >= row.x1 || y >= row.y1) continue;
            CKObject *object = m_State.document->Context()->GetObject(row.object);
            return object && CKIsChildClassOf(object, cid) ? row.object : 0;
        }
        return 0;
    };
    if (!visible) {
        ImGui::End();
        return;
    }
    DevTabClose(open);
    m_HierarchyPickFrame = ImGui::GetFrameCount();
    CKContext *context = m_State.document->Context();
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    auto at = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };

    // The 3D Layout's selection shows here, a click here selects there.
    ObjectSelection &objects = m_State.objectSelection;
    m_Doc.hierarchyFollower.Pull(objects, m_Doc.hierarchySelection);

    // ------------------------------------------------------------- toolbar
    dl->AddRectFilled(origin, ImVec2(origin.x + available.x, origin.y + 21.0f * s), TOOLBAR);
    if (ImTextureID handle = DevBitmap(834)) dl->AddImage(handle, at(0, 0), at(6, 21));
    auto separator = [&](float x) {
        if (ImTextureID texture = DevBitmap(705)) dl->AddImage(texture, at(x, 1), at(x + 1, 20));
    };
    separator(7);
    static const int EXPAND[5] = {976, 977, 973, 973, 976};
    static const int COLLAPSE[5] = {978, 974, 975, 975, 978};
    static const int MESHES[5] = {1045, 1046, 1047, 1048, 1045};
    static const int MATERIALS[5] = {1052, 1042, 1043, 1044, 1052};
    static const int TEXTURES[5] = {1049, 1050, 1051, 1041, 1049};
    static const int CLOSE[5] = {613, 613, 614, 614, 613};
    int openSelected = -1; // 1 expand, 0 collapse
    if (DevBitmapButton("expand", at(10, 1), EXPAND, false, true, s, "Expand Selected")) openSelected = 1;
    if (DevBitmapButton("collapse", at(30, 1), COLLAPSE, false, true, s, "Collapse Selected")) openSelected = 0;
    separator(77);
    if (DevBitmapButton("meshes", at(78, 1), MESHES, m_HierarchyOptions.meshes, true, s, "Show/Hide Meshes"))
        m_HierarchyOptions.meshes = !m_HierarchyOptions.meshes;
    separator(95);
    // Materials and textures show under meshes shown (the documentation).
    if (DevBitmapButton("materials", at(99, 1), MATERIALS, m_HierarchyOptions.materials, true, s, "Show/Hide Materials"))
        m_HierarchyOptions.materials = !m_HierarchyOptions.materials;
    separator(116);
    if (DevBitmapButton("textures", at(120, 1), TEXTURES, m_HierarchyOptions.textures, true, s, "Show/Hide Textures"))
        m_HierarchyOptions.textures = !m_HierarchyOptions.textures;
    separator(137);
    if (DevBitmapButton("close", ImVec2(origin.x + available.x - 25.0f * s, origin.y + 4.0f * s), CLOSE, false, true, s,
                        nullptr))
        *open = false;

    // ---------------------------------------------------------------- tree
    const std::vector<HierarchyItem> roots = BuildHierarchy(
        context, m_HierarchyOptions, [this](CK_ID id) { return m_State.document->IsDiscarded(id); });
    auto startRename = [this](CK_ID id, const std::string &name) {
        m_Doc.hierarchyRename = id;
        std::strncpy(m_HierarchyRenameBuffer, AnsiToUtf8(name).c_str(), sizeof(m_HierarchyRenameBuffer) - 1);
        m_HierarchyRenameBuffer[sizeof(m_HierarchyRenameBuffer) - 1] = 0;
        m_HierarchyRenameFocus = true;
    };
    // The roots start open (Dev.exe sub_565180 expands the first level).
    if (!m_Doc.hierarchyStarted) {
        for (const HierarchyItem &root : roots) m_Doc.hierarchyOpen.insert(KeyOf(root));
        m_Doc.hierarchyStarted = true;
    }
    // Expand / Collapse Selected: the items selected and all below them.
    if (openSelected >= 0) {
        std::function<void(const HierarchyItem &, bool)> walk = [&](const HierarchyItem &item, bool chosen) {
            chosen = chosen || (item.object && m_Doc.hierarchySelection.count(item.object));
            if (chosen && !item.children.empty()) {
                if (openSelected) m_Doc.hierarchyOpen.insert(KeyOf(item));
                else m_Doc.hierarchyOpen.erase(KeyOf(item));
            }
            for (const HierarchyItem &child : item.children) walk(child, chosen);
        };
        for (const HierarchyItem &root : roots) walk(root, false);
    }
    struct Row {
        const HierarchyItem *item;
        int depth;
    };
    std::vector<Row> rows;
    std::function<void(const HierarchyItem &, int)> flatten = [&](const HierarchyItem &item, int depth) {
        rows.push_back({&item, depth});
        if (m_Doc.hierarchyOpen.count(KeyOf(item)))
            for (const HierarchyItem &child : item.children) flatten(child, depth + 1);
    };
    for (const HierarchyItem &root : roots) flatten(root, 0);

    ImGui::SetCursorScreenPos(at(0, 21));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ROWS);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##hierarchy", ImVec2(available.x, std::max(1.0f, available.y - 21.0f * s)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_None);
    ImDrawList *tl = ImGui::GetWindowDrawList();
    const float rowHeight = 18.0f * s, headerHeight = 14.0f * s;
    const float listWidth = ImGui::GetContentRegionAvail().x;
    const float priorityWidth = 70.0f * s;
    const float nameRight = listWidth - priorityWidth;
    const ImVec2 listOrigin = ImGui::GetCursorScreenPos();
    const float nameRightX = listOrigin.x + nameRight;
    const float scroll = ImGui::GetScrollY();
    // The rows (below the header, which stays on top).
    ImGui::Dummy(ImVec2(listWidth, headerHeight + rowHeight * rows.size()));
    const ImGuiIO &io = ImGui::GetIO();
    bool menu = false;
    for (size_t i = 0; i < rows.size(); ++i) {
        const float y = listOrigin.y + headerHeight + rowHeight * i;
        if (y + rowHeight < listOrigin.y + scroll + headerHeight || y > listOrigin.y + scroll + ImGui::GetWindowHeight())
            continue;
        const HierarchyItem &item = *rows[i].item;
        const float indent = 13.0f * s * rows[i].depth;
        const bool selected = item.object && m_Doc.hierarchySelection.count(item.object);
        const ImVec2 rowMin(listOrigin.x, y), rowMax(listOrigin.x + listWidth, y + rowHeight);
        if (selected) tl->AddRectFilled(rowMin, ImVec2(rowMax.x, rowMax.y - 1.0f), SELECTED);
        tl->AddLine(ImVec2(rowMin.x, rowMax.y - 1.0f), ImVec2(rowMax.x, rowMax.y - 1.0f), LINE);
        ImGui::PushID(static_cast<int>(i));
        // The arrow.
        const ImVec2 arrow(rowMin.x + 6.0f * s + indent, y + (rowHeight - 11.0f * s) * 0.5f);
        bool toggled = false;
        if (!item.children.empty()) {
            const bool expanded = m_Doc.hierarchyOpen.count(KeyOf(item)) != 0;
            if (ImTextureID icon = DevIconImage(expanded ? 218 : 217))
                tl->AddImage(icon, arrow, ImVec2(arrow.x + 11.0f * s, arrow.y + 11.0f * s));
            ImGui::SetCursorScreenPos(arrow);
            if (ImGui::InvisibleButton("##arrow", ImVec2(11.0f * s, 11.0f * s))) {
                toggled = true;
                if (expanded) m_Doc.hierarchyOpen.erase(KeyOf(item));
                else m_Doc.hierarchyOpen.insert(KeyOf(item));
            }
        }
        // The class icon and the name.
        const ImVec2 icon(rowMin.x + 18.0f * s + indent, arrow.y);
        if (ImTextureID texture = DevIconImage(DevClassIconId(item.cid)))
            tl->AddImage(texture, icon, ImVec2(icon.x + 11.0f * s, icon.y + 11.0f * s));
        // The tree's texts are in VI font 0 (Arial 12, narrowed), as the Level Manager's.
        const ImVec2 text(IM_ROUND(rowMin.x + 32.0f * s + indent), IM_ROUND(y + (rowHeight - 12.0f * s) * 0.5f));
        const bool renaming = m_Doc.hierarchyRename && m_Doc.hierarchyRename == item.object;
        if (!renaming) {
            const ImVec2 clip(nameRightX, rowMax.y);
            DrawVIText(tl, text, selected ? LINE : INK, AnsiToUtf8(item.name).c_str(), false, s, &clip);
        }
        // The Render Priority, centered (Dev.exe sub_567250).
        CKRenderObject *render = item.renderObject ? CKRenderObject::Cast(context->GetObject(item.object)) : nullptr;
        const bool editingPriority = render && m_Doc.zEditObject == item.object;
        if (render && !editingPriority) {
            char value[16];
            std::snprintf(value, sizeof(value), "%d", render->GetZOrder());
            const float w = VITextWidth(value, false, s);
            DrawVIText(tl, ImVec2(IM_ROUND(nameRightX + (priorityWidth - w) * 0.5f), text.y), selected ? LINE : INK, value,
                       false, s);
        }
        // The row's button (after the arrow, which takes its clicks).
        ImGui::SetCursorScreenPos(rowMin);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##row", ImVec2(listWidth, rowHeight - 1.0f),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (item.object) {
            const ImVec2 clipMin = tl->GetClipRectMin(), clipMax = tl->GetClipRectMax();
            m_HierarchyPickRows.push_back({std::max(rowMin.x, clipMin.x), std::max(rowMin.y, clipMin.y),
                                           std::min(rowMax.x, clipMax.x), std::min(rowMax.y, clipMax.y), item.object});
        }
        // The row as the UI tests see it (ShownHierarchyRows), when wholly in the list.
        if (y >= listOrigin.y + scroll + headerHeight && y + rowHeight <= listOrigin.y + scroll + ImGui::GetWindowHeight()) {
            ShownRow shown;
            shown.object = item.object;
            shown.name = item.name;
            shown.cid = item.cid;
            shown.depth = rows[i].depth;
            shown.children = !item.children.empty();
            shown.open = m_Doc.hierarchyOpen.count(KeyOf(item)) != 0;
            shown.selected = selected;
            shown.min = rowMin;
            shown.max = ImVec2(rowMax.x, rowMax.y - 1.0f);
            if (shown.children) {
                shown.arrowMin = arrow;
                shown.arrowMax = ImVec2(arrow.x + 11.0f * s, arrow.y + 11.0f * s);
            }
            if (render) {
                shown.priorityMin = ImVec2(nameRightX, rowMin.y);
                shown.priorityMax = shown.max;
            }
            m_HierarchyShownRows.push_back(shown);
        }
        const bool clicked = !toggled && ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        const bool onPriority = io.MousePos.x >= nameRightX;
        if (item.object && (clicked || (rightClicked && !selected))) {
            if (io.KeyShift && m_Doc.hierarchyAnchor) {
                // The range from the anchor in the rows shown.
                size_t anchor = i;
                for (size_t k = 0; k < rows.size(); ++k)
                    if (rows[k].item->object == m_Doc.hierarchyAnchor) anchor = k;
                if (!io.KeyCtrl) m_Doc.hierarchySelection.clear();
                for (size_t k = std::min(anchor, i); k <= std::max(anchor, i); ++k)
                    if (rows[k].item->object) m_Doc.hierarchySelection.insert(rows[k].item->object);
            } else if (io.KeyCtrl && clicked) {
                if (!m_Doc.hierarchySelection.erase(item.object)) m_Doc.hierarchySelection.insert(item.object);
                m_Doc.hierarchyAnchor = item.object;
            } else {
                m_Doc.hierarchySelection = {item.object};
                m_Doc.hierarchyAnchor = item.object;
            }
        } else if (!item.object && clicked) {
            m_Doc.hierarchySelection.clear();
        }
        if (rightClicked && item.object) menu = true;
        // A double click: the name renames (the documentation's mouse
        // shortcut), the priority edits it.
        if (item.object && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !toggled) {
            if (onPriority && render) {
                m_Doc.zEditObject = item.object;
                m_ZEditValue = render->GetZOrder();
                m_ZEditFocus = true;
            } else {
                startRename(item.object, item.name);
            }
        }
        // Dragging an entity onto another or a root sets its parent; Dev
        // refuses to start with a body part or entities of different depths
        // (Dev.exe sub_565DD0).
        bool draggable = true;
        if (item.object && !item.resource && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            std::vector<CK_ID> dragged;
            if (m_Doc.hierarchySelection.count(item.object))
                dragged.assign(m_Doc.hierarchySelection.begin(), m_Doc.hierarchySelection.end());
            else
                dragged.push_back(item.object);
            const char *refusal = nullptr;
            int depth = -1;
            for (CK_ID id : dragged) {
                CKObject *object = context->GetObject(id);
                if (CKBodyPart::Cast(object)) {
                    refusal = "Cannot modify character hierarchy!";
                    break;
                }
                int d = 0;
                if (CK3dEntity *entity = CK3dEntity::Cast(object))
                    for (CK3dEntity *up = entity->GetParent(); up; up = up->GetParent()) ++d;
                else if (CK2dEntity *entity2d = CK2dEntity::Cast(object))
                    for (CK2dEntity *up = entity2d->GetParent(); up; up = up->GetParent()) ++d;
                if (depth >= 0 && d != depth) refusal = "Cannot move multiple entities with different depths!";
                depth = d;
            }
            if (refusal) {
                draggable = false;
                if (!m_HierarchyDragRefused) m_State.Log(LogLevel::Warning, refusal);
                m_HierarchyDragRefused = true;
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_HierarchyDragRefused = false;
        if (item.object && !item.resource && draggable && ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(PAYLOAD, &item.object, sizeof(item.object));
            ImGui::TextUnformatted(AnsiToUtf8(item.name).c_str());
            ImGui::EndDragDropSource();
        }
        if (!item.resource) {
            const int background = item.root == HierarchyItem::BACKGROUND_2D   ? 1
                                   : item.root == HierarchyItem::FOREGROUND_2D ? 0
                                                                               : -1;
            HierarchyDrop(item.object, background);
        }
        // The edits in place.
        if (renaming) {
            ImGui::SetCursorScreenPos(ImVec2(text.x - 2.0f * s, y + 1.0f * s));
            ImGui::SetNextItemWidth(std::max(40.0f * s, nameRightX - text.x - 4.0f * s));
            if (m_HierarchyRenameFocus) {
                ImGui::SetKeyboardFocusHere();
                m_HierarchyRenameFocus = false;
            }
            const bool entered = ImGui::InputText("##rename", m_HierarchyRenameBuffer, sizeof(m_HierarchyRenameBuffer),
                                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (entered) {
                const EditResult result =
                    RenameLevelObject(*m_State.document, item.object, Utf8ToAnsi(m_HierarchyRenameBuffer));
                if (!result.ok && !result.message.empty())
                    m_State.Log(LogLevel::Warning, "Hierarchy Manager: " + AnsiToUtf8(result.message));
                m_Doc.hierarchyRename = 0;
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsItemActive() && !ImGui::IsItemActivated() &&
                                                                 !m_HierarchyRenameFocus && ImGui::IsMouseClicked(0))) {
                m_Doc.hierarchyRename = 0;
            }
        }
        if (editingPriority) {
            ImGui::SetCursorScreenPos(ImVec2(nameRightX + 4.0f * s, y + 1.0f * s));
            ImGui::SetNextItemWidth(priorityWidth - 8.0f * s);
            if (m_ZEditFocus) {
                ImGui::SetKeyboardFocusHere();
                m_ZEditFocus = false;
            }
            const bool entered =
                ImGui::InputInt("##priority", &m_ZEditValue, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
            if (entered) {
                // Every selected render object takes it (Dev: "Works for multiselection").
                std::vector<CK_ID> targets(1, item.object);
                if (m_Doc.hierarchySelection.count(item.object)) targets.assign(m_Doc.hierarchySelection.begin(), m_Doc.hierarchySelection.end());
                const EditResult result = SetRenderPriority(*m_State.document, targets,
                                                            std::clamp(m_ZEditValue, -10000, 10000));
                if (!result.ok && !result.Unchanged())
                    m_State.Log(LogLevel::Warning, "Hierarchy Manager: " + AnsiToUtf8(result.message));
                m_Doc.zEditObject = 0;
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                       (!ImGui::IsItemActive() && !ImGui::IsItemActivated() && ImGui::IsMouseClicked(0))) {
                m_Doc.zEditObject = 0;
            }
        }
        ImGui::PopID();
    }
    // The header over the rows (Hierarchy, Render Priority).
    {
        const ImVec2 hMin(listOrigin.x, listOrigin.y + scroll), hMax(listOrigin.x + listWidth, hMin.y + headerHeight);
        tl->AddRectFilled(hMin, hMax, HEADER);
        tl->AddLine(ImVec2(hMin.x, hMax.y - 2.0f), ImVec2(hMax.x, hMax.y - 2.0f), IM_COL32(31, 31, 31, 255));
        tl->AddLine(ImVec2(hMin.x, hMax.y - 1.0f), ImVec2(hMax.x, hMax.y - 1.0f), IM_COL32(100, 100, 100, 255));
        tl->AddLine(ImVec2(listOrigin.x + nameRight, hMin.y), ImVec2(listOrigin.x + nameRight, hMax.y - 2.0f),
                    IM_COL32(100, 100, 100, 255));
        const float ty = IM_ROUND(hMin.y + (headerHeight - 2.0f - 12.0f * s) * 0.5f);
        const float hw = VITextWidth("Hierarchy", false, s), pw = VITextWidth("Render Priority", false, s);
        DrawVIText(tl, ImVec2(IM_ROUND(listOrigin.x + (nameRight - hw) * 0.5f), ty), INK, "Hierarchy", false, s);
        DrawVIText(tl, ImVec2(IM_ROUND(listOrigin.x + nameRight + (priorityWidth - pw) * 0.5f), ty), INK, "Render Priority",
                   false, s);
    }
    const bool focused = ImGui::IsWindowFocused();
    // The documentation's keys: Up / Down / Home / End / Page Up / Page Down
    // move the selection, Left closes or goes to the parent, Right opens or
    // goes to the first child.
    if (focused && !m_Doc.hierarchyRename && !m_Doc.zEditObject && !rows.empty() && !ImGui::GetIO().WantTextInput) {
        int current = -1;
        for (size_t k = 0; k < rows.size(); ++k)
            if (rows[k].item->object && rows[k].item->object == m_Doc.hierarchyAnchor) current = static_cast<int>(k);
        const int page = std::max(1, static_cast<int>(ImGui::GetWindowHeight() / rowHeight) - 1);
        int next = -2;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = current + 1;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = current < 0 ? 0 : current - 1;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) next = current + page;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) next = current - page;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) next = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End)) next = static_cast<int>(rows.size()) - 1;
        if (current >= 0 && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            const HierarchyItem &item = *rows[current].item;
            if (!item.children.empty() && m_Doc.hierarchyOpen.count(KeyOf(item))) m_Doc.hierarchyOpen.erase(KeyOf(item));
            else
                for (int k = current - 1; k >= 0; --k)
                    if (rows[k].depth < rows[current].depth) {
                        next = k;
                        break;
                    }
        }
        if (current >= 0 && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            const HierarchyItem &item = *rows[current].item;
            if (!item.children.empty() && !m_Doc.hierarchyOpen.count(KeyOf(item))) m_Doc.hierarchyOpen.insert(KeyOf(item));
            else if (!item.children.empty()) next = current + 1;
        }
        if (next != -2) {
            next = std::clamp(next, 0, static_cast<int>(rows.size()) - 1);
            // Rows without an object (the roots) are passed over.
            const int step = next >= current ? 1 : -1;
            while (next >= 0 && next < static_cast<int>(rows.size()) && !rows[next].item->object) next += step;
            if (next >= 0 && next < static_cast<int>(rows.size())) {
                m_Doc.hierarchySelection = {rows[next].item->object};
                m_Doc.hierarchyAnchor = rows[next].item->object;
                const float y = headerHeight + rowHeight * next;
                if (y < ImGui::GetScrollY() + headerHeight) ImGui::SetScrollY(y - headerHeight);
                else if (y + rowHeight > ImGui::GetScrollY() + ImGui::GetWindowHeight())
                    ImGui::SetScrollY(y + rowHeight - ImGui::GetWindowHeight());
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // The selection reaches the 3D Layout (its render objects; not while locked).
    m_Doc.hierarchyFollower.Push(objects, m_Doc.hierarchySelection, context);
    // F2 renames the item selected.
    if (focused && !m_Doc.hierarchyRename && m_Doc.hierarchySelection.size() == 1 && ImGui::IsKeyPressed(ImGuiKey_F2)) {
        const CK_ID id = *m_Doc.hierarchySelection.begin();
        CKObject *object = context->GetObject(id);
        if (object) startRename(id, object->GetName() ? object->GetName() : "");
    }

    // The shortcut menu (documentation "Popup menu"): Setup and Rename for
    // one item, Edit Render Priority, Set / Remove Initial Conditions on
    // Hierarchy.
    if (menu) ImGui::OpenPopup("##hierarchymenu");
    PushDevMenuStyle();
    if (ImGui::BeginPopup("##hierarchymenu")) {
        const bool single = m_Doc.hierarchySelection.size() == 1;
        const CK_ID first = m_Doc.hierarchySelection.empty() ? 0 : *m_Doc.hierarchySelection.begin();
        if (ImGui::MenuItem("Setup", nullptr, false, single)) m_State.events.Post(ShowObjectSetup{first});
        if (ImGui::MenuItem("Rename", "F2", false, single)) {
            CKObject *object = context->GetObject(first);
            startRename(first, object && object->GetName() ? object->GetName() : "");
        }
        CKRenderObject *render = CKRenderObject::Cast(context->GetObject(first));
        if (ImGui::MenuItem("Edit Render Priority", nullptr, false, render != nullptr)) {
            m_Doc.zEditObject = first;
            m_ZEditValue = render->GetZOrder();
            m_ZEditFocus = true;
        }
        ImGui::Separator();
        auto initialConditions = [&](InitialConditions action, const char *label) {
            std::vector<CK_ID> targets;
            for (CK_ID id : m_Doc.hierarchySelection)
                for (CK_ID object : HierarchyOf(context, id))
                    if (std::find(targets.begin(), targets.end(), object) == targets.end()) targets.push_back(object);
            const EditResult result = ApplyInitialConditions(*m_State.document, targets, action);
            if (!result.ok && !result.message.empty()) m_State.Log(LogLevel::Warning, std::string(label) + ": " + AnsiToUtf8(result.message));
        };
        if (ImGui::MenuItem("Set Initial Conditions on Hierarchy", nullptr, false, !m_Doc.hierarchySelection.empty()))
            initialConditions(InitialConditions::Set, "Set Initial Conditions on Hierarchy");
        if (ImGui::MenuItem("Remove Initial Conditions on Hierarchy", nullptr, false, !m_Doc.hierarchySelection.empty()))
            initialConditions(InitialConditions::Remove, "Remove Initial Conditions on Hierarchy");
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
    ImGui::End();
}

} // namespace vdev

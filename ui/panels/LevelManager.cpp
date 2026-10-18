// Level Manager panel, after Dev 2.5's (screenshot with 2D Text.cmo; column
// and folder names from the language pack, [Level View]).
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include "CKAll.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/panels/EditorPanels.h"
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "ui/controls/FileDialogs.h"
#include "core/level/LevelCommands.h"
#include "core/level/LevelTree.h"
#include "core/level/SceneCommands.h"
#include "ui/schematic/SchematicCanvas.h"
#include "core/graph/ScriptCommands.h"
#include "ui/controls/ObjectText.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

std::string NameOf(CKObject *object, const char *unnamed = "<Unnamed>") { return DisplayName(object, unnamed, unnamed); }

// Case-insensitive name order (LevelTree).
bool NameLess(CKObject *a, CKObject *b) { return LevelNameLess(a, b); }

// Dev's Level Manager (Dev.exe sub_5687A0 makes it, sub_56C730 fills its
// tree, sub_577260 / sub_577AD0 its cells; its tree is the multitree of
// Dev.exe sub_40B380 drawn by sub_40CBD0), in Dev pixels: InterfaceControls
// colors (0 black, 2 65, 4 128, 6 176, 7 200, 9 white).
constexpr float ROW = 17.0f;    // the multitree's row (+0x10C)
constexpr float HEADER = 15.0f; // its header
// The tree's arrows and the cells' icons, Dev.exe's own (sub_5687A0's
// image lists): arrow closed / open; Show/Hide shown / hidden; IC; Visible
// visible / hierarchically hidden / hidden (grayed under a hierarchically
// hidden parent); A, crossed A, the dash (leave as is), R, crossed R.
constexpr int ARROW_CLOSED = 0xD9, ARROW_OPEN = 0xDA;
constexpr int LAYER_SHOWN = 0x181, LAYER_HIDDEN = 0x182;
constexpr int IC_SET = 0x186;
constexpr int EYE[2][3] = {{0x216, 0x2D2, 0x217}, {0x2D0, 0x2D3, 0x2D1}};
constexpr int FLAG_A = 0x218, FLAG_A_CROSSED = 0x219, FLAG_LEAVE = 0x2EF, FLAG_R = 0x2D4, FLAG_R_CROSSED = 0x2D5;
// The resvis.dll icons of Dev's folders (sub_56C730: LoadIcons' 9 Level
// Scripts, 38 a folder, 4 places, 57 worksets).
constexpr int ICON_LEVEL_SCRIPTS = 0xCA, ICON_FOLDER = 0x188, ICON_PLACES = 0xC5, ICON_WORKSETS = 0x3F3;

// A 3D or 2D entity under a parent that hides its hierarchy (Dev.exe
// sub_577A40): its eye is grayed.
bool HiddenByParent(CKObject *object) {
    if (CK3dEntity *entity = CK3dEntity::Cast(object)) {
        for (CK3dEntity *up = entity->GetParent(); up; up = up->GetParent())
            if (up->IsHierarchicallyHide()) return true;
    } else if (CK2dEntity *entity = CK2dEntity::Cast(object)) {
        for (CK2dEntity *up = entity->GetParent(); up; up = up->GetParent())
            if (up->IsHierarchicallyHide()) return true;
    }
    return false;
}

void DrawIcon(ImDrawList *dl, int id, const ImVec2 &at, float s, bool exe) {
    if (!id) return;
    if (ImTextureID texture = exe ? DevExeIcon(id) : DevIconImage(id))
        dl->AddImage(texture, at, ImVec2(at.x + 11.0f * s, at.y + 11.0f * s));
}

// A VIButton (BS_PUSHLIKE: Level / Scene): pressed ZC_P_BUTTON (65, white
// text), else ZC_N_BUTTON (200, black text), the border white under the
// mouse (NR / PR), else ZC_x_BUTTONBORDER 128; the bold VI font.
bool PushButton(const char *id, const ImVec2 &min, const ImVec2 &max, const char *label, bool pressed, float s) {
    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float rounding = 5.0f * s;
    dl->AddRectFilled(min, max, vi::Color(pressed ? 2 : 7), rounding);
    dl->AddRect(min, max, vi::Color(hovered ? 9 : 4), rounding, 0, 1.0f);
    const float width = VITextWidth(label, true, s);
    DrawVIText(dl, ImVec2(IM_ROUND((min.x + max.x - width) * 0.5f), IM_ROUND((min.y + max.y - 14.0f * s) * 0.5f)),
               vi::Color(pressed ? 9 : 0), label, true, s);
    return clicked;
}

// The .nms file dialog (the CK file API takes ANSI paths).
FileDialogOptions ScriptFileDialog() {
    FileDialogOptions options;
    options.filter = L"Virtools Scripts (*.nms)\0*.nms\0All files (*.*)\0*.*\0";
    return options;
}

} // namespace

// Dev keeps one selection for its editors: a 3D Layout selection selects
// the rows, a Level Manager click selects its render objects in the 3D
// Layout (unless its selection is locked).
void EditorPanels::SyncObjectSelection() {
    ObjectSelection &objects = m_State.objectSelection;
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
    if (!m_Doc.levelFollower.Pull(objects, m_Doc.levelSelection))
        m_Doc.levelFollower.Push(objects, m_Doc.levelSelection, context);
}

#ifdef VIRTOOLSDEV_UI_TESTS
std::vector<EditorPanels::ShownRow> EditorPanels::ShownLevelRows() const {
    if (ImGui::GetFrameCount() - m_PickFrame > 1) return {};
    return m_ShownRows;
}
#endif

void EditorPanels::ScriptsPanel(bool *open) {
    SyncObjectSelection();
    m_PickRows.clear();
#ifdef VIRTOOLSDEV_UI_TESTS
    m_ShownRows.clear();
#endif
    m_State.objectPick["Level Manager"] = [this](float x, float y, CK_CLASSID cid) -> CK_ID {
        if (ImGui::GetFrameCount() - m_PickFrame > 2 || !m_State.HasDocument()) return 0;
        for (const PickRow &row : m_PickRows) {
            if (x < row.x0 || y < row.y0 || x >= row.x1 || y >= row.y1) continue;
            CKObject *object = m_State.document->Context()->GetObject(row.object);
            return object && CKIsChildClassOf(object, cid) ? row.object : 0;
        }
        return 0;
    };
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible =
        BeginDevWindow("Level Manager", open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }
    m_PickFrame = ImGui::GetFrameCount();
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    auto at = [&](float x, float y) { return ImVec2(IM_ROUND(origin.x + x * s), IM_ROUND(origin.y + y * s)); };
    const float viewBottom = origin.y + available.y;
    // The toolbar, 21 high (Dev.exe sub_5687A0): Toggle Item Show/Hide (the
    // Show/Hide column's hidden items), Expand / Collapse Selected, Set IC
    // For Selected.
    dl->AddRectFilled(origin, ImVec2(origin.x + available.x, at(0, 21).y), vi::Color(2));
    if (ImTextureID cap = DevBitmap(834)) dl->AddImage(cap, at(0, 0), at(6, 21));
    auto separator = [&](float x) {
        if (ImTextureID texture = DevBitmap(705)) dl->AddImage(texture, at(x, 1), at(x + 1, 20));
    };
    static const int SHOW_HIDE[5] = {807, 807, 808, 808, 808};
    static const int EXPAND[5] = {976, 977, 973, 973, 976};
    static const int COLLAPSE[5] = {978, 974, 975, 975, 978};
    if (DevBitmapButton("showhide", at(9, 1), SHOW_HIDE, m_HideLayer, true, s, "Toggle Item Show/Hide"))
        m_HideLayer = !m_HideLayer;
    separator(26);
    separator(37);
    if (DevBitmapButton("expand", at(38, 1), EXPAND, false, true, s, "Expand Selected"))
        (m_Doc.levelSelection.empty() ? m_LevelTreeOpen : m_LevelBranchOpen) = 1;
    if (DevBitmapButton("collapse", at(58, 1), COLLAPSE, false, true, s, "Collapse Selected"))
        (m_Doc.levelSelection.empty() ? m_LevelTreeOpen : m_LevelBranchOpen) = 0;
    separator(75);
    if (vi::ToolbarButton("setic", at(86, 3), at(221, 18), "Set IC For Selected", s) && !m_Doc.levelSelection.empty() &&
        m_State.HasDocument())
        Report(ApplyInitialConditions(*m_State.document,
                                      std::vector<CK_ID>(m_Doc.levelSelection.begin(), m_Doc.levelSelection.end()),
                                      InitialConditions::Set),
               "Set Initial Conditions");
    // The left frame (6 to 76, ZC 4) and the tree's border.
    dl->AddRectFilled(at(6, 21), ImVec2(at(76, 0).x, viewBottom), vi::Color(4), 4.0f * s, ImDrawFlags_RoundCornersTopLeft);
    dl->AddRectFilled(ImVec2(at(76, 0).x - 1.0f, at(0, 21).y), ImVec2(at(76, 0).x, viewBottom), vi::Color(0));
    if (!m_State.HasDocument()) {
        DrawVIText(dl, at(82, 26), vi::Color(0), "No composition loaded", false, s);
        ImGui::End();
        return;
    }
    CKContext *context = m_State.document->Context();
    CKLevel *level = context ? context->GetCurrentLevel() : nullptr;
    CKScene *levelScene = level ? level->GetLevelScene() : nullptr;
    // The context scene (Dev's Level / Scene buttons): the level scene in
    // Level mode, the scene chosen in the list in Scene mode. It is the scene
    // whose flags the Level section shows and A / D change; this editor does
    // not launch it.
    CKScene *contextScene = levelScene;
    if (!m_LevelMode)
        if (CKScene *chosen = CKScene::Cast(context->GetObject(m_Doc.contextScene))) contextScene = chosen;
    m_Doc.contextSceneId = contextScene ? contextScene->GetID() : 0;
    // Dev.exe sub_56F1B0: Ctrl+C, Ctrl+V, Ctrl+F; F3 / Shift+F3.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        const ImGuiIO &io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) CopyOnLevel();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteOnLevel();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) m_OpenFind = true;
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F3, false)) FindOnLevel(!io.KeyShift);
        // Ctrl+F3, Ctrl+Shift+F3: the current row's name found (Dev.exe sub_56F1B0).
        if (io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
            const int current = NavCurrent();
            if (current >= 0) {
                const NavRow &row = m_NavRows[current];
                CKObject *object = row.object ? context->GetObject(row.object) : nullptr;
                m_FindText = object ? AnsiToUtf8(object->GetName() ? object->GetName() : "") : row.label;
            }
            FindOnLevel(!io.KeyShift);
        }
        // Alt+S: the rows selected saved as objects (Save Objects as..., sub_571DD0).
        if (io.KeyAlt && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            const std::vector<CK_ID> targets = LevelTargets();
            if (!targets.empty()) {
                m_Doc.deleteObjects = targets;
                SaveObjectsDialog();
            }
        }
    }
    // Dev Level Manager keys (documentation "Level Manager Keyboard Shortcuts").
    // Ctrl+A: selects all children of the selected items (Scripts, group members).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_A)) {
        for (CK_ID id : LevelTargets()) {
            CKBeObject *object = CKBeObject::Cast(context->GetObject(id));
            for (int i = 0; object && i < object->GetScriptCount(); ++i) m_Doc.levelSelection.insert(object->GetScript(i)->GetID());
            if (CKGroup *group = CKGroup::Cast(object))
                for (int i = 0; i < group->GetObjectCount(); ++i)
                    if (group->GetObject(i)) m_Doc.levelSelection.insert(group->GetObject(i)->GetID());
        }
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt) {
        const std::vector<CK_ID> targets = LevelTargets();
        if (!targets.empty() && ImGui::IsKeyPressed(ImGuiKey_P)) CreatePlaceOf(targets, "Place Selected in New Place");
        if (!targets.empty() && ImGui::IsKeyPressed(ImGuiKey_G)) CreateGroupOf(targets, "Place Selected in New Group");
        if (ImGui::IsKeyPressed(ImGuiKey_C)) Report(CreateScene(*m_State.document, "", targets), "Create Scene");
        if (ImGui::IsKeyPressed(ImGuiKey_S)) CreateScripts();
        if (ImGui::IsKeyPressed(ImGuiKey_R)) Report(CreateArray(*m_State.document, ""), "Create Array");
        if (ImGui::IsKeyPressed(ImGuiKey_W)) CreateWorkset(0);
        // Space: the 3D Layout's selection locked, a row selected, or
        // unlocked (Dev.exe sub_5D2B80).
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            ObjectSelection &objects = m_State.objectSelection;
            if (objects.locked) {
                objects.locked = false;
            } else if (!m_Doc.levelSelection.empty()) {
                m_Doc.levelFollower.Push(objects, m_Doc.levelSelection, context);
                if (!objects.objects.empty()) objects.locked = true;
            }
        }
        // 1 / 2: set / remove the Show/Hide Layer flag of the selected items.
        if (!targets.empty() && (ImGui::IsKeyPressed(ImGuiKey_1) || ImGui::IsKeyPressed(ImGuiKey_2))) {
            const bool hide = ImGui::IsKeyPressed(ImGuiKey_1);
            for (CK_ID id : targets)
                if (hide) m_Doc.layerHidden.insert(id);
                else m_Doc.layerHidden.erase(id);
        }
        // Return: a Script shows in the Schematic, an entity its Setup.
        if (targets.size() == 1 && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
            if (CKBehavior::Cast(context->GetObject(targets.front()))) {
                ShowScript(targets.front());
                m_State.events.Post(FocusSchematic{});
            } else {
                m_State.events.Post(ShowObjectSetup{targets.front()});
            }
        }
        if (!targets.empty() && (ImGui::IsKeyPressed(ImGuiKey_V) || ImGui::IsKeyPressed(ImGuiKey_H)))
            Report(ShowObjects(*m_State.document, targets, ImGui::IsKeyPressed(ImGuiKey_V)),
                   "Show/Hide in 3D Layout");
        // + / -: the object moves in its group's order.
        if (targets.size() == 1 && m_Doc.selectionGroup &&
            (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) || ImGui::IsKeyPressed(ImGuiKey_Equal) ||
             ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract) || ImGui::IsKeyPressed(ImGuiKey_Minus)))
            Report(MoveInGroup(*m_State.document, m_Doc.selectionGroup, targets.front(),
                               ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) || ImGui::IsKeyPressed(ImGuiKey_Equal) ? -1 : 1),
                   "Move");
        if (!targets.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteLevelRows(targets);
        if (ImGui::IsKeyPressed(ImGuiKey_F2) && targets.size() == 1) {
            m_Doc.renameObject = targets.front();
            m_Doc.renameWorkset = 0;
            CKObject *object = context->GetObject(m_Doc.renameObject);
            m_RenameText = object && object->GetName() ? AnsiToUtf8(object->GetName()) : std::string();
            m_Doc.openRename = true;
        }
        // A / D: activate / deactivate in their context scene.
        if (!targets.empty() && (ImGui::IsKeyPressed(ImGuiKey_A) || ImGui::IsKeyPressed(ImGuiKey_D))) {
            const bool activate = ImGui::IsKeyPressed(ImGuiKey_A);
            Report(ModifySceneFlags(*m_State.document, targets,
                                    activate ? CK_SCENEOBJECT_START_ACTIVATE : CK_SCENEOBJECT_START_DEACTIVATE,
                                    (activate ? CK_SCENEOBJECT_START_DEACTIVATE : CK_SCENEOBJECT_START_ACTIVATE) |
                                        CK_SCENEOBJECT_START_LEAVE,
                                    activate ? "Activate" : "Deactivate", m_Doc.contextSceneId),
                   activate ? "Activate" : "Deactivate");
        }
    }
    RenameDialog();
    QuestionDialog();
    FindDialog();
    PriorityDialog();
    CopyOptionsDialog();
    SaveAsDialog();
    // Show Object/Script In Level Manager: open the tree, highlight and scroll to the row.
    const CK_ID focus = m_Doc.levelFocus;
    if (focus) m_LevelTreeOpen = 1;

    // ------------------------------------------------------- the left frame
    // The creation tools (20 x 18 cells, resvis.dll bitmaps), Level / Scene
    // (the context of the scene columns), the scenes between their arrows.
    std::vector<CKScene *> scenes;
    for (int i = 0; level && i < level->GetSceneCount(); ++i)
        if (CKScene *each = level->GetScene(i)) scenes.push_back(each);
    std::sort(scenes.begin(), scenes.end(), NameLess); // the list is LBS_SORT
    // The tree's scene sections in the rows' order.
    std::vector<CKScene *> sceneRows = scenes;
    std::stable_sort(sceneRows.begin(), sceneRows.end(), [this](CKObject *a, CKObject *b) { return LevelLess(a, b); });
    auto rowLess = [this](CKObject *a, CKObject *b) { return LevelLess(a, b); };
    {
        static const int PLACE[5] = {1028, 1029, 1030, 1030, 1028};
        static const int GROUP[5] = {1025, 1026, 1027, 1027, 1025};
        static const int ARRAY[5] = {1036, 1023, 1024, 1024, 1036};
        static const int SCENE[5] = {1031, 1032, 1033, 1033, 1031};
        static const int WORKSET[5] = {1035, 1034, 1022, 1022, 1035};
        static const int SCRIPT[5] = {1039, 1038, 1037, 1037, 1039};
        if (DevBitmapButton("createplace", at(10, 27), PLACE, false, true, s, "Create Place (P)"))
            CreatePlaceOf(LevelTargets(), "Create Place");
        if (DevBitmapButton("creategroup", at(30, 27), GROUP, false, true, s, "Create Group (G)"))
            CreateGroupOf(LevelTargets(), "Create Group");
        if (DevBitmapButton("createarray", at(50, 27), ARRAY, false, true, s, "Create Array (R)"))
            Report(CreateArray(*m_State.document, ""), "Create Array");
        if (DevBitmapButton("createscene", at(10, 45), SCENE, false, true, s, "Create Scene (C)"))
            Report(CreateScene(*m_State.document, "", LevelTargets()), "Create Scene");
        if (DevBitmapButton("createworkset", at(30, 45), WORKSET, false, true, s, "Create Workset (W)")) CreateWorkset(0);
        if (DevBitmapButton("createscript", at(50, 45), SCRIPT, false, !m_Doc.levelSelection.empty(), s, "Create Script (S)"))
            CreateScripts();
        if (PushButton("##level", at(9, 69), at(71, 87), "Level", m_LevelMode, s)) m_LevelMode = true;
        if (PushButton("##scene", at(9, 91), at(71, 109), "Scene", !m_LevelMode, s) && !scenes.empty()) {
            m_LevelMode = false;
            if (!CKScene::Cast(context->GetObject(m_Doc.contextScene)))
                m_Doc.contextScene =
                    (level->GetCurrentScene() && level->GetCurrentScene() != levelScene ? level->GetCurrentScene() : scenes.front())
                        ->GetID();
        }
        int chosen = -1;
        for (size_t i = 0; i < scenes.size(); ++i)
            if (scenes[i]->GetID() == m_Doc.contextScene) chosen = static_cast<int>(i);
        static const int UP[5] = {671, 672, 673, 673, 671};
        static const int DOWN[5] = {668, 669, 670, 670, 668};
        if (DevBitmapButton("sceneup", at(9, 109), UP, false, true, s, nullptr) && chosen > 0) {
            m_Doc.contextScene = scenes[chosen - 1]->GetID();
            m_LevelMode = false;
        }
        // The scene list (an owner-drawn list box, VI font 0).
        const ImVec2 listMin = at(9, 126), listMax = at(71, 288);
        dl->AddRectFilled(listMin, listMax, IM_COL32(204, 204, 204, 255));
        const float item = 14.0f * s;
        dl->PushClipRect(listMin, listMax, true);
        for (size_t i = 0; i < scenes.size(); ++i) {
            const ImVec2 min(listMin.x, listMin.y + item * i), max(listMax.x, min.y + item);
            if (min.y >= listMax.y) break;
            const bool selected = !m_LevelMode && scenes[i]->GetID() == m_Doc.contextScene;
            if (selected) dl->AddRectFilled(min, max, vi::Color(4));
            const ImVec2 textMax(max.x - 2.0f * s, max.y);
            DrawVIText(dl, ImVec2(min.x + 2.0f * s, IM_ROUND(min.y + (item - 12.0f * s) * 0.5f)),
                       vi::Color(selected ? 9 : 0), NameOf(scenes[i], "<Unnamed Scene>").c_str(), false, s, &textMax);
            ImGui::SetCursorScreenPos(min);
            ImGui::PushID(static_cast<int>(scenes[i]->GetID()));
            if (ImGui::InvisibleButton("##scene", ImVec2(max.x - min.x, std::min(item, listMax.y - min.y)))) {
                m_Doc.contextScene = scenes[i]->GetID();
                m_LevelMode = false;
            }
            ImGui::PopID();
        }
        dl->PopClipRect();
        if (DevBitmapButton("scenedown", at(9, 288), DOWN, false, true, s, nullptr) &&
            chosen + 1 < static_cast<int>(scenes.size())) {
            m_Doc.contextScene = scenes[chosen < 0 ? 0 : chosen + 1]->GetID();
            m_LevelMode = false;
        }
    }

    // ------------------------------------------------------------- the tree
    // Its header (the columns in Dev's order, the Show/Hide one first), the
    // rows 17 high: the context section (the level in Level mode, the scene
    // chosen in Scene mode; Dev.exe sub_40FD30) on ZC 7, the others on ZC 6,
    // a selected row ZC 4 with white text; a white line under each row, a
    // black one under the last (sub_40CBD0, flag 2 of sub_411B60).
    ImGui::SetCursorScreenPos(at(76, 21));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vi::Color(6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##leveltree", ImVec2(std::max(1.0f, origin.x + available.x - at(76, 0).x), std::max(1.0f, viewBottom - at(0, 21).y)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);
    // The rows open and close for one composition: their ids are the
    // objects' ids, which CK gives again to the next composition's objects.
    ImGui::PushID(static_cast<int>(m_State.host.Generation()));
    ImDrawList *tl = ImGui::GetWindowDrawList();
    const ImVec2 list = ImGui::GetCursorScreenPos();
    const float scrollY = ImGui::GetScrollY();
    const float windowHeight = ImGui::GetWindowHeight();
    float columnX[10] = {};
    columnX[0] = list.x;
    for (int i = 0; i < 9; ++i) columnX[i + 1] = columnX[i] + IM_ROUND(m_LevelColumns[i] * s);
    // The rows span the visible width (within the scroll bar), or the columns.
    const float rowRight = std::max(columnX[9], list.x + ImGui::GetContentRegionAvail().x);
    const float rowHeight = ROW * s, headerHeight = HEADER * s;
    const float rowsTop = list.y + headerHeight;
    // The rows' items below the header only.
    ImGui::PushClipRect(ImVec2(list.x, list.y + scrollY + headerHeight), ImVec2(rowRight, list.y + scrollY + windowHeight), true);

    int index = 0, depth = 0;
    // The objects of the open rows above (folders: 0), and the scene of the
    // section drawn (0: the level's): a file dropped onto a row needs them.
    std::vector<CK_ID> openRows;
    CK_ID sectionScene = 0;
    bool section = true; // the rows belong to the context section
    // With Toggle Show/Hide on, items flagged hidden are not listed.
    auto listed = [&](CK_ID id) { return !(m_HideLayer && m_Doc.layerHidden.count(id)); };
    // A row's cells (Dev.exe sub_577260 / sub_577AD0): layer: the Show/Hide
    // flag's item (0: shown, not flaggable); object / scene: IC, the flags of
    // that scene, Visible; flags: Activate, Reset, Active Now, Priority;
    // owner: the last column's text.
    struct Cells {
        CK_ID layer = 0;
        CKSceneObject *object = nullptr;
        CKScene *scene = nullptr;
        bool flags = false;
        std::string owner;
        int workset = 0; // a workset's row
        CK_ID group = 0; // the group the row is listed in
    };
    std::vector<int> openWorksets; // beside openRows: the workset each is (0: none)
    // The click on a row: selected, its section kept (Delete's).
    auto selectRow = [&](CK_ID id) {
        SelectRow(id);
        m_Doc.selectionScene = sectionScene;
        m_Doc.selectionGroup = 0;
    };
    // A row: its background and cells, the arrow, the icon and the name;
    // the row's button is the last item (clicks, menus, drags). Returns
    // whether it is open; then the rows below are its children until
    // endRow.
    auto beginRow = [&](const char *id, int icon, const std::string &label, bool children, bool defaultOpen,
                        bool folder, bool selected, const Cells &cells) -> bool {
        ImGuiStorage *storage = ImGui::GetStateStorage();
        const ImGuiID key = ImGui::GetID(id);
        if (m_LevelTreeOpen >= 0 && children) storage->SetBool(key, m_LevelTreeOpen != 0);
        // Expand / Collapse Selected: the rows selected and under them.
        if (m_LevelBranchOpen >= 0 && children) {
            bool branch = selected;
            for (CK_ID above : openRows) branch = branch || (above && m_Doc.levelSelection.count(above));
            if (branch) storage->SetBool(key, m_LevelBranchOpen != 0);
        }
        bool open = children && storage->GetBool(key, defaultOpen);
        // A folder the keys moved to shows selected.
        if (!cells.layer && key == m_NavKey && m_Doc.levelSelection.empty() && m_NavSelection.empty()) selected = true;
        const float y = rowsTop + rowHeight * index++;
        const ImVec2 rowMin(list.x, y), rowMax(rowRight, y + rowHeight);
        const bool shown = rowMax.y >= list.y + scrollY + headerHeight && rowMin.y <= list.y + scrollY + windowHeight;
#ifdef VIRTOOLSDEV_UI_TESTS
        // The row as the UI tests see it (ShownLevelRows), when wholly in the list.
        ShownRow report;
        report.object = cells.layer;
        report.name = label;
        report.cid = cells.object ? cells.object->GetClassID() : 0;
        report.depth = depth;
        report.children = children;
        report.selected = selected;
        report.min = rowMin;
        report.max = ImVec2(rowMax.x, rowMax.y - 1.0f);
#endif
        const ImU32 ink = vi::Color(selected ? 9 : 0);
        if (shown) {
            tl->AddRectFilled(rowMin, rowMax, vi::Color(selected ? 4 : section ? 7 : 6));
            tl->AddRectFilled(ImVec2(rowMin.x, rowMax.y - 1.0f), rowMax, vi::Color(9));
        }
        auto cellIcon = [&](int column, int iconId) {
            if (!shown || !iconId) return;
            const float x = IM_ROUND((columnX[column] + columnX[column + 1] - 11.0f * s) * 0.5f);
            DrawIcon(tl, iconId, ImVec2(x, IM_ROUND(y + (rowHeight - 11.0f * s) * 0.5f)), s, true);
        };
        auto cellButton = [&](const char *button, int column) {
            ImGui::SetCursorScreenPos(ImVec2(columnX[column], y));
            return ImGui::InvisibleButton(button, ImVec2(columnX[column + 1] - columnX[column], rowHeight - 1.0f));
        };
        ImGui::PushID(id);
        // Show/Hide: the tray with its ball while the item shows.
        const bool hidden = cells.layer && m_Doc.layerHidden.count(cells.layer);
        cellIcon(0, hidden ? LAYER_HIDDEN : LAYER_SHOWN);
        if (cells.layer && cellButton("##layer", 0)) {
            const std::vector<CK_ID> rows = m_Doc.levelSelection.count(cells.layer) ? LevelTargets() : std::vector<CK_ID>{cells.layer};
            for (CK_ID row : rows)
                if (!hidden) m_Doc.layerHidden.insert(row);
                else m_Doc.layerHidden.erase(row);
        }
        CKSceneObject *object = cells.object;
        CKScene *scene = cells.scene;
        const bool here = object && scene && scene->IsObjectHere(object);
        // IC: a cross when the scene keeps the object's initial conditions.
        if (here && scene->GetObjectInitialValue(object)) cellIcon(2, IC_SET);
        // Visible: the eye of the object shown, hiding its hierarchy, hidden.
        if (object && object->CanBeHide()) {
            const int state = object->IsVisible() ? 0 : object->IsHierarchicallyHide() ? 1 : 2;
            cellIcon(3, EYE[HiddenByParent(object) ? 1 : 0][state]);
#ifdef VIRTOOLSDEV_UI_TESTS
            report.eyeMin = ImVec2(columnX[3], y);
            report.eyeMax = ImVec2(columnX[4], y + rowHeight - 1.0f);
#endif
            if (cellButton("##visible", 3)) {
                const std::vector<CK_ID> rows = m_Doc.levelSelection.count(object->GetID()) ? LevelTargets()
                                                                                         : std::vector<CK_ID>{object->GetID()};
                // Dev.exe sub_574C10: shown, hidden, hidden with its hierarchy, shown.
                const CK_OBJECT_SHOWOPTION next = object->IsVisible() ? CKHIDE
                                                  : !CKRenderObject::Cast(object) || object->IsHierarchicallyHide()
                                                      ? CKSHOW
                                                      : CKHIERARCHICALHIDE;
                Report(ShowObjectsAs(*m_State.document, rows, next), "Show/Hide in 3D Layout");
            }
        }
        if (cells.flags && here) {
            const CK_SCENEOBJECT_FLAGS flags = scene->GetObjectFlags(object);
            cellIcon(4, flags & CK_SCENEOBJECT_START_ACTIVATE     ? FLAG_A
                        : flags & CK_SCENEOBJECT_START_DEACTIVATE ? FLAG_A_CROSSED
                        : flags & CK_SCENEOBJECT_START_LEAVE      ? FLAG_LEAVE
                                                                  : FLAG_A);
            cellIcon(5, flags & CK_SCENEOBJECT_START_RESET ? FLAG_R : FLAG_R_CROSSED);
            cellIcon(6, flags & CK_SCENEOBJECT_ACTIVE ? FLAG_A : FLAG_A_CROSSED);
            if (cellButton("##activate", 4)) FlagClick(object->GetID(), 1, scene);
            if (cellButton("##reset", 5)) FlagClick(object->GetID(), 2, scene);
            if (cellButton("##activenow", 6)) FlagClick(object->GetID(), 3, scene);
#ifdef VIRTOOLSDEV_UI_TESTS
            report.activateMin = ImVec2(columnX[4], y);
            report.activateMax = ImVec2(columnX[5], y + rowHeight - 1.0f);
            report.activeMin = ImVec2(columnX[6], y);
            report.activeMax = ImVec2(columnX[7], y + rowHeight - 1.0f);
#endif
            int priority = -1;
            if (CKBeObject *beObject = CKBeObject::Cast(object)) priority = beObject->GetPriority();
            else if (CKBehavior *behavior = CKBehavior::Cast(object)) priority = behavior->GetPriority();
            // A double click on the priority edits it.
            cellButton("##priority", 7);
            if (priority != -1 && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_PriorityObject = object->GetID();
                m_PriorityValue = priority;
                m_OpenPriority = true;
            }
            if (shown && priority != -1) {
                const std::string text = std::to_string(priority);
                const float x = IM_ROUND((columnX[7] + columnX[8] - VITextWidth(text.c_str(), false, s)) * 0.5f);
                DrawVIText(tl, ImVec2(x, IM_ROUND(y + (rowHeight - 12.0f * s) * 0.5f)), ink, text.c_str(), false, s);
            }
        }
        if (shown && !cells.owner.empty()) {
            const ImVec2 clip(rowRight, y + rowHeight);
            DrawVIText(tl, ImVec2(columnX[8] + 2.0f * s, IM_ROUND(y + (rowHeight - 12.0f * s) * 0.5f)), ink,
                       cells.owner.c_str(), false, s, &clip);
        }
        // The name: the arrow, the class icon, the label (sub_40CBD0: 4 +
        // 13 per level, the arrow 11, 2, the icon 11, 2).
        const float indent = 13.0f * s * depth;
        const ImVec2 arrow(IM_ROUND(columnX[1] + 4.0f * s + indent), IM_ROUND(y + (rowHeight - 11.0f * s) * 0.5f));
        bool toggled = false;
        if (children) {
            if (shown) DrawIcon(tl, open ? ARROW_OPEN : ARROW_CLOSED, arrow, s, true);
            ImGui::SetCursorScreenPos(arrow);
            if (ImGui::InvisibleButton("##arrow", ImVec2(11.0f * s, 11.0f * s))) toggled = true;
#ifdef VIRTOOLSDEV_UI_TESTS
            report.arrowMin = arrow;
            report.arrowMax = ImVec2(arrow.x + 11.0f * s, arrow.y + 11.0f * s);
#endif
        }
        if (shown) {
            DrawIcon(tl, icon, ImVec2(arrow.x + 13.0f * s, arrow.y), s, false);
            const ImVec2 clip(columnX[2], y + rowHeight);
            DrawVIText(tl, ImVec2(arrow.x + 26.0f * s, IM_ROUND(y + (rowHeight - 12.0f * s) * 0.5f)), ink, label.c_str(),
                       false, s, &clip);
        }
        ImGui::PopID();
        // The row's button (the cells and the arrow take their own clicks).
        ImGui::SetCursorScreenPos(rowMin);
        ImGui::PushID(id);
        ImGui::InvisibleButton("##row", ImVec2(rowRight - rowMin.x, rowHeight - 1.0f));
        ImGui::PopID();
        if (ImGui::IsItemClicked()) {
            m_NavKey = key;
            m_NavClicked = true;
        }
        // A Data Resources file dropped onto the row (Dev.exe sub_56B120).
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(RESOURCE_FILE_PAYLOAD)) {
                const auto *file = static_cast<const ResourceFilePayload *>(payload->Data);
                m_LevelDrop.pending = true;
                m_LevelDrop.path = file->path;
                m_LevelDrop.cid = file->cid;
                m_LevelDrop.target = LevelDropTarget();
                m_LevelDrop.target.row = cells.layer;
                for (auto it = openRows.rbegin(); it != openRows.rend(); ++it)
                    if (*it) m_LevelDrop.target.above.push_back(*it);
                m_LevelDrop.target.scene = sectionScene;
                m_LevelDrop.selected = selected;
            }
            // Rows dragged onto this one (Dev.exe sub_56E790).
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(LEVEL_OBJECT_PAYLOAD)) {
                CK_ID dragged = 0;
                std::memcpy(&dragged, payload->Data, sizeof(dragged));
                m_LevelMove.pending = true;
                m_LevelMove.dragged = dragged;
                m_LevelMove.target = LevelDropTarget();
                m_LevelMove.target.row = cells.layer;
                for (auto it = openRows.rbegin(); it != openRows.rend(); ++it)
                    if (*it) m_LevelMove.target.above.push_back(*it);
                m_LevelMove.target.scene = sectionScene;
                m_LevelMove.workset = cells.workset;
                m_LevelMove.inWorkset = !openWorksets.empty() && openWorksets.back() != 0;
                m_LevelMove.copy = ImGui::GetIO().KeyShift;
            }
            // An attribute from the Attribute Manager.
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(ATTRIBUTE_PAYLOAD)) {
                m_LevelAttribute.pending = true;
                std::memcpy(&m_LevelAttribute.attribute, payload->Data, sizeof(AttributePayload));
                m_LevelAttribute.row = cells.layer;
                m_LevelAttribute.selected = selected;
            }
            // A Building Block from the Building Blocks (Dev.exe sub_56BAB0).
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(BUILDING_BLOCK_PAYLOAD)) {
                m_LevelBlock.pending = true;
                std::memcpy(&m_LevelBlock.guid, payload->Data, sizeof(CKGUID));
                m_LevelBlock.row = cells.layer;
                m_LevelBlock.selected = selected;
            }
            ImGui::EndDragDropTarget();
        }
        // A double click opens or closes a row with children (Dev.exe
        // sub_569960: the Setup only for a row without).
        if (children && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) toggled = true;
        (void)folder;
#ifdef VIRTOOLSDEV_UI_TESTS
        report.open = open;
#endif
        NavRow nav;
        nav.object = cells.layer;
        nav.scene = sectionScene;
        nav.group = cells.group;
        nav.key = key;
        nav.depth = depth;
        nav.children = children;
        nav.open = open;
        nav.label = label;
        m_NavDrawn.push_back(std::move(nav));
#ifdef VIRTOOLSDEV_UI_TESTS
        if (rowMin.y >= list.y + scrollY + headerHeight && rowMax.y <= list.y + scrollY + windowHeight)
            m_ShownRows.push_back(report);
#endif
        if (toggled) {
            open = !open;
            storage->SetBool(key, open);
        }
        if (open) {
            ImGui::PushID(id);
            ++depth;
            openRows.push_back(cells.layer);
            openWorksets.push_back(cells.workset);
        }
        return open;
    };
    auto endRow = [&]() {
        --depth;
        openRows.pop_back();
        openWorksets.pop_back();
        ImGui::PopID();
    };
    // A row dragged (onto another, the 3D Layout's material drop).
    auto dragSource = [](CKObject *object) {
        if (!ImGui::BeginDragDropSource()) return;
        const CK_ID dragged = object->GetID();
        ImGui::SetDragDropPayload(LEVEL_OBJECT_PAYLOAD, &dragged, sizeof(dragged));
        ImGui::TextUnformatted(NameOf(object).c_str());
        ImGui::EndDragDropSource();
    };
    // A double click on the row just drawn.
    auto doubleClicked = [] { return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left); };
    auto focusOn = [&](CK_ID id) {
        if (focus && focus == id) ImGui::SetScrollY(rowsTop + rowHeight * (index - 1) - list.y - windowHeight * 0.5f);
    };

    // owner: Dev's "Script Owner: %s" (Worksets).
    auto scriptRow = [&](CKBehavior *script, CKScene *scene, bool owner) {
        if (!script || !listed(script->GetID())) return;
        ImGui::PushID(static_cast<int>(script->GetID()));
        Cells cells;
        cells.layer = script->GetID();
        cells.object = script;
        cells.scene = scene;
        cells.flags = true;
        if (owner) cells.owner = "Script Owner: " + NameOf(script->GetOwner(), "-");
        const bool selected = focus ? focus == script->GetID() : m_Doc.levelSelection.count(script->GetID()) != 0;
        beginRow("script", DevClassIconId(CKCID_BEHAVIOR), NameOf(script), false, false, false, selected, cells);
        dragSource(script);
        if (ImGui::IsItemClicked()) {
            selectRow(script->GetID());
            ShowScript(script->GetID());
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
            ShowScript(script->GetID());
            m_State.events.Post(FocusSchematic{});
        }
        RowMenu(script->GetID(), script->GetID());
        // Dev: double-click switches to the Schematic.
        if (doubleClicked()) m_State.events.Post(FocusSchematic{});
        focusOn(script->GetID());
        ImGui::PopID();
    };
    // A folder: its menu's Select Children (the objects it holds).
    auto folderRow = [&](const char *id, const std::string &label, bool empty, int icon = ICON_FOLDER,
                         std::vector<CK_ID> children = {}) {
        const bool open = beginRow(id, icon, label, !empty, true, true, false, Cells());
        if (!children.empty()) {
            PushDevMenuStyle();
            if (ImGui::BeginPopupContextItem("foldermenu")) {
                if (ImGui::MenuItem("Select Children")) {
                    m_Doc.levelSelection = std::set<CK_ID>(children.begin(), children.end());
                    m_Doc.selectionScene = sectionScene;
                    m_Doc.selectionGroup = 0;
                }
                ImGui::EndPopup();
            }
            PopDevMenuStyle();
        }
        return open;
    };
    auto idsOf = [](const auto &objects) {
        std::vector<CK_ID> ids;
        for (CKObject *object : objects)
            if (object) ids.push_back(object->GetID());
        return ids;
    };
    auto folderIds = [&](const LevelFolders &folders) {
        std::vector<CK_ID> ids;
        for (const auto &folder : folders)
            for (CKSceneObject *object : folder.second) ids.push_back(object->GetID());
        return ids;
    };
    using Folders = LevelFolders;
    std::function<void(CKSceneObject *, CKScene *, CKGroup *)> objectRow;
    // Objects by Dev folder name, sorted by name.
    auto folderRows = [&](const Folders &folders, CKScene *scene) {
        for (const auto &folder : folders) {
            ImGui::PushID(folder.first.c_str());
            if (folderRow("folder", folder.first, folder.second.empty(), ICON_FOLDER, idsOf(folder.second))) {
                std::vector<CKSceneObject *> objects = folder.second;
                std::sort(objects.begin(), objects.end(), rowLess);
                for (CKSceneObject *object : objects) objectRow(object, scene, nullptr);
                endRow();
            }
            ImGui::PopID();
        }
    };
    std::set<CK_ID> openGroups; // a group shown inside itself stops there
    objectRow = [&](CKSceneObject *object, CKScene *scene, CKGroup *inGroup) {
        if (!listed(object->GetID())) return;
        ImGui::PushID(static_cast<int>(object->GetID()));
        // Animations are scene objects without Scripts.
        CKBeObject *beObject = CKBeObject::Cast(object);
        const int scripts = beObject ? beObject->GetScriptCount() : 0;
        // A group lists its objects (ordered: Move Up/Down); a character
        // its animations and body parts (documentation "Name Column").
        CKGroup *group = CKGroup::Cast(object);
        const int members = group && !openGroups.count(group->GetID()) ? group->GetObjectCount() : 0;
        CKCharacter *character = CKCharacter::Cast(object);
        const int animations = character ? character->GetAnimationCount() : 0;
        const int parts = character ? character->GetBodyPartCount() : 0;
        const bool children = scripts > 0 || members > 0 || animations > 0 || parts > 0;
        const bool selected = focus ? focus == object->GetID() : m_Doc.levelSelection.count(object->GetID()) != 0;
        Cells cells;
        cells.layer = object->GetID();
        cells.object = object;
        cells.scene = scene;
        cells.flags = beObject != nullptr;
        cells.group = inGroup ? inGroup->GetID() : 0;
        // Objects start folded (Dev); folders start open.
        const bool expanded =
            beginRow("object", DevClassIconId(object->GetClassID()), NameOf(object), children, false, false, selected, cells);
        // Dragged onto a row, or to the 3D Layout (a material onto a face,
        // Dev.exe sub_4C0A30).
        dragSource(object);
        focusOn(object->GetID());
        if (doubleClicked() && !children) m_State.events.Post(ShowObjectSetup{object->GetID()});
        if (ImGui::IsItemClicked()) {
            selectRow(object->GetID());
            m_Doc.selectionGroup = inGroup ? inGroup->GetID() : 0;
            if (scripts) ShowScript(beObject->GetScript(0)->GetID());
        }
        // Return or middle button on an entity shows its Setup.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) m_State.events.Post(ShowObjectSetup{object->GetID()});
        RowMenu(object->GetID(), scripts ? beObject->GetScript(0)->GetID() : 0, inGroup ? inGroup->GetID() : 0);
        if (expanded) {
            // Its Scripts in their order, or by priority.
            std::vector<CKObject *> ordered;
            for (int i = 0; i < scripts; ++i) ordered.push_back(beObject->GetScript(i));
            if (m_LevelSort == 1) std::stable_sort(ordered.begin(), ordered.end(), rowLess);
            for (CKObject *script : ordered) scriptRow(CKBehavior::Cast(script), scene, false);
            std::vector<CK_ID> animationIds;
            for (int i = 0; i < animations; ++i)
                if (character->GetAnimation(i)) animationIds.push_back(character->GetAnimation(i)->GetID());
            if (animations && folderRow("animations", "Animations", false, ICON_FOLDER, animationIds)) {
                for (int i = 0; i < animations; ++i)
                    if (CKAnimation *animation = character->GetAnimation(i)) objectRow(animation, scene, nullptr);
                endRow();
            }
            std::vector<CKObject *> sorted;
            for (int i = 0; i < parts; ++i)
                if (character->GetBodyPart(i)) sorted.push_back(character->GetBodyPart(i));
            if (parts && folderRow("bodyparts", "Body Parts", false, ICON_FOLDER, idsOf(sorted))) {
                std::sort(sorted.begin(), sorted.end(), rowLess);
                for (CKObject *part : sorted) objectRow(CKSceneObject::Cast(part), scene, nullptr);
                endRow();
            }
            if (members) {
                openGroups.insert(group->GetID());
                for (int i = 0; i < group->GetObjectCount(); ++i)
                    if (CKBeObject *member = group->GetObject(i)) objectRow(member, scene, group);
                openGroups.erase(group->GetID());
            }
            endRow();
        }
        ImGui::PopID();
    };

    // The objects of a scene by folder and place (LevelTree).
    using Contents = SceneContents;
    const bool showDynamic = m_State.preferences.Bool("3D Layout\\Show Dynamic Objects");
    auto contents = [&](CKScene *scene) { return ListSceneContents(context, scene, level, showDynamic); };
    // A place, its Scripts and its objects by class.
    auto placeRows = [&](const Contents &all, CKScene *scene) {
        std::vector<CKBeObject *> places = all.places;
        std::stable_sort(places.begin(), places.end(), rowLess);
        for (CKBeObject *object : places) {
            if (!listed(object->GetID())) continue;
            ImGui::PushID(static_cast<int>(object->GetID()));
            auto inside = all.placed.find(object->GetID());
            const bool children = object->GetScriptCount() > 0 || inside != all.placed.end();
            const bool selected = focus ? focus == object->GetID() : m_Doc.levelSelection.count(object->GetID()) != 0;
            Cells cells;
            cells.layer = object->GetID();
            cells.object = object;
            cells.scene = scene;
            cells.flags = true;
            const bool expanded = beginRow("place", DevClassIconId(CKCID_PLACE), NameOf(object), children, false, false,
                                           selected, cells);
            dragSource(object);
            if (ImGui::IsItemClicked()) selectRow(object->GetID());
            if (doubleClicked() && !children) m_State.events.Post(ShowObjectSetup{object->GetID()});
            if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) m_State.events.Post(ShowObjectSetup{object->GetID()});
            RowMenu(object->GetID(), object->GetScriptCount() ? object->GetScript(0)->GetID() : 0);
            focusOn(object->GetID());
            if (expanded) {
                for (int i = 0; i < object->GetScriptCount(); ++i) scriptRow(object->GetScript(i), scene, false);
                if (inside != all.placed.end()) folderRows(inside->second, scene);
                endRow();
            }
            ImGui::PopID();
        }
    };
    // Worksets (editor-side; see EditorState.h).
    std::function<void(int, CKScene *)> worksetRows = [&](int parent, CKScene *scene) {
        for (const Workset &workset : std::vector<Workset>(m_State.worksets)) {
            if (workset.parent != parent) continue;
            ImGui::PushID(10000 + workset.id);
            bool hasChildren = !workset.members.empty();
            for (const Workset &other : m_State.worksets) hasChildren = hasChildren || other.parent == workset.id;
            Cells worksetCells;
            worksetCells.workset = workset.id;
            const bool open = beginRow("workset", ICON_WORKSETS, workset.name, hasChildren, true, true, false, worksetCells);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                m_Doc.levelMenuWorkset = workset.id;
                ImGui::OpenPopup("WorksetMenu");
            }
            if (m_Doc.levelMenuWorkset == workset.id) WorksetMenu(workset.id);
            if (open) {
                worksetRows(workset.id, scene);
                for (CK_ID member : workset.members) {
                    CKObject *object = context->GetObject(member);
                    if (CKBehavior *script = CKBehavior::Cast(object)) scriptRow(script, scene, true);
                    else if (CKSceneObject *sceneObject = CKSceneObject::Cast(object)) objectRow(sceneObject, scene, nullptr);
                }
                endRow();
            }
            ImGui::PopID();
        }
    };
    std::vector<CKObject *> levelScripts;
    for (int i = 0; level && i < level->GetScriptCount(); ++i) levelScripts.push_back(level->GetScript(i));
    std::sort(levelScripts.begin(), levelScripts.end(), rowLess);

    // Level section (documentation "Name Column"): Level Scripts, Global, Places, Worksets.
    if (level) {
        section = m_LevelMode;
        sectionScene = 0;
        ImGui::PushID("level");
        Cells cells;
        cells.layer = level->GetID();
        const bool levelOpen = beginRow("level", DevClassIconId(CKCID_LEVEL), NameOf(level, "<Unnamed Level>"), true, true,
                                        false, m_Doc.levelSelection.count(level->GetID()) != 0, cells);
        if (ImGui::IsItemClicked()) selectRow(level->GetID());
        RowMenu(level->GetID(), 0);
        if (levelOpen) {
            if (folderRow("levelscripts", "Level Scripts", levelScripts.empty(), ICON_LEVEL_SCRIPTS, idsOf(levelScripts))) {
                for (CKObject *script : levelScripts) scriptRow(CKBehavior::Cast(script), contextScene, false);
                endRow();
            }
            // Objects are listed with the flags of the context scene.
            const Contents all = contents(levelScene);
            if (folderRow("global", "Global", all.global.empty(), ICON_FOLDER, folderIds(all.global))) {
                folderRows(all.global, contextScene);
                endRow();
            }
            if (folderRow("places", "Places", all.places.empty(), ICON_PLACES, idsOf(all.places))) {
                placeRows(all, contextScene);
                endRow();
            }
            if (folderRow("worksets", "Worksets", m_State.worksets.empty(), ICON_WORKSETS)) {
                worksetRows(0, contextScene);
                endRow();
            }
            endRow();
        }
        ImGui::PopID();
    }
    // Scene sections, beside the level: Level Scripts (with the scene's
    // flags), Scene Scripts, Entities, Places, Worksets. Double click:
    // the Scene Setup (Launch Scene is in the row menu).
    for (CKScene *each : sceneRows) {
        if (!listed(each->GetID())) continue;
        section = !m_LevelMode && each->GetID() == m_Doc.contextScene;
        sectionScene = each->GetID();
        ImGui::PushID(static_cast<int>(each->GetID()));
        Cells cells;
        cells.layer = each->GetID();
        const bool expanded = beginRow("scene", DevClassIconId(CKCID_SCENE),
                                       NameOf(each, "<Unnamed Scene>") + (each == level->GetCurrentScene() ? "  (current)" : ""),
                                       true, false, false, m_Doc.levelSelection.count(each->GetID()) != 0, cells);
        if (ImGui::IsItemClicked()) selectRow(each->GetID());
        RowMenu(each->GetID(), 0);
        if (expanded) {
            if (folderRow("levelscripts", "Level Scripts", levelScripts.empty(), ICON_LEVEL_SCRIPTS, idsOf(levelScripts))) {
                for (CKObject *script : levelScripts) scriptRow(CKBehavior::Cast(script), each, false);
                endRow();
            }
            std::vector<CKObject *> sceneScripts;
            for (int i = 0; i < each->GetScriptCount(); ++i) sceneScripts.push_back(each->GetScript(i));
            if (m_LevelSort == 1) std::stable_sort(sceneScripts.begin(), sceneScripts.end(), rowLess);
            if (folderRow("scenescripts", "Scene Scripts", sceneScripts.empty(), ICON_LEVEL_SCRIPTS, idsOf(sceneScripts))) {
                for (CKObject *script : sceneScripts) scriptRow(CKBehavior::Cast(script), each, false);
                endRow();
            }
            const Contents all = contents(each);
            if (folderRow("entities", "Entities", all.global.empty(), ICON_FOLDER, folderIds(all.global))) {
                folderRows(all.global, each);
                endRow();
            }
            if (folderRow("places", "Places", all.places.empty(), ICON_PLACES, idsOf(all.places))) {
                placeRows(all, each);
                endRow();
            }
            // The worksets, with this scene's flags.
            if (folderRow("worksets", "Worksets", m_State.worksets.empty(), ICON_WORKSETS)) {
                worksetRows(0, each);
                endRow();
            }
            endRow();
        }
        ImGui::PopID();
    }
    m_LevelTreeOpen = -1;
    m_LevelBranchOpen = -1;
    m_Doc.levelFocus = 0;
    if (m_LevelDrop.pending) {
        LevelDrop drop = m_LevelDrop;
        m_LevelDrop = LevelDrop();
        // Several Data Resources files dragged: each dropped.
        ResourceFilePayload dropped = {};
        std::snprintf(dropped.path, sizeof(dropped.path), "%s", drop.path.c_str());
        dropped.cid = drop.cid;
        for (const ResourceFilePayload &each : m_State.DraggedResources(dropped)) {
            drop.path = each.path;
            drop.cid = each.cid;
            DropOnLevel(drop);
        }
    }
    if (m_LevelMove.pending) {
        const LevelMove move = m_LevelMove;
        m_LevelMove = LevelMove();
        MoveOnLevel(move);
    }
    if (m_LevelBlock.pending) {
        LevelBlockDrop drop = m_LevelBlock;
        m_LevelBlock = LevelBlockDrop();
        // Several Building Blocks dragged: each dropped, none asking.
        const std::vector<CKGUID> blocks = m_State.DraggedBlocks(drop.guid);
        drop.ask = blocks.size() == 1;
        for (const CKGUID &guid : blocks) {
            drop.guid = guid;
            DropBlockOnLevel(drop);
        }
    }
    if (m_LevelAttribute.pending) {
        const LevelAttributeDrop drop = m_LevelAttribute;
        m_LevelAttribute = LevelAttributeDrop();
        if (drop.row)
            m_State.DropAttributeOn(drop.selected ? LevelTargets() : std::vector<CK_ID>{drop.row}, drop.attribute);
        else
            m_State.Log(LogLevel::Warning, "Add Attribute: drop it on an object");
    }
    m_LevelRows = index;
    m_NavRows.swap(m_NavDrawn);
    m_NavDrawn.clear();
    // A row clicked: its selection (made by the click) is the keys'.
    if (m_NavClicked) {
        m_NavClicked = false;
        m_NavSelection = m_Doc.levelSelection;
    }
    // Dev's tree keys (documentation "Level Manager Keyboard Shortcuts").
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && !m_NavRows.empty() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        const int count = static_cast<int>(m_NavRows.size());
        const int current = NavCurrent();
        const float visible = std::max(rowHeight, ImGui::GetCurrentWindow()->InnerRect.GetHeight() - headerHeight);
        const int page = std::max(1, static_cast<int>(visible / rowHeight) - 1);
        int target = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) target = current < 0 ? 0 : std::min(count - 1, current + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) target = current < 0 ? 0 : std::max(0, current - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) target = std::min(count - 1, std::max(0, current) + page);
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) target = std::max(0, current - page);
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) target = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) target = count - 1;
        if (current >= 0 && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            const NavRow &row = m_NavRows[current];
            if (row.children && !row.open) ImGui::GetStateStorage()->SetBool(row.key, true);
            else if (row.children && current + 1 < count) target = current + 1;
        }
        if (current >= 0 && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            const NavRow &row = m_NavRows[current];
            if (row.children && row.open) {
                ImGui::GetStateStorage()->SetBool(row.key, false);
            } else {
                for (int i = current - 1; i >= 0 && target < 0; --i)
                    if (m_NavRows[i].depth < row.depth) target = i;
            }
        }
        if (target >= 0 && target != current) {
            const NavRow row = m_NavRows[target];
            m_NavKey = row.key;
            m_Doc.levelSelection.clear();
            if (row.object) m_Doc.levelSelection.insert(row.object);
            m_NavSelection = m_Doc.levelSelection;
            m_Doc.selectionScene = row.scene;
            m_Doc.selectionGroup = row.group;
            // As a click: a Script shows in the Schematic, an object its first one.
            CKObject *object = row.object ? context->GetObject(row.object) : nullptr;
            if (CKBehavior *script = CKBehavior::Cast(object)) {
                ShowScript(script->GetID());
            } else if (CKBeObject *beObject = CKBeObject::Cast(object)) {
                if (!CKLevel::Cast(object) && !CKScene::Cast(object) && !CKPlace::Cast(object) && beObject->GetScriptCount())
                    ShowScript(beObject->GetScript(0)->GetID());
            }
            // Scrolled to show it.
            const float top = rowHeight * target;
            if (top < scrollY) ImGui::SetScrollY(top);
            else if (top + rowHeight > scrollY + visible) ImGui::SetScrollY(top + rowHeight - visible);
        }
    }
    // The last row's line is black.
    if (index > 0) {
        const float last = rowsTop + rowHeight * index;
        tl->AddRectFilled(ImVec2(list.x, last - 1.0f), ImVec2(rowRight, last), vi::Color(0));
    }
    ImGui::PopClipRect();

    // The header over the rows, its columns' titles centered (VI font 0),
    // black lines at their ends and under it; a column's end drags.
    {
        static const char *const TITLES[9] = {"", "Name", "IC", "Visible", "Activate At Scene Start", "Reset At Scene Start",
                                              "Active Now", "Priority", ""};
        const float top = list.y + scrollY;
        tl->AddRectFilled(ImVec2(list.x, top), ImVec2(rowRight, top + headerHeight), vi::Color(6));
        for (int i = 0; i < 9; ++i) {
            // Name and Priority sort the rows; again, the other way.
            if (i == 1 || i == 7) {
                ImGui::SetCursorScreenPos(ImVec2(columnX[i] + 4.0f * s, top));
                ImGui::PushID(100 + i);
                if (ImGui::InvisibleButton("##sort", ImVec2(std::max(1.0f, columnX[i + 1] - columnX[i] - 8.0f * s), headerHeight))) {
                    const int sort = i == 7 ? 1 : 0;
                    m_LevelSortReversed = sort == m_LevelSort ? !m_LevelSortReversed : false;
                    m_LevelSort = sort;
                }
                ImGui::PopID();
                if ((i == 7) == (m_LevelSort == 1)) {
                    // The sorted column's mark.
                    const float x = columnX[i + 1] - 9.0f * s, y = top + headerHeight * 0.5f;
                    if (m_LevelSortReversed != (m_LevelSort == 1))
                        tl->AddTriangleFilled(ImVec2(x, y + 2.0f * s), ImVec2(x + 6.0f * s, y + 2.0f * s), ImVec2(x + 3.0f * s, y - 2.0f * s),
                                              vi::Color(3));
                    else
                        tl->AddTriangleFilled(ImVec2(x, y - 2.0f * s), ImVec2(x + 6.0f * s, y - 2.0f * s), ImVec2(x + 3.0f * s, y + 2.0f * s),
                                              vi::Color(3));
                }
            }
            const float width = VITextWidth(TITLES[i], false, s);
            const ImVec2 clip(columnX[i + 1] - 1.0f, top + headerHeight);
            DrawVIText(tl, ImVec2(IM_ROUND(std::max(columnX[i] + 1.0f, (columnX[i] + columnX[i + 1] - width) * 0.5f)),
                                  IM_ROUND(top + (headerHeight - 12.0f * s) * 0.5f)),
                       vi::Color(0), TITLES[i], false, s, &clip);
            tl->AddRectFilled(ImVec2(columnX[i + 1] - 1.0f, top), ImVec2(columnX[i + 1], top + headerHeight), vi::Color(0));
            ImGui::SetCursorScreenPos(ImVec2(columnX[i + 1] - 3.0f * s, top));
            ImGui::PushID(i);
            ImGui::InvisibleButton("##resize", ImVec2(6.0f * s, headerHeight));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive())
                m_LevelColumns[i] = std::max(10.0f, m_LevelColumns[i] + ImGui::GetIO().MouseDelta.x / s);
            ImGui::PopID();
        }
        tl->AddRectFilled(ImVec2(list.x, top + headerHeight - 1.0f), ImVec2(rowRight, top + headerHeight), vi::Color(0));
    }
    // The scroll range: the header and the rows.
    ImGui::SetCursorScreenPos(list);
    ImGui::Dummy(ImVec2(columnX[9] - list.x, headerHeight + rowHeight * index));
    ImGui::PopID();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace vdev

namespace vdev {

// Click selects one row; Ctrl+click adds or removes it.
// Dev.exe sub_56B380: the file by its folder's class (DropResourceFile); a
// behavior file dropped onto a selected row goes to every selected row
// (sub_56BAB0); an imported file asks about duplicate names first.
void EditorPanels::DropOnLevel(const LevelDrop &drop) {
    if (!m_State.HasDocument()) return;
    std::vector<LevelDropTarget> targets{drop.target};
    const bool behaviors = drop.cid <= 0 || CKIsChildClassOf(drop.cid, CKCID_BEHAVIOR);
    if (behaviors && drop.selected && m_Doc.levelSelection.size() > 1) {
        targets.clear();
        for (CK_ID row : m_Doc.levelSelection) {
            LevelDropTarget target;
            target.row = row;
            target.scene = drop.target.scene;
            targets.push_back(target);
        }
    }
    const std::string path = drop.path;
    const CK_CLASSID cid = drop.cid;
    for (const LevelDropTarget &target : targets) {
        auto load = [this, path, cid, target](const std::vector<DuplicateName> *duplicates) {
            if (!m_State.HasDocument()) return;
            const EditResult result = DropResourceFile(*m_State.document, path, cid, target, duplicates);
            m_State.Log(result.ok ? LogLevel::Info : LogLevel::Warning,
                        (result.ok ? std::string("Loaded: ") : std::string("Load failed: ")) + AnsiToUtf8(result.message) + " (" + path + ")");
            if (result.ok) m_Canvas.Invalidate();
        };
        if (!behaviors && ResourceFileIsImported(cid)) m_State.LoadAskingDuplicates(path, load);
        else load(nullptr);
    }
}

void EditorPanels::SelectRow(CK_ID id) {
    if (!ImGui::GetIO().KeyCtrl) m_Doc.levelSelection.clear();
    if (!m_Doc.levelSelection.insert(id).second && ImGui::GetIO().KeyCtrl) m_Doc.levelSelection.erase(id);
}

// Dev.exe sub_574C10: Activate At Scene Start goes A, D, Leave, A; Reset At
// Scene Start toggles R; Active Now activates or deactivates now. The rows
// selected all take the clicked one's next state.
void EditorPanels::FlagClick(CK_ID id, int column, CKScene *scene) {
    if (!column || !scene || !m_State.HasDocument()) return;
    CKSceneObject *object = CKSceneObject::Cast(m_State.document->Context()->GetObject(id));
    if (!object) return;
    const CKDWORD flags = scene->GetObjectFlags(object);
    const std::vector<CK_ID> targets = m_Doc.levelSelection.count(id) ? std::vector<CK_ID>(m_Doc.levelSelection.begin(), m_Doc.levelSelection.end())
                                                                  : std::vector<CK_ID>{id};
    if (column == 1) {
        CKDWORD add = CK_SCENEOBJECT_START_ACTIVATE, remove = CK_SCENEOBJECT_START_DEACTIVATE | CK_SCENEOBJECT_START_LEAVE;
        const char *label = "Activate At Scene Start";
        if (flags & CK_SCENEOBJECT_START_ACTIVATE) {
            add = CK_SCENEOBJECT_START_DEACTIVATE;
            remove = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_LEAVE;
            label = "Deactivate At Scene Start";
        } else if (flags & CK_SCENEOBJECT_START_DEACTIVATE) {
            add = CK_SCENEOBJECT_START_LEAVE;
            remove = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_DEACTIVATE;
            label = "Leave At Scene Start";
        } else if (!(flags & CK_SCENEOBJECT_START_LEAVE)) {
            // None set (shown as A): deactivate, as from A.
            add = CK_SCENEOBJECT_START_DEACTIVATE;
            remove = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_LEAVE;
            label = "Deactivate At Scene Start";
        }
        Report(ModifySceneFlags(*m_State.document, targets, add, remove, label, scene->GetID()), "Activate At Scene Start");
    } else if (column == 3) {
        Report(SetActiveNow(*m_State.document, targets, scene->GetID(), !(flags & CK_SCENEOBJECT_ACTIVE)), "Active Now");
    } else {
        const bool reset = !(flags & CK_SCENEOBJECT_START_RESET);
        Report(ModifySceneFlags(*m_State.document, targets, reset ? CK_SCENEOBJECT_START_RESET : 0,
                                reset ? 0 : CK_SCENEOBJECT_START_RESET, "Reset At Scene Start", scene->GetID()),
               "Reset At Scene Start");
    }
}

// Dev Level Manager popup (language pack [Level View]<Popup Menu>), the
// implemented part.
void EditorPanels::RowMenu(CK_ID id, CK_ID script, CK_ID group) {
    if (ImGui::IsItemVisible()) {
        // The row's name cell as shown (clipped to the table).
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const ImVec2 clipMin = dl->GetClipRectMin(), clipMax = dl->GetClipRectMax();
        m_PickRows.push_back({std::max(min.x, clipMin.x), std::max(min.y, clipMin.y), std::min(max.x, clipMax.x),
                              std::min(max.y, clipMax.y), id});
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        m_Doc.levelMenuGroup = group;
        if (!m_Doc.levelSelection.count(id)) {
            m_Doc.levelSelection.clear();
            m_Doc.levelSelection.insert(id);
        }
        m_Doc.levelMenuObject = id;
        ImGui::OpenPopup("LevelRowMenu");
    }
    if (m_Doc.levelMenuObject != id) return;
    PushDevMenuStyle();
    if (ImGui::BeginPopup("LevelRowMenu")) {
        const std::vector<CK_ID> targets(m_Doc.levelSelection.begin(), m_Doc.levelSelection.end());
        CKContext *context = m_State.document->Context();
        CKObject *object = context->GetObject(id);
        const bool beObject = CKBeObject::Cast(object) && !CKBehavior::Cast(object) && !CKScene::Cast(object) &&
                              !CKLevel::Cast(object);
        // Only Script rows have "Show in Schematic" (user request).
        if (script && CKBehavior::Cast(object) && ImGui::MenuItem("Show in Schematic")) {
            ShowScript(script);
            m_State.events.Post(FocusSchematic{});
        }
        if (ImGui::MenuItem("Setup", nullptr, false, object != nullptr)) m_State.events.Post(ShowObjectSetup{id});
        if (CKScene *scene = CKScene::Cast(object)) {
            CKLevel *level = context->GetCurrentLevel();
            if (ImGui::MenuItem("Launch Scene", nullptr, false, level && scene != level->GetCurrentScene())) {
                const CKERROR launched = level->LaunchScene(scene);
                m_State.Log(launched == CK_OK ? LogLevel::Info : LogLevel::Warning,
                            "Launch Scene " + NameOf(scene) + (launched == CK_OK ? "" : " failed"));
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Set Initial Conditions"))
            Report(ApplyInitialConditions(*m_State.document, targets, InitialConditions::Set),
                   "Set Initial Conditions");
        if (ImGui::MenuItem("Restore Initial Conditions"))
            Report(ApplyInitialConditions(*m_State.document, targets, InitialConditions::Restore),
                   "Restore Initial Conditions");
        if (ImGui::MenuItem("Remove Initial Conditions"))
            Report(ApplyInitialConditions(*m_State.document, targets, InitialConditions::Remove),
                   "Remove Initial Conditions");
        ImGui::Separator();
        if (ImGui::MenuItem("Activate", "A"))
            Report(ModifySceneFlags(*m_State.document, targets, CK_SCENEOBJECT_START_ACTIVATE,
                                    CK_SCENEOBJECT_START_DEACTIVATE | CK_SCENEOBJECT_START_LEAVE, "Activate", m_Doc.contextSceneId),
                   "Activate");
        if (ImGui::MenuItem("Deactivate", "D"))
            Report(ModifySceneFlags(*m_State.document, targets, CK_SCENEOBJECT_START_DEACTIVATE,
                                    CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_LEAVE, "Deactivate", m_Doc.contextSceneId),
                   "Deactivate");
        ImGui::Separator();
        CKLevel *level = context->GetCurrentLevel();
        if (ImGui::BeginMenu("Send To Scene", level && level->GetSceneCount() > 0)) {
            for (int i = 0; level && i < level->GetSceneCount(); ++i)
                if (CKScene *scene = level->GetScene(i))
                    if (ImGui::MenuItem(NameOf(scene, "<Unnamed Scene>").c_str()))
                        Report(SendToScene(*m_State.document, targets, scene->GetID()), "Send To Scene");
            ImGui::EndMenu();
        }
        const std::vector<CKObject *> groups = m_State.document->LiveObjects(CKCID_GROUP);
        if (ImGui::BeginMenu("Send To Group", !groups.empty())) {
            for (CKObject *group : groups)
                if (ImGui::MenuItem(NameOf(group).c_str()))
                    Report(SendToGroup(*m_State.document, targets, group->GetID()), "Send To Group");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Send to Place", level && level->GetPlaceCount() > 0)) {
            for (int i = 0; level && i < level->GetPlaceCount(); ++i)
                if (CKPlace *place = level->GetPlace(i))
                    if (ImGui::MenuItem(NameOf(place).c_str()))
                        Report(SendToPlace(*m_State.document, targets, place->GetID()), "Send to Place");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Remove From Place"))
            Report(RemoveFromPlace(*m_State.document, targets), "Remove From Place");
        if (ImGui::MenuItem("Place Selected in New Group", "G")) CreateGroupOf(targets, "Place Selected in New Group");
        if (ImGui::MenuItem("Place Selected in New Place", "P")) CreatePlaceOf(targets, "Place Selected in New Place");
        ImGui::Separator();
        if (ImGui::MenuItem("Show Selected in 3D Layout", "V"))
            Report(ShowObjects(*m_State.document, targets, true), "Show in 3D Layout");
        if (ImGui::MenuItem("Hide Selected in 3D Layout", "H"))
            Report(ShowObjects(*m_State.document, targets, false), "Hide in 3D Layout");
        if (m_Doc.levelMenuGroup) {
            if (ImGui::MenuItem("Move Up", "+")) Report(MoveInGroup(*m_State.document, m_Doc.levelMenuGroup, id, -1), "Move Up");
            if (ImGui::MenuItem("Move Down", "-")) Report(MoveInGroup(*m_State.document, m_Doc.levelMenuGroup, id, 1), "Move Down");
        }
        if (ImGui::BeginMenu("Send to Workset", !m_State.worksets.empty())) {
            for (const Workset &workset : std::vector<Workset>(m_State.worksets))
                if (ImGui::MenuItem(workset.name.c_str())) AddToWorkset(workset.id, targets);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Add Attributes", beObject)) {
            if (ImGui::MenuItem("Add Attribute...")) {
                m_State.events.Post(OpenAddAttribute{targets});
            }
            ImGui::Separator();
            AttributeMenu(targets);
            ImGui::EndMenu();
        }
        if (SaveOptionOf(object) >= 0 && ImGui::BeginMenu("Set Save Options")) {
            // CK_BITMAP_SAVEOPTIONS; sounds: CK_SOUND_SAVEOPTIONS.
            const char *const bitmaps[] = {"Raw Data", "External", "Image Format", "Use Global", "Include Original File"};
            const char *const sounds[] = {"External", "Include Original File", "Use Global"};
            const bool sound = CKSound::Cast(object) != nullptr;
            const int count = sound ? 3 : 5;
            for (int option = 0; option < count; ++option)
                if (ImGui::MenuItem(sound ? sounds[option] : bitmaps[option], nullptr, SaveOptionOf(object) == option))
                    Report(SetSaveOptions(*m_State.document, targets, option), "Set Save Options");
            ImGui::EndMenu();
        }
        // A Script: saved as a behavior file (.nms).
        if (CKBehavior::Cast(object) && ImGui::MenuItem("Save as...")) {
            std::vector<CK_ID> scripts;
            for (CK_ID target : targets)
                if (CKBehavior::Cast(context->GetObject(target))) scripts.push_back(target);
            m_Canvas.SaveBehaviorsAs(scripts, NameOf(object, "Script") + ".nms");
        }
        // An entity (one or several): Save as..., a .nmo with its Scripts
        // hidden or not; Save Objects as... only on a Scene or the Level.
        if (!CKBehavior::Cast(object) && !CKLevel::Cast(object) && !CKScene::Cast(object) && object &&
            ImGui::MenuItem("Save as...")) {
            m_Doc.deleteObjects.clear();
            for (CK_ID target : targets)
                if (CKObject *each = context->GetObject(target))
                    if (!CKBehavior::Cast(each) && !CKLevel::Cast(each) && !CKScene::Cast(each)) m_Doc.deleteObjects.push_back(target);
            m_SaveAsName = NameOf(object, "Objects");
            m_OpenSaveAs = true;
        }
        if ((CKLevel::Cast(object) || CKScene::Cast(object)) && ImGui::MenuItem("Save Objects as...")) {
            // The level: what it holds (its Scripts, scenes and objects), not itself.
            std::vector<CK_ID> saved;
            for (CK_ID target : targets) {
                CKLevel *each = CKLevel::Cast(context->GetObject(target));
                if (!each) {
                    saved.push_back(target);
                    continue;
                }
                for (int i = 0; i < each->GetScriptCount(); ++i)
                    if (each->GetScript(i)) saved.push_back(each->GetScript(i)->GetID());
                for (int i = 0; i < each->GetSceneCount(); ++i)
                    if (each->GetScene(i)) saved.push_back(each->GetScene(i)->GetID());
                if (CKScene *scene = each->GetLevelScene())
                    for (CKSceneObjectIterator it = scene->GetObjectIterator(); !it.End(); it++)
                        if (CKObject *held = context->GetObject(it.GetObjectID()))
                            if (held != each && !held->IsDynamic()) saved.push_back(held->GetID());
            }
            std::sort(saved.begin(), saved.end());
            saved.erase(std::unique(saved.begin(), saved.end()), saved.end());
            m_Doc.deleteObjects = saved; // the objects to save
            SaveObjectsDialog();
        }
        if (ImGui::MenuItem("Delete", "Delete", false, !CKLevel::Cast(object))) DeleteLevelRows(targets);
        ImGui::Separator();
        if (ImGui::MenuItem("Create Script", nullptr, false, beObject)) CreateScripts();
        if (ImGui::MenuItem("Load Script...", nullptr, false, beObject)) LoadScriptOn(id);
        if (ImGui::MenuItem("Rename", "F2", false, object != nullptr)) {
            m_Doc.renameObject = id;
            m_Doc.renameWorkset = 0;
            m_RenameText = NameOf(object, "");
            m_Doc.openRename = true;
        }
        // Dev.exe sub_573250: the selection becomes the children of the items
        // selected (their Scripts, a group's members), the 3D Layout's too.
        std::set<CK_ID> children;
        for (CK_ID target : targets) {
            CKBeObject *parent = CKBeObject::Cast(context->GetObject(target));
            if (!parent || CKBehavior::Cast(parent)) continue;
            for (int i = 0; i < parent->GetScriptCount(); ++i) children.insert(parent->GetScript(i)->GetID());
            if (CKGroup *members = CKGroup::Cast(parent))
                for (int i = 0; i < members->GetObjectCount(); ++i)
                    if (members->GetObject(i)) children.insert(members->GetObject(i)->GetID());
        }
        if (!children.empty()) {
            ImGui::Separator();
            if (ImGui::MenuItem("Select Children")) m_Doc.levelSelection = children;
        }
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
}

// Dev "Create Workset (W)": with the selected objects and Scripts.
void EditorPanels::CreateWorkset(int parent) {
    Workset workset;
    workset.id = m_State.nextWorkset++;
    workset.parent = parent;
    workset.name = "New Workset";
    if (!parent) workset.members = LevelTargets();
    m_State.worksets.push_back(workset);
    m_LevelTreeOpen = 1;
}

// The Scripts of a workset: its Scripts, its objects' Scripts, its sub-worksets'.
std::vector<CK_ID> EditorPanels::WorksetScripts(int id) const {
    std::vector<CK_ID> scripts;
    if (!m_State.HasDocument()) return scripts;
    CKContext *context = m_State.document->Context();
    for (const Workset &workset : m_State.worksets) {
        if (workset.id == id) {
            for (CK_ID member : workset.members) {
                CKObject *object = context->GetObject(member);
                if (CKBehavior *script = CKBehavior::Cast(object)) scripts.push_back(script->GetID());
                else if (CKBeObject *owner = CKBeObject::Cast(object))
                    for (int i = 0; i < owner->GetScriptCount(); ++i) scripts.push_back(owner->GetScript(i)->GetID());
            }
        } else if (workset.parent == id) {
            const std::vector<CK_ID> nested = WorksetScripts(workset.id);
            scripts.insert(scripts.end(), nested.begin(), nested.end());
        }
    }
    return scripts;
}

// Dev workset popup: "Create Workset", "Show/Hide Workset Scripts in Schematic".
void EditorPanels::WorksetMenu(int id) {
    PushDevMenuStyle();
    if (ImGui::BeginPopup("WorksetMenu")) {
        if (ImGui::MenuItem("Create Workset")) CreateWorkset(id);
        if (ImGui::MenuItem("Show Workset Scripts in Schematic")) {
            for (CK_ID script : WorksetScripts(id)) m_State.hiddenScripts.erase(script);
            m_State.events.Post(RefreshScriptStack{});
        }
        if (ImGui::MenuItem("Hide Workset Scripts in Schematic")) {
            for (CK_ID script : WorksetScripts(id)) m_State.hiddenScripts.insert(script);
            m_State.events.Post(RefreshScriptStack{});
        }
        if (ImGui::MenuItem("Add Selection", nullptr, false, !m_Doc.levelSelection.empty())) AddToWorkset(id, LevelTargets());
        if (ImGui::MenuItem("Remove Selection From Workset", nullptr, false, !m_Doc.levelSelection.empty()))
            for (Workset &workset : m_State.worksets)
                if (workset.id == id)
                    workset.members.erase(std::remove_if(workset.members.begin(), workset.members.end(),
                                                         [&](CK_ID member) { return m_Doc.levelSelection.count(member) != 0; }),
                                          workset.members.end());
        if (ImGui::MenuItem("Rename", "F2")) {
            m_Doc.renameWorkset = id;
            m_Doc.renameObject = 0;
            for (const Workset &workset : m_State.worksets)
                if (workset.id == id) m_RenameText = workset.name;
            m_Doc.openRename = true;
        }
        if (ImGui::MenuItem("Delete")) {
            std::set<int> doomed{id};
            for (bool grew = true; grew;) {
                grew = false;
                for (const Workset &workset : m_State.worksets)
                    if (doomed.count(workset.parent) && doomed.insert(workset.id).second) grew = true;
            }
            m_State.worksets.erase(std::remove_if(m_State.worksets.begin(), m_State.worksets.end(),
                                                  [&](const Workset &workset) { return doomed.count(workset.id) != 0; }),
                                   m_State.worksets.end());
        }
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
}

// Dev "Create Script (S)": a new Script on each selected object.
void EditorPanels::CreateScripts() {
    if (!m_State.HasDocument()) return;
    size_t steps = 0;
    CK_ID last = 0;
    for (const EditResult &result : CreateScriptsOn(*m_State.document, LevelTargets())) {
        Report(result, "Create Script");
        if (result.ok) {
            ++steps;
            last = result.created;
        }
    }
    if (last) ShowScript(last);
    if (!steps && m_Doc.levelSelection.empty()) m_State.Log(LogLevel::Info, "Create Script: select objects first");
}

void EditorPanels::LoadScriptOn(CK_ID object) {
    m_State.RequestFile(ScriptFileDialog(), [this, object](const FileDialogResult &selection) {
        if (selection.status != FileDialogResult::Status::Selected) return;
        const EditResult result = LoadScripts(*m_State.document, selection.paths.front(), object);
        Report(result, "Load Script");
        if (result.ok) ShowScript(result.created);
    });
}

// Dev "Add Attributes": the attribute types by category, those the
// selection's classes accept (hidden attributes are left out).
void EditorPanels::AttributeMenu(const std::vector<CK_ID> &targets) {
    CKContext *context = m_State.document->Context();
    CKAttributeManager *attributes = context->GetAttributeManager();
    if (!attributes) return;
    std::map<std::string, std::vector<CKAttributeType>> categories;
    for (CKAttributeType type = 0; type < attributes->GetAttributeCount(); ++type) {
        if (!attributes->IsAttributeIndexValid(type) || (attributes->GetAttributeFlags(type) & CK_ATTRIBUT_HIDDEN)) continue;
        const CK_CLASSID compatible = attributes->GetAttributeCompatibleClassId(type);
        bool accepted = false;
        for (CK_ID id : targets)
            if (CKBeObject *object = CKBeObject::Cast(context->GetObject(id)))
                accepted = accepted || !compatible || CKIsChildClassOf(object, compatible);
        if (!accepted) continue;
        const char *category = attributes->GetAttributeCategory(type);
        categories[category && *category ? category : "(No category)"].push_back(type);
    }
    if (categories.empty()) ImGui::TextDisabled("No attribute applies to the selection");
    for (const auto &category : categories)
        if (ImGui::BeginMenu(AnsiToUtf8(category.first).c_str())) {
            for (CKAttributeType type : category.second) {
                const char *name = attributes->GetAttributeNameByType(type);
                ImGui::PushID(type);
                if (ImGui::MenuItem(AnsiToUtf8(name ? name : "?").c_str())) {
                    std::vector<std::pair<std::string, bool>> each;
                    const EditResult result = AddAttribute(*m_State.document, targets, type, &each);
                    m_State.LogAttributeAdds(name, each, result.ok);
                    if (!result.ok && each.empty()) Report(result, "Add Attributes");
                }
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
}

// Dev "Save Objects as..." (.nmo).
void EditorPanels::SaveAsDialog() {
    const char *id = "Save as###LevelSaveAs";
    if (m_OpenSaveAs) {
        ImGui::OpenPopup(id);
        m_OpenSaveAs = false;
    }
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("%d object(s) saved as a Virtools file (.nmo)", static_cast<int>(m_Doc.deleteObjects.size()));
    // Protects the work: the Scripts open hidden in the Schematic (in the file only).
    ImGui::Checkbox("Hide script representation in Schematic view", &m_SaveAsHide);
    if (ImGui::Button("Save...", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        ImGui::CloseCurrentPopup();
        FileDialogOptions options;
        options.save = true;
        options.filter = L"Virtools Files (*.nmo)\0*.nmo\0All files (*.*)\0*.*\0";
        options.extension = L"nmo";
        const std::wstring initial = Utf8ToWide(m_SaveAsName) + L".nmo";
        options.initial = initial.c_str();

        const auto objects = m_Doc.deleteObjects;
        const bool hide = m_SaveAsHide;
        m_State.RequestFile(options, [this, objects, hide](const FileDialogResult &selection) {
            if (selection.status != FileDialogResult::Status::Selected) return;
            std::string error;
            const std::string &path = selection.paths.front();
            if (SaveObjectsAs(*m_State.document, objects, path, &error, hide))
                m_State.Log(LogLevel::Info, "Saved " + std::to_string(objects.size()) + " object(s) to " + path);
            else m_State.Log(LogLevel::Error, "Save as: " + error);
        });
        m_Doc.deleteObjects.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_Doc.deleteObjects.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorPanels::SaveObjectsDialog() {
    FileDialogOptions options;
    options.save = true;
    options.filter = L"Virtools Files (*.nmo)\0*.nmo\0All files (*.*)\0*.*\0";
    options.extension = L"nmo";
    options.initial = L"Objects.nmo";

    const auto objects = m_Doc.deleteObjects;
    m_State.RequestFile(options, [this, objects](const FileDialogResult &selection) {
        if (selection.status != FileDialogResult::Status::Selected) return;
        std::string error;
        const std::string &path = selection.paths.front();
        if (SaveObjectsAs(*m_State.document, objects, path, &error))
            m_State.Log(LogLevel::Info, "Saved " + std::to_string(objects.size()) + " object(s) to " + path);
        else m_State.Log(LogLevel::Error, "Save Objects as: " + error);
    });
    m_Doc.deleteObjects.clear();
}

// Dev Level Manager Delete: destroys the objects; the undo history is
// cleared (Dev: "Undo stack will be cleared").
// Dev.exe sub_5BF7B0 (mode 1): "Select Delete Options", the session's
// delete dependencies kept with OK.
// The dialog: Dev's Edit Parameter without its name and type (options 16
// and 32) titled "Select Delete Options", on a "Delete Options" parameter
// (CK2UI's Dependencies UI).
void EditorPanels::DeleteObjectsDialog() {
    const char *id = "Select Delete Options###DeleteOptions";
    if (m_Doc.openDeleteObjects) {
        m_Doc.openDeleteObjects = false;
        m_DeleteOptions = m_State.DeleteDependencies();
        m_DeleteBox.Clear();
        m_DeleteBox.AddDependencies("Delete Options", CKPGUID_DELETEDEPENDENCIES, m_DeleteOptions);
        ImGui::OpenPopup(id);
    }
    const DialogAction action = EditParameterValueDialog(id, "Select Delete Options", m_DeleteBox);
    if (action == DialogAction::None) return;
    const bool ok = action == DialogAction::Ok && m_DeleteBox.Dependencies(0, m_DeleteOptions);
    m_DeleteBox.Clear();
    if (ok) {
        m_State.selection.Clear();
        m_State.DeleteDependencies() = m_DeleteOptions;
        const EditResult result =
            DestroyLevelObjects(*m_State.document, m_Doc.deleteObjects, &m_State.DeleteDependencies());
        Report(result, "Delete");
        if (result.ok) m_Doc.levelSelection.clear();
    }
    m_Doc.deleteObjects.clear();
}

void EditorPanels::RenameDialog() {
    if (m_Doc.openRename) {
        ImGui::OpenPopup("Rename##level");
        m_Doc.openRename = false;
    }
    if (!ImGui::BeginPopupModal("Rename##level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    char buffer[256];
    strncpy(buffer, m_RenameText.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    const bool enter = ImGui::InputText("##name", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue);
    m_RenameText = buffer;
    if (enter || ImGui::Button("OK", ImVec2(100, 0))) {
        if (m_Doc.renameWorkset) {
            for (Workset &workset : m_State.worksets)
                if (workset.id == m_Doc.renameWorkset) workset.name = m_RenameText;
            m_Doc.renameWorkset = 0;
        } else {
            Report(RenameLevelObject(*m_State.document, m_Doc.renameObject, Utf8ToAnsi(m_RenameText)), "Rename");
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Nothing to do (EditStatus::NoChange) is not reported.
void EditorPanels::Report(const EditResult &result, const char *what) {
    if (result.Unchanged()) return;
    m_State.Log(result.ok ? LogLevel::Info : LogLevel::Warning,
                std::string(what) + (result.ok ? ": " : " refused: ") + AnsiToUtf8(result.message));
}

// ---------------------------------------------------------------- drops, Delete

namespace {

std::string LevelNamed(CKObject *object) { return AnsiToUtf8(object && object->GetName() ? object->GetName() : ""); }

std::string LevelMessage(const char *format, const std::string &a, const std::string &b = std::string()) {
    std::string text = format;
    const size_t first = text.find("%s");
    if (first != std::string::npos) text.replace(first, 2, a);
    const size_t second = text.find("%s");
    if (second != std::string::npos) text.replace(second, 2, b);
    return text;
}

} // namespace

// The selection when the row dragged is in it (Dev drags the selected rows),
// else the row; onto the row (Dev.exe sub_56E790): a workset takes them; a
// Script's row and a folder are their owner's / parent's; then by the
// class: scene, level, place, group, else the object (its Scripts) and the
// row it is in (sub_572D20).
void EditorPanels::MoveOnLevel(const LevelMove &move) {
    if (!m_State.HasDocument()) return;
    CKContext *context = m_State.document->Context();
    std::vector<CK_ID> objects =
        m_Doc.levelSelection.count(move.dragged) ? LevelTargets() : std::vector<CK_ID>{move.dragged};
    objects.erase(std::remove_if(objects.begin(), objects.end(), [context](CK_ID id) { return !context->GetObject(id); }),
                  objects.end());
    if (objects.empty()) return;
    if (move.workset) {
        AddToWorkset(move.workset, objects);
        return;
    }
    if (move.copy) {
        // The Scripts copied onto the object (or the level) dropped on, the
        // objects into the container (Copy options first).
        std::vector<CKObject *> rows;
        if (CKObject *row = context->GetObject(move.target.row)) rows.push_back(row);
        for (CK_ID above : move.target.above)
            if (CKObject *row = context->GetObject(above)) rows.push_back(row);
        while (!rows.empty() && CKBehavior::Cast(rows.front())) rows.erase(rows.begin());
        if (rows.empty()) return;
        std::vector<CK_ID> scripts, others;
        for (CK_ID id : objects) (CKBehavior::Cast(context->GetObject(id)) ? scripts : others).push_back(id);
        if (!scripts.empty()) {
            CKBeObject *owner = CKBeObject::Cast(rows.front());
            if (owner && !CKScene::Cast(owner) && !CKGroup::Cast(owner) && !CKPlace::Cast(owner))
                for (const EditResult &pasted : PasteScriptsOn(*m_State.document, scripts, {owner->GetID()}, true))
                    Report(pasted, "Copy Script");
        }
        if (others.empty()) return;
        CKObject *container = rows.front();
        if (!CKScene::Cast(container) && !CKLevel::Cast(container) && !CKPlace::Cast(container) && !CKGroup::Cast(container))
            container = rows.size() > 1 && !move.inWorkset ? rows[1] : nullptr;
        if (!container) return;
        m_CopyObjects = others;
        m_CopyContainer = container->GetID();
        m_OpenCopyOptions = true;
        return;
    }
    std::vector<CKObject *> rows;
    if (CKObject *row = context->GetObject(move.target.row)) rows.push_back(row);
    for (CK_ID above : move.target.above)
        if (CKObject *row = context->GetObject(above)) rows.push_back(row);
    while (!rows.empty() && CKBehavior::Cast(rows.front())) rows.erase(rows.begin());
    if (rows.empty()) return;
    CKObject *target = rows.front();
    if (CKScene *scene = CKScene::Cast(target)) {
        MoveOnScene(scene, objects);
        return;
    }
    if (CKLevel *level = CKLevel::Cast(target)) {
        // The level (sub_572440): 3D entities leave their place, Scripts
        // become the level's.
        std::vector<CK_ID> placed, scripts;
        for (CK_ID id : objects) {
            CKObject *object = context->GetObject(id);
            if (CK3dEntity *entity = CK3dEntity::Cast(object)) {
                if (entity->GetReferencePlace() && !CKPlace::Cast(entity)) placed.push_back(id);
            } else if (CKBehavior::Cast(object)) {
                scripts.push_back(id);
            }
        }
        if (!placed.empty()) Report(RemoveFromPlace(*m_State.document, placed), "Remove From Place");
        MoveScriptsOn(level, scripts, nullptr);
        return;
    }
    if (CKPlace *place = CKPlace::Cast(target)) {
        MoveOnPlace(place, objects);
        return;
    }
    if (CKGroup *group = CKGroup::Cast(target)) {
        MoveOnGroup(group, objects);
        return;
    }
    // An object: the Scripts go to it (sub_56E6E0), the others into the row
    // it is in (not a workset's): a scene, the level, a place, a group.
    std::vector<CK_ID> scripts, others;
    for (CK_ID id : objects) (CKBehavior::Cast(context->GetObject(id)) ? scripts : others).push_back(id);
    if (CKBeObject *beObject = CKBeObject::Cast(target))
        MoveScriptsOn(beObject, scripts, "Incompatible Class, cannot copy script <%s> to object <%s> !");
    if (others.empty() || move.inWorkset || rows.size() < 2) return;
    CKObject *container = rows[1];
    if (CKScene *scene = CKScene::Cast(container)) {
        std::vector<CK_ID> absent;
        for (CK_ID id : others)
            if (CKSceneObject *object = CKSceneObject::Cast(context->GetObject(id)))
                if (!object->IsInScene(scene)) absent.push_back(id);
        if (!absent.empty()) Report(SendToScene(*m_State.document, absent, scene->GetID()), "Send To Scene");
    } else if (CKLevel::Cast(container)) {
        std::vector<CK_ID> placed;
        for (CK_ID id : others)
            if (CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(id)))
                if (entity->GetReferencePlace() && !CKPlace::Cast(entity)) placed.push_back(id);
        if (!placed.empty()) Report(RemoveFromPlace(*m_State.document, placed), "Remove From Place");
    } else if (CKPlace *place = CKPlace::Cast(container)) {
        std::vector<CK_ID> entities;
        for (CK_ID id : others)
            if (CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(id)))
                if (entity->GetReferencePlace() != place && !CKPlace::Cast(entity)) entities.push_back(id);
        if (!entities.empty()) Report(SendToPlace(*m_State.document, entities, place->GetID()), "Send to Place");
    } else if (CKGroup *group = CKGroup::Cast(container)) {
        std::vector<CK_ID> members;
        for (CK_ID id : others)
            if (CKBeObject *object = CKBeObject::Cast(context->GetObject(id)))
                if (!object->IsInGroup(group) && object != group) members.push_back(id);
        if (!members.empty()) Report(SendToGroup(*m_State.document, members, group->GetID()), "Send To Group");
    }
}

void EditorPanels::MoveScriptsOn(CKBeObject *target, const std::vector<CK_ID> &scripts, const char *refusal) {
    if (scripts.empty() || !target) return;
    std::vector<std::string> refused;
    const EditResult result = MoveScripts(*m_State.document, scripts, target->GetID(), &refused);
    for (const std::string &name : refused)
        m_State.Log(LogLevel::Warning, refusal ? LevelMessage(refusal, AnsiToUtf8(name), LevelNamed(target))
                                               : "Incompatible class: " + AnsiToUtf8(name));
    if (result.ok || refused.empty()) Report(result, "Move Script");
}

// A scene (sub_572AF0): the Scripts its class takes (1527); the objects,
// none in a place (1536: the drop stops there); a group with its members
// when the answer is Yes ("Add every Object in Group to Scene ?").
void EditorPanels::MoveOnScene(CKScene *scene, const std::vector<CK_ID> &objects) {
    CKContext *context = m_State.document->Context();
    std::vector<CK_ID> scripts, others;
    bool groups = false;
    for (CK_ID id : objects) {
        CKObject *object = context->GetObject(id);
        if (!CKSceneObject::Cast(object)) continue;
        if (CKBehavior::Cast(object)) {
            scripts.push_back(id);
            continue;
        }
        CK3dEntity *entity = CK3dEntity::Cast(object);
        if (entity && entity->GetReferencePlace() && !CKPlace::Cast(entity)) {
            m_State.Log(LogLevel::Warning, "Cannot add Object to scene, it belongs to a place !");
            break;
        }
        if (CKGroup *group = CKGroup::Cast(object)) groups = groups || group->GetObjectCount() > 0;
        if (!CKSceneObject::Cast(object)->IsInScene(scene)) others.push_back(id);
    }
    MoveScriptsOn(scene, scripts, "Cannot move script <%s> on scene, incompatible class !");
    if (others.empty()) return;
    const CK_ID sceneId = scene->GetID();
    if (!groups) {
        Report(SendToScene(*m_State.document, others, sceneId), "Add To Scene");
        return;
    }
    Ask("Add Group to Scene", "Add every Object in Group to Scene ?", [this, others, sceneId](bool yes) {
        if (m_State.HasDocument()) Report(SendToScene(*m_State.document, others, sceneId, yes), "Add To Scene");
    });
}

// A place (sub_5727C0): 3D entities become its children (1524: already
// there), the Scripts its class takes (1525), nothing else (1526).
void EditorPanels::MoveOnPlace(CKPlace *place, const std::vector<CK_ID> &objects) {
    CKContext *context = m_State.document->Context();
    std::vector<CK_ID> entities, scripts;
    for (CK_ID id : objects) {
        CKObject *object = context->GetObject(id);
        if (!object || CKPlace::Cast(object)) continue;
        if (CK3dEntity *entity = CK3dEntity::Cast(object)) {
            if (entity->GetReferencePlace() == place)
                m_State.Log(LogLevel::Warning, LevelMessage("Object <%s> already in Place !", LevelNamed(object)));
            else
                entities.push_back(id);
        } else if (CKBehavior::Cast(object)) {
            scripts.push_back(id);
        } else {
            m_State.Log(LogLevel::Warning,
                        LevelMessage("Cannot move object <%s> on place, can only add object derived from 3D Entity !", LevelNamed(object)));
        }
    }
    if (!entities.empty()) Report(SendToPlace(*m_State.document, entities, place->GetID()), "Send to Place");
    MoveScriptsOn(place, scripts, "Cannot move script <%s> on place, incompatible class !");
}

// A group (sub_572560): the behavioral objects join it, the Scripts its
// class takes go to it (1523).
void EditorPanels::MoveOnGroup(CKGroup *group, const std::vector<CK_ID> &objects) {
    CKContext *context = m_State.document->Context();
    std::vector<CK_ID> members, scripts;
    for (CK_ID id : objects) {
        CKObject *object = context->GetObject(id);
        if (CKBehavior::Cast(object)) scripts.push_back(id);
        else if (CKBeObject *beObject = CKBeObject::Cast(object))
            if (beObject != group && !beObject->IsInGroup(group) && !CKLevel::Cast(object) && !CKScene::Cast(object))
                members.push_back(id);
    }
    if (!members.empty()) Report(SendToGroup(*m_State.document, members, group->GetID()), "Send To Group");
    MoveScriptsOn(group, scripts, "Cannot move script <%s> on group, incompatible class !");
}

void EditorPanels::AddToWorkset(int id, const std::vector<CK_ID> &objects) {
    if (!m_State.HasDocument()) return;
    CKContext *context = m_State.document->Context();
    CKLevel *level = context->GetCurrentLevel();
    CKScene *scene = level ? level->GetLevelScene() : nullptr;
    for (Workset &workset : m_State.worksets) {
        if (workset.id != id) continue;
        for (CK_ID member : objects) {
            CKObject *object = context->GetObject(member);
            if (!object) continue;
            CKSceneObject *sceneObject = CKSceneObject::Cast(object);
            if (sceneObject && scene && !CKLevel::Cast(object) && !CKScene::Cast(object) && !sceneObject->IsInScene(scene)) {
                m_State.Log(LogLevel::Warning,
                            LevelMessage("Cannot add <%s> to the Scene Workset if the object is not referenced in the scene !", LevelNamed(object)));
            } else if (std::find(workset.members.begin(), workset.members.end(), member) != workset.members.end()) {
                m_State.Log(LogLevel::Warning, LevelMessage("<%s> already present in Workset !", LevelNamed(object)));
            } else {
                workset.members.push_back(member);
            }
        }
    }
}

// Dev's Delete (sub_5734A0) by the section of the rows: in a scene's, the
// objects leave the scene (a group: with its members when the answer is
// Yes), its Scripts and scenes are destroyed; an object's Script is
// deleted in the level (1534), a group's member stays (1528). In the
// level's, a group's members leave it; the rest is destroyed.
void EditorPanels::DeleteLevelRows(const std::vector<CK_ID> &targets) {
    if (!m_State.HasDocument() || targets.empty()) return;
    CKContext *context = m_State.document->Context();
    CKLevel *level = context->GetCurrentLevel();
    CKScene *scene = CKScene::Cast(context->GetObject(m_Doc.selectionScene));
    CKGroup *group = CKGroup::Cast(context->GetObject(m_Doc.selectionGroup));
    if (scene && level && scene != level->GetLevelScene()) {
        if (group) {
            m_State.Log(LogLevel::Warning, "Cannot remove an entity from a group in a scene, please remove it from level !");
            return;
        }
        std::vector<CK_ID> destroy, leave, sceneScripts;
        bool groups = false, scripts = false;
        for (CK_ID id : targets) {
            CKObject *object = context->GetObject(id);
            if (CKBehavior *script = CKBehavior::Cast(object)) {
                if (CKScene::Cast(script->GetOwner())) sceneScripts.push_back(id);
                else scripts = true;
            } else if (CKScene::Cast(object)) {
                destroy.push_back(id);
            } else if (CKSceneObject::Cast(object) && !CKLevel::Cast(object)) {
                leave.push_back(id);
                if (CKGroup *member = CKGroup::Cast(object)) groups = groups || member->GetObjectCount() > 0;
            }
        }
        if (scripts) m_State.Log(LogLevel::Warning, "Please delete object scripts in the level.");
        DeleteScriptsAsked(sceneScripts);
        const CK_ID sceneId = scene->GetID();
        if (!leave.empty()) {
            if (groups)
                Ask("Remove Group from Scene", "Remove every Object in Group from Scene ?", [this, leave, sceneId](bool yes) {
                    if (m_State.HasDocument()) Report(RemoveFromScene(*m_State.document, leave, sceneId, yes), "Remove From Scene");
                });
            else
                Report(RemoveFromScene(*m_State.document, leave, sceneId, true), "Remove From Scene");
        }
        if (!destroy.empty()) {
            m_Doc.deleteObjects = destroy;
            m_Doc.openDeleteObjects = true;
        }
        return;
    }
    // Scripts (documentation: Delete "Deletes the script"), after Dev's
    // confirmation; the others with the Delete Options.
    std::vector<CK_ID> rest, scripts;
    for (CK_ID id : targets) (CKBehavior::Cast(context->GetObject(id)) ? scripts : rest).push_back(id);
    if (!scripts.empty()) DeleteScriptsAsked(scripts);
    if (rest.empty()) return;
    if (group) {
        const std::vector<CK_ID> members = rest;
        if (!members.empty()) Report(RemoveGroupMembers(*m_State.document, group->GetID(), members), "Remove From Group");
        return;
    }
    m_Doc.deleteObjects = rest;
    m_Doc.openDeleteObjects = true;
}

// Create Group (sub_576910): of the behavioral objects (1535 for the
// others), in the current scene too when it is not the level's.
void EditorPanels::CreateGroupOf(const std::vector<CK_ID> &targets, const char *what) {
    if (!m_State.HasDocument()) return;
    CKContext *context = m_State.document->Context();
    for (CK_ID id : targets) {
        CKObject *object = context->GetObject(id);
        if (object && !CKBeObject::Cast(object))
            m_State.Log(LogLevel::Warning, LevelMessage("Cannot add Object <%s> to group, incompatible class !", LevelNamed(object)));
    }
    CKScene *current = context->GetCurrentScene();
    Report(CreateGroup(*m_State.document, "", targets, current ? current->GetID() : 0), what);
}

// Create Place (sub_573F10): "Place Created: %s".
void EditorPanels::CreatePlaceOf(const std::vector<CK_ID> &targets, const char *what) {
    if (!m_State.HasDocument()) return;
    const EditResult result = CreatePlace(*m_State.document, "", targets);
    Report(result, what);
    if (result.ok) m_State.Log(LogLevel::Info, LevelMessage("Place Created: %s", LevelNamed(m_State.document->Context()->GetObject(result.created))));
}

void EditorPanels::Ask(const std::string &title, const std::string &text, std::function<void(bool yes)> answer) {
    m_LevelQuestion.open = true;
    m_LevelQuestion.title = title;
    m_LevelQuestion.text = text;
    m_LevelQuestion.answer = std::move(answer);
}

void EditorPanels::QuestionDialog() {
    const std::string id = m_LevelQuestion.title + "###LevelQuestion";
    if (m_LevelQuestion.open) {
        m_LevelQuestion.open = false;
        ImGui::OpenPopup(id.c_str());
    }
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(m_LevelQuestion.text.c_str());
    int answer = -1;
    if (ImGui::Button("Yes", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) answer = 1;
    ImGui::SameLine();
    if (ImGui::Button("No", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) answer = 0;
    if (answer >= 0) {
        std::function<void(bool)> reply = std::move(m_LevelQuestion.answer);
        m_LevelQuestion.answer = nullptr;
        ImGui::CloseCurrentPopup();
        if (reply) reply(answer == 1);
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- copy, paste, find

void EditorPanels::CopyOnLevel() {
    if (!m_State.HasDocument()) return;
    CKContext *context = m_State.document->Context();
    const std::vector<CK_ID> targets = LevelTargets();
    std::vector<CK_ID> objects, scripts;
    for (CK_ID id : targets) {
        CKObject *object = context->GetObject(id);
        if (!object || CKLevel::Cast(object)) continue;
        CKBehavior *script = CKBehavior::Cast(object);
        if (!script) objects.push_back(id);
        else if (script->GetFlags() & CKBEHAVIOR_SCRIPT) scripts.push_back(id);
    }
    if (!objects.empty()) {
        m_State.objectClipboard = objects;
        m_State.objectsCopied = true;
        m_State.Log(LogLevel::Info, "Copied " + std::to_string(objects.size()) + " object(s)");
    } else if (!scripts.empty()) {
        m_State.scriptClipboard = scripts;
        m_State.objectsCopied = false;
        m_State.Log(LogLevel::Info, "Copied " + std::to_string(scripts.size()) + " Script(s)");
    }
}

void EditorPanels::PasteOnLevel() {
    if (!m_State.HasDocument()) return;
    CKContext *context = m_State.document->Context();
    CKLevel *level = context->GetCurrentLevel();
    const std::vector<CK_ID> targets = LevelTargets();
    if (targets.empty()) {
        m_State.Log(LogLevel::Info, "Paste: select rows in the Level Manager");
        return;
    }
    auto alive = [&](const std::vector<CK_ID> &ids) {
        std::vector<CK_ID> kept;
        for (CK_ID id : ids)
            if (context->GetObject(id) && !m_State.document->IsDiscarded(id)) kept.push_back(id);
        return kept;
    };
    if (m_State.objectsCopied) {
        const std::vector<CK_ID> objects = alive(m_State.objectClipboard);
        if (objects.empty()) return;
        // The containers selected; another row: its section (a scene, or
        // the level: copies).
        std::vector<CK_ID> containers;
        bool copies = false;
        for (CK_ID id : targets) {
            CKObject *row = context->GetObject(id);
            if (CKScene::Cast(row) || CKGroup::Cast(row) || CKPlace::Cast(row)) {
                if (std::find(containers.begin(), containers.end(), id) == containers.end()) containers.push_back(id);
                continue;
            }
            CKScene *section = CKScene::Cast(context->GetObject(m_Doc.selectionScene));
            if (section && level && section != level->GetLevelScene()) {
                if (std::find(containers.begin(), containers.end(), section->GetID()) == containers.end())
                    containers.push_back(section->GetID());
            } else {
                copies = true;
            }
        }
        EditGroup group(*m_State.document, "Paste");
        if (copies) Report(DuplicateObjects(*m_State.document, objects), "Paste");
        for (CK_ID id : containers) {
            CKObject *container = context->GetObject(id);
            std::vector<CK_ID> chosen;
            for (CK_ID object : objects) {
                CKObject *o = context->GetObject(object);
                if (CKGroup *g = CKGroup::Cast(container)) {
                    if (CKBeObject::Cast(o) && !CKGroup::Cast(o) && !CKBehavior::Cast(o) && !CKBeObject::Cast(o)->IsInGroup(g))
                        chosen.push_back(object);
                } else if (CKPlace *place = CKPlace::Cast(container)) {
                    CK3dEntity *entity = CK3dEntity::Cast(o);
                    if (entity && !CKPlace::Cast(o) && entity->GetReferencePlace() != place) chosen.push_back(object);
                } else if (CKScene *scene = CKScene::Cast(container)) {
                    CKSceneObject *sceneObject = CKSceneObject::Cast(o);
                    if (sceneObject && !CKBehavior::Cast(o) && !sceneObject->IsInScene(scene)) chosen.push_back(object);
                }
            }
            if (chosen.empty()) continue;
            if (CKGroup::Cast(container)) Report(SendToGroup(*m_State.document, chosen, id), "Paste");
            else if (CKPlace::Cast(container)) Report(SendToPlace(*m_State.document, chosen, id), "Paste");
            else Report(SendToScene(*m_State.document, chosen, id), "Paste");
        }
        return;
    }
    // Scripts: onto the objects selected their class takes; a Building
    // Block is not a Script ("Can only paste scripts on objects !").
    std::vector<CK_ID> scripts;
    bool blocks = false;
    for (CK_ID id : alive(m_State.scriptClipboard)) {
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(id));
        if (behavior && (behavior->GetFlags() & CKBEHAVIOR_SCRIPT)) scripts.push_back(id);
        else blocks = true;
    }
    if (blocks) m_State.Log(LogLevel::Warning, "Can only paste scripts on objects !");
    if (scripts.empty()) return;
    std::vector<CK_ID> owners;
    for (CK_ID id : targets)
        if (CKBeObject::Cast(context->GetObject(id)) && !CKBehavior::Cast(context->GetObject(id))) owners.push_back(id);
    size_t steps = 0;
    for (const EditResult &pasted : PasteScriptsOn(*m_State.document, scripts, owners, true)) {
        Report(pasted, "Paste Script");
        if (pasted.ok) ++steps;
    }
    if (!steps && owners.empty()) m_State.Log(LogLevel::Info, "Paste Script: select objects in the Level Manager");
}

// The rows in the tree's order: the level (its Scripts, its objects by
// folder with their Scripts, its places), then each scene the same way.
std::vector<std::pair<CK_ID, CK_ID>> EditorPanels::LevelSearchOrder() {
    std::vector<std::pair<CK_ID, CK_ID>> order;
    if (!m_State.HasDocument()) return order;
    CKContext *context = m_State.document->Context();
    CKLevel *level = context->GetCurrentLevel();
    if (!level) return order;
    const bool showDynamic = m_State.preferences.Bool("3D Layout\\Show Dynamic Objects");
    auto withScripts = [&](CKObject *object, CK_ID section) {
        order.push_back({object->GetID(), section});
        if (CKBeObject *beObject = CKBeObject::Cast(object))
            for (int i = 0; i < beObject->GetScriptCount(); ++i)
                if (CKBehavior *script = beObject->GetScript(i)) order.push_back({script->GetID(), section});
    };
    auto contents = [&](CKScene *scene, CK_ID section) {
        const SceneContents all = ListSceneContents(context, scene, level, showDynamic);
        auto folders = [&](const LevelFolders &list) {
            for (const auto &folder : list) {
                std::vector<CKSceneObject *> objects = folder.second;
                std::sort(objects.begin(), objects.end(), [this](CKObject *x, CKObject *y) { return LevelLess(x, y); });
                for (CKSceneObject *object : objects) withScripts(object, section);
            }
        };
        folders(all.global);
        for (CKBeObject *place : all.places) {
            withScripts(place, section);
            auto inside = all.placed.find(place->GetID());
            if (inside != all.placed.end()) folders(inside->second);
        }
    };
    order.push_back({level->GetID(), 0});
    std::vector<CKObject *> scripts;
    for (int i = 0; i < level->GetScriptCount(); ++i)
        if (level->GetScript(i)) scripts.push_back(level->GetScript(i));
    std::sort(scripts.begin(), scripts.end(), [this](CKObject *x, CKObject *y) { return LevelLess(x, y); });
    for (CKObject *script : scripts) order.push_back({script->GetID(), 0});
    contents(level->GetLevelScene(), 0);
    std::vector<CKScene *> scenes;
    for (int i = 0; i < level->GetSceneCount(); ++i)
        if (CKScene *scene = level->GetScene(i)) scenes.push_back(scene);
    std::sort(scenes.begin(), scenes.end(), [this](CKObject *x, CKObject *y) { return LevelLess(x, y); });
    for (CKScene *scene : scenes) {
        withScripts(scene, scene->GetID());
        contents(scene, scene->GetID());
    }
    return order;
}

void EditorPanels::FindOnLevel(bool forward) {
    if (!m_State.HasDocument() || m_FindText.empty()) {
        if (!m_FindText.empty() || !m_State.HasDocument()) return;
        m_OpenFind = true;
        return;
    }
    CKContext *context = m_State.document->Context();
    const std::vector<std::pair<CK_ID, CK_ID>> order = LevelSearchOrder();
    if (order.empty()) return;
    // From the current row (the one found last when it is still selected).
    int start = -1;
    if (m_FindIndex >= 0 && m_FindIndex < static_cast<int>(order.size()) && m_Doc.levelSelection.count(order[m_FindIndex].first))
        start = m_FindIndex;
    else if (!m_Doc.levelSelection.empty())
        for (size_t i = 0; i < order.size() && start < 0; ++i)
            if (m_Doc.levelSelection.count(order[i].first)) start = static_cast<int>(i);
    std::string wanted = Utf8ToAnsi(m_FindText);
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const int count = static_cast<int>(order.size());
    for (int step = 1; step <= count; ++step) {
        const int i = ((start < 0 ? (forward ? -1 : 0) : start) + (forward ? step : -step) + count * 2) % count;
        CKObject *object = context->GetObject(order[i].first);
        if (!object) continue;
        const bool script = CKBehavior::Cast(object) != nullptr;
        if (script ? !m_FindScripts : !m_FindObjects) continue;
        std::string name = object->GetName() ? object->GetName() : "";
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name.find(wanted) == std::string::npos) continue;
        m_FindIndex = i;
        m_Doc.levelSelection = {order[i].first};
        m_Doc.selectionScene = order[i].second;
        m_Doc.selectionGroup = 0;
        m_Doc.levelFocus = order[i].first;
        return;
    }
#ifdef _WIN32
    MessageBeep(MB_ICONEXCLAMATION);
#else
    if (SDL_Window *window = SDL_GetKeyboardFocus()) SDL_FlashWindow(window, SDL_FLASH_BRIEFLY);
#endif
    m_State.Log(LogLevel::Info, "Find Item: '" + m_FindText + "' not found");
}

// Dev's Find Item (an Edit Parameter of Find, Objects, Scripts).
void EditorPanels::FindDialog() {
    if (m_OpenFind) {
        m_OpenFind = false;
        ImGui::OpenPopup("Find Item###LevelFind");
    }
    if (!ImGui::BeginPopupModal("Find Item###LevelFind", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(260);
    const bool entered = ImGui::InputText("Find", &m_FindText, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Checkbox("Objects", &m_FindObjects);
    ImGui::SameLine();
    ImGui::Checkbox("Scripts", &m_FindScripts);
    if (ImGui::Button("OK", ImVec2(80, 0)) || entered) {
        ImGui::CloseCurrentPopup();
        m_FindIndex = -1;
        FindOnLevel(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- keys, Building Blocks

int EditorPanels::NavCurrent() const {
    if (m_Doc.levelSelection == m_NavSelection)
        for (size_t i = 0; i < m_NavRows.size(); ++i)
            if (m_NavRows[i].key == m_NavKey) return static_cast<int>(i);
    for (size_t i = 0; i < m_NavRows.size(); ++i)
        if (m_NavRows[i].object && m_Doc.levelSelection.count(m_NavRows[i].object)) return static_cast<int>(i);
    return -1;
}

// Dev.exe sub_56BAB0, sub_56B590 (a Script: "Cannot drop on script with
// hidden graph!"), sub_56B950 (an object), sub_578C30 (asked for its inputs
// when alone).
void EditorPanels::DropBlockOnLevel(const LevelBlockDrop &drop) {
    if (!m_State.HasDocument()) return;
    CKContext *context = m_State.document->Context();
    if (!drop.row) {
        m_State.Log(LogLevel::Warning, "Can't assign behavior: drop target is not an entity !");
        return;
    }
    const std::vector<CK_ID> rows = drop.selected ? LevelTargets() : std::vector<CK_ID>{drop.row};
    CKObjectDeclaration *declaration = CKGetObjectDeclarationFromGuid(drop.guid);
    const std::string blockName = AnsiToUtf8(declaration && declaration->GetName() ? declaration->GetName() : "");
    const CKGUID guid = drop.guid;
    auto incompatible = [&](CKObject *owner, const EditResult &result) {
        m_State.Log(LogLevel::Warning, "Incompatible Class, cannot add behavior <" + blockName + "> to object <" +
                                           (owner ? NameOf(owner) : std::string()) + ">: " + AnsiToUtf8(result.message));
    };
    auto dropAll = [&]() -> std::pair<CK_ID, CK_ID> {
        CK_ID script = 0, block = 0;
        for (CK_ID id : rows) {
            CKObject *object = context->GetObject(id);
            if (CKBehavior *behavior = CKBehavior::Cast(object)) {
                if (m_State.hiddenScripts.count(id)) {
                    m_State.Log(LogLevel::Warning, "Cannot drop on script with hidden graph!");
                    continue;
                }
                const EditResult inserted = DropBuildingBlockInto(*m_State.document, id, guid);
                if (inserted.ok) {
                    script = id;
                    block = inserted.created;
                } else {
                    incompatible(behavior->GetOwner(), inserted);
                }
            } else if (CKBeObject::Cast(object)) {
                CK_ID into = 0;
                bool insertRefused = false;
                const EditResult dropped =
                    DropBuildingBlockOn(*m_State.document, id, guid, m_State.hiddenScripts, &into, &insertRefused);
                if (dropped.ok) {
                    script = into;
                    block = dropped.created;
                } else if (insertRefused) {
                    incompatible(object, dropped);
                } else {
                    m_State.Log(LogLevel::Warning, "Cannot create script, item is not an entity ! (" + AnsiToUtf8(dropped.message) + ")");
                }
            }
        }
        return {script, block};
    };
    std::pair<CK_ID, CK_ID> made;
    if (rows.size() == 1 && dropAsking && drop.ask) {
        made = dropAsking(dropAll);
    } else {
        EditGroup group(*m_State.document, "Drop Behavior");
        made = dropAll();
    }
    if (!made.second) return;
    m_State.RememberBlock(guid);
    m_State.events.Post(ShowElement{made.first, ElementRef{ElementKind::Behavior, made.first, made.second}, made.second});
}

// ---------------------------------------------------------------- sort, priority, copies, Script deletion

int LevelPriorityOf(CKObject *object) {
    if (CKBehavior *behavior = CKBehavior::Cast(object)) return behavior->GetPriority();
    if (CKBeObject *beObject = CKBeObject::Cast(object)) return beObject->GetPriority();
    return 0;
}

bool EditorPanels::LevelLess(CKObject *a, CKObject *b) const {
    if (m_LevelSort == 1) {
        // By priority, the highest first (it runs first), then by name.
        const int pa = LevelPriorityOf(a), pb = LevelPriorityOf(b);
        if (pa != pb) return m_LevelSortReversed ? pa < pb : pa > pb;
        return NameLess(a, b);
    }
    return m_LevelSortReversed ? NameLess(b, a) : NameLess(a, b);
}

void EditorPanels::PriorityDialog() {
    const char *id = "Priority###LevelPriority";
    if (m_OpenPriority) {
        m_OpenPriority = false;
        ImGui::OpenPopup(id);
    }
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    CKObject *object = m_State.HasDocument() ? m_State.document->Context()->GetObject(m_PriorityObject) : nullptr;
    ImGui::Text("Priority of %s", object ? NameOf(object).c_str() : "");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(120);
    const bool entered = ImGui::InputInt("##priority", &m_PriorityValue, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("OK", ImVec2(80, 0)) || entered) {
        if (object) Report(SetPriorities(*m_State.document, {m_PriorityObject}, m_PriorityValue), "Set Priority");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// The Copy options (CKPGUID_COPYDEPENDENCIES, the session's, as the 3D
// Layout's Duplicate), then the copies: the level's (Global), a place's, a
// scene's (in the level too), a group's.
void EditorPanels::CopyOptionsDialog() {
    const char *id = "Edit Parameter###LevelCopyOptions";
    if (m_OpenCopyOptions) {
        m_OpenCopyOptions = false;
        m_CopyBox.Clear();
        m_CopyBox.AddDependencies("Copy options", CKPGUID_COPYDEPENDENCIES, m_State.CopyDependencies());
        ImGui::OpenPopup(id);
    }
    const DialogAction action = EditParameterValueDialog(id, "Edit Parameter", m_CopyBox);
    if (action == DialogAction::None) return;
    CKDependencies options = m_State.CopyDependencies();
    const bool copy = action == DialogAction::Ok && m_CopyBox.Dependencies(0, options);
    m_CopyBox.Clear();
    const std::vector<CK_ID> objects = std::move(m_CopyObjects);
    m_CopyObjects.clear();
    if (!copy || !m_State.HasDocument() || objects.empty()) return;
    m_State.CopyDependencies() = options;
    CKContext *context = m_State.document->Context();
    CKObject *container = context->GetObject(m_CopyContainer);
    if (!container) return;
    EditGroup group(*m_State.document, "Copy");
    std::vector<std::pair<CK_ID, CK_ID>> copies;
    const EditResult made = DuplicateObjects(*m_State.document, objects, &copies, &m_State.CopyDependencies());
    if (!made.ok) {
        Report(made, "Copy");
        return;
    }
    std::vector<CK_ID> fresh;
    for (const auto &each : copies) fresh.push_back(each.second);
    if (fresh.empty()) return;
    if (CKPlace *place = CKPlace::Cast(container)) {
        std::vector<CK_ID> entities;
        for (CK_ID id : fresh)
            if (CK3dEntity::Cast(context->GetObject(id)) && !CKPlace::Cast(context->GetObject(id))) entities.push_back(id);
        if (!entities.empty()) Report(SendToPlace(*m_State.document, entities, place->GetID()), "Copy");
    } else if (CKScene *scene = CKScene::Cast(container)) {
        CKLevel *level = context->GetCurrentLevel();
        if (!level || scene != level->GetLevelScene()) Report(SendToScene(*m_State.document, fresh, scene->GetID()), "Copy");
    } else if (CKGroup *target = CKGroup::Cast(container)) {
        std::vector<CK_ID> members;
        for (CK_ID id : fresh)
            if (CKBeObject::Cast(context->GetObject(id)) && id != target->GetID()) members.push_back(id);
        if (!members.empty()) Report(SendToGroup(*m_State.document, members, target->GetID()), "Copy");
    }
    m_State.Log(LogLevel::Info, "Copied " + std::to_string(fresh.size()) + " object(s)");
}

void EditorPanels::DeleteScriptsAsked(const std::vector<CK_ID> &scripts) {
    if (!m_State.HasDocument() || scripts.empty()) return;
    CKObject *first = m_State.document->Context()->GetObject(scripts.front());
    const std::string text = scripts.size() == 1 ? "Delete the Script <" + NameOf(first) + ">?"
                                                 : "Delete the " + std::to_string(scripts.size()) + " Scripts selected?";
    Ask("Confirm Script(s) Deletion", text, [this, scripts](bool yes) {
        if (!yes || !m_State.HasDocument()) return;
        EditGroup group(*m_State.document, scripts.size() > 1 ? "Delete Scripts" : "Delete Script");
        for (CK_ID script : scripts)
            if (m_State.document->Context()->GetObject(script)) Report(DeleteScript(*m_State.document, script), "Delete Script");
        for (CK_ID script : scripts) m_Doc.levelSelection.erase(script);
    });
}

} // namespace vdev

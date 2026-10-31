// Dev's Unused Object Explorer, Building
// Block Version Test, Behavior Link
// Properties and About windows; layouts after the Dev 2.5 screenshots.

#include "ui/tools/ToolWindows.h"


#include <algorithm>
#include <cstring>
#include <map>

#include "imgui.h"
#include "imgui_internal.h"

#include "CKAll.h"

#include "ui/controls/DevTheme.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/FileDialogs.h"
#include "core/level/LevelCommands.h"
#include "core/graph/ReplaceCommands.h"
#include "ui/controls/ObjectText.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

std::string NameOf(CKObject *object, const char *none = "") { return DisplayName(object, none); }

// Dev dialogs end with right-aligned buttons.
bool RightButton(const char *label, float width = 90.0f) {
    return ImGui::Button(label, ImVec2(width, 0));
}

// A modal popup centered on the main viewport.
bool BeginDevModal(const char *name, bool *openFlag, bool canOpen, const ImVec2 &size, ImGuiWindowFlags flags = 0) {
    // canOpen: focus changes of the frame would close a popup opened now.
    if (*openFlag && canOpen) {
        ImGui::OpenPopup(name);
        *openFlag = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (size.x > 0) ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    return ImGui::BeginPopupModal(name, nullptr, flags);
}

} // namespace

void ToolWindows::Draw(bool openPopups) {
    m_OpenPopups = openPopups;
    GeneralPreferences();
    UnusedObjectExplorer();
    VersionTestDialog();
    AboutDialog();
    AllObjectsDialog();
    LinkPropertiesDialog();
}

void ToolWindows::CloseManagers() {
    m_ShowPreferences = false;
}

void ToolWindows::DocumentClosed() {
    m_Doc = DocumentState();
}

// ------------------------------------------------------ Unused Object Explorer

void ToolWindows::OpenUnusedObjects() {
    if (!m_State.HasDocument()) return;
    std::string error;
    m_Doc.unused = UnusedObjects(*m_State.document, &error);
    m_Doc.excluded.clear();
    // Without the walk everything would look unused: no list to delete from.
    if (!error.empty()) {
        m_State.Log(LogLevel::Warning, "Unused Object Explorer: " + AnsiToUtf8(error));
        return;
    }
    m_OpenUnused = true;
}

// Dev.exe sub_58C870: the unused objects under their class (Mesh, Material,
// Texture, Animation, Object Animation, CurvePoint); columns Name and
// Exclude; Delete destroys the objects not excluded.
void ToolWindows::UnusedObjectExplorer() {
    if (!BeginDevModal("Unused Object Explorer", &m_OpenUnused, m_OpenPopups, ImVec2(420, 360))) return;
    if (!m_State.HasDocument()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    CKContext *context = m_State.document->Context();
    std::map<CK_CLASSID, std::vector<const UnusedObject *>> byClass;
    for (const UnusedObject &object : m_Doc.unused)
        if (context->GetObject(object.id)) byClass[object.cid].push_back(&object);
    const float footer = ImGui::GetFrameHeightWithSpacing() + 6;
    if (ImGui::BeginTable("unused", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, -footer))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Exclude", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        for (const auto &entry : byClass) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const char *cls = CKClassIDToString(entry.first);
            if (!ImGui::TreeNodeEx(cls ? cls : "?", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns))
                continue;
            for (const UnusedObject *object : entry.second) {
                ImGui::PushID(static_cast<int>(object->id));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Indent();
                ImGui::TextUnformatted(object->name.empty() ? "<Unnamed>" : AnsiToUtf8(object->name).c_str());
                ImGui::Unindent();
                ImGui::TableSetColumnIndex(1);
                const bool excluded = m_Doc.excluded.count(object->id) != 0;
                if (ImGui::Selectable(excluded ? "A" : "-", excluded)) {
                    if (excluded) m_Doc.excluded.erase(object->id);
                    else m_Doc.excluded.insert(object->id);
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::EndTable();
    }
    if (m_Doc.unused.empty()) ImGui::TextDisabled("No unused object");
    else ImGui::Text("%d unused object(s)", static_cast<int>(m_Doc.unused.size()));
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 2 * 90 - ImGui::GetStyle().ItemSpacing.x);
    ImGui::BeginDisabled(m_Doc.unused.size() == m_Doc.excluded.size());
    if (RightButton("Delete")) {
        std::vector<CK_ID> doomed;
        for (const UnusedObject &object : m_Doc.unused)
            if (!m_Doc.excluded.count(object.id) && context->GetObject(object.id)) doomed.push_back(object.id);
        const EditResult result = DestroyLevelObjects(*m_State.document, doomed);
        m_State.Log(result.ok ? LogLevel::Info : LogLevel::Warning, "Unused Object Explorer: deleted " + AnsiToUtf8(result.message));
        m_Doc.unused.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (RightButton("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ------------------------------------------------- Building Block Version Test

void ToolWindows::RunVersionTest() {
    if (!m_State.HasDocument()) return;
    m_Doc.versionTest = TestBuildingBlockVersions(*m_State.document);
    m_State.Log(LogLevel::Info, AnsiToUtf8(m_Doc.versionTest.report));
    m_OpenVersionTest = true;
}

// Dev's report; with changed prototypes it asks whether to replace them
// (each replacement is one undo step, ReplaceBuildingBlock).
void ToolWindows::VersionTestDialog() {
    if (!BeginDevModal("Building Block Version Test", &m_OpenVersionTest, m_OpenPopups, ImVec2(0, 0),
                       ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::PushTextWrapPos(520.0f);
    ImGui::TextUnformatted(AnsiToUtf8(m_Doc.versionTest.report).c_str());
    if (!m_Doc.versionTest.changed.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Would you like to replace building blocks whose prototype have changed?\n"
                               "(warning: the composition may not work if you choose yes, you should have to "
                               "update some links manually)");
    }
    ImGui::PopTextWrapPos();
    if (!m_Doc.versionTest.changed.empty()) {
        if (ImGui::Button("Yes", ImVec2(90, 0))) {
            int replaced = 0;
            CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
            for (const auto &target : m_Doc.versionTest.changed) {
                CKBehavior *behavior = context ? CKBehavior::Cast(context->GetObject(target.second)) : nullptr;
                if (!behavior) continue;
                const EditResult result = ReplaceBuildingBlock(*m_State.document, target.first,
                                                               target.second, behavior->GetPrototypeGuid());
                if (result.ok) ++replaced;
                else m_State.Log(LogLevel::Warning, "Replace " + NameOf(behavior) + ": " + AnsiToUtf8(result.message));
            }
            m_State.Log(LogLevel::Info, std::to_string(replaced) + " Building Block(s) replaced");
            m_Doc.versionTest.changed.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    } else if (ImGui::Button("OK", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
               ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------- Behavior Link Properties

// The rows are Dev's Edit Parameters dialog's (parameters of the values,
// edited on the box's copies; nothing of the composition is changed).
void ToolWindows::OpenLinkProperties() {
    CKContext *context = m_State.host.Context();
    if (!context) return;
    CKBehaviorManager *behaviors = context->GetBehaviorManager();
    const int delay = m_State.preferences.defaultLinkDelay;
    const CKBOOL prevent = m_State.preferences.autoLoopLinkDelay ? TRUE : FALSE;
    const int iterations =
        behaviors ? behaviors->GetBehaviorMaxIteration() : m_State.preferences.Int("BehaviorManager\\MaxIteration");
    m_LinkBox.Clear();
    auto row = [&](const char *name, const CKGUID &type, int value) {
        CKParameterOut *parameter = context->CreateCKParameterOut(const_cast<CKSTRING>(name), type, TRUE);
        if (!parameter) return;
        parameter->SetValue(&value);
        m_LinkBox.Add(parameter, name);
        context->DestroyObject(parameter); // the box edits its own copy
    };
    row("Default Link Delay", CKPGUID_INT, delay);
    row("Prevent Infinite Loop", CKPGUID_BOOL, prevent);
    row("Max Behavioral Iterations", CKPGUID_INT, iterations);
    m_OpenLinkProperties = m_LinkBox.Count() == 3;
}

// The Schematic toolbar's Behavior Link Properties ([Schematic]<Dialog>
// "Default Link Delay", "Prevent Infinite Loop", "Max Behavioral Iterations"):
// Dev's Edit Parameters dialog of three rows, OK and Cancel.
void ToolWindows::LinkPropertiesDialog() {
    const char *id = "###BehaviorLinkProperties";
    if (m_OpenLinkProperties && m_OpenPopups) {
        ImGui::OpenPopup(id);
        m_OpenLinkProperties = false;
    }
    const DialogAction action = EditParametersDialog(id, "Behavior Link Properties", m_LinkBox, false);
    if (action == DialogAction::Ok && m_LinkBox.Count() == 3) {
        auto value = [&](size_t row) {
            int v = 0;
            if (CKParameter *work = m_LinkBox.Work(row)) work->GetValue(&v);
            return v;
        };
        Preferences &p = m_State.preferences;
        p.defaultLinkDelay = std::max(0, std::min(32764, value(0)));
        p.autoLoopLinkDelay = value(1) != 0;
        p.SetInt("BehaviorManager\\MaxIteration", std::max(1, value(2)));
        if (applyPreferences) applyPreferences();
    }
    if (action == DialogAction::Ok || action == DialogAction::Cancel) m_LinkBox.Clear();
}

// ---------------------------------------------------------------- About

void ToolWindows::AboutDialog() {
    if (!BeginDevModal("About Virtools", &m_OpenAbout, m_OpenPopups, ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("NeMoDev");
    ImGui::TextUnformatted("An editor for Virtools compositions using the Ballanced runtime.");
    ImGui::Separator();
    ImGui::Text("CK version %x", static_cast<unsigned>(CKGetVersion()));
    ImGui::Text("Runtime: %s", AnsiToUtf8(m_State.runtimeDirectory).c_str());
    const RuntimePluginCounts &counts = m_State.host.Counts();
    ImGui::Text("%d Building Block DLLs, %d prototypes, %d managers", counts.behaviorDlls, counts.behaviorPrototypes,
                counts.managerDlls);
    ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ToolWindows::OpenAllObjects() {
    CKContext *context = m_State.host.Context();
    if (!context) return;
    m_MemoryObjects.clear();
    m_MemoryTotal = 0;
    CKScene *scene = context->GetCurrentScene();
    const XObjectPointerArray &objects = context->GetObjectListByType(CKCID_OBJECT, TRUE);
    for (int i = 0; i < objects.Size(); ++i) {
        CKObject *object = objects[i];
        if (!object) continue;
        MemoryObject row;
        row.name = AnsiToUtf8(object->GetName() ? object->GetName() : "");
        const char *className = CKClassIDToString(object->GetClassID());
        row.className = AnsiToUtf8(className ? className : "");
        row.id = object->GetID();
        row.size = context->GetObjectSize(object);
        row.render = CKIsChildClassOf(object, CKCID_RENDEROBJECT) != FALSE;
        row.visible = row.render && object->IsVisible();
        row.dynamic = object->IsDynamic() != FALSE;
        row.sceneObject = scene && CKIsChildClassOf(object, CKCID_SCENEOBJECT);
        row.inScene = row.sceneObject && static_cast<CKSceneObject *>(object)->IsInScene(scene);
        m_MemoryTotal += row.size;
        m_MemoryObjects.push_back(std::move(row));
    }
    m_OpenAllObjects = true;
}

// Dev's template 135 (387 x 421 units): the list (7, 7, 373 x 387) in report
// view with Name, Class (centered), ID, Size, Visi., Dyn. and
// IsInCurrentScene of 130, 120, 30, 60, 30, 30, 60 pixels; under it
// "Total Object Size = %d" (8, 400) and "Selected Objects (%d) Size = %d"
// (177, 400); OK. A header clicked sorts by its column, again the other way
// (sub_5BB8D0); a click selects a row, Ctrl+click adds or takes it,
// Shift+click the rows from the last one clicked.
void ToolWindows::AllObjectsDialog() {
    const char *id = "###AllObjects";
    if (m_OpenAllObjects && m_OpenPopups) {
        m_OpenAllObjects = false;
        m_MemorySort = -1;
        m_MemoryAscending = true;
        m_MemoryAnchor = -1;
        ImGui::OpenPopup(id);
    }
    if (!ImGui::IsPopupOpen(id)) return;
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 units(387.0f, 421.0f);
    const float pad = 2.0f;
    ImVec2 o;
    if (!vi::BeginDialog(id, "All CK Object In Memory", vi::TemplateClient(units, s, pad), s, o)) return;
    const vi::TemplateLayout l = vi::LayoutTemplate(o, units, s, pad);
    vi::TemplatePanel(l);
    std::vector<vi::ListColumn> columns{{"Name", 130.0f},          {"Class", 120.0f, vi::CENTER}, {"ID", 30.0f},
                                       {"Size", 60.0f},          {"Visi.", 30.0f},              {"Dyn.", 30.0f},
                                       {"IsInCurrentScene", 60.0f}};
    for (vi::ListColumn &column : columns) column.width *= s;
    int header = -1;
    const int clicked = vi::ListView(
        "##objects", l.Min(7, 7), l.Max(7, 7, 373, 387), s, columns, static_cast<int>(m_MemoryObjects.size()),
        [&](int r, int c) -> std::string {
            const MemoryObject &row = m_MemoryObjects[r];
            switch (c) {
            case 0: return row.name;
            case 1: return row.className;
            case 2: return std::to_string(row.id);
            case 3: return std::to_string(row.size);
            case 4: return row.visible ? "X" : "";
            case 5: return row.dynamic ? "X" : "";
            default: return row.inScene ? "X" : "";
            }
        },
        [&](int r) { return m_MemoryObjects[r].selected; }, nullptr, nullptr, false, &header);
    if (clicked >= 0) {
        const ImGuiIO &io = ImGui::GetIO();
        if (io.KeyShift && m_MemoryAnchor >= 0) {
            for (int r = 0; r < static_cast<int>(m_MemoryObjects.size()); ++r)
                m_MemoryObjects[r].selected = r >= std::min(clicked, m_MemoryAnchor) && r <= std::max(clicked, m_MemoryAnchor);
        } else if (io.KeyCtrl) {
            m_MemoryObjects[clicked].selected = !m_MemoryObjects[clicked].selected;
            m_MemoryAnchor = clicked;
        } else {
            for (MemoryObject &row : m_MemoryObjects) row.selected = false;
            m_MemoryObjects[clicked].selected = true;
            m_MemoryAnchor = clicked;
        }
    }
    if (header >= 0) {
        m_MemoryAscending = header == m_MemorySort ? !m_MemoryAscending : true;
        m_MemorySort = header;
        const bool ascending = m_MemoryAscending;
        std::stable_sort(m_MemoryObjects.begin(), m_MemoryObjects.end(), [header, ascending](const MemoryObject &a, const MemoryObject &b) {
            int order = 0;
            switch (header) {
            case 0: order = _stricmp(a.name.c_str(), b.name.c_str()); break;
            case 1: order = _stricmp(a.className.c_str(), b.className.c_str()); break;
            case 2: order = a.id < b.id ? -1 : a.id > b.id ? 1 : 0; break;
            case 3: order = a.size < b.size ? -1 : a.size > b.size ? 1 : 0; break;
            case 4: order = static_cast<int>(b.visible) - static_cast<int>(a.visible); break;
            case 5: order = static_cast<int>(b.dynamic) - static_cast<int>(a.dynamic); break;
            default: order = static_cast<int>(b.inScene) - static_cast<int>(a.inScene); break;
            }
            return ascending ? order < 0 : order > 0;
        });
        m_MemoryAnchor = -1;
    }
    int count = 0, size = 0;
    for (const MemoryObject &row : m_MemoryObjects)
        if (row.selected) {
            ++count;
            size += row.size;
        }
    char line[96];
    std::snprintf(line, sizeof(line), "Total Object Size = %d", m_MemoryTotal);
    vi::Text(l.Min(8, 400), l.Max(8, 400, 163, 14), line);
    std::snprintf(line, sizeof(line), "Selected Objects (%d) Size = %d", count, size);
    vi::Text(l.Min(177, 400), l.Max(177, 400, 203, 14), line);
    if (vi::DialogButton("OK", ImVec2(l.right - 60.0f * s, l.buttonsY), s, ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_MemoryObjects.clear();
        ImGui::CloseCurrentPopup();
    }
    vi::EndDialog();
}

} // namespace vdev

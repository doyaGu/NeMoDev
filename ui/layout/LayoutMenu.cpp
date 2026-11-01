// The 3D Layout's shortcut menu (Dev.exe sub_4C1BB0) and the commands it
// runs (sub_4B95C0, WM_COMMAND).

#include "ui/layout/LayoutWindow.h"
#include <cstdio>
#include <cstdlib>

#include <algorithm>

#include "ui/controls/ValueEditor.h"

namespace vdev {

void LayoutWindow::Execute(const char *id, const std::vector<EditorValue> &arguments, AfterCommand after) {
    const auto client = Client();
    const auto result = client.Command(id, arguments);
    if (result.status == EDITOR_QUEUED) m_Commands.push_back({result.operation, client.Scope().document, after});
    else Complete(result, after);
}
void LayoutWindow::Complete(const EditorResponse &result, AfterCommand after) {
    if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) {
        if (!result.message.empty()) m_State.Log(LogLevel::Warning, result.message);
        return;
    }
    const auto *created = EditorClient::Find(result, "created");
    if (after == AfterCommand::ShowScript && created && created->type == EDITOR_OBJECT && created->object)
        ShowScriptInSchematic(created->object);
    if (after == AfterCommand::ScriptsAdded)
        if (const auto *objects = EditorClient::Find(result, "objects"); objects && objects->type == EDITOR_OBJECT_LIST)
            m_State.Log(LogLevel::Info, std::to_string(objects->objects.size()) + " scripts added to selection.");
    if (after == AfterCommand::Group && created && created->type == EDITOR_OBJECT && created->object) {
        m_Group = created->object;
        LayoutSelectionData selection;
        if (ReadLayoutSelection(Client(), selection)) m_GroupRevision = unsigned(selection.revision);
    }
    if (after == AfterCommand::SelectCreated)
        if (const auto *objects = EditorClient::Find(result, "copies"); objects && objects->type == EDITOR_OBJECT_LIST) {
            for (const auto object : objects->objects)
                if (Client().Query("object.details", {ObjectArgument("target", object)}).status != EDITOR_OK) return;
            Execute("selection.lock", {IntegerArgument("locked", 0)});
            Execute("selection.replace", {ObjectsArgument("objects", objects->objects), IntegerArgument("render-only", 1)});
        }
}
void LayoutWindow::PollCommands() {
    auto pending = std::move(m_Commands); m_Commands.clear();
    for (const auto &command : pending) {
        if (command.document != Client().Scope().document) continue;
        const auto result = Client().Completion(command.operation);
        if (result.status == EDITOR_QUEUED) m_Commands.push_back(command);
        else Complete(result, command.after);
    }
}

void LayoutWindow::OpenContextMenu(int x, int y) {
    LayoutSceneData scene;
    if (x < 0 || y < 0 || !ReadLayoutScene(Client(), "context", scene,
        {IntegerArgument("x", uint64_t(x)), IntegerArgument("y", uint64_t(y))}) || !scene.scene) return;
    m_Menu = {};
    for (const auto &row : scene.rows) m_Menu.objects.push_back({row.object,
        row.kind.empty() ? row.className : row.kind, row.name, row.scripts});
    m_Menu.camera = scene.savedCamera; m_Menu.frame = scene.frame; m_Menu.initial = scene.initial;
    m_Menu.initialTarget = scene.initialTarget; m_Menu.frameShapes = std::move(scene.frameShapes);
    ImGui::OpenPopup("##layoutmenu");
}

void LayoutWindow::ContextMenuPopup() {
    if (!ImGui::BeginPopup("##layoutmenu")) return;
    if (!Client().Scope().document.value || (!m_Menu.objects.empty() &&
        m_Menu.objects.front().object.document != Client().Scope().document)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const std::vector<MenuObject> objects = m_Menu.objects;
    LayoutSelectionData selected;
    const bool selection = ReadLayoutSelection(Client(), selected) && !selected.objects.empty();
    char text[1024];
    // The objects' Setups ("%s Setup (%s)"), the camera's after a separator.
    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i].object == m_Menu.camera && objects.size() > 1) ImGui::Separator();
        std::snprintf(text, sizeof(text), "%s Setup (%s)##setup%d", objects[i].cls.c_str(), objects[i].name.c_str(),
                      static_cast<int>(i));
        if (ImGui::MenuItem(text)) Execute("object.open-setup", {ObjectArgument("target", objects[i].object)});
    }
    if (m_Menu.frame && ImGui::BeginMenu("Change Representation")) {
        if (ImGui::MenuItem("No Representation")) SetRepresentation(0);
        const auto &shapes = m_Menu.frameShapes;
        for (size_t i = 0; i < shapes.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::MenuItem(shapes[i].c_str())) SetRepresentation(static_cast<int>(i) + 1);
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    if (!objects.empty() && objects.back().object != m_Menu.camera) ImGui::Separator();
    if (ImGui::MenuItem("Reset Current Camera Settings")) ResetCamera();
    if (ImGui::BeginMenu("Select Camera")) {
        // The default views, then the composition's cameras ("name (class)"):
        // 20 at most.
        LayoutSceneData scene;
        ReadLayoutScene(Client(), "cameras", scene);
        int count = 0; bool separated = false;
        for (const auto &camera : scene.rows) {
            if (!camera.view && !separated) { ImGui::Separator(); separated = true; }
            if (count >= 20) { ImGui::MenuItem(">> Use camera combo for more...", nullptr, false, false); break; }
            std::string label = camera.name;
            if (!camera.view) label += " (" + camera.className + ")";
            ImGui::PushID(int(camera.object.object));
            if (ImGui::MenuItem(label.c_str(), nullptr, scene.camera == camera.object))
                Execute("layout.set-camera", {ObjectArgument("target", camera.object)});
            ImGui::PopID(); ++count;
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (!objects.empty()) {
        if (ImGui::MenuItem("Set Initial Conditions")) InitialConditionsOn(0);
        if (ImGui::MenuItem("Restore Initial Conditions", nullptr, false, m_Menu.initial)) InitialConditionsOn(1);
        if (ImGui::MenuItem("Remove Initial Conditions", nullptr, false, m_Menu.initial)) InitialConditionsOn(2);
        ImGui::Separator();
        if (ImGui::BeginMenu("Create Script on")) {
            for (size_t i = 0; i < objects.size(); ++i) {
                if (objects[i].object == m_Menu.camera && objects.size() > 1) ImGui::Separator();
                std::snprintf(text, sizeof(text), "%s (%s)##script%d", objects[i].name.c_str(), objects[i].cls.c_str(),
                              static_cast<int>(i));
                if (ImGui::MenuItem(text)) CreateScriptOn(objects[i].object);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Create Scripts on Selection", nullptr, false, selection)) CreateScriptsOnSelection();
        int scripts = 0;
        for (const auto &entry : objects) scripts += int(entry.scripts.size());
        if (ImGui::BeginMenu("Show Script", scripts > 0)) {
            // "'%s' on %s (%s)": 40 at most.
            int shown = 0;
            bool separated = false, more = false;
            for (size_t i = 0; i < objects.size() && !more; ++i) {
                if (objects[i].scripts.empty()) continue;
                if (objects[i].object == m_Menu.camera && scripts > 1 && !separated && shown < 40) {
                    ImGui::Separator();
                    separated = true;
                }
                for (const auto &script : objects[i].scripts) {
                    if (shown >= 40) {
                        ImGui::MenuItem(">> Use schematic for more...", nullptr, false, false);
                        more = true;
                        break;
                    }
                    std::snprintf(text, sizeof(text), "'%s' on %s (%s)##show%d", script.name.c_str(),
                                  objects[i].name.c_str(), objects[i].cls.c_str(), shown);
                    if (ImGui::MenuItem(text)) ShowScriptInSchematic(script.object);
                    ++shown;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
    }
    if (ImGui::MenuItem("Select All", "Ctrl+A")) SelectAll(false);
    if (ImGui::MenuItem("Clear Selection", "Ctrl+D")) ClearSelection();
    if (ImGui::MenuItem("Invert Selection", "Ctrl+I")) SelectAll(true);
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate", nullptr, false, selection)) Duplicate(selected.objects);
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Del", false, selection)) DeleteSelection();
    ImGui::EndPopup();
}

// Dev (commands 177..179): the first object of the menu, or the character
// after it when that object is a body part.
void LayoutWindow::InitialConditionsOn(int action) {
    if (!m_Menu.initialTarget) return;
    const std::vector<ObjectRef> objects{m_Menu.initialTarget};
    Execute("scene.initial-conditions", {ObjectsArgument("objects", objects),
        IntegerArgument("mode", uint64_t(std::clamp(action, 0, 2))), IntegerArgument("hierarchy", 0)});
}

// Dev (commands 160..165): a Script on the object, shown in the Schematic.
void LayoutWindow::CreateScriptOn(ObjectRef object) {
    if (object) Execute("script.create", {ObjectsArgument("objects", std::vector<ObjectRef>{object})}, AfterCommand::ShowScript);
}

// Dev (command 172): a Script on each selected object; "%d scripts added to selection.".
void LayoutWindow::CreateScriptsOnSelection() {
    LayoutSelectionData selection;
    if (ReadLayoutSelection(Client(), selection) && !selection.objects.empty())
        Execute("script.create", {ObjectsArgument("objects", selection.objects)}, AfterCommand::ScriptsAdded);
}

void LayoutWindow::ShowScriptInSchematic(ObjectRef script) {
    if (script) Execute("script.navigate", {ObjectArgument("target", script)});
}

// Command 150; one undo step, the default views' not marking the
// composition modified.
void LayoutWindow::ResetCamera() {
    LayoutSceneData scene;
    if (ReadLayoutScene(Client(), "cameras", scene) && scene.camera)
        Execute("layout.reset-camera", {ObjectArgument("target", scene.camera)});
}

// Dev.exe sub_4C53C0: the selected 3D Frames take the representation. Here
// the frame right-clicked, the menu's object, counts too: selected, the
// selection's frames change with it; not selected, it alone (by the
// selection alone, a frame not selected did not change though the menu was
// shown for it).
void LayoutWindow::SetRepresentation(int representation) {
    LayoutSelectionData selection;
    if (ReadLayoutSelection(Client(), selection)) Execute("layout.frame-representation",
        {ObjectArgument("target", m_Menu.frame), ObjectsArgument("objects", selection.objects),
         IntegerArgument("value", uint64_t(representation))});
}

// Dev.exe sub_5BDD60 (mode -1): the "Copy Options" parameter dialog, the
// session's copy dependencies kept with OK.
void LayoutWindow::Duplicate(const std::vector<ObjectRef> &objects, uint64_t clipboardRevision) {
    LayoutSceneData scene;
    if (objects.empty() || !ReadLayoutScene(Client(), "groups", scene) || !scene.scene) return;
    const auto options = Client().Query("scene.copy-options"); if (options.status != EDITOR_OK) return;
    m_Doc.pendingCopy = objects;
    m_Doc.copyTarget = scene.scene; m_Doc.copyClipboardRevision = clipboardRevision;
    if (const auto *values = EditorClient::Find(options, "options")) {
        m_CopyOptions.Resize(int(values->integers.size()));
        for (size_t i = 0; i < values->integers.size(); ++i) m_CopyOptions[int(i)] = CKDWORD(values->integers[i]);
    }
    m_CopyOptions.m_Flags = CK_DEPENDENCIES_FLAGS(EditorClient::Integer(options, "flags"));
    m_Doc.openCopyOptions = true;
}

// Dev's Edit Parameter dialog without its name and type (options 16 and
// 32) on a "Copy options" parameter: CK2UI's Dependencies UI.
void LayoutWindow::CopyOptionsDialog() {
    const char *id = "Edit Parameter###CopyOptions";
    if (m_Doc.openCopyOptions && m_OpenPopups) {
        m_CopyBox.Clear();
        m_CopyBox.AddDependencies("Copy options", CKPGUID_COPYDEPENDENCIES, m_CopyOptions);
        ImGui::OpenPopup(id);
        m_Doc.openCopyOptions = false;
    }
    const DialogAction action = EditParameterValueDialog(id, "Edit Parameter", m_CopyBox);
    if (action == DialogAction::None) return;
    const bool copy = action == DialogAction::Ok && m_CopyBox.Dependencies(0, m_CopyOptions);
    m_CopyBox.Clear();
    if (!copy) {
        m_Doc.pendingCopy.clear();
        return;
    }
    const auto objects = std::move(m_Doc.pendingCopy); m_Doc.pendingCopy.clear();
    if (objects.empty()) return;
    std::vector<uint64_t> dependencies;
    for (int i = 0; i < m_CopyOptions.Size(); ++i) dependencies.push_back(m_CopyOptions[i]);
    const EditorValue options{EDITOR_TEXT("options"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {dependencies.data(), uint32_t(dependencies.size())}};
    std::vector<EditorValue> arguments{ObjectArgument("target", m_Doc.copyTarget), IntegerArgument("custom", 1),
        IntegerArgument("flags", m_CopyOptions.m_Flags), options};
    if (m_Doc.copyClipboardRevision) arguments.push_back(IntegerArgument("clipboard-revision", m_Doc.copyClipboardRevision));
    else arguments.push_back(ObjectsArgument("objects", objects));
    Execute(m_Doc.copyClipboardRevision ? "scene.duplicate-clipboard" : "scene.duplicate", arguments, AfterCommand::SelectCreated);
}

void LayoutWindow::DeleteSelection() {
    LayoutSelectionData selection;
    if (ReadLayoutSelection(Client(), selection) && !selection.objects.empty())
        Execute("object.confirm-delete", {ObjectsArgument("objects", selection.objects)});
}

} // namespace vdev

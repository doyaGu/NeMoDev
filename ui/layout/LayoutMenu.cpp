// The 3D Layout's shortcut menu (Dev.exe sub_4C1BB0) and the commands it
// runs (sub_4B95C0, WM_COMMAND).

#include "ui/layout/LayoutWindow.h"
#include <cstdio>
#include <cstdlib>

#include <algorithm>

#include "core/level/ObjectEditCommands.h"
#include "core/level/SceneCommands.h"
#include "core/runtime/TextEncoding.h"
#include "ui/controls/ValueEditor.h"

namespace vdev {

std::string DevClassName(CKObject *object) {
    if (!object) return "Object";
    const CK_CLASSID cid = object->GetClassID();
    auto named = [](CK_CLASSID id) {
        const char *name = CKClassIDToString(id);
        return std::string(name ? name : "Object");
    };
    if (cid == CKCID_3DENTITY) {
        const CKDWORD flags = CK3dEntity::Cast(object)->GetFlags();
        if (flags & CK_3DENTITY_FRAME) return "3D Frame";
        if (flags & CK_3DENTITY_TARGETLIGHT) return named(CKCID_TARGETLIGHT);
        if (flags & CK_3DENTITY_TARGETCAMERA) return named(CKCID_TARGETCAMERA);
        if (flags & CK_3DENTITY_PORTAL) return "Portal";
        return named(cid);
    }
    if (cid == CKCID_2DENTITY) return "2D Frame";
    if (CKLight *light = CKLight::Cast(object)) {
        switch (light->GetType()) {
        case VX_LIGHTPOINT: return "Point Light";
        case VX_LIGHTSPOT: return "Spot Light";
        case VX_LIGHTDIREC: return "Directional Light";
        case VX_LIGHTPARA: return "Parallel Light";
        default: return named(cid);
        }
    }
    if (CKBehavior *behavior = CKBehavior::Cast(object))
        if (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) return "Script";
    return named(cid);
}

std::string DevObjectName(CKObject *object) {
    if (!object) return "None";
    if (object->GetName()) return AnsiToUtf8(object->GetName());
    char text[96];
    const char *cls = CKClassIDToString(object->GetClassID());
    std::snprintf(text, sizeof(text), "%s_%x", cls ? cls : "Object", static_cast<unsigned>(object->GetID()));
    return text;
}

void LayoutWindow::OpenContextMenu(int x, int y) {
    CKContext *context = m_State.host.Context();
    if (!context || !m_State.HasDocument()) return;
    // Dev opens it in a scene only (here also the level scene of a
    // composition not launched).
    CKScene *scene = InitialConditionsScene(context);
    if (!scene) return;
    const LayoutPickInfo info = m_View.PickInfo(x, y);

    m_Menu = ContextMenu();
    auto add = [&](CKObject *object, const std::string &cls) {
        if (object) m_Menu.objects.push_back({object->GetID(), cls.empty() ? DevClassName(object) : cls, DevObjectName(object)});
    };
    auto initial = [&](CKObject *object) {
        CKSceneObject *sceneObject = CKSceneObject::Cast(object);
        if (sceneObject && scene->GetObjectInitialValue(sceneObject)) m_Menu.initial = true;
    };
    if (CKObject *sprite = context->GetObject(info.entity2d)) {
        add(sprite, "");
        initial(sprite);
    }
    CKObject *entity = context->GetObject(info.entity3d);
    CKObject *character = context->GetObject(info.character);
    if (entity) {
        if (!character) initial(entity);
        if (CKCurvePoint *point = CKCurvePoint::Cast(entity)) {
            add(point->GetCurve(), ""); // the point's curve
        } else if (CKGrid::Cast(entity)) {
            add(entity, ""); // Dev: its grid editor
            add(entity, "Grid 3D");
        } else {
            add(entity, "");
        }
    }
    if (character) {
        add(character, "");
        initial(character);
    }
    // A private mesh (an interface object's) hides its material and texture.
    CKObject *mesh = context->GetObject(info.mesh);
    const bool hidden = mesh && mesh->IsPrivate();
    if (mesh && !hidden) add(mesh, "");
    if (!hidden) {
        CKObject *material = context->GetObject(info.material);
        CKObject *texture = context->GetObject(info.texture);
        if (material && !material->IsPrivate()) add(material, "");
        if (texture && !texture->IsPrivate()) add(texture, "");
    }
    if (CK3dEntity *placed = CK3dEntity::Cast(entity)) add(placed->GetReferencePlace(), "");
    CKCamera *camera = m_View.Camera();
    if (camera && !m_View.IsViewCamera(camera->GetID())) {
        add(camera, "Current Camera");
        m_Menu.camera = camera->GetID();
    }
    CK3dEntity *frame = CK3dEntity::Cast(entity);
    m_Menu.frame = frame && (frame->GetFlags() & CK_3DENTITY_FRAME) ? frame->GetID() : 0;
    ImGui::OpenPopup("##layoutmenu");
}

void LayoutWindow::ContextMenuPopup() {
    if (!ImGui::BeginPopup("##layoutmenu")) return;
    CKContext *context = m_State.host.Context();
    if (!context || !m_State.HasDocument()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const std::vector<MenuObject> objects = m_Menu.objects;
    const bool selection = !AliveSelection().empty();
    char text[1024];
    // The objects' Setups ("%s Setup (%s)"), the camera's after a separator.
    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i].object == m_Menu.camera && objects.size() > 1) ImGui::Separator();
        std::snprintf(text, sizeof(text), "%s Setup (%s)##setup%d", objects[i].cls.c_str(), objects[i].name.c_str(),
                      static_cast<int>(i));
        if (ImGui::MenuItem(text) && context->GetObject(objects[i].object))
            m_State.events.Post(ShowObjectSetup{objects[i].object});
    }
    if (m_Menu.frame && ImGui::BeginMenu("Change Representation")) {
        if (ImGui::MenuItem("No Representation")) SetRepresentation(0);
        const std::vector<std::string> shapes = m_View.FrameShapeNames();
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
        CKCamera *current = m_View.Camera();
        int count = 0;
        bool separated = false;
        for (CK_ID id : m_View.Cameras()) {
            CKObject *camera = context->GetObject(id);
            if (!camera) continue;
            const bool view = m_View.IsViewCamera(id);
            if (!view && !separated) {
                ImGui::Separator();
                separated = true;
            }
            if (count >= 20) {
                ImGui::MenuItem(">> Use camera combo for more...", nullptr, false, false);
                break;
            }
            std::string label = DevObjectName(camera);
            if (!view) label += " (" + DevClassName(camera) + ")";
            ImGui::PushID(static_cast<int>(id));
            if (ImGui::MenuItem(label.c_str(), nullptr, current == camera)) m_View.SetCamera(id);
            ImGui::PopID();
            ++count;
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
        for (const MenuObject &entry : objects)
            if (CKBeObject *owner = CKBeObject::Cast(context->GetObject(entry.object))) scripts += owner->GetScriptCount();
        if (ImGui::BeginMenu("Show Script", scripts > 0)) {
            // "'%s' on %s (%s)": 40 at most.
            int shown = 0;
            bool separated = false, more = false;
            for (size_t i = 0; i < objects.size() && !more; ++i) {
                CKBeObject *owner = CKBeObject::Cast(context->GetObject(objects[i].object));
                if (!owner || !owner->GetScriptCount()) continue;
                if (objects[i].object == m_Menu.camera && scripts > 1 && !separated && shown < 40) {
                    ImGui::Separator();
                    separated = true;
                }
                for (int s = 0; s < owner->GetScriptCount(); ++s) {
                    CKBehavior *script = owner->GetScript(s);
                    if (!script) continue;
                    if (shown >= 40) {
                        ImGui::MenuItem(">> Use schematic for more...", nullptr, false, false);
                        more = true;
                        break;
                    }
                    std::snprintf(text, sizeof(text), "'%s' on %s (%s)##show%d", DevObjectName(script).c_str(),
                                  objects[i].name.c_str(), objects[i].cls.c_str(), shown);
                    if (ImGui::MenuItem(text)) ShowScriptInSchematic(script->GetID());
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
    if (ImGui::MenuItem("Duplicate", nullptr, false, selection)) Duplicate(AliveSelection());
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Del", false, selection)) DeleteSelection();
    ImGui::EndPopup();
}

// Dev (commands 177..179): the first object of the menu, or the character
// after it when that object is a body part.
void LayoutWindow::InitialConditionsOn(int action) {
    if (m_Menu.objects.empty() || !m_State.HasDocument()) return;
    CKContext *context = m_State.host.Context();
    CK_ID target = m_Menu.objects[0].object;
    if (CK3dEntity::Cast(context->GetObject(target)) && m_Menu.objects.size() > 1 &&
        CKCharacter::Cast(context->GetObject(m_Menu.objects[1].object)))
        target = m_Menu.objects[1].object;

    static const InitialConditions actions[3] = {InitialConditions::Set, InitialConditions::Restore,
                                                 InitialConditions::Remove};
    const EditResult result =
        ApplyInitialConditions(*m_State.document, {target}, actions[std::clamp(action, 0, 2)]);

    if (!result.ok && !result.message.empty()) m_State.Log(LogLevel::Warning, AnsiToUtf8(result.message));
}

// Dev (commands 160..165): a Script on the object, shown in the Schematic.
void LayoutWindow::CreateScriptOn(CK_ID object) {
    if (!m_State.HasDocument() || !m_State.host.Context()->GetObject(object)) return;
    const EditResult result = CreateScript(*m_State.document, object, "");
    if (!result.ok) {
        m_State.Log(LogLevel::Warning, "Create Script: " + AnsiToUtf8(result.message));
        return;
    }
    if (result.created) ShowScriptInSchematic(result.created);
}

// Dev (command 172): a Script on each selected object; "%d scripts added to selection.".
void LayoutWindow::CreateScriptsOnSelection() {
    if (!m_State.HasDocument()) return;
    std::vector<CK_ID> owners;
    for (CK_ID id : AliveSelection())
        if (CKBeObject::Cast(m_State.host.Context()->GetObject(id))) owners.push_back(id);
    size_t steps = 0;
    for (const EditResult &result : CreateScriptsOn(*m_State.document, owners))
        if (result.ok) ++steps;
    if (steps) m_State.Log(LogLevel::Info, std::to_string(steps) + " scripts added to selection.");
}

void LayoutWindow::ShowScriptInSchematic(CK_ID script) {
    m_State.events.Post(ShowElement{script});
}

// Command 150; one undo step, the default views' not marking the
// composition modified.
void LayoutWindow::ResetCamera() {
    CKCamera *camera = m_View.Camera();
    if (!camera) return;
    std::vector<CK_ID> moved{camera->GetID()};
    if (CKTargetCamera *targeted = CKTargetCamera::Cast(camera))
        if (CK3dEntity *target = targeted->GetTarget()) moved.push_back(target->GetID());
    ObjectsGesture gesture;
    if (!gesture.Begin(m_State.host.Context(), moved)) return;
    m_View.ResetCamera();
    if (m_State.HasDocument())
        gesture.Commit(*m_State.document, "Reset Current Camera Settings", !m_View.IsViewCamera(camera->GetID()));
}

// Dev.exe sub_4C53C0: the selected 3D Frames take the representation. Here
// the frame right-clicked, the menu's object, counts too: selected, the
// selection's frames change with it; not selected, it alone (by the
// selection alone, a frame not selected did not change though the menu was
// shown for it).
void LayoutWindow::SetRepresentation(int representation) {
    CKContext *context = m_State.host.Context();
    const std::vector<CK_ID> selection = AliveSelection();
    std::vector<CK_ID> frames;
    if (std::find(selection.begin(), selection.end(), m_Menu.frame) == selection.end()) {
        if (context->GetObject(m_Menu.frame)) frames.push_back(m_Menu.frame);
    } else {
        for (CK_ID id : selection) {
            CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(id));
            if (entity && (entity->GetFlags() & CK_3DENTITY_FRAME)) frames.push_back(id);
        }
    }
    m_State.SetFrameRepresentation(frames, representation);
}

// Dev.exe sub_5BDD60 (mode -1): the "Copy Options" parameter dialog, the
// session's copy dependencies kept with OK.
void LayoutWindow::Duplicate(const std::vector<CK_ID> &objects) {
    if (!m_State.HasDocument() || objects.empty()) return;
    m_Doc.pendingCopy = objects;
    m_CopyOptions = m_State.CopyDependencies();
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
    m_State.CopyDependencies() = m_CopyOptions;
    const std::vector<CK_ID> objects = std::move(m_Doc.pendingCopy);
    m_Doc.pendingCopy.clear();
    if (!m_State.HasDocument() || objects.empty()) return;
    std::vector<std::pair<CK_ID, CK_ID>> copies;
    const EditResult result =
        DuplicateObjects(*m_State.document, objects, &copies, &m_State.CopyDependencies());
    if (!result.ok) {
        m_State.Log(LogLevel::Warning, AnsiToUtf8(result.message));
        return;
    }
    std::vector<CK_ID> made;
    for (const auto &copy : copies) {
        auto found = m_State.frameRepresentations.find(copy.first);
        if (found != m_State.frameRepresentations.end()) m_State.frameRepresentations[copy.second] = found->second;
        made.push_back(copy.second);
    }
    m_State.objectSelection.locked = false;
    SetSelection(made);
}

void LayoutWindow::DeleteSelection() {
    const std::vector<CK_ID> objects = AliveSelection();
    // The Level Manager's Delete dialog.
    if (!objects.empty()) m_State.events.Post(ConfirmDeleteObjects{objects});
}

} // namespace vdev

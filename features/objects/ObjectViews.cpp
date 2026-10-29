#include "features/objects/ObjectViews.h"
#include "editor/ImGuiDrawing.h"
#include "misc/cpp/imgui_stdlib.h"

namespace vdev {
namespace {
uint32_t DrawText(void *data, const EditorHost *host, const EditorTextInput *input) {
    std::string value(input->value.data, input->value.size);
    const std::string label(input->label.data, input->label.size);
    if (input->focus)
        ImGui::SetKeyboardFocusHere();
    const bool accept = ImGui::InputText(label.c_str(), &value,
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool active = ImGui::IsItemActive() || ImGui::IsItemActivated();
    if (data) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
            value.clear();
    }
    host->editValue(host->data, input->edit, {value.data(), static_cast<uint32_t>(value.size())});
    return ImGui::IsKeyPressed(ImGuiKey_Escape)
               ? EDITOR_INPUT_CANCEL
               : (accept ? EDITOR_INPUT_ACCEPT : 0u) | (active ? EDITOR_INPUT_ACTIVE : 0u);
}
struct ObjectView {
    ObjectRef object{};
    std::string name, type;
    uint64_t revision = 0;
    bool editable = false;
    EditorEdit edit = 0;
};
EditorStatus Fields(void *data, const EditorValue *fields, uint32_t count) {
    auto &view = *static_cast<ObjectView *>(data);
    for (uint32_t i = 0; i < count; ++i) {
        const auto &field = fields[i];
        const std::string key(field.key.data, field.key.size);
        if (field.type == EDITOR_TEXT_VALUE) {
            if (key == "name")
                view.name.assign(field.text.data, field.text.size);
            else if (key == "class")
                view.type.assign(field.text.data, field.text.size);
        }
    }
    return EDITOR_OK;
}
void *Create(void *data, const EditorHost *, EditorView) {
    auto *view = new ObjectView;
    view->editable = data != nullptr;
    return view;
}
void Destroy(void *data) { delete static_cast<ObjectView *>(data); }
void Draw(void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
    EditorImGuiWindow window(*frame);
    if (!window) return;
    auto &view = *static_cast<ObjectView *>(data);
    const auto object = view.edit ? view.object : host->selection(host->data);
    const auto revision = host->revision(host->data);
    if (object != view.object || revision != view.revision) {
        if (object != view.object && view.edit) {
            host->finishEdit(host->data, view.edit, 0);
            view.edit = 0;
        }
        view.object = object;
        view.revision = revision;
        view.name.clear();
        view.type.clear();
        const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, object, {}};
        const EditorRequest request{host->scope(host->data, id), &target, 1};
        host->query(host->data, EditorString EDITOR_TEXT("object.details"), &request, {&view, Fields});
    }
    if (!view.object) {
        ImGui::TextUnformatted("Select an object");
        return;
    }
    ImGui::Text("Name: %s", view.name.c_str());
    ImGui::Text("Class: %s", view.type.c_str());
    if (view.editable && !view.edit) {
        const auto session = host->scope(host->data, id);
        ImGui::BeginDisabled(host->canEdit(host->data, EditorString EDITOR_TEXT("object.name"), session, view.object) !=
                             EDITOR_OK);
        if (ImGui::Button("Rename"))
            host->beginEdit(host->data, EditorString EDITOR_TEXT("object.name"), session, view.object, &view.edit);
        ImGui::EndDisabled();
    }
    if (view.edit) {
        const auto action = host->drawEdit(host->data, view.edit, EditorString EDITOR_TEXT("##name"), 0);
        if (action & EDITOR_INPUT_CANCEL) {
            host->finishEdit(host->data, view.edit, 0);
            view.edit = 0;
        } else if ((action & EDITOR_INPUT_ACCEPT) || ImGui::Button("Apply")) {
            const auto result = host->finishEdit(host->data, view.edit, 1);
            if (result != EDITOR_REFUSED)
                view.edit = 0;
        } else {
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                host->finishEdit(host->data, view.edit, 0);
                view.edit = 0;
            }
        }
    }
}
} // namespace
EditorConfiguration DefaultObjectConfiguration() {
    EditorConfiguration config;
    config.providers.push_back({"editor", "text.single-line", "builtin.text"});
    config.providers.push_back({"command", "object.rename", "builtin.objects"});
    config.providers.push_back({"command", "object.create-frame3d", "builtin.objects"});
    config.providers.push_back({"query", "object.details", "builtin.objects"});
    config.providers.push_back({"view", "object.properties", "builtin.properties"});
    config.providers.push_back({"view", "object.summary", "builtin.summary"});
    return config;
}
bool RegisterObjectViews(EditorApplication &editor, std::string &error) {
    static const EditorString dependencies[] = {EDITOR_TEXT("objects.editing")};
    static bool clearButton = true, editableView = true;
    static const EditorTextEditorDesc editors[] = {
        {sizeof(EditorTextEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("text.single-line"),
         EDITOR_TEXT("builtin.text"), EDITOR_TEXT("Text"), DrawText, nullptr},
        {sizeof(EditorTextEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("text.single-line"),
         EDITOR_TEXT("builtin.text-clear"), EDITOR_TEXT("Text with Clear"), DrawText, &clearButton}};
    static const EditorViewDesc views[] = {
        {sizeof(EditorViewDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("object.properties"),
         EDITOR_TEXT("builtin.properties"), EDITOR_TEXT("Object Properties"), Create, Destroy, Draw, &editableView},
        {sizeof(EditorViewDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("object.summary"), EDITOR_TEXT("builtin.summary"),
         EDITOR_TEXT("Object Summary"), Create, Destroy, Draw, nullptr}};
    static const EditorMenuDesc menus[] = {{EDITOR_TEXT("object.rename.level"), EDITOR_TEXT("object.name"),
                                            EDITOR_TEXT("level.context"), EDITOR_TEXT("Rename"), 0},
                                           {EDITOR_TEXT("object.rename.hierarchy"), EDITOR_TEXT("object.name"),
                                            EDITOR_TEXT("hierarchy.context"), EDITOR_TEXT("Rename"), 0}};
    static const EditorBindingDesc bindings[] = {{EDITOR_TEXT("object.name"), EDITOR_TEXT("F2")}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EditorLiteral("objects.views");
    static const EditorMenuLocation locations[] = {
        {EDITOR_TEXT("level.context"), EDITOR_TEXT("Level Manager menu")},
        {EDITOR_TEXT("hierarchy.context"), EDITOR_TEXT("Hierarchy Manager menu")}};
    module.locations = locations;
    module.locationCount = 2;
    module.dependencies = dependencies;
    module.dependencyCount = 1;
    module.editors = editors;
    module.editorCount = 2;
    module.views = views;
    module.viewCount = 2;
    module.menus = menus;
    module.menuCount = 2;
    module.bindings = bindings;
    module.bindingCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev

#include "ui/app/FileInputs.h"

#include "application/EditorApplication.h"
#include "core/runtime/TextEncoding.h"
#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/FileDialogs.h"

#include <map>
#include "ui/controls/BehaviorInfoFields.h"

namespace vdev {
namespace {
std::string Text(EditorString text) {
    return {text.data ? text.data : "", text.size};
}
struct Fields {
    std::map<std::string, std::string> text;
    std::map<std::string, uint64_t> number;
    static EditorStatus Copy(void *data, const EditorValue *values, uint32_t count) {
        auto &self = *static_cast<Fields *>(data);
        for (uint32_t i = 0; i < count; ++i) {
            if (values[i].type == EDITOR_TEXT_VALUE)
                self.text[Text(values[i].key)] = Text(values[i].text);
            else if (values[i].type == EDITOR_INTEGER_VALUE)
                self.number[Text(values[i].key)] = values[i].integer;
        }
        return EDITOR_OK;
    }
};
struct FileInputs {
    SDL_Window *window;
    const EditorHost *host;
    EditorView view;
    FileDialogs dialogs;
    uint64_t shown = 0, answered = 0, exportInput = 0;
    Fields exportFields;
    BehaviorInfoFields behavior;
    FileInputs(SDL_Window *window, const EditorHost *host, EditorView view) : window(window), host(host), view(view) {}
    ~FileInputs() { dialogs.Close(); }
    void Respond(uint64_t id, const char *choice, const std::string &path = {}, const std::string &message = {}, bool exportOptions = false) {
        const EditorValue values[] = {{EditorLiteral("input"), EDITOR_INTEGER_VALUE, {}, {}, id},
                                      {EditorLiteral("choice"), EDITOR_TEXT_VALUE, {}, EditorLiteral(choice)},
                                      {EditorLiteral("path"), EDITOR_TEXT_VALUE, {}, EditorText(path)},
                                      {EditorLiteral("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)},
                                      {EDITOR_TEXT("hide"), EDITOR_INTEGER_VALUE, {}, {}, behavior.hide ? 1u : 0u},
                                      {EDITOR_TEXT("author"), EDITOR_TEXT_VALUE, {}, EditorText(behavior.author)},
                                      {EDITOR_TEXT("version"), EDITOR_TEXT_VALUE, {}, EditorText(behavior.version)},
                                      {EDITOR_TEXT("description"), EDITOR_TEXT_VALUE, {}, EditorText(behavior.description)}};
        const EditorRequest request{host->scope(host->data, view), values, exportOptions ? 8u : 4u};
        host->execute(host->data, EditorLiteral("document.respond"), &request, {});
        answered = id;
    }
    EditorStatus Query(const char *id, Fields &fields, const EditorValue *values = nullptr, uint32_t count = 0) {
        const EditorRequest request{host->scope(host->data, view), values, count};
        return host->query(host->data, EditorLiteral(id), &request, {&fields, Fields::Copy});
    }
    void Draw() {
        dialogs.Dispatch();
        Fields fields;
        if (Query("document.input", fields) != EDITOR_OK) return;
        const auto id = fields.number["input"];
        if (answered == id) return;
        if (fields.number["kind"] == EDITOR_FILE_INPUT) {
            if (shown == id) return;
            shown = id;
            if (fields.number["filters"] > 512) {
                Respond(id, "error");
                return;
            }
            std::wstring filter;
            for (uint64_t i = 0; i < fields.number["filters"]; ++i) {
                const EditorValue values[] = {{EditorLiteral("input"), EDITOR_INTEGER_VALUE, {}, {}, id},
                                              {EditorLiteral("index"), EDITOR_INTEGER_VALUE, {}, {}, i}};
                Fields item;
                if (Query("document.file-filter", item, values, 2) != EDITOR_OK) {
                    Respond(id, "error");
                    return;
                }
                filter += Utf8ToWide(item.text["label"]);
                filter.push_back(0);
                filter += Utf8ToWide(item.text["pattern"]);
                filter.push_back(0);
            }
            const auto title = Utf8ToWide(fields.text["title"]), extension = Utf8ToWide(fields.text["extension"]);
            FileDialogOptions options;
            options.owner = window;
            options.save = fields.number["save"] != 0;
            options.initial = Utf8ToWide(fields.text["initial"]);
            options.filter = filter.c_str();
            options.title = title.empty() ? nullptr : title.c_str();
            options.extension = extension.empty() ? nullptr : extension.c_str();
            dialogs.Request(options, [this, id](const FileDialogResult &result) {
                if (result.status == FileDialogResult::Status::Selected)
                    Respond(id, "accept", result.paths.front());
                else
                    Respond(id, result.status == FileDialogResult::Status::Error ? "error" : "cancel", {},
                            result.error);
            });
            return;
        }
        if (fields.number["kind"] == EDITOR_OBJECT_EXPORT_INPUT || fields.number["kind"] == EDITOR_BEHAVIOR_EXPORT_INPUT) {
            const bool behaviorExport = fields.number["kind"] == EDITOR_BEHAVIOR_EXPORT_INPUT;
            const char *popup = behaviorExport ? "Behavior Information###behaviorinfo" : "Save as###LevelSaveAs";
            if (exportInput != id) {
                const EditorValue argument{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, id};
                exportFields = {};
                if (Query("document.export-options", exportFields, &argument, 1) != EDITOR_OK) { Respond(id, "error"); return; }
                exportInput = id;
                behavior.hide = exportFields.number["hide"] != 0;
                behavior.name = exportFields.text["name"];
                behavior.type = exportFields.text["class"];
                behavior.author = exportFields.text["author"];
                behavior.version = exportFields.text["version"];
                behavior.description = exportFields.text["description"];
                ImGui::OpenPopup(popup);
            }
            if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            const bool valid = !behaviorExport || DrawBehaviorInfoFields(behavior, true);
            if (!behaviorExport) {
                ImGui::Text("%d object(s) saved as a Virtools file (.nmo)", int(exportFields.number["objects"]));
                ImGui::Checkbox("Hide script representation in Schematic view", &behavior.hide);
            }
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button(behaviorExport ? "OK" : "Save...", ImVec2(behaviorExport ? 80 : 100, 0)) ||
                (!behaviorExport && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
                ImGui::CloseCurrentPopup();
                Respond(id, "accept", {}, {}, true);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(behaviorExport ? 80 : 100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup(); Respond(id, "cancel");
            }
            ImGui::EndPopup();
            return;
        }
        if (fields.number["kind"] == EDITOR_CONFIRM_INPUT) {
            const char *popup = "###DevMessage";
            if (!ImGui::IsPopupOpen(popup)) ImGui::OpenPopup(popup);
            const int answer = vi::MessageDialog(popup, fields.text["title"], fields.text["message"],
                                                 int(fields.number["icon"]), true);
            if (answer) Respond(id, answer == 1 ? "accept" : "cancel");
            return;
        }
        if (fields.number["kind"] != EDITOR_SAVE_CHANGES_INPUT) {
            Respond(id, "error");
            return;
        }
        if (!ImGui::IsPopupOpen("Unsaved changes")) ImGui::OpenPopup("Unsaved changes");
        if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextUnformatted("Save changes to the composition?");
        const auto button = [&](const char *label, const char *answer) {
            if (ImGui::Button(label, ImVec2(110, 0))) {
                ImGui::CloseCurrentPopup();
                Respond(id, answer);
            }
        };
        button(fields.text["initial"].empty() ? "Save..." : "Save", "save");
        ImGui::SameLine();
        button("Save As...", "save-as");
        ImGui::SameLine();
        button("Discard", "discard");
        ImGui::SameLine();
        button("Cancel", "cancel");
        ImGui::EndPopup();
    }
};
} // namespace

bool RegisterFileInputs(EditorApplication &editor, SDL_Window *window, std::string &error) {
    EditorViewDesc view{sizeof(view),
                        EDITOR_IMGUI_VERSION,
                        EDITOR_TEXT("document.file-dialogs"),
                        EDITOR_TEXT("builtin.file-dialogs"),
                        EDITOR_TEXT("File dialogs"),
                        [](void *data, const EditorHost *host, EditorView id) -> void * {
                            return new FileInputs(static_cast<SDL_Window *>(data), host, id);
                        },
                        [](void *data) { delete static_cast<FileInputs *>(data); },
                        [](void *data, const EditorHost *, EditorView, EditorViewFrame *) { static_cast<FileInputs *>(data)->Draw(); },
                        window,
                        EDITOR_OVERLAY_VIEW,
                        1, EDITOR_WORKSPACE};
    const EditorString dependency = EDITOR_TEXT("documents.operations");
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EditorLiteral("files.dialogs");
    module.dependencies = &dependency;
    module.dependencyCount = 1;
    module.views = &view;
    module.viewCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev

#include "ui/app/OutputInputs.h"
#include "application/EditorApplication.h"
#include "core/runtime/TextEncoding.h"
#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/FileDialogs.h"
#include "ui/setup/SetupLayout.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>

namespace vdev {
namespace {
std::string Text(EditorString value) {
    return {value.data ? value.data : "", value.size};
}
struct Fields {
    std::map<std::string, std::string> text;
    std::map<std::string, uint64_t> number;
    static EditorStatus Copy(void *data, const EditorValue *values, uint32_t count) {
        auto &self = *static_cast<Fields *>(data);
        for (uint32_t i = 0; i < count; ++i) {
            if (values[i].type == EDITOR_TEXT_VALUE) self.text[Text(values[i].key)] = Text(values[i].text);
            if (values[i].type == EDITOR_INTEGER_VALUE) self.number[Text(values[i].key)] = values[i].integer;
        }
        return EDITOR_OK;
    }
};
uint64_t Number(const std::string &text) {
    if (text.empty() || text.front() == '-') return 0;
    char *end = nullptr;
    const auto value = std::strtoull(text.c_str(), &end, 10);
    return end == text.c_str() + text.size() && value <= uint64_t(std::numeric_limits<int>::max()) ? value : 0;
}
struct OutputInputs {
    SDL_Window *window;
    const EditorHost *host;
    EditorView view;
    FileDialogs files;
    uint64_t shown = 0, optionsInput = 0;
    std::string path, title, width, height;
    bool cmo = false, preview = false;
    std::string printText[6];
    uint64_t printValues[6]{};
    bool printOptions[5]{};
    OutputInputs(SDL_Window *window, const EditorHost *host, EditorView view)
        : window(window), host(host), view(view) {}
    ~OutputInputs() { files.Close(); }
    EditorStatus Query(const char *id, Fields &fields, uint64_t input = 0) {
        const EditorValue value{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, input};
        const EditorRequest request{host->scope(host->data, view), input ? &value : nullptr, input ? 1u : 0u};
        return host->query(host->data, EditorLiteral(id), &request, {&fields, Fields::Copy});
    }
    void Respond(uint64_t id, const char *choice, const std::string &selected = {}, const std::string &message = {}) {
        const EditorValue values[] = {
            {EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, id},
            {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE, {}, EditorLiteral(choice)},
            {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, EditorText(selected.empty() ? path : selected)},
            {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE, {}, EditorText(title)},
            {EDITOR_TEXT("width"), EDITOR_INTEGER_VALUE, {}, {}, Number(width)},
            {EDITOR_TEXT("height"), EDITOR_INTEGER_VALUE, {}, {}, Number(height)},
            {EDITOR_TEXT("cmo"), EDITOR_INTEGER_VALUE, {}, {}, cmo ? 1u : 0u},
            {EDITOR_TEXT("preview"), EDITOR_INTEGER_VALUE, {}, {}, preview ? 1u : 0u},
            {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)}};
        const EditorRequest request{host->scope(host->data, view), values, 9};
        host->execute(host->data, EDITOR_TEXT("output.respond"), &request, {});
    }
    void SetPrint(uint64_t id, const char *field, uint64_t value) {
        const EditorValue values[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, id},
                                      {EDITOR_TEXT("field"), EDITOR_TEXT_VALUE, {}, EditorLiteral(field)},
                                      {EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, value}};
        const EditorRequest request{host->scope(host->data, view), values, 3};
        host->execute(host->data, EDITOR_TEXT("output.print-setting"), &request, {});
    }
    void Web(uint64_t id) {
        const char *popup = "###CreateWebPage";
        if (!ImGui::IsPopupOpen(popup)) ImGui::OpenPopup(popup);
        const float s = setup::PageUnit();
        vi::DialogLayout layout;
        if (!vi::BeginScrollableDialog(popup, "Create a Web Page", {620 * s, 300 * s}, s, layout)) return;
        bool destination = false;
        if (vi::BeginDialogContent("##web-page", layout)) {
            {
                setup::SetupColumns columns("##web-sections", 2);
                if (columns) {
                    columns.Next();
                    ImGui::SeparatorText("Location");
                    const auto at = ImGui::GetCursorScreenPos();
                    destination = vi::Button("##destination", at,
                        {at.x + ImGui::GetContentRegionAvail().x, at.y + ImGui::GetFrameHeight()}, "- Choose Destination -");
                    ImGui::TextWrapped("%s", path.c_str());
                    columns.Next();
                    ImGui::SeparatorText("Settings");
                    setup::SetupForm form("##web-settings");
                    if (form) {
                        auto edit = [&](const char *label, const char *id, std::string &value) {
                            form.Row(label);
                            const auto pos = ImGui::GetCursorScreenPos();
                            vi::Edit(id, pos, {pos.x + ImGui::GetContentRegionAvail().x,
                                               pos.y + ImGui::GetFrameHeight()}, value);
                        };
                        edit("Web Page Title", "##title", title);
                        edit("Width", "##width", width);
                        edit("Height", "##height", height);
                    }
                }
            }
            vi::Check("##cmo", ImGui::GetCursorScreenPos(), 12 * s, cmo);
            ImGui::SameLine(); ImGui::TextUnformatted("Save as Cmo");
            vi::Check("##preview", ImGui::GetCursorScreenPos(), 12 * s, preview);
            ImGui::SameLine(); ImGui::TextUnformatted("Preview In Browser");
        }
        vi::EndDialogContent();
        const bool cancel = vi::DialogButton("Cancel", {layout.right - 60 * s, layout.buttonsY}, s, ImGuiKey_Escape);
        const bool valid = !path.empty() && Number(width) && Number(height);
        ImGui::BeginDisabled(!valid);
        const bool ok = vi::DialogButton("OK", {layout.right - 130 * s, layout.buttonsY}, s, ImGuiKey_Enter);
        ImGui::EndDisabled();
        if (cancel || ok || destination) ImGui::CloseCurrentPopup();
        vi::EndDialog();
        if (cancel)
            Respond(id, "cancel");
        else if (ok)
            Respond(id, "accept");
        else if (destination)
            Respond(id, "destination");
    }
    void Print(uint64_t id) {
        const char *popup = "###PrintParameters";
        if (!ImGui::IsPopupOpen(popup)) ImGui::OpenPopup(popup);
        const float s = setup::PageUnit();
        vi::DialogLayout layout;
        if (!vi::BeginScrollableDialog(popup, "Schematic Print  Parameters", {760 * s, 300 * s}, s, layout)) return;
        if (vi::BeginDialogContent("##print-parameters", layout)) {
            {
                setup::SetupColumns columns("##print-sections", 3);
                if (columns) {
                    const char *headings[] = {"pixel per mm", "Resolution to print (for 1 page)", "Pages"};
                    const char *ids[] = {"##perMmH", "##perMmV", "##resH", "##resV", "##pagesH", "##pagesV"};
                    const char *fields[] = {"per-mm-h", "per-mm-v", "resolution-h", "resolution-v", "pages-h", "pages-v"};
                    for (int section = 0; section < 3; ++section) {
                        columns.Next();
                        ImGui::TextWrapped("%s", headings[section]);
                        ImGui::Separator();
                        const char *forms[] = {"##print-density", "##print-resolution", "##print-pages"};
                        setup::SetupForm form(forms[section]);
                        if (form)
                            for (int axis = 0; axis < 2; ++axis) {
                                const int i = section * 2 + axis;
                                form.Row(axis ? "Vertical" : "Horizontal");
                                const auto at = ImGui::GetCursorScreenPos();
                                if (vi::Edit(ids[i], at, {at.x + ImGui::GetContentRegionAvail().x,
                                                           at.y + ImGui::GetFrameHeight()}, printText[i]))
                                    SetPrint(id, fields[i], std::max(uint64_t(1), Number(printText[i])));
                            }
                    }
                }
            }
            ImGui::TextWrapped("Total pages count = %llu", static_cast<unsigned long long>(printValues[4] * printValues[5]));
            const char *optionIds[] = {"##fillHeader", "##fillBody", "##fillShapes", "##bitmaps", "##colorLinks"};
            const char *optionFields[] = {"fill-header", "fill-body", "fill-shapes", "bitmaps", "color-links"};
            const char *labels[] = {"Fill Script Header", "Fill Script Body", "Fill Shapes", "Print Bitmaps",
                                    "Use Color for Links"};
            for (int i = 0; i < 5; ++i) {
                if (vi::Check(optionIds[i], ImGui::GetCursorScreenPos(), 12 * s, printOptions[i]))
                    SetPrint(id, optionFields[i], printOptions[i] ? 1 : 0);
                ImGui::SameLine(); ImGui::TextUnformatted(labels[i]);
            }
        }
        vi::EndDialogContent();
        const bool cancel = vi::DialogButton("Cancel", {layout.right - 60 * s, layout.buttonsY}, s, ImGuiKey_Escape);
        const bool ok = vi::DialogButton("OK", {layout.right - 130 * s, layout.buttonsY}, s, ImGuiKey_Enter);
        if (ok || cancel) ImGui::CloseCurrentPopup();
        vi::EndDialog();
        if (ok || cancel) Respond(id, ok ? "accept" : "cancel");
    }
    void Draw() {
        files.Dispatch();
        Fields fields;
        if (Query("output.input", fields) != EDITOR_OK) {
            for (const char *popup : {"###CreateWebPage", "###PrintParameters"})
                if (ImGui::BeginPopupModal(popup)) {
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            return;
        }
        const auto id = fields.number["input"], kind = fields.number["kind"];
        if (kind == EDITOR_FILE_INPUT) {
            if (shown == id) return;
            shown = id;
            FileDialogOptions options;
            options.owner = window;
            options.save = true;
            options.filter = L"Html Files (*.htm;*.html)\0*.htm;*.html\0";
            options.extension = L"htm";
            options.title = L"Select file to save";
            options.initial = Utf8ToWide(fields.text["path"]);
            files.Request(options, [this, id](const FileDialogResult &result) {
                Respond(id,
                        result.status == FileDialogResult::Status::Selected ? "accept"
                        : result.status == FileDialogResult::Status::Error  ? "error"
                                                                            : "cancel",
                        result.paths.empty() ? std::string() : result.paths.front(), result.error);
            });
            return;
        }
        if (kind == EDITOR_CONFIRM_INPUT) {
            const char *popup = "###DevMessage";
            if (!ImGui::IsPopupOpen(popup)) ImGui::OpenPopup(popup);
            const int result = vi::MessageDialog(popup, fields.text["title"], fields.text["message"], 0, true);
            if (result) Respond(id, result == 1 ? "accept" : "cancel");
            return;
        }
        Fields options;
        if (Query("output.options", options, id) != EDITOR_OK) return;
        if (optionsInput != id) {
            optionsInput = id;
            path = options.text["path"];
            title = options.text["title"];
            width = std::to_string(options.number["width"]);
            height = std::to_string(options.number["height"]);
            cmo = options.number["cmo"] != 0;
            preview = options.number["preview"] != 0;
            std::fill(std::begin(printValues), std::end(printValues), uint64_t(-1));
        }
        const char *names[] = {"per-mm-h", "per-mm-v", "resolution-h", "resolution-v", "pages-h", "pages-v"};
        for (int i = 0; i < 6; ++i)
            if (printValues[i] != options.number[names[i]]) {
                printValues[i] = options.number[names[i]];
                printText[i] = std::to_string(printValues[i]);
            }
        const char *flags[] = {"fill-header", "fill-body", "fill-shapes", "bitmaps", "color-links"};
        for (int i = 0; i < 5; ++i) printOptions[i] = options.number[flags[i]] != 0;
        if (kind == EDITOR_WEB_PAGE_INPUT)
            Web(id);
        else if (kind == EDITOR_PRINT_INPUT)
            Print(id);
        else
            Respond(id, "error");
    }
};
} // namespace
bool RegisterOutputInputs(EditorApplication &editor, SDL_Window *window, std::string &error) {
    const EditorViewDesc view{
        sizeof(EditorViewDesc),
        EDITOR_IMGUI_VERSION,
        EDITOR_TEXT("document.output-dialogs"),
        EDITOR_TEXT("builtin.output-dialogs"),
        EDITOR_TEXT("Web and Print dialogs"),
        [](void *data, const EditorHost *host, EditorView id) -> void * {
            return new OutputInputs(static_cast<SDL_Window *>(data), host, id);
        },
        [](void *data) { delete static_cast<OutputInputs *>(data); },
        [](void *data, const EditorHost *, EditorView, EditorViewFrame *) { static_cast<OutputInputs *>(data)->Draw(); },
        window,
        EDITOR_OVERLAY_VIEW,
        1, EDITOR_WORKSPACE};
    const EditorString dependency = EDITOR_TEXT("documents.outputs");
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("outputs.dialogs");
    module.dependencies = &dependency;
    module.dependencyCount = 1;
    module.views = &view;
    module.viewCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev

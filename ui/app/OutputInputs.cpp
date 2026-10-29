#include "ui/app/OutputInputs.h"
#include "application/EditorApplication.h"
#include "core/runtime/TextEncoding.h"
#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/FileDialogs.h"

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
        const float s = ImGui::GetFontSize() / 13.0f;
        const ImVec2 client(254.0f * 1.5f * s, 172.0f * 1.625f * s + 26.0f * s);
        ImVec2 o;
        if (!vi::BeginDialog(popup, "Create a Web Page", client, s, o)) return;
        const auto P = [&](float x, float y) { return ImVec2(o.x + x * 1.5f * s, o.y + y * 1.625f * s); };
        vi::Panel(ImVec2(o.x + 2 * s, o.y + 2 * s), ImVec2(o.x + client.x - 2 * s, P(0, 172).y), s);
        auto *draw = ImGui::GetWindowDrawList();
        draw->AddRect(P(4, 5), P(249, 149), vi::Color(0));
        draw->AddRect(P(9, 20), P(242, 58), vi::Color(0));
        draw->AddRect(P(9, 75), P(242, 140), vi::Color(0));
        vi::Text(P(9, 9), P(47, 18), "Location", true);
        const bool destination = vi::Button("##destination", P(18, 27), P(101, 39), "- Choose Destination -");
        vi::Text(P(17, 45), P(234, 54), path.c_str());
        vi::Text(P(9, 63), P(47, 72), "Settings", true);
        vi::Text(P(11, 79), P(227, 89), "  Web Page Title");
        vi::Edit("##title", P(15, 91), P(232, 103), title, vi::LEFT);
        vi::Text(P(16, 108), P(87, 117), "Window Size");
        vi::Edit("##width", P(17, 121), P(53, 133), width);
        vi::Edit("##height", P(61, 121), P(97, 133), height);
        const float box = 8 * 1.5f * s;
        vi::Check("##cmo", P(18, 155), box, cmo);
        vi::Text(ImVec2(P(18, 155).x + box + 3 * s, P(18, 155).y), P(79, 164), "Save as Cmo");
        vi::Check("##preview", P(86, 155), box, preview);
        vi::Text(ImVec2(P(86, 155).x + box + 3 * s, P(86, 155).y), P(171, 164), "Preview In Browser");
        const float by = o.y + client.y - 23 * s;
        const bool cancel = vi::DialogButton("Cancel", ImVec2(o.x + client.x - 70 * s, by), s, ImGuiKey_Escape);
        const bool valid = !path.empty() && Number(width) && Number(height);
        ImGui::BeginDisabled(!valid);
        const bool ok = vi::DialogButton("OK", ImVec2(o.x + client.x - 140 * s, by), s, ImGuiKey_Enter);
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
        const float s = ImGui::GetFontSize() / 13.0f;
        const ImVec2 client(333 * 1.5f * s, 111 * 1.625f * s + 26 * s);
        ImVec2 o;
        if (!vi::BeginDialog(popup, "Schematic Print  Parameters", client, s, o)) return;
        const auto P = [&](float x, float y) { return ImVec2(o.x + x * 1.5f * s, o.y + y * 1.625f * s); };
        vi::Panel(ImVec2(o.x + 2 * s, o.y + 2 * s), ImVec2(o.x + client.x - 2 * s, P(0, 111).y), s);
        vi::Text(P(40, 6), P(96, 14), "pixel per mm");
        vi::Text(P(125, 6), P(233, 14), "Resolution to print (for 1 page)");
        vi::Text(P(242, 6), P(284, 14), "Pages");
        vi::Text(P(2, 24), P(34, 32), "Horizontal");
        vi::Text(P(2, 39), P(26, 47), "Vertical");
        const float edits[6][2] = {{40, 24}, {40, 39}, {139, 24}, {139, 39}, {242, 24}, {242, 39}};
        const char *ids[] = {"##perMmH", "##perMmV", "##resH", "##resV", "##pagesH", "##pagesV"};
        const char *fields[] = {"per-mm-h", "per-mm-v", "resolution-h", "resolution-v", "pages-h", "pages-v"};
        for (int i = 0; i < 6; ++i)
            if (vi::Edit(ids[i], P(edits[i][0], edits[i][1]), P(edits[i][0] + 76, edits[i][1] + 12), printText[i]))
                SetPrint(id, fields[i], std::max(uint64_t(1), Number(printText[i])));
        vi::Text(P(242, 57), P(306, 65), "Total pages count =");
        vi::Text(P(315, 57), P(333, 65), std::to_string(printValues[4] * printValues[5]).c_str());
        const char *optionIds[] = {"##fillHeader", "##fillBody", "##fillShapes", "##bitmaps", "##colorLinks"};
        const char *optionFields[] = {"fill-header", "fill-body", "fill-shapes", "bitmaps", "color-links"};
        const char *labels[] = {"Fill Script Header", "Fill Script Body", "Fill Shapes", "Print Bitmaps",
                                "Use Color for Links"};
        const float box = 8 * 1.5f * s;
        for (int i = 0; i < 5; ++i) {
            if (vi::Check(optionIds[i], P(6, 57 + 11 * float(i)), box, printOptions[i]))
                SetPrint(id, optionFields[i], printOptions[i] ? 1 : 0);
            vi::Text(ImVec2(P(6, 57 + 11 * float(i)).x + box + 3 * s, P(6, 57 + 11 * float(i)).y),
                     P(100, 67 + 11 * float(i)), labels[i]);
        }
        const float by = o.y + client.y - 23 * s;
        const bool cancel = vi::DialogButton("Cancel", ImVec2(o.x + client.x - 70 * s, by), s, ImGuiKey_Escape);
        const bool ok = vi::DialogButton("OK", ImVec2(o.x + client.x - 140 * s, by), s, ImGuiKey_Enter);
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

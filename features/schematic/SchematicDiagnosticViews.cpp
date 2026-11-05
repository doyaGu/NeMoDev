#include "features/schematic/SchematicDiagnosticViews.h"
#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevControls.h"
#include "ui/setup/SetupLayout.h"
#include <algorithm>

namespace vdev {
namespace {
const EditorField *Field(const EditorField &row, const char *key) {
    for (const auto &field : row.fields) if (field.key == key) return &field;
    return nullptr;
}
uint64_t Number(const EditorField &row, const char *key) { const auto *field = Field(row, key); return field ? field->integer : 0; }
std::string Text(const EditorField &row, const char *key) { const auto *field = Field(row, key); return field ? field->text : std::string(); }
ObjectRef Object(const EditorField &row, const char *key) { const auto *field = Field(row, key); return field ? field->object : ObjectRef{}; }
struct DiagnosticView {
    bool invalid;
    DocumentToken document{};
    EditorView schematic = 0;
    uint64_t revision = 0;
    std::vector<EditorField> rows, messageUses;
    std::vector<EditorField> selectedAddress;
    int messageType = -1;
    ObjectRef messageUse{};
    EditorOperation pending = 0;
    EditorOperation navigation = 0;
    std::string message;
    explicit DiagnosticView(bool invalid) : invalid(invalid) {}
    void Goto(const EditorClient &client, const EditorField &row) {
        const auto *address = Field(row, "address"); if (!address || address->type != EDITOR_RECORD) return;
        auto scope = client.Scope(); if (schematic) scope = client.Host()->scope(client.Host()->data, schematic);
        if ((schematic && scope.view != schematic) || scope.document != client.Scope().document) {
            message = "This Schematic position is no longer available"; return;
        }
        const auto values = BorrowEditorFields(address->fields);
        const auto result = EditorClient(client.Host(), scope.view).Command("script.navigate", {values.data(), values.data() + values.size()});
        if (result.status == EDITOR_QUEUED) navigation = result.operation;
        else if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) message = "This Schematic position is no longer available";
    }
    void Delete(const EditorClient &client, bool all) {
        std::vector<uint64_t> slots;
        for (const auto &row : rows) if (all || (Field(row, "address") && Field(row, "address")->fields == selectedAddress)) slots.push_back(Number(row, "slot"));
        const EditorValue values[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, revision},
            {EDITOR_TEXT("slots"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {slots.data(), uint32_t(slots.size())}}};
        const auto result = client.Command("schematic.delete-invalid-links", {values, values + 2});
        if (result.status == EDITOR_QUEUED) pending = result.operation;
        else if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) message = result.message.empty() ? "The links changed; refresh and select them again" : result.message;
        else { selectedAddress.clear(); revision = 0; }
    }
    void Invalid(const EditorClient &client, EditorViewFrame &frame) {
        const float s = setup::PageUnit();
        vi::DialogLayout layout;
        EditorPrepareViewWindow(frame);
        if (!vi::BeginScrollableWindow(frame.window.data, "Invalid Link Explorer", {420 * s, 240 * s}, s,
                                       layout)) {
            vi::EndWindow();
            return;
        }
        if (vi::BeginDialogContent("##invalid-content", layout)) {
            std::vector<vi::ListColumn> columns{{"In Script", 184 * s}, {"In Behavior", 184 * s}, {"", 0}};
            bool twice = false;
            const auto at = ImGui::GetCursorScreenPos();
            const auto clicked = vi::ListView(
                "##invalidlinks", at,
                {at.x + std::max(368 * s, ImGui::GetContentRegionAvail().x), at.y + 200 * s}, s, columns,
                int(rows.size()),
                [&](int r, int c) {
                    return c < 2 ? Text(rows[size_t(r)], c ? "graph" : "script") : std::string();
                },
                [&](int r) {
                    const auto *address = Field(rows[size_t(r)], "address");
                    return address && address->fields == selectedAddress;
                },
                &twice);
            if (clicked >= 0)
                if (const auto *address = Field(rows[size_t(clicked)], "address"))
                    selectedAddress = address->fields;
            const EditorField *picked = nullptr;
            for (const auto &row : rows)
                if (const auto *address = Field(row, "address");
                    address && address->fields == selectedAddress) {
                    picked = &row;
                    break;
                }
            setup::SetupColumns actions("##invalid-actions", 3);
            if (actions) {
                actions.Next();
                if ((vi::Button("Goto", "Goto", picked != nullptr) || twice) && picked)
                    Goto(client, *picked);
                actions.Next();
                if (vi::Button("Delete", "Delete", picked && !pending))
                    Delete(client, false);
                actions.Next();
                if (vi::Button("Delete All", "Delete All", !rows.empty() && !pending))
                    Delete(client, true);
            }
        }
        vi::EndDialogContent();
        if (vi::DialogButton("Close", ImVec2(layout.right - 60 * s, layout.buttonsY), s))
            frame.close = 1;
        if (!pending && vi::DialogButton("Refresh", ImVec2(layout.contentMin.x, layout.buttonsY), s)) {
            const auto result = client.Command("schematic.refresh-invalid-links");
            if (result.status == EDITOR_QUEUED) pending = result.operation;
            else if (result.status == EDITOR_OK) { revision = 0; selectedAddress.clear(); }
        }
        if (!message.empty()) ImGui::SetTooltip("%s", message.c_str());
        vi::EndWindow();
    }
    void Messages(const EditorClient &client, EditorViewFrame &frame) {
        ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
        EditorImGuiWindow window(frame); if (!window) return;
        const float unit = setup::PageUnit();
        const float height = 260 * unit;
        const EditorField *picked = nullptr;
        {
            setup::SetupColumns columns("##message-sections", 2);
            if (columns) {
                columns.Next();
                ImGui::BeginChild("messagelist", ImVec2(0, height), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextDisabled("Messages");
                const EditorField *chosen = nullptr;
                for (const auto &row : rows) {
                    const int type = int(Number(row, "type"));
                    const auto count = Number(row, "count");
                    const auto label = Text(row, "name") + (count ? "  (" + std::to_string(count) + ")" : "");
                    ImGui::PushID(type);
                    if (ImGui::Selectable(label.c_str(), messageType == type)) {
                        messageType = type;
                        messageUse = {};
                        messageUses.clear();
                    }
                    ImGui::PopID();
                    if (messageType == type)
                        chosen = &row;
                }
                ImGui::EndChild();
                columns.Next();
                const float half = (height - ImGui::GetStyle().ItemSpacing.y) * .5f;
                uint64_t usesRevision = 0;
                if (chosen) ReadSchematicRows(client, "schematic.message-uses", messageUses, usesRevision, {IntegerArgument("message", uint64_t(messageType))});
                auto list = [&](const char *id, const char *title, bool senders) {
                    ImGui::BeginChild(id, ImVec2(0, half), ImGuiChildFlags_Borders,
                                      ImGuiWindowFlags_HorizontalScrollbar);
                    ImGui::TextDisabled("%s", title);
                    if (chosen) for (const auto &use : messageUses) {
                            const auto object = Object(use, "object");
                            if ((Number(use, "sender") != 0) != senders)
                                continue;
                            if (object == messageUse)
                                picked = &use;
                            const auto label = Text(use, "name") + "   (" + Text(use, "script") + ")";
                            ImGui::PushID(int(object.object));
                            if (ImGui::Selectable(label.c_str(), messageUse == object,
                                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                                messageUse = object;
                                picked = &use;
                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                    Goto(client, use);
                            }
                            ImGui::PopID();
                        }
                    ImGui::EndChild();
                };
                list("senders", "Senders", true);
                list("receivers", "Receivers", false);
            }
        }
        ImGui::BeginDisabled(!picked); if (ImGui::Button("Goto", ImVec2(90, 0)) && picked) Goto(client, *picked); ImGui::EndDisabled();
        if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
    }
    void Draw(const EditorClient &client, EditorViewFrame &frame) {
        if (document != client.Scope().document) { document = client.Scope().document; rows.clear(); messageUses.clear(); selectedAddress.clear(); revision = 0; pending = navigation = 0; messageType = -1; messageUse = {}; message.clear(); }
        if (!document.value) return;
        if (navigation) {
            const auto result = client.Completion(navigation);
            if (result.status != EDITOR_QUEUED) {
                navigation = 0;
                if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) message = "This Schematic position is no longer available";
            }
        }
        if (pending) {
            const auto result = client.Completion(pending);
            if (result.status != EDITOR_QUEUED) {
                pending = 0;
                if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) { selectedAddress.clear(); revision = 0; }
                else message = result.message.empty() ? "The links changed; refresh and select them again" : result.message;
            }
        }
        ReadSchematicRows(client, invalid ? "schematic.invalid-links" : "schematic.messages", rows, revision);
        if (invalid) Invalid(client, frame); else Messages(client, frame);
    }
};
struct Window { const char *role; const char *type; };
EditorStatus Open(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    const auto &window = *static_cast<Window *>(data); const EditorClient client(host, request->scope.view);
    const auto opened = client.Command("workspace.open-role", {{EDITOR_TEXT("role"), EDITOR_TEXT_VALUE, {}, EditorLiteral(window.role)}});
    if (opened.status != EDITOR_OK) return opened.status;
    const auto view = EditorClient::Integer(opened, "view");
    const EditorValue parent{EDITOR_TEXT("schematic"), EDITOR_INTEGER_VALUE, {}, {}, request->scope.view};
    const EditorRequest navigation{request->scope, &parent, 1};
    return host->navigateView(host->data, EditorLiteral(window.type), view, &navigation, sink);
}
} // namespace
bool ReadSchematicRows(const EditorClient &client, const char *query, std::vector<EditorField> &rows, uint64_t &revision, const std::vector<EditorValue> &arguments) {
    std::vector<EditorField> next; uint64_t first = 0, expected = 0;
    do {
        auto args = arguments; args.push_back(IntegerArgument("first", first)); args.push_back(IntegerArgument("revision", expected));
        const auto result = client.Query(query, args);
        if (result.status != EDITOR_OK) return false;
        const auto *items = EditorClient::Find(result, "rows"); const auto end = EditorClient::Integer(result, "next"), total = EditorClient::Integer(result, "total"), version = EditorClient::Integer(result, "revision");
        if (!items || items->type != EDITOR_RECORD || end < first || end > total || end - first != items->fields.size() || total > 1000000 ||
            (end == first && first < total) || (first && version != expected)) return false;
        expected = version; next.insert(next.end(), items->fields.begin(), items->fields.end()); first = end;
        if (first == total) break;
    } while (true);
    rows = std::move(next); revision = expected; return true;
}
bool RegisterSchematicDiagnosticViews(EditorApplication &editor, std::string &error) {
    static bool kinds[] = {true, false};
    const char *ids[] = {"schematic.invalid-links", "schematic.messages"}, *titles[] = {"Invalid Link Explorer", "Message Explorer"};
    static const EditorParameter nav{EDITOR_TEXT("schematic"), EDITOR_INTEGER_VALUE, 1};
    static const EditorParameter fields[] = {{EDITOR_TEXT("rows"), EDITOR_RECORD}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("schematic"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE}};
    EditorViewDesc views[2]{};
    for (size_t i = 0; i < 2; ++i) {
        auto &view = views[i]; view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION; view.id = EditorLiteral(ids[i]);
        view.provider = EDITOR_TEXT("builtin.schematic-diagnostics"); view.title = EditorLiteral(titles[i]); view.area = EDITOR_WORKSPACE; view.data = kinds + i;
        view.create = [](void *data, const EditorHost *, EditorView) -> void * { return new DiagnosticView(*static_cast<bool *>(data)); };
        view.destroy = [](void *data) { delete static_cast<DiagnosticView *>(data); };
        view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) { static_cast<DiagnosticView *>(data)->Draw(EditorClient(host, id), *frame); };
        view.parameters = &nav; view.count = 1; view.fields = fields; view.fieldCount = 4;
        view.navigate = [](void *data, const EditorHost *, const EditorRequest *request) {
            auto &view = *static_cast<DiagnosticView *>(data);
            for (uint32_t i = 0; i < request->count; ++i) view.schematic = request->values[i].integer;
            return EDITOR_OK;
        };
        view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const auto &view = *static_cast<DiagnosticView *>(data); EditorField rows{"rows", EDITOR_RECORD}; rows.fields = view.rows;
            const std::vector<EditorField> fields{std::move(rows), {"revision", EDITOR_INTEGER_VALUE, {}, {}, view.revision},
                {"schematic", EDITOR_INTEGER_VALUE, {}, {}, view.schematic}, {"message", EDITOR_TEXT_VALUE, {}, view.message}};
            const auto values = BorrowEditorFields(fields); return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
        };
    }
    static Window windows[] = {{"main.invalid-links", "schematic.invalid-links"}, {"main.messages", "schematic.messages"}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.open-invalid-links"), EDITOR_TEXT("builtin.schematic-diagnostics"), EDITOR_TEXT("Invalid Link Explorer"), nullptr, 0, Open, windows, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.open-messages"), EDITOR_TEXT("builtin.schematic-diagnostics"), EDITOR_TEXT("Message Explorer"), nullptr, 0, Open, windows + 1, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE}};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("schematic.diagnostics");
    module.views = views; module.viewCount = 2; module.commands = commands; module.commandCount = 2;
    return editor.Register(module, error);
}
} // namespace vdev

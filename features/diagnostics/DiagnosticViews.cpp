#include "features/diagnostics/DiagnosticViews.h"
#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"
#include "ui/controls/DevControls.h"
#include "ui/setup/SetupLayout.h"
#include "workspace/EditorClient.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace vdev {
namespace {
const EditorField *Field(const EditorField &row, const char *key) {
    for (const auto &field : row.fields) if (field.key == key) return &field;
    return nullptr;
}
uint64_t Number(const EditorField &row, const char *key) { const auto *field = Field(row, key); return field ? field->integer : 0; }
const std::string &Text(const EditorField &row, const char *key) { static const std::string empty; const auto *field = Field(row, key); return field ? field->text : empty; }
ObjectRef Object(const EditorField &row) { const auto *field = Field(row, "object"); return field ? field->object : ObjectRef{}; }
EditorStatus State(const EditorClient &client, const char *id, const std::vector<EditorValue> &args = {}) {
    const EditorRequest request{client.Scope(), args.data(), uint32_t(args.size())};
    return client.Host()->commandState(client.Host()->data, EditorLiteral(id), &request);
}
enum class Kind { Unused, Versions, Memory };
const char *Query(Kind kind) { return kind == Kind::Unused ? "diagnostics.unused" : kind == Kind::Versions ? "diagnostics.versions" : "diagnostics.memory"; }
const char *Scan(Kind kind) { return kind == Kind::Unused ? "diagnostics.scan-unused" : kind == Kind::Versions ? "diagnostics.scan-versions" : "diagnostics.scan-memory"; }
struct CheckView {
    Kind kind;
    DocumentToken document{};
    bool started = false;
    EditorOperation scan = 0, change = 0;
    uint64_t revision = 0, version = 0, bytes = 0;
    std::vector<EditorField> rows;
    std::vector<size_t> order;
    std::set<ObjectRef> excluded;
    std::set<size_t> selected;
    int sort = -1, anchor = -1;
    bool ascending = true;
    std::string report, message;
    explicit CheckView(Kind kind) : kind(kind) {}
    bool Read(const EditorClient &client, bool force = false) {
        const auto current = client.Revision(Query(kind));
        if (!force && version && current == version) return true;
        std::vector<EditorField> next;
        uint64_t first = 0, expected = 0;
        EditorResponse page;
        do {
            page = client.Query(Query(kind), {IntegerArgument("first", first), IntegerArgument("revision", expected)});
            if (page.status != EDITOR_OK) {
                if (page.status == EDITOR_NO_CHANGE) {
                    rows.clear(); order.clear(); selected.clear(); excluded.clear();
                    anchor = sort = -1; revision = 0; report.clear(); bytes = 0;
                }
                return false;
            }
            const auto *items = EditorClient::Find(page, "rows");
            const auto end = EditorClient::Integer(page, "next"), total = EditorClient::Integer(page, "total");
            const auto published = EditorClient::Integer(page, "revision");
            if (!items || end < first || end > total || (end == first && end < total) || (expected && published != expected)) return false;
            expected = published; next.insert(next.end(), items->fields.begin(), items->fields.end());
            if (end == total) break;
            first = end;
        } while (true);
        // A different query provider, or another diagnostic's scan, may
        // publish a new cache version without changing these rows. Keep this
        // instance's exclusions, selection and sort in that case.
        if (!force && revision == expected && rows == next) {
            version = current; report = EditorClient::Text(page, "report"); bytes = EditorClient::Integer(page, "bytes");
            return true;
        }
        rows = std::move(next); revision = expected; version = current;
        report = EditorClient::Text(page, "report"); bytes = EditorClient::Integer(page, "bytes");
        order.resize(rows.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        excluded.clear(); selected.clear(); anchor = sort = -1; ascending = true;
        return true;
    }
    void Refresh(const EditorClient &client) {
        const auto result = client.Command(Scan(kind));
        started = true;
        if (result.status == EDITOR_QUEUED) scan = result.operation;
        else if (result.status == EDITOR_OK) { message.clear(); Read(client, true); }
        else message = result.message.empty() ? "The check could not be started." : result.message;
    }
    void Complete(const EditorClient &client, EditorViewFrame &frame) {
        if (document != client.Scope().document) {
            document = client.Scope().document; started = false; scan = change = 0;
            rows.clear(); order.clear(); selected.clear(); excluded.clear(); report.clear(); message.clear(); revision = version = bytes = 0;
        }
        if (scan) {
            const auto result = client.Completion(scan);
            if (result.status != EDITOR_QUEUED) {
                scan = 0;
                if (result.status == EDITOR_OK) { message.clear(); Read(client, true); }
                else message = result.message.empty() ? "The check could not be completed." : result.message;
            }
        }
        if (change) {
            const auto result = client.Completion(change);
            if (result.status != EDITOR_QUEUED) {
                change = 0;
                if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) frame.close = 1;
                else
                    message = result.message.empty() ? result.status == EDITOR_STALE
                                                           ? "The composition changed. Run the check again "
                                                             "before applying."
                                                           : "The operation failed."
                                                     : result.message;
            }
        }
        if (!started && State(client, Scan(kind)) == EDITOR_OK) Refresh(client);
        if (!scan && started && revision) Read(client);
    }
    void Apply(const EditorClient &client, const char *id, const std::vector<EditorValue> &args, EditorViewFrame &frame) {
        const auto result = client.Command(id, args);
        if (result.status == EDITOR_QUEUED) change = result.operation;
        else if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) frame.close = 1;
        else
            message = result.message.empty() ? "The composition changed or the operation failed. Run "
                                               "the check again."
                                             : result.message;
    }
    void Unused(const EditorClient &client, EditorViewFrame &frame) {
        const float footer = ImGui::GetFrameHeightWithSpacing() * 2 + 8;
        if (ImGui::BeginTable("unused", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, -footer))) {
            ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Exclude", ImGuiTableColumnFlags_WidthFixed, 60); ImGui::TableHeadersRow();
            std::map<uint64_t, std::vector<size_t>> groups;
            for (size_t i = 0; i < rows.size(); ++i) groups[Number(rows[i], "class")].push_back(i);
            for (const auto &group : groups) {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                if (!ImGui::TreeNodeEx(Text(rows[group.second.front()], "class-name").c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns)) continue;
                for (auto i : group.second) {
                    const auto ref = Object(rows[i]); ImGui::PushID(int(i)); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Indent();
                    const auto name = Text(rows[i], "name"); ImGui::TextUnformatted(name.empty() ? "<Unnamed>" : name.c_str()); ImGui::Unindent();
                    ImGui::TableSetColumnIndex(1);
                    const bool on = excluded.count(ref) != 0;
                    if (ImGui::Selectable(on ? "A" : "-", on)) { if (on) excluded.erase(ref); else excluded.insert(ref); }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::EndTable();
        }
        ImGui::Text("%llu unused object(s)", static_cast<unsigned long long>(rows.size()));
        if (ImGui::Button("Refresh") && !scan) Refresh(client);
        const std::vector<ObjectRef> none;
        const std::vector<EditorValue> availability{IntegerArgument("revision", revision), ObjectsArgument("objects", none)};
        ImGui::SameLine(); ImGui::BeginDisabled(scan || change || excluded.size() == rows.size() || State(client, "diagnostics.delete-unused", availability) != EDITOR_OK);
        if (ImGui::Button("Delete")) {
            std::vector<ObjectRef> targets;
            for (const auto &row : rows) if (!excluded.count(Object(row))) targets.push_back(Object(row));
            Apply(client, "diagnostics.delete-unused", {IntegerArgument("revision", revision), ObjectsArgument("objects", targets)}, frame);
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Cancel") || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))) frame.close = 1;
    }
    void Versions(const EditorClient &client, EditorViewFrame &frame) {
        ImGui::PushTextWrapPos(520); ImGui::TextUnformatted(report.empty() ? scan || !started ? "Checking the composition..." : "No result available." : report.c_str());
        if (!rows.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Would you like to replace building blocks whose prototype have "
                                   "changed?\n"
                                   "(warning: the composition may not work if you choose yes, you "
                                   "should have to update some links manually)");
        }
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Refresh") && !scan) Refresh(client);
        ImGui::SameLine();
        if (!rows.empty()) {
            const std::vector<EditorValue> args{IntegerArgument("revision", revision)};
            ImGui::BeginDisabled(scan || change || State(client, "diagnostics.replace-versions", args) != EDITOR_OK);
            if (ImGui::Button("Yes", ImVec2(90, 0))) Apply(client, "diagnostics.replace-versions", args, frame);
            ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button("No", ImVec2(90, 0)) || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))) frame.close = 1;
        } else if (ImGui::Button("OK", ImVec2(90, 0)) || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)))) frame.close = 1;
    }
    void Memory(const EditorClient &, EditorViewFrame &frame) {
        const float s = setup::PageUnit();
        vi::DialogLayout layout;
        EditorPrepareViewWindow(frame);
        if (!vi::BeginScrollableWindow(frame.window.data, "All CK Object In Memory", {600 * s, 540 * s}, s,
                                       layout)) {
            vi::EndWindow();
            return;
        }
        if (vi::BeginDialogContent("##memory-content", layout)) {
            std::vector<vi::ListColumn> columns{
                {"Name", 130}, {"Class", 120, vi::CENTER}, {"ID", 30}, {"Size", 60}, {"Visi.", 30},
                {"Dyn.", 30},  {"IsInCurrentScene", 60}};
            for (auto &column : columns)
                column.width *= s;
            float width = 0;
            for (const auto &column : columns)
                width += column.width;
            width = std::max(width, ImGui::GetContentRegionAvail().x);
            int header = -1;
            const auto at = ImGui::GetCursorScreenPos();
            const auto clicked = vi::ListView(
                "##objects", at, {at.x + width, at.y + 460 * s}, s, columns, int(order.size()),
                [&](int r, int c) {
                    const auto &row = rows[order[size_t(r)]];
                    switch (c) {
                    case 0:
                        return Text(row, "name");
                    case 1:
                        return Text(row, "class-name");
                    case 2:
                        return std::to_string(Number(row, "id"));
                    case 3:
                        return std::to_string(Number(row, "size"));
                    case 4:
                        return std::string(Number(row, "visible") ? "X" : "");
                    case 5:
                        return std::string(Number(row, "dynamic") ? "X" : "");
                    default:
                        return std::string(Number(row, "in-scene") ? "X" : "");
                    }
                },
                [&](int r) { return selected.count(order[size_t(r)]) != 0; }, nullptr, nullptr, false,
                &header);
            if (clicked >= 0) {
                const auto &io = ImGui::GetIO();
                const auto row = order[size_t(clicked)];
                if (io.KeyShift && anchor >= 0) {
                    selected.clear();
                    for (int r = std::min(clicked, anchor); r <= std::max(clicked, anchor); ++r)
                        selected.insert(order[size_t(r)]);
                } else if (io.KeyCtrl) {
                    if (selected.count(row))
                        selected.erase(row);
                    else
                        selected.insert(row);
                    anchor = clicked;
                } else {
                    selected = {row};
                    anchor = clicked;
                }
            }
            if (header >= 0) {
                ascending = sort == header ? !ascending : true;
                sort = header;
                std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                    int comparison = 0;
                    if (header < 2)
                        comparison = ImStricmp(Text(rows[a], header ? "class-name" : "name").c_str(),
                                               Text(rows[b], header ? "class-name" : "name").c_str());
                    else {
                        const char *key = header == 2   ? "id"
                                          : header == 3 ? "size"
                                          : header == 4 ? "visible"
                                          : header == 5 ? "dynamic"
                                                        : "in-scene";
                        const auto x = Number(rows[a], key), y = Number(rows[b], key);
                        comparison = x < y ? -1 : x > y ? 1 : 0;
                        if (header > 3)
                            comparison = -comparison;
                    }
                    return ascending ? comparison < 0 : comparison > 0;
                });
                anchor = -1;
            }
            uint64_t selectedBytes = 0;
            for (auto row : selected)
                selectedBytes += Number(rows[row], "size");
            if (!message.empty())
                ImGui::TextWrapped("%s", message.c_str());
            ImGui::TextWrapped("Total Object Size = %llu", static_cast<unsigned long long>(bytes));
            ImGui::TextWrapped("Selected Objects (%llu) Size = %llu",
                               static_cast<unsigned long long>(selected.size()),
                               static_cast<unsigned long long>(selectedBytes));
        }
        vi::EndDialogContent();
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (vi::DialogButton("OK", ImVec2(layout.right - 60 * s, layout.buttonsY), s,
                             focused ? ImGuiKey_Enter : ImGuiKey_None) ||
            (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            frame.close = 1;
        vi::EndWindow();
    }
    void Draw(const EditorClient &client, EditorViewFrame &frame) {
        Complete(client, frame);
        if (frame.close) return;
        if (kind == Kind::Memory) { Memory(client, frame); return; }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
        if (kind == Kind::Unused) ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_Appearing);
        EditorImGuiWindow window(frame, ImGuiWindowFlags_NoDocking | (kind == Kind::Unused ? ImGuiWindowFlags_None : ImGuiWindowFlags_AlwaysAutoResize));
        if (!window) return;
        if (kind == Kind::Unused) Unused(client, frame);
        else Versions(client, frame);
        if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
    }
};
struct Window { const char *role; bool document; };
EditorStatus Open(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    const auto &window = *static_cast<Window *>(data);
    const EditorValue role{EDITOR_TEXT("role"), EDITOR_TEXT_VALUE, {}, EditorLiteral(window.role)};
    const EditorRequest next{request->scope, &role, 1};
    return host->execute(host->data, EditorString EDITOR_TEXT("workspace.open-role"), &next, sink);
}
EditorStatus OpenState(void *data, const EditorHost *, const EditorRequest *request) {
    return static_cast<Window *>(data)->document && !request->scope.document.value ? EDITOR_REFUSED : EDITOR_OK;
}
EditorViewDesc View(const char *id, const char *title, Kind *data) {
    EditorViewDesc view{}; view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EditorLiteral(id); view.provider = EDITOR_TEXT("builtin.diagnostic-views"); view.title = EditorLiteral(title);
    view.area = EDITOR_APPLICATION; view.data = data;
    view.create = [](void *data, const EditorHost *, EditorView) -> void * { return new CheckView(*static_cast<Kind *>(data)); };
    view.destroy = [](void *data) { delete static_cast<CheckView *>(data); };
    view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) { static_cast<CheckView *>(data)->Draw(EditorClient(host, id), *frame); };
    static const EditorParameter fields[] = {{EDITOR_TEXT("rows"), EDITOR_RECORD}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("report"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("selected"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("excluded"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("sort"), EDITOR_INTEGER_VALUE}};
    view.fields = fields; view.fieldCount = uint32_t(std::size(fields));
    view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &view = *static_cast<CheckView *>(data); EditorField rows{"rows", EDITOR_RECORD}; rows.fields = view.rows;
        std::vector<EditorField> fields{rows, {"revision", EDITOR_INTEGER_VALUE, {}, {}, view.revision}, {"report", EDITOR_TEXT_VALUE, {}, view.report},
            {"selected", EDITOR_OBJECT_LIST}, {"excluded", EDITOR_OBJECT_LIST}, {"message", EDITOR_TEXT_VALUE, {}, view.message},
            {"sort", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(view.sort))}};
        for (auto i : view.selected) fields[3].objects.push_back(Object(view.rows[i]));
        fields[4].objects.assign(view.excluded.begin(), view.excluded.end());
        const auto values = BorrowEditorFields(fields); return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
    };
    return view;
}
struct About {};
EditorViewDesc AboutView() {
    EditorViewDesc view{}; view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("diagnostics.about"); view.provider = EDITOR_TEXT("builtin.diagnostic-views");
    view.title = EDITOR_TEXT("About Virtools"); view.area = EDITOR_APPLICATION;
    view.create = [](void *, const EditorHost *, EditorView) -> void * { return new About; };
    view.destroy = [](void *data) { delete static_cast<About *>(data); };
    view.draw = [](void *, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
        const auto data = EditorClient(host, id).Query("diagnostics.info");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
        EditorImGuiWindow window(*frame, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);
        if (!window) return;
        ImGui::TextUnformatted("NeMoDev"); ImGui::TextUnformatted("An editor for Virtools compositions."); ImGui::Separator();
        ImGui::Text("CK version %llx", static_cast<unsigned long long>(EditorClient::Integer(data, "version")));
        ImGui::Text("Runtime: %s", EditorClient::Text(data, "runtime").c_str());
        ImGui::Text("%llu Building Block DLLs, %llu prototypes, %llu managers", static_cast<unsigned long long>(EditorClient::Integer(data, "dlls")),
            static_cast<unsigned long long>(EditorClient::Integer(data, "prototypes")), static_cast<unsigned long long>(EditorClient::Integer(data, "managers")));
        ImGui::Text("Dear ImGui %s", ImGui::GetVersion()); ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(90, 0)) || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)))) frame->close = 1;
    };
    return view;
}
EditorStatus CloseTools(void *, const EditorHost *host, const EditorRequest *, EditorSink) {
    const EditorClient client(host, 0); const auto instances = client.Query("workspace.instances");
    const auto *types = EditorClient::Find(instances, "types"), *ids = EditorClient::Find(instances, "views");
    if (instances.status != EDITOR_OK || !types || !ids || types->texts.size() != ids->integers.size()) return EDITOR_ERROR;
    for (size_t i = 0; i < types->texts.size(); ++i) if (types->texts[i].rfind("diagnostics.", 0) == 0) {
        const auto result = client.Command("workspace.close-view", {IntegerArgument("view", ids->integers[i])});
        if (result.status != EDITOR_OK) return result.status;
    }
    return EDITOR_OK;
}
} // namespace
bool RegisterDiagnosticViews(EditorApplication &editor, std::string &error) {
    static Kind kinds[] = {Kind::Unused, Kind::Versions, Kind::Memory};
    const EditorViewDesc views[] = {View("diagnostics.unused", "Unused Object Explorer", &kinds[0]),
        View("diagnostics.versions", "Building Block Version Test", &kinds[1]),
        View("diagnostics.memory", "All CK Object In Memory", &kinds[2]), AboutView()};
    static Window windows[] = {{"main.unused", true}, {"main.versions", true}, {"main.memory", false}, {"main.about", false}};
    const char *ids[] = {"diagnostics.open-unused", "diagnostics.open-versions", "diagnostics.open-memory", "diagnostics.open-about"};
    std::vector<EditorCommandDesc> commands;
    for (size_t i = 0; i < 4; ++i) commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(ids[i]), EDITOR_TEXT("builtin.diagnostic-views"),
        views[i].title, nullptr, 0, Open, &windows[i], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE, OpenState});
    commands.push_back({sizeof(EditorCommandDesc), EDITOR_TEXT("diagnostics.close-tools"), EDITOR_TEXT("builtin.diagnostic-views"),
        EDITOR_TEXT("Close Diagnostic Tools"), nullptr, 0, CloseTools, nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE});
    const EditorMenuLocation locations[] = {{EDITOR_TEXT("options.unused"), EDITOR_TEXT("Unused object tools")},
        {EDITOR_TEXT("options.versions"), EDITOR_TEXT("Building Block version tools")}, {EDITOR_TEXT("help.about"), EDITOR_TEXT("About")}};
    const EditorMenuDesc menus[] = {{EDITOR_TEXT("diagnostics.unused"), EDITOR_TEXT("diagnostics.open-unused"), EDITOR_TEXT("options.unused"), EDITOR_TEXT("Unused Object Explorer"), 0},
        {EDITOR_TEXT("diagnostics.versions"), EDITOR_TEXT("diagnostics.open-versions"), EDITOR_TEXT("options.versions"), EDITOR_TEXT("Building Block Version Test"), 0},
        {EDITOR_TEXT("diagnostics.about"), EDITOR_TEXT("diagnostics.open-about"), EDITOR_TEXT("help.about"), EDITOR_TEXT("About Virtools..."), 0}};
    const EditorBindingDesc binding{EDITOR_TEXT("diagnostics.open-memory"), EDITOR_TEXT("Ctrl+Alt+F12")};
    const EditorString dependencies[] = {EDITOR_TEXT("diagnostics.checks"), EDITOR_TEXT("workspace.views"), EDITOR_TEXT("schematic.editing")};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("diagnostics.views");
    module.commands = commands.data(); module.commandCount = uint32_t(commands.size()); module.views = views; module.viewCount = 4;
    module.locations = locations; module.locationCount = 3; module.menus = menus; module.menuCount = 3; module.bindings = &binding; module.bindingCount = 1;
    module.dependencies = dependencies; module.dependencyCount = uint32_t(std::size(dependencies));
    return editor.Register(module, error);
}
} // namespace vdev

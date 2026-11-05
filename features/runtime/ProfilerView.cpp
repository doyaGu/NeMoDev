#include "features/runtime/RuntimeViewData.h"
#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevControls.h"
#include "ui/setup/SetupLayout.h"
#include <algorithm>

namespace vdev {
namespace runtime {
namespace {
void Bar(double fraction, bool known) {
    const ImVec2 size(std::max(1.f, ImGui::GetContentRegionAvail().x - 80 * setup::PageUnit()),
                      ImGui::GetTextLineHeight()),
        at = ImGui::GetCursorScreenPos();
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(100, 100, 100, 255));
    if (known && fraction > 0)
        draw->AddRectFilled(at, ImVec2(at.x + size.x * float(std::min(1.0, fraction)), at.y + size.y), IM_COL32(220, 120, 40, 255));
    ImGui::Dummy(size);
}
void TimeRow(setup::SetupForm &form, const char *label, double time, double total) {
    form.Row(label);
    Bar(total > 0 ? time / total : 0, total > 0);
    ImGui::SameLine();
    if (total > 0) ImGui::Text("%.2f ms", time);
    else ImGui::TextUnformatted("NA");
}
struct ProfileRow {
    std::string name;
    double time = 0;
    ObjectRef object{}, script{};
    bool dynamic = false;
    std::vector<size_t> children;
};
struct ProfilerViewState {
    bool details = false, sorted = false, observing = false;
    EditorOperation observation = 0;
    uint64_t version = 0;
    DocumentToken document{};
    std::vector<ProfileRow> rows;
    std::vector<size_t> roots;
    void Draw(const EditorClient &, EditorViewFrame &);
    bool ReadRows(const EditorClient &, uint64_t revision, uint64_t count);
    void Details(const EditorClient &, bool dynamic);
};
bool ProfilerViewState::ReadRows(const EditorClient &client, uint64_t revision, uint64_t count) {
    std::vector<ProfileRow> next;
    std::vector<size_t> nextRoots;
    if (count > 1000000) return false;
    while (next.size() < count) {
        const auto page = client.Query("runtime.profile-rows", {IntegerArgument("first", next.size()), IntegerArgument("revision", revision)});
        const auto *values = EditorClient::Find(page, "rows");
        if (page.status != EDITOR_OK || Integer(page, "revision") != revision || Integer(page, "total") != count ||
            !values || values->fields.empty() || next.size() + values->fields.size() > count) return false;
        for (const auto &record : values->fields) {
            const auto find = [&](const char *key) -> const EditorField * {
                for (const auto &field : record.fields) if (field.key == key) return &field;
                return nullptr;
            };
            const auto *name = find("name"), *time = find("time"), *parent = find("parent"),
                       *object = find("object"), *script = find("script"), *dynamic = find("dynamic");
            if (!name || !time || !parent || !object || !script || !dynamic ||
                name->type != EDITOR_TEXT_VALUE || time->type != EDITOR_REAL_VALUE || parent->type != EDITOR_INTEGER_VALUE ||
                object->type != EDITOR_OBJECT || script->type != EDITOR_OBJECT || dynamic->type != EDITOR_INTEGER_VALUE) return false;
            const auto index = next.size();
            const auto owner = int64_t(parent->integer);
            if (owner < -1 || owner >= int64_t(index)) return false;
            next.push_back({name->text, time->real, object->object, script->object, dynamic->integer != 0, {}});
            if (owner < 0) nextRoots.push_back(index);
            else next[size_t(owner)].children.push_back(index);
        }
    }
    rows = std::move(next); roots = std::move(nextRoots); version = revision;
    return true;
}
void ProfilerViewState::Details(const EditorClient &client, bool dynamic) {
    if (!ImGui::BeginTable("details", 2,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                           ImVec2(0, 260 * setup::PageUnit())))
        return;
    ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80 * setup::PageUnit());
    const auto order = [&](const std::vector<size_t> &indices, bool managers = false) {
        auto result = indices;
        if (sorted) std::stable_sort(result.begin(), result.end(), [&](size_t a, size_t b) {
            // The fixed User/Render/Managers groups retain their native order.
            if (!managers && (!rows[a].object || !rows[b].object)) return a < b;
            return rows[a].time > rows[b].time;
        });
        return result;
    };
    const auto draw = [&](auto &&self, size_t index) -> void {
        const auto &row = rows[index];
        if (row.dynamic && !dynamic) return;
        ImGui::PushID(int(index)); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
        const bool leaf = row.children.empty();
        const bool open = ImGui::TreeNodeEx(row.name.c_str(), leaf ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : ImGuiTreeNodeFlags_SpanAllColumns);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            Show(client, row.script, row.object == row.script ? ObjectRef{} : row.object);
        ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f ms", row.time);
        if (open && !leaf) {
            for (auto child : order(row.children, row.name == "Managers")) self(self, child);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    for (auto root : order(roots)) draw(draw, root);
    ImGui::EndTable();
}
void ProfilerViewState::Draw(const EditorClient &client, EditorViewFrame &frame) {
    if (!observing) {
        const auto result = observation ? client.Completion(observation) :
            client.Command("runtime.observe-profile", {IntegerArgument("enabled", 1)});
        if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) { observing = true; observation = 0; }
        else if (result.status == EDITOR_QUEUED && result.operation) observation = result.operation;
    }
    if (document != client.Scope().document) {
        document = client.Scope().document; rows.clear(); roots.clear(); version = 0;
    }
    const float s = setup::PageUnit();
    vi::DialogLayout layout;
    EditorPrepareViewWindow(frame);
    if (!vi::BeginScrollableWindow(frame.window.data, "Profiler", {800 * s, 500 * s}, s, layout)) {
        vi::EndWindow();
        return;
    }
    if (vi::BeginDialogContent("##profiler-content", layout)) {
        const auto profile = client.Query("runtime.profile", {IntegerArgument("details", details)});
        const auto state = client.Query("runtime.state");
        if (profile.status != EDITOR_OK || !Integer(profile, "available") || !Integer(state, "available")) {
            ImGui::TextDisabled("No composition is open");
        } else {
            const auto number = [&](const char *key) { return Integer(profile, key); };
            const auto time = [&](const char *key) { return Real(profile, key); };
            const double total = time("frame-time");
            {
                setup::SetupColumns columns("##profile-sections", 2);
                if (columns) {
                    columns.Next();
                    ImGui::TextUnformatted("Execution Times");
                    ImGui::Separator();
                    {
                        setup::SetupForm form("##execution-times");
                        if (form) {
                            TimeRow(form, "Interface/Windows", time("interface-time"), total);
                            TimeRow(form, "Rendering", time("render-time"), total);
                            TimeRow(form, "Behaviors Graphs Management", time("graph-time"), total);
                            TimeRow(form, "Behaviors Code Execution", time("behavior-time"), total);
                            TimeRow(form, "Parametric Operations", time("operation-time"),
                                    total > 0 ? time("behavior-time") : 0);
                            TimeRow(form, "Animations", time("animation-time"),
                                    total > 0 ? time("behavior-time") : 0);
                        }
                    }
                    columns.Next();
                    ImGui::TextUnformatted("Database");
                    ImGui::Separator();
                    ImGui::Text("3D Objects  %llu", static_cast<unsigned long long>(number("objects")));
                    ImGui::Text("Textures    %llu", static_cast<unsigned long long>(number("textures")));
                    ImGui::Text("Sprites     %llu", static_cast<unsigned long long>(number("sprites")));
                    ImGui::Text("Behaviors   %llu", static_cast<unsigned long long>(number("behaviors")));
                    ImGui::Text("Faces       %llu", static_cast<unsigned long long>(number("faces")));
                    ImGui::Text("Vertices    %llu", static_cast<unsigned long long>(number("vertices")));
                    ImGui::BeginDisabled(!Ready(client));
                    if (ImGui::Button("Refresh"))
                        client.Command("runtime.refresh-profile");
                    ImGui::EndDisabled();
                    columns.Next();
                    ImGui::TextUnformatted("Render Engine");
                    ImGui::Separator();
                    if (total > 0 && Integer(state, "state") == EDITOR_EXECUTION_RUNNING)
                        ImGui::Text("Framerate       %.1f", 1000 / total);
                    else
                        ImGui::TextUnformatted("Framerate       -");
                    ImGui::Text("Faces drawn     %llu",
                                static_cast<unsigned long long>(number("drawn-faces")));
                    ImGui::Text("Objects Drawn   %llu",
                                static_cast<unsigned long long>(number("drawn-objects")));
                    ImGui::Text("Lines drawn     %llu",
                                static_cast<unsigned long long>(number("drawn-lines")));
                    columns.Next();
                    ImGui::TextUnformatted("Video");
                    ImGui::Separator();
                    const auto bytes = number("texture-bytes");
                    if (bytes <= 0x100000)
                        ImGui::Text("Textures   %llu ( %llu KB)",
                                    static_cast<unsigned long long>(number("video-textures")),
                                    static_cast<unsigned long long>(bytes / 1024));
                    else
                        ImGui::Text("Textures   %llu ( %.3g MB)",
                                    static_cast<unsigned long long>(number("video-textures")),
                                    bytes / 1048576.0);
                    ImGui::Text("Sprites    %llu ( %.3g Mb)",
                                static_cast<unsigned long long>(number("video-sprites")),
                                number("sprite-bytes") / 1048576.0);
                    columns.Next();
                    ImGui::TextUnformatted("Behavioral Engine");
                    ImGui::Separator();
                    ImGui::Text("Objects Processed     %llu",
                                static_cast<unsigned long long>(number("executed-objects")));
                    ImGui::Text("Behaviors Executed    %llu",
                                static_cast<unsigned long long>(number("executed-behaviors")));
                    ImGui::Text("    ( Building Blocks) %llu",
                                static_cast<unsigned long long>(number("executed-blocks")));
                    columns.Next();
                    ImGui::Text("Links Parsed     %llu",
                                static_cast<unsigned long long>(number("parsed-links")));
                    ImGui::Text("Delayed Links    %llu",
                                static_cast<unsigned long long>(number("delayed-links")));
                }
            }
            if (ImGui::ArrowButton("##details", details ? ImGuiDir_Up : ImGuiDir_Down))
                details = !details;
            ImGui::SameLine();
            ImGui::TextUnformatted("Details");
            if (details) {
                ImGui::Checkbox("Sort By Execution time", &sorted);
                if (version != number("revision"))
                    ReadRows(client, number("revision"), number("row-count"));
                Details(client, number("show-dynamic") != 0);
            }
        }
    }
    vi::EndDialogContent();
    ImGui::SetCursorScreenPos({layout.right - 90 * s, layout.buttonsY});
    if (ImGui::Button("Close", ImVec2(90 * s, 0)))
        frame.close = 1;
    vi::EndWindow();
}
} // namespace
EditorViewDesc ProfilerView() {
    EditorViewDesc view{};
    view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("runtime.profiler"); view.provider = EDITOR_TEXT("builtin.runtime-views");
    view.title = EDITOR_TEXT("Profiler"); view.area = EDITOR_APPLICATION;
    view.create = [](void *, const EditorHost *, EditorView) -> void * { return new ProfilerViewState; };
    view.destroy = [](void *data) { delete static_cast<ProfilerViewState *>(data); };
    view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
        static_cast<ProfilerViewState *>(data)->Draw(EditorClient(host, id), *frame);
    };
    return view;
}
} // namespace runtime
} // namespace vdev

#include "ui/panels/EventLogView.h"
#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include <cfloat>
#include <limits>

namespace vdev {
namespace {
// Dev's Event Log (dialog 572): the resizable status-bar window, initially
// anchored at the work area's bottom-right, with its original Clear/Close buttons.
class EventLogView {
  public:
    void Draw(const EditorHost *, EditorView, EditorViewFrame &);
  private:
    void Read(const EditorHost *host, EditorView id) {
        const EditorRequest request{host->scope(host->data, id), nullptr, 0};
        struct Summary { uint64_t revision, count; } summaryValues{m_Revision, 0};
        const EditorSink summary{&summaryValues, [](void *data, const EditorValue *fields, uint32_t count) {
            for (uint32_t i = 0; i < count; ++i)
                if (std::string(fields[i].key.data, fields[i].key.size) == "revision")
                    static_cast<Summary *>(data)->revision = fields[i].integer;
                else if (std::string(fields[i].key.data, fields[i].key.size) == "count")
                    static_cast<Summary *>(data)->count = fields[i].integer;
            return EDITOR_OK;
        }};
        if (host->query(host->data, EditorString EDITOR_TEXT("log.summary"), &request, summary) != EDITOR_OK ||
            summaryValues.revision == m_Revision) return;
        const auto revision = summaryValues.revision, count = summaryValues.count;
        struct Page {
            std::vector<uint64_t> sequences, repeats;
            std::vector<std::string> texts;
            uint64_t next = 0, offset = 0, revision = 0;
        };
        uint64_t first = 0, offset = 0, lastSequence = 0;
        std::vector<std::string> texts;
        std::vector<uint64_t> repeats;
        do {
            Page page;
            const EditorValue cursor[] = {{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, {}, {}, first},
                {EDITOR_TEXT("offset"), EDITOR_INTEGER_VALUE, {}, {}, offset},
                {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, revision}};
            const EditorRequest read{request.scope, cursor, 3};
            const EditorSink entries{&page, [](void *data, const EditorValue *fields, uint32_t count) {
                auto &page = *static_cast<Page *>(data);
                for (uint32_t i = 0; i < count; ++i) {
                    const auto &field = fields[i];
                    const std::string key(field.key.data, field.key.size);
                    if (key == "texts")
                        for (uint32_t j = 0; j < field.texts.count; ++j)
                            page.texts.emplace_back(field.texts.data[j].data, field.texts.data[j].size);
                    else if ((key == "repeats" || key == "sequences") && field.integers.count) {
                        auto &values = key == "repeats" ? page.repeats : page.sequences;
                        values.assign(field.integers.data, field.integers.data + field.integers.count);
                    } else if (key == "revision") page.revision = field.integer;
                    else if (key == "next") page.next = field.integer;
                    else if (key == "offset") page.offset = field.integer;
                }
                return page.texts.size() == page.repeats.size() && page.texts.size() == page.sequences.size() ?
                    EDITOR_OK : EDITOR_INVALID;
            }};
            if (host->query(host->data, EditorString EDITOR_TEXT("log.entries"), &read, entries) != EDITOR_OK ||
                page.revision != revision) return;
            if (page.texts.empty()) break;
            for (size_t i = 0; i < page.texts.size(); ++i) {
                if (!texts.empty() && page.sequences[i] == lastSequence) texts.back() += page.texts[i];
                else { texts.push_back(std::move(page.texts[i])); repeats.push_back(page.repeats[i]); }
                lastSequence = page.sequences[i];
            }
            if (page.next < first || (page.next == first && page.offset <= offset)) return;
            first = page.next;
            offset = page.offset;
        } while (offset || first < count);
        m_Texts = std::move(texts);
        m_Repeats = std::move(repeats);
        m_Revision = revision;
    }
    bool m_LogPlaced = false;
    size_t m_LastLogSize = 0;
    uint64_t m_Revision = std::numeric_limits<uint64_t>::max();
    std::vector<std::string> m_Texts;
    std::vector<uint64_t> m_Repeats;
};
void EventLogView::Draw(const EditorHost *host, EditorView id, EditorViewFrame &frame) {
    Read(host, id);
    const float s = ImGui::GetFontSize() / 13.0f;
    const float title = vi::TitleHeight(s);
    if (!m_LogPlaced) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y),
                                ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(354.0f * 1.5f * s + 2.0f, 286.0f * 1.625f * s + title + 2.0f), ImGuiCond_Always);
        // The status bar takes its room from the work area a frame after it shows.
        m_LogPlaced = ImGui::GetFrameCount() > 2;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f * s, title + 80.0f * s), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CurrentDevTheme().dialogRounding * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, vi::Color(4));
    ImGui::PushStyleColor(ImGuiCol_Border, vi::Color(0));
    EditorImGuiWindow window(frame,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!window) return;
    // The dark title bar, as Dev's dialogs; moved by it.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    dl->AddRectFilled(ImVec2(at.x + 1.0f, at.y + 1.0f), ImVec2(at.x + size.x - 1.0f, at.y + 1.0f + title), vi::Color(2),
                      CurrentDevTheme().dialogRounding * s, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(at.x + 1.0f, at.y + 1.0f + title), ImVec2(at.x + size.x - 1.0f, at.y + 1.0f + title),
                CurrentDevTheme().controlOutline);
    vi::Text(ImVec2(at.x + 9.0f * s, at.y + 1.0f + (title - ImGui::GetFontSize()) * 0.5f),
             ImVec2(at.x + size.x - 6.0f * s, at.y + title), "Event Log", true, vi::LEFT, vi::Color(9));
    ImGui::SetCursorScreenPos(at);
    ImGui::InvisibleButton("##caption", ImVec2(size.x, title + 1.0f));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(at.x + delta.x, at.y + delta.y));
    }
    // The text (a read-only VIEdit, one line per entry), the buttons below.
    const ImVec2 client(at.x + 1.0f, at.y + 2.0f + title);
    const ImVec2 clientMax(at.x + size.x - 1.0f, at.y + size.y - 1.0f);
    const float buttons = clientMax.y - 25.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(client.x + 1.0f, client.y + 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vi::Color(7));
    ImGui::PushStyleColor(ImGuiCol_Text, vi::Color(0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(CurrentDevTheme().eventLogPadding.x * s, CurrentDevTheme().eventLogPadding.y * s));
    ImGui::BeginChild("lines", ImVec2(clientMax.x - client.x - 2.0f, buttons - client.y - 3.0f * s),
                      ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_Texts.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            if (m_Repeats[i] > 1) ImGui::Text("%s  (x%llu)", m_Texts[i].c_str(), static_cast<unsigned long long>(m_Repeats[i]));
            else ImGui::TextUnformatted(m_Texts[i].c_str());
        }
    }
    if (m_LastLogSize != m_Texts.size()) ImGui::SetScrollHereY(1.0f);
    m_LastLogSize = m_Texts.size();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const float by = clientMax.y - 22.0f * s;
    if (vi::DialogButton("Close", ImVec2(clientMax.x - 70.0f * s, by), s, focused ? ImGuiKey_Escape : ImGuiKey_None))
        frame.close = 1;
    if (vi::DialogButton("Clear", ImVec2(clientMax.x - 140.0f * s, by), s)) {
        const EditorRequest request{host->scope(host->data, id), nullptr, 0};
        host->execute(host->data, EditorString EDITOR_TEXT("log.clear"), &request, {});
    }
}
} // namespace
bool RegisterEventLogView(EditorApplication &editor, std::string &error) {
    const EditorViewDesc view{sizeof(EditorViewDesc), EDITOR_IMGUI_VERSION,
        EDITOR_TEXT("application.event-log"), EDITOR_TEXT("builtin.log"), EDITOR_TEXT("Event Log"),
        [](void *, const EditorHost *, EditorView) -> void * { return new EventLogView; },
        [](void *data) { delete static_cast<EventLogView *>(data); },
        [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
            static_cast<EventLogView *>(data)->Draw(host, id, *frame);
        }, nullptr, EDITOR_WINDOW_VIEW, 1, EDITOR_WORKSPACE};
    const EditorString dependency = EDITOR_TEXT("application.log");
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("log.view");
    module.dependencies = &dependency;
    module.dependencyCount = 1;
    module.views = &view;
    module.viewCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev

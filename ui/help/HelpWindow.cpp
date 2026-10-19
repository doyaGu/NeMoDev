#include "ui/help/HelpWindow.h"
#include "core/runtime/Resources.h"
#include "ui/app/DesktopFiles.h"
#include "misc/cpp/imgui_stdlib.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace vdev {
void HelpWindow::Open(const std::string &topic) {
    m_Open = m_Focus = true;
    if (!m_Loaded) m_Loaded = m_Book.Load(DataFilePath("help"), m_Error);
    if (m_Loaded) m_Pending = topic.empty() ? m_Book.Home() : topic;
}
void HelpWindow::Close() {
    m_Open = false;
    m_Images.clear();
}
void HelpWindow::Navigate(const std::string &topic) {
    if (m_Current == topic) return;
    if (!m_History.empty()) m_History[m_Visit].scroll = m_Scroll;
    m_History.resize(m_History.empty() ? 0 : m_Visit + 1);
    m_History.push_back({topic, 0.0f});
    if (m_History.size() > 128) m_History.erase(m_History.begin());
    m_Visit = m_History.size() - 1;
    m_Current = topic;
    if (const auto *page = m_Book.Find(topic)) m_Visited.insert(page->path);
    const size_t fragment = topic.find('#');
    m_Anchor = fragment == std::string::npos ? std::string() : topic.substr(fragment + 1);
    m_RestoreScroll = 0.0f;
    m_Error = m_Book.Find(topic) ? std::string() : "This topic is missing from the source help archive: " + topic;
    m_Images.clear();
    m_Notice.clear();
}
void HelpWindow::History(int direction) {
    m_History[m_Visit].scroll = m_Scroll;
    m_Visit = static_cast<size_t>(static_cast<int>(m_Visit) + direction);
    m_Current = m_History[m_Visit].topic;
    m_RestoreScroll = m_History[m_Visit].scroll;
    m_Anchor.clear();
    m_Error = m_Book.Find(m_Current) ? std::string() : "This topic is missing from the source help archive: " + m_Current;
    // Retire images at the beginning of the next frame, after submission.
    m_RetireImages = true;
    m_Notice.clear();
}
void HelpWindow::Contents(const std::vector<HelpContents> &items) {
    for (const auto &item : items) {
        ImGui::PushID(&item);
        if (item.children.empty()) {
            if (ImGui::Selectable(item.title.c_str(), !item.topic.empty() && m_Book.Find(item.topic) == m_Book.Find(m_Current)) && !item.topic.empty())
                m_Pending = item.topic;
        } else {
            const bool open = ImGui::TreeNodeEx(item.title.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && !item.topic.empty()) m_Pending = item.topic;
            if (open) { Contents(item.children); ImGui::TreePop(); }
        }
        ImGui::PopID();
    }
}
UiTexture *HelpWindow::Image(const std::string &path) {
    const auto found = m_Images.find(path);
    if (found != m_Images.end()) return found->second.get();
    std::vector<unsigned char> pixels;
    int width = 0, height = 0;
    std::unique_ptr<UiTexture> image;
    const std::string file = m_Book.ImagePath(path);
    if (!file.empty() && ReadImageRgba(file, pixels, width, height)) image = MainRenderer().CreateTexture(width, height, pixels.data());
    return m_Images.emplace(path, std::move(image)).first->second.get();
}

void HelpWindow::FollowLink(const std::string &link) {
    if (link.compare(0, 7, "http://") == 0 || link.compare(0, 8, "https://") == 0) {
        if (!SDL_OpenURL(link.c_str())) m_Notice = SDL_GetError();
        else m_Visited.insert(link);
    } else if (link.find(':') != std::string::npos) m_Notice = "This legacy scripted link cannot run in the help viewer.";
    else m_Pending = link;
}

void HelpWindow::DrawPage(const HelpTopic &topic) {
    const float width = ImGui::GetContentRegionAvail().x;
    const float scale = ImGui::GetFontSize() / 14.0f * m_Zoom;
    if (m_LayoutTopic != &topic || m_LayoutWidth != width || m_LayoutScale != scale) {
        m_Page = HelpLayout(m_Fonts, scale).Build(topic, width);
        m_LayoutTopic = &topic; m_LayoutWidth = width; m_LayoutScale = scale;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float localY = ImGui::GetCursorPosY();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < m_Page.drawings.size(); ++i) {
        const auto &item = m_Page.drawings[i];
        const ImVec2 at(origin.x + item.position.x, origin.y + item.position.y);
        const ImVec2 end(at.x + item.size.x, at.y + item.size.y);
        unsigned color = item.color, flags = item.flags;
        if (!item.link.empty() && item.size.x > 0 && item.size.y > 0) {
            ImGui::SetCursorScreenPos(at); ImGui::PushID(static_cast<int>(i));
            if (ImGui::InvisibleButton((item.text + "##help-link").c_str(), item.size, ImGuiButtonFlags_EnableNav)) FollowLink(item.link);
            const auto *target = m_Book.Find(item.link);
            if (m_Visited.count(target ? target->path : item.link)) { color = item.visitedColor; flags = item.visitedFlags; }
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", item.link.c_str());
                color = item.hoverColor; flags = item.hoverFlags;
            }
            ImGui::PopID();
        }
        if (item.kind == HelpDrawing::Fill) draw->AddRectFilled(at, end, item.color);
        else if (item.kind == HelpDrawing::Line) draw->AddLine(at, end, item.color, item.thickness);
        else if (item.kind == HelpDrawing::Image) {
            if (UiTexture *texture = Image(item.image)) {
                draw->AddImage(texture->Id(), at, end);
#ifdef VIRTOOLSDEV_UI_TESTS
                ++m_ShownImages;
#endif
            } else draw->AddText(at, 0xff0000a0, ("[Missing image: " + item.image + "]").c_str());
        } else {
            if (item.background) draw->AddRectFilled(at, end, item.background);
            const int first = draw->VtxBuffer.Size;
            draw->AddText(item.font, item.fontSize, at, color, item.text.c_str());
            if ((flags & HelpAppearance::Bold) && (flags & HelpAppearance::Monospace))
                draw->AddText(item.font, item.fontSize, ImVec2(at.x + 0.5f, at.y), color, item.text.c_str());
            if (flags & HelpAppearance::Italic)
                for (int vertex = first; vertex < draw->VtxBuffer.Size; ++vertex)
                    draw->VtxBuffer[vertex].pos.x += (at.y + item.fontSize - draw->VtxBuffer[vertex].pos.y) * 0.15f;
            if (flags & HelpAppearance::Underline) draw->AddLine(ImVec2(at.x, at.y + item.fontSize), ImVec2(end.x, at.y + item.fontSize), color);
#ifdef VIRTOOLSDEV_UI_TESTS
            m_ShownText += item.text; m_ShownText += ' ';
#endif
        }
    }
    for (const auto &anchor : m_Page.anchors) m_Anchors[anchor.first] = localY + anchor.second;
    ImGui::SetCursorScreenPos(origin); ImGui::Dummy(m_Page.size);
}
void HelpWindow::Draw() {
    if (m_RetireImages) { m_Images.clear(); m_RetireImages = false; }
    if (!m_Open) { m_Images.clear(); return; }
    if (!m_Pending.empty()) { Navigate(m_Pending); m_Pending.clear(); }
    ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);
    if (m_Focus) { ImGui::SetNextWindowFocus(); m_Focus = false; }
    if (!ImGui::Begin("Help", &m_Open)) { ImGui::End(); return; }
    ImGui::BeginDisabled(m_History.empty() || m_Visit == 0);
    if (ImGui::Button("Back")) History(-1);
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(m_History.empty() || m_Visit + 1 == m_History.size());
    if (ImGui::Button("Forward")) History(1);
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Home") && m_Loaded) m_Pending = m_Book.Home();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("Zoom", &m_Zoom, 0.75f, 2.0f, "%.2fx");
    ImGui::TextWrapped("Original Virtools reference. Some features and scripted examples are not available in NemoDev.");
    if (ImGui::BeginTable("help", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Navigation", ImGuiTableColumnFlags_WidthFixed, 270.0f);
        ImGui::TableSetupColumn("Topic", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##search", "Search all help topics", &m_Query)) m_Results = m_Book.Search(m_Query);
        ImGui::BeginChild("navigation", ImVec2(0, 0));
        if (m_Query.empty()) Contents(m_Book.Contents());
        else {
            ImGui::Text("%zu topic(s)", m_Results.size());
            for (const auto *topic : m_Results) {
                ImGui::PushID(topic);
                if (ImGui::Selectable(topic->title.c_str(), topic == m_Book.Find(m_Current))) m_Pending = topic->path;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", topic->path.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        const HelpTopic *topic = m_Book.Find(m_Current);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, topic ? topic->appearance.background : 0xffffffffu);
        ImGui::BeginChild("article", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        m_Anchors.clear();
#ifdef VIRTOOLSDEV_UI_TESTS
        m_ShownText.clear(); m_ShownImages = 0;
#endif
        if (!m_Error.empty()) ImGui::TextWrapped("%s", m_Error.c_str());
        if (!m_Notice.empty()) ImGui::TextWrapped("%s", m_Notice.c_str());
        if (topic) {
            if (ImGui::SmallButton("Copy text")) SDL_SetClipboardText(topic->text.c_str());
            ImGui::TextDisabled("%s", topic->path.c_str());
            ImGui::Separator();
            DrawPage(*topic);
        }
        if (!m_Anchor.empty()) {
            const auto anchor = m_Anchors.find(m_Anchor);
            if (anchor != m_Anchors.end()) m_RestoreScroll = anchor->second;
            else m_Notice = "The source topic does not contain anchor: " + m_Anchor;
            m_Anchor.clear();
        }
        if (m_RestoreScroll >= 0.0f) { ImGui::SetScrollY(m_RestoreScroll); m_RestoreScroll = -1.0f; }
        m_Scroll = ImGui::GetScrollY();
        ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::EndTable();
    }
    ImGui::End();
}
}

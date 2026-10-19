#include "ui/help/HelpWindow.h"
#include "core/runtime/Resources.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace vdev {
void HelpWindow::Open(const std::string &topic) {
    m_Open = m_Focus = true;
    if (!m_Loaded)
        m_Loaded = m_Book.Load(DataFilePath("help"), m_Error);
    if (m_Loaded)
        m_Pending = topic.empty() ? m_Book.Home() : topic;
}
void HelpWindow::Close() {
    if (m_Document && !m_History.empty())
        m_History[m_Visit].position = ReadingPosition();
    m_Open = false;
    m_Document.reset();
    m_LayoutTopic = nullptr;
}
ImVec2 HelpWindow::ScrollPosition() const {
    const ImGuiWindow *article = ImGui::FindWindowByID(m_ArticleId);
    return article ? article->Scroll : ImVec2();
}
HtmlReadingPosition HelpWindow::ReadingPosition() const {
    const ImVec2 scroll = ScrollPosition();
    if (m_LayoutTopic && m_LayoutTopic == m_Book.Find(m_Current))
        return m_Document->CapturePosition({scroll.x, scroll.y - m_PageY});
    return {};
}
void HelpWindow::Navigate(const std::string &topic) {
    const bool samePage = m_Book.Find(m_Current) == m_Book.Find(topic);
    if (m_Current != topic) {
        if (!m_History.empty() && m_Document)
            m_History[m_Visit].position = ReadingPosition();
        m_History.resize(m_History.empty() ? 0 : m_Visit + 1);
        m_History.push_back({topic, {}});
        if (m_History.size() > 128)
            m_History.erase(m_History.begin());
        m_Visit = m_History.size() - 1;
    }
    m_Current = topic;
    if (m_Document)
        m_Document->CancelPointer();
    const size_t fragment = topic.find('#');
    m_Anchor = fragment == std::string::npos ? std::string() : topic.substr(fragment + 1);
    m_RestoreScroll = samePage && !m_Anchor.empty() ? ScrollPosition() : ImVec2();
    m_RestorePosition.reset();
    m_Error = m_Book.Find(topic) ? std::string() : "This topic is missing from the source help archive: " + topic;
    m_Notice.clear();
}
void HelpWindow::History(int direction) {
    m_History[m_Visit].position = ReadingPosition();
    m_Visit = static_cast<size_t>(static_cast<int>(m_Visit) + direction);
    m_Current = m_History[m_Visit].topic;
    m_RestorePosition = m_History[m_Visit].position;
    m_RestoreScroll = ImVec2(-1, -1);
    m_Anchor.clear();
    m_Error =
        m_Book.Find(m_Current) ? std::string() : "This topic is missing from the source help archive: " + m_Current;
    if (m_Document)
        m_Document->CancelPointer();
    m_Notice.clear();
}
void HelpWindow::Contents(const std::vector<HelpContents> &items) {
    for (const auto &item : items) {
        ImGui::PushID(&item);
        if (item.children.empty()) {
            if (ImGui::Selectable(item.title.c_str(),
                                  !item.topic.empty() && m_Book.Find(item.topic) == m_Book.Find(m_Current)) &&
                !item.topic.empty())
                m_Pending = item.topic;
        } else {
            const bool open = ImGui::TreeNodeEx(item.title.c_str(),
                                                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && !item.topic.empty())
                m_Pending = item.topic;
            if (open) {
                Contents(item.children);
                ImGui::TreePop();
            }
        }
        ImGui::PopID();
    }
}
void HelpWindow::FollowLink(const HtmlNavigation &navigation) {
    const auto &url = navigation.url;
    if (url.compare(0, 12, "book://help/") == 0) {
        if (const auto *topic = m_Book.Find(url)) {
            m_Pending = topic->path;
            const size_t fragment = url.find('#');
            if (fragment != std::string::npos)
                m_Pending += url.substr(fragment);
            m_Visited.insert(url);
        } else
            m_Notice = "This topic is missing from the source help archive: " + url;
    } else if (url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0) {
        if (!SDL_OpenURL(url.c_str()))
            m_Notice = SDL_GetError();
        else
            m_Visited.insert(url);
    } else
        m_Notice = "This link requires an external application: " + url;
    if (m_Document)
        m_Document->SetVisited({m_Visited.begin(), m_Visited.end()});
}
void HelpWindow::DrawPage(const HelpTopic &topic) {
    const ImGuiWindow *window = ImGui::GetCurrentWindow();
    const float width = std::max(1.0f, window->InnerRect.GetWidth() - 2 * window->WindowPadding.x);
    const float scale = ImGui::GetFontSize() / 14.0f * m_Zoom;
    const float height = std::max(1.0f, window->InnerRect.GetHeight());
    if (m_LayoutTopic != &topic || m_LayoutWidth != width || m_LayoutHeight != height || m_LayoutScale != scale) {
        if (m_LayoutTopic == &topic && !m_RestorePosition && m_RestoreScroll.y < 0 && m_Anchor.empty())
            m_RestorePosition = ReadingPosition();
        if (m_LayoutTopic != &topic) {
            auto document = std::make_unique<HtmlDocument>(m_Book, m_Renderer);
            if (!document->Load(topic.html, HelpBook::Url(topic.path), m_Error) ||
                !document->Layout({width, height}, scale, m_Error)) {
                ImGui::TextWrapped("%s", m_Error.c_str());
                return;
            }
            document->SetVisited({m_Visited.begin(), m_Visited.end()});
            m_Document = std::move(document);
            m_LayoutTopic = &topic;
        } else if (!m_Document->Layout({width, height}, scale, m_Error)) {
            ImGui::TextWrapped("%s", m_Error.c_str());
            return;
        }
        m_LayoutWidth = width;
        m_LayoutHeight = height;
        m_LayoutScale = scale;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float localY = ImGui::GetCursorPosY();
    m_PageY = localY;
    const auto size = m_Document->ContentSize();
    const ImVec2 mouse = ImGui::GetMousePos();
    const HtmlPoint point{mouse.x - origin.x, mouse.y - origin.y};
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         window->InnerRect.Contains(mouse) && point.y >= 0;
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        m_Document->CancelPointer();
    if (hovered) {
        m_Document->Hover(point);
        const auto hit = m_Document->HitTest(point);
        if (hit.link)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (!hit.title.empty())
            ImGui::SetTooltip("%s", hit.title.c_str());
        else if (hit.link)
            ImGui::SetTooltip("%s", hit.navigation.url.c_str());
        if (ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left])
            m_Document->PointerDown(point);
    } else
        m_Document->Leave();
    if (ImGui::GetIO().MouseReleased[ImGuiMouseButton_Left]) {
        HtmlNavigation navigation;
        if (m_Document->PointerUp(hovered ? point : HtmlPoint{-1, -1}, navigation))
            FollowLink(navigation);
    }
    m_Renderer.Begin(ImGui::GetWindowDrawList());
    try {
        const float left = std::max(0.0f, window->InnerRect.Min.x - origin.x);
        const float top = std::max(0.0f, window->InnerRect.Min.y - origin.y);
        m_Document->Draw({origin.x, origin.y}, {left, top, std::max(0.0f, window->InnerRect.Max.x - origin.x - left),
                                                std::max(0.0f, window->InnerRect.Max.y - origin.y - top)});
    } catch (const std::exception &error) {
        m_Notice = error.what();
    }
    // ImGui owns keyboard focus; HTML owns link identity and activation. Pointer
    // activation uses actual HTML hit regions, including overlapping image maps.
    const auto links = m_Document->Links();
    for (const auto &link : links) {
        if (link.tabIndex < 0)
            continue;
        ImGui::PushID(static_cast<int>(link.id >> 32));
        ImGui::PushID(static_cast<int>(link.id));
        for (size_t i = 0; i < link.bounds.size(); ++i) {
            const auto &box = link.bounds[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetCursorScreenPos(ImVec2(origin.x + box.x, origin.y + box.y));
            const std::string label = (link.title.empty() ? link.label : link.title) + "##help-link";
            if (ImGui::InvisibleButton(label.c_str(), ImVec2(box.width, box.height), ImGuiButtonFlags_EnableNav) &&
                !ImGui::GetIO().MouseReleased[ImGuiMouseButton_Left]) {
                HtmlNavigation navigation;
                if (m_Document->Activate(link.id, navigation))
                    FollowLink(navigation);
            }
            if (ImGui::IsItemFocused())
                ImGui::GetWindowDrawList()->AddRect(ImVec2(origin.x + box.x, origin.y + box.y),
                                                    ImVec2(origin.x + box.x + box.width, origin.y + box.y + box.height),
                                                    0xff0033ff);
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::PopID();
    }
    if (m_RestorePosition) {
        HtmlPoint position;
        if (m_Document->RestorePosition(*m_RestorePosition, position))
            m_RestoreScroll = ImVec2(std::max(0.0f, position.x), std::max(0.0f, localY + position.y));
        m_RestorePosition.reset();
    }
    if (!m_Anchor.empty()) {
        HtmlPoint position;
        if (m_Document->FindAnchor(m_Anchor, position))
            m_RestoreScroll = ImVec2(position.x, localY + position.y);
        else
            m_Notice = "The source topic does not contain anchor: " + m_Anchor;
        m_Anchor.clear();
    }
#ifdef VIRTOOLSDEV_UI_TESTS
    // Inspect all actual paint operations without submitting offscreen UI geometry.
    m_Renderer.Begin(nullptr);
    m_Document->Draw({}, {0, 0, size.width, size.height});
    m_PagePaint.drawings = m_Renderer.Drawings();
    m_PagePaint.size = ImVec2(size.width, size.height);
#endif
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(size.width, size.height));
}
#ifdef VIRTOOLSDEV_UI_TESTS
size_t HelpWindow::ShownImages() const {
    return std::count_if(m_PagePaint.drawings.begin(), m_PagePaint.drawings.end(),
                         [](const auto &drawing) { return drawing.kind == HtmlRenderer::Drawing::Image; });
}
#endif
void HelpWindow::Draw() {
    if (!m_Open) {
        Close();
        return;
    }
    if (!m_Pending.empty()) {
        Navigate(m_Pending);
        m_Pending.clear();
    }
    ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);
    if (m_Focus) {
        ImGui::SetNextWindowFocus();
        m_Focus = false;
    }
    if (!ImGui::Begin("Help", &m_Open)) {
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(m_History.empty() || m_Visit == 0);
    if (ImGui::Button("Back"))
        History(-1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_History.empty() || m_Visit + 1 == m_History.size());
    if (ImGui::Button("Forward"))
        History(1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Home") && m_Loaded)
        m_Pending = m_Book.Home();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("Zoom", &m_Zoom, 0.75f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped(
        "Original Virtools reference. Some features and scripted examples are not available in NemoDev.");
    if (ImGui::BeginTable("help", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Navigation", ImGuiTableColumnFlags_WidthFixed, 270.0f);
        ImGui::TableSetupColumn("Topic", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_F, false))
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##search", "Search all help topics", &m_Query))
            m_Results = m_Book.Search(m_Query);
        ImGui::BeginChild("navigation", ImVec2(0, 0));
        if (m_Query.empty())
            Contents(m_Book.Contents());
        else {
            ImGui::Text("%zu topic(s)", m_Results.size());
            for (const auto *topic : m_Results) {
                ImGui::PushID(topic);
                if (ImGui::Selectable(topic->title.c_str(), topic == m_Book.Find(m_Current)))
                    m_Pending = topic->path;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", topic->path.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        const HelpTopic *topic = m_Book.Find(m_Current);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, 0xffffffffu);
        ImGui::BeginChild("article", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        m_ArticleId = ImGui::GetCurrentWindow()->ID;
        if (!m_Error.empty())
            ImGui::TextWrapped("%s", m_Error.c_str());
        if (!m_Notice.empty())
            ImGui::TextWrapped("%s", m_Notice.c_str());
        if (topic) {
            if (ImGui::SmallButton("Copy text"))
                SDL_SetClipboardText(topic->text.c_str());
            ImGui::TextDisabled("%s", topic->path.c_str());
            ImGui::Separator();
            DrawPage(*topic);
        }
        if (m_RestoreScroll.y >= 0.0f) {
            ImGui::SetScrollX(m_RestoreScroll.x);
            ImGui::SetScrollY(m_RestoreScroll.y);
            m_RestoreScroll = ImVec2(-1, -1);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::EndTable();
    }
    ImGui::End();
}
} // namespace vdev

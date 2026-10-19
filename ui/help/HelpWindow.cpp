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
    } else if (link.find(':') != std::string::npos) m_Notice = "This legacy scripted link cannot run in the help viewer.";
    else m_Pending = link;
}

void HelpWindow::Paragraph(const HelpBlock &block) {
    const float size = ImGui::GetFontSize();
    const float line = size + 4.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float localY = ImGui::GetCursorPosY();
    const float width = std::max(20.0f, m_LineWidth > 0.0f ? m_LineWidth : ImGui::GetContentRegionAvail().x);
    float x = 0.0f, y = 0.0f, height = line;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    auto newline = [&] { x = 0.0f; y += height; height = line; };
    for (const auto &span : block.spans) {
        if (!span.anchor.empty()) m_Anchors[span.anchor] = localY + y;
        if (!span.image.empty()) {
            UiTexture *texture = Image(span.image);
            if (texture) {
                const float scale = std::min(m_Zoom, width / texture->Width());
                const float w = texture->Width() * scale, h = texture->Height() * scale;
                if (x && x + w > width) newline();
                draw->AddImage(texture->Id(), ImVec2(origin.x + x, origin.y + y), ImVec2(origin.x + x + w, origin.y + y + h));
                if (!span.link.empty()) {
                    ImGui::SetCursorScreenPos(ImVec2(origin.x + x, origin.y + y));
                    ImGui::PushID(m_Item++);
                    if (ImGui::InvisibleButton("image link", ImVec2(w, h), ImGuiButtonFlags_EnableNav)) FollowLink(span.link);
                    if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", span.link.c_str()); }
                    ImGui::PopID();
                }
                x += w + 3.0f; height = std::max(height, h + 3.0f);
#ifdef VIRTOOLSDEV_UI_TESTS
                ++m_ShownImages;
#endif
            } else {
                const std::string label = "[Missing image: " + span.image + "]";
                if (x) newline();
                draw->AddText(ImVec2(origin.x, origin.y + y), ImGui::GetColorU32(ImGuiCol_TextDisabled), label.c_str());
                newline();
            }
            continue;
        }
#ifdef VIRTOOLSDEV_UI_TESTS
        m_ShownText += span.text;
#endif
        ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        const bool light = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f;
        if (span.style & (8 | 32)) color = light ? IM_COL32(20, 80, 135, 255) : IM_COL32(110, 190, 245, 255);
        if (span.style & (16 | 64)) color = light ? IM_COL32(140, 75, 15, 255) : IM_COL32(235, 170, 95, 255);
        if (!span.link.empty()) color = light ? IM_COL32(25, 75, 145, 255) : ImGui::GetColorU32(ImGuiCol_TextLink);
        const char *at = span.text.data(), *end = at + span.text.size();
        while (at < end) {
            if (*at == '\n') { newline(); ++at; continue; }
            const char *next = at;
            if (*next == ' ' || *next == '\t') ++next;
            else while (next < end && *next != ' ' && *next != '\t' && *next != '\n') ++next;
            float w = ImGui::CalcTextSize(at, next).x;
            if (block.kind != HelpBlock::Preformatted && x && x + w > width) newline();
            if (*at == ' ' && x == 0.0f && block.kind != HelpBlock::Preformatted) { at = next; continue; }
            // Split long identifiers on UTF-8 boundaries when no word fits.
            if (block.kind != HelpBlock::Preformatted && w > width) {
                next = at;
                while (next < end && *next != ' ' && *next != '\n') {
                    const char *character = next + 1;
                    while (character < end && (static_cast<unsigned char>(*character) & 0xc0) == 0x80) ++character;
                    if (next > at && ImGui::CalcTextSize(at, character).x > width) break;
                    next = character;
                }
                w = ImGui::CalcTextSize(at, next).x;
            }
            const ImVec2 pos(origin.x + x, origin.y + y);
            if (span.style & 4) draw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + line), ImGui::GetColorU32(ImGuiCol_FrameBg));
            const int firstVertex = draw->VtxBuffer.Size;
            draw->AddText(pos, color, at, next);
            if (span.style & 1) draw->AddText(ImVec2(pos.x + 0.5f, pos.y), color, at, next);
            if (span.style & 2)
                for (int i = firstVertex; i < draw->VtxBuffer.Size; ++i)
                    draw->VtxBuffer[i].pos.x += (pos.y + size - draw->VtxBuffer[i].pos.y) * 0.15f;
            if (!span.link.empty() && w > 0.0f) {
                ImGui::SetCursorScreenPos(pos);
                ImGui::PushID(m_Item++);
                if (ImGui::InvisibleButton((span.text + "##help-link").c_str(), ImVec2(w, line), ImGuiButtonFlags_EnableNav)) FollowLink(span.link);
                if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", span.link.c_str()); }
                draw->AddLine(ImVec2(pos.x, pos.y + size), ImVec2(pos.x + w, pos.y + size), color);
                ImGui::PopID();
            }
            x += w; at = next;
        }
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(block.kind == HelpBlock::Preformatted ? std::max(width, x) : width, y + height));
}
void HelpWindow::Blocks(const std::vector<HelpBlock> &blocks) {
    for (const auto &block : blocks) {
        ImGui::PushID(&block);
        if (block.kind == HelpBlock::Rule) ImGui::Separator();
        else if (block.kind == HelpBlock::Table) {
            const int columns = static_cast<int>(std::min(32u, std::max(1u, block.value)));
            const float previousWidth = m_LineWidth;
            const float width = previousWidth > 0.0f ? previousWidth : ImGui::GetContentRegionAvail().x;
            for (const auto &row : block.children) {
                const ImVec2 start = ImGui::GetCursorScreenPos();
                float height = 1.0f;
                int column = 0;
                for (const auto &cell : row.children) {
                    const int span = std::min(columns - column, static_cast<int>(std::max(1u, cell.value)));
                    if (span <= 0) break;
                    m_LineWidth = std::max(20.0f, width * span / columns - 12.0f);
                    ImGui::SetCursorScreenPos(ImVec2(start.x + width * column / columns + 6, start.y + 6));
                    ImGui::BeginGroup(); Blocks(cell.children); ImGui::EndGroup();
                    height = std::max(height, ImGui::GetItemRectSize().y + 12.0f);
                    column += span;
                }
                column = 0;
                for (const auto &cell : row.children) {
                    const int span = std::min(columns - column, static_cast<int>(std::max(1u, cell.value)));
                    if (span <= 0) break;
                    ImGui::GetWindowDrawList()->AddRect(ImVec2(start.x + width * column / columns, start.y),
                        ImVec2(start.x + width * (column + span) / columns, start.y + height), ImGui::GetColorU32(ImGuiCol_Border));
                    column += span;
                }
                ImGui::SetCursorScreenPos(start); ImGui::Dummy(ImVec2(width, height));
            }
            m_LineWidth = previousWidth;
        } else if (block.kind == HelpBlock::ListItem) {
            if (block.value) ImGui::Text("%u.", block.value); else ImGui::Bullet();
            ImGui::SameLine();
            ImGui::BeginGroup(); Blocks(block.children); ImGui::EndGroup();
        } else {
            const float font = ImGui::GetFontSize();
            if (block.kind == HelpBlock::Heading) ImGui::PushFont(nullptr, font * (block.value <= 2 ? 1.35f : 1.15f));
            Paragraph(block);
            if (block.kind == HelpBlock::Heading) ImGui::PopFont();
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
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
        ImGui::BeginChild("article", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * m_Zoom);
        m_Item = 0; m_Anchors.clear();
#ifdef VIRTOOLSDEV_UI_TESTS
        m_ShownText.clear(); m_ShownImages = 0;
#endif
        if (!m_Error.empty()) ImGui::TextWrapped("%s", m_Error.c_str());
        if (!m_Notice.empty()) ImGui::TextWrapped("%s", m_Notice.c_str());
        if (const HelpTopic *topic = m_Book.Find(m_Current)) {
            if (ImGui::SmallButton("Copy text")) SDL_SetClipboardText(topic->text.c_str());
            ImGui::TextWrapped("%s", topic->title.c_str());
            ImGui::TextDisabled("%s", topic->path.c_str());
            ImGui::Separator();
            Blocks(topic->blocks);
        }
        if (!m_Anchor.empty()) {
            const auto anchor = m_Anchors.find(m_Anchor);
            if (anchor != m_Anchors.end()) m_RestoreScroll = anchor->second;
            else m_Notice = "The source topic does not contain anchor: " + m_Anchor;
            m_Anchor.clear();
        }
        if (m_RestoreScroll >= 0.0f) { ImGui::SetScrollY(m_RestoreScroll); m_RestoreScroll = -1.0f; }
        m_Scroll = ImGui::GetScrollY();
        ImGui::PopFont(); ImGui::EndChild(); ImGui::EndTable();
    }
    ImGui::End();
}
}

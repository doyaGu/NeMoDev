#include "ui/help/HelpWindow.h"
#include "application/EditorApplication.h"
#include "editor/ImGuiDrawing.h"
#include "core/runtime/Resources.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#ifdef VIRTOOLSDEV_UI_TESTS
#include <map>
#endif

namespace vdev {
void HelpWindow::Open(const std::string &topic) {
    if (!m_Loaded)
        m_Loaded = m_Book.Load(DataFilePath("help"), m_Error);
    if (m_Loaded)
        m_Pending = topic.empty() ? m_Book.Home() : topic;
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
}
bool HelpWindow::DrawPage(const HelpTopic &topic) {
    const ImGuiWindow *window = ImGui::GetCurrentWindow();
    const float width = std::max(1.0f, window->InnerRect.GetWidth() - 2 * window->WindowPadding.x);
    const float scale = ImGui::GetFontSize() / 14.0f * m_Zoom;
    const float height = std::max(1.0f, window->InnerRect.GetHeight());
    if (m_LayoutTopic != &topic || m_LayoutWidth != width || m_LayoutHeight != height || m_LayoutScale != scale) {
        if (m_LayoutTopic == &topic && !m_RestorePosition && m_RestoreScroll.y < 0 && m_Anchor.empty())
            m_RestorePosition = ReadingPosition();
    }
    if (m_LayoutTopic != &topic) {
        auto document = std::make_unique<HtmlDocument>(m_Book, m_Renderer);
        if (!document->Load(topic.html, HelpBook::Url(topic.path), m_Error)) {
            ImGui::TextWrapped("%s", m_Error.c_str());
            return false;
        }
        document->SetVisited({m_Visited.begin(), m_Visited.end()});
        if (!document->Layout({width, height}, scale, m_Error)) {
            ImGui::TextWrapped("%s", m_Error.c_str());
            return false;
        }
        m_Document = std::move(document);
        m_LayoutTopic = &topic;
    } else if (!m_Document->Layout({width, height}, scale, m_Error)) {
        // An interrupted interaction may require rebuilding even at the same size.
        ImGui::TextWrapped("%s", m_Error.c_str());
        return false;
    }
    m_LayoutWidth = width;
    m_LayoutHeight = height;
    m_LayoutScale = scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float localY = ImGui::GetCursorPosY();
    m_PageY = localY;
    const ImVec2 mouse = ImGui::GetMousePos();
    const HtmlPoint point{mouse.x - origin.x, mouse.y - origin.y};
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         window->InnerRect.Contains(mouse) && point.y >= 0;
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        m_Document->CancelPointer();
    if (hovered) {
        m_Document->Hover(point);
        const auto hit = m_Document->HitTest(point);
        m_Renderer.SetCursor(hit.cursor);
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
    m_Document->SetVisited({m_Visited.begin(), m_Visited.end()});
    m_Renderer.Begin(ImGui::GetWindowDrawList());
    const float left = std::max(0.0f, window->InnerRect.Min.x - origin.x);
    const float top = std::max(0.0f, window->InnerRect.Min.y - origin.y);
    m_Document->Draw({origin.x, origin.y}, {left, top, std::max(0.0f, window->InnerRect.Max.x - origin.x - left),
                                            std::max(0.0f, window->InnerRect.Max.y - origin.y - top)});
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
    const auto size = m_Document->ContentSize();
#ifdef VIRTOOLSDEV_UI_TESTS
    // Inspect all actual paint operations without submitting offscreen UI geometry.
    m_Renderer.Begin(nullptr);
    m_Document->Draw({}, {0, 0, size.width, size.height});
    m_PagePaint.drawings = m_Renderer.Drawings();
    m_PagePaint.size = ImVec2(size.width, size.height);
#endif
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(size.width, size.height));
    return true;
}
#ifdef VIRTOOLSDEV_UI_TESTS
size_t HelpWindow::ShownImages() const {
    return std::count_if(m_PagePaint.drawings.begin(), m_PagePaint.drawings.end(),
                         [](const auto &drawing) { return drawing.kind == HtmlRenderer::Drawing::Image; });
}
#endif
void HelpWindow::Draw(EditorViewFrame &frame) {
    if (!m_Pending.empty()) {
        Navigate(m_Pending);
        m_Pending.clear();
    }
    ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);
    EditorImGuiWindow window(frame);
    if (!window) return;
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
        "Original Virtools reference. Some features and scripted examples are not available in NeMoDev.");
    if (ImGui::BeginTable("help", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Navigation", ImGuiTableColumnFlags_WidthFixed, 270.0f);
        ImGui::TableSetupColumn("Topic", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            ImGui::SetKeyboardFocusHere();
            ImGui::SetKeyOwner(ImGuiKey_F, ImGui::GetCurrentWindow()->ID, ImGuiInputFlags_LockThisFrame);
        }
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
            const bool copy = ImGui::SmallButton("Copy text");
            ImGui::TextDisabled("%s", topic->path.c_str());
            ImGui::Separator();
            try {
                if (DrawPage(*topic) && copy && !SDL_SetClipboardText(m_Document->Text().c_str()))
                    m_Notice = SDL_GetError();
            } catch (const std::exception &error) {
                m_Notice = error.what();
            }
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
}
EditorStatus HelpWindow::SaveState(EditorSink sink) const {
    std::vector<EditorString> topics, visits, visited;
    std::vector<std::string> positions;
    positions.reserve(m_History.size());
    for (size_t i = 0; i < m_History.size(); ++i) {
        topics.push_back(EditorText(m_History[i].topic));
        positions.push_back(i == m_Visit && m_Document && m_LayoutTopic == m_Book.Find(m_Current) ?
                            ReadingPosition().Save() : m_History[i].position.Save());
    }
    for (const auto &position : positions) visits.push_back(EditorText(position));
    for (const auto &url : m_Visited) visited.push_back(EditorText(url));
    const EditorValue fields[] = {{EDITOR_TEXT("current"), EDITOR_TEXT_VALUE, {}, EditorText(m_Current)},
        {EDITOR_TEXT("pending"), EDITOR_TEXT_VALUE, {}, EditorText(m_Pending)},
        {EDITOR_TEXT("query"), EDITOR_TEXT_VALUE, {}, EditorText(m_Query)},
        {EDITOR_TEXT("zoom"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, m_Zoom},
        {EDITOR_TEXT("visit"), EDITOR_INTEGER_VALUE, {}, {}, m_Visit},
        {EDITOR_TEXT("topics"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {topics.data(), uint32_t(topics.size())}},
        {EDITOR_TEXT("positions"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {visits.data(), uint32_t(visits.size())}},
        {EDITOR_TEXT("visited"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {visited.data(), uint32_t(visited.size())}}};
    return sink.write ? sink.write(sink.data, fields, 8) : EDITOR_INVALID;
}
EditorStatus HelpWindow::RestoreState(const EditorRequest &request) {
    std::string current, pending, query;
    std::vector<Visit> history;
    std::vector<std::string> topics, positions;
    std::set<std::string> visited;
    size_t visit = 0;
    double zoom = 1;
    const auto texts = [](EditorStrings values) {
        std::vector<std::string> result;
        for (uint32_t i = 0; i < values.count; ++i) result.emplace_back(values.data[i].data, values.data[i].size);
        return result;
    };
    if (request.count != 8) return EDITOR_INVALID;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &field = request.values[i];
        const std::string key(field.key.data, field.key.size);
        if (key == "current" && field.type == EDITOR_TEXT_VALUE) current.assign(field.text.data, field.text.size);
        else if (key == "pending" && field.type == EDITOR_TEXT_VALUE) pending.assign(field.text.data, field.text.size);
        else if (key == "query" && field.type == EDITOR_TEXT_VALUE) query.assign(field.text.data, field.text.size);
        else if (key == "zoom" && field.type == EDITOR_REAL_VALUE) zoom = field.real;
        else if (key == "visit" && field.type == EDITOR_INTEGER_VALUE) visit = field.integer;
        else if (key == "topics" && field.type == EDITOR_TEXT_LIST) topics = texts(field.texts);
        else if (key == "positions" && field.type == EDITOR_TEXT_LIST) positions = texts(field.texts);
        else if (key == "visited" && field.type == EDITOR_TEXT_LIST) {
            const auto urls = texts(field.texts);
            visited.insert(urls.begin(), urls.end());
        } else return EDITOR_INVALID;
    }
    if (topics.size() != positions.size() || topics.size() > 128 ||
        visit >= std::max(size_t(1), topics.size()) || !std::isfinite(zoom) || zoom < 0.75 || zoom > 2) return EDITOR_INVALID;
    for (size_t i = 0; i < topics.size(); ++i) {
        HtmlReadingPosition position;
        if (!position.Load(positions[i])) return EDITOR_INVALID;
        history.push_back({topics[i], std::move(position)});
    }
    m_Current = std::move(current);
    m_Pending = std::move(pending);
    m_Query = std::move(query);
    m_Zoom = float(zoom);
    m_Visit = visit;
    m_History = std::move(history);
    m_Visited = std::move(visited);
    m_Results = m_Book.Search(m_Query);
    m_Anchor.clear();
    m_RestoreScroll = ImVec2(-1, -1);
    if (!m_History.empty()) m_RestorePosition = m_History[m_Visit].position;
    m_Error = m_Current.empty() || m_Book.Find(m_Current) ? std::string() : "This topic is missing from the source help archive: " + m_Current;
    return EDITOR_OK;
}
namespace {
#ifdef VIRTOOLSDEV_UI_TESTS
std::map<EditorView, HelpWindow *> shownHelp;
#endif
EditorStatus OpenHelp(void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    EditorView id = 0;
    EditorValue topic{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE};
    for (uint32_t i = 0; i < request->count; ++i) {
        const auto &value = request->values[i];
        const std::string key(value.key.data, value.key.size);
        if (key == "topic") topic.text = value.text;
        if (key == "view") id = value.integer;
    }
    const EditorRequest navigation{request->scope, &topic, 1};
    return host->navigateView(host->data, EditorString EDITOR_TEXT("help.reference"), id, &navigation, sink);
}
} // namespace
bool RegisterHelpViews(EditorApplication &editor, HtmlFonts fonts, std::string &error) {
    const EditorParameter topic{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, 1};
    const EditorParameter arguments[] = {topic, {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1}};
    const EditorCommandDesc command{sizeof(EditorCommandDesc), EDITOR_TEXT("help.open"), EDITOR_TEXT("builtin.help"),
        EDITOR_TEXT("Online Reference"), arguments, 2, OpenHelp, nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE,
        [](void *, const EditorHost *, const EditorRequest *) {
            std::error_code error;
            return std::filesystem::is_regular_file(std::filesystem::u8path(DataFilePath("help/help.ndh")), error) ?
                EDITOR_OK : EDITOR_REFUSED;
        }};
    auto *data = new HtmlFonts(fonts);
    EditorViewDesc view{sizeof(EditorViewDesc),
                        EDITOR_IMGUI_VERSION,
                        EDITOR_TEXT("help.reference"),
                        EDITOR_TEXT("builtin.help"),
                        EDITOR_TEXT("Help"),
                        [](void *data, const EditorHost *, EditorView id) -> void * {
                            auto view = std::make_unique<HelpWindow>(*static_cast<HtmlFonts *>(data));
                            view->Open();
#ifdef VIRTOOLSDEV_UI_TESTS
                            shownHelp.emplace(id, view.get());
#endif
                            return view.release();
                        },
                        [](void *data) {
#ifdef VIRTOOLSDEV_UI_TESTS
                            for (auto it = shownHelp.begin(); it != shownHelp.end();)
                                if (it->second == data)
                                    it = shownHelp.erase(it);
                                else
                                    ++it;
#endif
                            delete static_cast<HelpWindow *>(data);
                        },
                        [](void *data, const EditorHost *, EditorView, EditorViewFrame *frame) {
                            static_cast<HelpWindow *>(data)->Draw(*frame);
                        },
                        data,
                        EDITOR_WINDOW_VIEW,
                        1,
                        EDITOR_WORKSPACE,
                        &topic,
                        1,
                        [](void *data, const EditorHost *, const EditorRequest *request) {
                            const auto &topic = request->values[0].text;
                            static_cast<HelpWindow *>(data)->Open(
                                std::string(topic.data ? topic.data : "", topic.size));
                            return EDITOR_OK;
                        },
                        1,
                        [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
                            return static_cast<HelpWindow *>(data)->SaveState(sink);
                        },
                        [](void *data, const EditorHost *, const EditorRequest *request) {
                            return static_cast<HelpWindow *>(data)->RestoreState(*request);
                        }};
    static const EditorParameter settings[] = {
        {EDITOR_TEXT("current"), EDITOR_TEXT_VALUE},  {EDITOR_TEXT("pending"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("query"), EDITOR_TEXT_VALUE},    {EDITOR_TEXT("zoom"), EDITOR_REAL_VALUE},
        {EDITOR_TEXT("visit"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("topics"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("positions"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("visited"), EDITOR_TEXT_LIST}};
    view.settings = settings;
    view.settingCount = uint32_t(std::size(settings));
    const EditorBindingDesc binding{EDITOR_TEXT("help.open"), EDITOR_TEXT("F1")};
    const EditorMenuLocation location{EDITOR_TEXT("help.reference"), EDITOR_TEXT("Help menu")};
    const EditorMenuDesc menu{EDITOR_TEXT("help.reference"), EDITOR_TEXT("help.open"),
        EDITOR_TEXT("help.reference"), EDITOR_TEXT("Online Reference"), 0};
    const EditorString dependency = EDITOR_TEXT("workspace.views");
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("help.reference");
    module.dependencies = &dependency;
    module.dependencyCount = 1;
    module.views = &view;
    module.viewCount = 1;
    module.commands = &command;
    module.commandCount = 1;
    module.menus = &menu;
    module.menuCount = 1;
    module.bindings = &binding;
    module.bindingCount = 1;
    module.locations = &location;
    module.locationCount = 1;
    module.data = data;
    module.stop = [](void *data) { delete static_cast<HtmlFonts *>(data); };
    if (editor.Register(module, error)) return true;
    delete data;
    return false;
}
#ifdef VIRTOOLSDEV_UI_TESTS
const HelpWindow &HelpForTests() { return *shownHelp.at(HelpViewForTests()); }
EditorView HelpViewForTests() { return shownHelp.empty() ? 0 : shownHelp.begin()->first; }
#endif
} // namespace vdev

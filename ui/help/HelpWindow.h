#pragma once
#include "ui/help/HelpBook.h"
#include "ui/render/HtmlRenderer.h"
#include <optional>
#include <set>

namespace vdev {
class HelpWindow {
public:
    explicit HelpWindow(HtmlFonts fonts) : m_Renderer(fonts) {}
    void Open(const std::string &topic = std::string());
    void Draw();
    void Close();
#ifdef VIRTOOLSDEV_UI_TESTS
    const std::string &CurrentTopic() const { return m_Current; }
    const std::string &ShownText() const { return m_Document->Text(); }
    size_t ShownImages() const;
    struct PagePainting {
        std::vector<HtmlRenderer::Drawing> drawings;
        ImVec2 size;
    };
    const PagePainting &PagePaint() const { return m_PagePaint; }
    const HtmlDocument &PageDocument() const { return *m_Document; }
#endif
private:
    void Navigate(const std::string &topic);
    void History(int direction);
    void Contents(const std::vector<HelpContents> &items);
    bool DrawPage(const HelpTopic &topic);
    void FollowLink(const HtmlNavigation &navigation);
    HtmlReadingPosition ReadingPosition() const;
    ImVec2 ScrollPosition() const;
    struct Visit {
        std::string topic;
        HtmlReadingPosition position;
    };
    HelpBook m_Book;
    HtmlRenderer m_Renderer;
    std::unique_ptr<HtmlDocument> m_Document;
    const HelpTopic *m_LayoutTopic = nullptr;
    float m_LayoutWidth = 0, m_LayoutHeight = 0, m_LayoutScale = 0;
    bool m_Open = false, m_Focus = false, m_Loaded = false;
    std::string m_Current, m_Pending, m_Error, m_Notice, m_Query;
    std::vector<const HelpTopic *> m_Results;
    std::vector<Visit> m_History;
    std::set<std::string> m_Visited;
    size_t m_Visit = 0;
    ImVec2 m_RestoreScroll = ImVec2(-1, -1);
    ImGuiID m_ArticleId = 0;
    std::optional<HtmlReadingPosition> m_RestorePosition;
    float m_PageY = 0, m_Zoom = 1;
    std::string m_Anchor;
#ifdef VIRTOOLSDEV_UI_TESTS
    PagePainting m_PagePaint;
#endif
};
} // namespace vdev

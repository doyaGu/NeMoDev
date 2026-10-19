#pragma once
#include "ui/help/HelpBook.h"
#include "ui/help/HelpLayout.h"
#include "ui/render/UiRenderer.h"
#include <map>
#include <set>

namespace vdev {
class HelpWindow {
public:
    explicit HelpWindow(HelpFonts fonts) : m_Fonts(fonts) {}
    void Open(const std::string &topic = std::string());
    void Draw();
    void Close();
#ifdef VIRTOOLSDEV_UI_TESTS
    const std::string &CurrentTopic() const { return m_Current; }
    const std::string &ShownText() const { return m_ShownText; }
    size_t ShownImages() const { return m_ShownImages; }
    const HelpPageLayout &PageLayout() const { return m_Page; }
#endif
private:
    void Navigate(const std::string &topic);
    void History(int direction);
    void Contents(const std::vector<HelpContents> &items);
    void DrawPage(const HelpTopic &topic);
    void DrawImageAreas(const HelpDrawing &image, ImVec2 origin);
    void FollowLink(const std::string &link);
    UiTexture *Image(const std::string &path);
    struct Visit { std::string topic; ImVec2 scroll; };
    HelpBook m_Book;
    HelpFonts m_Fonts;
    HelpPageLayout m_Page;
    const HelpTopic *m_LayoutTopic = nullptr;
    float m_LayoutWidth = 0.0f, m_LayoutScale = 0.0f;
    bool m_Open = false, m_Focus = false, m_Loaded = false;
    bool m_RetireImages = false;
    std::string m_Current, m_Pending, m_Error, m_Notice, m_Query;
    std::vector<const HelpTopic *> m_Results;
    std::vector<Visit> m_History;
    std::set<std::string> m_Visited;
    size_t m_Visit = 0;
    ImVec2 m_Scroll, m_RestoreScroll = ImVec2(-1, -1);
    std::string m_Anchor;
    std::map<std::string, std::unique_ptr<UiTexture>> m_Images;
    float m_Zoom = 1.0f;
#ifdef VIRTOOLSDEV_UI_TESTS
    std::string m_ShownText;
    size_t m_ShownImages = 0;
#endif
};
}

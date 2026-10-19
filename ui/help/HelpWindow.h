#pragma once
#include "ui/help/HelpBook.h"
#include "ui/render/UiRenderer.h"
#include <map>

namespace vdev {
class HelpWindow {
public:
    void Open(const std::string &topic = std::string());
    void Draw();
    void Close();
#ifdef VIRTOOLSDEV_UI_TESTS
    const std::string &CurrentTopic() const { return m_Current; }
    const std::string &ShownText() const { return m_ShownText; }
    size_t ShownImages() const { return m_ShownImages; }
#endif
private:
    void Navigate(const std::string &topic);
    void History(int direction);
    void Contents(const std::vector<HelpContents> &items);
    void Blocks(const std::vector<HelpBlock> &blocks);
    void Paragraph(const HelpBlock &block);
    void FollowLink(const std::string &link);
    UiTexture *Image(const std::string &path);
    struct Visit { std::string topic; float scroll = 0.0f; };
    HelpBook m_Book;
    bool m_Open = false, m_Focus = false, m_Loaded = false;
    bool m_RetireImages = false;
    std::string m_Current, m_Pending, m_Error, m_Notice, m_Query;
    std::vector<const HelpTopic *> m_Results;
    std::vector<Visit> m_History;
    size_t m_Visit = 0;
    float m_Scroll = 0.0f, m_RestoreScroll = -1.0f;
    std::string m_Anchor;
    std::map<std::string, float> m_Anchors;
    std::map<std::string, std::unique_ptr<UiTexture>> m_Images;
    int m_Item = 0;
    float m_Zoom = 1.0f;
    float m_LineWidth = 0.0f;
#ifdef VIRTOOLSDEV_UI_TESTS
    std::string m_ShownText;
    size_t m_ShownImages = 0;
#endif
};
}

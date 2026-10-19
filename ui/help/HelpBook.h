#pragma once
#include "html/HtmlDocument.h"
#include <map>
#include <string>
#include <vector>

namespace vdev {

struct HelpTopic {
    std::string path, title, text, html;
};
struct HelpContents {
    std::string title, topic;
    std::vector<HelpContents> children;
};

// Owns the optional, compiled CHM content. All paths and text here are UTF-8
// help data; they never pass through CK's object strings or path interfaces.
class HelpBook : public HtmlResources {
public:
    bool Load(const std::string &directory, std::string &error);
    const std::string &Home() const { return m_Home; }
    const std::vector<HelpContents> &Contents() const { return m_Contents; }
    const HelpTopic *Find(const std::string &path) const;
    std::vector<const HelpTopic *> Search(const std::string &text) const;
    static std::string Url(const std::string &path);
    bool Read(const std::string &url, std::vector<unsigned char> &bytes, std::string &error) override;

private:
    std::string ResourceFile(const std::string &path) const;
    std::string m_Directory, m_Home;
    std::vector<HelpContents> m_Contents;
    std::vector<HelpTopic> m_Topics;
    std::map<std::string, size_t> m_Index;
    std::vector<std::string> m_SearchText;
};
} // namespace vdev

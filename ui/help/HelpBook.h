#pragma once
#include <map>
#include <string>
#include <vector>

namespace vdev {

struct HelpSpan {
    std::string text, link, image, anchor;
    unsigned style = 0; // bold, italic, code, input, output, parameter input/output
};
struct HelpBlock {
    enum Kind { Paragraph, Heading, Preformatted, ListItem, Rule, Table, Row, Cell };
    Kind kind = Paragraph;
    unsigned value = 0; // heading level, list number, table columns or cell span
    std::vector<HelpSpan> spans;
    std::vector<HelpBlock> children;
};
struct HelpTopic {
    std::string path, title, text;
    std::vector<HelpBlock> blocks;
};
struct HelpContents {
    std::string title, topic;
    std::vector<HelpContents> children;
};

// Owns the optional, compiled CHM content. All paths and text here are UTF-8
// help data; they never pass through CK's object strings or path interfaces.
class HelpBook {
public:
    bool Load(const std::string &directory, std::string &error);
    const std::string &Home() const { return m_Home; }
    const std::vector<HelpContents> &Contents() const { return m_Contents; }
    const HelpTopic *Find(const std::string &path) const;
    std::vector<const HelpTopic *> Search(const std::string &text) const;
    std::string ImagePath(const std::string &path) const;
private:
    std::string m_Directory, m_Home;
    std::vector<HelpContents> m_Contents;
    std::vector<HelpTopic> m_Topics;
    std::map<std::string, size_t> m_Index;
    std::vector<std::string> m_SearchText;
};
}

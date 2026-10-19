#include "ui/help/HelpBook.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace vdev {
static std::string HelpLower(std::string text) {
    for (char &c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}
static std::string HelpTopicKey(std::string text) {
    const size_t fragment = text.find('#');
    if (fragment != std::string::npos) text.resize(fragment);
    std::replace(text.begin(), text.end(), '\\', '/');
    std::string decoded;
    const auto hex = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && hex(text[i+1]) >= 0 && hex(text[i+2]) >= 0) {
            decoded += char(hex(text[i+1]) * 16 + hex(text[i+2]));
            i += 2;
        } else decoded += text[i];
    }
    return HelpLower(std::filesystem::u8path(decoded).lexically_normal().generic_u8string());
}

// The compiler writes counts before allocations. Bound both individual values
// and the complete file, including recursive tables and contents entries.
class HelpReader {
public:
    explicit HelpReader(const std::filesystem::path &path) : file(path, std::ios::binary) {
        if (!file) throw std::runtime_error("Help content is not installed.");
        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        if (size < 8 || size > 128 * 1024 * 1024) throw std::runtime_error("Invalid help content size.");
        remaining = static_cast<size_t>(size);
        file.seekg(0);
        char magic[8];
        Read(magic, 8);
        if (std::string(magic, 8) != "NDEVHLP1") throw std::runtime_error("Unsupported help content version.");
    }
    unsigned Number(unsigned limit = 100000) {
        unsigned char bytes[4];
        Read(reinterpret_cast<char *>(bytes), 4);
        const unsigned value = bytes[0] | (unsigned(bytes[1]) << 8) | (unsigned(bytes[2]) << 16) | (unsigned(bytes[3]) << 24);
        if (value > limit) throw std::runtime_error("Invalid help content count.");
        return value;
    }
    std::string Text() {
        const unsigned size = Number(4 * 1024 * 1024);
        if (size > remaining) throw std::runtime_error("Truncated help text.");
        std::string value(size, '\0');
        Read(value.data(), size);
        if (value.find('\0') != std::string::npos) throw std::runtime_error("Invalid help text.");
        return value;
    }
    unsigned Count(unsigned minimumBytes, unsigned limit = 100000) {
        const unsigned count = Number(limit);
        if (count > remaining / minimumBytes) throw std::runtime_error("Truncated help entries.");
        return count;
    }
    std::vector<HelpContents> Contents(unsigned depth = 0) {
        if (depth > 64) throw std::runtime_error("Help contents are too deeply nested.");
        std::vector<HelpContents> items(Count(12));
        for (auto &item : items) {
            item.title = Text(); item.topic = Text(); item.children = Contents(depth + 1);
        }
        return items;
    }
    std::vector<HelpBlock> Blocks(unsigned depth = 0) {
        if (depth > 64) throw std::runtime_error("Help tables are too deeply nested.");
        std::vector<HelpBlock> blocks(Count(16));
        for (auto &block : blocks) {
            block.kind = static_cast<HelpBlock::Kind>(Number(HelpBlock::Cell));
            block.value = Number(10000);
            block.spans.resize(Count(20));
            for (auto &span : block.spans) {
                span.text = Text(); span.link = Text(); span.image = Text(); span.anchor = Text(); span.style = Number(127);
            }
            block.children = Blocks(depth + 1);
        }
        return blocks;
    }
    bool Finished() const { return remaining == 0; }
private:
    void Read(char *bytes, size_t size) {
        if (size > remaining || !file.read(bytes, static_cast<std::streamsize>(size)))
            throw std::runtime_error("Truncated help content.");
        remaining -= size;
    }
    std::ifstream file;
    size_t remaining = 0;
};

static void HelpPlainText(const std::vector<HelpBlock> &blocks, std::string &text) {
    for (const auto &block : blocks) {
        for (const auto &span : block.spans) text += span.text;
        HelpPlainText(block.children, text);
        text += '\n';
    }
}
bool HelpBook::Load(const std::string &directory, std::string &error) {
    try {
        HelpReader reader(std::filesystem::u8path(directory) / "help.ndh");
        HelpBook loaded;
        loaded.m_Directory = directory;
        loaded.m_Home = reader.Text();
        loaded.m_Contents = reader.Contents();
        loaded.m_Topics.resize(reader.Count(12, 10000));
        for (auto &topic : loaded.m_Topics) {
            topic.path = reader.Text(); topic.title = reader.Text(); topic.blocks = reader.Blocks();
            const size_t at = loaded.m_SearchText.size();
            if (!loaded.m_Index.emplace(HelpTopicKey(topic.path), at).second)
                throw std::runtime_error("Duplicate help topic: " + topic.path);
            HelpPlainText(topic.blocks, topic.text);
            loaded.m_SearchText.push_back(HelpLower(topic.title + ' ' + topic.text));
        }
        if (!reader.Finished() || !loaded.Find(loaded.m_Home)) throw std::runtime_error("Invalid help topic index.");
        *this = std::move(loaded);
        error.clear();
        return true;
    } catch (const std::exception &failure) {
        error = failure.what();
        return false;
    }
}
const HelpTopic *HelpBook::Find(const std::string &path) const {
    const auto found = m_Index.find(HelpTopicKey(path));
    return found == m_Index.end() ? nullptr : &m_Topics[found->second];
}
std::vector<const HelpTopic *> HelpBook::Search(const std::string &query) const {
    std::istringstream words(HelpLower(query));
    std::vector<std::string> terms;
    std::string term;
    while (words >> term) terms.push_back(term);
    std::vector<std::pair<int, size_t>> matches;
    if (terms.empty()) return {};
    for (size_t i = 0; i < m_Topics.size(); ++i) {
        const std::string title = HelpLower(m_Topics[i].title);
        bool found = true;
        int score = 0;
        for (const auto &word : terms) {
            if (m_SearchText[i].find(word) == std::string::npos) { found = false; break; }
            if (title.find(word) != std::string::npos) ++score;
        }
        if (found) matches.emplace_back(-score, i);
    }
    std::stable_sort(matches.begin(), matches.end());
    std::vector<const HelpTopic *> result;
    for (const auto &match : matches) result.push_back(&m_Topics[match.second]);
    return result;
}
std::string HelpBook::ImagePath(const std::string &path) const {
    const auto relative = std::filesystem::u8path(path).lexically_normal();
    if (relative.empty() || relative.is_absolute() || path.find(':') != std::string::npos) return {};
    for (const auto &part : relative) if (part == "..") return {};
    // CHM paths are case-insensitive. Resolve images on Linux as on Windows,
    // without altering the source filenames or treating missing files as valid.
    auto current = std::filesystem::u8path(m_Directory);
    std::error_code error;
    for (const auto &part : relative) {
        const auto exact = current / part;
        if (std::filesystem::exists(exact, error)) { current = exact; continue; }
        bool found = false;
        for (const auto &entry : std::filesystem::directory_iterator(current, error)) {
            if (HelpLower(entry.path().filename().u8string()) == HelpLower(part.u8string())) {
                if (found) return {};
                current = entry.path(); found = true;
            }
        }
        if (!found) return {};
    }
    return std::filesystem::is_regular_file(current, error) ? current.u8string() : std::string();
}
}

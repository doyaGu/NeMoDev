#include "ui/help/HelpBook.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace vdev {
static std::string HelpLower(std::string text) {
    for (char &c : text)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return text;
}
static std::string HelpTopicKey(std::string text) {
    if (text.compare(0, 12, "book://help/") == 0)
        text.erase(0, 12);
    const size_t fragment = text.find_first_of("?#");
    if (fragment != std::string::npos)
        text.resize(fragment);
    std::replace(text.begin(), text.end(), '\\', '/');
    std::string decoded;
    const auto hex = [](char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
            decoded += char(hex(text[i + 1]) * 16 + hex(text[i + 2]));
            i += 2;
        } else
            decoded += text[i];
    }
    return HelpLower(std::filesystem::u8path(decoded).lexically_normal().generic_u8string());
}

// The compiler writes counts before allocations. Bound both individual values
// and the complete file, including recursive tables and contents entries.
class HelpReader {
public:
    explicit HelpReader(const std::filesystem::path &path) : file(path, std::ios::binary) {
        if (!file)
            throw std::runtime_error("Help content is not installed.");
        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        if (size < 8 || size > 128 * 1024 * 1024)
            throw std::runtime_error("Invalid help content size.");
        remaining = static_cast<size_t>(size);
        file.seekg(0);
        char magic[8];
        Read(magic, 8);
        if (std::string(magic, 8) != "NDEVHLP5")
            throw std::runtime_error("Unsupported help content version.");
    }
    unsigned Number(unsigned limit = 100000) {
        unsigned char bytes[4];
        Read(reinterpret_cast<char *>(bytes), 4);
        const unsigned value =
            bytes[0] | (unsigned(bytes[1]) << 8) | (unsigned(bytes[2]) << 16) | (unsigned(bytes[3]) << 24);
        if (value > limit)
            throw std::runtime_error("Invalid help content count.");
        return value;
    }
    std::string Text() {
        const unsigned size = Number(4 * 1024 * 1024);
        if (size > remaining)
            throw std::runtime_error("Truncated help text.");
        std::string value(size, '\0');
        Read(value.data(), size);
        if (value.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid help text.");
        return value;
    }
    unsigned Count(unsigned minimumBytes, unsigned limit = 100000) {
        const unsigned count = Number(limit);
        if (count > remaining / minimumBytes)
            throw std::runtime_error("Truncated help entries.");
        return count;
    }
    std::vector<HelpContents> Contents(unsigned depth = 0) {
        if (depth > 64)
            throw std::runtime_error("Help contents are too deeply nested.");
        std::vector<HelpContents> items(Count(12));
        for (auto &item : items) {
            item.title = Text();
            item.topic = Text();
            item.children = Contents(depth + 1);
        }
        return items;
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

bool HelpBook::Load(const std::string &directory, std::string &error) {
    try {
        HelpReader reader(std::filesystem::u8path(directory) / "help.ndh");
        HelpBook loaded;
        loaded.m_Directory = directory;
        loaded.m_Home = reader.Text();
        loaded.m_Contents = reader.Contents();
        loaded.m_Topics.resize(reader.Count(16, 10000));
        for (auto &topic : loaded.m_Topics) {
            topic.path = reader.Text();
            topic.title = reader.Text();
            topic.html = reader.Text();
            topic.text = reader.Text();
            const size_t at = loaded.m_SearchText.size();
            if (!loaded.m_Index.emplace(HelpTopicKey(topic.path), at).second)
                throw std::runtime_error("Duplicate help topic: " + topic.path);
            loaded.m_SearchText.push_back(HelpLower(topic.title + ' ' + topic.text));
        }
        if (!reader.Finished() || !loaded.Find(loaded.m_Home))
            throw std::runtime_error("Invalid help topic index.");
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
    while (words >> term)
        terms.push_back(term);
    std::vector<std::pair<int, size_t>> matches;
    if (terms.empty())
        return {};
    for (size_t i = 0; i < m_Topics.size(); ++i) {
        const std::string title = HelpLower(m_Topics[i].title);
        bool found = true;
        int score = 0;
        for (const auto &word : terms) {
            if (m_SearchText[i].find(word) == std::string::npos) {
                found = false;
                break;
            }
            if (title.find(word) != std::string::npos)
                ++score;
        }
        if (found)
            matches.emplace_back(-score, i);
    }
    std::stable_sort(matches.begin(), matches.end());
    std::vector<const HelpTopic *> result;
    for (const auto &match : matches)
        result.push_back(&m_Topics[match.second]);
    return result;
}
std::string HelpBook::ResourceFile(const std::string &path) const {
    const auto relative = std::filesystem::u8path(path).lexically_normal();
    if (relative.empty() || relative.is_absolute() || path.find(':') != std::string::npos)
        return {};
    for (const auto &part : relative)
        if (part == "..")
            return {};
    // CHM paths are case-insensitive. Resolve images on Linux as on Windows,
    // without altering the source filenames or treating missing files as valid.
    auto current = std::filesystem::u8path(m_Directory);
    std::error_code error;
    for (const auto &part : relative) {
        const auto exact = current / part;
        if (std::filesystem::exists(exact, error)) {
            current = exact;
            continue;
        }
        bool found = false;
        for (const auto &entry : std::filesystem::directory_iterator(current, error)) {
            if (HelpLower(entry.path().filename().u8string()) == HelpLower(part.u8string())) {
                if (found)
                    return {};
                current = entry.path();
                found = true;
            }
        }
        if (!found)
            return {};
    }
    const auto resolved = std::filesystem::weakly_canonical(current, error);
    if (error)
        return {};
    const auto root = std::filesystem::weakly_canonical(std::filesystem::u8path(m_Directory), error);
    if (error)
        return {};
    const auto confined = resolved.lexically_relative(root);
    if (confined.empty() || confined.is_absolute())
        return {};
    for (const auto &part : confined)
        if (part == "..")
            return {};
    return std::filesystem::is_regular_file(resolved, error) ? resolved.u8string() : std::string();
}
std::string HelpBook::Url(const std::string &path) {
    std::string result = "book://help/";
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned char c : path) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' ||
            c == '_' || c == '.' || c == '~')
            result += char(c);
        else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}
bool HelpBook::Read(const std::string &url, std::vector<unsigned char> &bytes, std::string &error) {
    if (url.compare(0, 12, "book://help/") != 0) {
        error = "Resources outside this help book are not loaded: " + url;
        return false;
    }
    const std::string key = HelpTopicKey(url);
    if (key.find('\0') != std::string::npos) {
        error = "Invalid help resource URL.";
        return false;
    }
    const std::string path = ResourceFile(key);
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size < 0 || size > 64 * 1024 * 1024) {
        error = "Missing or invalid help resource: " + url;
        return false;
    }
    std::vector<unsigned char> loaded(static_cast<size_t>(size));
    file.seekg(0);
    if (size && !file.read(reinterpret_cast<char *>(loaded.data()), static_cast<std::streamsize>(size))) {
        error = "Cannot read help resource: " + url;
        return false;
    }
    bytes = std::move(loaded);
    error.clear();
    return true;
}
} // namespace vdev

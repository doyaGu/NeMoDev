#include "core/document/BehaviorDescription.h"

#include <cstdio>
#include <filesystem>

namespace vdev {
namespace fs = std::filesystem;

std::string BehaviorDescriptionPath(const std::string &nms) {
    return (nms.size() >= 3 ? nms.substr(0, nms.size() - 3) : nms) + "dsc";
}

bool ReadBehaviorDescription(const std::string &nms, BehaviorDescription &description) {
    #ifdef _WIN32
    FILE *file = _wfopen(fs::u8path(BehaviorDescriptionPath(nms)).c_str(), L"rb");
    #else
    FILE *file = fopen(BehaviorDescriptionPath(nms).c_str(), "rb");
    #endif
    if (!file) return false;
    BehaviorDescription read;
    int cid = 0;
    if (std::fread(&cid, 4, 1, file) != 1 || std::fread(&read.version, 4, 1, file) != 1) {
        std::fclose(file);
        return false;
    }
    read.cid = static_cast<CK_CLASSID>(cid);
    auto line = [file](std::string &text) {
        for (int c; (c = std::fgetc(file)) != EOF && c != '\n';) text += static_cast<char>(c);
    };
    line(read.name);
    line(read.author);
    std::string rest;
    for (int c; (c = std::fgetc(file)) != EOF;) rest += static_cast<char>(c);
    std::fclose(file);
    if (!rest.empty()) rest.pop_back();
    for (char c : rest)
        if (c != '\r') read.description += c;
    description = read;
    return true;
}

bool WriteBehaviorDescription(const std::string &nms, const BehaviorDescription &description) {
    #ifdef _WIN32
    FILE *file = _wfopen(fs::u8path(BehaviorDescriptionPath(nms)).c_str(), L"wb");
    #else
    FILE *file = fopen(BehaviorDescriptionPath(nms).c_str(), "wb");
    #endif
    if (!file) return false;
    const int cid = static_cast<int>(description.cid);
    std::fwrite(&cid, 4, 1, file);
    std::fwrite(&description.version, 4, 1, file);
    std::string text;
    for (char c : description.description) {
        if (c == '\n') text += '\r';
        if (c != '\r') text += c;
    }
    std::fprintf(file, "%s\n%s\n%s\n", description.name.c_str(), description.author.c_str(), text.c_str());
    return std::fclose(file) == 0;
}

} // namespace vdev

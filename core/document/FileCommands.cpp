#include "core/document/FileCommands.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <vector>

#include "CKAll.h"

namespace vdev {

namespace {

// Dev.exe's resource 711 (RT_HTML).
const char WEB_PAGE_TEMPLATE[] =
    "<HTML>\r\n"
    "<HEAD>\r\n"
    "<TITLE>$PAGENAME$</TITLE>\r\n"
    "</HEAD>\r\n"
    "<BODY BGCOLOR=\"#000000\" TEXT=\"#00FF00\" LINK=\"#FFFFFF\" VLINK=\"#C0C0C0\">\r\n"
    "<OBJECT\r\n"
    "    CLASSID=\"CLSID:C4925E65-7A1E-11D2-8BB4-00A0C9CC72C3\"\r\n"
    "    ID=\"Virtools\" WIDTH=\"$WIDTH$\" HEIGHT=\"$HEIGHT$\"\r\n"
    "    CODEBASE=\"http://player.virtools.com/downloads/player/Install$PLAYER_VERSION$/Installer.exe#Version=$PLAYER_COMPONENTS$,0,1\">\r\n"
    "    <PARAM NAME=\"SRC\" VALUE=\"$CMO$\">\r\n"
    "    <EMBED\r\n"
    "        SRC=\"$CMO$\"\r\n"
    "        TYPE=\"application/x-virtools\"\r\n"
    "        PLUGINSPAGE=\"http://player.virtools.com/downloads/playernetscape$PLAYER_VERSION$.asp\"\r\n"
    "        WIDTH=\"$WIDTH$\" HEIGHT=\"$HEIGHT$\"\r\n"
    "        NAME=\"Virtools\">\r\n"
    "    </EMBED> \r\n"
    "</OBJECT>\r\n"
    "</BODY>\r\n"
    "</HTML>\r\n"
    "\r\n";

void ReplaceAll(std::string &text, const std::string &tag, const std::string &value) {
    for (size_t at = text.find(tag); at != std::string::npos; at = text.find(tag, at + value.size()))
        text.replace(at, tag.size(), value);
}

size_t NameStart(const std::string &path) {
    const size_t slash = path.find_last_of("\\/:");
    return slash == std::string::npos ? 0 : slash + 1;
}

} // namespace

std::string VersionedPath(const std::string &path, int step) {
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return std::string();
    size_t start = dot;
    while (start > 0 && path[start - 1] >= '0' && path[start - 1] <= '9') --start;
    int number = 0, digits = 2;
    if (start < dot) {
        number = std::atoi(path.substr(start, dot - start).c_str());
        if (dot - start > 2) digits = static_cast<int>(dot - start);
    }
    char text[64];
    std::snprintf(text, sizeof(text), "%0*d", digits, number + step);
    std::string versioned = path.substr(0, start);
    if (versioned.empty() || versioned.back() != '_') versioned += '_';
    return versioned + text + path.substr(dot);
}

std::string WebPageCompositionPath(const std::string &htmlPath, bool cmo) {
    // Dev splits the page's path: its drive and folder, its name, the extension.
    const size_t name = NameStart(htmlPath);
    const size_t dot = htmlPath.rfind('.');
    const std::string stem = dot == std::string::npos || dot < name ? htmlPath : htmlPath.substr(0, dot);
    return stem + (cmo ? ".cmo" : ".vmo");
}

std::string WebPageHtml(const std::string &title, const std::string &compositionPath, int width, int height, int major,
                        int minor) {
    std::string page = WEB_PAGE_TEMPLATE;
    const std::string dotted = std::to_string(major) + "." + std::to_string(minor);
    ReplaceAll(page, "$PLAYER_VERSION$", dotted);
    ReplaceAll(page, "$PLAYER_COMPONENTS$", std::to_string(major) + "," + std::to_string(minor));
    ReplaceAll(page, "$PAGENAME$", title);
    ReplaceAll(page, "$CMO$", compositionPath.substr(NameStart(compositionPath)));
    ReplaceAll(page, "$WIDTH$", std::to_string(width));
    ReplaceAll(page, "$HEIGHT$", std::to_string(height));
    return page;
}


bool IsPlayerFile(const std::string &path) {
    // CKFileInfo: the signature, CRC, CK version, file version, file version 2,
    // write mode.
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    char header[28] = {};
    if (!file.read(header, sizeof(header)) || std::memcmp(header, "Nemo Fi", 8) != 0) return false;
    CKDWORD writeMode = 0;
    std::memcpy(&writeMode, header + 24, sizeof(writeMode));
    return (writeMode & CKFILE_FORVIEWER) != 0;
}

} // namespace vdev

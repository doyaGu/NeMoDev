#pragma once

#include <cctype>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

namespace vdev {

struct EmbeddedImage {
    const char *group;
    const char *kind;
    int id;
    int width;
    int height;
    const unsigned char *rgba;
};
const EmbeddedImage *FindEmbeddedImage(const char *group, const char *kind, int id);

// The images built into the program (resources/images.txt lists them,
// resources/resources.rc names them, tools/ExtractResources writes both) and
// the data files beside it (resources/data).
//
// An image belongs to a group: the editor's own (ui), the parameter UIs' (parameter)
// or the application's (app). Its resource name is <GROUP>_<KIND>_<id>.
struct ResourceEntry {
    std::string group; // ui, parameter, app
    std::string kind;  // bitmap, icon, appicon (the program's own icon)
    int id = 0;
    int size = 0; // an icon's, as drawn
};

// The manifest's entries: "<group> <kind> <id> [size]" per line, '#' a comment.
inline std::vector<ResourceEntry> ReadResourceManifest(std::istream &in) {
    std::vector<ResourceEntry> entries;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        ResourceEntry entry;
        if (!(fields >> entry.group >> entry.kind >> entry.id)) continue;
        fields >> entry.size;
        entries.push_back(entry);
    }
    return entries;
}

// The program's icon: Windows shows the first named icon, so its name sorts before every other.
constexpr const char *APP_ICON_NAME = "APPICON";

// The image's resource name, upper case.
inline std::string ResourceName(const std::string &group, const std::string &kind, int id) {
    if (kind == "appicon") return APP_ICON_NAME;
    std::string name = group + "_" + kind + "_" + std::to_string(id);
    for (char &c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return name;
}

// The data files the editor reads.
constexpr const char *INTERFACE_FILE = "interface.nmo"; // the shapes of frames and icons
constexpr const char *HELP_FILE = "documentation.chm";

// An application data path, independent of the working directory. UTF-8.
std::string DataFilePath(const std::string &name);

} // namespace vdev

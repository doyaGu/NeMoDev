// Extracts the images the editor draws from a Virtools Dev install into
// a local resource directory: the bitmaps and icons listed in images.txt,
// their resource declarations, and interface.nmo and documentation.chm.
// The editor embeds the images and deploys the data beside its executable.
//
//   ExtractResources <Dev install> <resources> [images.txt]
//
// The groups are where an image comes from: ui (resvis.dll), parameter
// (Managers\CK2UI.dll), app (Dev.exe).
//
// A bitmap is its RT_BITMAP resource with a file header; an icon its
// RT_GROUP_ICON with the RT_ICON images it names, as an .ico file.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/runtime/Resources.h"

namespace fs = std::filesystem;

namespace {

using vdev::ResourceEntry;

// Each group's file in the Dev install.
const std::pair<const char *, const char *> SOURCES[] = {
    {"ui", "resvis.dll"}, {"parameter", "Managers\\CK2UI.dll"}, {"app", "Dev.exe"}};

#pragma pack(push, 1)
struct GroupIconEntry { // GRPICONDIRENTRY
    BYTE width, height, colors, reserved;
    WORD planes, bitCount;
    DWORD bytes;
    WORD id;
};
struct IconFileEntry { // ICONDIRENTRY
    BYTE width, height, colors, reserved;
    WORD planes, bitCount;
    DWORD bytes;
    DWORD offset;
};
#pragma pack(pop)

std::vector<BYTE> ResourceData(HMODULE module, int id, LPCWSTR type) {
    HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(id), type);
    HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
    const BYTE *data = loaded ? static_cast<const BYTE *>(LockResource(loaded)) : nullptr;
    const DWORD size = found ? SizeofResource(module, found) : 0;
    return data && size ? std::vector<BYTE>(data, data + size) : std::vector<BYTE>();
}

bool Write(const fs::path &path, const std::vector<BYTE> &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

// RT_BITMAP: a BITMAPINFOHEADER (or a larger header), its color table, the
// bits; a .bmp file puts a BITMAPFILEHEADER before them.
std::vector<BYTE> BitmapFile(const std::vector<BYTE> &resource) {
    if (resource.size() < sizeof(BITMAPINFOHEADER)) return {};
    const auto *info = reinterpret_cast<const BITMAPINFOHEADER *>(resource.data());
    DWORD colors = info->biClrUsed;
    if (!colors && info->biBitCount <= 8) colors = 1u << info->biBitCount;
    DWORD table = colors * sizeof(RGBQUAD);
    if (info->biSize == sizeof(BITMAPINFOHEADER) && info->biCompression == BI_BITFIELDS) table += 3 * sizeof(DWORD);
    BITMAPFILEHEADER file = {};
    file.bfType = 0x4D42; // BM
    file.bfSize = static_cast<DWORD>(sizeof(file) + resource.size());
    file.bfOffBits = static_cast<DWORD>(sizeof(file) + info->biSize + table);
    std::vector<BYTE> bytes(reinterpret_cast<const BYTE *>(&file), reinterpret_cast<const BYTE *>(&file) + sizeof(file));
    bytes.insert(bytes.end(), resource.begin(), resource.end());
    return bytes;
}

// RT_GROUP_ICON: a directory naming RT_ICON images by id; an .ico file has
// the same directory with each image's offset in the file, then the images.
std::vector<BYTE> IconFile(HMODULE module, const std::vector<BYTE> &group) {
    if (group.size() < 6) return {};
    const WORD count = *reinterpret_cast<const WORD *>(group.data() + 4);
    if (group.size() < 6 + count * sizeof(GroupIconEntry)) return {};
    const auto *entries = reinterpret_cast<const GroupIconEntry *>(group.data() + 6);
    std::vector<std::vector<BYTE>> images;
    for (WORD i = 0; i < count; ++i) {
        images.push_back(ResourceData(module, entries[i].id, MAKEINTRESOURCEW(3))); // RT_ICON
        if (images.back().empty()) return {};
    }
    std::vector<BYTE> bytes(group.begin(), group.begin() + 6);
    DWORD offset = static_cast<DWORD>(6 + count * sizeof(IconFileEntry));
    for (WORD i = 0; i < count; ++i) {
        IconFileEntry entry = {entries[i].width,  entries[i].height,   entries[i].colors, entries[i].reserved,
                               entries[i].planes, entries[i].bitCount, static_cast<DWORD>(images[i].size()), offset};
        offset += entry.bytes;
        bytes.insert(bytes.end(), reinterpret_cast<const BYTE *>(&entry),
                     reinterpret_cast<const BYTE *>(&entry) + sizeof(entry));
    }
    for (const auto &image : images) bytes.insert(bytes.end(), image.begin(), image.end());
    return bytes;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: ExtractResources <Dev install> <resources> [images.txt]\n");
        return 2;
    }
    const fs::path dev = argv[1], out = argv[2];
    const fs::path manifestPath = argc == 4 ? fs::path(argv[3]) : out / "images.txt";
    std::ifstream manifest(manifestPath);
    if (!manifest) {
        std::fprintf(stderr, "no %s\n", manifestPath.string().c_str());
        return 2;
    }
    const std::vector<ResourceEntry> entries = vdev::ReadResourceManifest(manifest);
    manifest.close();
    fs::create_directories(out);
    if (!fs::exists(out / "images.txt") || !fs::equivalent(manifestPath, out / "images.txt")) {
        std::error_code error;
        fs::copy_file(manifestPath, out / "images.txt", fs::copy_options::overwrite_existing, error);
        if (error) {
            std::fprintf(stderr, "cannot copy resource manifest: %s\n", error.message().c_str());
            return 1;
        }
    }
    std::map<std::string, HMODULE> groups;
    for (const auto &source : SOURCES) {
        HMODULE handle = LoadLibraryExW((dev / source.second).wstring().c_str(), nullptr,
                                        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (!handle) {
            std::fprintf(stderr, "cannot read %s\n", (dev / source.second).string().c_str());
            return 1;
        }
        groups[source.first] = handle;
    }
    std::ostringstream rc;
    rc << "// Written by tools/ExtractResources from images.txt; see README.md.\n\n";
    std::set<std::string> names;
    int failures = 0, written = 0;
    for (const ResourceEntry &entry : entries) {
        auto group = groups.find(entry.group);
        const bool bitmap = entry.kind == "bitmap";
        if (group == groups.end() || (!bitmap && entry.kind != "icon" && entry.kind != "appicon")) {
            std::fprintf(stderr, "unknown entry: %s %s %d\n", entry.group.c_str(), entry.kind.c_str(), entry.id);
            ++failures;
            continue;
        }
        const std::string file = entry.kind + "-" + std::to_string(entry.id) + (bitmap ? ".bmp" : ".ico");
        const fs::path folder = fs::path("images") / entry.group;
        const std::string name = vdev::ResourceName(entry.group, entry.kind, entry.id);
        if (!names.insert(name).second) continue; // an icon at two sizes
        HMODULE handle = group->second;
        const std::vector<BYTE> bytes =
            bitmap ? BitmapFile(ResourceData(handle, entry.id, MAKEINTRESOURCEW(2)))   // RT_BITMAP
                   : IconFile(handle, ResourceData(handle, entry.id, MAKEINTRESOURCEW(14))); // RT_GROUP_ICON
        fs::create_directories(out / folder);
        if (bytes.empty() || !Write(out / folder / file, bytes)) {
            std::fprintf(stderr, "missing: %s %s %d\n", entry.group.c_str(), entry.kind.c_str(), entry.id);
            ++failures;
            continue;
        }
        rc << name << (bitmap ? " BITMAP \"" : " ICON \"") << (folder / file).generic_string() << "\"\n";
        ++written;
    }
    std::ofstream(out / "resources.rc") << rc.str();
    for (const fs::path file : {fs::path("InterfacePlugins") / vdev::INTERFACE_FILE,
                                fs::path("Documentation") / vdev::HELP_FILE}) {
        if (file.filename() == vdev::HELP_FILE && !fs::exists(dev / file)) continue;
        std::error_code error;
        fs::create_directories(out / "data");
        fs::copy_file(dev / file, out / "data" / file.filename(), fs::copy_options::overwrite_existing, error);
        if (error) {
            std::fprintf(stderr, "cannot copy %s: %s\n", (dev / file).string().c_str(), error.message().c_str());
            ++failures;
        }
    }
    std::printf("%d resources written, %d failures\n", written, failures);
    return failures ? 1 : 0;
}

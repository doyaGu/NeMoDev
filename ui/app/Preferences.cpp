#include "ui/app/Preferences.h"
#include "core/runtime/TextEncoding.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <functional>
#include <map>
#include <vector>

#include "ui/schematic/SchematicStyle.h"

namespace vdev {

namespace {

constexpr size_t MAX_RECENT_FILES = 8;

struct Field {
    const char *key; // also Dev's registry value name
    std::function<int &(Preferences &)> integer;
    std::function<bool &(Preferences &)> boolean;
    std::function<CKDWORD &(Preferences &)> color;
};

const std::vector<Field> &Fields() {
    using P = Preferences;
    static const std::vector<Field> fields = {
        {"Automatic Link", nullptr, [](P &p) -> bool & { return p.automaticLink; }, nullptr},
        {"Strict MultiSelection", nullptr, [](P &p) -> bool & { return p.strictMultiSelection; }, nullptr},
        {"Default Link Delay", [](P &p) -> int & { return p.defaultLinkDelay; }, nullptr, nullptr},
        {"Auto Loop Link Delay", nullptr, [](P &p) -> bool & { return p.autoLoopLinkDelay; }, nullptr},
        {"Search Best Path Link", nullptr, [](P &p) -> bool & { return p.searchBestPath; }, nullptr},
        {"Max Recent Building Block Count", [](P &p) -> int & { return p.maxRecentBlocks; }, nullptr, nullptr},
        {"Enable Tooltips", nullptr, [](P &p) -> bool & { return p.tooltips; }, nullptr},
        {"Tooltips Delay(ms)", [](P &p) -> int & { return p.tooltipDelayMs; }, nullptr, nullptr},
        {"Show Script Left Part", nullptr, [](P &p) -> bool & { return p.showScriptLeftPart; }, nullptr},
        {"Show Control Points", nullptr, [](P &p) -> bool & { return p.showControlPoints; }, nullptr},
        {"Show Link Delay", nullptr, [](P &p) -> bool & { return p.showLinkDelay; }, nullptr},
        {"Show Priorities", nullptr, [](P &p) -> bool & { return p.showPriorities; }, nullptr},
        {"Behavior Breakpoint Auto Reengage", nullptr, [](P &p) -> bool & { return p.behaviorBreakpointReengage; }, nullptr},
        {"Link Breakpoint Auto Reengage", nullptr, [](P &p) -> bool & { return p.linkBreakpointReengage; }, nullptr},
        {"Parameter Breakpoint Auto Reengage", nullptr, [](P &p) -> bool & { return p.parameterBreakpointReengage; }, nullptr},
        {"Redraw Schematic Only When Breakpoint Reached", nullptr, [](P &p) -> bool & { return p.redrawOnlyAtBreakpoint; }, nullptr},
        {"Io Link Color", nullptr, nullptr, [](P &p) -> CKDWORD & { return p.ioLinkColor; }},
        {"Parameter Link Color", nullptr, nullptr, [](P &p) -> CKDWORD & { return p.parameterLinkColor; }},
        {"Exported Parameter Link Color", nullptr, nullptr, [](P &p) -> CKDWORD & { return p.exportedLinkColor; }},
        {"Parameter Operation Link Color", nullptr, nullptr, [](P &p) -> CKDWORD & { return p.operationLinkColor; }},
        {"Valid Link Color", nullptr, nullptr, [](P &p) -> CKDWORD & { return p.validLinkColor; }},
        {"Invalid Link Color", nullptr, nullptr, [](P &p) -> CKDWORD & { return p.invalidLinkColor; }},
        {"Behavior Obsolete Color", nullptr, nullptr, [](P &p) -> CKDWORD & { return p.obsoleteColor; }},
        {"Behavior No Prototype Color", nullptr, nullptr, [](P &p) -> CKDWORD & { return p.noPrototypeColor; }},
        {"Behavior No CKObject Color", nullptr, nullptr, [](P &p) -> CKDWORD & { return p.noObjectColor; }},
        {"Building Block to Replace Color", nullptr, nullptr, [](P &p) -> CKDWORD & { return p.toReplaceColor; }},
    };
    return fields;
}

void Assign(Preferences &preferences, const Field &field, unsigned long value) {
    if (field.integer) field.integer(preferences) = static_cast<int>(value);
    if (field.boolean) field.boolean(preferences) = value != 0;
    if (field.color) field.color(preferences) = static_cast<CKDWORD>(value & 0xFFFFFF);
}

// Split "<registry key>\<value name>" at the last backslash.
std::pair<std::string, std::string> SplitKey(const std::string &key) {
    const size_t slash = key.find_last_of('\\');
    return {key.substr(0, slash), key.substr(slash + 1)};
}

} // namespace

// Dev's defaults: the values Dev wrote under HKCU\Software\Virtools on the
// reference machine (untouched preferences, matching the screenshots) and
// Dev.exe's defaults (sub_5AF380 for Miscellaneous Controls: compression
// level 4, whole file compressed, textures as raw data, sounds external,
// quantization 10). "File Options\..." and "BehaviorManager\..." come from
// dev.ini, not the registry.
const std::vector<DevSetting> &DevSettings() {
    static const std::vector<DevSetting> settings = {
        // 3D Layout - Rendering
        {"3D Layout\\Author Mode\\Device Driver", "", 's'},
        {"3D Layout\\Author Mode\\Render Type Index", "2", 'i'},
        {"3D Layout\\Author Mode\\Render Type Wireframe", "0", 'b'},
        {"3D Layout\\Author Mode\\Render Type Textured", "1", 'b'},
        {"3D Layout\\Author Mode\\Show Frames", "1", 'b'},
        {"3D Layout\\Author Mode\\Show Curves", "1", 'b'},
        {"3D Layout\\Author Mode\\Show WorldAxis", "1", 'b'},
        {"3D Layout\\Author Mode\\Show Lights", "1", 'b'},
        {"3D Layout\\Author Mode\\Show Cameras", "1", 'b'},
        {"3D Layout\\Author Mode\\Show ObjectAxis", "1", 'b'},
        {"3D Layout\\Author Mode\\Show Portals", "1", 'b'},
        {"3D Layout\\Author Mode\\Show Grid", "1", 'b'},
        {"3D Layout\\Author RealTime Notifications", "0", 'b'},
        {"3D Layout\\Environment\\Ignore Camera Aspect Ratio", "0", 'b'},
        {"3D Layout\\Environment\\Screen Size", "0|0", 's'},
        {"3D Layout\\Environment\\Ambient Light Color", "986895", 'i'},
        {"3D Layout\\Environment\\Background Color", "4286611584", 'i'},
        {"3D Layout\\Environment\\Fog Color", "0", 'i'},
        {"3D Layout\\Environment\\Fog Mode", "0", 'i'},
        {"3D Layout\\Environment\\Fog Start", "1", 'f'},
        {"3D Layout\\Environment\\Fog End", "100", 'f'},
        {"3D Layout\\Environment\\Fog Density", "1", 'f'},
        {"3D Layout\\Player Mode\\FullScren Driver", "", 's'},
        {"3D Layout\\Player Mode\\Render Type Index", "2", 'i'},
        {"3D Layout\\Player Mode\\Render Type Wireframe", "0", 'b'},
        {"3D Layout\\Player Mode\\Render Type Textured", "1", 'b'},
        {"3D Layout\\Player Mode\\Render Mode", "640x480x32x59", 's'},
        {"3D Layout\\Player Mode\\Show Frames", "0", 'b'},
        {"3D Layout\\Player Mode\\Show Curves", "0", 'b'},
        {"3D Layout\\Player Mode\\Show Lights", "0", 'b'},
        {"3D Layout\\Player Mode\\Show Cameras", "0", 'b'},
        {"3D Layout\\Player Mode\\Show Grid", "0", 'b'},
        {"3D Layout\\Player Mode\\Show Portals", "0", 'b'},
        {"3D Layout\\Player Mode\\Auto FullScreen", "0", 'b'},
        {"3D Layout\\Player Mode\\Auto Show Selection", "1", 'b'},
        {"3D Layout\\Player Mode\\Update Cursor", "1", 'b'},
        {"3D Layout\\Player RealTime Notifications", "0", 'b'},
        {"3D Layout\\Player RealTime Notifications FrameSkip", "60", 'i'},
        // 3D Layout - Actions
        {"3D Layout\\Actions\\Object Translate Step", "0.1", 'f'},
        {"3D Layout\\Actions\\Object Rotate Step", "180", 'f'},
        {"3D Layout\\Actions\\Object Scale Step", "5", 'f'},
        {"3D Layout\\Actions\\Object Translate Sticky Mode", "1", 'b'},
        {"3D Layout\\Actions\\Object Rotate Sticky Mode", "0", 'b'},
        {"3D Layout\\Actions\\Object Scale Sticky Mode", "0", 'b'},
        {"3D Layout\\Actions\\Camera Rotate Step", "1", 'f'},
        {"3D Layout\\Actions\\Camera Translate Step", "100", 'f'},
        {"3D Layout\\Actions\\Camera Zoom Step", "500", 'f'},
        {"3D Layout\\Actions\\Camera Orthographic Zoom Step", "10", 'f'},
        {"3D Layout\\Actions\\Camera Roll Step", "5", 'f'},
        {"3D Layout\\Actions\\Camera Fov Step", "1", 'f'},
        {"3D Layout\\Actions\\Camera Fly Turn Step", "1", 'f'},
        {"3D Layout\\Actions\\Camera Fly Advance Step", "100", 'f'},
        {"3D Layout\\Actions\\Camera Rotate Constant Speed Mode", "0", 'b'},
        {"3D Layout\\Actions\\Camera Translate Constant Speed Mode", "0", 'b'},
        {"3D Layout\\Actions\\Camera Zoom Constant Speed Mode", "1", 'b'},
        {"3D Layout\\Actions\\Camera OrthoZoom Constant Speed Mode", "1", 'b'},
        {"3D Layout\\Actions\\Camera Roll Constant Speed Mode", "0", 'b'},
        {"3D Layout\\Actions\\Camera Fov Constant Speed Mode", "0", 'b'},
        {"3D Layout\\Actions\\Camera Navigate Constant Speed Mode", "1", 'b'},
        {"3D Layout\\Actions\\Free Camera Orbit Type", "0", 'i'},
        {"3D Layout\\Actions\\Free Camera Orbit Value", "10", 'f'},
        {"3D Layout\\Actions\\Orbit Selection Type", "0", 'i'},
        // 3D Layout - Interface
        {"3D Layout\\Interface Objects\\Constant Frame", "0", 'b'},
        {"3D Layout\\Interface Objects\\Constant Camera", "0", 'b'},
        {"3D Layout\\Interface Objects\\Constant Light", "0", 'b'},
        {"3D Layout\\Interface Objects\\Constant Curve Point", "0", 'b'},
        {"3D Layout\\Interface Objects\\Constant Object Axis", "0", 'b'},
        {"3D Layout\\Interface Objects\\Snap Marker", "0", 'b'},
        {"3D Layout\\Interface Objects\\Frame Zoom Percentage", "60", 'f'},
        {"3D Layout\\Interface Objects\\Camera Zoom Percentage", "80", 'f'},
        {"3D Layout\\Interface Objects\\Light Zoom Percentage", "80", 'f'},
        {"3D Layout\\Interface Objects\\Curve Point Zoom Percentage", "10", 'f'},
        {"3D Layout\\Interface Objects\\Object Axis Zoom Percentage", "10", 'f'},
        {"3D Layout\\Interface Objects\\Snap Marker Percentage", "60", 'f'},
        {"3D Layout\\Auto Show Light Cone", "1", 'b'},
        {"3D Layout\\Auto Show Camera Cone", "1", 'b'},
        {"3D Layout\\Auto Show Entities In Player Mode", "0", 'b'},
        {"3D Layout\\Auto Show Setup", "1", 'b'},
        {"3D Layout\\Show New Setups", "0", 'b'}, // "Do not open setups of the same type" is its inverse
        {"3D Layout\\Actions\\Camera Plane Distance", "30", 'f'},
        {"3D Layout\\Actions\\Camera Plane Distance Ortho", "1000", 'f'},
        {"3D Layout\\Debug Mode", "0", 'b'},
        {"3D Layout\\Strict Selection", "0", 'b'}, // the 3D Layout's Selection Mode (rectangles take what they touch)
        {"3D Layout\\World Size Min", "-1e+006|-1e+006|-1e+006", 's'},
        {"3D Layout\\World Size Max", "1e+006|1e+006|1e+006", 's'},
        {"3D Layout\\World Step", "1|1|1", 's'},
        {"3D Layout\\Screen Offset", "0|0", 's'},
        {"3D Layout\\Screen Step", "20|20", 's'},
        {"3D Layout\\Homogenous Screen Grid", "0", 'b'},
        {"3D Layout\\Show Screen Grid", "0", 'b'},
        // 3D Layout - Reference Guide
        {"3D Layout\\Reference Grid\\Orientation", "5", 'i'},
        {"3D Layout\\Reference Grid\\Sections", "20;20", 's'},
        {"3D Layout\\Reference Grid\\Step", "10;10", 's'},
        {"3D Layout\\Reference Grid\\Color", "-1", 'i'}, // 0xFFFFFFFF
        {"3D Layout\\Reference Grid\\Orientation Dir", "1e-006|1e-006|1", 's'},
        {"3D Layout\\Reference Grid\\Orientation Up", "1e-006|1|1e-006", 's'},
        {"3D Layout\\Reference Grid\\Orientation Right", "1|1e-006|1e-006", 's'},
        // 3D Layout - Snap & Align
        {"3D Layout\\Snap\\Mode", "0", 'i'},
        {"3D Layout\\Snap\\Translation Influence (3D Entities)", "8", 'i'},
        {"3D Layout\\Snap\\Translation Influence (Sprites)", "5", 'i'},
        {"3D Layout\\Snap\\Rotation Angle", "10", 'f'},
        {"3D Layout\\Snap\\Scaling Percentage", "10", 'f'},
        // Schematic (the rest of the page)
        {"Schematic\\Horizontal Scrollbar", "1", 'b'},
        {"Schematic\\Vertical Scrollbar", "1", 'b'},
        {"Schematic\\Save Script Snapshot", "1", 'b'},
        {"Schematic\\Auto Resize Scripts", "0", 'b'},
        {"Schematic\\Move Item Coherence Test", "0", 'b'},
        // The Schematic Explorer's settings, kept when it closes (Dev.exe
        // sub_5B5A80): Show in List, Parse All Items, the tree view, the
        // Parameter Filter and Derived.
        {"Schematic Explorer\\Show In List", "1", 'i'},
        {"Schematic Explorer\\Parse All Items", "0", 'b'},
        {"Schematic Explorer\\Tree View", "0", 'b'},
        {"Schematic Explorer\\Parameter Filter", "0", 'i'},
        {"Schematic Explorer\\Derived", "0", 'b'},
        {"Schematic\\Test Parameter Link after Loading", "1", 'b'},
        {"Schematic\\Remove Invalid Link after Loading", "0", 'b'},
        {"Schematic\\Automatic Scroll", "1", 'b'},
        {"Schematic\\Autoscroll Delay", "50", 'i'},
        {"Schematic\\Edit Parameter Operation Mode", "1", 'i'}, // 1 Easy Finding, 0 Quick Finding
        // Miscellaneous Controls
        {"CK2 Settings\\Render Engine", "ck2_3d", 's'},
        {"CK2 Settings\\Sound Enabled", "1", 'b'},
        {"CK2 Settings\\Direct Input Enabled", "1", 'b'},
        {"CK2 Settings\\Quantization Sampling Factor", "10", 'i'},
        {"3D Layout\\Show Dynamic Objects", "1", 'b'},
        {"File Options\\Compressed", "1", 'b'},
        {"File Options\\Compression Level", "4", 'i'},
        {"File Options\\Sounds", "0", 'i'},   // CK_SOUND_SAVEOPTIONS
        {"File Options\\Textures", "0", 'i'}, // CK_TEXTURE_SAVEOPTIONS
        {"File Options\\Texture Format", "", 's'}, // bitmap reader extension (Use specific format)
        // Behavior Link Properties (Schematic toolbar)
        {"BehaviorManager\\MaxIteration", "8000", 'i'},
    };
    return settings;
}

std::map<std::string, std::string> Preferences::DefaultSettings() {
    std::map<std::string, std::string> values;
    for (const DevSetting &setting : DevSettings()) values[setting.key] = setting.fallback;
    return values;
}

std::string Preferences::Text(const std::string &key) const {
    auto found = settings.find(key);
    return found != settings.end() ? found->second : std::string();
}

bool Preferences::Bool(const std::string &key) const {
    return std::atoi(Text(key).c_str()) != 0;
}

int Preferences::Int(const std::string &key) const {
    return std::atoi(Text(key).c_str());
}

float Preferences::Float(const std::string &key) const {
    return static_cast<float>(std::atof(Text(key).c_str()));
}

SceneEnvironment Preferences::NewSceneEnvironment() const {
    const std::string prefix = "3D Layout\\Environment\\";
    SceneEnvironment environment;
    environment.ambient = static_cast<CKDWORD>(std::strtoul(Text(prefix + "Ambient Light Color").c_str(), nullptr, 10));
    environment.background = static_cast<CKDWORD>(std::strtoul(Text(prefix + "Background Color").c_str(), nullptr, 10));
    environment.fogColor = static_cast<CKDWORD>(std::strtoul(Text(prefix + "Fog Color").c_str(), nullptr, 10));
    environment.fogMode = Int(prefix + "Fog Mode");
    environment.fogStart = Float(prefix + "Fog Start");
    environment.fogEnd = Float(prefix + "Fog End");
    environment.fogDensity = Float(prefix + "Fog Density");
    return environment;
}

void Preferences::SetFloat(const std::string &key, float value) {
    char text[64];
    std::snprintf(text, sizeof(text), "%g", value);
    settings[key] = text;
}

void Preferences::ResetSettings(const std::string &prefix) {
    for (const DevSetting &setting : DevSettings())
        if (std::strncmp(setting.key, prefix.c_str(), prefix.size()) == 0) settings[setting.key] = setting.fallback;
}

int Preferences::ImportDevIni(const std::string &runtimeDirectory) {
    std::ifstream in(std::filesystem::u8path(runtimeDirectory) / "dev.ini");
    if (!in) return 0;
    // "<Section>" lines, then "name = value" lines.
    std::string section, line;
    int found = 0;
    while (std::getline(in, line)) {
        const size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos) continue;
        std::string text = line.substr(a, line.find_last_not_of(" \t\r") - a + 1);
        if (text[0] == '<') {
            section = text.substr(1, text.find('>') - 1);
            continue;
        }
        const size_t equal = text.find('=');
        if (equal == std::string::npos) continue;
        std::string name = text.substr(0, text.find_last_not_of(" \t", equal - 1) + 1);
        std::string value = text.substr(text.find_first_not_of(" \t", equal + 1) == std::string::npos
                                            ? text.size()
                                            : text.find_first_not_of(" \t", equal + 1));
        if (section == "File Options") {
            if (name == "Compression") {
                SetBool("File Options\\Compressed", value == "Compressed");
                ++found;
            } else if (name == "Compression Level") {
                SetInt("File Options\\Compression Level", std::atoi(value.c_str()));
                ++found;
            } else if (name == "Sounds") {
                SetInt("File Options\\Sounds", value == "References Original Files" ? 0 : 1);
                ++found;
            } else if (name == "Textures,Sprites") {
                const int option = value == "Raw Data" ? 0 : value == "External Files" ? 1 : value == "Image Format" ? 2 : 4;
                SetInt("File Options\\Textures", option);
                ++found;
            }
        } else if (section == "BehaviorManager" && name == "MaxIteration") {
            SetInt("BehaviorManager\\MaxIteration", std::atoi(value.c_str()));
            ++found;
        }
    }
    return found;
}

bool Preferences::ImportDevPaths() {
#ifdef _WIN32
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Virtools\\Data Paths", 0, KEY_READ, &root) != ERROR_SUCCESS)
        return false;
    paths.clear();
    wchar_t name[256];
    for (DWORD index = 0;; ++index) {
        DWORD length = sizeof(name) / sizeof(*name);
        if (RegEnumKeyExW(root, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, name, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
        DWORD count = 0, size = sizeof(count), type = 0;
        RegQueryValueExW(key, L"path count", nullptr, &type, reinterpret_cast<BYTE *>(&count), &size);
        std::vector<std::string> list;
        for (DWORD i = 0; i < count; ++i) {
            wchar_t value[32768] = L"";
            DWORD valueSize = sizeof(value) - sizeof(*value);
            if (RegQueryValueExW(key, std::to_wstring(i).c_str(), nullptr, &type, reinterpret_cast<BYTE *>(value),
                                 &valueSize) == ERROR_SUCCESS &&
                type == REG_SZ && value[0])
                list.push_back(WideToUtf8(value));
        }
        RegCloseKey(key);
        paths.emplace_back(WideToAnsi(name), list);
    }
    RegCloseKey(root);
    return !paths.empty();
#else
    return false;
#endif
}

bool Preferences::Load(const std::string &path) {
    std::ifstream in(std::filesystem::u8path(path));
    if (!in) return false;
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(in, line)) {
        const size_t equal = line.find('=');
        if (equal == std::string::npos || line.empty() || line[0] == '#') continue;
        auto trim = [](std::string text) {
            const size_t a = text.find_first_not_of(" \t\r");
            const size_t b = text.find_last_not_of(" \t\r");
            return a == std::string::npos ? std::string() : text.substr(a, b - a + 1);
        };
        values[trim(line.substr(0, equal))] = trim(line.substr(equal + 1));
    }
    for (const Field &field : Fields()) {
        auto found = values.find(field.key);
        if (found != values.end()) Assign(*this, field, std::strtoul(found->second.c_str(), nullptr, 0));
    }
    recentFiles.clear();
    for (int i = 1; i <= MAX_RECENT_FILES; ++i) {
        auto found = values.find("Recent File " + std::to_string(i));
        if (found != values.end() && !found->second.empty()) recentFiles.push_back(found->second);
    }
    settingsSaved = false;
    for (const DevSetting &setting : DevSettings()) {
        auto found = values.find(setting.key);
        if (found == values.end()) continue;
        settings[setting.key] = found->second;
        settingsSaved = true;
    }
    // "Path Category N = name", "Path N M = path".
    paths.clear();
    pathsSaved = values.count("Path Categories") != 0;
    const int categories = std::atoi(values["Path Categories"].c_str());
    for (int c = 0; c < categories; ++c) {
        std::pair<std::string, std::vector<std::string>> category;
        category.first = values["Path Category " + std::to_string(c)];
        for (int i = 0;; ++i) {
            auto found = values.find("Path " + std::to_string(c) + " " + std::to_string(i));
            if (found == values.end()) break;
            category.second.push_back(found->second);
        }
        paths.push_back(category);
    }
    dataResources.clear();
    for (int i = 1;; ++i) {
        auto found = values.find("Data Resource " + std::to_string(i));
        if (found == values.end()) break;
        if (!found->second.empty()) dataResources.push_back(found->second);
    }
    return true;
}

void Preferences::AddRecentFile(const std::string &path) {
    if (path.empty()) return;
    recentFiles.erase(std::remove_if(recentFiles.begin(), recentFiles.end(),
                                     [&](const std::string &other) { return _stricmp(other.c_str(), path.c_str()) == 0; }),
                      recentFiles.end());
    recentFiles.insert(recentFiles.begin(), path);
    if (recentFiles.size() > MAX_RECENT_FILES) recentFiles.resize(MAX_RECENT_FILES);
}

bool Preferences::Save(const std::string &path) const {
    std::ofstream out(std::filesystem::u8path(path));
    if (!out) return false;
    out << "# Virtools Dev preferences (names as Dev 2.5 stores them)\n";
    Preferences copy = *this;
    for (const Field &field : Fields()) {
        out << field.key << " = ";
        if (field.integer) out << field.integer(copy);
        if (field.boolean) out << (field.boolean(copy) ? 1 : 0);
        if (field.color) {
            char text[16];
            std::snprintf(text, sizeof(text), "0x%06X", static_cast<unsigned>(field.color(copy)));
            out << text;
        }
        out << '\n';
    }
    for (size_t i = 0; i < recentFiles.size(); ++i) out << "Recent File " << i + 1 << " = " << recentFiles[i] << '\n';
    for (const DevSetting &setting : DevSettings()) out << setting.key << " = " << Text(setting.key) << '\n';
    if (pathsSaved) {
        out << "Path Categories = " << paths.size() << '\n';
        for (size_t c = 0; c < paths.size(); ++c) {
            out << "Path Category " << c << " = " << paths[c].first << '\n';
            for (size_t i = 0; i < paths[c].second.size(); ++i)
                out << "Path " << c << " " << i << " = " << paths[c].second[i] << '\n';
        }
    }
    for (size_t i = 0; i < dataResources.size(); ++i) out << "Data Resource " << i + 1 << " = " << dataResources[i] << '\n';
    return static_cast<bool>(out);
}

int Preferences::ImportFromDev() {
#ifdef _WIN32
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Virtools\\Schematic", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return 0;
    int found = 0;
    for (const Field &field : Fields()) {
        DWORD value = 0, size = sizeof(value), type = 0;
        if (RegQueryValueExA(key, field.key, nullptr, &type, reinterpret_cast<BYTE *>(&value), &size) ==
                ERROR_SUCCESS &&
            type == REG_DWORD) {
            Assign(*this, field, value);
            ++found;
        }
    }
    RegCloseKey(key);
    return found + ImportDevSettings();
#else
    return 0;
#endif
}

int Preferences::ImportDevSettings() {
#ifdef _WIN32
    int found = 0;
    // The other pages: DWORDs for b/i, strings for f/s.
    for (const DevSetting &setting : DevSettings()) {
        const auto split = SplitKey(setting.key);
        if (split.first == "File Options" || split.first == "BehaviorManager") continue; // dev.ini
        HKEY subkey = nullptr;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, ("Software\\Virtools\\" + split.first).c_str(), 0, KEY_READ, &subkey) !=
            ERROR_SUCCESS)
            continue;
        char data[512] = "";
        DWORD size = sizeof(data) - 1, type = 0;
        if (RegQueryValueExA(subkey, split.second.c_str(), nullptr, &type, reinterpret_cast<BYTE *>(data), &size) ==
            ERROR_SUCCESS) {
            if (type == REG_DWORD && (setting.kind == 'b' || setting.kind == 'i')) {
                DWORD value = 0;
                std::memcpy(&value, data, sizeof(value));
                settings[setting.key] = std::to_string(setting.kind == 'b' ? (value != 0) : static_cast<int>(value));
                ++found;
            } else if (type == REG_SZ && (setting.kind == 'f' || setting.kind == 's')) {
                settings[setting.key] = data;
                ++found;
            }
        }
        RegCloseKey(subkey);
    }
    return found;
#else
    return 0;
#endif
}

unsigned int ColorRefToColor(CKDWORD color, int alpha) {
    return IM_COL32(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, alpha);
}

void ApplyColors(const Preferences &preferences, SchematicStyle &style) {
    style.behaviorLink = ColorRefToColor(preferences.ioLinkColor);
    style.parameterLink = ColorRefToColor(preferences.parameterLinkColor);
    style.highlightLink = ColorRefToColor(preferences.exportedLinkColor);
    style.operationLink = ColorRefToColor(preferences.operationLinkColor);
    style.validLink = ColorRefToColor(preferences.validLinkColor);
    style.invalidLink = ColorRefToColor(preferences.invalidLinkColor);
    style.obsoleteBorder = ColorRefToColor(preferences.obsoleteColor);
    style.noPrototypeBorder = ColorRefToColor(preferences.noPrototypeColor);
    style.missingBorder = ColorRefToColor(preferences.noObjectColor);
    style.toReplaceBorder = ColorRefToColor(preferences.toReplaceColor);
}

} // namespace vdev

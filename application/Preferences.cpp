#include "application/Preferences.h"
#include "application/EditorApplication.h"
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

#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <cctype>

namespace vdev {

namespace {

constexpr size_t MAX_RECENT_FILES = 8;

struct Field {
    const char *key; // also Dev's registry value name
    std::function<int &(Preferences &)> integer;
    std::function<bool &(Preferences &)> boolean;
    std::function<uint32_t &(Preferences &)> color;
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
        {"Io Link Color", nullptr, nullptr, [](P &p) -> uint32_t & { return p.ioLinkColor; }},
        {"Parameter Link Color", nullptr, nullptr, [](P &p) -> uint32_t & { return p.parameterLinkColor; }},
        {"Exported Parameter Link Color", nullptr, nullptr, [](P &p) -> uint32_t & { return p.exportedLinkColor; }},
        {"Parameter Operation Link Color", nullptr, nullptr, [](P &p) -> uint32_t & { return p.operationLinkColor; }},
        {"Valid Link Color", nullptr, nullptr, [](P &p) -> uint32_t & { return p.validLinkColor; }},
        {"Invalid Link Color", nullptr, nullptr, [](P &p) -> uint32_t & { return p.invalidLinkColor; }},
        {"Behavior Obsolete Color", nullptr, nullptr, [](P &p) -> uint32_t & { return p.obsoleteColor; }},
        {"Behavior No Prototype Color", nullptr, nullptr, [](P &p) -> uint32_t & { return p.noPrototypeColor; }},
        {"Behavior No CKObject Color", nullptr, nullptr, [](P &p) -> uint32_t & { return p.noObjectColor; }},
        {"Building Block to Replace Color", nullptr, nullptr, [](P &p) -> uint32_t & { return p.toReplaceColor; }},
    };
    return fields;
}

void Assign(Preferences &preferences, const Field &field, unsigned long value) {
    if (field.integer) field.integer(preferences) = static_cast<int>(value);
    if (field.boolean) field.boolean(preferences) = value != 0;
    if (field.color) field.color(preferences) = static_cast<uint32_t>(value & 0xFFFFFF);
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

void Preferences::SetFloat(const std::string &key, float value) {
    char text[64];
    std::snprintf(text, sizeof(text), "%g", value);
    settings[key] = text;
}

void Preferences::ResetSettings(const std::string &prefix) {
    for (const DevSetting &setting : DevSettings())
        if (std::strncmp(setting.key, prefix.c_str(), prefix.size()) == 0) settings[setting.key] = setting.fallback;
}

namespace {
bool GeneralField(const std::string &key) {
    static const std::set<std::string> names = {"Automatic Link", "Strict MultiSelection", "Search Best Path Link",
        "Max Recent Building Block Count", "Enable Tooltips", "Tooltips Delay(ms)", "Io Link Color", "Parameter Link Color",
        "Exported Parameter Link Color", "Parameter Operation Link Color", "Valid Link Color", "Invalid Link Color",
        "Behavior Obsolete Color", "Behavior No Prototype Color", "Behavior No CKObject Color", "Building Block to Replace Color"};
    return names.count(key) != 0;
}
bool GeneralSetting(const std::string &key) {
    return key.rfind("Schematic Explorer\\", 0) != 0 && key.rfind("BehaviorManager\\", 0) != 0;
}
bool ValidText(const std::string &text) {
    return text.size() <= 4096 && text.find_first_of(std::string("\0\r\n", 3)) == std::string::npos;
}
bool ValidNumbers(const std::string &key, const std::string &text) {
    size_t count = 0;
    if (key == "3D Layout\\World Size Min" || key == "3D Layout\\World Size Max" || key == "3D Layout\\World Step" ||
        key.rfind("3D Layout\\Reference Grid\\Orientation ", 0) == 0) count = 3;
    if (key == "3D Layout\\Screen Offset" || key == "3D Layout\\Screen Step" || key == "3D Layout\\Environment\\Screen Size" ||
        key == "3D Layout\\Reference Grid\\Sections" || key == "3D Layout\\Reference Grid\\Step") count = 2;
    if (!count) return true;
    std::string item;
    std::istringstream input(text);
    const char separator = key == "3D Layout\\Reference Grid\\Sections" || key == "3D Layout\\Reference Grid\\Step" ? ';' : '|';
    std::vector<double> values;
    while (std::getline(input, item, separator)) {
        char *end = nullptr;
        const double value = std::strtod(item.c_str(), &end);
        if (end == item.c_str() || *end || !std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return false;
        values.push_back(value);
    }
    if (values.size() != count || (!text.empty() && text.back() == separator)) return false;
    if (key == "3D Layout\\Environment\\Screen Size") return (values[0] == 0 && values[1] == 0) || (values[0] > 0 && values[1] > 0);
    return true;
}
bool ValidInteger(const std::string &key, int64_t value) {
    if (key == "Schematic\\Max Recent Building Block Count") return value >= 1 && value <= 100;
    if (key == "Schematic\\Tooltips Delay(ms)") return value >= 0 && value <= std::numeric_limits<int>::max();
    if (key == "File Options\\Compression Level") return value >= 1 && value <= 9;
    if (key == "File Options\\Sounds") return value == 0 || value == 1;
    if (key == "File Options\\Textures") return value == 0 || value == 1 || value == 2 || value == 4;
    if (key == "CK2 Settings\\Quantization Sampling Factor") return value >= 1 && value <= 100;
    if (key == "3D Layout\\Author Mode\\Render Type Index" || key == "3D Layout\\Player Mode\\Render Type Index") return value >= 0 && value <= 3;
    if (key == "Schematic\\Edit Parameter Operation Mode") return value == 0 || value == 1;
    const bool color = key.size() >= 5 && key.compare(key.size() - 5, 5, "Color") == 0;
    return value >= std::numeric_limits<int32_t>::min() && value <= (color ? int64_t(UINT32_MAX) : int64_t(INT32_MAX));
}
} // namespace

const std::vector<PreferencePage> &Preferences::Pages() {
    static const std::vector<PreferencePage> pages{{"rendering", "3D Layout - Rendering"}, {"actions", "3D Layout - Actions"},
        {"interface", "3D Layout - Interface"}, {"reference-guide", "3D Layout - Reference Guide"},
        {"snap-align", "3D Layout - Snap & Align"}, {"schematic", "Schematic"}, {"miscellaneous", "Miscellaneous Controls"}};
    return pages;
}
bool PreferenceNativeText(const std::string &key) {
    return key == "3D Layout\\Author Mode\\Device Driver" || key == "3D Layout\\Player Mode\\FullScren Driver" ||
        key == "CK2 Settings\\Render Engine";
}
std::vector<EditorField> Preferences::Values(bool generalOnly) const {
    Preferences copy = *this;
    std::vector<EditorField> result;
    for (const auto &field : Fields()) if (!generalOnly || GeneralField(field.key)) {
        const auto value = field.boolean ? uint64_t(field.boolean(copy)) : field.integer ? uint64_t(int64_t(field.integer(copy))) : uint64_t(field.color(copy));
        result.push_back({std::string("Schematic\\") + field.key, EDITOR_INTEGER_VALUE, {}, {}, value});
    }
    for (const auto &field : DevSettings()) if (!generalOnly || GeneralSetting(field.key)) {
        EditorField value{field.key, field.kind == 's' ? EDITOR_TEXT_VALUE : field.kind == 'f' ? EDITOR_REAL_VALUE : EDITOR_INTEGER_VALUE};
        if (field.kind == 's') value.text = Text(field.key);
        else if (field.kind == 'f') value.real = Float(field.key);
        else if (field.kind == 'b') value.integer = Bool(field.key);
        else value.integer = uint64_t(std::strtoll(Text(field.key).c_str(), nullptr, 10));
        result.push_back(std::move(value));
    }
    return result;
}
bool Preferences::SetValues(const std::vector<EditorField> &values, std::string &error) {
    return DecodeValues(values, error, true);
}
bool Preferences::ReadValues(const std::vector<EditorField> &values, std::string &error) {
    return DecodeValues(values, error, false);
}
bool Preferences::RestoreValues(const std::vector<EditorField> &values, std::string &error) {
    return DecodeValues(values, error, false, false);
}
bool Preferences::DecodeValues(const std::vector<EditorField> &values, std::string &error, bool validate, bool generalOnly) {
    Preferences next = *this;
    const auto current = Values(generalOnly);
    std::set<std::string> keys;
    for (const auto &value : values) {
        const auto found = std::find_if(current.begin(), current.end(), [&](const EditorField &field) { return field.key == value.key; });
        if (found == current.end() || found->type != value.type || !keys.insert(value.key).second) { error = "Unknown or duplicate preference: " + value.key; return false; }
        if (value == *found) continue;
        bool assigned = false;
        for (const auto &field : Fields()) if ((!generalOnly || GeneralField(field.key)) && value.key == std::string("Schematic\\") + field.key) {
            if (validate && ((field.boolean && value.integer > 1) || (field.color && value.integer > 0xFFFFFF) ||
                (field.integer && !ValidInteger(value.key, int64_t(value.integer))))) { error = "Invalid preference: " + value.key; return false; }
            Assign(next, field, static_cast<unsigned long>(value.integer)); assigned = true; break;
        }
        if (assigned) continue;
        const auto setting = std::find_if(DevSettings().begin(), DevSettings().end(), [&](const DevSetting &field) { return value.key == field.key; });
        if (setting == DevSettings().end()) { error = "Unknown preference: " + value.key; return false; }
        if (value.type == EDITOR_TEXT_VALUE) {
            if (validate && (!ValidText(value.text) || !ValidNumbers(value.key, value.text))) { error = "Invalid preference: " + value.key; return false; }
            next.Set(value.key, value.text);
        } else if (value.type == EDITOR_REAL_VALUE) {
            if (!std::isfinite(value.real) || std::abs(value.real) > std::numeric_limits<float>::max()) { error = "Invalid preference: " + value.key; return false; }
            next.SetFloat(value.key, float(value.real));
        } else {
            if (validate && ((setting->kind == 'b' && value.integer > 1) || (setting->kind == 'i' && !ValidInteger(value.key, int64_t(value.integer))))) {
                error = "Invalid preference: " + value.key; return false;
            }
            next.Set(value.key, std::to_string(int64_t(value.integer)));
        }
    }
    *this = std::move(next); return true;
}
void Preferences::ResetPage(int page, const std::string &firstEngine) {
    static const char *const prefixes[][12] = {
        {"3D Layout\\Author Mode\\", "3D Layout\\Player Mode\\", "3D Layout\\Environment\\Ignore", "3D Layout\\Environment\\Screen", "3D Layout\\Author RealTime", "3D Layout\\Player RealTime"},
        {"3D Layout\\Actions\\Object", "3D Layout\\Actions\\Camera Rotate", "3D Layout\\Actions\\Camera Translate", "3D Layout\\Actions\\Camera Zoom", "3D Layout\\Actions\\Camera Ortho", "3D Layout\\Actions\\Camera Roll", "3D Layout\\Actions\\Camera Fov", "3D Layout\\Actions\\Camera Fly", "3D Layout\\Actions\\Camera Navigate", "3D Layout\\Actions\\Free", "3D Layout\\Actions\\Orbit"},
        {"3D Layout\\Interface Objects\\", "3D Layout\\Auto Show", "3D Layout\\Show New Setups", "3D Layout\\Debug Mode", "3D Layout\\World", "3D Layout\\Screen", "3D Layout\\Homogenous", "3D Layout\\Actions\\Camera Plane"},
        {"3D Layout\\Reference Grid\\"}, {"3D Layout\\Snap\\"}, {"Schematic\\"}, {"CK2 Settings\\", "File Options\\", "3D Layout\\Show Dynamic Objects"}};
    if (page < 0 || size_t(page) >= Pages().size()) return;
    for (auto prefix : prefixes[page]) if (prefix) ResetSettings(prefix);
    if (page == 5) {
        Preferences defaults;
        for (const auto &field : Fields()) if (GeneralField(field.key)) {
            if (field.boolean) field.boolean(*this) = field.boolean(defaults);
            if (field.integer) field.integer(*this) = field.integer(defaults);
            if (field.color) field.color(*this) = field.color(defaults);
        }
    }
    if (page == 6 && !firstEngine.empty()) Set("CK2 Settings\\Render Engine", firstEngine);
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
                                     [&](const std::string &other) { return other.size() == path.size() && std::equal(other.begin(), other.end(), path.begin(),
                                         [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }); }),
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

} // namespace vdev

// Dev's General Preferences (Options > General Preferences, Ctrl+P): the
// "Current Prefs" pages laid out as the Dev 2.5 screenshots, with Dev's
// labels ([Preferences] of the language pack) and Dev's registry value
// names (Preferences::settings). Edited on a copy; OK keeps it,
// Cancel drops it, Default restores the page's Dev defaults.

#include "ui/tools/ToolWindows.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>

#include "imgui.h"

#include "CKAll.h"

#include "ui/controls/DevTheme.h"
#include "ui/controls/TypeAhead.h"
#include "core/layout/LayoutView.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

const char *const ToolWindows::PREFERENCE_PAGES[] = {
    "3D Layout - Rendering",       "3D Layout - Actions", "3D Layout - Interface", "3D Layout - Reference Guide",
    "3D Layout - Snap & Align",    "Schematic",           "Miscellaneous Controls"};
const int ToolWindows::PREFERENCE_PAGE_COUNT = 7;

namespace {

// The registry key prefixes a page's Default button restores.
const char *const PAGE_PREFIXES[][12] = {
    {"3D Layout\\Author Mode\\", "3D Layout\\Player Mode\\", "3D Layout\\Environment\\Ignore",
     "3D Layout\\Environment\\Screen", "3D Layout\\Author RealTime", "3D Layout\\Player RealTime"},
    {"3D Layout\\Actions\\Object", "3D Layout\\Actions\\Camera Rotate", "3D Layout\\Actions\\Camera Translate",
     "3D Layout\\Actions\\Camera Zoom", "3D Layout\\Actions\\Camera Ortho", "3D Layout\\Actions\\Camera Roll",
     "3D Layout\\Actions\\Camera Fov", "3D Layout\\Actions\\Camera Fly", "3D Layout\\Actions\\Camera Navigate",
     "3D Layout\\Actions\\Free", "3D Layout\\Actions\\Orbit"},
    {"3D Layout\\Interface Objects\\", "3D Layout\\Auto Show", "3D Layout\\Show New Setups", "3D Layout\\Debug Mode",
     "3D Layout\\World", "3D Layout\\Screen", "3D Layout\\Homogenous", "3D Layout\\Actions\\Camera Plane"},
    {"3D Layout\\Reference Grid\\", nullptr},
    {"3D Layout\\Snap\\", nullptr},
    {"Schematic\\", nullptr},
    {"CK2 Settings\\", "File Options\\", "3D Layout\\Show Dynamic Objects", nullptr},
};

// The page sizes of the screenshots.
const ImVec2 PAGE_SIZES[] = {{1150, 560}, {742, 830}, {1115, 770}, {665, 590}, {607, 520}, {883, 560}, {985, 560}};

void Bold(const char *text) {
    if (ImFont *bold = BoldFont()) ImGui::PushFont(bold);
    ImGui::TextUnformatted(text);
    if (BoldFont()) ImGui::PopFont();
}

// A bold title above a bordered box (Dev's group boxes).
void BeginBox(const char *title, ImVec2 size) {
    // A group: a box beside another keeps its title above it.
    ImGui::BeginGroup();
    if (title) Bold(title);
    if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild(title ? title : "box", size, ImGuiChildFlags_Borders | (size.y <= 0 ? ImGuiChildFlags_AutoResizeY : 0));
}

// Dev's dialog widths are for its 13 pixel font.
ImVec2 PageSize(int page) {
    const float scale = ImGui::GetFontSize() / 13.0f;
    return ImVec2(PAGE_SIZES[page].x * scale, PAGE_SIZES[page].y * scale);
}

void EndBox() {
    ImGui::EndChild();
    ImGui::EndGroup();
}

void Centered(const char *text, bool bold = true) {
    const float width = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetContentRegionAvail().x - width) * 0.5f));
    if (bold) Bold(text);
    else ImGui::TextUnformatted(text);
}

bool Check(Preferences &p, const char *key, const char *label) {
    bool value = p.Bool(key);
    if (!ImGui::Checkbox(label, &value)) return false;
    p.SetBool(key, value);
    return true;
}

// A numeric text field (Dev's underlined edit boxes).
void FloatField(Preferences &p, const char *key, const char *id, float width = 90, const char *format = "%g") {
    float value = p.Float(key);
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputFloat(id, &value, 0, 0, format)) p.SetFloat(key, value);
}

void IntField(Preferences &p, const char *key, const char *id, float width = 90) {
    int value = p.Int(key);
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputInt(id, &value, 0, 0)) p.SetInt(key, value);
}

// "a|b|c" or "a;b" lists of numbers (Dev stores vectors as text).
std::vector<float> Numbers(const std::string &text) {
    std::vector<float> values;
    std::string item;
    for (char c : text + "|") {
        if (c == '|' || c == ';') {
            values.push_back(static_cast<float>(std::atof(item.c_str())));
            item.clear();
        } else {
            item += c;
        }
    }
    return values;
}

std::string Join(const std::vector<float> &values, char separator) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << separator;
        out << values[i];
    }
    return out.str();
}

// One component of a vector setting as a field.
void ComponentField(Preferences &p, const char *key, size_t index, const char *id, char separator,
                    float width = 120, const char *format = "%g") {
    std::vector<float> values = Numbers(p.Text(key));
    if (values.size() <= index) values.resize(index + 1, 0.0f);
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputFloat(id, &values[index], 0, 0, format)) p.Set(key, Join(values, separator));
}

// "<value> 1 ---x--- 100": Dev's value box, range and slider.
void SliderRow(float &value, float low, float high, const char *id, float x) {
    ImGui::SameLine(x);
    ImGui::SetNextItemWidth(60);
    ImGui::InputFloat((std::string(id) + "v").c_str(), &value, 0, 0, "%g");
    ImGui::SameLine();
    ImGui::Text("%g", low);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::SliderFloat((std::string(id) + "s").c_str(), &value, low, high, "");
    ImGui::SameLine();
    ImGui::Text("%g", high);
    value = std::max(low, std::min(high, value));
}

void SettingSlider(Preferences &p, const char *key, float low, float high, const char *id, float x) {
    float value = p.Float(key);
    const float before = value;
    SliderRow(value, low, high, id, x);
    if (value != before) p.SetFloat(key, std::floor(value + 0.5f));
}

void ComponentSlider(Preferences &p, const char *key, size_t index, char separator, float low, float high,
                     const char *id, float x) {
    std::vector<float> values = Numbers(p.Text(key));
    if (values.size() <= index) values.resize(index + 1, low);
    const float before = values[index];
    SliderRow(values[index], low, high, id, x);
    if (values[index] != before) {
        values[index] = std::floor(values[index] + 0.5f);
        p.Set(key, Join(values, separator));
    }
}

bool Combo(const char *id, int &index, const std::vector<std::string> &items, float width) {
    ImGui::SetNextItemWidth(width);
    const char *preview = index >= 0 && index < static_cast<int>(items.size()) ? items[index].c_str() : "";
    bool changed = false;
    if (ImGui::BeginCombo(id, preview)) {
        TypeAhead typeAhead;
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (typeAhead.Selectable(items[i].c_str(), i == index)) {
                index = i;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

// The render drivers of the render manager (none when no render engine runs).
std::vector<std::string> RenderDrivers(CKContext *context, const std::string &stored) {
    std::vector<std::string> drivers;
    CKRenderManager *render = context ? context->GetRenderManager() : nullptr;
    for (int i = 0; render && i < render->GetRenderDriverCount(); ++i)
        if (VxDriverDesc *desc = render->GetRenderDriverDescription(i))
            drivers.push_back(AnsiToUtf8(DriverName(desc)));
    const std::string current = AnsiToUtf8(stored);
    if (!current.empty() && std::find(drivers.begin(), drivers.end(), current) == drivers.end()) drivers.push_back(current);
    return drivers;
}

void DriverCombo(Preferences &p, CKContext *context, const char *key, const char *id, float width) {
    const std::vector<std::string> drivers = RenderDrivers(context, p.Text(key));
    int index = static_cast<int>(std::find(drivers.begin(), drivers.end(), AnsiToUtf8(p.Text(key))) - drivers.begin());
    if (Combo(id, index, drivers, width)) p.Set(key, Utf8ToAnsi(drivers[index]));
}

// "640x480x32x59" <-> "640x480x32 @ 59Hz".
std::string ModeLabel(const std::string &mode) {
    int w = 0, h = 0, bpp = 0, hz = 0;
    if (std::sscanf(mode.c_str(), "%dx%dx%dx%d", &w, &h, &bpp, &hz) == 4)
        return std::to_string(w) + "x" + std::to_string(h) + "x" + std::to_string(bpp) + " @ " + std::to_string(hz) + "Hz";
    return mode;
}

} // namespace

void ToolWindows::OpenGeneralPreferences(int page) {
    m_Edit = m_State.preferences;
    if (page >= 0 && page < PREFERENCE_PAGE_COUNT) m_Page = page;
    m_ShowPreferences = true;
    m_FocusPreferences = true;
}

void ToolWindows::GeneralPreferences() {
    if (m_FocusPreferences && m_OpenPopups) {
        ImGui::OpenPopup("General Preferences");
        m_CenterPreferences = 3;
        m_FocusPreferences = false;
    }
    // The dialog fits its page; centered while it takes its size.
    if (m_CenterPreferences > 0) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        --m_CenterPreferences;
    }
    if (!ImGui::BeginPopupModal("General Preferences", nullptr,
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        m_ShowPreferences = false;
        return;
    }
    // "Current Prefs" at the top right.
    const float comboWidth = 270;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - comboWidth - ImGui::CalcTextSize("Current Prefs").x - 20);
    Bold("Current Prefs");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo("##page", PREFERENCE_PAGES[m_Page])) {
        TypeAhead typeAhead;
        for (int i = 0; i < PREFERENCE_PAGE_COUNT; ++i)
            if (typeAhead.Selectable(PREFERENCE_PAGES[i], i == m_Page) && i != m_Page) {
                m_Page = i;
                m_CenterPreferences = 3;
            }
        ImGui::EndCombo();
    }
    ImGui::BeginChild("page", ImVec2(PageSize(m_Page).x, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    Preferences &p = m_Edit;
    switch (m_Page) {
    case 0: RenderingPage(p); break;
    case 1: ActionsPage(p); break;
    case 2: InterfacePage(p); break;
    case 3: ReferenceGuidePage(p); break;
    case 4: SnapPage(p); break;
    case 5: SchematicPage(p); break;
    default: MiscPage(p); break;
    }
    ImGui::EndChild();
    if (ImGui::Button("Default", ImVec2(90, 0))) {
        // Dev's Default restores the page shown.
        for (const char *prefix : PAGE_PREFIXES[m_Page])
            if (prefix) p.ResetSettings(prefix);
        if (m_Page == 5) {
            // The Schematic page fields of Preferences.
            const Preferences defaults;
            p.automaticLink = defaults.automaticLink;
            p.strictMultiSelection = defaults.strictMultiSelection;
            p.searchBestPath = defaults.searchBestPath;
            p.maxRecentBlocks = defaults.maxRecentBlocks;
            p.tooltips = defaults.tooltips;
            p.tooltipDelayMs = defaults.tooltipDelayMs;
            p.ioLinkColor = defaults.ioLinkColor;
            p.parameterLinkColor = defaults.parameterLinkColor;
            p.exportedLinkColor = defaults.exportedLinkColor;
            p.operationLinkColor = defaults.operationLinkColor;
            p.validLinkColor = defaults.validLinkColor;
            p.invalidLinkColor = defaults.invalidLinkColor;
            p.obsoleteColor = defaults.obsoleteColor;
            p.noPrototypeColor = defaults.noPrototypeColor;
            p.noObjectColor = defaults.noObjectColor;
            p.toReplaceColor = defaults.toReplaceColor;
        }
        if (m_Page == 6) {
            // Dev.exe sub_5AF380: the first render engine.
            CKPluginManager *plugins = CKGetPluginManager();
            CKPluginEntry *engine = plugins ? plugins->GetPluginInfo(CKPLUGIN_RENDERENGINE_DLL, 0) : nullptr;
            CKPluginDll *dll = engine ? plugins->GetPluginDllInfo(engine->m_PluginDllIndex) : nullptr;
            if (dll && dll->m_DllFileName.CStr()) {
                std::string name = std::filesystem::path(dll->m_DllFileName.CStr()).stem().string();
                for (char &c : name) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                p.Set("CK2 Settings\\Render Engine", name);
            }
        }
    }
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 2 * 90 - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("OK", ImVec2(90, 0))) {
        p.maxRecentBlocks = std::max(1, std::min(100, p.maxRecentBlocks));
        p.tooltipDelayMs = std::max(0, p.tooltipDelayMs);
        m_State.preferences = p;
        if (applyPreferences) applyPreferences();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ------------------------------------------------------- 3D Layout - Rendering

void ToolWindows::RenderingPage(Preferences &p) {
    CKContext *context = m_State.host.Context();
    static const std::vector<std::string> renderTypes = {"Wireframe", "Flat", "Gouraud", "Phong"};
    const float half = ImGui::GetContentRegionAvail().x * 0.5f - 6;
    ImGui::BeginGroup();
    BeginBox("Author Mode", ImVec2(half, 0));
    ImGui::TextUnformatted("Render Device");
    ImGui::SameLine(200);
    DriverCombo(p, context, "3D Layout\\Author Mode\\Device Driver", "##authordevice", half - 215);
    ImGui::TextUnformatted("Render Type");
    ImGui::SameLine(200);
    int type = p.Int("3D Layout\\Author Mode\\Render Type Index");
    if (Combo("##authortype", type, renderTypes, half - 215)) p.SetInt("3D Layout\\Author Mode\\Render Type Index", type);
    ImGui::Spacing();
    const float column = 270;
    Check(p, "3D Layout\\Author Mode\\Render Type Wireframe", "Add Wireframe Layer");
    ImGui::SameLine(column);
    Check(p, "3D Layout\\Author Mode\\Render Type Textured", "Textured");
    Check(p, "3D Layout\\Author Mode\\Show Frames", "Show Frames");
    ImGui::SameLine(column);
    Check(p, "3D Layout\\Author Mode\\Show Lights", "Show Lights");
    Check(p, "3D Layout\\Author Mode\\Show Curves", "Show Curves");
    ImGui::SameLine(column);
    Check(p, "3D Layout\\Author Mode\\Show Cameras", "Show Cameras");
    Check(p, "3D Layout\\Author Mode\\Show WorldAxis", "Show World Axis");
    ImGui::SameLine(column);
    Check(p, "3D Layout\\Author Mode\\Show ObjectAxis", "Show Objects Axis");
    Check(p, "3D Layout\\Author RealTime Notifications", "Realtime Setup Updates");
    ImGui::SameLine(column);
    Check(p, "3D Layout\\Author Mode\\Show Portals", "Show Portals");
    EndBox();
    ImGui::Spacing();
    BeginBox("Author Level/Scene Settings", ImVec2(half, 0));
    Check(p, "3D Layout\\Environment\\Ignore Camera Aspect Ratio", "Ignore camera aspect ratio");
    ImGui::TextUnformatted("Screen size");
    ImGui::SameLine(200);
    // "0|0": the window extents; presets of Dev's list (UI reference); else custom.
    static const std::vector<std::string> sizes = {"Window extents", "320x200", "320x256", "512x384",
                                                   "640x400", "640x480", "800x600", "Custom"};
    std::vector<float> size = Numbers(p.Text("3D Layout\\Environment\\Screen Size"));
    size.resize(2, 0.0f);
    int sizeIndex = size[0] == 0 && size[1] == 0 ? 0 : 7;
    for (int i = 1; i < 7; ++i)
        if (sizes[i] == std::to_string(static_cast<int>(size[0])) + "x" + std::to_string(static_cast<int>(size[1])))
            sizeIndex = i;
    if (Combo("##screensize", sizeIndex, sizes, 160)) {
        if (sizeIndex == 0) p.Set("3D Layout\\Environment\\Screen Size", "0|0");
        else if (sizeIndex < 7) {
            int w = 0, h = 0;
            std::sscanf(sizes[sizeIndex].c_str(), "%dx%d", &w, &h);
            p.Set("3D Layout\\Environment\\Screen Size", std::to_string(w) + "|" + std::to_string(h));
        } else if (size[0] == 0) {
            p.Set("3D Layout\\Environment\\Screen Size", "640|480");
        }
    }
    if (sizeIndex == 7) {
        ImGui::SameLine();
        ComponentField(p, "3D Layout\\Environment\\Screen Size", 0, "##sw", '|', 60, "%.0f");
        ImGui::SameLine();
        ComponentField(p, "3D Layout\\Environment\\Screen Size", 1, "##sh", '|', 60, "%.0f");
    }
    EndBox();
    ImGui::EndGroup();
    ImGui::SameLine();
    BeginBox("Player Mode", ImVec2(0, 0));
    ImGui::TextUnformatted("Fullscreen Render Device");
    ImGui::SameLine(220);
    DriverCombo(p, context, "3D Layout\\Player Mode\\FullScren Driver", "##playerdevice", -1);
    ImGui::TextUnformatted("Render Type");
    ImGui::SameLine(220);
    int playerType = p.Int("3D Layout\\Player Mode\\Render Type Index");
    if (Combo("##playertype", playerType, renderTypes, -1)) p.SetInt("3D Layout\\Player Mode\\Render Type Index", playerType);
    ImGui::TextUnformatted("Render Mode");
    ImGui::SameLine(220);
    {
        // The display modes of the fullscreen driver (the first one for a
        // name no driver has, as Player Mode's fullscreen takes).
        std::vector<std::string> modes, labels;
        CKRenderManager *render = context ? context->GetRenderManager() : nullptr;
        const int driver = LayoutFullScreenDriver(context, p.Text("3D Layout\\Player Mode\\FullScren Driver"));
        if (VxDriverDesc *desc = render && driver < render->GetRenderDriverCount()
                                     ? render->GetRenderDriverDescription(driver)
                                     : nullptr) {
            for (int m = 0; m < DisplayModeCount(desc); ++m) {
                const VxDisplayMode &mode = DisplayModeAt(desc, m);
                modes.push_back(std::to_string(mode.Width) + "x" + std::to_string(mode.Height) + "x" +
                                std::to_string(mode.Bpp) + "x" + std::to_string(mode.RefreshRate));
            }
        }
        const std::string current = p.Text("3D Layout\\Player Mode\\Render Mode");
        if (std::find(modes.begin(), modes.end(), current) == modes.end()) modes.insert(modes.begin(), current);
        for (const auto &mode : modes) labels.push_back(ModeLabel(mode));
        int index = static_cast<int>(std::find(modes.begin(), modes.end(), current) - modes.begin());
        if (Combo("##rendermode", index, labels, -1)) p.Set("3D Layout\\Player Mode\\Render Mode", modes[index]);
    }
    ImGui::Spacing();
    const float playerColumn = 320;
    Check(p, "3D Layout\\Player Mode\\Render Type Wireframe", "Add Wireframe Layer##player");
    ImGui::SameLine(playerColumn);
    Check(p, "3D Layout\\Player Mode\\Render Type Textured", "Textured##player");
    Check(p, "3D Layout\\Player Mode\\Show Frames", "Show All Frames");
    ImGui::SameLine(playerColumn);
    Check(p, "3D Layout\\Player Mode\\Show Lights", "Show All Lights");
    Check(p, "3D Layout\\Player Mode\\Show Curves", "Show All Curves");
    ImGui::SameLine(playerColumn);
    Check(p, "3D Layout\\Player Mode\\Show Cameras", "Show All Cameras");
    Check(p, "3D Layout\\Player Mode\\Show Grid", "Show Reference Guides");
    ImGui::SameLine(playerColumn);
    Check(p, "3D Layout\\Player Mode\\Show Portals", "Show All Portals");
    ImGui::Spacing();
    Check(p, "3D Layout\\Player Mode\\Auto FullScreen", "Automatic switching to FullScreen Mode");
    Check(p, "3D Layout\\Player Mode\\Auto Show Selection", "Automatic show selection");
    Check(p, "3D Layout\\Player RealTime Notifications", "Realtime setup updates every");
    ImGui::SameLine(330);
    IntField(p, "3D Layout\\Player RealTime Notifications FrameSkip", "##frameskip", 90);
    ImGui::SameLine();
    ImGui::TextUnformatted("frames");
    Check(p, "3D Layout\\Player Mode\\Update Cursor", "Update Cursor In Player Mode");
    EndBox();
}

// ---------------------------------------------------------- 3D Layout - Actions

void ToolWindows::ActionsPage(Preferences &p) {
    BeginBox("Actions", ImVec2(0, 0));
    Centered("Objects Manipulation");
    struct Row {
        const char *label, *key, *unit, *mode, *modeLabel;
    };
    auto rows = [&](const Row *list, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            const Row &row = list[i];
            ImGui::PushID(row.key);
            ImGui::TextUnformatted(row.label);
            ImGui::SameLine(250);
            FloatField(p, row.key, "##value", 90);
            ImGui::SameLine(355);
            ImGui::TextUnformatted(row.unit);
            if (row.mode) {
                ImGui::SameLine(490);
                Check(p, row.mode, row.modeLabel);
            }
            ImGui::PopID();
        }
    };
    static const Row objects[] = {
        {"Translation", "3D Layout\\Actions\\Object Translate Step", "Units", "3D Layout\\Actions\\Object Translate Sticky Mode", "Sticky Mode"},
        {"Rotation", "3D Layout\\Actions\\Object Rotate Step", "Degrees", nullptr, nullptr},
        {"Scaling", "3D Layout\\Actions\\Object Scale Step", "Local Units", nullptr, nullptr},
    };
    rows(objects, 3);
    ImGui::Spacing();
    Centered("Camera Navigation");
    static const Row cameras[] = {
        {"Rotation (Orbit)", "3D Layout\\Actions\\Camera Rotate Step", "Degrees", "3D Layout\\Actions\\Camera Rotate Constant Speed Mode", "Constant Speed"},
        {"Translation (Pan)", "3D Layout\\Actions\\Camera Translate Step", "World Units", "3D Layout\\Actions\\Camera Translate Constant Speed Mode", "Constant Speed"},
        {"Zoom (Dolly/Zoom)", "3D Layout\\Actions\\Camera Zoom Step", "World Units", "3D Layout\\Actions\\Camera Zoom Constant Speed Mode", "Constant Speed"},
        {"Ortho Zoom (Dolly/Zoom)", "3D Layout\\Actions\\Camera Orthographic Zoom Step", "World Units", "3D Layout\\Actions\\Camera OrthoZoom Constant Speed Mode", "Constant Speed"},
        {"Roll", "3D Layout\\Actions\\Camera Roll Step", "Degrees", "3D Layout\\Actions\\Camera Roll Constant Speed Mode", "Constant Speed"},
        {"Fov", "3D Layout\\Actions\\Camera Fov Step", "Degrees", "3D Layout\\Actions\\Camera Fov Constant Speed Mode", "Constant Speed"},
        {"Flycam Rotation", "3D Layout\\Actions\\Camera Fly Turn Step", "Degrees", nullptr, nullptr},
    };
    rows(cameras, 7);
    // Flycam Translation: the one Constant Speed of both flycam rows.
    static const Row fly[] = {{"Flycam Translation", "3D Layout\\Actions\\Camera Fly Advance Step", "World Units",
                               "3D Layout\\Actions\\Camera Navigate Constant Speed Mode", "Constant Speed##fly"}};
    rows(fly, 1);
    EndBox();
    ImGui::Spacing();
    BeginBox("Orbit", ImVec2(0, 0));
    ImGui::Indent(60);
    Bold("Free Cameras Target");
    ImGui::Unindent(60);
    static const char *const targets[] = {"N percent between frontplane and backplane", "N units plus the frontplane",
                                          "Fixed position"};
    const int orbit = p.Int("3D Layout\\Actions\\Free Camera Orbit Type");
    for (int i = 0; i < 3; ++i) {
        bool on = orbit == i;
        if (ImGui::Checkbox(targets[i], &on)) p.SetInt("3D Layout\\Actions\\Free Camera Orbit Type", i);
    }
    ImGui::Dummy(ImVec2(140, 0));
    SettingSlider(p, "3D Layout\\Actions\\Free Camera Orbit Value", 1, 100, "##orbit", 150);
    ImGui::Spacing();
    ImGui::TextUnformatted("Orbit around selection");
    ImGui::SameLine(380);
    static const std::vector<std::string> selections = {"only if objects selected", "only if selection is locked", "never"};
    int selection = p.Int("3D Layout\\Actions\\Orbit Selection Type");
    if (Combo("##orbitselection", selection, selections, 220)) p.SetInt("3D Layout\\Actions\\Orbit Selection Type", selection);
    EndBox();
}

// -------------------------------------------------------- 3D Layout - Interface

void ToolWindows::InterfacePage(Preferences &p) {
    const float half = ImGui::GetContentRegionAvail().x * 0.5f - 6;
    ImGui::BeginGroup();
    BeginBox("Interface Objects", ImVec2(half, 0));
    Centered("Constant Size On Screen");
    struct Row {
        const char *label, *constant, *zoom;
    };
    static const Row rows[] = {
        {"3D Frames", "3D Layout\\Interface Objects\\Constant Frame", "3D Layout\\Interface Objects\\Frame Zoom Percentage"},
        {"Cameras", "3D Layout\\Interface Objects\\Constant Camera", "3D Layout\\Interface Objects\\Camera Zoom Percentage"},
        {"Lights", "3D Layout\\Interface Objects\\Constant Light", "3D Layout\\Interface Objects\\Light Zoom Percentage"},
        {"CurvePoints", "3D Layout\\Interface Objects\\Constant Curve Point",
         "3D Layout\\Interface Objects\\Curve Point Zoom Percentage"},
    };
    for (const Row &row : rows) {
        ImGui::PushID(row.label);
        Check(p, row.constant, row.label);
        SettingSlider(p, row.zoom, 1, 100, "##zoom", 220);
        ImGui::PopID();
    }
    {
        // Object axis or snap marker, variable or constant size (one combo,
        // its size beside it; the entries are the editor's reading of Dev's
        // two settings).
        static const std::vector<std::string> kinds = {"Object Axis,Variable Size", "Object Axis,Constant Size",
                                                       "Snap Marker,Variable Size", "Snap Marker,Constant Size"};
        int &shown = m_AxisKindShown;
        const char *constant = shown ? "3D Layout\\Interface Objects\\Snap Marker" : "3D Layout\\Interface Objects\\Constant Object Axis";
        int index = shown * 2 + (p.Bool(constant) ? 1 : 0);
        if (Combo("##axiskind", index, kinds, 210)) {
            shown = index / 2;
            p.SetBool(shown ? "3D Layout\\Interface Objects\\Snap Marker" : "3D Layout\\Interface Objects\\Constant Object Axis",
                      index % 2 != 0);
        }
        SettingSlider(p, shown ? "3D Layout\\Interface Objects\\Snap Marker Percentage"
                               : "3D Layout\\Interface Objects\\Object Axis Zoom Percentage",
                      1, 100, "##axiszoom", 220);
    }
    EndBox();
    ImGui::Spacing();
    BeginBox("Setups", ImVec2(half, 0));
    ImGui::Indent(80);
    Bold("On");
    ImGui::Unindent(80);
    Check(p, "3D Layout\\Auto Show Light Cone", "Automatic show light cone");
    Check(p, "3D Layout\\Auto Show Camera Cone", "Automatic show camera cone");
    Check(p, "3D Layout\\Auto Show Entities In Player Mode", "Automatic show entities in player mode");
    Check(p, "3D Layout\\Auto Show Setup", "Automatic show setup");
    {
        bool same = !p.Bool("3D Layout\\Show New Setups");
        if (ImGui::Checkbox("Do not open setups of the same type", &same)) p.SetBool("3D Layout\\Show New Setups", !same);
    }
    ImGui::Indent(80);
    Bold("Curve");
    ImGui::Unindent(80);
    ImGui::TextUnformatted("View Plane Distance");
    ImGui::SameLine(300);
    FloatField(p, "3D Layout\\Actions\\Camera Plane Distance", "##plane", 90);
    ImGui::TextUnformatted("View Plane Distance (Ortho)");
    ImGui::SameLine(300);
    FloatField(p, "3D Layout\\Actions\\Camera Plane Distance Ortho", "##planeortho", 90);
    EndBox();
    ImGui::Spacing();
    BeginBox("Global", ImVec2(half, 0));
    Check(p, "3D Layout\\Debug Mode", "Show On-screen Debug Data");
    EndBox();
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    BeginBox("World Settings", ImVec2(0, 0));
    Centered("World Size");
    ImGui::SetCursorPosX(60);
    ImGui::TextUnformatted("Min");
    ImGui::SameLine(300);
    ImGui::TextUnformatted("Max");
    static const char *const axes[] = {"X", "Y", "Z"};
    for (size_t i = 0; i < 3; ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(axes[i]);
        ImGui::SameLine(40);
        ComponentField(p, "3D Layout\\World Size Min", i, "##min", '|', 130, "%.0f");
        ImGui::SameLine(250);
        ImGui::TextUnformatted(axes[i]);
        ImGui::SameLine(280);
        ComponentField(p, "3D Layout\\World Size Max", i, "##max", '|', 130, "%.0f");
        ImGui::PopID();
    }
    ImGui::Spacing();
    Centered("World Step (Increments)");
    for (size_t i = 0; i < 3; ++i) {
        ImGui::PushID(static_cast<int>(i) + 10);
        ImGui::TextUnformatted(axes[i]);
        ImGui::SameLine(40);
        ComponentField(p, "3D Layout\\World Step", i, "##step", '|', 130, "%.4f");
        ImGui::PopID();
    }
    EndBox();
    ImGui::Spacing();
    BeginBox("Screen Guide", ImVec2(0, 0));
    ImGui::TextUnformatted("Offset X:");
    ImGui::SameLine(110);
    ComponentField(p, "3D Layout\\Screen Offset", 0, "##ox", '|', 90);
    ImGui::TextUnformatted("Offset Y:");
    ImGui::SameLine(110);
    ComponentField(p, "3D Layout\\Screen Offset", 1, "##oy", '|', 90);
    ImGui::TextUnformatted("Step X:");
    ComponentSlider(p, "3D Layout\\Screen Step", 0, '|', 2, 50, "##sx", 110);
    ImGui::TextUnformatted("Step Y:");
    ComponentSlider(p, "3D Layout\\Screen Step", 1, '|', 2, 50, "##sy", 110);
    ImGui::Spacing();
    Check(p, "3D Layout\\Homogenous Screen Grid", "Homogeneous Coordinates");
    EndBox();
    ImGui::EndGroup();
}

// -------------------------------------------------- 3D Layout - Reference Guide

void ToolWindows::ReferenceGuidePage(Preferences &p) {
    BeginBox("Grid", ImVec2(0, 0));
    // Orientation: X 1, Y 2, Z 4 (X+Z is Dev's 5); Free Transformation 8
    // (the editor's reading of the registry value).
    const int orientation = p.Int("3D Layout\\Reference Grid\\Orientation");
    ImGui::TextUnformatted("Guide Orientation");
    static const std::pair<const char *, int> planes[] = {{"X+Y", 3}, {"Y+Z", 6}, {"X+Z", 5}};
    for (int i = 0; i < 3; ++i) {
        ImGui::SameLine(i == 0 ? 220.0f : 0.0f, i == 0 ? -1.0f : 30.0f);
        bool on = (orientation & 7) == planes[i].second;
        if (ImGui::Checkbox(planes[i].first, &on))
            p.SetInt("3D Layout\\Reference Grid\\Orientation", planes[i].second | (orientation & 8));
    }
    ImGui::SetCursorPosX(220);
    bool free = (orientation & 8) != 0;
    if (ImGui::Checkbox("Free Transformation", &free))
        p.SetInt("3D Layout\\Reference Grid\\Orientation", (orientation & 7) | (free ? 8 : 0));
    ImGui::Separator();
    Centered("Sections Count", false);
    ImGui::TextUnformatted("Horizontal");
    ComponentSlider(p, "3D Layout\\Reference Grid\\Sections", 0, ';', 1, 100, "##sh", 150);
    ImGui::TextUnformatted("Vertical");
    ComponentSlider(p, "3D Layout\\Reference Grid\\Sections", 1, ';', 1, 100, "##sv", 150);
    ImGui::Spacing();
    Centered("Size per Section", false);
    ImGui::TextUnformatted("Horizontal");
    ComponentSlider(p, "3D Layout\\Reference Grid\\Step", 0, ';', 1, 500, "##ph", 150);
    ImGui::TextUnformatted("Vertical");
    ComponentSlider(p, "3D Layout\\Reference Grid\\Step", 1, ';', 1, 500, "##pv", 150);
    EndBox();
}

// ------------------------------------------------------ 3D Layout - Snap & Align

void ToolWindows::SnapPage(Preferences &p) {
    // Snap\Mode: Dev's bits (Dev.exe dialog 746, sub_5AC1D0).
    const int mode = p.Int("3D Layout\\Snap\\Mode");
    auto bit = [&](const char *label, int mask) {
        bool on = (mode & mask) != 0;
        if (ImGui::Checkbox(label, &on)) p.SetInt("3D Layout\\Snap\\Mode", on ? (mode | mask) : (mode & ~mask));
    };
    BeginBox("3D Entities", ImVec2(0, 0));
    bit("World Sections", 0x1);
    ImGui::SameLine(280);
    bit("Meshes Vertices", 0x8);
    bit("Objects Axis", 0x4);
    ImGui::SameLine(280);
    bit("Meshes Edges", 0x10);
    ImGui::SetCursorPosX(280);
    bit("Meshes Faces", 0x20);
    ImGui::Spacing();
    ImGui::TextUnformatted("Translate Influence Radius");
    ImGui::SameLine(300);
    IntField(p, "3D Layout\\Snap\\Translation Influence (3D Entities)", "##radius", 90);
    ImGui::SameLine();
    ImGui::TextUnformatted("Pixels");
    ImGui::TextUnformatted("Rotate Step");
    ImGui::SameLine(300);
    FloatField(p, "3D Layout\\Snap\\Rotation Angle", "##rotate", 90);
    ImGui::SameLine();
    ImGui::TextUnformatted("Degrees");
    ImGui::TextUnformatted("Scale Step");
    ImGui::SameLine(300);
    FloatField(p, "3D Layout\\Snap\\Scaling Percentage", "##scale", 90);
    ImGui::SameLine();
    ImGui::TextUnformatted("Percent");
    EndBox();
    ImGui::Spacing();
    BeginBox("2D Entities", ImVec2(0, 0));
    bit("Screen Borders", 0x40);
    ImGui::SameLine(280);
    bit("Sprites", 0x100);
    bit("Screen Guide Grid", 0x80);
    ImGui::Spacing();
    ImGui::TextUnformatted("Translate Influence Size");
    ImGui::SameLine(300);
    IntField(p, "3D Layout\\Snap\\Translation Influence (Sprites)", "##influence", 90);
    ImGui::SameLine();
    ImGui::TextUnformatted("Pixels");
    EndBox();
}

// ---------------------------------------------------------------- Schematic

void ToolWindows::SchematicPage(Preferences &p) {
    const float half = ImGui::GetContentRegionAvail().x * 0.45f;
    BeginBox("Main", ImVec2(half, 0));
    ImGui::Checkbox("Automatic Link", &p.automaticLink);
    Check(p, "Schematic\\Horizontal Scrollbar", "Horizontal Scrollbar");
    Check(p, "Schematic\\Vertical Scrollbar", "Vertical Scrollbar");
    ImGui::Checkbox("Strict Multi-Selection", &p.strictMultiSelection);
    Check(p, "Schematic\\Save Script Snapshot", "Save Scripts Snapshots");
    Check(p, "Schematic\\Auto Resize Scripts", "Auto Resize Scripts to minimum height");
    Check(p, "Schematic\\Test Parameter Link after Loading", "Test Parameter Link after Loading");
    Check(p, "Schematic\\Remove Invalid Link after Loading", "Remove Invalid Parameter Link after Loading");
    ImGui::Checkbox("Path Finding Link Mode", &p.searchBestPath);
    Check(p, "Schematic\\Automatic Scroll", "Automatic Scroll Delay (ms)");
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 90);
    IntField(p, "Schematic\\Autoscroll Delay", "##scrolldelay", 90);
    ImGui::TextUnformatted("Max Recent Building Block");
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 90);
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("##maxrecent", &p.maxRecentBlocks, 0, 0);
    const int mode = p.Int("Schematic\\Edit Parameter Operation Mode");
    bool easy = mode != 0, quick = mode == 0;
    if (ImGui::Checkbox("Easy Finding Parameter Operation Dialog", &easy)) p.SetInt("Schematic\\Edit Parameter Operation Mode", 1);
    if (ImGui::Checkbox("Quick Finding Parameter Operation Dialog", &quick)) p.SetInt("Schematic\\Edit Parameter Operation Mode", 0);
    EndBox();
    ImGui::SameLine();
    ImGui::BeginGroup();
    BeginBox("Colors", ImVec2(0, 0));
    // Dev stores COLORREF (0x00BBGGRR); a click on the swatch edits it.
    auto color = [&](const char *label, CKDWORD &colorref) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 76);
        float value[3] = {(colorref & 0xFF) / 255.0f, ((colorref >> 8) & 0xFF) / 255.0f, ((colorref >> 16) & 0xFF) / 255.0f};
        if (ImGui::ColorButton("##swatch", ImVec4(value[0], value[1], value[2], 1.0f), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(72, ImGui::GetTextLineHeight() + 2)))
            ImGui::OpenPopup("pick");
        if (ImGui::BeginPopup("pick")) {
            if (ImGui::ColorPicker3("##picker", value, ImGuiColorEditFlags_NoSidePreview))
                colorref = static_cast<CKDWORD>(value[0] * 255.0f + 0.5f) |
                           (static_cast<CKDWORD>(value[1] * 255.0f + 0.5f) << 8) |
                           (static_cast<CKDWORD>(value[2] * 255.0f + 0.5f) << 16);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };
    color("Event Link Color", p.ioLinkColor);
    color("Parameter Link Color", p.parameterLinkColor);
    color("Exported Parameter Link Color", p.exportedLinkColor);
    color("Parameter Operation Link Color", p.operationLinkColor);
    color("Valid Link Color", p.validLinkColor);
    color("Invalid Link Color", p.invalidLinkColor);
    color("Obsolete Building Block Color", p.obsoleteColor);
    color("Building Block Without Prototype Color", p.noPrototypeColor);
    color("Behavior Without CKObject Color", p.noObjectColor);
    color("Building Block to Replace Color", p.toReplaceColor);
    EndBox();
    ImGui::Spacing();
    ImGui::Checkbox("Enable Tooltips.", &p.tooltips);
    ImGui::SameLine();
    ImGui::TextUnformatted("Delay(ms):");
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 90);
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("##tooltipdelay", &p.tooltipDelayMs, 0, 0);
    ImGui::EndGroup();
}

// ---------------------------------------------------------- Miscellaneous Controls

void ToolWindows::MiscPage(Preferences &p) {
    ImGui::Indent(60);
    char version[64];
    std::snprintf(version, sizeof(version), "(Version %x)", static_cast<unsigned>(CKGetVersion()));
    ImGui::TextUnformatted("CK2 Settings");
    ImGui::SameLine(240);
    ImGui::TextUnformatted(version);
    ImGui::Separator();
    ImGui::TextUnformatted("Render Engine");
    ImGui::SameLine(240);
    // The render engine plugins, named by their dll in lower case (Dev's "Render Engine").
    CKPluginManager *plugins = CKGetPluginManager();
    std::vector<std::string> engines;
    std::vector<CKPluginEntry *> entries;
    for (int i = 0; plugins && i < plugins->GetPluginCount(CKPLUGIN_RENDERENGINE_DLL); ++i) {
        CKPluginEntry *entry = plugins->GetPluginInfo(CKPLUGIN_RENDERENGINE_DLL, i);
        CKPluginDll *dll = entry ? plugins->GetPluginDllInfo(entry->m_PluginDllIndex) : nullptr;
        if (!dll || !dll->m_DllFileName.CStr()) continue;
        std::string name = std::filesystem::path(dll->m_DllFileName.CStr()).stem().string();
        for (char &c : name) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        engines.push_back(name);
        entries.push_back(entry);
    }
    std::vector<std::string> labels;
    for (const auto &engine : engines) {
        std::string upper = engine;
        for (char &c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        labels.push_back(upper);
    }
    const std::string current = p.Text("CK2 Settings\\Render Engine");
    int index = static_cast<int>(std::find(engines.begin(), engines.end(), current) - engines.begin());
    if (index == static_cast<int>(engines.size())) {
        engines.push_back(current);
        labels.push_back(current);
        entries.push_back(nullptr);
    }
    if (Combo("##engine", index, labels, 200)) p.Set("CK2 Settings\\Render Engine", engines[index]);
    ImGui::Unindent(60);
    ImGui::BeginChild("engineinfo", ImVec2(600, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    CKPluginEntry *engine = index < static_cast<int>(entries.size()) ? entries[index] : nullptr;
    auto info = [](const char *label, const std::string &value) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(200);
        ImGui::TextUnformatted(value.c_str());
    };
    info("Version:", engine ? std::to_string(engine->m_PluginInfo.m_Version) : std::string());
    info("Description:", engine && engine->m_PluginInfo.m_Description.CStr() ? AnsiToUtf8(engine->m_PluginInfo.m_Description.CStr()) : std::string());
    info("Author:", engine && engine->m_PluginInfo.m_Author.CStr() ? AnsiToUtf8(engine->m_PluginInfo.m_Author.CStr()) : std::string());
    info("Summary:", engine && engine->m_PluginInfo.m_Summary.CStr() ? AnsiToUtf8(engine->m_PluginInfo.m_Summary.CStr()) : std::string());
    ImGui::EndChild();
    if (current != m_State.preferences.Text("CK2 Settings\\Render Engine"))
        ImGui::TextDisabled("The Render Engine will not change until you restart Virtools!");
    ImGui::Spacing();
    Check(p, "3D Layout\\Show Dynamic Objects", "Show Dynamic Objects");
    Check(p, "CK2 Settings\\Sound Enabled", "Sound Enabled");
    Check(p, "CK2 Settings\\Direct Input Enabled", "Direct Input Enabled");
    Check(p, "File Options\\Compressed", "Compress file using compression level:");
    ImGui::SameLine(400);
    {
        static const std::vector<std::string> levels = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
        int level = std::max(1, std::min(9, p.Int("File Options\\Compression Level"))) - 1;
        if (Combo("##level", level, levels, 110)) p.SetInt("File Options\\Compression Level", level + 1);
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Sound files save options");
    ImGui::SameLine(400);
    {
        // CK_SOUND_SAVEOPTIONS: 0 external, 1 the original file inside.
        static const std::vector<std::string> sounds = {"External Files", "Included inside CMO file"};
        int sound = std::max(0, std::min(1, p.Int("File Options\\Sounds")));
        if (Combo("##sounds", sound, sounds, 330)) p.SetInt("File Options\\Sounds", sound);
    }
    ImGui::TextUnformatted("Textures/Sprites save options");
    ImGui::SameLine(400);
    {
        // CK_TEXTURE_SAVEOPTIONS: raw data 0, external 1, image format 2, original file 4.
        static const std::vector<std::string> textures = {"Saved as Raw Data", "External Files", "Use specific format",
                                                          "Included inside CMO file"};
        static const int codes[] = {0, 1, 2, 4};
        const int code = p.Int("File Options\\Textures");
        int texture = 0;
        for (int i = 0; i < 4; ++i)
            if (codes[i] == code) texture = i;
        if (Combo("##textures", texture, textures, 330)) p.SetInt("File Options\\Textures", codes[texture]);
        if (codes[texture] == 2) {
            // The format: a bitmap reader that can save.
            std::vector<std::string> formats, extensions;
            for (int i = 0; plugins && i < plugins->GetPluginCount(CKPLUGIN_BITMAP_READER); ++i) {
                CKPluginEntry *reader = plugins->GetPluginInfo(CKPLUGIN_BITMAP_READER, i);
                if (!reader || !reader->m_PluginInfo.m_Extension.m_Data[0]) continue;
                if (reader->m_ReadersInfo && !(reader->m_ReadersInfo->m_ReaderFlags & CK_DATAREADER_FILESAVE)) continue;
                extensions.push_back(reader->m_PluginInfo.m_Extension.m_Data);
                formats.push_back(AnsiToUtf8(std::string(reader->m_PluginInfo.m_Summary.CStr() ? reader->m_PluginInfo.m_Summary.CStr() : "") +
                                             " (" + reader->m_PluginInfo.m_Extension.m_Data + ")"));
            }
            const std::string ext = p.Text("File Options\\Texture Format");
            int format = static_cast<int>(std::find(extensions.begin(), extensions.end(), ext) - extensions.begin());
            ImGui::SetCursorPosX(400);
            if (Combo("##format", format, formats, 330)) p.Set("File Options\\Texture Format", extensions[format]);
        }
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Image Quantization Quality");
    ImGui::SameLine(400);
    {
        // VxMath's quantization sampling factor; Dev's label: factor / 10.
        int factor = std::max(1, std::min(100, p.Int("CK2 Settings\\Quantization Sampling Factor")));
        ImGui::TextUnformatted("1");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        if (ImGui::SliderInt("##quantization", &factor, 1, 100, "")) p.SetInt("CK2 Settings\\Quantization Sampling Factor", factor);
        ImGui::SameLine();
        static const char *const quality[] = {"Very slow / Best Quality", "Slow / High Quality",
                                              "Average / Medium Quality", "Fastest / Low Quality"};
        ImGui::Text("100   %s", quality[std::min(3, factor / 10)]);
    }
}

} // namespace vdev

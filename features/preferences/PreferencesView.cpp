#include "features/preferences/PreferencesView.h"
#include "application/Preferences.h"
#include "workspace/EditorClient.h"
#include "workspace/EditorPopup.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/TypeAhead.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <sstream>

namespace vdev {

namespace {
struct PreferencesView {
    EditorClient client;
    EditorPopup popup;
    Preferences draft;
    std::vector<EditorField> catalog;
    std::set<std::string> invalid;
    uint64_t revision = 0, catalogVersion = 0;
    EditorOperation operation = 0;
    int page = 0, axis = 0, center = 3;
    bool loaded = false;
    std::string message;
    PreferencesView(const EditorHost *host, EditorView id) : client(host, id) {}
    const std::vector<EditorField> &Rows(const char *key) const;
    void Draw(EditorViewFrame &);
    bool Check(Preferences &, const char *, const char *);
    void NumberField(Preferences &, const char *, const char *, float, const char *, bool);
    void FloatField(Preferences &, const char *, const char *, float = 90, const char * = "%g");
    void IntField(Preferences &, const char *, const char *, float = 90);
    void RenderingPage(Preferences &);
    void ActionsPage(Preferences &);
    void InterfacePage(Preferences &);
    void ReferenceGuidePage(Preferences &);
    void SnapPage(Preferences &);
    void SchematicPage(Preferences &);
    void MiscPage(Preferences &);
};
const EditorField *Field(const std::vector<EditorField> &fields, const char *key) {
    for (const auto &field : fields) if (field.key == key) return &field;
    return nullptr;
}
std::string Text(const EditorField &row, const char *key) {
    const auto *field = Field(row.fields, key); return field ? field->text : std::string();
}
const std::vector<EditorField> &PreferencesView::Rows(const char *key) const {
    static const std::vector<EditorField> empty;
    const auto *field = Field(catalog, key); return field ? field->fields : empty;
}
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

bool PreferencesView::Check(Preferences &p, const char *key, const char *label) {
    EditorValue value{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(p.Bool(key))};
    const EditorSink update{&value, [](void *data, const EditorValue *next, uint32_t count) {
        if (!next || count != 1 || next->type != EDITOR_INTEGER_VALUE || next->integer > 1) return EDITOR_INVALID;
        static_cast<EditorValue *>(data)->integer = next->integer; return EDITOR_OK;
    }};
    const EditorValueInput input{sizeof(input), 0, EditorLiteral(label), value, nullptr, 0, 0, update, EDITOR_VALUE_INLINE, 0, ImGui::GetFrameHeight()};
    const auto action = client.Host()->drawValue(client.Host()->data, EditorString EDITOR_TEXT("value.boolean"), &input);
    if (action & EDITOR_INPUT_INVALID) invalid.insert(key);
    else if (action & EDITOR_INPUT_CHANGED) { invalid.erase(key); p.SetBool(key, value.integer != 0); return true; }
    return false;
}
void PreferencesView::NumberField(Preferences &p, const char *key, const char *id, float width, const char *format, bool real) {
    EditorValue value{EDITOR_TEXT("value"), real ? EDITOR_REAL_VALUE : EDITOR_INTEGER_VALUE};
    if (real) value.real = p.Float(key); else value.integer = uint64_t(int64_t(p.Int(key)));
    char text[96];
    if (real) std::snprintf(text, sizeof(text), format, value.real); else std::snprintf(text, sizeof(text), "%d", int(int64_t(value.integer)));
    const EditorValue display{EDITOR_TEXT("display"), EDITOR_TEXT_VALUE, {}, EditorLiteral(text)};
    const EditorSink update{&value, [](void *data, const EditorValue *next, uint32_t count) {
        auto &value = *static_cast<EditorValue *>(data);
        if (!next || count != 1 || next->type != value.type) return EDITOR_INVALID;
        if (next->type == EDITOR_REAL_VALUE && (!std::isfinite(next->real) || std::abs(next->real) > std::numeric_limits<float>::max())) return EDITOR_INVALID;
        if (next->type == EDITOR_INTEGER_VALUE && (int64_t(next->integer) < INT32_MIN || int64_t(next->integer) > INT32_MAX)) return EDITOR_INVALID;
        value.integer = next->integer; value.real = next->real; return EDITOR_OK;
    }};
    const EditorValueInput input{sizeof(input), 0, EditorLiteral(id), value, &display, 1, 0, update, EDITOR_VALUE_DIALOG, width, ImGui::GetFrameHeight()};
    const auto action = client.Host()->drawValue(client.Host()->data, EditorLiteral(real ? "value.real" : "value.integer"), &input);
    if (action & EDITOR_INPUT_INVALID) invalid.insert(key);
    else if (action & EDITOR_INPUT_CHANGED) {
        invalid.erase(key);
        if (real) p.SetFloat(key, float(value.real)); else p.SetInt(key, int(int64_t(value.integer)));
    }
}
void PreferencesView::FloatField(Preferences &p, const char *key, const char *id, float width, const char *format) { NumberField(p, key, id, width, format, true); }
void PreferencesView::IntField(Preferences &p, const char *key, const char *id, float width) { NumberField(p, key, id, width, "%d", false); }

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

void DriverCombo(Preferences &p, const std::vector<EditorField> &rows, const char *key, const char *id, float width) {
    std::vector<std::string> drivers;
    for (const auto &row : rows) drivers.push_back(Text(row, "name"));
    const auto current = p.Text(key);
    if (!current.empty() && std::find(drivers.begin(), drivers.end(), current) == drivers.end()) drivers.push_back(current);
    int index = int(std::find(drivers.begin(), drivers.end(), current) - drivers.begin());
    if (Combo(id, index, drivers, width)) p.Set(key, drivers[size_t(index)]);
}

// "640x480x32x59" <-> "640x480x32 @ 59Hz".
std::string ModeLabel(const std::string &mode) {
    int w = 0, h = 0, bpp = 0, hz = 0;
    if (std::sscanf(mode.c_str(), "%dx%dx%dx%d", &w, &h, &bpp, &hz) == 4)
        return std::to_string(w) + "x" + std::to_string(h) + "x" + std::to_string(bpp) + " @ " + std::to_string(hz) + "Hz";
    return mode;
}


void PreferencesView::Draw(EditorViewFrame &frame) {
    if (!loaded) {
        const auto result = client.Query("preferences.values");
        const auto *value = EditorClient::Find(result, "value");
        if (result.status == EDITOR_OK && value && draft.ReadValues(value->fields, message)) {
            revision = EditorClient::Integer(result, "revision"); loaded = true; message.clear();
        }
    }
    const auto version = client.Revision("preferences.catalog");
    if (catalog.empty() || version != catalogVersion) {
        const auto result = client.Query("preferences.catalog");
        if (result.status == EDITOR_OK) { catalog = result.fields; catalogVersion = version; }
    }
    if (operation) {
        const auto result = client.Completion(operation);
        if (result.status != EDITOR_QUEUED) {
            operation = 0;
            if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) { frame.close = 1; return; }
            message = result.message.empty() ? "Preferences changed or could not be saved. Close and reopen this dialog." : result.message;
        }
    }
    popup.Open(frame);
    if (center > 0) { ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(.5f,.5f)); --center; }
    if (!ImGui::BeginPopupModal(frame.window.data, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::BeginDisabled(!loaded || operation != 0);
    const auto &pages = Preferences::Pages();
    const float comboWidth = 270;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - comboWidth - ImGui::CalcTextSize("Current Prefs").x - 20);
    Bold("Current Prefs"); ImGui::SameLine(); ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo("##page", pages[size_t(page)].title)) {
        TypeAhead typeAhead;
        for (int i = 0; size_t(i) < pages.size(); ++i) if (typeAhead.Selectable(pages[size_t(i)].title, i == page) && i != page) { page = i; center = 3; }
        ImGui::EndCombo();
    }
    ImGui::BeginChild("page", ImVec2(PageSize(page).x, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    switch (page) {
    case 0: RenderingPage(draft); break;
    case 1: ActionsPage(draft); break;
    case 2: InterfacePage(draft); break;
    case 3: ReferenceGuidePage(draft); break;
    case 4: SnapPage(draft); break;
    case 5: SchematicPage(draft); break;
    default: MiscPage(draft); break;
    }
    ImGui::EndChild();
    if (ImGui::Button("Default", ImVec2(90,0))) {
        const auto &engines = Rows("engines");
        draft.ResetPage(page, engines.empty() ? std::string() : Text(engines.front(), "name")); invalid.clear();
    }
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 2 * 90 - ImGui::GetStyle().ItemSpacing.x);
    const auto available = client.Query("preferences.state");
    const bool canApply = loaded && !operation && invalid.empty() && available.status == EDITOR_OK && EditorClient::Integer(available, "available");
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button("OK", ImVec2(90,0))) {
        draft.maxRecentBlocks = std::clamp(draft.maxRecentBlocks, 1, 100); draft.tooltipDelayMs = std::max(0, draft.tooltipDelayMs);
        EditorField value{"value", EDITOR_RECORD}; value.fields = draft.Values();
        const std::vector<EditorField> fields{value}; const auto borrowed = BorrowEditorFields(fields);
        const auto result = client.Command("preferences.apply", {IntegerArgument("revision", revision), borrowed.values.front()});
        if (result.status == EDITOR_QUEUED) operation = result.operation;
        else if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) frame.close = 1;
        else message = result.message.empty() ? "Preferences changed or could not be saved. Close and reopen this dialog." : result.message;
    }
    ImGui::EndDisabled(); ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90,0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) frame.close = 1;
    if (!invalid.empty()) ImGui::TextUnformatted("Enter a valid value before applying.");
    if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
    ImGui::EndPopup();
}

// ------------------------------------------------------- 3D Layout - Rendering

void PreferencesView::RenderingPage(Preferences &p) {
    const auto &drivers = Rows("drivers");
    static const std::vector<std::string> renderTypes = {"Wireframe", "Flat", "Gouraud", "Phong"};
    const float half = ImGui::GetContentRegionAvail().x * 0.5f - 6;
    ImGui::BeginGroup();
    BeginBox("Author Mode", ImVec2(half, 0));
    ImGui::TextUnformatted("Render Device");
    ImGui::SameLine(200);
    DriverCombo(p, drivers, "3D Layout\\Author Mode\\Device Driver", "##authordevice", half - 215);
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
    DriverCombo(p, drivers, "3D Layout\\Player Mode\\FullScren Driver", "##playerdevice", -1);
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
        const auto stored = p.Text("3D Layout\\Player Mode\\FullScren Driver");
        auto driver = std::find_if(drivers.begin(), drivers.end(), [&](const EditorField &row) { return ImStricmp(Text(row, "name").c_str(), stored.c_str()) == 0; });
        if (driver == drivers.end() && !drivers.empty()) driver = drivers.begin();
        if (driver != drivers.end()) if (const auto *choices = Field(driver->fields, "modes")) modes = choices->texts;
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

void PreferencesView::ActionsPage(Preferences &p) {
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

void PreferencesView::InterfacePage(Preferences &p) {
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
        int &shown = axis;
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

void PreferencesView::ReferenceGuidePage(Preferences &p) {
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

void PreferencesView::SnapPage(Preferences &p) {
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

void PreferencesView::SchematicPage(Preferences &p) {
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
    auto color = [&](const char *label, uint32_t &colorref) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 76);
        float value[3] = {(colorref & 0xFF) / 255.0f, ((colorref >> 8) & 0xFF) / 255.0f, ((colorref >> 16) & 0xFF) / 255.0f};
        if (ImGui::ColorButton("##swatch", ImVec4(value[0], value[1], value[2], 1.0f), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(72, ImGui::GetTextLineHeight() + 2)))
            ImGui::OpenPopup("pick");
        if (ImGui::BeginPopup("pick")) {
            if (ImGui::ColorPicker3("##picker", value, ImGuiColorEditFlags_NoSidePreview))
                colorref = static_cast<uint32_t>(value[0] * 255.0f + 0.5f) |
                           (static_cast<uint32_t>(value[1] * 255.0f + 0.5f) << 8) |
                           (static_cast<uint32_t>(value[2] * 255.0f + 0.5f) << 16);
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

void PreferencesView::MiscPage(Preferences &p) {
    ImGui::Indent(60);
    char version[64];
    std::snprintf(version, sizeof(version), "(Version %x)", static_cast<unsigned>(Field(catalog, "version") ? Field(catalog, "version")->integer : 0));
    ImGui::TextUnformatted("CK2 Settings");
    ImGui::SameLine(240);
    ImGui::TextUnformatted(version);
    ImGui::Separator();
    ImGui::TextUnformatted("Render Engine");
    ImGui::SameLine(240);
    const auto &entries = Rows("engines");
    std::vector<std::string> engines, labels;
    for (const auto &entry : entries) { engines.push_back(Text(entry, "name")); labels.push_back(Text(entry, "label")); }
    const std::string current = p.Text("CK2 Settings\\Render Engine");
    int index = static_cast<int>(std::find(engines.begin(), engines.end(), current) - engines.begin());
    if (index == static_cast<int>(engines.size())) {
        engines.push_back(current);
        labels.push_back(current);
    }
    if (Combo("##engine", index, labels, 200)) p.Set("CK2 Settings\\Render Engine", engines[index]);
    ImGui::Unindent(60);
    ImGui::BeginChild("engineinfo", ImVec2(600, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    const EditorField *engine = index < static_cast<int>(entries.size()) ? &entries[size_t(index)] : nullptr;
    auto info = [](const char *label, const std::string &value) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(200);
        ImGui::TextUnformatted(value.c_str());
    };
    info("Version:", engine && Field(engine->fields, "version") ? std::to_string(Field(engine->fields, "version")->integer) : std::string());
    info("Description:", engine ? Text(*engine, "description") : std::string());
    info("Author:", engine ? Text(*engine, "author") : std::string());
    info("Summary:", engine ? Text(*engine, "summary") : std::string());
    ImGui::EndChild();
    if (current != (Field(catalog, "engine") ? Field(catalog, "engine")->text : std::string()))
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
            for (const auto &reader : Rows("formats")) { extensions.push_back(Text(reader, "extension")); formats.push_back(Text(reader, "label")); }
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

} // namespace

bool RegisterPreferencesView(EditorApplication &editor, std::string &error) {
    static const EditorParameter page{EDITOR_TEXT("page"), EDITOR_TEXT_VALUE, 1};
    EditorViewDesc view{}; view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("preferences.general"); view.provider = EDITOR_TEXT("builtin.preference-views");
    view.title = EDITOR_TEXT("General Preferences"); view.singleInstance = 1; view.area = EDITOR_APPLICATION;
    view.create = [](void *, const EditorHost *host, EditorView id) -> void * { return new PreferencesView(host, id); };
    view.destroy = [](void *data) { delete static_cast<PreferencesView *>(data); };
    view.draw = [](void *data, const EditorHost *, EditorView, EditorViewFrame *frame) { static_cast<PreferencesView *>(data)->Draw(*frame); };
    view.parameters = &page; view.count = 1;
    view.navigate = [](void *data, const EditorHost *, const EditorRequest *request) {
        const auto &value = request->values[0].text;
        if (!value.size) return EDITOR_OK;
        const std::string name(value.data, value.size);
        const auto &pages = Preferences::Pages();
        const auto found = std::find_if(pages.begin(), pages.end(), [&](const PreferencePage &page) { return name == page.id; });
        if (found == pages.end()) return EDITOR_INVALID;
        auto &view = *static_cast<PreferencesView *>(data); view.page = int(found - pages.begin()); view.center = 3;
        return EDITOR_OK;
    };
    view.stateVersion = 1;
    static const EditorParameter settings[] = {{EDITOR_TEXT("page"), EDITOR_TEXT_VALUE},
                                               {EDITOR_TEXT("axis"), EDITOR_INTEGER_VALUE}};
    view.settings = settings;
    view.settingCount = uint32_t(std::size(settings));
    view.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &view = *static_cast<PreferencesView *>(data);
        const EditorValue fields[] = {{EDITOR_TEXT("page"), EDITOR_TEXT_VALUE, {}, EditorLiteral(Preferences::Pages()[size_t(view.page)].id)},
            {EDITOR_TEXT("axis"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(view.axis)}};
        return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
    };
    view.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        std::vector<EditorField> fields;
        if (CopyEditorFields(fields, request->values, request->count) != EDITOR_OK || fields.size() != 2) return EDITOR_INVALID;
        const auto *page = Field(fields, "page"), *axis = Field(fields, "axis");
        if (!page || page->type != EDITOR_TEXT_VALUE || !axis || axis->type != EDITOR_INTEGER_VALUE || axis->integer > 1) return EDITOR_INVALID;
        const auto &pages = Preferences::Pages();
        const auto found = std::find_if(pages.begin(), pages.end(), [&](const PreferencePage &item) { return page->text == item.id; });
        if (found == pages.end()) return EDITOR_INVALID;
        auto &view = *static_cast<PreferencesView *>(data); view.page = int(found - pages.begin()); view.axis = int(axis->integer);
        return EDITOR_OK;
    };
    static const EditorParameter fields[] = {{EDITOR_TEXT("page"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("value"), EDITOR_RECORD}, {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE}};
    view.fields = fields; view.fieldCount = uint32_t(std::size(fields));
    view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &view = *static_cast<PreferencesView *>(data);
        EditorField value{"value", EDITOR_RECORD}; value.fields = view.draft.Values();
        const std::vector<EditorField> fields{{"page", EDITOR_TEXT_VALUE, {}, Preferences::Pages()[size_t(view.page)].id},
            {"revision", EDITOR_INTEGER_VALUE, {}, {}, view.revision}, value, {"message", EDITOR_TEXT_VALUE, {}, view.message}};
        const auto borrowed = BorrowEditorFields(fields); return sink.write ? sink.write(sink.data, borrowed.data(), uint32_t(borrowed.size())) : EDITOR_INVALID;
    };
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("preferences.open"), EDITOR_TEXT("builtin.preference-views"), EDITOR_TEXT("General Preferences"),
            &page, 1, [](void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
                const auto &page = request->values[0].text;
                const std::string name(page.data ? page.data : "", page.size);
                if (!name.empty() && std::none_of(Preferences::Pages().begin(), Preferences::Pages().end(),
                    [&](const PreferencePage &page) { return name == page.id; })) return EDITOR_INVALID;
                const EditorClient client(host, request->scope.view);
                const auto result = client.Command("workspace.open-role", {{EDITOR_TEXT("role"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("main.preferences")}});
                if (result.status != EDITOR_OK) return result.status;
                if (!request->values[0].text.size) {
                    const EditorValue field{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(result, "view")};
                    return sink.write ? sink.write(sink.data, &field, 1) : EDITOR_OK;
                }
                return host->navigateView(host->data, EditorString EDITOR_TEXT("preferences.general"), EditorClient::Integer(result, "view"), request, sink);
            }, nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("preferences.close"), EDITOR_TEXT("builtin.preference-views"), EDITOR_TEXT("Close General Preferences"),
            nullptr, 0, [](void *, const EditorHost *host, const EditorRequest *, EditorSink) {
                const EditorClient client(host, 0);
                const auto views = client.Query("workspace.instances");
                const auto *types = EditorClient::Find(views, "types"), *ids = EditorClient::Find(views, "views");
                if (views.status != EDITOR_OK || !types || !ids || types->texts.size() != ids->integers.size()) return EDITOR_ERROR;
                for (size_t i = 0; i < types->texts.size(); ++i) if (types->texts[i] == "preferences.general") {
                    const auto result = client.Command("workspace.close-view", {IntegerArgument("view", ids->integers[i])});
                    if (result.status != EDITOR_OK) return result.status;
                }
                return EDITOR_OK;
            }, nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE}};
    const EditorMenuLocation location{EDITOR_TEXT("options.preferences"), EDITOR_TEXT("Options preferences")};
    const EditorMenuDesc menu{EDITOR_TEXT("preferences.open"), EDITOR_TEXT("preferences.open"), EDITOR_TEXT("options.preferences"), EDITOR_TEXT("General Preferences"), 0};
    const EditorBindingDesc binding{EDITOR_TEXT("preferences.open"), EDITOR_TEXT("Ctrl+P")};
    const EditorString dependencies[] = {EDITOR_TEXT("preferences.settings"), EDITOR_TEXT("workspace.views")};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("preferences.views");
    module.commands = commands; module.commandCount = 2; module.views = &view; module.viewCount = 1;
    module.locations = &location; module.locationCount = 1; module.menus = &menu; module.menuCount = 1;
    module.bindings = &binding; module.bindingCount = 1; module.dependencies = dependencies; module.dependencyCount = 2;
    return editor.Register(module, error);
}
} // namespace vdev

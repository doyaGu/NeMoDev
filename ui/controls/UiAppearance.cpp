#include "ui/controls/UiAppearance.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/DevBitmaps.h"
#include "core/runtime/Resources.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

namespace vdev {
namespace {
std::string Text(EditorString s) { return {s.data ? s.data : "", s.size}; }
std::string Role(const char *name) {
    std::string result;
    for (const unsigned char c : std::string(name)) {
        if (std::isupper(c) && !result.empty()) result += '-';
        result += char(std::tolower(c));
    }
    return result;
}
uint32_t Rgba(ImVec4 color) {
    const auto channel = [](float value) { return unsigned(std::lround(std::clamp(value, 0.f, 1.f) * 255)); };
    return (channel(color.x) << 24) | (channel(color.y) << 16) | (channel(color.z) << 8) | channel(color.w);
}
ImVec4 Color(uint32_t value) {
    return {float(value >> 24) / 255, float((value >> 16) & 255) / 255, float((value >> 8) & 255) / 255,
            float(value & 255) / 255};
}
template <class C, class F, class V> void Visit(DevTheme &theme, C color, F scalar, V pair) {
    for (int i = 0; i < ImGuiCol_COUNT; ++i) color("color." + Role(ImGui::GetStyleColorName(i)), theme.style.Colors[i]);
    const char *chrome[] = {"text",   "text-disabled",  "popup-bg",         "menu-bar-bg",    "window-bg",
                            "header", "header-hovered", "button",           "button-hovered", "button-active",
                            "border", "frame-bg",       "frame-bg-hovered", "check-mark",     "separator"};
    const char *menu[] = {"popup-bg", "border", "header-hovered", "header", "separator", "text"};
    const char *controls[] = {"text",  "menu",   "toolbar",   "chrome", "disabled", "shadow",
                              "panel", "border", "highlight", "paper",  "alert"};
    const char *pill[] = {"active", "hovered", "border", "text", "text-disabled"};
    for (int i = 0; i < 15; ++i) color(std::string("color.chrome.") + chrome[i], theme.chrome[i]);
    for (int i = 0; i < 6; ++i) color(std::string("color.menu.") + menu[i], theme.menu[i]);
    const auto packed = [&](const std::string &role, ImU32 &value) {
        auto c = ImGui::ColorConvertU32ToFloat4(value);
        color(role, c);
        value = ImGui::ColorConvertFloat4ToU32(c);
    };
    for (int i = 0; i < 11; ++i) packed(std::string("color.control.") + controls[i], theme.controls[i]);
    for (int i = 0; i < 5; ++i) packed(std::string("color.pill.") + pill[i], theme.pill[i]);
    packed("color.tab-text", theme.tabText);
    packed("color.control.line", theme.controlLine);
    packed("color.control.outline", theme.controlOutline);
    packed("color.control.table-header", theme.controlTableHeader);
    packed("color.control.table-sort", theme.controlTableSort);
    const char *layout[] = {"top-bar",
                            "top-border",
                            "tools",
                            "tools-border",
                            "panel",
                            "button-border",
                            "scrollbar",
                            "scrollbar-border",
                            "message",
                            "selection",
                            "overlay",
                            "player-frame",
                            "button-checked-border"};
    for (int i = 0; i < 13; ++i) packed(std::string("color.layout.") + layout[i], theme.layout[i]);
    VisitSchematicColors(theme.schematic, [&](const char *role, ImU32 &value) {
        packed(std::string("color.schematic.") + role, value);
    });
    packed("color.selection.fill", theme.selectionFill);
    packed("color.selection.text", theme.selectionText);
    packed("color.selection.border", theme.selectionBorder);
    packed("color.selection.hover-border", theme.hoverBorder);
    packed("color.preview.background", theme.previewBackground);
    packed("color.preview.border", theme.previewBorder);
    packed("color.preview.grid", theme.previewGrid);
    packed("color.preview.curve", theme.previewCurve);
    packed("color.preview.marker", theme.previewMarker);
    packed("color.preview.image-background", theme.imageBackground);
    packed("color.warning-text", theme.warningText);
    packed("color.snapshot.background", theme.snapshotBackground);
    packed("color.snapshot.empty-border", theme.snapshotEmptyBorder);
    struct Scalar {
        const char *name;
        float ImGuiStyle::*member;
    };
    const Scalar scalars[] = {{"alpha", &ImGuiStyle::Alpha},
                              {"disabled-alpha", &ImGuiStyle::DisabledAlpha},
                              {"window-rounding", &ImGuiStyle::WindowRounding},
                              {"child-rounding", &ImGuiStyle::ChildRounding},
                              {"frame-rounding", &ImGuiStyle::FrameRounding},
                              {"popup-rounding", &ImGuiStyle::PopupRounding},
                              {"scrollbar-rounding", &ImGuiStyle::ScrollbarRounding},
                              {"grab-rounding", &ImGuiStyle::GrabRounding},
                              {"tab-rounding", &ImGuiStyle::TabRounding},
                              {"window-border-size", &ImGuiStyle::WindowBorderSize},
                              {"frame-border-size", &ImGuiStyle::FrameBorderSize},
                              {"popup-border-size", &ImGuiStyle::PopupBorderSize},
                              {"tab-border-size", &ImGuiStyle::TabBorderSize},
                              {"scrollbar-size", &ImGuiStyle::ScrollbarSize},
                              {"indent-spacing", &ImGuiStyle::IndentSpacing},
                              {"grab-min-size", &ImGuiStyle::GrabMinSize}};
    for (const auto &s : scalars) scalar(std::string("metric.") + s.name, theme.style.*s.member);
    pair("metric.window-padding", theme.style.WindowPadding);
    pair("metric.frame-padding", theme.style.FramePadding);
    pair("metric.item-spacing", theme.style.ItemSpacing);
    pair("metric.item-inner-spacing", theme.style.ItemInnerSpacing);
    scalar("metric.menu.rounding", theme.menuRounding);
    scalar("metric.vi-normal-width", theme.viWidth[0]);
    scalar("metric.vi-bold-width", theme.viWidth[1]);
    pair("metric.menu.padding", theme.menuPadding);
    pair("metric.menu.spacing", theme.menuSpacing);
    scalar("metric.dialog.title-height", theme.dialogTitleHeight);
    scalar("metric.dialog.rounding", theme.dialogRounding);
    scalar("metric.group.rounding", theme.groupRounding);
    scalar("metric.group.border", theme.groupBorder);
    scalar("metric.setup.coordinate-scale", theme.setupCoordinateScale);
    scalar("metric.setup.column-width", theme.setupColumnWidth);
    scalar("metric.setup.sidebar-width", theme.setupSidebarWidth);
    scalar("metric.setup.label-width", theme.setupLabelWidth);
    pair("metric.setup.spacing", theme.setupSpacing);
    pair("metric.list.padding", theme.listPadding);
    pair("metric.event-log.padding", theme.eventLogPadding);
}
std::vector<EditorField> DefaultValues() {
    auto theme = DefaultDevTheme();
    std::vector<EditorField> result;
    Visit(
        theme,
        [&](const std::string &role, ImVec4 &c) { result.push_back({role, EDITOR_INTEGER_VALUE, {}, {}, Rgba(c)}); },
        [&](const std::string &role, float &v) {
            EditorField value{role, EDITOR_REAL_VALUE};
            value.real = v;
            result.push_back(std::move(value));
        },
        [&](const std::string &role, ImVec2 &v) {
            EditorField value{role, EDITOR_REAL_LIST};
            value.reals = {v.x, v.y};
            result.push_back(std::move(value));
        });
    for (const auto *role : {"body", "vi-normal", "vi-bold", "monospace"})
        result.push_back({std::string("font.") + role, EDITOR_TEXT_VALUE, {}, std::string("ui.font.") + role});
    return result;
}
bool CompileTheme(const std::map<std::string, EditorField> &values, DevTheme &theme, std::string &error) {
    try {
        Visit(
            theme,
            [&](const std::string &role, ImVec4 &c) {
                if (const auto it = values.find(role); it != values.end()) {
                    if (it->second.type != EDITOR_INTEGER_VALUE) throw std::runtime_error("Expected a color: " + role);
                    c = Color(uint32_t(it->second.integer));
                }
            },
            [&](const std::string &role, float &v) {
                if (const auto it = values.find(role); it != values.end()) {
                    if (it->second.type != EDITOR_REAL_VALUE)
                        throw std::runtime_error("Expected a scalar metric: " + role);
                    v = float(it->second.real);
                }
            },
            [&](const std::string &role, ImVec2 &v) {
                if (const auto it = values.find(role); it != values.end()) {
                    if (it->second.type != EDITOR_REAL_LIST || it->second.reals.size() != 2)
                        throw std::runtime_error("Expected a metric pair: " + role);
                    v = {float(it->second.reals[0]), float(it->second.reals[1])};
                }
            });
        if (theme.viWidth[0] <= 0 || theme.viWidth[0] > 4 || theme.viWidth[1] <= 0 || theme.viWidth[1] > 4)
            throw std::runtime_error("Invalid interface font width");
        if (theme.style.Alpha > 1 || theme.style.DisabledAlpha > 1) throw std::runtime_error("Invalid theme alpha");
        if (theme.setupCoordinateScale <= 0 || theme.setupCoordinateScale > 4 || theme.dialogTitleHeight < 1 ||
            theme.setupColumnWidth < 120 || theme.setupColumnWidth > 2000 || theme.setupSidebarWidth < 72 ||
            theme.setupSidebarWidth > 400 || theme.setupLabelWidth < 20 || theme.setupLabelWidth > 600)
            throw std::runtime_error("Invalid page geometry");
        return true;
    } catch (const std::exception &failure) {
        error = failure.what();
        return false;
    }
}
std::string ImageId(const EmbeddedImage &image) {
    return std::string(image.group) + "." + image.kind + "." + std::to_string(image.id);
}
// Check the SFNT directory before handing a file to the font loader. This
// catches missing, truncated and unrelated assets without triggering an assert.
bool FontFile(const std::string &file, std::vector<unsigned char> &data) {
    std::ifstream input(std::filesystem::u8path(file), std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (!input || length < 12 || length > 64 * 1024 * 1024) return false;
    data.resize(size_t(length));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(data.data()), length);
    if (!input) return false;
    const auto u32 = [&](size_t p) {
        return (uint32_t(data[p]) << 24) | (uint32_t(data[p + 1]) << 16) | (uint32_t(data[p + 2]) << 8) | data[p + 3];
    };
    const auto signature = u32(0);
    if (signature != 0x00010000 && signature != 0x4f54544f && signature != 0x74727565) return false;
    const auto count = (unsigned(data[4]) << 8) | data[5];
    if (!count || count > 4096 || 12 + size_t(count) * 16 > data.size()) return false;
    for (unsigned i = 0; i < count; ++i) {
        const auto offset = u32(12 + i * 16 + 8), size = u32(12 + i * 16 + 12);
        if (offset > data.size() || size > data.size() - offset) return false;
    }
    return true;
}
} // namespace

bool RegisterDefaultAppearance(EditorApplication &editor, std::string &error) {
    const auto values = DefaultValues();
    const auto borrowed = BorrowEditorFields(values);
    EditorThemeDesc theme{
        sizeof(theme),   EDITOR_TEXT("editor.theme"), EDITOR_TEXT("builtin.classic"), EDITOR_TEXT("Classic"), {},
        borrowed.data(), uint32_t(borrowed.size())};
    const auto directory = std::filesystem::u8path(DataFilePath("fonts"));
    const auto regular = (directory / "NotoSans-Regular.ttf").u8string();
    const auto bold = (directory / "NotoSans-Bold.ttf").u8string();
    const auto cjk = (directory / "NotoSansSC-Regular.otf").u8string();
    const bool hasCjk = std::filesystem::is_regular_file(std::filesystem::u8path(cjk));
    const EditorString normalFiles[] = {EditorText(regular), EditorText(cjk)};
    const EditorString boldFiles[] = {EditorText(bold), EditorText(cjk)};
    const EditorString monoFiles[] = {{}, EditorText(regular), EditorText(cjk)};
    const EditorFontDesc fonts[] = {
        {sizeof(EditorFontDesc), EDITOR_TEXT("ui.font.body"), EDITOR_TEXT("builtin.classic"), EDITOR_TEXT("Text"),
         normalFiles, hasCjk ? 2u : 1u, 14},
        {sizeof(EditorFontDesc), EDITOR_TEXT("ui.font.vi-normal"), EDITOR_TEXT("builtin.classic"),
         EDITOR_TEXT("Interface Text"), normalFiles, hasCjk ? 2u : 1u, 12},
        {sizeof(EditorFontDesc), EDITOR_TEXT("ui.font.vi-bold"), EDITOR_TEXT("builtin.classic"),
         EDITOR_TEXT("Interface Bold"), boldFiles, hasCjk ? 2u : 1u, 14},
        {sizeof(EditorFontDesc), EDITOR_TEXT("ui.font.monospace"), EDITOR_TEXT("builtin.classic"),
         EDITOR_TEXT("Monospace"), monoFiles, hasCjk ? 3u : 2u, 14}};
    unsigned count = 0;
    const auto *embedded = EmbeddedImages(&count);
    std::vector<std::string> names;
    names.reserve(count);
    std::vector<EditorImageDesc> images;
    for (unsigned i = 0; i < count; ++i) {
        const auto &image = embedded[i];
        names.push_back(ImageId(image));
        images.push_back({sizeof(EditorImageDesc), EditorText(names.back()), EDITOR_TEXT("builtin.classic"),
                          EditorText(names.back()), uint32_t(image.width), uint32_t(image.height), image.rgba,
                          uint32_t(image.width * image.height * 4)});
    }
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("appearance.classic");
    module.themes = &theme;
    module.themeCount = 1;
    module.fonts = fonts;
    module.fontCount = 4;
    module.images = images.data();
    module.imageCount = uint32_t(images.size());
    return editor.Register(module, error);
}
EditorConfiguration DefaultAppearanceConfiguration() {
    EditorConfiguration config;
    config.providers.push_back({"theme", "editor.theme", "builtin.classic"});
    for (const auto *role : {"body", "vi-normal", "vi-bold", "monospace"})
        config.providers.push_back({"font", std::string("ui.font.") + role, "builtin.classic"});
    unsigned count = 0;
    const auto *images = EmbeddedImages(&count);
    for (unsigned i = 0; i < count; ++i) config.providers.push_back({"image", ImageId(images[i]), "builtin.classic"});
    return config;
}

struct UiAppearance::State {
    struct Font {
        ImFont *face = nullptr;
        EditorFontDesc desc{};
    };
    std::map<std::pair<std::string, std::string>, Font> fonts;
    std::map<std::string, Font> preparedFonts;
    DevTheme preparedTheme{};
    uint64_t prepared = 0, applied = 0, revision = 0, fontRevision = 1;
    float scale = 0;
    HtmlFonts html;
    ImGuiContext *context = nullptr;
    ImGuiID hook = 0;
    void ForgetContext() {
        context = nullptr;
        hook = 0;
        fonts.clear();
        preparedFonts.clear();
        prepared = applied = 0;
        scale = 0;
        html = {};
        ++fontRevision;
        ResetDevImages();
        SetBoldFont(nullptr);
        SetVIFonts(nullptr, nullptr);
    }
    void Detach() {
        if (context && hook) ImGui::RemoveContextHook(context, hook);
        ForgetContext();
    }
    void Bind(ImGuiContext *value) {
        if (context == value) return;
        Detach();
        context = value;
        ImGuiContextHook shutdown;
        shutdown.Type = ImGuiContextHookType_Shutdown;
        shutdown.UserData = this;
        shutdown.Callback = [](ImGuiContext *, ImGuiContextHook *hook) {
            static_cast<State *>(hook->UserData)->ForgetContext();
        };
        hook = ImGui::AddContextHook(context, &shutdown);
    }
};
UiAppearance::UiAppearance() : m_State(new State) {}
UiAppearance::~UiAppearance() { Reset(); }
bool UiAppearance::Prepare(const EditorApplication &editor, std::string &error) try {
    auto &s = *m_State;
    auto *context = ImGui::GetCurrentContext();
    if (!context || context->IO.Fonts->Locked) {
        error = "Font atlas is not available between frames";
        return false;
    }
    s.Bind(context);
    if (s.prepared == editor.AppearanceRevision()) return true;
    auto theme = DefaultUiTheme();
    const auto &values = editor.ThemeValues("editor.theme");
    if (!CompileTheme(values, theme, error)) return false;
    const auto available = editor.Fonts();
    std::map<std::string, State::Font> prepared;
    for (const auto &entry : values)
        if (entry.first.compare(0, 5, "font.") == 0) {
            const auto selected = std::find_if(available.begin(), available.end(),
                                               [&](const auto &d) { return Text(d.id) == entry.second.text; });
            if (selected == available.end()) {
                error = "Theme font is unavailable: " + entry.second.text;
                return false;
            }
            const auto key = std::make_pair(Text(selected->id), Text(selected->provider));
            auto found = s.fonts.find(key);
            if (found == s.fonts.end()) {
                std::vector<std::vector<unsigned char>> data(selected->count);
                for (uint32_t i = 0; i < selected->count; ++i)
                    if (selected->files[i].size && !FontFile(Text(selected->files[i]), data[i])) {
                        error = "Cannot load font: " + Text(selected->files[i]);
                        return false;
                    }
                ImFont *font = nullptr;
                for (uint32_t i = 0; i < selected->count; ++i) {
                    ImFontConfig config;
                    config.SizePixels = selected->height;
                    config.MergeMode = i != 0;
                    config.DstFont = font;
                    if (data[i].empty())
                        font = context->IO.Fonts->AddFontDefaultVector(&config);
                    else {
                        // Copy to allocator-owned memory only after every source is validated.
                        auto *bytes = ImGui::MemAlloc(data[i].size());
                        if (!bytes) {
                            error = "Cannot allocate font data";
                            return false;
                        }
                        std::copy(data[i].begin(), data[i].end(), static_cast<unsigned char *>(bytes));
                        const auto name = std::filesystem::u8path(Text(selected->files[i])).filename().u8string();
                        ImFormatString(config.Name, IM_ARRAYSIZE(config.Name), "%s", name.c_str());
                        auto *added = context->IO.Fonts->AddFontFromMemoryTTF(bytes, int(data[i].size()),
                                                                              selected->height, &config);
                        if (!added) {
                            if (font) context->IO.Fonts->RemoveFont(font);
                            error = "Invalid font: " + Text(selected->files[i]);
                            return false;
                        }
                        if (!font) font = added;
                    }
                }
                found = s.fonts.emplace(key, State::Font{font, *selected}).first;
            }
            prepared.emplace(entry.first, found->second);
        }
    if (!PrepareDevImages(editor.Images(), values, error)) return false;
    s.preparedFonts = std::move(prepared);
    s.preparedTheme = theme;
    s.prepared = editor.AppearanceRevision();
    return true;
} catch (const std::exception &failure) {
    error = failure.what();
    return false;
} catch (...) {
    error = "UI resource preparation failed";
    return false;
}
bool UiAppearance::Apply(const EditorApplication &editor, float scale, std::string &error) {
    auto &s = *m_State;
    if (!std::isfinite(scale) || scale <= 0) {
        error = "Invalid UI scale";
        return false;
    }
    if (s.context == ImGui::GetCurrentContext() && s.applied == editor.AppearanceRevision() && s.scale == scale)
        return true;
    if (ImGui::GetCurrentContext() && ImGui::GetCurrentContext()->WithinFrameScope) {
        error = "UI appearance can only change between frames";
        return false;
    }
    if (!Prepare(editor, error)) return false;
    const auto font = [&](const char *role) -> State::Font {
        const auto it = s.preparedFonts.find(role);
        return it == s.preparedFonts.end() ? State::Font{} : it->second;
    };
    const auto body = font("font.body"), normal = font("font.vi-normal"), bold = font("font.vi-bold"),
               mono = font("font.monospace");
    if (body.face) {
        ImGui::GetIO().FontDefault = body.face;
        s.preparedTheme.style.FontSizeBase = body.desc.height;
    } else
        ImGui::GetIO().FontDefault = nullptr;
    SetBoldFont(font("font.bold").face);
    SetVIFonts(normal.face, bold.face, normal.face ? normal.desc.height : 12, bold.face ? bold.desc.height : 14,
               normal.face ? s.preparedTheme.viWidth[0] : 1, bold.face ? s.preparedTheme.viWidth[1] : 1);
    SetDevTheme(s.preparedTheme);
    ApplyDevTheme(ImGui::GetStyle(), scale);
    if (s.applied != editor.AppearanceRevision()) ApplyDevImages();
    if (s.html.normal != body.face || s.html.bold != bold.face || s.html.monospace != mono.face) ++s.fontRevision;
    s.html = {body.face, bold.face, mono.face, s.fontRevision};
    s.applied = editor.AppearanceRevision();
    s.scale = scale;
    ++s.revision;
    return true;
}
void UiAppearance::Reset() {
    m_State->Detach();
    *m_State = State{};
}
HtmlFonts UiAppearance::Fonts() const { return m_State->html; }
uint64_t UiAppearance::Revision() const { return m_State->revision; }
} // namespace vdev

#include "ui/controls/UiAppearance.h"
#include "ui/controls/DevTheme.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, const std::string &error) {
    if (!value) throw std::runtime_error(error);
}
} // namespace
int main() {
    using namespace vdev;
    try {
        EditorApplication editor([] { return static_cast<Document *>(nullptr); });
        const EditorString builtin[] = {{}};
        const auto file = std::filesystem::temp_directory_path() /
                          ("nemodev-invalid-appearance-font-" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ttf");
        struct Remove {
            std::filesystem::path file;
            ~Remove() {
                std::error_code e;
                std::filesystem::remove(file, e);
            }
        } remove{file};
        {
            std::ofstream output(file, std::ios::binary);
            output << "not a TrueType font";
        }
        const auto filename = file.u8string();
        const EditorString invalid[] = {EditorText(filename)};
        const EditorFontDesc fonts[] = {{sizeof(EditorFontDesc), EDITOR_TEXT("test.font"), EDITOR_TEXT("test.one"),
                                         EDITOR_TEXT("One"), builtin, 1, 14},
                                        {sizeof(EditorFontDesc), EDITOR_TEXT("test.font"), EDITOR_TEXT("test.two"),
                                         EDITOR_TEXT("Two"), builtin, 1, 18},
                                        {sizeof(EditorFontDesc), EDITOR_TEXT("test.font"), EDITOR_TEXT("test.invalid"),
                                         EDITOR_TEXT("Invalid"), invalid, 1, 14}};
        const double spacing[] = {11, 6};
        EditorValue values[] = {
            {EDITOR_TEXT("font.body"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("test.font")},
            {EDITOR_TEXT("color.schematic.canvas"), EDITOR_INTEGER_VALUE, {}, {}, 0x112233ff},
            {EDITOR_TEXT("color.schematic.trace"), EDITOR_INTEGER_VALUE, {}, {}, 0xaa00bbff},
            {EDITOR_TEXT("color.selection.fill"), EDITOR_INTEGER_VALUE, {}, {}, 0x304050ff},
            {EDITOR_TEXT("metric.dialog.title-height"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, 31},
            {EDITOR_TEXT("metric.group.rounding"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, 11},
            {EDITOR_TEXT("metric.setup.coordinate-scale"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, 1.5},
            {EDITOR_TEXT("metric.setup.spacing"), EDITOR_REAL_LIST}};
        values[7].reals = {spacing, 2};
        const EditorValue invalidScale{EDITOR_TEXT("metric.setup.coordinate-scale"), EDITOR_REAL_VALUE};
        const EditorThemeDesc themes[] = {{sizeof(EditorThemeDesc),
                                           EDITOR_TEXT("editor.theme"),
                                           EDITOR_TEXT("test.one"),
                                           EDITOR_TEXT("Theme"),
                                           {},
                                           values,
                                           uint32_t(std::size(values))},
                                          {sizeof(EditorThemeDesc), EDITOR_TEXT("editor.theme"),
                                           EDITOR_TEXT("test.invalid-metrics"), EDITOR_TEXT("Invalid Metrics"),
                                           EDITOR_TEXT("test.one"), &invalidScale, 1}};
        EditorModuleDesc module{};
        module.size = sizeof(module);
        module.version = EDITOR_VERSION;
        module.id = EDITOR_TEXT("test.appearance");
        module.fonts = fonts;
        module.fontCount = 3;
        module.themes = themes;
        module.themeCount = uint32_t(std::size(themes));
        std::string error;
        Check(editor.Register(module, error), error);
        EditorConfiguration config;
        config.providers = {{"font", "test.font", "test.one"}, {"theme", "editor.theme", "test.one"}};
        Check(editor.Activate({}, error, config), error);
        UiAppearance appearance;
        for (int context = 0; context < 3; ++context) {
            auto *gui = ImGui::CreateContext();
            Check(appearance.Apply(editor, 1, error), error);
            auto *first = ImGui::GetIO().FontDefault;
            const auto padding = ImGui::GetStyle().WindowPadding;
            Check(first && first->OwnerAtlas == ImGui::GetIO().Fonts, "font was reused across atlas lifetimes");
            const auto &page = CurrentDevTheme();
            Check(page.schematic.canvas == IM_COL32(17, 34, 51, 255) &&
                      page.schematic.trace == IM_COL32(170, 0, 187, 255) &&
                      page.selectionFill == IM_COL32(48, 64, 80, 255) && page.dialogTitleHeight == 31 &&
                      page.groupRounding == 11 && page.setupCoordinateScale == 1.5f && page.setupSpacing.x == 11 &&
                      page.setupSpacing.y == 6,
                  "page colors and geometry did not use the selected theme");
            for (int i = 0; i < 6; ++i) {
                config.providers[0].provider = i % 2 ? "test.one" : "test.two";
                auto *previous = ImGui::GetIO().FontDefault;
                Check(editor.Configure(config, error) && appearance.Prepare(editor, error), error);
                Check(ImGui::GetIO().FontDefault == previous, "preparation published a font");
                Check(appearance.Apply(editor, 2, error), error);
                Check(ImGui::GetStyle().FontScaleDpi == 2, "font DPI was not applied");
                Check(appearance.Apply(editor, 1, error), error);
                Check(ImGui::GetStyle().WindowPadding.x == padding.x, "DPI changes accumulated theme sizes");
            }
            Check(ImGui::GetIO().Fonts->Fonts.Size == 2, "switching providers grew the font atlas every time");
            const auto *active = ImGui::GetIO().FontDefault;
            config.providers[0].provider = "test.invalid";
            Check(editor.Configure(config, error) && !appearance.Prepare(editor, error), "corrupt font was accepted");
            Check(ImGui::GetIO().FontDefault == active && ImGui::GetIO().Fonts->Fonts.Size == 2,
                  "failed font preparation changed the live atlas");
            config.providers[0].provider = "test.one";
            Check(editor.Configure(config, error) && appearance.Apply(editor, 1, error), error);
            config.providers[1].provider = "test.invalid-metrics";
            Check(editor.Configure(config, error) && !appearance.Prepare(editor, error),
                  "zero Setup coordinate scale was accepted");
            Check(CurrentDevTheme().setupCoordinateScale == 1.5f && ImGui::GetIO().FontDefault == first,
                  "invalid geometry changed the current appearance");
            config.providers[1].provider = "test.one";
            Check(editor.Configure(config, error) && appearance.Apply(editor, 1, error), error);
            ImGui::DestroyContext(gui);
            Check(appearance.Fonts().normal == nullptr && BoldFont() == nullptr, "context shutdown retained a font");
        }
        appearance.Reset();
        editor.Stop();
        std::cout << "Appearance cache, corrupt fonts, DPI and context lifetimes passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "application/EditorApplication.h"
#include "application/EditorProfile.h"
#include "ui/controls/DevTheme.h"
#include "core/document/Document.h"
#include "core/runtime/CkRuntimeHost.h"
#include <array>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
EditorModuleDesc Root(EditorThemeDesc &theme, EditorFontDesc &font, EditorImageDesc &image) {
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("appearance.classic");
    module.themes = &theme;
    module.themeCount = 1;
    module.fonts = &font;
    module.fontCount = 1;
    module.images = &image;
    module.imageCount = 1;
    return module;
}
} // namespace
void TestEditorAppearance(vdev::CkRuntimeHost &runtime, const std::string &file) {
    using namespace vdev;
    const auto classic = DefaultDevTheme();
    SetDevTheme(classic);
    ImGuiStyle scaled;
    ApplyDevTheme(scaled, 2);
    Check(scaled.WindowPadding.x == classic.style.WindowPadding.x * 2, "theme DPI scale was not applied");
    ApplyDevTheme(scaled, 1);
    Check(scaled.WindowPadding.x == classic.style.WindowPadding.x &&
              scaled.FramePadding.y == classic.style.FramePadding.y && scaled.FontScaleDpi == 1 &&
              classic.tabText == IM_COL32(236, 236, 236, 255),
          "repeated theme scaling accumulated sizes");
    Document document(runtime);
    std::string error;
    Check(document.New(&error), error);
    EditorApplication app([&] { return &document; });
    const EditorString files[] = {{}};
    EditorFontDesc font{
        sizeof(font), EDITOR_TEXT("ui.font.body"), EDITOR_TEXT("builtin.classic"), EDITOR_TEXT("Text"), files, 1, 14};
    std::array<unsigned char, 16> pixels{};
    EditorImageDesc image{
        sizeof(image), EDITOR_TEXT("ui.bitmap.777"), EDITOR_TEXT("builtin.classic"), EDITOR_TEXT("Icon"), 2, 2,
        pixels.data(), uint32_t(pixels.size())};
    double padding[] = {6, 6};
    EditorValue values[] = {{EDITOR_TEXT("color.window-bg"), EDITOR_INTEGER_VALUE, {}, {}, 0xb0b0b0ff},
                            {EDITOR_TEXT("font.body"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("ui.font.body")},
                            {EDITOR_TEXT("metric.window-padding"), EDITOR_REAL_LIST}};
    values[2].reals = {padding, 2};
    EditorThemeDesc theme{sizeof(theme),
                          EDITOR_TEXT("editor.theme"),
                          EDITOR_TEXT("builtin.classic"),
                          EDITOR_TEXT("Classic"),
                          {},
                          values,
                          3};
    auto module = Root(theme, font, image);
    Check(app.Register(module, error) && app.Load(file, error), error);
    values[0].integer = 0;
    padding[0] = 99;
    pixels[0] = 99;
    EditorConfiguration config;
    config.providers = {{"theme", "editor.theme", "builtin.classic"},
                        {"font", "ui.font.body", "builtin.classic"},
                        {"image", "ui.bitmap.777", "builtin.classic"}};
    Check(app.Activate({}, error, config), error);
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    const auto revision = app.AppearanceRevision();
    Check(app.ThemeValues("editor.theme").at("color.window-bg").integer == 0xb0b0b0ff &&
              app.ThemeValues("editor.theme").at("metric.window-padding").reals.front() == 6 &&
              app.Images().front().pixels[0] == 0,
          "appearance declarations were not copied");
    Check(app.Configure(config, error) && app.AppearanceRevision() == revision,
          "unrelated configuration invalidated appearance");
    for (auto &choice : config.providers) choice.provider = "example.appearance";
    Check(app.Configure(config, error), error);
    const auto alternate = app.AppearanceRevision();
    const auto publicValues = app.Query("appearance.values", {app.Scope()});
    Check(publicValues.status == EDITOR_OK && publicValues.fields.size() == 1 && !publicValues.fields[0].fields.empty(),
          "external views cannot read theme values");
    uint64_t publicVersion = 0;
    Check(app.QueryRevision("appearance.values", publicVersion) == EDITOR_OK && publicVersion != 0,
          "appearance values have no published version");
    Check(alternate != revision && app.ThemeValues("editor.theme").at("color.window-bg").integer == 0x24364aff &&
              app.ThemeValues("editor.theme").at("metric.window-padding").reals.front() == 6 &&
              app.Fonts().front().height == 16 && app.Images().front().pixels[0] == 201,
          "external appearance did not inherit roles or replace resources");
    auto rejected = config;
    rejected.providers[0].provider = "example.cycle";
    Check(!app.Configure(rejected, error) && app.AppearanceRevision() == alternate &&
              app.Configuration().providers[0].provider == "example.appearance",
          "theme cycle replaced the active configuration");
    rejected = config;
    rejected.providers[2].provider = "missing.provider";
    Check(!app.Configure(rejected, error) && app.AppearanceRevision() == alternate,
          "unknown resource provider was accepted");
    bool refused = false;
    std::thread other(
        [&] { refused = !app.Configure(config, error) && app.Themes().empty() && app.AppearanceRevision() == 0; });
    other.join();
    Check(refused, "appearance operations escaped the editor thread");
    Check(app.ProviderModule({"theme", "editor.theme", "example.appearance"}) == "example.appearance" &&
              app.ProviderModule({"font", "ui.font.body", "example.appearance"}) == "example.appearance" &&
              app.ProviderModule({"image", "ui.bitmap.777", "example.appearance"}) == "example.appearance",
          "resource ownership was lost");
    EditorProfile profile;
    Check(profile.Capture({}, config, {}, [&](const auto &p) { return app.ProviderModule(p); }, error), error);
    std::string encoded;
    Check(profile.Encode(encoded, error), error);
    EditorProfile roundtrip;
    Check(roundtrip.Decode(encoded, "/tmp/appearance.json", error), error);
    Check(roundtrip.Contributions().providers.size() == 3, "resource choices did not round-trip");
    Check(roundtrip.SetModule({"example.appearance", false}, error) && roundtrip.Contributions().providers.empty(),
          "disabled resource module choices were not deferred");
    Check(roundtrip.SetModule({"example.appearance", true}, error) && roundtrip.Contributions().providers.size() == 3,
          "resource choices were lost while the module was disabled");
    Check(document.History().UndoCount() == 0 && !document.Dirty(), "appearance edited the document");
    app.Stop();
    // Reject malformed resources before publishing any contribution.
    for (int bad = 0; bad < 8; ++bad) {
        EditorApplication invalid([&] { return &document; });
        auto badTheme = theme;
        auto badFont = font;
        auto badImage = image;
        auto badModule = Root(badTheme, badFont, badImage);
        if (bad == 0) badTheme.size = 0;
        if (bad == 1) badFont.height = std::numeric_limits<float>::quiet_NaN();
        if (bad == 2) badFont.count = 0;
        if (bad == 3) badImage.byteCount = 15;
        if (bad == 4) badImage.width = UINT32_MAX;
        if (bad == 5) badImage.pixels = nullptr;
        if (bad == 6) badModule.version = EDITOR_VERSION - 1;
        if (bad == 7) badTheme.values = nullptr;
        Check(!invalid.Register(badModule, error), "malformed appearance resource was registered");
    }
}

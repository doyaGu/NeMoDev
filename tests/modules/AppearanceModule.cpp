#include "editor/Editor.h"
#include <array>
#include <cstddef>

namespace {
const EditorString builtin[] = {{}};
const EditorString missing[] = {EDITOR_TEXT("missing-appearance-font.ttf")};
const EditorFontDesc fonts[] = {{sizeof(EditorFontDesc), EDITOR_TEXT("ui.font.body"), EDITOR_TEXT("example.appearance"),
                                 EDITOR_TEXT("Example Text"), builtin, 1, 16},
                                {sizeof(EditorFontDesc), EDITOR_TEXT("ui.font.body"),
                                 EDITOR_TEXT("example.invalid-font"), EDITOR_TEXT("Missing Font"), missing, 1, 16}};
const EditorValue values[] = {
    {EDITOR_TEXT("color.window-bg"), EDITOR_INTEGER_VALUE, {}, {}, 0x24364aff},
    {EDITOR_TEXT("color.chrome.window-bg"), EDITOR_INTEGER_VALUE, {}, {}, 0x182838ff},
    {EDITOR_TEXT("color.schematic.canvas"), EDITOR_INTEGER_VALUE, {}, {}, 0x536475ff},
    {EDITOR_TEXT("color.control.text"), EDITOR_INTEGER_VALUE, {}, {}, 0xd5eaffff},
    {EDITOR_TEXT("color.preview.curve"), EDITOR_INTEGER_VALUE, {}, {}, 0xc9217bff},
    {EDITOR_TEXT("color.snapshot.background"), EDITOR_INTEGER_VALUE, {}, {}, 0x324354ff},
    {EDITOR_TEXT("metric.dialog.title-height"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, 31},
    {EDITOR_TEXT("metric.dialog.rounding"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, 10},
    {EDITOR_TEXT("metric.setup.coordinate-scale"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, 1.25},
    {EDITOR_TEXT("metric.setup.column-width"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, 260},
    {EDITOR_TEXT("metric.setup.sidebar-width"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, 120},
    {EDITOR_TEXT("metric.setup.label-width"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, 84}};
const EditorValue invalidLayout{EDITOR_TEXT("metric.setup.column-width"), EDITOR_REAL_VALUE};
const EditorThemeDesc themes[] = {
    {sizeof(EditorThemeDesc), EDITOR_TEXT("editor.theme"), EDITOR_TEXT("example.appearance"),
     EDITOR_TEXT("Example Theme"), EDITOR_TEXT("builtin.classic"), values, uint32_t(std::size(values))},
    {sizeof(EditorThemeDesc), EDITOR_TEXT("editor.theme"), EDITOR_TEXT("example.cycle"), EDITOR_TEXT("Cycle"),
     EDITOR_TEXT("example.cycle"), nullptr, 0},
    {sizeof(EditorThemeDesc), EDITOR_TEXT("editor.theme"), EDITOR_TEXT("example.invalid-layout"),
     EDITOR_TEXT("Invalid Layout"), EDITOR_TEXT("builtin.classic"), &invalidLayout, 1}};
std::array<unsigned char, 17 * 19 * 4> pixels;
EditorImageDesc image;
} // namespace
extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host,
                                                                       EditorModuleDesc *module) {
    if (!host || host->version != EDITOR_VERSION || !module || module->size != sizeof(*module)) return EDITOR_INVALID;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 201;
        pixels[i + 1] = 37;
        pixels[i + 2] = 175;
        pixels[i + 3] = 255;
    }
    image = {sizeof(EditorImageDesc),
             EDITOR_TEXT("ui.bitmap.777"),
             EDITOR_TEXT("example.appearance"),
             EDITOR_TEXT("Example Explorer"),
             17,
             19,
             pixels.data(),
             uint32_t(pixels.size())};
    *module = {};
    module->size = sizeof(*module);
    module->version = EDITOR_VERSION;
    module->id = EDITOR_TEXT("example.appearance");
    module->themes = themes;
    module->themeCount = uint32_t(std::size(themes));
    module->fonts = fonts;
    module->fontCount = 2;
    module->images = &image;
    module->imageCount = 1;
    return EDITOR_OK;
}

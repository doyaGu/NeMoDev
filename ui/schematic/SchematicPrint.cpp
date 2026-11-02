#include "ui/schematic/SchematicCanvas.h"
#include "ui/controls/DevTheme.h"
#include "ui/render/UiRenderer.h"
#include "ui/schematic/Snapshots.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace vdev {
namespace {
class PrintedSchematic final : public SchematicPrintContent {
  public:
    struct Script {
        SchematicModel model;
        float offset, scale, top;
        Point anchor;
    };
    std::vector<Script> scripts;
    SchematicStyle style;
    DrawOptions options;
    bool Render(float x, float y, int width, int height, float scale, const SchematicPrint &print,
                std::vector<unsigned char> &pixels, std::string &error) const override {
        ImDrawList list(ImGui::GetDrawListSharedData());
        list._ResetForNewFrame();
        list._SetPixelDensity(1.0f); // RenderPage uses one UI unit per output texel.
        const ImVec2 size{float(width), float(height)};
        list.PushClipRect(ImVec2(0, 0), size);
        list.PushTexture(ImGui::GetIO().Fonts->TexRef);
        list.AddRectFilled(ImVec2(0, 0), size, print.fillBody ? style.canvas : IM_COL32(255, 255, 255, 255));
        DrawOptions draw = options;
        draw.style = &style;
        draw.print = &print;
        draw.clipMin = ImVec2(0, 0);
        draw.clipMax = size;
        ViewTransform page;
        page.origin = ImVec2(-x * scale, -y * scale);
        page.zoom = scale;
        page.panY = scripts.empty() ? 0 : scripts.front().top;
        for (const auto &script : scripts) {
            ViewTransform body;
            body.origin = page.ToScreen(script.anchor.h, script.anchor.v + script.offset);
            body.zoom = scale * script.scale;
            body.panX = script.anchor.h;
            body.panY = script.anchor.v + script.offset;
            draw.scriptZoom = script.scale;
            DrawScript(&list, script.model, script.offset, page, body, draw);
        }
        ImDrawData data;
        data.Valid = true;
        data.DisplayPos = ImVec2(0, 0);
        data.DisplaySize = size;
        data.FramebufferScale = ImVec2(1, 1);
        ImVector<ImTextureData *> textures = ImGui::GetPlatformIO().Textures;
        for (auto *texture : ImGui::GetIO().Fonts->TexList)
            if (!textures.contains(texture)) textures.push_back(texture);
        data.Textures = &textures;
        data.AddDrawList(&list);
        if (!MainRenderer().RenderPage(&data, width, height, pixels)) {
            error = "Cannot render a Schematic print tile";
            return false;
        }
        for (size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]);
            pixels[i + 3] = 255;
        }
        return true;
    }
};

} // namespace
std::unique_ptr<SchematicPrintContent> SchematicCanvas::CapturePrint() const {
    if (m_Views.empty() || !m_Font) return {};
    auto snapshot = std::make_unique<PrintedSchematic>();
    snapshot->style = m_Style;
    snapshot->options.font = m_Font;
    snapshot->options.boldFont = BoldFont();
    snapshot->options.fontSize = 13.0f;
    snapshot->options.showLocalParams = m_Navigation.options.showLocalParams;
    snapshot->options.showLinkInfo = m_Navigation.options.showLinkInfo;
    snapshot->options.showControlPoints = m_Navigation.options.showControlPoints;
    snapshot->options.showPriorities = m_Navigation.options.showPriorities;
    snapshot->options.showHeaders = m_Navigation.options.showHeaders;
    for (const auto &view : m_Views) {
        PrintedSchematic::Script script{view.model, view.offsetY, view.scale, view.stackTop, view.anchor};
        if (view.model.script.open)
            snapshot->width = std::max(snapshot->width, view.model.script.bounds.hSize - 100.0f);
        snapshot->scripts.push_back(std::move(script));
    }
    snapshot->width = std::max(140.0f, snapshot->width);
    snapshot->height = m_Views.back().stackTop + m_Views.back().model.script.bounds.vSize - m_Views.front().stackTop;
    return snapshot;
}
} // namespace vdev

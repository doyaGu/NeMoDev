#include "core/document/OutputFiles.h"
#include "ui/app/EditorApp.h"
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
#ifdef VIRTOOLSDEV_UI_TESTS
class PngPages final : public PrintDestination {
  public:
    explicit PngPages(std::string directory) : directory(std::move(directory)) {}
    ~PngPages() override { Cancel(); }
    const PrinterSettings &Settings() const override { return settings; }
    bool ImageFiles() const override { return true; }
    std::string SubmittedMessage(const std::string &) const override {
        return std::to_string(written.size()) + " page(s) written to " + directory;
    }
    bool PreparePages(int count, int w, int h, std::string &error) override {
        if (uint64_t(w) * h * 4 > 512u * 1024u * 1024u) {
            error = "Print image is too large";
            return false;
        }
        width = w;
        height = h;
        std::error_code failure;
        std::filesystem::create_directories(std::filesystem::u8path(directory), failure);
        if (failure) {
            error = failure.message();
            return false;
        }
        files = std::make_unique<OutputFiles>(report);
        for (int i = 1; i <= count; ++i) {
            auto path =
                files->Add((std::filesystem::u8path(directory) / ("page" + std::to_string(i) + ".png")).u8string());
            if (path.empty()) {
                error = Errors();
                return false;
            }
            staged.push_back(std::move(path));
        }
        return true;
    }
    PrinterStartStatus BeginDocument(const std::string &, std::string &) override {
        return PrinterStartStatus::Started;
    }
    bool BeginPage(std::string &) override {
        pixels.assign(size_t(width) * height * 4, 255);
        return page < staged.size();
    }
    bool DrawTile(const unsigned char *bgra, int w, int h, int left, int top, int right, int bottom,
                  std::string &error) override {
        if (left < 0 || top < 0 || right > width || bottom > height || right - left != w || bottom - top != h) {
            error = "Invalid print tile";
            return false;
        }
        for (int y = 0; y < h; ++y)
            std::memcpy(pixels.data() + (size_t(top + y) * width + left) * 4, bgra + size_t(y) * w * 4, size_t(w) * 4);
        return true;
    }
    bool EndPage(std::string &error) override {
        if (page >= staged.size() || !WriteBgraPng(staged[page], width, height, pixels.data())) {
            error = "Cannot write the Schematic print page";
            return false;
        }
        std::vector<unsigned char> verified;
        int w = 0, h = 0;
        if (!ReadImageRgba(staged[page], verified, w, h) || w != width || h != height ||
            verified.size() != pixels.size()) {
            error = "Cannot verify the Schematic print page";
            return false;
        }
        ++page;
        return true;
    }
    bool Submit(std::string &job, std::string &error) override {
        if (page != staged.size() || !files || !files->Publish(written)) {
            error = Errors();
            return false;
        }
        job = directory;
        return true;
    }
    void Cancel() override {
        files.reset();
        pixels.clear();
    }
    std::vector<std::string> WrittenFiles() const override { return written; }

  private:
    std::string Errors() const {
        std::string text;
        for (const auto &item : report.errors) {
            if (!text.empty()) text += "; ";
            text += item;
        }
        for (const auto &item : report.notes) {
            if (!text.empty()) text += "; ";
            text += item;
        }
        return text.empty() ? "Cannot publish print pages" : text;
    }
    PrinterSettings settings;
    std::string directory;
    SaveReport report;
    std::unique_ptr<OutputFiles> files;
    std::vector<std::string> staged, written;
    std::vector<unsigned char> pixels;
    int width = 0, height = 0;
    size_t page = 0;
};
#endif
} // namespace
std::unique_ptr<SchematicPrintContent> SchematicCanvas::CapturePrint() const {
    if (m_Views.empty() || !m_Font) return {};
    auto snapshot = std::make_unique<PrintedSchematic>();
    snapshot->style = m_State.style;
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
std::unique_ptr<SchematicPrintContent> EditorApp::CapturePrint(std::string &error) {
    auto snapshot = m_Schematics.Current().CapturePrint();
    if (!snapshot) error = "No visible Script to print";
    return snapshot;
}
PrintChoice EditorApp::ChoosePrinter(const std::string &directory) {
#ifdef VIRTOOLSDEV_UI_TESTS
    if (!directory.empty()) return {PrinterSelectionStatus::Selected, std::make_shared<PngPages>(directory), {}};
#else
    if (!directory.empty()) return {PrinterSelectionStatus::Failed, {}, "Unsupported print destination"};
#endif
    auto selection = Printer::Select(m_Window);
    return {selection.status, std::move(selection.printer), std::move(selection.error)};
}
} // namespace vdev

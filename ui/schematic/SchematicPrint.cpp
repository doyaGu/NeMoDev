#include "ui/app/EditorApp.h"
#include "ui/render/UiRenderer.h"
#include "ui/controls/DevControls.h"
#include "core/runtime/TextEncoding.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace vdev {
namespace {
int Ceil(float total, int part) { return std::max(1, int(std::ceil(total / std::max(1, part)))); }
bool RenderPage(const std::function<void(ImDrawList *)> &draw, int width, int height, std::vector<unsigned char> &pixels) {
    ImDrawList list(ImGui::GetDrawListSharedData());
    list._ResetForNewFrame();
    list.PushClipRect(ImVec2(0,0), ImVec2(float(width),float(height)));
    list.PushTexture(ImGui::GetIO().Fonts->TexRef);
    draw(&list);
    ImDrawData data; data.Valid = true;
    data.DisplayPos = ImVec2(0,0); data.DisplaySize = ImVec2(float(width),float(height));
    data.FramebufferScale = ImVec2(1,1);
    ImVector<ImTextureData *> textures = ImGui::GetPlatformIO().Textures;
    for (ImTextureData *texture : ImGui::GetIO().Fonts->TexList)
        if (!textures.contains(texture)) textures.push_back(texture);
    data.Textures = &textures; data.AddDrawList(&list);
    if (!MainRenderer().RenderPage(&data, width, height, pixels)) return false;
    for (size_t i = 0; i < pixels.size(); i += 4) { std::swap(pixels[i], pixels[i+2]); pixels[i+3] = 255; }
    return true;
}
}
void EditorApp::PrintSchematic(
#ifdef VIRTOOLSDEV_UI_TESTS
    const std::string &pngFolder
#endif
) {
    if (!m_State.HasDocument() || m_Print.open) return;
    if (m_State.document->Scripts().empty()) { m_State.Log(LogLevel::Error, "No Script to print"); return; }
    PrintJob job;
    if (!m_Canvas.PrintSize(job.width, job.height)) { m_State.Log(LogLevel::Error, "No visible Script to print"); return; }
    if (job.height <= 0.0f) return;
    PrinterSettings settings;
#ifdef VIRTOOLSDEV_UI_TESTS
    job.pngFolder = pngFolder;
    if (pngFolder.empty())
#endif
    {
        PrinterSelection selection = Printer::Select(m_Window);
        if (selection.status == PrinterSelectionStatus::Failed) m_State.Log(LogLevel::Error, selection.error);
        if (selection.status == PrinterSelectionStatus::Cancelled) m_State.Log(LogLevel::Info, "Print cancelled");
        if (selection.status != PrinterSelectionStatus::Selected) return;
        job.printer = std::move(selection.printer); settings = job.printer->Settings();
    }
    for (int axis = 0; axis < 2; ++axis) {
        job.paper[axis] = settings.paper[axis];
        const int mm = std::max(1, job.paper[axis]/10 - 10);
        job.device[axis] = std::max(1, int(mm * settings.dpi[axis] / 25.4));
        job.resolution[axis] = 4 * mm;
    }
    job.width = std::max(job.width, float(job.resolution[0]));
    job.pages[0] = Ceil(job.width, job.resolution[0]); job.pages[1] = Ceil(job.height, job.resolution[1]);
    job.open = true; m_Print = std::move(job);
    const int values[6] = {m_Print.perMm[0], m_Print.perMm[1], m_Print.resolution[0], m_Print.resolution[1], m_Print.pages[0], m_Print.pages[1]};
    for (int i = 0; i < 6; ++i) m_Print.text[i] = std::to_string(values[i]);
}

void EditorApp::PrintParametersDialog() {
    const char *id = "###PrintParameters";
    if (!m_Print.open) {
        if (ImGui::BeginPopupModal(id)) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
        return;
    }
    if (m_FocusSchematic <= 0 && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 client(333.0f * 1.5f * s, 111.0f * 1.625f * s + 26.0f * s);
    ImVec2 o;
    if (!vi::BeginDialog(id, "Schematic Print  Parameters", client, s, o)) return;
    auto P = [&](float x, float y) { return ImVec2(o.x + x * 1.5f * s, o.y + y * 1.625f * s); };
    vi::Panel(ImVec2(o.x + 2.0f * s, o.y + 2.0f * s), ImVec2(o.x + client.x - 2.0f * s, P(0, 111).y), s);
    vi::Text(P(40, 6), P(96, 14), "pixel per mm");
    vi::Text(P(125, 6), P(233, 14), "Resolution to print (for 1 page)");
    vi::Text(P(242, 6), P(284, 14), "Pages");
    vi::Text(P(2, 24), P(34, 32), "Horizontal");
    vi::Text(P(2, 39), P(26, 47), "Vertical");
    static const float EDITS[6][2] = {{40, 24}, {40, 39}, {139, 24}, {139, 39}, {242, 24}, {242, 39}};
    static const char *const IDS[6] = {"##perMmH", "##perMmV", "##resH", "##resV", "##pagesH", "##pagesV"};
    PrintJob &job = m_Print;
    for (int i = 0; i < 6; ++i) {
        if (!vi::Edit(IDS[i], P(EDITS[i][0], EDITS[i][1]), P(EDITS[i][0] + 76, EDITS[i][1] + 12), job.text[i])) continue;
        const int value = std::max(1, std::atoi(job.text[i].c_str()));
        const int axis = i % 2;
        const int mm = std::max(1, job.paper[axis] / 10 - 10);
        const float total = axis ? job.height : job.width;
        if (i < 2) {
            job.perMm[axis] = value;
            job.resolution[axis] = value * mm;
            job.pages[axis] = Ceil(total, job.resolution[axis]);
        } else if (i < 4) {
            job.resolution[axis] = value;
            job.perMm[axis] = value / mm;
            job.pages[axis] = Ceil(total, value);
        } else {
            job.pages[axis] = value;
            job.resolution[axis] = std::max(1, static_cast<int>(total) / value);
            job.perMm[axis] = job.resolution[axis] / mm;
        }
        const int values[6] = {job.perMm[0], job.perMm[1], job.resolution[0], job.resolution[1], job.pages[0], job.pages[1]};
        for (int k = 0; k < 6; ++k)
            if (k != i) job.text[k] = std::to_string(values[k]);
    }
    vi::Text(P(242, 57), P(306, 65), "Total pages count =");
    vi::Text(P(315, 57), P(333, 65), std::to_string(job.pages[0] * job.pages[1]).c_str());
    const float box = 8.0f * 1.5f * s;
    struct Option {
        float y;
        const char *id, *label;
        bool *value;
    } options[] = {{57, "##fillHeader", "Fill Script Header", &m_PrintOptions.fillHeader},
                   {68, "##fillBody", "Fill Script Body", &m_PrintOptions.fillBody},
                   {79, "##fillShapes", "Fill Shapes", &m_PrintOptions.fillShapes},
                   {90, "##bitmaps", "Print Bitmaps", &m_PrintOptions.bitmaps},
                   {101, "##colorLinks", "Use Color for Links", &m_PrintOptions.colorLinks}};
    for (const Option &option : options) {
        vi::Check(option.id, P(6, option.y), box, *option.value);
        vi::Text(ImVec2(P(6, option.y).x + box + 3.0f * s, P(6, option.y).y), P(100, option.y + 10), option.label);
    }
    const float by = o.y + client.y - 23.0f * s;
    const bool cancel = vi::DialogButton("Cancel", ImVec2(o.x + client.x - 70.0f * s, by), s, ImGuiKey_Escape);
    const bool ok = vi::DialogButton("OK", ImVec2(o.x + client.x - 140.0f * s, by), s, ImGuiKey_Enter);
    if (ok || cancel) ImGui::CloseCurrentPopup();
    vi::EndDialog();
    if (cancel) m_State.Log(LogLevel::Info, "Print cancelled");
    if (ok) PrintPages();
    if (ok || cancel) EndPrint();
}

// Preserve the original column/row order and 2048-pixel tiling. Printer
// adapters own submission and native cleanup; PNG capture uses the same tiles.
void EditorApp::PrintPages() {
    PrintJob &job = m_Print;
    const int columns = std::max(1, job.pages[0]), count = columns * std::max(1, job.pages[1]);
    const int resolution[2] = {std::max(1, job.resolution[0]), std::max(1, job.resolution[1])};
    constexpr int TILE = 2048;
    float scale = float(job.device[0]) / resolution[0];
#ifdef VIRTOOLSDEV_UI_TESTS
    if (!job.printer) scale = std::min(scale, 2.0f);
#endif
    scale = std::max(scale, 1.0f);
    const int width = std::max(1, int(resolution[0] * scale + 0.5f)), height = std::max(1, int(resolution[1] * scale + 0.5f));
    const float toDeviceX = job.printer ? float(job.device[0])/width : 1.0f;
    const float toDeviceY = job.printer ? float(job.device[1])/height : 1.0f;
    std::string error;
    if (job.printer) {
        const PrinterStartStatus started = job.printer->BeginDocument("Schematic View", error);
        if (started == PrinterStartStatus::Cancelled) { m_State.Log(LogLevel::Info, "Print cancelled"); return; }
        if (started == PrinterStartStatus::Failed) { m_State.Log(LogLevel::Error, error); return; }
    }
#ifdef VIRTOOLSDEV_UI_TESTS
    int written = 0;
#endif
    bool aborted = false;
    for (int page = 0; page < count && !aborted; ++page) {
        const float x = float(resolution[0] * (page % columns)), y = float(resolution[1] * (page / columns));
#ifdef VIRTOOLSDEV_UI_TESTS
        std::vector<unsigned char> whole;
        if (!job.printer) whole.assign(static_cast<size_t>(width)*height*4, 255);
#endif
        if (job.printer && !job.printer->BeginPage(error)) { aborted = true; break; }
        for (int ty = 0; ty < height && !aborted; ty += TILE)
            for (int tx = 0; tx < width && !aborted; tx += TILE) {
                const int tw = std::min(TILE, width-tx), th = std::min(TILE, height-ty);
                std::vector<unsigned char> pixels;
                if (!RenderPage([&](ImDrawList *list) {
                    m_Canvas.DrawPrintPage(list, x + tx/scale, y + ty/scale, ImVec2(tw/scale,th/scale), scale, m_PrintOptions);
                }, tw, th, pixels)) { error = "Cannot render a Schematic print tile"; aborted = true; break; }
                if (job.printer) {
                    const int left = int(tx*toDeviceX+0.5f), top = int(ty*toDeviceY+0.5f);
                    const int right = int((tx+tw)*toDeviceX+0.5f), bottom = int((ty+th)*toDeviceY+0.5f);
                    if (!job.printer->DrawTile(pixels.data(), tw, th, left, top, right, bottom, error)) aborted = true;
                }
#ifdef VIRTOOLSDEV_UI_TESTS
                else {
                    for (int row = 0; row < th; ++row)
                        std::memcpy(whole.data() + (static_cast<size_t>(ty+row)*width+tx)*4,
                                    pixels.data() + static_cast<size_t>(row)*tw*4, static_cast<size_t>(tw)*4);
                }
#endif
            }
        if (!aborted && job.printer && !job.printer->EndPage(error)) aborted = true;
#ifdef VIRTOOLSDEV_UI_TESTS
        if (!aborted && !job.printer) {
            const auto directory = std::filesystem::u8path(job.pngFolder);
            std::error_code failure; std::filesystem::create_directories(directory, failure);
            const auto file = directory / ("page" + std::to_string(page+1) + ".png");
            if (failure || !WriteBgraPng(file.u8string(), width, height, whole.data())) {
                error = "Cannot write the Schematic print page"; aborted = true;
            } else ++written;
        }
#endif
    }
    if (aborted) {
        if (job.printer) job.printer->Cancel();
        m_State.Log(LogLevel::Error, error); return;
    }
    if (job.printer) {
        std::string identifier;
        if (!job.printer->Submit(identifier, error)) { job.printer->Cancel(); m_State.Log(LogLevel::Error, error); }
        else m_State.Log(LogLevel::Info, "Printer job submitted: " + identifier);
    }
#ifdef VIRTOOLSDEV_UI_TESTS
    else m_State.Log(LogLevel::Info, std::to_string(written) + " page(s) written to " + job.pngFolder);
#endif
}
void EditorApp::EndPrint() { m_Print = PrintJob(); }
} // namespace vdev

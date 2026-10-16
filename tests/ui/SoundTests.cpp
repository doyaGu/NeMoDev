// "sound": the Sound and Array Setups on the sample opened from the command
// line (SoundIsland.cmo: sounds and arrays loaded with it; the samples of
// the library's repository have neither). The sound's Loop, Gain and Type,
// an array's cell, each undone.

#include <cmath>
#include <cstring>
#include <string>

#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

// A cell's text (CKDataArray::GetElementStringValue).
std::string CellText(CKDataArray *array, int row, int column) {
    const int size = array->GetElementStringValue(row, column, nullptr, 0);
    if (size <= 0 || size > 4096) return std::string();
    std::string text(static_cast<size_t>(size), '\0');
    array->GetElementStringValue(row, column, &text[0], static_cast<int>(text.size()));
    text.resize(std::strlen(text.c_str()));
    return text;
}

} // namespace

void RegisterSoundTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // The first sound's Setup: Loop, the Gain typed in, the Type (Point and
    // Background); Ctrl+Z gives each back, unmodified.
    t = IM_REGISTER_TEST(engine, "sound", "sound_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID sound = OpenSetupOf(ctx, CKCID_WAVESOUND, "//###setupsound");
        IM_CHECK_SILENT(sound != 0);
        auto loop = [sound] { return Object<CKWaveSound>(sound)->GetLoopMode() != 0; };
        auto gain = [sound] { return Object<CKWaveSound>(sound)->GetGain(); };
        auto point = [sound] { return Object<CKWaveSound>(sound)->GetType() == CK_WAVESOUND_POINT; };
        const bool loopBefore = Read<bool>(ctx, loop);
        ctx->ItemClick("**/Loop");
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, loop) != loopBefore);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, loop) == loopBefore);
        IM_CHECK(!App().State().document->Dirty());
        const float gainBefore = Read<float>(ctx, gain);
        const float typed = gainBefore > 0.5f ? 0.25f : 0.75f;
        ctx->ItemInputValue("**/Gain", typed);
        ctx->Yield(3);
        IM_CHECK(std::fabs(Read<float>(ctx, gain) - typed) < 1e-3f);
        Undo(ctx);
        IM_CHECK(std::fabs(Read<float>(ctx, gain) - gainBefore) < 1e-3f);
        IM_CHECK(!App().State().document->Dirty());
        const bool pointBefore = Read<bool>(ctx, point);
        ctx->ItemClick(pointBefore ? "**/Background" : "**/Point");
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, point) != pointBefore);
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, point) == pointBefore);
        IM_CHECK(!App().State().document->Dirty());
    };

    // An array whose first column holds numbers or text, its Setup: the
    // first cell typed in (7); Ctrl+Z gives the cell back, unmodified.
    t = IM_REGISTER_TEST(engine, "sound", "array_cell_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const CK_ID array = Read<CK_ID>(ctx, [] {
            const XObjectPointerArray &list = App().State().host.Context()->GetObjectListByType(CKCID_DATAARRAY, TRUE);
            for (int i = 0; i < list.Size(); ++i)
                if (CKDataArray *each = CKDataArray::Cast(list[i]))
                    if (!each->IsPrivate() && each->GetColumnCount() > 0 && each->GetRowCount() > 0) {
                        const CK_ARRAYTYPE type = each->GetColumnType(0);
                        if (type == CKARRAYTYPE_INT || type == CKARRAYTYPE_FLOAT || type == CKARRAYTYPE_STRING)
                            return each->GetID();
                    }
            return CK_ID(0);
        });
        IM_CHECK_SILENT(array != 0);
        IM_CHECK_SILENT(ShowSetup(ctx, array, "//###setuparray"));
        auto cell = [array] { return CellText(Object<CKDataArray>(array), 0, 0); };
        const std::string before = Read<std::string>(ctx, cell);
        ctx->LogInfo("cell (0, 0): '%s'", before.c_str());
        ctx->ItemInputValue("**/##cell", before == "7" ? "8" : "7");
        ctx->Yield(3);
        IM_CHECK(Read<std::string>(ctx, cell) != before);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK_STR_EQ(Read<std::string>(ctx, cell).c_str(), before.c_str());
        IM_CHECK(!App().State().document->Dirty());
    };
}

} // namespace vdevuitest

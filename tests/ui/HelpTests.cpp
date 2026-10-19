#include "tests/ui/UiTests.h"
#include "imgui_internal.h"
#include <cmath>
#include <cstring>

namespace vdevuitest {
void RegisterHelpTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = IM_REGISTER_TEST(engine, "help", "contents_search_history");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const bool dirty = App().State().document->Dirty();
        const int history = App().State().History().UndoCount();
        ctx->MenuClick("//##MainMenuBar/Help/Online Reference");
        ctx->Yield(4);
        IM_CHECK(InFront(ctx, "//Help"));
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
        ctx->ItemInputValue("//Help/**/##search", "Key Event");
        ctx->ItemClick("//Help/**/Key Event BB");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/Controllers/Key Event.html");
        IM_CHECK(App().Help().ShownText().find("Activates one output") != std::string::npos);
        IM_CHECK(App().Help().ShownText().find("Key State:") != std::string::npos);
        IM_CHECK_GT(App().Help().ShownImages(), size_t(0));
        ctx->ItemClick("//Help/**/Copy text");
        IM_CHECK(Read<std::string>(ctx, [] { char *text = SDL_GetClipboardText(); std::string copy = text ? text : ""; SDL_free(text); return copy; }).find("Key State:") != std::string::npos);
        ctx->ItemClick("//Help/**/BEOBJECT##help-link"); ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "concepts/class_hierarchy.html");
        ctx->ItemClick("//Help/Back"); ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/Controllers/Key Event.html");
        ctx->ItemNavActivate("//Help/**/BEOBJECT##help-link"); ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "concepts/class_hierarchy.html");
        ctx->ItemClick("//Help/Back"); ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/Controllers/Key Event.html");
        ctx->ItemClick("//Help/Back"); ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
        ctx->ItemClick("//Help/Forward"); ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/Controllers/Key Event.html");
        ctx->ItemClick("//Help/Home"); ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
        IM_CHECK(App().State().document->Dirty() == dirty);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
    };
    t = IM_REGISTER_TEST(engine, "help", "tables_anchors_scroll_and_close");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"concepts/8_parameters.html#top"}); });
        ctx->Yield(4);
        IM_CHECK(InFront(ctx, "//Help"));
        IM_CHECK_GT(App().Help().ShownImages(), size_t(3));
        IM_CHECK(App().Help().ShownText().find("Parameter Output, pOut") != std::string::npos);
        ImGuiWindow *article = nullptr;
        for (ImGuiWindow *child : ctx->GetWindowByRef("//Help")->DC.ChildWindows)
            if (std::strstr(child->Name, "/article_")) article = child;
        IM_CHECK(article);
        const float start = article->Scroll.y;
        ctx->MouseMoveToPos(article->InnerRect.GetCenter());
        ctx->MouseWheelY(-8.0f); ctx->Yield(4);
        const float scroll = article->Scroll.y;
        IM_CHECK_GT(scroll, start + 100.0f);
        ctx->ItemClick("//Help/Home"); ctx->Yield(3);
        ctx->ItemClick("//Help/Back"); ctx->Yield(4);
        IM_CHECK(std::fabs(article->Scroll.y - scroll) < 2.0f);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_F); ctx->Yield(2);
        IM_CHECK_EQ(ImGui::GetActiveID(), ctx->ItemInfo("//Help/**/##search").ID);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->WindowClose("//Help"); ctx->Yield(3);
        IM_CHECK(!InFront(ctx, "//Help"));
        ctx->MenuClick("//##MainMenuBar/Help/Online Reference"); ctx->Yield(4);
        IM_CHECK(InFront(ctx, "//Help"));
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
    };
    t = IM_REGISTER_TEST(engine, "help", "building_block_entry_points");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Building Blocks");
        ctx->ItemInputValue("//Building Blocks/##filter", "Rotate");
        for (int action = 0; action < 3; ++action) {
            ctx->WindowFocus("//Building Blocks");
            ctx->MouseMove("//Building Blocks/**/Rotate");
            if (action == 0) ctx->KeyPress(ImGuiKey_F1);
            else if (action == 1) ctx->ItemDoubleClick("//Building Blocks/**/Rotate");
            else {
                ctx->ItemClick("//Building Blocks/**/Rotate", ImGuiMouseButton_Right);
                ctx->ItemClick("//$FOCUSED/Html Help");
            }
            ctx->Yield(4);
            IM_CHECK(InFront(ctx, "//Help"));
            IM_CHECK(App().Help().CurrentTopic() == "behaviors/3D Transformations/Rotate.html");
            IM_CHECK(App().Help().ShownText().find("Rotate") != std::string::npos);
            ctx->ItemClick("//Help/Home"); ctx->Yield(3);
        }
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"missing-source.html"}); });
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "missing-source.html");
        ctx->ItemClick("//Help/Back"); ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
    };
}
}

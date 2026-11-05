#include "tests/ui/UiTests.h"
#include "core/document/DocumentSaver.h"
#include "core/document/FileCommands.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/level/LevelCommands.h"
#include "features/layout/LayoutSceneData.h"
#include "ui/help/HelpWindow.h"
#include "workspace/EditorClient.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace vdevuitest {
namespace {
vdev::EditorClient Client(EditorView view = 0) {
    return {App().WorkspaceForTests().Application().Host(), view};
}
void Resize(ImGuiTestContext *ctx, const char *ref, ImVec2 size) {
    OnMainThread(ctx, [=] {
        auto *window = ImGui::FindWindowByName(ref + 2);
        IM_CHECK_NO_RET(window);
        ImGui::SetWindowPos(window, {40, 70});
        ImGui::SetWindowSize(window, size);
    });
    ctx->Yield(4);
    ctx->SetRef(ref);
}
void Appearance() {
    auto &workspace = App().WorkspaceForTests();
    auto config = workspace.Application().Configuration();
    for (const auto *kind : {"theme", "font"}) {
        const char *id = std::string(kind) == "theme" ? "editor.theme" : "ui.font.body";
        config.providers.erase(
            std::remove_if(config.providers.begin(), config.providers.end(),
                           [&](const auto &row) { return row.kind == kind && row.id == id; }),
            config.providers.end());
        config.providers.push_back({kind, id, "example.appearance"});
    }
    std::string error;
    IM_CHECK_NO_RET(workspace.Configure(config, error));
}
struct RestoreAppearance {
    ImGuiTestContext *ctx;
    vdev::EditorConfiguration configuration = App().WorkspaceForTests().Application().Configuration();
    ~RestoreAppearance() {
        OnMainThread(ctx, [&] {
            std::string error;
            App().WorkspaceForTests().Configure(configuration, error);
        });
    }
};
void Click(ImGuiTestContext *ctx, const char *ref, const char *item) {
    ctx->SetRef(ref);
    ShowScrollableItem(ctx, ref, item);
    ctx->ItemClick(item);
}
std::string Phase() {
    const char *value = std::getenv("NEMODEV_FINISH_PHASE");
    return value ? value : "";
}
std::filesystem::path Root() {
    const char *value = std::getenv("NEMODEV_FINISH_ROOT");
    return std::filesystem::u8path(value ? value : "");
}
void Drain(ImGuiTestContext *ctx) {
    for (int i = 0; i < 500; ++i) {
        ctx->Yield();
        const bool done =
            Read<bool>(ctx, [] { return App().StartupSettled() && !App().State().runtimeOperation.Busy(); });
        if (done && i > 4)
            return;
    }
    IM_CHECK_NO_RET(false);
}
std::vector<vdev::EditorField> HelpState() {
    std::vector<vdev::EditorField> fields;
    const EditorSink sink{&fields, [](void *data, const EditorValue *values, uint32_t count) {
                              return vdev::CopyEditorFields(
                                  *static_cast<std::vector<vdev::EditorField> *>(data), values, count);
                          }};
    IM_CHECK_NO_RET(App().Help().SaveState(sink) == EDITOR_OK);
    return fields;
}
} // namespace
void RegisterWorkspaceLayoutTests(ImGuiTestEngine *engine) {
    auto *test =
        IM_REGISTER_TEST(engine, "workspacelayout", "schematic_explorer_reflow_keeps_navigation_and_cancel");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        RestoreAppearance restore{ctx};
        OnMainThread(ctx, [] {
            auto &doc = *App().State().document;
            const auto script =
                vdev::CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Explorer layout Script");
            IM_CHECK_NO_RET(script.ok);
            App().Canvas().ShowScript(script.created);
            App().State().History().Clear();
            Appearance();
        });
        ctx->Yield(4);
        OnMainThread(ctx, [] {
            IM_CHECK_NO_RET(Client().Command("schematic.open-explorer").status <= EDITOR_NO_CHANGE);
        });
        ctx->Yield(4);
        const char *ref = "//Schematic Explorer";
        Resize(ctx, ref, {1100, 750});
        Click(ctx, ref, "**/##text");
        ctx->KeyChars("Explorer layout");
        const auto active = Read<ImGuiID>(ctx, [] { return ImGui::GetActiveID(); });
        Resize(ctx, ref, {430, 350});
        IM_CHECK_EQ(Read<ImGuiID>(ctx, [] { return ImGui::GetActiveID(); }), active);
        ctx->KeyPress(ImGuiKey_Tab);
        Click(ctx, ref, "**/treeview");
        ctx->Yield(3);
        Click(ctx, ref, "**/treeview");
        ctx->Yield(3);
        Click(ctx, ref, "**/Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }), 0);
    };
    test = IM_REGISTER_TEST(engine, "workspacelayout",
                            "layout_explorer_reflow_preserves_locked_selection_and_cancel");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        RestoreAppearance restore{ctx};
        vdev::ObjectRef target;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            const auto made = vdev::CreateLayoutObject(doc, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            target = doc.Reference(made.created);
            Client().Command("selection.replace", {vdev::ObjectsArgument("objects", {target})});
            Client().Command("selection.lock", {vdev::IntegerArgument("locked", 1)});
            App().State().History().Clear();
            Appearance();
            IM_CHECK_NO_RET(
                Client(App().WorkspaceForTests().Role("main.layout")).Command("layout.explorer").status <=
                EDITOR_NO_CHANGE);
        });
        ctx->Yield(4);
        const char *ref = "//3D Layout Explorer";
        Resize(ctx, ref, {1100, 750});
        Resize(ctx, ref, {430, 350});
        Click(ctx, ref, "**/None");
        IM_CHECK(Read<bool>(ctx, [&] {
            return App().State().objectSelection.References() == std::vector<vdev::ObjectRef>{target};
        }));
        Click(ctx, ref, "**/Cancel");
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [&] {
            return App().State().objectSelection.Locked() &&
                   App().State().objectSelection.References() == std::vector<vdev::ObjectRef>{target};
        }));
        OnMainThread(ctx, [] { Client().Command("selection.lock", {vdev::IntegerArgument("locked", 0)}); });
    };
    test =
        IM_REGISTER_TEST(engine, "workspacelayout", "snapshot_grid_reflow_keeps_slots_and_explicit_readback");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        RestoreAppearance restore{ctx};
        uint64_t before = 0;
        OnMainThread(ctx, [&] {
            Appearance();
            before = App().Layout().View().ReadbackFrames();
            IM_CHECK_NO_RET(
                Client(App().WorkspaceForTests().Role("main.layout")).Command("layout.snapshots").status <=
                EDITOR_NO_CHANGE);
        });
        ctx->Yield(4);
        const char *ref = "//Snapshot Buffer";
        Resize(ctx, ref, {1000, 700});
        const auto wide = ctx->ItemInfo("**/$$5/##slot").RectFull;
        Resize(ctx, ref, {430, 350});
        ShowScrollableItem(ctx, ref, "**/$$5/##slot");
        const auto narrow = ctx->ItemInfo("**/$$5/##slot").RectFull;
        IM_CHECK(std::abs(wide.Min.y - narrow.Min.y) > 10);
        IM_CHECK_EQ(Read<uint64_t>(ctx, [] { return App().Layout().View().ReadbackFrames(); }), before);
        Click(ctx, ref, "**/Take");
        ctx->Yield(4);
        IM_CHECK_EQ(Read<uint64_t>(ctx, [] { return App().Layout().View().ReadbackFrames(); }), before + 1);
        ShowScrollableItem(ctx, ref, "**/$$0/##slot");
        ctx->ItemClick("**/$$0/##slot", ImGuiMouseButton_Right);
        ctx->Yield(3);
        ctx->SetRef("//$FOCUSED");
        IM_CHECK(ctx->ItemExists("Delete"));
        ctx->ItemClick("Delete");
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [] {
            const auto data = Client().Query("snapshot.slots");
            const auto *slots = vdev::EditorClient::Find(data, "slots");
            vdev::EditorResponse first;
            if (slots && !slots->fields.empty())
                first.fields = slots->fields[0].fields;
            return slots && vdev::EditorClient::Integer(first, "present") == 0;
        }));
        ctx->SetRef(ref);
        Click(ctx, ref, "**/Close");
        ctx->Yield(3);
    };
    test = IM_REGISTER_TEST(engine, "workspacelayout", "report_scroll_reflow_preserves_document_history");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        RestoreAppearance restore{ctx};
        const auto history = App().State().History().UndoCount();
        OnMainThread(ctx, [] {
            Appearance();
            std::string text;
            for (int i = 0; i < 200; ++i)
                text += "Report line with a long resource path and object name " + std::to_string(i) + "\n";
            IM_CHECK_NO_RET(Client()
                                .Command("message.show", {vdev::TextArgument("title", "Layout report"),
                                                          vdev::TextArgument("format", "report"),
                                                          vdev::TextArgument("text", text)})
                                .status == EDITOR_OK);
        });
        ctx->Yield(4);
        const char *ref = "//Layout report";
        Resize(ctx, ref, {1000, 700});
        Resize(ctx, ref, {430, 250});
        const auto button = ctx->ItemInfo("OK").RectFull;
        IM_CHECK(Read<bool>(ctx, [=] {
            const auto *window = ImGui::FindWindowByName(ref + 2);
            return button.Min.y >= window->Pos.y && button.Max.y <= window->Pos.y + window->Size.y;
        }));
        ctx->ItemClick("OK");
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
    };
    test = IM_REGISTER_TEST(engine, "workspacelayout", "debugger_reflow_keeps_input_and_runtime_commands");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        RestoreAppearance restore{ctx};
        uint64_t original = 0;
        const auto history = App().State().History().UndoCount();
        OnMainThread(ctx, [&] {
            Appearance();
            original = vdev::EditorClient::Integer(Client().Query("runtime.state"), "max-frames");
            IM_CHECK_NO_RET(Client().Command("runtime.open-debugger").status == EDITOR_OK);
        });
        ctx->Yield(4);
        const auto ref = RuntimeRef("debugger");
        Resize(ctx, ref.c_str(), {1100, 700});
        Click(ctx, ref.c_str(), "**/##maxframes");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("28");
        const auto active = Read<ImGuiID>(ctx, [] { return ImGui::GetActiveID(); });
        Resize(ctx, ref.c_str(), {430, 300});
        IM_CHECK_EQ(Read<ImGuiID>(ctx, [] { return ImGui::GetActiveID(); }), active);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<uint64_t>(ctx,
                                   [] {
                                       return vdev::EditorClient::Integer(Client().Query("runtime.state"),
                                                                          "max-frames");
                                   }),
                    original);
        Click(ctx, ref.c_str(), "**/##maxframes");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("28");
        ctx->KeyPress(ImGuiKey_Tab);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<uint64_t>(ctx,
                                   [] {
                                       return vdev::EditorClient::Integer(Client().Query("runtime.state"),
                                                                          "max-frames");
                                   }),
                    28);
        const auto view = App().WorkspaceForTests().Role("main.debugger");
        Click(ctx, ref.c_str(), "**/Debug Log");
        ctx->Yield(3);
        IM_CHECK_EQ(
            Read<uint64_t>(
                ctx, [&] { return vdev::EditorClient::Integer(Client().Inspect(view), "log-visible"); }),
            0);
        Resize(ctx, ref.c_str(), {1100, 700});
        Click(ctx, ref.c_str(), "**/Debug Log");
        ctx->Yield(3);
        IM_CHECK_EQ(
            Read<uint64_t>(
                ctx, [&] { return vdev::EditorClient::Integer(Client().Inspect(view), "log-visible"); }),
            1);
        Click(ctx, ref.c_str(), "Close/##dialogbutton");
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            Client().Command("runtime.set-max-frames", {vdev::IntegerArgument("value", original)});
        });
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
    };
    test = IM_REGISTER_TEST(engine, "workspacelayout", "memory_list_reflow_keeps_sort_and_close");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        RestoreAppearance restore{ctx};
        const auto history = App().State().History().UndoCount();
        OnMainThread(ctx, [] {
            Appearance();
            IM_CHECK_NO_RET(Client().Command("diagnostics.open-memory").status == EDITOR_OK);
        });
        Drain(ctx);
        const auto ref = DiagnosticRef("memory");
        Resize(ctx, ref.c_str(), {1100, 750});
        Click(ctx, ref.c_str(), "**/##objects/$$1/##header");
        ctx->Yield(3);
        Resize(ctx, ref.c_str(), {430, 300});
        IM_CHECK_EQ(Read<uint64_t>(ctx,
                                   [] {
                                       return vdev::EditorClient::Integer(
                                           Client().Inspect(App().WorkspaceForTests().Role("main.memory")),
                                           "sort");
                                   }),
                    1);
        Click(ctx, ref.c_str(), "OK/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
    };
    test = IM_REGISTER_TEST(engine, "workspacelayout", "invalid_links_reflow_keeps_refresh_and_close");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        RestoreAppearance restore{ctx};
        OnMainThread(ctx, [] {
            auto &doc = *App().State().document;
            const auto script =
                vdev::CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Invalid list Script");
            IM_CHECK_NO_RET(script.ok);
            App().Canvas().ShowScript(script.created);
            Appearance();
            IM_CHECK_NO_RET(Client(App().WorkspaceForTests().Role("main.schematic"))
                                .Command("schematic.open-invalid-links")
                                .status == EDITOR_OK);
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        const auto ref =
            "//" + App().WorkspaceForTests().WindowName(App().WorkspaceForTests().Role("main.invalid-links"));
        Resize(ctx, ref.c_str(), {1100, 700});
        Resize(ctx, ref.c_str(), {430, 300});
        Click(ctx, ref.c_str(), "Refresh/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(Read<uint64_t>(ctx, [] {
                     return vdev::EditorClient::Integer(
                         Client().Inspect(App().WorkspaceForTests().Role("main.invalid-links")), "revision");
                 }) != 0);
        Click(ctx, ref.c_str(), "Close/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
    };
    test = IM_REGISTER_TEST(engine, "workspacelayout", "profiler_reflow_keeps_details_sort_and_close");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        RestoreAppearance restore{ctx};
        const auto history = App().State().History().UndoCount();
        OnMainThread(ctx, [] {
            Appearance();
            IM_CHECK_NO_RET(Client().Command("runtime.open-profiler").status == EDITOR_OK);
        });
        ctx->Yield(5);
        const auto ref = RuntimeRef("profiler");
        Resize(ctx, ref.c_str(), {1100, 750});
        Click(ctx, ref.c_str(), "**/##details");
        ctx->Yield(3);
        Click(ctx, ref.c_str(), "**/Sort By Execution time");
        ctx->Yield(3);
        Resize(ctx, ref.c_str(), {430, 300});
        ShowScrollableItem(ctx, ref.c_str(), "**/Sort By Execution time");
        IM_CHECK(ctx->ItemInfo("**/Sort By Execution time").StatusFlags & ImGuiItemStatusFlags_Checked);
        Resize(ctx, ref.c_str(), {1100, 750});
        IM_CHECK(ctx->ItemExists("**/Sort By Execution time"));
        Click(ctx, ref.c_str(), "Close");
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
    };
    test = IM_REGISTER_TEST(engine, "workspacelayout", "message_explorer_reflow_keeps_selection_and_lists");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        RestoreAppearance restore{ctx};
        OnMainThread(ctx, [] {
            auto &doc = *App().State().document;
            const auto script =
                vdev::CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Message layout Script");
            IM_CHECK_NO_RET(script.ok);
            App().Canvas().ShowScript(script.created);
            const auto message = doc.Context()->GetMessageManager()->AddMessageType("Message reflow layout");
            const auto graph = vdev::CreateBehaviorGraph(doc, script.created, script.created,
                                                        "Message layout sender", {200, 160});
            IM_CHECK_NO_RET(graph.ok);
            auto *sender = CKBehavior::Cast(doc.Context()->GetObject(graph.created));
            sender->ModifyFlags(CKBEHAVIOR_MESSAGESENDER, 0);
            auto *input = sender->CreateInputParameter("Message", CKPGUID_MESSAGE);
            auto *value = sender->CreateLocalParameter("Message value", CKPGUID_MESSAGE);
            IM_CHECK_NO_RET(input && value && value->SetValue(&message, sizeof(message)) == CK_OK &&
                            input->SetDirectSource(value) == CK_OK);
            doc.MarkTopologyChanged();
            Appearance();
            IM_CHECK_NO_RET(Client(App().WorkspaceForTests().Role("main.schematic"))
                                .Command("schematic.open-messages")
                                .status == EDITOR_OK);
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        const auto view = App().WorkspaceForTests().Role("main.messages");
        const auto ref = "//" + App().WorkspaceForTests().WindowName(view);
        Resize(ctx, ref.c_str(), {1100, 700});
        Click(ctx, ref.c_str(), "**/Message reflow layout  (1)");
        const auto selected = ctx->ItemInfo("**/Message reflow layout  (1)").ID;
        Resize(ctx, ref.c_str(), {430, 300});
        ShowScrollableItem(ctx, ref.c_str(), "**/Message reflow layout  (1)");
        IM_CHECK_EQ(ctx->ItemInfo("**/Message reflow layout  (1)").ID, selected);
        ctx->ScrollToBottom(ctx->GetWindowByRef(ref.c_str())->ID);
        ctx->Yield(4);
        IM_CHECK(ctx->ItemExists("**/Message layout sender   (Message layout Script)"));
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                Client().Command("workspace.close-view", {vdev::IntegerArgument("view", view)}).status ==
                EDITOR_OK);
        });
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
    };
    test = IM_REGISTER_TEST(engine, "startupflows", "help_cross_startup_and_external_provider");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto phase = Phase();
        auto &workspace = App().WorkspaceForTests();
        if (phase == "help-save") {
            OnMainThread(ctx, [] {
                IM_CHECK_NO_RET(
                    Client().Command("help.open", {vdev::TextArgument("topic", "topic.html")}).status ==
                    EDITOR_OK);
            });
            ctx->Yield(5);
            const auto ref = Read<std::string>(
                ctx, [] { return "//" + App().WorkspaceForTests().WindowName(vdev::HelpViewForTests()); });
            Resize(ctx, ref.c_str(), {430, 320});
            IM_CHECK(App().Help().CurrentTopic() == "topic.html");
            Resize(ctx, ref.c_str(), {820, 540});
            OnMainThread(ctx, [&] {
                for (auto *child : ImGui::FindWindowByName(ref.c_str() + 2)->DC.ChildWindows)
                    if (std::string(child->Name).find("/article_") != std::string::npos)
                        ImGui::SetScrollY(child, 300);
            });
            ctx->Yield(4);
        } else if (phase == "help-native") {
            IM_CHECK(App().Help().CurrentTopic() == "topic.html");
            const auto state = Read<std::vector<vdev::EditorField>>(ctx, [] { return HelpState(); });
            const auto positions =
                std::find_if(state.begin(), state.end(), [](const auto &f) { return f.key == "positions"; });
            IM_CHECK(positions != state.end() && !positions->texts.empty());
        } else if (phase == "help-external") {
            OnMainThread(ctx, [&] {
                const auto opened =
                    Client().Command("help.open", {vdev::TextArgument("topic", "topic.html")});
                IM_CHECK_NO_RET(opened.status == EDITOR_OK);
                const auto view = vdev::EditorClient::Integer(opened, "view");
                const auto state = Client().Inspect(view);
                IM_CHECK_NO_RET(vdev::EditorClient::Text(state, "current") == "topic.html");
                IM_CHECK_NO_RET(vdev::EditorClient::Find(state, "positions")->texts.size() == 1);
            });
        } else
            IM_CHECK(false);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(Client().Command("workspace.save").status == EDITOR_OK); });
    };
    test = IM_REGISTER_TEST(engine, "startupflows", "composition_entrypoints_share_open_and_failure_state");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto phase = Phase();
        const auto root = Root();
        const auto cmo = root / "中文 scene.cmo", nmo = root / "--runtime.nmo";
        if (phase == "files-save") {
            OnMainThread(ctx, [&] {
                auto &doc = *App().State().document;
                const auto made = vdev::CreateLayoutObject(doc, vdev::LayoutObject::Frame3D);
                IM_CHECK_NO_RET(made.ok &&
                                vdev::RenameLevelObject(doc, doc.Reference(made.created), "Entry object").ok);
                std::string error;
                IM_CHECK_NO_RET(vdev::SaveObjectsAs(doc, {made.created}, nmo.u8string(), &error));
                vdev::SaveReport report;
                IM_CHECK_NO_RET(vdev::SaveDocumentAs(doc, cmo.u8string(), {}, report));
            });
            return;
        }
        const bool library = phase.find("nmo") != std::string::npos;
        const auto file = library ? nmo : cmo;
        const auto check = [&] {
            OnMainThread(ctx, [&] {
                auto &doc = *App().State().document;
                auto *level = doc.Context()->GetCurrentLevel();
                IM_CHECK_NO_RET(level && level->GetLevelScene());
                IM_CHECK_NO_RET(doc.Context()->GetObjectByName("Entry object"));
                IM_CHECK_NO_RET((doc.CreatedFrom() == vdev::Document::Origin::LoadedComposition) != library);
                IM_CHECK_NO_RET(App().State().History().UndoCount() == 0);
            });
        };
        check();
        if (phase.rfind("files-startup", 0) == 0)
            return;
        for (int route = 0; route < 3; ++route) {
            OnMainThread(ctx, [&] {
                App().State().document->MarkSaved();
                App().State().History().MarkSavePoint();
                if (route == 0)
                    Client().Command("document.open", {vdev::TextArgument("path", file.u8string())});
                else if (route == 1)
                    App().DropFiles({vdev::Utf8ToWide(file.u8string())}, 0, 0);
                else {
                    const auto opened = Client().Command("document.open");
                    IM_CHECK_NO_RET(opened.status == EDITOR_QUEUED);
                    const auto input = Client().Query("document.input");
                    IM_CHECK_NO_RET(Client()
                                        .Command("document.respond",
                                                 {vdev::IntegerArgument(
                                                      "input", vdev::EditorClient::Integer(input, "input")),
                                                  vdev::TextArgument("choice", "accept"),
                                                  vdev::TextArgument("path", file.u8string())})
                                        .status == EDITOR_OK);
                }
            });
            Drain(ctx);
            check();
        }
        OnMainThread(ctx, [] {
            App().State().document->MarkSaved();
            App().State().History().MarkSavePoint();
        });
        ctx->MenuClick("//##MainMenuBar/File");
        const auto recent = Read<ImGuiID>(ctx, [&] {
            auto *menu = ImGui::FindWindowByName("###Menu_00");
            IM_CHECK_NO_RET(menu);
            return menu ? menu->GetID(("1) " + file.u8string()).c_str()) : ImGuiID(0);
        });
        ctx->ItemClick(recent);
        Drain(ctx);
        check();
        vdev::DocumentToken token;
        size_t undo = 0;
        std::vector<vdev::ObjectRef> selection;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *object = doc.Context()->GetObjectByName("Entry object");
            const auto target = doc.Reference(object->GetID());
            IM_CHECK_NO_RET(vdev::RenameLevelObject(doc, target, "Kept edit").ok);
            Client().Command("selection.replace", {vdev::ObjectsArgument("objects", {target})});
            doc.MarkSaved();
            App().State().History().MarkSavePoint();
            token = doc.Token();
            undo = App().State().History().UndoCount();
            selection = App().State().objectSelection.References();
            std::ofstream(root / "broken.nmo") << "Invalid NMO";
            Client().Command("document.open", {vdev::TextArgument("path", (root / "broken.nmo").u8string())});
        });
        Drain(ctx);
        IM_CHECK(Read<bool>(ctx, [&] {
            return App().State().document->Token() == token && App().State().History().UndoCount() == undo &&
                   App().State().objectSelection.References() == selection;
        }));
        OnMainThread(ctx, [] {
            IM_CHECK_NO_RET(App().State().History().Undo());
            IM_CHECK_NO_RET(App().State().document->Context()->GetObjectByName("Entry object"));
            IM_CHECK_NO_RET(App().State().History().Redo());
            IM_CHECK_NO_RET(App().State().document->Context()->GetObjectByName("Kept edit"));
        });
    };
}
} // namespace vdevuitest

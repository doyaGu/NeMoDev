#include "tests/ui/UiTests.h"
#include "features/objects/ObjectViews.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/document/BehaviorDescription.h"
#include "core/document/FileCommands.h"
#include "core/document/LinkValidation.h"
#include "ui/render/UiRenderer.h"
#include "ui/schematic/Snapshots.h"
#include "core/document/ToolCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "features/layout/LayoutSceneData.h"
#include "core/document/ObjectPictures.h"
#include "core/runtime/Bitmaps.h"
#include <fstream>
#include <cstdlib>
#include <chrono>
#include <filesystem>

namespace vdevuitest {
namespace {
int LiveViews() {
    auto &app = (&App().WorkspaceForTests())->Application();
    const EditorRequest request{app.Scope(), nullptr, 0};
    const auto result = app.Query("example.views", request);
    return result.status == EDITOR_OK && !result.fields.empty() ? std::stoi(result.fields.front().text) : -1;
}
} // namespace
void RegisterModuleTests(ImGuiTestEngine *engine) {
    auto *profile = IM_REGISTER_TEST(engine, "profiles", "profile_startup_state");
    profile->GuiFunc = MainThreadGui;
    profile->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(8);
        OnMainThread(ctx, [&] {
            const auto option = [](const char *key) { const auto *value = std::getenv(key); return std::string(value ? value : ""); };
            auto &workspace = App().WorkspaceForTests();
            auto &editor = workspace.Application();
            const vdev::EditorClient client(editor.Host(), 0);
            const auto modules = client.Query("profile.modules");
            IM_CHECK_NO_RET(modules.status == EDITOR_OK);
            const auto *rows = vdev::EditorClient::Find(modules, "modules");
            IM_CHECK_NO_RET(rows);
            const auto active = [&](const std::string &id) {
                for (const auto &row : rows->fields) if (row.key == id)
                    for (const auto &field : row.fields) if (field.key == "active") return field.integer != 0;
                return false;
            };
            IM_CHECK_NO_RET(active("application.operations") && active("profile.settings"));
            const auto layout = option("NEMODEV_PROFILE_LAYOUT");
            const auto view = workspace.Role("main.layout");
            if (layout == "disabled") IM_CHECK_NO_RET(!view && !App().Layout().UsesGpuViewport());
            else if (layout == "external") {
                IM_CHECK_NO_RET(view && workspace.WindowName(view) == "External 3D Layout###3D Layout");
                IM_CHECK_NO_RET(editor.CommandProvider("object.rename") == "example.objects");
                const auto painters = editor.CaptureValueEditors();
                const auto *painter = painters.at("text.single-line");
                IM_CHECK_NO_RET(std::string(painter->provider.data, painter->provider.size) == "example.text");
                IM_CHECK_NO_RET(std::any_of(editor.Configuration().providers.begin(), editor.Configuration().providers.end(), [](const auto &choice) {
                    return choice.kind == "query" && choice.id == "setup.details" && choice.provider == "example.setups";
                }));
            } else {
                IM_CHECK_NO_RET(view && App().Layout().UsesGpuViewport());
            }
            const auto help = option("NEMODEV_PROFILE_HELP");
            IM_CHECK_NO_RET(active("help.reference") == (help != "disabled"));
            if (help == "replacement") IM_CHECK_NO_RET(client.Query("help.reference.value").status == EDITOR_OK);
            else IM_CHECK_NO_RET(client.Query("help.reference.value").status != EDITOR_OK);
            if (option("NEMODEV_PROFILE_BINDINGS") == "yes") {
                IM_CHECK_NO_RET(editor.Shortcuts().at("help.open").size() == 2);
                IM_CHECK_NO_RET(std::any_of(editor.Menus().begin(), editor.Menus().end(), [](const auto &menu) {
                    return menu.id == "help.reference" && menu.order == 567;
                }));
            }
            if (option("NEMODEV_PROFILE_SCHEMATIC") == "disabled") IM_CHECK_NO_RET(!workspace.Role("main.schematic"));
            if (option("NEMODEV_PROFILE_EXAMPLE") == "disabled")
                IM_CHECK_NO_RET(client.Command("example.show-document-info").status != EDITOR_OK);
            if (option("NEMODEV_PROFILE_WRITE_FAILURE") == "yes") {
                namespace fs = std::filesystem;
                const auto path = fs::u8path(option("NEMODEV_PROFILE_FILE"));
                const auto backup = fs::u8path(path.u8string() + ".retained");
                const auto bytes = [](const fs::path &file) {
                    std::ifstream input(file, std::ios::binary);
                    return std::string(std::istreambuf_iterator<char>(input), {});
                };
                const auto original = bytes(path);
                fs::rename(path, backup);
                struct RestoreProfile {
                    fs::path file, backup;
                    ~RestoreProfile() { std::error_code ignored; fs::remove(file, ignored); fs::rename(backup, file, ignored); }
                } restore{path, backup};
                fs::create_directory(path);
                const auto oldView = workspace.Role("main.layout");
                auto *texture = App().Layout().GpuViewportTextureForTests();
                auto configured = editor.Configuration();
                for (auto &choice : configured.providers) if (choice.kind == "view" && choice.id == "layout.viewport")
                    choice.provider = "example.layout-window";
                std::string error;
                IM_CHECK_NO_RET(!workspace.Configure(configured, error) && !error.empty());
                IM_CHECK_NO_RET(workspace.Role("main.layout") == oldView &&
                    App().Layout().GpuViewportTextureForTests() == texture && App().Layout().UsesGpuViewport());
                vdev::EditorField setting{"module", EDITOR_RECORD};
                setting.fields = {{"id", EDITOR_TEXT_VALUE, {}, "example.document-info"},
                    {"enabled", EDITOR_INTEGER_VALUE, {}, {}, 0}};
                const std::vector<vdev::EditorField> fields{setting}; const auto values = vdev::BorrowEditorFields(fields);
                IM_CHECK_NO_RET(editor.Execute("profile.set-module", {client.Scope(), values.data(), uint32_t(values.size())}).status == EDITOR_ERROR);
                IM_CHECK_NO_RET(bytes(backup) == original);
            }
            if (option("NEMODEV_PROFILE_SAVE") == "yes") {
                auto &state = App().State();
                const auto token = state.document->Token();
                const auto history = state.History().UndoCount();
                const auto selection = state.objectSelection.Revision();
                auto configuration = editor.Configuration();
                for (auto &choice : configuration.providers) {
                    if (choice.kind == "view" && choice.id == "layout.viewport") choice.provider = "example.layout-window";
                    if (choice.kind == "command" && choice.id == "object.rename") choice.provider = "example.objects";
                    if (choice.kind == "query" && choice.id == "setup.details") choice.provider = "example.setups";
                    if (choice.kind == "editor" && choice.id == "text.single-line") choice.provider = "example.text";
                }
                std::string error;
                IM_CHECK_NO_RET(workspace.Configure(configuration, error));
                vdev::EditorField setting{"module", EDITOR_RECORD};
                setting.fields = {{"id", EDITOR_TEXT_VALUE, {}, "example.document-info"},
                    {"enabled", EDITOR_INTEGER_VALUE, {}, {}, 0}};
                const std::vector<vdev::EditorField> fields{setting};
                const auto values = vdev::BorrowEditorFields(fields);
                IM_CHECK_NO_RET(editor.Execute("profile.set-module", {client.Scope(), values.data(), uint32_t(values.size())}).status == EDITOR_OK);
                IM_CHECK_NO_RET(client.Command("example.show-document-info").status == EDITOR_OK);
                IM_CHECK_NO_RET(state.document->Token() == token && state.History().UndoCount() == history &&
                    state.objectSelection.Revision() == selection);
            }
        });
    };
    auto *discovery = IM_REGISTER_TEST(engine, "modules", "discovered_module_uses_builtin_workspace_and_queries");
    discovery->GuiFunc = MainThreadGui;
    discovery->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        EditorView opened = 0;
        size_t undo = 0;
        bool redo = false, dirty = false;
        uint64_t selection = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            undo = state.History().UndoCount(); redo = state.History().CanRedo();
            dirty = state.document->Dirty(); selection = state.objectSelection.Revision();
            auto &workspace = App().WorkspaceForTests();
            const auto result = EditorCommands().Command("example.show-document-info");
            IM_CHECK_NO_RET(result.status == EDITOR_OK);
            opened = workspace.Open("example.document-info");
            IM_CHECK_NO_RET(opened && workspace.Open("example.document-info") == opened);
            workspace.Focus(opened);
        });
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            const auto window = workspace.WindowName(opened);
            const auto *shown = ImGui::FindWindowByName(window.c_str());
            IM_CHECK_NO_RET(shown && shown->WasActive);
            IM_CHECK_NO_RET(workspace.Close(opened));
            auto &state = App().State();
            IM_CHECK_NO_RET(state.History().UndoCount() == undo && state.History().CanRedo() == redo &&
                state.document->Dirty() == dirty && state.objectSelection.Revision() == selection);
        });
    };
    auto *setupTest = IM_REGISTER_TEST(engine, "modules", "setup_queries_and_open_command");
    setupTest->GuiFunc = MainThreadGui;
    setupTest->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State();
        auto &workspace = *(&App().WorkspaceForTests());
        auto &editor = workspace.Application();
        vdev::EditorConfiguration initial;
        vdev::ObjectRef target{};
        size_t steps = 0;
        uint64_t selection = 0, opens = 0, lists = 0, pages = 0;
        bool dirty = false;
        const auto counts = [&] { return vdev::EditorClient(editor.Host(), 0).Query("example.setup-counts"); };
        OnMainThread(ctx, [&] {
            const auto created = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(created.ok);
            target = state.document->Reference(created.created);
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
            initial = editor.Configuration();
            auto configuration = initial;
            for (auto &choice : configuration.providers)
                if ((choice.kind == "query" && (choice.id == "setup.page" || choice.id == "setup.objects")) ||
                    (choice.kind == "command" && choice.id == "object.open-setup"))
                    choice.provider = "example.setups";
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(configuration, error));
            const auto before = counts();
            opens = vdev::EditorClient::Integer(before, "opens");
            lists = vdev::EditorClient::Integer(before, "lists");
            pages = vdev::EditorClient::Integer(before, "pages");
            steps = state.History().UndoCount();
            selection = state.objectSelection.Revision();
            dirty = state.document->Dirty();
            EditorCommands().Command("object.open-setup", {vdev::ObjectArgument("target", target)});
        });
        ctx->Yield(4);
        auto *window = ctx->GetWindowByRef("//###setup3d");
        IM_CHECK(window && window->WasActive && std::string(window->Name).rfind("External Setup###", 0) == 0);
        IM_CHECK(ShowsObject(ctx, target.object));
        ctx->SetRef("//###setup3d");
        ctx->ItemClick("##name");
        ctx->Yield(2);
        ctx->KeyPress(ImGuiKey_Escape);
        OnMainThread(ctx, [&] {
            const auto after = counts();
            IM_CHECK_NO_RET(vdev::EditorClient::Integer(after, "opens") == opens + 1);
            IM_CHECK_NO_RET(vdev::EditorClient::Integer(after, "pages") > pages);
            IM_CHECK_NO_RET(vdev::EditorClient::Integer(after, "lists") > lists);
            auto stale = target;
            ++stale.lifetime;
            const auto shown = App().Setups().OpenSetups();
            IM_CHECK_NO_RET(vdev::EditorClient(editor.Host(), 0)
                                .Command("object.open-setup", {vdev::ObjectArgument("target", stale)})
                                .status == EDITOR_STALE);
            IM_CHECK_NO_RET(App().Setups().OpenSetups().size() == shown.size());
            IM_CHECK_NO_RET(state.History().UndoCount() == steps && state.objectSelection.Revision() == selection &&
                            state.document->Dirty() == dirty);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(initial, error));
        });
        ctx->Yield(3);
        window = ctx->GetWindowByRef("//###setup3d");
        IM_CHECK(window && std::string(window->Name).rfind("3D Frame Setup###", 0) == 0);
        OnMainThread(ctx, [&] {
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
            IM_CHECK_NO_RET(state.History().Undo());
        });
    };
    setupTest = IM_REGISTER_TEST(engine, "modules", "setup_class_queries_and_action");
    setupTest->GuiFunc = MainThreadGui;
    setupTest->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State();
        auto &workspace = *(&App().WorkspaceForTests());
        auto &editor = workspace.Application();
        vdev::EditorConfiguration initial;
        vdev::ObjectRef target{};
        VxMatrix before;
        size_t steps = 0;
        uint64_t details = 0, properties = 0, actions = 0;
        const auto counts = [&] { return vdev::EditorClient(editor.Host(), 0).Query("example.setup-class-counts"); };
        OnMainThread(ctx, [&] {
            auto *entity =
                CK3dEntity::Cast(state.document->Context()->CreateObject(CKCID_3DOBJECT, "External class page"));
            IM_CHECK_NO_RET(entity);
            state.document->Include(entity->GetID());
            VxVector position(12, 34, 56);
            entity->SetPosition(&position);
            before = entity->GetWorldMatrix();
            target = state.document->Reference(entity->GetID());
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
            initial = editor.Configuration();
            auto configuration = initial;
            for (auto &choice : configuration.providers)
                if ((choice.kind == "query" && (choice.id == "setup.details" || choice.id == "setup.properties")) ||
                    (choice.kind == "command" && choice.id == "setup.reset-world-matrix"))
                    choice.provider = "example.setups";
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(configuration, error));
            const auto old = counts();
            details = vdev::EditorClient::Integer(old, "details");
            properties = vdev::EditorClient::Integer(old, "properties");
            actions = vdev::EditorClient::Integer(old, "actions");
            steps = state.History().UndoCount();
            EditorCommands().Command("object.open-setup", {vdev::ObjectArgument("target", target)});
        });
        ctx->Yield(4);
        ctx->SetRef("//###setup3d");
        ctx->ItemClick("**/Reset World Matrix");
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            const auto after = counts();
            IM_CHECK_NO_RET(vdev::EditorClient::Integer(after, "details") > details &&
                            vdev::EditorClient::Integer(after, "properties") > properties &&
                            vdev::EditorClient::Integer(after, "actions") == actions + 1);
            auto *entity = CK3dEntity::Cast(state.document->Resolve(target));
            IM_CHECK_NO_RET(entity && state.History().UndoCount() == steps + 1);
            IM_CHECK_NO_RET(state.History().Undo() &&
                            std::memcmp(&before, &entity->GetWorldMatrix(), sizeof(before)) == 0);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(initial, error));
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
        });
    };
    setupTest = IM_REGISTER_TEST(engine, "modules", "layout_workspace_provider_restore_and_gpu_lifetime");
    setupTest->GuiFunc = MainThreadGui;
    setupTest->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        ImVec2 min, max;
        IM_CHECK(App().Layout().ShownView(min, max));
        ctx->MouseMoveToPos(Center(min, max));
        ctx->KeyPress(ImGuiKey_X);
        ctx->Yield(3);
        auto &state = App().State();
        auto &workspace = *(&App().WorkspaceForTests());
        auto &app = workspace.Application();
        const vdev::EditorClient client(app.Host(), 0);
        vdev::EditorConfiguration initial;
        EditorView original = 0;
        unsigned dock = 0;
        uint64_t history = 0, selection = 0, allocations = 0, uploads = 0, reads = 0;
        bool dirty = false;
        vdev::UiTexture *texture = nullptr;
        vdev::SnapshotData snapshots;
        int slot = -1;
        std::vector<unsigned char> pixels;
        OnMainThread(ctx, [&] {
            original = workspace.Role("main.layout");
            IM_CHECK_NO_RET(original);
            initial = app.Configuration();
            const auto *window = ImGui::FindWindowByName(workspace.WindowName(original).c_str());
            IM_CHECK_NO_RET(window && window->DockId);
            dock = window->DockId;
            history = state.History().UndoCount();
            dirty = state.document->Dirty();
            selection = state.objectSelection.Revision();
            texture = App().Layout().GpuViewportTextureForTests();
            IM_CHECK_NO_RET(texture && App().Layout().Tool() == vdev::LayoutTool::Translate);
            const auto beforeRead = App().Layout().View().ReadbackFrames();
            const auto taken = client.Command("snapshot.take");
            IM_CHECK_NO_RET(taken.status == EDITOR_OK && App().Layout().View().ReadbackFrames() == beforeRead + 1);
            slot = int(vdev::EditorClient::Integer(taken, "slot"));
            IM_CHECK_NO_RET(vdev::ReadSnapshots(client, snapshots) && snapshots.Has(slot));
            pixels = snapshots.Pixels(slot);
            allocations = App().Layout().GpuViewportCreationsForTests();
            uploads = App().Layout().ViewportUploadsForTests();
            reads = App().Layout().View().ReadbackFrames();
            auto configured = initial;
            for (auto &choice : configured.providers)
                if (choice.kind == "view" && choice.id == "layout.viewport")
                    choice.provider = "example.layout-window";
            IM_CHECK_NO_RET(
                client.Command("example.layout-window-failure", {vdev::IntegerArgument("fail", 1)}).status ==
                EDITOR_OK);
            std::string error;
            IM_CHECK_NO_RET(!workspace.Configure(configured, error));
            IM_CHECK_NO_RET(workspace.Role("main.layout") == original && app.Scope(original).view &&
                            App().Layout().GpuViewportTextureForTests() == texture);
            IM_CHECK_NO_RET(
                client.Command("example.layout-window-failure", {vdev::IntegerArgument("fail", 0)}).status ==
                EDITOR_OK);
            IM_CHECK_NO_RET(workspace.Configure(configured, error));
            IM_CHECK_NO_RET(!app.Scope(original).view && !App().Layout().UsesGpuViewport() &&
                            !App().Layout().View().Started());
            IM_CHECK_NO_RET(workspace.WindowName(workspace.Role("main.layout")) == "External 3D Layout###3D Layout");
        });
        ctx->Yield(4);
        ctx->SetRef("//3D Layout");
        IM_CHECK(ctx->ItemExists("External Layout Window"));
        IM_CHECK(Read<bool>(ctx, [&] {
            const auto *window = ImGui::FindWindowByName(workspace.WindowName(workspace.Role("main.layout")).c_str());
            return window && window->DockId == dock;
        }));
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(initial, error));
        });
        ctx->Yield(5);
        IM_CHECK(Read<bool>(ctx, [&] {
            const auto *window = ImGui::FindWindowByName(workspace.WindowName(workspace.Role("main.layout")).c_str());
            return window && window->DockId == dock && workspace.Role("main.layout") != original &&
                   App().Layout().UsesGpuViewport() && App().Layout().Tool() == vdev::LayoutTool::Translate &&
                   App().Layout().GpuViewportCreationsForTests() == allocations + 1 &&
                   App().Layout().ViewportUploadsForTests() == uploads &&
                   App().Layout().View().ReadbackFrames() == reads && state.History().UndoCount() == history &&
                   state.document->Dirty() == dirty && state.objectSelection.Revision() == selection;
        }));
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(vdev::ReadSnapshots(client, snapshots) && snapshots.Has(slot) &&
                            snapshots.Pixels(slot) == pixels);
            IM_CHECK_NO_RET(vdev::RemoveSnapshots(client, snapshots, {slot}).status == EDITOR_OK);
        });
    };
    setupTest = IM_REGISTER_TEST(engine, "modules", "setup_workspace_provider_restore_and_lifetime");
    setupTest->GuiFunc = MainThreadGui;
    setupTest->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State();
        auto &workspace = *(&App().WorkspaceForTests());
        auto &app = workspace.Application();
        const vdev::EditorClient client(app.Host(), 0);
        vdev::ObjectRef target{};
        vdev::EditorConfiguration initial;
        EditorView original = 0, external = 0;
        size_t steps = 0;
        uint64_t selection = 0;
        bool dirty = false;
        OnMainThread(ctx, [&] {
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
            auto *object = state.document->Context()->CreateObject(CKCID_3DENTITY, "Workspace Setup target");
            IM_CHECK_NO_RET(object);
            state.document->Include(object->GetID());
            target = state.document->Reference(object->GetID());
            initial = app.Configuration();
            steps = state.History().UndoCount();
            dirty = state.document->Dirty();
            selection = state.objectSelection.Revision();
            EditorCommands().Command("object.open-setup", {vdev::ObjectArgument("target", target)});
        });
        ctx->Yield(4);
        ctx->SetRef("//###setup3d");
        ctx->ItemClick("**/Attribute");
        ctx->Yield(2);
        OnMainThread(ctx, [&] {
            original = workspace.Role("setup3d");
            IM_CHECK_NO_RET(original);
            auto configured = initial;
            for (auto &choice : configured.providers)
                if (choice.kind == "view" && choice.id == "object.setup")
                    choice.provider = "example.setup-window";
            std::string error;
            IM_CHECK_NO_RET(client.Command("example.setup-window-failure", {vdev::IntegerArgument("fail", 1)}).status ==
                            EDITOR_OK);
            IM_CHECK_NO_RET(!workspace.Configure(configured, error));
            IM_CHECK_NO_RET(workspace.Role("setup3d") == original && app.Scope(original).view &&
                            App().Setups().OpenSetups().size() == 1);
            IM_CHECK_NO_RET(client.Command("example.setup-window-failure", {vdev::IntegerArgument("fail", 0)}).status ==
                            EDITOR_OK);
            IM_CHECK_NO_RET(workspace.Configure(configured, error));
            external = workspace.Role("setup3d");
            IM_CHECK_NO_RET(external && external != original && !app.Scope(original).view);
            const auto shown = App().Setups().OpenSetups();
            IM_CHECK_NO_RET(shown.size() == 1 && shown.front().object == target && shown.front().front);
        });
        ctx->Yield(4);
        auto *window = ctx->GetWindowByRef("//###setup3d");
        IM_CHECK(window && window->WasActive);
        ctx->SetRef("//###setup3d");
        IM_CHECK(ctx->ItemExists("External Setup Window"));
        IM_CHECK(Read<bool>(ctx, [&] { return workspace.WindowName(external).rfind("External Setup###", 0) == 0; }));
        IM_CHECK(ShowsObject(ctx, target.object));
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(initial, error));
            const auto restored = workspace.Role("setup3d");
            IM_CHECK_NO_RET(restored && restored != external && !app.Scope(external).view);
            const auto shown = App().Setups().OpenSetups();
            IM_CHECK_NO_RET(shown.size() == 1 && shown.front().object == target && shown.front().front);
            IM_CHECK_NO_RET(state.History().UndoCount() == steps && state.document->Dirty() == dirty &&
                            state.objectSelection.Revision() == selection);
        });
        ctx->Yield(4);
        ctx->SetRef("//###setup3d");
        IM_CHECK(ctx->ItemExists("**/Add Attribute"));
        OnMainThread(ctx, [&] {
            const auto restored = workspace.Role("setup3d");
            IM_CHECK_NO_RET(client.Command("workspace.close-view", {vdev::IntegerArgument("view", restored)}).status ==
                            EDITOR_OK);
            IM_CHECK_NO_RET(App().Setups().OpenSetups().empty() && !app.Scope(restored).view);
        });
    };
    setupTest = IM_REGISTER_TEST(engine, "modules", "setup_attributes_and_pictures_use_selected_providers");
    setupTest->GuiFunc = MainThreadGui;
    setupTest->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State();
        auto &workspace = *(&App().WorkspaceForTests());
        auto &editor = workspace.Application();
        const vdev::EditorClient client(editor.Host(), 0);
        vdev::EditorConfiguration initial;
        vdev::ObjectRef source{}, destination{};
        uint64_t calls[8]{};
        size_t steps = 0;
        const char *queryIds[] = {"object.attributes", "attribute.clipboard", "object.picture"};
        const char *commandIds[] = {"attribute.copy-values", "attribute.paste-attributes", "attribute.edit-value",
                                    "object.remove-picture", "object.load-picture"};
        OnMainThread(ctx, [&] {
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
            auto *context = state.document->Context();
            auto *first = CKBeObject::Cast(context->CreateObject(CKCID_3DENTITY, "External attributes source"));
            auto *second = CKBeObject::Cast(context->CreateObject(CKCID_3DENTITY, "External attributes destination"));
            IM_CHECK_NO_RET(first && second);
            state.document->Include(first->GetID());
            state.document->Include(second->GetID());
            source = state.document->Reference(first->GetID());
            destination = state.document->Reference(second->GetID());
            const auto type = context->GetAttributeManager()->RegisterNewAttributeType(
                "Setup module value", CKPGUID_INT, CKCID_BEOBJECT, CK_ATTRIBUT_USER);
            IM_CHECK_NO_RET(type >= 0 && first->SetAttribute(type));
            int value = 3;
            first->GetAttributeParameter(type)->SetValue(&value);
            std::vector<unsigned char> pixels(52 * 40 * 4, 255);
            IM_CHECK_NO_RET(
                vdev::SetObjectPicture(*state.document, second->GetID(), vdev::BitmapFromPixels(pixels.data(), 52, 40))
                    .ok);
            steps = state.History().UndoCount();
            initial = editor.Configuration();
            auto configured = initial;
            for (auto &choice : configured.providers)
                if ((choice.kind == "query" &&
                     std::find(std::begin(queryIds), std::end(queryIds), choice.id) != std::end(queryIds)) ||
                    (choice.kind == "command" &&
                     std::find(std::begin(commandIds), std::end(commandIds), choice.id) != std::end(commandIds)))
                    choice.provider = "example.setups";
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(configured, error));
            const auto before = client.Query("example.setup-common-counts");
            const auto *counts = vdev::EditorClient::Find(before, "counts");
            IM_CHECK_NO_RET(counts && counts->integers.size() == 8);
            std::copy(counts->integers.begin(), counts->integers.end(), calls);
            EditorCommands().Command("object.open-setup", {vdev::ObjectArgument("target", source)});
        });
        ctx->Yield(4);
        ctx->SetRef("//###setup3d");
        ctx->ItemClick("**/Attribute");
        ctx->ItemClick("**/Setup module value", ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Copy Attribute Parameter Value");
        OnMainThread(ctx, [&] {
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
            EditorCommands().Command("object.open-setup", {vdev::ObjectArgument("target", destination)});
        });
        ctx->Yield(4);
        ctx->SetRef("//###setup3d");
        ctx->ItemClick("**/Attribute");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_V);
        ctx->Yield(3);
        ctx->ItemDoubleClick("**/3");
        ctx->Yield(3);
        ctx->SetRef("//Edit Parameter###AttributeValue");
        ctx->ItemClick("**/Cancel/##dialogbutton");
        ctx->Yield(3);
        ctx->SetRef("//###setup3d");
        const auto parameter = ctx->ItemInfo("**/Parameter").RectFull;
        ctx->MouseMoveToPos(ImVec2(parameter.GetCenter().x, parameter.Min.y - 30));
        ctx->MouseClick(ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Remove Snapshot");
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(state.History().UndoCount() == steps + 2 &&
                            !state.document->Pictures().count(destination.object));
            const auto load = client.Command("object.load-picture", {vdev::ObjectArgument("target", destination)});
            IM_CHECK_NO_RET(load.status == EDITOR_QUEUED);
            const auto input = client.Query("snapshot.input");
            IM_CHECK_NO_RET(client
                                .Command("snapshot.respond",
                                         {vdev::IntegerArgument("input", vdev::EditorClient::Integer(input, "input")),
                                          vdev::TextArgument("choice", "cancel")})
                                .status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Completion(load.operation).status == EDITOR_CANCELLED);
            const auto after = client.Query("example.setup-common-counts");
            const auto *counts = vdev::EditorClient::Find(after, "counts");
            IM_CHECK_NO_RET(counts && counts->integers.size() == 8);
            for (unsigned i = 0; i < 8; ++i)
                IM_CHECK_NO_RET(counts->integers[i] > calls[i]);
            IM_CHECK_NO_RET(state.History().Undo() && state.History().Undo() && state.History().Undo());
            IM_CHECK_NO_RET(state.History().UndoCount() + 1 == steps);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(initial, error));
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
        });
    };
    auto *test = IM_REGISTER_TEST(engine, "modules", "canvas_queries_and_commands");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        auto &state = App().State(); auto &workspace = *(&App().WorkspaceForTests()); auto &editor = workspace.Application();
        vdev::EditorConfiguration initial; vdev::ObjectRef script{}, graph{}, camera{};
        EditorView schematic = 0, diagnostic = 0;
        size_t steps = 0; bool dirty = false; uint64_t selection = 0;
        OnMainThread(ctx, [&] {
            initial = editor.Configuration(); steps = state.History().UndoCount(); dirty = state.document->Dirty();
            selection = state.objectSelection.Revision();
            auto config = initial;
            for (auto &choice : config.providers)
                if ((choice.kind == "query" && choice.id == "layout.scene") ||
                    (choice.kind == "command" && (choice.id == "layout.set-camera" || choice.id == "schematic.toggle-fold" ||
                                                   choice.id == "schematic.refresh-invalid-links"))) choice.provider = "example.canvas";
            std::string error; IM_CHECK_NO_RET(workspace.Configure(config, error));
            const vdev::EditorClient client(editor.Host(), 0);
            vdev::LayoutSceneData data;
            IM_CHECK_NO_RET(vdev::ReadLayoutScene(client, "cameras", data) && !data.rows.empty());
            IM_CHECK_NO_RET(data.rows.front().name.rfind("External ", 0) == 0);
            camera = data.camera;
        });
        const auto count = [&](const char *key) { return Read<uint64_t>(ctx, [&] {
            return vdev::EditorClient::Integer(editor.Query("example.canvas-counts", {editor.Scope(), nullptr, 0}), key);
        }); };
        const auto reads = count("reads"), cameras = count("cameras"), folds = count("folds"), refreshes = count("refreshes");
        ImVec2 min, max;
        IM_CHECK(Read<bool>(ctx, [&] { return App().Layout().ShownView(min, max); }));
        ctx->WindowFocus("//3D Layout"); ctx->MouseMoveToPos(Center(min, max)); ctx->KeyPress(ImGuiKey_N); ctx->Yield(4);
        IM_CHECK_EQ(count("cameras"), cameras + 1); IM_CHECK(count("reads") > reads);
        IM_CHECK(Read<bool>(ctx, [&] { return App().Layout().View().Camera()->GetID() != camera.object &&
            state.History().UndoCount() == steps && state.document->Dirty() == dirty && state.objectSelection.Revision() == selection; }));
        OnMainThread(ctx, [&] {
            const auto made = vdev::CreateScript(*state.document, state.document->Context()->GetCurrentLevel()->GetID(), "External canvas");
            IM_CHECK_NO_RET(made.ok); script = state.document->Reference(made.created);
            const auto child = vdev::CreateBehaviorGraph(*state.document, made.created, made.created, "External fold", Point(80, 80));
            IM_CHECK_NO_RET(child.ok); graph = state.document->Reference(child.created);
            schematic = workspace.Open("script.schematic"); IM_CHECK_NO_RET(schematic);
            App().Canvas(schematic).ShowScript(script.object);
            App().Canvas(schematic).ToggleFold(script.object, graph.object);
        });
        ctx->Yield(4); IM_CHECK_EQ(count("folds"), folds + 1);
        IM_CHECK(Read<bool>(ctx, [&] { return state.History().UndoCount() == steps + 3 &&
            vdev::FindRecord(state.document->Session(script.object)->Data(), graph.object)->folded; }));
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, [&] { return !vdev::FindRecord(state.document->Session(script.object)->Data(), graph.object)->folded; }));
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), schematic);
            IM_CHECK_NO_RET(client.Command("schematic.open-invalid-links").status == EDITOR_OK);
            diagnostic = workspace.Role("main.invalid-links"); IM_CHECK_NO_RET(diagnostic);
            const auto inspected = workspace.Inspect(diagnostic);
            IM_CHECK_NO_RET(inspected.status == EDITOR_OK && vdev::EditorClient::Integer(inspected, "schematic") == schematic);
        });
        ctx->Yield(4);
        const auto window = "//" + workspace.WindowName(diagnostic);
        ctx->WindowFocus(window.c_str()); ctx->ItemClick((window + "/Refresh/##dialogbutton").c_str()); ctx->Yield(4);
        IM_CHECK_EQ(count("refreshes"), refreshes + 1);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(state.History().UndoCount() == steps + 2);
            IM_CHECK_NO_RET(workspace.Close(diagnostic) && workspace.Close(schematic));
            std::string error; IM_CHECK_NO_RET(workspace.Configure(initial, error));
            const vdev::EditorClient client(editor.Host(), 0);
            IM_CHECK_NO_RET(client.Command("layout.set-camera", {vdev::ObjectArgument("target", camera)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(state.History().Undo() && state.History().Undo());
        });
        ctx->Yield(3);
    };

    test = IM_REGISTER_TEST(engine, "modules", "schematic_diagnostics_keep_their_source_view");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &workspace = *(&App().WorkspaceForTests()); auto &editor = workspace.Application();
        EditorView first = 0, second = 0, messages = 0, invalid = 0;
        CK_ID script = 0, senderId = 0; size_t steps = 0;
        OnMainThread(ctx, [&] {
            steps = state.History().UndoCount(); first = workspace.Role("main.schematic");
            const auto made = vdev::CreateScript(*state.document, state.document->Context()->GetCurrentLevel()->GetID(), "Canvas diagnostics");
            IM_CHECK_NO_RET(made.ok); script = made.created;
            const auto child = vdev::CreateBehaviorGraph(*state.document, script, script, "Canvas sender", Point(80, 80));
            IM_CHECK_NO_RET(child.ok); senderId = child.created;
            auto data = state.document->Session(script)->Data(); auto *root = Object<CKBehavior>(script);
            auto *chunk = GenerateInterfaceChunk(data, root); IM_CHECK_NO_RET(chunk); root->SetInterfaceChunk(chunk);
            auto *sender = Object<CKBehavior>(senderId); sender->ModifyFlags(CKBEHAVIOR_MESSAGESENDER, 0);
            const auto type = state.document->Context()->GetMessageManager()->AddMessageType("Canvas diagnostic message");
            auto *input = sender->CreateInputParameter("Message", CKPGUID_MESSAGE);
            auto *value = sender->CreateLocalParameter("Message value", CKPGUID_MESSAGE);
            IM_CHECK_NO_RET(input && value && value->SetValue(&type, sizeof(type)) == CK_OK && input->SetDirectSource(value) == CK_OK);
            state.document->MarkTopologyChanged(); state.document->RefreshScripts();
            IM_CHECK_NO_RET(vdev::EditScriptData(*state.document, script, "Invalid diagnostic fixture", [=](InterfaceData &data) {
                Link link; link.start = LinkEndpoint(script, 0, ENDPOINT_BIN); link.end = LinkEndpoint(UINT32_MAX - 1, 0, ENDPOINT_BIN);
                data.rootBehavior.links.push_back(link);
            }));
            second = workspace.Open("script.schematic"); IM_CHECK_NO_RET(second && second != first);
            App().Canvas(second).ShowScript(script); workspace.Focus(second);
            const vdev::EditorClient client(editor.Host(), second);
            IM_CHECK_NO_RET(client.Command("schematic.refresh-invalid-links").status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("schematic.open-invalid-links").status == EDITOR_OK &&
                client.Command("schematic.open-messages").status == EDITOR_OK);
            invalid = workspace.Role("main.invalid-links"); messages = workspace.Role("main.messages");
            IM_CHECK_NO_RET(invalid && messages);
        }); ctx->Yield(6);
        OnMainThread(ctx, [&] {
            const auto a = workspace.Inspect(messages), b = workspace.Inspect(invalid);
            const auto *rows = vdev::EditorClient::Find(b, "rows");
            IM_CHECK_NO_RET(a.status == EDITOR_OK && b.status == EDITOR_OK && rows && rows->fields.size() == 1 &&
                vdev::EditorClient::Integer(a, "schematic") == second && vdev::EditorClient::Integer(b, "schematic") == second);
        });
        const auto messageWindow = "//" + workspace.WindowName(messages), invalidWindow = "//" + workspace.WindowName(invalid);
        ctx->WindowFocus(messageWindow.c_str());
        ctx->ItemClick((messageWindow + "/**/Canvas diagnostic message  (1)").c_str()); ctx->Yield(3);
        ctx->ItemClick((messageWindow + "/**/Canvas sender   (Canvas diagnostics)").c_str()); ctx->Yield(3);
        auto firstSelection = Read<uint64_t>(ctx, [&] { return App().Canvas(first).Navigation().selection.Revision(); });
        ctx->ItemClick((messageWindow + "/Goto").c_str()); ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [&] { return App().Canvas(second).Navigation().selection.script == script &&
            App().Canvas(second).Navigation().selection.items.front().object == senderId &&
            App().Canvas(first).Navigation().selection.Revision() == firstSelection; }));
        ctx->WindowFocus(invalidWindow.c_str()); ctx->ItemClick((invalidWindow + "/Delete All").c_str()); ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [&] { return state.document->Session(script)->Data().rootBehavior.links.empty() &&
            state.History().UndoCount() == steps + 4; }));
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, [&] { return state.document->Session(script)->Data().rootBehavior.links.size() == 1; }));
        firstSelection = Read<uint64_t>(ctx, [&] { return App().Canvas(first).Navigation().selection.Revision(); });
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(workspace.Close(second)); }); ctx->Yield(3);
        ctx->WindowFocus(messageWindow.c_str()); ctx->ItemClick((messageWindow + "/Goto").c_str()); ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [&] { return !vdev::EditorClient::Text(workspace.Inspect(messages), "message").empty(); }));
        IM_CHECK_EQ(Read<uint64_t>(ctx, [&] { return App().Canvas(first).Navigation().selection.Revision(); }), firstSelection);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(workspace.Close(messages) && workspace.Close(invalid));
            IM_CHECK_NO_RET(state.History().Undo() && state.History().Undo() && state.History().Undo());
        }); ctx->Yield(3);
    };

    test = IM_REGISTER_TEST(engine, "modules", "schematic_changes_reach_external_views");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State();
        auto &workspace = *(&App().WorkspaceForTests());
        auto &editor = workspace.Application();
        vdev::EditorConfiguration initial;
        vdev::SchematicNavigation navigation;
        CK_ID script = 0, graph = 0;
        EditorView first = 0, second = 0, observer = 0;
        size_t steps = 0;
        CKDWORD before = 0;
        OnMainThread(ctx, [&] {
            initial = editor.Configuration();
            first = workspace.Role("main.schematic");
            navigation = App().Canvas(first).Navigation();
            steps = state.History().UndoCount();
            const auto created = vdev::CreateScript(*state.document, state.document->Context()->GetCurrentLevel()->GetID(),
                                                    "Notification view");
            IM_CHECK_NO_RET(created.ok);
            script = created.created;
            const auto child = vdev::CreateBehaviorGraph(*state.document, script, script, "Child graph", Point(80, 80));
            IM_CHECK_NO_RET(child.ok);
            graph = child.created;
            before = state.document->Session(script)->Data().header.color;
            second = workspace.Open("script.schematic");
            IM_CHECK_NO_RET(first && second && first != second);
            App().Canvas(first).ShowScript(script);
            App().Canvas(second).ShowScript(script);
            workspace.Focus(first);
            auto config = initial;
            for (auto &choice : config.providers)
                if (choice.kind == "command" && choice.id == "schematic.script-color-set")
                    choice.provider = "example.schematic";
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
            observer = editor.OpenScope();
            const EditorValue fields[] = {
                {EDITOR_TEXT("target"), EDITOR_OBJECT, state.document->Reference(script)},
                {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, observer}};
            IM_CHECK_NO_RET(editor.Execute("example.schematic-watch", {editor.Scope(observer), fields, 2}).status == EDITOR_OK);
        });
        ctx->Yield(5);
        const auto count = [&](const char *key) {
            return Read<uint64_t>(ctx, [&] {
                const auto result = editor.Query("example.schematic-state", {editor.Scope(observer), nullptr, 0});
                for (const auto &field : result.fields) if (field.key == key) return field.integer;
                return UINT64_MAX;
            });
        };
        const auto notices = count("notices"), writes = count("writes");
        const auto topology = Read<uint64_t>(ctx, [&] { return state.document->TopologyRevision(); });
        OnMainThread(ctx, [&] { App().Canvas(first).ToggleFold(script, graph); });
        ctx->Yield(4);
        IM_CHECK_EQ(count("notices"), notices + 1);
        IM_CHECK_EQ(Read<uint64_t>(ctx, [&] { return state.document->TopologyRevision(); }), topology);
        IM_CHECK(Read<bool>(ctx, [&] { return vdev::FindRecord(state.document->Session(script)->Data(), graph)->folded; }));
        Undo(ctx);
        IM_CHECK_EQ(count("notices"), notices + 2);
        Redo(ctx);
        IM_CHECK_EQ(count("notices"), notices + 3);
        Undo(ctx);
        IM_CHECK_EQ(count("notices"), notices + 4);

        // The ordinary color dialog must use the selected external command;
        // drafting and cancelling publish no document modification.
        ctx->WindowFocus(("//" + App().SchematicWindow(first)).c_str());
        ctx->Yield(3);
        const auto shown = Read<std::vector<vdev::SchematicCanvas::ShownScript>>(ctx,
            [&] { return App().Canvas(first).ShownScripts(); });
        const auto found = std::find_if(shown.begin(), shown.end(), [&](const auto &item) { return item.script == script; });
        IM_CHECK(found != shown.end());
        const auto open = [&] {
            ctx->MouseMoveToPos(Center(found->headerMin, found->headerMax));
            ctx->MouseClick(1);
            ctx->Yield(3);
            ctx->ItemClick("//$FOCUSED/Set Color");
            ctx->Yield(3);
            ctx->ItemClick("//Script Color/**/##color/##hex/##Text");
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
            ctx->KeyChars("FF00AA");
            ctx->Yield(3);
        };
        open();
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(count("notices"), notices + 4);
        IM_CHECK_EQ(count("writes"), writes);
        open();
        ctx->ItemClick("//Script Color/OK");
        ctx->Yield(4);
        IM_CHECK_EQ(count("writes"), writes + 1);
        IM_CHECK_EQ(count("notices"), notices + 5);
        IM_CHECK_EQ(count("color"), 0xAA00FFu);
        IM_CHECK_EQ(state.History().UndoCount(), steps + 3);
        IM_CHECK_EQ(Read<uint64_t>(ctx, [&] { return state.document->TopologyRevision(); }), topology);
        Undo(ctx);
        IM_CHECK_EQ(count("notices"), notices + 6);
        IM_CHECK_EQ(count("color"), before);
        OnMainThread(ctx, [&] {
            editor.CloseScope(observer);
            IM_CHECK_NO_RET(workspace.Close(second));
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(initial, error));
            IM_CHECK_NO_RET(state.History().Undo() && state.History().Undo());
            App().Canvas(first).Navigation() = navigation;
            App().Canvas(first).RefreshStack();
            workspace.Focus(first);
        });
        ctx->Yield(3);
        IM_CHECK_EQ(state.History().UndoCount(), steps);
    };

    test = IM_REGISTER_TEST(engine, "modules", "rename_cancel_undo_configuration_and_loaded_view");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, SceneRef("level", "").c_str());
        CK_ID object = 0;
        vdev::EditorConfiguration initial;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            initial = (&App().WorkspaceForTests())->Application().Configuration();
            const auto created = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(created.ok);
            object = created.created;
            state.History().Clear();
            state.document->MarkSaved();
            state.History().MarkSavePoint();
        });
        ctx->Yield(3);
        const auto row = ClickLevelRow(ctx, CKCID_3DENTITY);
        IM_CHECK(row && row->object == object);
        const auto before = ObjectName(ctx, object);
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//Rename##level/##name"));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == before);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 0);
        ctx->WindowFocus(SceneRef("level", "").c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->ItemInputValue("//Rename##level/##name", "Unified Name");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Unified Name");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Undo(ctx);
        IM_CHECK(ObjectName(ctx, object) == before);
        Redo(ctx);
        IM_CHECK(ObjectName(ctx, object) == "Unified Name");
        EditorView first = 0, second = 0, external = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *(&App().WorkspaceForTests());
            state.objectSelection.Restore(*state.document, {object});
            first = workspace.Open("object.properties");
            second = workspace.Open("object.summary");
            external = workspace.Open("example.object");
            IM_CHECK_NO_RET(first && second && external && first != second && second != external);
            IM_CHECK_NO_RET(LiveViews() == 1);
            auto config = initial;
            for (auto &provider : config.providers)
                if (provider.kind == "editor" && provider.id == "text.single-line") provider.provider = "example.text";
            config.bindings.emplace_back("object.name", "Ctrl+R");
            config.menus.push_back({"object.rename.level", "main.edit", 0});
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        ctx->WindowFocus(SceneRef("level", "").c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(2);
        IM_CHECK(!ctx->ItemExists("//Rename##level/##name"));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_R);
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//Rename##level/##name"));
        ctx->ItemInputValue("//Rename##level/##name", "External Editor");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "External Editor");
        // Moving a menu entry changes its presentation only; it opens the same
        // property editor and submits the same rename command.
        ctx->MenuClick("//##MainMenuBar/Views/Rename###object.rename.level");
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//Rename##workspace/##name"));
        ctx->ItemInputValue("//Rename##workspace/##name", "Moved Menu");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Moved Menu");
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &app = (&App().WorkspaceForTests())->Application();
            const auto reference = state.document->Reference(object);
            const EditorValue args[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, reference, {}},
                                        {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("Command")}};
            const EditorRequest request{app.Scope(), args, 2};
            const auto count = state.History().UndoCount();
            IM_CHECK_NO_RET(app.Execute("example.rename", request).status == EDITOR_OK);
            IM_CHECK_NO_RET(state.History().UndoCount() == count + 1);
        });
        ctx->Yield(4);
        IM_CHECK(ObjectName(ctx, object) == "External Command");
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *(&App().WorkspaceForTests());
            workspace.Close(external);
            IM_CHECK_NO_RET(LiveViews() == 0);
            const auto context = state.document->Context();
            const auto token = state.document->Token();
            const auto count = state.History().UndoCount();
            workspace.Close(first);
            workspace.Close(second);
            IM_CHECK_NO_RET(state.document->Context() == context && state.document->Token() == token &&
                            state.History().UndoCount() == count);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(initial, error));
        });
    };
    test = IM_REGISTER_TEST(engine, "modules", "hierarchy_setup_and_property_view_share_edits");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        CK_ID object = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const auto created = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(created.ok);
            object = created.created;
            state.objectSelection.Restore(*state.document, {object});
            App().OpenSceneView("hierarchy");
            state.History().Clear();
            state.document->MarkSaved();
            state.History().MarkSavePoint();
        });
        ctx->Yield(4);
        ctx->WindowFocus(SceneRef("hierarchy", "").c_str());
        for (int pass = 0; pass < 20; ++pass) {
            const auto rows = App().HierarchyRows();
            const vdev::SceneShownRow *closed = nullptr;
            for (const auto &row : rows)
                if (row.children && !row.open && (!closed || row.depth < closed->depth))
                    closed = &row;
            if (!closed)
                break;
            ctx->MouseMoveToPos(Center(closed->arrowMin, closed->arrowMax));
            ctx->MouseClick(0);
            ctx->Yield(2);
        }
        bool found = false;
        for (const auto &row : App().HierarchyRows()) {
            if (row.object != object)
                continue;
            ctx->MouseMoveToPos(ImVec2(row.arrowMax.x > 0.0f ? row.arrowMax.x + 40.0f : row.min.x + 80.0f,
                                       (row.min.y + row.max.y) * 0.5f));
            ctx->MouseClick(0);
            found = true;
            break;
        }
        IM_CHECK(found);
        const auto original = ObjectName(ctx, object);
        ctx->Yield(3);
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [] { return (&App().WorkspaceForTests())->Application().HasEdits(); }));
        ctx->KeyCharsReplaceEnter("Hierarchy Name");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Hierarchy Name");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Undo(ctx);
        IM_CHECK(ObjectName(ctx, object) == original);
        ctx->WindowFocus(SceneRef("hierarchy", "").c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->WindowClose(SceneRef("hierarchy", "").c_str());
        ctx->Yield(3);
        IM_CHECK(!Read<bool>(ctx, [] { return (&App().WorkspaceForTests())->Application().HasEdits(); }));
        IM_CHECK(ShowSetup(ctx, object, "//###setup3d"));
        ctx->WindowFocus("//###setup3d");
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//###setuprename/##name"));
        ctx->ItemInputValue("//###setuprename/##name", "Setup Name");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Setup Name");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Undo(ctx);
        IM_CHECK(ObjectName(ctx, object) == original);
        ctx->WindowClose("//###setup3d");
        EditorView view = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            state.objectSelection.Restore(*state.document, {object});
            view = (&App().WorkspaceForTests())->Open("object.properties");
            IM_CHECK_NO_RET(view);
        });
        ctx->Yield(4);
        const auto window = "//###editor-view-" + std::to_string(view);
        ctx->ItemClick((window + "/Rename").c_str());
        ctx->ItemInputValue((window + "/##name").c_str(), "Property Name");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Property Name");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Undo(ctx);
        ctx->ItemClick((window + "/Rename").c_str());
        ctx->Yield(2);
        ctx->WindowClose(window.c_str());
        ctx->Yield(3);
        IM_CHECK(!Read<bool>(ctx, [] { return (&App().WorkspaceForTests())->Application().HasEdits(); }));
        IM_CHECK(ObjectName(ctx, object) == original);
    };
    test = IM_REGISTER_TEST(engine, "modules", "stale_native_target_cannot_be_renamed");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        CK_ID id = 0;
        size_t history = 0;
        EditorView owner = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &document = *state.document;
            auto &workspace = *(&App().WorkspaceForTests());
            auto *object = document.Context()->CreateObject(CKCID_OBJECT, "Original target");
            IM_CHECK_NO_RET(object);
            id = object->GetID();
            const auto ref = document.Reference(id);
            EditorEdit edit = 0;
            IM_CHECK_NO_RET(workspace.Begin("object.name", ref, edit, owner));
            IM_CHECK_NO_RET(workspace.Application().UpdateEdit(edit, "Wrong target") == EDITOR_OK);
            IM_CHECK_NO_RET(workspace.Finish(edit, true));
            history = state.History().UndoCount();
            IM_CHECK_NO_RET(document.Context()->DestroyObject(id, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
            auto *replacement = document.Context()->CreateObject(CKCID_OBJECT, "Replacement target");
            IM_CHECK_NO_RET(replacement && replacement->GetID() == id);
            IM_CHECK_NO_RET(!document.Resolve(ref));
            // Leave the owner alive so rejection depends on native lifetime.
        });
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, id) == "Replacement target");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == history);
        OnMainThread(ctx, [&] {
            (&App().WorkspaceForTests())->CloseOwner(owner);
            IM_CHECK_NO_RET(App().State().document->Context()->DestroyObject(id) == CK_OK);
        });
    };
    test = IM_REGISTER_TEST(engine, "modules", "application_messages_use_selected_commands_and_view");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration original, selected;
        std::vector<vdev::ObjectRef> selection;
        vdev::DocumentToken document{};
        size_t history = 0;
        bool dirty = false, redo = false;
        uint64_t shows = 0, replies = 0;
        vdev::EditorResponse pending;
        const auto configureOptions = [&](bool refuseReply, bool refuseView) {
            OnMainThread(ctx, [&] {
                const vdev::EditorClient client((&App().WorkspaceForTests())->Application().Host(), 0);
                IM_CHECK_NO_RET(client.Command("example.message-options", {
                    vdev::IntegerArgument("reply", refuseReply), vdev::IntegerArgument("view", refuseView)}).status == EDITOR_OK);
            });
        };
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *(&App().WorkspaceForTests());
            auto &editor = workspace.Application();
            original = editor.Configuration();
            selected = original;
            for (auto &choice : selected.providers)
                if (choice.kind == "command" && (choice.id == "message.show" || choice.id == "message.respond"))
                    choice.provider = "example.messages";
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(selected, error));
            const auto counts = vdev::EditorClient(editor.Host(), 0).Query("example.message-counts");
            shows = vdev::EditorClient::Integer(counts, "shows");
            replies = vdev::EditorClient::Integer(counts, "replies");
            document = state.document->Token();
            history = state.History().UndoCount();
            redo = state.History().CanRedo();
            dirty = state.document->Dirty();
            selection = state.objectSelection.References();
        });
        configureOptions(true, false);
        ctx->MenuClick("//##MainMenuBar/File/Source Control/Get Latest Version...");
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client((&App().WorkspaceForTests())->Application().Host(), 0);
            pending = client.Query("message.current");
            IM_CHECK_NO_RET(vdev::EditorClient::Text(pending, "text") == "Get Latest Version function not available" &&
                vdev::EditorClient::Integer(client.Query("example.message-counts"), "shows") == shows + 1);
        });
        ctx->ItemClick("//###ApplicationMessage/OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(ctx->GetWindowByRef("//###ApplicationMessage")->WasActive);
        OnMainThread(ctx, [&] {
            auto &workspace = *(&App().WorkspaceForTests());
            const vdev::EditorClient client(workspace.Application().Host(), 0);
            IM_CHECK_NO_RET(client.Query("message.current").fields == pending.fields &&
                vdev::EditorClient::Integer(client.Query("example.message-counts"), "replies") == replies + 1);
            const auto view = workspace.Open("application.messages");
            IM_CHECK_NO_RET(view && workspace.Close(view));
        });
        ctx->Yield(3);
        IM_CHECK(!ctx->GetWindowByRef("//###ApplicationMessage")->WasActive);
        OnMainThread(ctx, [&] {
            auto &workspace = *(&App().WorkspaceForTests());
            IM_CHECK_NO_RET(workspace.Open("application.messages"));
            const vdev::EditorClient client(workspace.Application().Host(), 0);
            IM_CHECK_NO_RET(client.Query("message.current").fields == pending.fields);
        });
        ctx->Yield(3);
        configureOptions(false, true);
        OnMainThread(ctx, [&] {
            auto replacement = selected;
            for (auto &choice : replacement.providers)
                if (choice.kind == "view" && choice.id == "application.messages") choice.provider = "example.messages";
            std::string error;
            IM_CHECK_NO_RET(!(&App().WorkspaceForTests())->Configure(replacement, error));
        });
        ctx->Yield(3);
        IM_CHECK(ctx->GetWindowByRef("//###ApplicationMessage")->WasActive);
        configureOptions(false, false);
        OnMainThread(ctx, [&] {
            for (auto &choice : selected.providers)
                if (choice.kind == "view" && choice.id == "application.messages") choice.provider = "example.messages";
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(selected, error));
        });
        ctx->Yield(4);
        IM_CHECK(!ctx->GetWindowByRef("//###ApplicationMessage")->WasActive);
        ctx->ItemClick("//External application message/Accept");
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *(&App().WorkspaceForTests());
            const vdev::EditorClient client(workspace.Application().Host(), 0);
            IM_CHECK_NO_RET(client.Query("message.current").status == EDITOR_NO_CHANGE &&
                vdev::EditorClient::Integer(client.Query("example.message-counts"), "replies") == replies + 2);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(original, error));
            IM_CHECK_NO_RET(state.document->Token() == document && state.document->Dirty() == dirty &&
                state.History().UndoCount() == history && state.History().CanRedo() == redo &&
                state.objectSelection.References() == selection);
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "duplicate_names_keep_drafts_cancel_and_retry_selected_command");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto visible = [&] {
            const auto *window = ctx->GetWindowByRef("//###DuplicateNames");
            return window && window->WasActive;
        };
        const auto file = std::filesystem::temp_directory_path() /
            ("duplicate-choices-" + std::to_string(SDL_GetTicksNS()) + ".nmo");
        struct RemoveFile { std::filesystem::path path; ~RemoveFile() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{file};
        struct Choices { unsigned calls = 0; std::vector<vdev::DuplicateAction> values; };
        const auto choices = std::make_shared<Choices>();
        vdev::EditorConfiguration previous;
        size_t history = 0;
        bool dirty = false;
        vdev::DocumentToken token{};
        std::vector<vdev::ObjectRef> selection;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            previous = editor.Configuration();
            auto selected = previous;
            bool replaced = false;
            for (auto &provider : selected.providers)
                if (provider.kind == "command" && provider.id == "document.respond") {
                    provider.provider = "example.file-replies";
                    replaced = true;
                }
            if (!replaced) selected.providers.push_back({"command", "document.respond", "example.file-replies"});
            std::string error;
            if (!(&App().WorkspaceForTests())->Configure(selected, error)) ctx->LogError("Configure: %s", error.c_str());
            IM_CHECK_NO_RET(error.empty());
            std::vector<CK_ID> objects;
            for (auto kind : {vdev::LayoutObject::Frame3D, vdev::LayoutObject::Material, vdev::LayoutObject::Texture}) {
                const auto made = vdev::CreateLayoutObject(*state.document, kind);
                IM_CHECK_NO_RET(made.ok);
                objects.push_back(made.created);
            }
            IM_CHECK_NO_RET(vdev::SaveObjectsAs(*state.document, objects, file.u8string(), &error));
            history = state.History().UndoCount();
            dirty = state.document->Dirty();
            token = state.document->Token();
            selection = state.objectSelection.References();
        });
        const auto begin = [&] {
            OnMainThread(ctx, [&] {
                App().State().files.WithDuplicates(file.u8string(), [choices](const std::vector<vdev::DuplicateName> *values) {
                    ++choices->calls;
                    choices->values.clear();
                    if (values) for (const auto &value : *values) choices->values.push_back(value.action);
                });
            });
            for (int i = 0; i < 100 && !visible(); ++i) ctx->Sleep(.01f);
        };
        const auto choose = [&](int node, const char *action) {
            ctx->SetRef("//###DuplicateNames");
            ctx->ItemClick(("**/$$" + std::to_string(node) + "/##row").c_str(), 1);
            ctx->Yield(2);
            ctx->ItemClick((std::string("//$FOCUSED/") + action).c_str());
            ctx->SetRef("");
            ctx->Yield(2);
        };
        begin();
        IM_CHECK(visible());
        choose(0, "Rename");
        OnMainThread(ctx, [&] {
            const auto *pending = App().State().files.PendingDuplicates();
            IM_CHECK_NO_RET(pending && std::all_of(pending->duplicates.begin(), pending->duplicates.end(),
                [](const auto &item) { return item.action == vdev::DuplicateAction::Ignore; }));
        });
        ctx->ItemClick("//###DuplicateNames/Cancel/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(!visible() && choices->calls == 0);
        begin();
        choose(0, "Use Current");
        choose(2, "Rename");
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client((&App().WorkspaceForTests())->Application().Host(), 0);
            IM_CHECK_NO_RET(client.Command("example.file-view-state", {vdev::IntegerArgument("refuse", 1)}).status == EDITOR_OK);
        });
        ctx->ItemClick("//###DuplicateNames/OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(visible() && choices->calls == 0);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const vdev::EditorClient client((&App().WorkspaceForTests())->Application().Host(), 0);
            IM_CHECK_NO_RET(state.files.PendingDuplicates() && state.History().UndoCount() == history &&
                            state.document->Dirty() == dirty && state.document->Token() == token &&
                            state.objectSelection.References() == selection);
            IM_CHECK_NO_RET(client.Command("example.file-view-state", {vdev::IntegerArgument("refuse", 0)}).status == EDITOR_OK);
        });
        ctx->ItemClick("//###DuplicateNames/OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK(!visible() && choices->calls == 1 && choices->values.size() >= 3);
        IM_CHECK(choices->values.front() == vdev::DuplicateAction::Rename);
        IM_CHECK(std::all_of(choices->values.begin() + 1, choices->values.end(),
                            [](auto value) { return value == vdev::DuplicateAction::UseCurrent; }));
        // An expired prompt closes only its own popup and cannot apply the old draft.
        begin();
        choose(0, "Rename");
        OnMainThread(ctx, [] { App().State().files.DocumentChanged(); });
        ctx->Yield(4);
        IM_CHECK(!visible() && choices->calls == 1);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.History().UndoCount() == history && state.document->Dirty() == dirty &&
                            state.document->Token() == token && state.objectSelection.References() == selection);
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(previous, error));
        });
        EditorOperation importing = 0;
        size_t entities = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const vdev::EditorClient client((&App().WorkspaceForTests())->Application().Host(), 0);
            std::vector<vdev::SetupObjectRow> rows;
            IM_CHECK_NO_RET(vdev::ReadSetupChoices(client, CKCID_3DENTITY, rows));
            entities = rows.size();
            const auto result = client.Command("document.import", {vdev::TextArgument("path", file.u8string())});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED);
            importing = result.operation;
        });
        for (int i = 0; i < 100 && !visible(); ++i) ctx->Sleep(.01f);
        IM_CHECK(visible());
        choose(0, "Rename");
        ctx->ItemClick("//###DuplicateNames/OK/##dialogbutton");
        EditorStatus completed = EDITOR_QUEUED;
        for (int i = 0; i < 1000 && completed == EDITOR_QUEUED; ++i) {
            ctx->Sleep(.01f);
            completed = Read<EditorStatus>(ctx, [&] {
                const vdev::EditorClient client((&App().WorkspaceForTests())->Application().Host(), 0);
                return client.Completion(importing).status;
            });
        }
        IM_CHECK(completed == EDITOR_OK && !visible());
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client((&App().WorkspaceForTests())->Application().Host(), 0);
            std::vector<vdev::SetupObjectRow> rows;
            IM_CHECK_NO_RET(vdev::ReadSetupChoices(client, CKCID_3DENTITY, rows) && rows.size() == entities + 1);
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "external_file_dialog_and_failed_replacement");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration original;
        vdev::DocumentToken document{};
        size_t history = 0, views = 0;
        EditorOperation operation = 0;
        const auto duplicateFile = std::filesystem::temp_directory_path() /
            ("external-duplicate-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".nmo");
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{duplicateFile};
        const auto loaded = std::make_shared<bool>(false), cancelled = std::make_shared<bool>(false);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *(&App().WorkspaceForTests());
            auto &editor = workspace.Application();
            original = editor.Configuration();
            const auto fixture = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(fixture.ok);
            std::string fileError;
            IM_CHECK_NO_RET(vdev::SaveObjectsAs(*state.document, {fixture.created}, duplicateFile.u8string(), &fileError));
            document = state.document->Token(); history = state.History().UndoCount(); views = workspace.ViewCount();
            auto choose = original;
            choose.providers.erase(std::remove_if(choose.providers.begin(), choose.providers.end(), [](const auto &p) {
                return p.kind == "view" && p.id == "document.file-dialogs";
            }), choose.providers.end());
            choose.providers.push_back({"view", "document.file-dialogs", "example.files"});
            const auto flag = [&](uint64_t value) {
                const EditorValue argument{vdev::EditorLiteral("refuse"), EDITOR_INTEGER_VALUE, {}, {}, value};
                return editor.Execute("example.file-view-state", {editor.Scope(), &argument, 1}).status;
            };
            IM_CHECK_NO_RET(flag(1) == EDITOR_OK);
            std::string error;
            IM_CHECK_NO_RET(!workspace.Configure(choose, error));
            IM_CHECK_NO_RET(workspace.ViewCount() == views);
            IM_CHECK_NO_RET(std::any_of(editor.Configuration().providers.begin(), editor.Configuration().providers.end(), [](const auto &p) {
                return p.kind == "view" && p.id == "document.file-dialogs" && p.provider == "builtin.file-dialogs";
            }));
            IM_CHECK_NO_RET(flag(0) == EDITOR_OK);
            IM_CHECK_NO_RET(workspace.Configure(choose, error));
            IM_CHECK_NO_RET(workspace.ViewCount() == views);
            const auto result = editor.Execute("document.open", {editor.Scope(), nullptr, 0});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED && result.operation);
            operation = result.operation;
        });
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            const EditorValue id{vdev::EditorLiteral("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation};
            const auto result = editor.Query("application.operation", {editor.Scope(), &id, 1});
            IM_CHECK_NO_RET(result.status == EDITOR_OK);
            IM_CHECK_NO_RET(std::any_of(result.fields.begin(), result.fields.end(), [](const auto &f) {
                return f.key == "status" && f.integer == EDITOR_CANCELLED;
            }));
            IM_CHECK_NO_RET(state.document->Token() == document && state.History().UndoCount() == history);
            state.files.WithDuplicates(duplicateFile.u8string(), [loaded](const std::vector<vdev::DuplicateName> *) { *loaded = true; },
                [cancelled](vdev::DocumentFiles::LoadResult::Stop stop) { *cancelled = stop == vdev::DocumentFiles::LoadResult::Stop::Cancelled; });
        });
        for (int i = 0; i < 100 && !*loaded && !*cancelled; ++i) ctx->Sleep(.01f);
        IM_CHECK(*cancelled && !*loaded);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const auto *popup = ImGui::FindWindowByName("###DuplicateNames");
            IM_CHECK_NO_RET(!state.files.PendingDuplicates() && (!popup || !popup->WasActive));
            IM_CHECK_NO_RET(state.document->Token() == document && state.History().UndoCount() == history);
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(original, error));
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->ViewCount() == views);
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "history_command_menu_and_shortcut_configuration");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration previous;
        CK_ID object = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *(&App().WorkspaceForTests());
            state.History().Clear();
            state.document->MarkSaved(); state.History().MarkSavePoint();
            const auto result = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(result.ok);
            object = result.created;
            previous = workspace.Application().Configuration();
            auto config = previous;
            config.bindings.emplace_back("history.undo", "Alt+U");
            config.menus.push_back({"menu.history.undo", "main.edit", 0});
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(3);
        ctx->WindowFocus("//3D Layout");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return App().State().document->IsLive(object); }));
        ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_U);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return !App().State().document->IsLive(object); }));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return App().State().document->IsLive(object); }));
        ctx->MenuClick("//##MainMenuBar/Views/Undo (3d Layout\\/Schematic only)");
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return !App().State().document->IsLive(object); }));
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(previous, error));
            IM_CHECK_NO_RET(state.History().Redo());
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "registered_creation_selection_and_stale_delete");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        vdev::EditorConfiguration previous;
        bool showSetup = false;
        CK_ID object = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            previous = (&App().WorkspaceForTests())->Application().Configuration();
            showSetup = App().SettingsForTests().Bool("3D Layout\\Auto Show Setup");
            App().SettingsForTests().SetBool("3D Layout\\Auto Show Setup", false);
            auto configured = previous;
            for (auto &choice : configured.providers)
                if (choice.kind == "command" && choice.id == "object.create-frame3d") choice.provider = "example.objects";
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(configured, error));
            state.History().Clear();
        });
        ctx->WindowFocus("//3D Layout");
        ctx->KeyPress(ImGuiKey_3);
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto *created = state.document->Context()->GetObjectByName("External Frame");
            IM_CHECK_NO_RET(created != nullptr);
            object = created->GetID();
            IM_CHECK_NO_RET(state.History().UndoCount() == 1);
            state.objectSelection.Restore(*state.document, {object});
        });
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return !App().State().document->IsLive(object); }));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
        ctx->Yield(2);
        OnMainThread(ctx, [&] { App().State().objectSelection.Restore(*App().State().document, {object}); });
        ctx->KeyPress(ImGuiKey_Space);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return App().State().objectSelection.Locked() && App().State().objectSelection.Contains(object); }));
        ctx->KeyPress(ImGuiKey_Space);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().objectSelection.Objects().empty() && App().State().History().UndoCount() == 1; }));
        OnMainThread(ctx, [&] { App().State().objectSelection.Restore(*App().State().document, {object}); });
        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(3);
        IM_CHECK(ctx->GetWindowByRef("//###DeleteOptions") != nullptr);
        OnMainThread(ctx, [&] {
            auto &document = *App().State().document;
            const auto old = document.Reference(object);
            auto *render = App().Layout().View().RenderContext();
            auto *renderObject = CKRenderObject::Cast(document.Resolve(old));
            // NONOTIFY skips the render manager. Keep this fixture's render
            // list valid while testing a command target's native lifetime.
            if (render && renderObject) render->RemoveObject(renderObject);
            IM_CHECK_NO_RET(!render || !renderObject || !render->IsObjectAttached(renderObject));
            IM_CHECK_NO_RET(document.Context()->DestroyObject(object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
            auto *replacement = document.Context()->CreateObject(CKCID_3DENTITY, "Delete replacement");
            IM_CHECK_NO_RET(replacement && replacement->GetID() == object && document.Reference(object) != old);
        });
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Delete replacement");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(previous, error));
            App().SettingsForTests().SetBool("3D Layout\\Auto Show Setup", showSetup);
            state.History().Clear();
            state.document->MarkSaved(); state.History().MarkSavePoint();
            const auto oldDocument = state.document->Token();
            auto &editor = (&App().WorkspaceForTests())->Application();
            IM_CHECK_NO_RET(editor.Execute("document.new", {editor.Scope(), nullptr, 0}).status == EDITOR_QUEUED);
            state.FinishRuntimeOperations();
            IM_CHECK_NO_RET(state.document->IsOpen() && state.document->Token() != oldDocument && !state.runtimeOperation.Busy());
        });
    };
    test = IM_REGISTER_TEST(engine, "modules", "resource_and_export_commands_use_registered_file_view");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        namespace fs = std::filesystem;
        const auto root = fs::temp_directory_path() /
            ("resource-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        IM_CHECK(fs::create_directory(root));
        struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
        const auto nmo = (root / "object.nmo").u8string(), nms = (root / "script.nms").u8string();
        CK_ID object = 0, script = 0;
        EditorOperation nmoOperation = 0, nmsOperation = 0, loadOperation = 0;
        size_t history = 0;
        vdev::DocumentToken token{};
        std::vector<vdev::ObjectRef> selection;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            const auto created = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(created.ok);
            object = created.created;
            const auto createdScript = vdev::CreateScript(*state.document, object, "Exported Script");
            IM_CHECK_NO_RET(createdScript.ok);
            script = createdScript.created;
            state.objectSelection.Restore(*state.document, {object});
            selection = state.objectSelection.References();
            history = state.History().UndoCount();
            token = state.document->Token();
            const auto ref = state.document->Reference(object);
            const EditorValue args[] = {
                {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {&ref, 1}},
                {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, vdev::EditorText(nmo)},
                {EDITOR_TEXT("ask-options"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
            const auto result = editor.Execute("object.export", {editor.Scope(), args, 3});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED);
            nmoOperation = result.operation;
        });
        ctx->Yield(4);
        IM_CHECK(ctx->ItemExists("//###LevelSaveAs/Save..."));
        ctx->ItemClick("//###LevelSaveAs/Hide script representation in Schematic view");
        ctx->ItemClick("//###LevelSaveAs/Save...");
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(fs::exists(nmo) && state.document->Token() == token && state.History().UndoCount() == history &&
                            state.objectSelection.References() == selection);
            auto &editor = (&App().WorkspaceForTests())->Application();
            const auto ref = state.document->Reference(script);
            const EditorValue args[] = {
                {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {&ref, 1}},
                {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, vdev::EditorText(nms)},
                {EDITOR_TEXT("ask-description"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
            const auto result = editor.Execute("script.export", {editor.Scope(), args, 3});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED);
            nmsOperation = result.operation;
        });
        ctx->Yield(5);
        IM_CHECK(ctx->ItemExists("//###behaviorinfo/##author"));
        ctx->ItemInputValue("//###behaviorinfo/##author", "UI Author");
        ctx->ItemInputValue("//###behaviorinfo/##version", "2.5");
        ctx->ItemClick("//###behaviorinfo/Hide script representation in schematic view");
        ctx->ItemClick("//###behaviorinfo/OK");
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            vdev::BehaviorDescription info;
            IM_CHECK_NO_RET(fs::exists(nms) && vdev::ReadBehaviorDescription(nms, info) && info.author == "UI Author" && info.version == 2.5f);
            IM_CHECK_NO_RET(state.History().UndoCount() == history && state.objectSelection.References() == selection);
            const auto ref = state.document->Reference(object);
            const auto file = vdev::EditorText(nms);
            const uint64_t cid = CKCID_BEHAVIOR;
            const EditorValue args[] = {
                {EDITOR_TEXT("paths"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {&file, 1}},
                {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {&cid, 1}},
                {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, {}, {}, 0, {&ref, 1}}};
            const auto result = editor.Execute("example.load-resources", {editor.Scope(), args, 3});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED);
            loadOperation = result.operation;
        });
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            IM_CHECK_NO_RET(Object<CKBeObject>(object)->GetScriptCount() == 2 && state.History().UndoCount() == history + 1 &&
                            state.objectSelection.References() == selection);
            for (const auto operation : {nmoOperation, nmsOperation, loadOperation}) {
                const EditorValue argument{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation};
                const auto result = editor.Query("application.operation", {editor.Scope(), &argument, 1});
                IM_CHECK_NO_RET(result.status == EDITOR_OK && std::any_of(result.fields.begin(), result.fields.end(), [](const auto &f) {
                    return f.key == "status" && f.integer == EDITOR_OK;
                }));
            }
            IM_CHECK_NO_RET(state.History().Undo() && Object<CKBeObject>(object)->GetScriptCount() == 1);
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "runtime_controls_share_commands_providers_and_bindings");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration previous;
        std::vector<vdev::EditorField> time;
        size_t history = 0;
        uint64_t plays = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            previous = editor.Configuration();
            auto selected = previous;
            for (auto &provider : selected.providers)
                if (provider.kind == "command" && provider.id == "runtime.play") provider.provider = "example.execution";
            selected.bindings.emplace_back("runtime.toggle-trace", "Alt+T");
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(selected, error));
            editor.Execute("runtime.open-debugger", {editor.Scope(), nullptr, 0});
            history = state.History().UndoCount();
            time = editor.Query("runtime.time", {editor.Scope(), nullptr, 0}).fields;
            const EditorValue value{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, 1};
            IM_CHECK_NO_RET(editor.Execute("runtime.set-max-frames", {editor.Scope(), &value, 1}).status == EDITOR_OK);
            plays = editor.Query("example.play-count", {editor.Scope(), nullptr, 0}).fields.front().integer;
        });
        ctx->Yield(4);
        ctx->ItemClick("//##StatusBar/play/##bitmap");
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            IM_CHECK_NO_RET(App().DebuggerForTests()->Frames() == 1 && App().DebuggerForTests()->GetState() == vdev::Debugger::State::Paused);
            IM_CHECK_NO_RET(editor.Query("example.play-count", {editor.Scope(), nullptr, 0}).fields.front().integer == plays + 1);
        });
        ctx->ItemClick("//##StatusBar/step/##bitmap");
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, [] { return App().DebuggerForTests()->Frames(); }) == 2);
        ctx->ItemClick("//##StatusBar/rewind/##bitmap");
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, [] { return App().DebuggerForTests()->Frames(); }) == 0);
        ctx->WindowFocus(RuntimeRef("debugger", "").c_str());
        ctx->ItemClick(RuntimeRef("debugger", "/Debug Inside Frames").c_str());
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [] { return App().DebuggerForTests()->trace && App().DebuggerForTests()->insideFrames; }));
        ctx->WindowFocus(SchematicRef().c_str());
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_T);
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [] { return App().DebuggerForTests()->trace; }));
        ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_T);
        ctx->Yield(3);
        IM_CHECK(!Read<bool>(ctx, [] { return App().DebuggerForTests()->trace; }));
        ctx->ItemClick("//##StatusBar/play/##bitmap", 1);
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//###TimeSettings/##scale"));
        ctx->ItemInput("//###TimeSettings/##scale");
        ctx->KeyCharsReplace("1.5");
        ctx->ItemClick("//###TimeSettings/OK/##dialogbutton");
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            const auto settings = editor.Query("runtime.time", {editor.Scope(), nullptr, 0});
            IM_CHECK_NO_RET(settings.status == EDITOR_OK && std::any_of(settings.fields.begin(), settings.fields.end(), [](const auto &f) {
                return f.key == "scale" && f.real == 1.5;
            }));
            IM_CHECK_NO_RET(state.History().UndoCount() == history);
            const auto restore = vdev::BorrowEditorFields(time);
            IM_CHECK_NO_RET(editor.Execute("runtime.set-time", {editor.Scope(), restore.data(), uint32_t(restore.size())}).status == EDITOR_OK);
            const EditorValue disabled{EDITOR_TEXT("enabled"), EDITOR_INTEGER_VALUE, {}, {}, 0};
            editor.Execute("runtime.set-inside-frames", {editor.Scope(), &disabled, 1});
            const EditorValue frames{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, 0};
            editor.Execute("runtime.set-max-frames", {editor.Scope(), &frames, 1});
            (&App().WorkspaceForTests())->Close((&App().WorkspaceForTests())->Role("main.debugger"));
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(previous, error));
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "web_print_inputs_and_owned_schematic_pages");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        namespace fs = std::filesystem;
        Settle(ctx);
        const auto root = fs::temp_directory_path() / ("output-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        IM_CHECK(fs::create_directory(root));
        struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
        const auto html = (root / "page.htm").u8string(), pages = (root / "pages").u8string();
        vdev::EditorConfiguration previous;
        EditorOperation web = 0, print = 0;
        size_t history = 0;
        bool dirty = false;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            state.document->MarkSaved(); state.History().MarkSavePoint();
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Application().Execute("document.new", {(&App().WorkspaceForTests())->Application().Scope(), nullptr, 0}).status == EDITOR_QUEUED);
            state.FinishRuntimeOperations();
            const auto frame = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(frame.ok);
            const auto script = vdev::CreateScript(*state.document, frame.created, "Printed Script");
            IM_CHECK_NO_RET(script.ok);
            const unsigned char green[] = {0, 200, 0, 255, 0, 200, 0, 255, 0, 200, 0, 255, 0, 200, 0, 255};
            void *bitmap = vdev::BitmapFromPixels(green, 2, 2);
            IM_CHECK_NO_RET(bitmap);
            state.document->KeepBitmap(bitmap);
            IM_CHECK_NO_RET(vdev::SetScriptSnapshot(*state.document, script.created, bitmap));
            previous = editor.Configuration();
            history = state.History().UndoCount(); dirty = state.document->Dirty();
        });
        ctx->Yield(6);
        OnMainThread(ctx, [&] {
            auto &editor = (&App().WorkspaceForTests())->Application();
            const EditorValue values[] = {{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, vdev::EditorText(html)},
                {EDITOR_TEXT("ask-options"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
            const auto result = editor.Execute("example.export-web", {editor.Scope(), values, 2});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED); web = result.operation;
        });
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//###CreateWebPage/##title"));
        ctx->ItemInput("//###CreateWebPage/##title"); ctx->KeyCharsReplace("Registered Web Page");
        ctx->ItemInput("//###CreateWebPage/##width"); ctx->KeyCharsReplace("640");
        ctx->ItemInput("//###CreateWebPage/##height"); ctx->KeyCharsReplace("480");
        ctx->ItemClick("//###CreateWebPage/##cmo");
        ctx->ItemClick("//###CreateWebPage/OK/##dialogbutton");
        ctx->Yield(4); OnMainThread(ctx, [] {});
        IM_CHECK(fs::exists(fs::u8path(html)) && fs::exists(root / "page.cmo"));
        std::ifstream in(fs::u8path(html), std::ios::binary);
        const std::string page{std::istreambuf_iterator<char>(in), {}};
        IM_CHECK(page.find("<TITLE>Registered Web Page</TITLE>") != std::string::npos);
        IM_CHECK(page.find("WIDTH=\"640\" HEIGHT=\"480\"") != std::string::npos);
        IM_CHECK(!vdev::IsPlayerFile((root / "page.cmo").u8string()));
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = (&App().WorkspaceForTests())->Application();
            IM_CHECK_NO_RET(state.History().UndoCount() == history && state.document->Dirty() == dirty);
            const EditorValue value{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, vdev::EditorText(pages)};
            const auto result = editor.Execute("example.print", {editor.Scope(), &value, 1});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED); print = result.operation;
        });
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//###PrintParameters/##pagesH"));
        ctx->KeyPress(ImGuiKey_Escape); ctx->Yield(3);
        IM_CHECK(!fs::exists(root / "pages" / "page1.png"));
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = (&App().WorkspaceForTests())->Application();
            const EditorValue id{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, print};
            const auto result = editor.Query("application.operation", {editor.Scope(), &id, 1});
            IM_CHECK_NO_RET(std::any_of(result.fields.begin(), result.fields.end(), [](const auto &f) { return f.key == "status" && f.integer == EDITOR_CANCELLED; }));
            const EditorValue value{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, vdev::EditorText(pages)};
            const auto started = editor.Execute("example.print", {editor.Scope(), &value, 1});
            IM_CHECK_NO_RET(started.status == EDITOR_QUEUED); print = started.operation;
        });
        ctx->Yield(3);
        ctx->ItemInput("//###PrintParameters/##pagesH"); ctx->KeyCharsReplace("2");
        ctx->ItemInput("//###PrintParameters/##pagesV"); ctx->KeyCharsReplace("2");
        ctx->ItemClick("//###PrintParameters/##fillShapes");
        ctx->ItemClick("//###PrintParameters/OK/##dialogbutton"); ctx->Yield(4);
        for (int i = 1; i <= 4; ++i) {
            std::vector<unsigned char> pixels; int w = 0, h = 0;
            IM_CHECK(vdev::ReadImageRgba((root / "pages" / ("page" + std::to_string(i) + ".png")).u8string(), pixels, w, h));
            IM_CHECK(w > 0 && h > 0 && pixels.size() == size_t(w) * h * 4);
            bool opaque = true; for (size_t at = 3; at < pixels.size(); at += 4) opaque &= pixels[at] == 255;
            IM_CHECK(opaque);
        }
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = (&App().WorkspaceForTests())->Application();
            IM_CHECK_NO_RET(state.History().UndoCount() == history && state.document->Dirty() == dirty);
            for (const auto operation : {web, print}) {
                const EditorValue id{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation};
                const auto result = editor.Query("application.operation", {editor.Scope(), &id, 1});
                IM_CHECK_NO_RET(std::any_of(result.fields.begin(), result.fields.end(), [](const auto &f) { return f.key == "status" && f.integer == EDITOR_OK; }));
            }
            auto selected = previous;
            for (auto &provider : selected.providers)
                if (provider.kind == "view" && provider.id == "document.output-dialogs") provider.provider = "example.outputs";
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(selected, error));
            IM_CHECK_NO_RET(editor.Execute("document.export-web", {editor.Scope(), nullptr, 0}).status == EDITOR_QUEUED);
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = (&App().WorkspaceForTests())->Application();
            IM_CHECK_NO_RET(editor.Query("output.input", {editor.Scope(), nullptr, 0}).status == EDITOR_NO_CHANGE);
            std::string error; IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(previous, error));
        });
        // The actual print renderer retains models and cloned bitmaps after
        // the original document/view has disappeared.
        std::unique_ptr<vdev::SchematicPrintContent> snapshot;
        OnMainThread(ctx, [&] { snapshot = App().Canvas().CapturePrint(); IM_CHECK_NO_RET(bool(snapshot)); });
        OnMainThread(ctx, [&] {
            auto &state = App().State(); state.document->MarkSaved(); state.History().MarkSavePoint();
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Application().Execute("document.new", {(&App().WorkspaceForTests())->Application().Scope(), nullptr, 0}).status == EDITOR_QUEUED);
            state.FinishRuntimeOperations();
            std::vector<unsigned char> pixels; std::string error;
            vdev::SchematicPrint options; options.bitmaps = true;
            IM_CHECK_NO_RET(snapshot->Render(0, 0, 600, 500, 1, options, pixels, error));
            IM_CHECK_NO_RET(pixels.size() == 600u * 500u * 4);
            size_t ink = 0, green = 0;
            for (size_t i = 0; i < pixels.size(); i += 4) {
                if (pixels[i] < 240 || pixels[i+1] < 240 || pixels[i+2] < 240) ++ink;
                if (int(pixels[i+1]) > int(pixels[i])+40 && int(pixels[i+1]) > int(pixels[i+2])+40) ++green;
            }
            IM_CHECK_NO_RET(ink > 100 && green > 0);
            snapshot.reset();
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "document_switch_failure_restores_live_views_and_selections");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = (&App().WorkspaceForTests())->Application();
            const auto frame = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(frame.ok);
            const auto script = vdev::CreateScript(*state.document, frame.created, "Retained Script");
            IM_CHECK_NO_RET(script.ok);
            state.document->MarkSaved(); state.History().MarkSavePoint();
            IM_CHECK_NO_RET(vdev::RenameLevelObject(*state.document, state.document->Reference(frame.created), "Redo after failed switch").ok);
            IM_CHECK_NO_RET(state.History().Undo());
            state.objectSelection.Restore(*state.document, {frame.created}, true);
            App().Canvas().Navigation().selection.Set(script.created, vdev::Selection::HeaderOf(script.created));
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = (&App().WorkspaceForTests())->Application();
            const auto context = state.document->Context(); const auto token = state.document->Token();
            const auto objectSelection = state.objectSelection;
            const auto selection = App().Canvas().Navigation().selection;
            const auto undo = state.History().UndoCount(); const auto redo = state.History().RedoLabel();
            const auto revision = state.document->TopologyRevision(); const auto dirty = state.document->Dirty();
            IM_CHECK_NO_RET(editor.Execute("runtime.play", {editor.Scope(), nullptr, 0}).status == EDITOR_OK);
            struct Views final : vdev::DocumentFiles::Views {
                vdev::EditorSession &state;
                vdev::DocumentFiles::Views *original;
                int mode = 0, detached = 0;
                bool released = false;
                explicit Views(vdev::EditorSession &state) : state(state), original(state.documentViews) { state.documentViews = this; }
                ~Views() { state.documentViews = original; }
                bool DocumentInteractionActive() const override { return mode == 3 || (mode == 2 && released) || original->DocumentInteractionActive(); }
                void DetachDocument(bool replaceContext) override {
                    ++detached; original->DetachDocument(replaceContext); released = true;
                    if (mode != 2) throw std::runtime_error("Injected failure after releasing actual views");
                }
                void RestoreDocument() override {
                    released = false; original->RestoreDocument();
                    if (mode == 1) throw std::runtime_error("Injected view recovery report");
                }
            } views(state);
            for (int mode : {0, 1, 2, 3}) {
                views.mode = mode;
                const auto status = editor.CommandState("document.new", {editor.Scope(), nullptr, 0});
                const auto result = editor.Execute("document.new", {editor.Scope(), nullptr, 0});
                if (mode == 3) IM_CHECK_NO_RET(status == EDITOR_REFUSED && result.status == EDITOR_REFUSED);
                else {
                    IM_CHECK_NO_RET(status == EDITOR_OK && result.status == EDITOR_QUEUED);
                    state.FinishRuntimeOperations();
                    const EditorValue operation{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, result.operation};
                    const auto completed = editor.Query("application.operation", {editor.Scope(), &operation, 1});
                    IM_CHECK_NO_RET(std::any_of(completed.fields.begin(), completed.fields.end(), [](const auto &f) { return f.key == "status" && f.integer == EDITOR_ERROR; }));
                }
                IM_CHECK_NO_RET(state.document->Context() == context && state.document->Token() == token);
                IM_CHECK_NO_RET(state.document->TopologyRevision() == revision && state.document->Dirty() == dirty);
                IM_CHECK_NO_RET(state.History().UndoCount() == undo && state.History().CanRedo() && state.History().RedoLabel() == redo);
                IM_CHECK_NO_RET(App().Canvas().Navigation().selection.script == selection.script && App().Canvas().Navigation().selection.items == selection.items);
                IM_CHECK_NO_RET(state.objectSelection.References() == objectSelection.References() && state.objectSelection.Locked() == objectSelection.Locked() && state.objectSelection.Revision() == objectSelection.Revision());
                IM_CHECK_NO_RET(App().DebuggerForTests()->GetState() == vdev::Debugger::State::Running);
            }
            IM_CHECK_NO_RET(views.detached == 3);
            editor.Execute("runtime.pause", {editor.Scope(), nullptr, 0});
        });
        ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [] { return App().Layout().UsesGpuViewport(); }));
    };

    test = IM_REGISTER_TEST(engine, "modules", "layout_behavior_drop_and_parameters_use_selected_providers");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration previous;
        vdev::ObjectRef owner{}, block{};
        uint64_t prototype = 0, drops = 0, finishes = 0;
        size_t history = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            previous = editor.Configuration();
            auto chosen = previous;
            for (auto &provider : chosen.providers)
                if (provider.kind == "command" &&
                    (provider.id == "scene.drop-behavior" || provider.id == "layout.finish-drop"))
                    provider.provider = "example.authoring";
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(chosen, error));
            const auto made = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            owner = state.document->Reference(made.created);
            history = state.History().UndoCount();
            for (const auto &entry : App().CatalogForTests().Entries())
                if (entry.name == "Rotate")
                    prototype = (uint64_t(entry.guid.d1) << 32) | entry.guid.d2;
            IM_CHECK_NO_RET(prototype);
            const auto counts = editor.Query("example.authoring-counts", {editor.Scope(), nullptr, 0});
            drops = vdev::EditorClient::Integer(counts, "drops");
            finishes = vdev::EditorClient::Integer(counts, "finishes");
            const EditorValue args[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {&owner, 1}},
                                        vdev::IntegerArgument("prototype", prototype)};
            const auto result = editor.Execute("scene.drop-behavior", {editor.Scope(), args, 2});
            IM_CHECK_NO_RET(result.status == EDITOR_OK && state.document->EditOpen());
            for (const auto &field : result.fields)
                if (field.key == "created")
                    block = field.object;
        });
        ctx->Yield(4);
        IM_CHECK(ctx->GetWindowByRef("//###DropParameters"));
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            const auto counts = editor.Query("example.authoring-counts", {editor.Scope(), nullptr, 0});
            IM_CHECK_NO_RET(vdev::EditorClient::Integer(counts, "drops") == drops + 1 &&
                            vdev::EditorClient::Integer(counts, "finishes") == finishes + 1);
            IM_CHECK_NO_RET(!state.document->EditOpen() && state.History().UndoCount() == history + 1 &&
                            state.document->Resolve(block));
            IM_CHECK_NO_RET(state.History().Undo() && !state.document->Resolve(block) && state.History().CanRedo());
            const EditorValue args[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {&owner, 1}},
                                        vdev::IntegerArgument("prototype", prototype)};
            IM_CHECK_NO_RET(editor.Execute("scene.drop-behavior", {editor.Scope(), args, 2}).status == EDITOR_OK);
        });
        ctx->Yield(4);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = (&App().WorkspaceForTests())->Application();
            const auto counts = editor.Query("example.authoring-counts", {editor.Scope(), nullptr, 0});
            IM_CHECK_NO_RET(vdev::EditorClient::Integer(counts, "drops") == drops + 2 &&
                            vdev::EditorClient::Integer(counts, "finishes") == finishes + 2);
            IM_CHECK_NO_RET(!state.document->EditOpen() && state.History().UndoCount() == history &&
                            state.History().CanRedo());
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(previous, error));
        });
    };
}
} // namespace vdevuitest

#include "tests/ui/UiTests.h"
#include "tests/ResourceFiles.h"
#include "workspace/ResourceDrag.h"
#include "workspace/ObjectDrag.h"
#include "features/layout/SnapshotData.h"
#include "features/scene/SceneViewData.h"
#include "features/scene/SceneViews.h"
#include "features/setup/SetupViewData.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

namespace vdevuitest {
namespace {
struct DragSource {
    std::string type;
    std::vector<unsigned char> bytes;
    bool visible = false;
    ~DragSource() { visible = false; }
} source;

void DropGui(ImGuiTestContext *ctx) {
    MainThreadGui(ctx);
    if (!source.visible)
        return;
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 180, viewport->WorkPos.y + viewport->WorkSize.y - 90),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(170, 75), ImGuiCond_Always);
    ImGui::Begin("Layout Drop Source", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
    ImGui::Button("Drag resource", ImVec2(140, 25));
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(source.type.c_str(), source.bytes.data(), source.bytes.size());
        ImGui::TextUnformatted("Test resource");
        ImGui::EndDragDropSource();
    }
    ImGui::End();
}
void Drag(ImGuiTestContext *ctx, const ImVec2 &to, bool cancel = false) {
    ctx->MouseMove("//Layout Drop Source/Drag resource");
    ctx->MouseDown(0);
    const auto from = ctx->UiContext->IO.MousePos;
    ctx->MouseMoveToPos(ImVec2(from.x - 25, from.y - 20));
    ctx->MouseMoveToPos(to);
    ctx->Yield(3);
    if (cancel)
        ctx->MouseMoveToPos(from);
    ctx->MouseUp(0);
    ctx->Yield(3);
}
std::set<CK_ID> LiveObjects(CK_CLASSID cid) {
    std::set<CK_ID> result;
    auto &doc = *App().State().document;
    const auto &objects = doc.Context()->GetObjectListByType(cid, FALSE);
    for (int i = 0; i < objects.Size(); ++i)
        if (objects[i] && doc.IsLive(objects[i]->GetID()))
            result.insert(objects[i]->GetID());
    return result;
}
void Bitmap(const std::filesystem::path &path) {
    // A 2 x 2, bottom-up, uncompressed 24-bit BMP; padded rows have eight bytes.
    unsigned char bytes[70]{};
    bytes[0] = 'B';
    bytes[1] = 'M';
    bytes[2] = 70;
    bytes[10] = 54;
    bytes[14] = 40;
    bytes[18] = 2;
    bytes[22] = 2;
    bytes[26] = 1;
    bytes[28] = 24;
    bytes[34] = 16;
    for (int i : {54, 57, 62, 65}) {
        bytes[i] = 20;
        bytes[i + 1] = 90;
        bytes[i + 2] = 180;
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}
struct LongResourceFolder {
    std::filesystem::path root, folder;
    LongResourceFolder() {
        root = vdevtest::ResourceTestRoot("resource-long-path-");
        folder = vdevtest::LongResourceDirectory(root);
    }
    ~LongResourceFolder() {
        source.visible = false;
        source.bytes.clear();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
} // namespace

void RegisterLayoutDropTests(ImGuiTestEngine *engine) {
    ImGuiTest *test = nullptr;

    test = IM_REGISTER_TEST(engine, "layout", "long_resource_path_schematic_snapshot_cancel_and_undo");
    test->GuiFunc = DropGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        LongResourceFolder sample;
        const auto path = (sample.folder / "Script image.bmp").u8string();
        Bitmap(std::filesystem::u8path(path));
        auto &state = App().State();
        const vdev::EditorClient client(state.workspace->Application().Host(), 0);
        vdev::ObjectRef script{};
        OnMainThread(ctx, [&] {
            const auto scripts = vdev::SceneObjects(client.Query("schematic.stack"), "all");
            IM_CHECK_NO_RET(!scripts.empty());
            if (!scripts.empty())
                script = scripts.front();
            IM_CHECK_NO_RET(client.Command("script.navigate", {vdev::ObjectArgument("target", script)}).status == EDITOR_OK);
        });
        ctx->Yield(5);
        ctx->WindowFocus(SchematicRef().c_str());
        ctx->Yield(3);
        ImVec2 at(-1, -1);
        for (const auto &shown : App().Canvas().ShownScripts())
            if (shown.script == script.object)
                at = {shown.headerMin.x + 40, shown.headerMin.y + 8};
        IM_CHECK(at.x >= 0);
        const auto pixels = [&] {
            const auto model = client.Query("schematic.model", {vdev::ObjectArgument("target", script),
                                                                vdev::IntegerArgument("open", 1)});
            const auto picture = client.Query("schematic.snapshot",
                                              {vdev::ObjectArgument("target", script), vdev::IntegerArgument("open", 1),
                                               vdev::IntegerArgument("revision", vdev::EditorClient::Integer(model, "revision")),
                                               vdev::IntegerArgument("topology", vdev::EditorClient::Integer(model, "topology")),
                                               vdev::IntegerArgument("view-revision", vdev::EditorClient::Integer(model, "view-revision"))});
            IM_CHECK_NO_RET(model.status == EDITOR_OK && picture.status == EDITOR_OK);
            return vdev::SceneIntegers(picture, "pixels");
        };
        const auto before = Read<std::vector<uint64_t>>(ctx, pixels);
        const auto steps = state.History().UndoCount();
        const bool dirty = state.document->Dirty();
        const auto reads = App().Layout().View().ReadbackFrames();
        source.visible = true;
        source.type = EDITOR_RESOURCE_FILE_PAYLOAD;
        source.bytes = vdev::WriteResourceDrag(0, {{path, CKCID_TEXTURE}});
        ctx->Yield(3);
        Drag(ctx, at, true);
        IM_CHECK(Read<bool>(ctx, [&] { return pixels() == before && state.History().UndoCount() == steps; }));
        Drag(ctx, at);
        const auto deadline = SDL_GetTicks() + 10000;
        bool loaded = false;
        while (SDL_GetTicks() < deadline) {
            loaded = Read<bool>(ctx, [&] { return state.History().UndoCount() == steps + 1; });
            if (loaded)
                break;
            ctx->Sleep(.01f);
        }
        IM_CHECK(loaded);
        const auto after = Read<std::vector<uint64_t>>(ctx, pixels);
        IM_CHECK(!after.empty() && after != before);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(client.Command("history.undo").status == EDITOR_OK && pixels() == before);
            IM_CHECK_NO_RET(client.Command("history.redo").status == EDITOR_OK && pixels() == after);
            IM_CHECK_NO_RET(client.Command("history.undo").status == EDITOR_OK && pixels() == before);
        });
        source.bytes = vdev::WriteResourceDrag(0, {{path + ".missing.bmp", CKCID_TEXTURE}});
        Drag(ctx, at);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(pixels() == before && state.History().UndoCount() == steps && state.History().CanRedo() &&
                            state.document->Dirty() == dirty);
            IM_CHECK_NO_RET(App().Layout().View().ReadbackFrames() == reads);
        });
    };

    test = IM_REGISTER_TEST(engine, "layout", "long_resource_paths_to_layout_and_level_keep_selection");
    test->GuiFunc = DropGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        LongResourceFolder sample;
        const std::vector<vdev::ResourceFile> files{{(sample.folder / "First image.bmp").u8string(), CKCID_TEXTURE},
                                                   {(sample.folder / "Second image.bmp").u8string(), CKCID_TEXTURE}};
        for (const auto &file : files)
            Bitmap(std::filesystem::u8path(file.path));
        InView found;
        IM_CHECK(SelectEntity(ctx, found));
        auto &state = App().State();
        auto &workspace = *state.workspace;
        const vdev::EditorClient client(workspace.Application().Host(), 0);
        const auto textures = [&] {
            std::vector<vdev::SetupObjectRow> rows;
            std::set<vdev::ObjectRef> result;
            if (vdev::ReadSetupChoices(client, CKCID_TEXTURE, rows))
                for (const auto &row : rows)
                    result.insert(row.object);
            return result;
        };
        source.visible = true;
        source.type = EDITOR_RESOURCE_FILE_PAYLOAD;
        source.bytes = vdev::WriteResourceDrag(1, files);
        ctx->Yield(3);
        const uint64_t reads = App().Layout().View().ReadbackFrames();
        const uint64_t uploads = App().Layout().ViewportUploadsForTests();
        const uint64_t allocations = App().Layout().GpuViewportCreationsForTests();
        for (bool level : {false, true}) {
            ImVec2 at = found.at;
            for (bool locked : {false, true}) {
                std::set<vdev::ObjectRef> before;
                std::vector<vdev::ObjectRef> selected;
                size_t steps = 0;
                bool redo = false, dirty = false;
                OnMainThread(ctx, [&] {
                    const auto result = client.Command("selection.lock", {vdev::IntegerArgument("locked", locked)});
                    IM_CHECK_NO_RET(result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE);
                    selected = vdev::SceneObjects(client.Query("selection.objects"), "objects");
                    before = textures();
                    steps = state.History().UndoCount();
                    redo = state.History().CanRedo();
                    dirty = state.document->Dirty();
                });
                if (level) {
                    ctx->WindowFocus(SceneRef("level").c_str());
                    ctx->Yield(3);
                    at = Read<ImVec2>(ctx, [&] {
                        for (const auto &row : vdev::SceneRowsForTests(workspace.Role("main.level")))
                            if (row.cid == CKCID_LEVEL)
                                return ImVec2(row.min.x + 130, (row.min.y + row.max.y) * .5f);
                        return ImVec2(-1, -1);
                    });
                    IM_CHECK(at.x >= 0);
                }
                Drag(ctx, at, true);
                OnMainThread(ctx, [&] {
                    IM_CHECK_NO_RET(textures() == before && state.History().UndoCount() == steps &&
                                    state.History().CanRedo() == redo && state.document->Dirty() == dirty);
                });
                Drag(ctx, at);
                const auto deadline = SDL_GetTicks() + 10000;
                bool loaded = false;
                while (SDL_GetTicks() < deadline) {
                    loaded = Read<bool>(ctx, [&] { return textures().size() == before.size() + 2; });
                    if (loaded)
                        break;
                    ctx->Sleep(.01f);
                }
                if (!loaded)
                    ctx->LogInfo("receiver=%s locked=%d textures=%zu expected=%zu", level ? "level" : "layout", locked,
                                 Read<size_t>(ctx, [&] { return textures().size(); }), before.size() + 2);
                IM_CHECK(loaded);
                OnMainThread(ctx, [&] {
                    std::set<std::string> paths;
                    for (auto object : textures())
                        if (!before.count(object)) {
                            vdev::SetupPageData page;
                            IM_CHECK_NO_RET(vdev::ReadSetupPage(client, object, -1, page));
                            paths.insert(page.filename);
                        }
                    IM_CHECK_NO_RET(paths == std::set<std::string>({files[0].path, files[1].path}));
                    IM_CHECK_NO_RET(state.History().UndoCount() == steps && state.History().CanRedo() == redo);
                    const auto selection = client.Query("selection.objects");
                    IM_CHECK_NO_RET(vdev::SceneObjects(selection, "objects") == selected &&
                                    bool(vdev::EditorClient::Integer(selection, "locked")) == locked);
                    IM_CHECK_NO_RET(App().Layout().View().ReadbackFrames() == reads &&
                                    App().Layout().ViewportUploadsForTests() == uploads &&
                                    App().Layout().GpuViewportCreationsForTests() == allocations);
                });
            }
        }
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(client.Command("selection.lock", {vdev::IntegerArgument("locked", 0)}).status == EDITOR_OK);
        });
    };

    test = IM_REGISTER_TEST(engine, "layout", "long_resource_path_add_slot_keeps_source_and_undo");
    test->GuiFunc = DropGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        LongResourceFolder sample;
        const auto first = (sample.folder / "First image.bmp").u8string();
        const auto second = (sample.folder / "Second image.bmp").u8string();
        Bitmap(std::filesystem::u8path(first));
        Bitmap(std::filesystem::u8path(second));
        IM_CHECK(first.size() > 260 && second.size() > 260);
        auto &state = App().State();
        auto &workspace = *state.workspace;
        const vdev::EditorClient client(workspace.Application().Host(), 0);
        vdev::ObjectRef target{};
        EditorView setup = 0;
        OnMainThread(ctx, [&] {
            const auto created = client.Command("object.create-texture");
            const auto *object = vdev::EditorClient::Find(created, "created");
            IM_CHECK_NO_RET(created.status == EDITOR_OK && object && object->object);
            target = object ? object->object : vdev::ObjectRef{};
            vdev::SetupDetails data;
            IM_CHECK_NO_RET(vdev::ReadSetupDetails(client, target, "bitmap", data));
            IM_CHECK_NO_RET(client.Command("setup.slot-replace",
                                          {vdev::ObjectArgument("target", target), vdev::TextArgument("path", first),
                                           vdev::IntegerArgument("slot", 0),
                                           vdev::IntegerArgument("document-revision", data.documentRevision)})
                                .status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("object.open-setup", {vdev::ObjectArgument("target", target)}).status == EDITOR_OK);
            const auto instances = client.Query("workspace.instances");
            const auto *views = vdev::EditorClient::Find(instances, "views");
            IM_CHECK_NO_RET(views);
            if (views)
                for (auto view : views->integers) {
                    const auto info = client.Inspect(view);
                    const auto *object = vdev::EditorClient::Find(info, "target");
                    if (object && object->object == target)
                        setup = view;
                }
            IM_CHECK_NO_RET(setup);
        });
        ctx->Yield(4);
        const auto window = "//" + workspace.WindowName(setup);
        ctx->WindowMove(window.c_str(), {10, 30});
        ctx->WindowResize(window.c_str(), {1300, 740});
        ctx->Yield(3);
        const auto preview = window + "/**/image/##preview";
        IM_CHECK(ctx->ItemExists(preview.c_str()));
        const auto at = ctx->ItemInfo(preview.c_str()).RectFull.GetCenter();
        source.visible = true;
        source.type = EDITOR_RESOURCE_FILE_PAYLOAD;
        source.bytes = vdev::WriteResourceDrag(1, {{first, CKCID_TEXTURE}, {second, CKCID_TEXTURE}});
        ctx->Yield(3);
        const size_t steps = state.History().UndoCount();
        for (auto modifier : {ImGuiMod_Ctrl, ImGuiMod_Shift}) {
            ctx->KeyDown(modifier);
            Drag(ctx, at);
            ctx->KeyUp(modifier);
            OnMainThread(ctx, [&] {
                vdev::SetupDetails data;
                vdev::SetupPageData page;
                IM_CHECK_NO_RET(vdev::ReadSetupDetails(client, target, "bitmap", data) && data.Values().Integer("slots") == 2);
                IM_CHECK_NO_RET(vdev::ReadSetupPage(client, target, -1, page) && page.filename == second);
                IM_CHECK_NO_RET(state.History().UndoCount() == steps + 1);
                IM_CHECK_NO_RET(client.Command("history.undo").status == EDITOR_OK);
                IM_CHECK_NO_RET(vdev::ReadSetupDetails(client, target, "bitmap", data) && data.Values().Integer("slots") == 1);
                IM_CHECK_NO_RET(client.Command("history.redo").status == EDITOR_OK);
                IM_CHECK_NO_RET(vdev::ReadSetupPage(client, target, -1, page) && page.filename == second);
                IM_CHECK_NO_RET(client.Command("history.undo").status == EDITOR_OK);
            });
            const bool dirty = state.document->Dirty();
            ctx->KeyDown(modifier);
            Drag(ctx, at, true);
            ctx->KeyUp(modifier);
            source.bytes = vdev::WriteResourceDrag(1, {{first, CKCID_TEXTURE}, {second + ".missing", CKCID_TEXTURE}});
            ctx->KeyDown(modifier);
            Drag(ctx, at);
            ctx->KeyUp(modifier);
            OnMainThread(ctx, [&] {
                vdev::SetupDetails data;
                vdev::SetupPageData page;
                IM_CHECK_NO_RET(vdev::ReadSetupDetails(client, target, "bitmap", data) && data.Values().Integer("slots") == 1);
                IM_CHECK_NO_RET(vdev::ReadSetupPage(client, target, -1, page) && page.filename == first);
                IM_CHECK_NO_RET(state.History().UndoCount() == steps && state.History().CanRedo() &&
                                state.document->Dirty() == dirty);
            });
            source.bytes = vdev::WriteResourceDrag(1, {{first, CKCID_TEXTURE}, {second, CKCID_TEXTURE}});
        }
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(client.Command("workspace.close-view", {vdev::IntegerArgument("view", setup)}).status == EDITOR_OK);
        });
    };

    test = IM_REGISTER_TEST(engine, "layout", "locked_material_multi_drag_uses_grabbed_row_and_rejects_bad_tail");
    test->GuiFunc = DropGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK(SelectEntity(ctx, found));
        auto &state = App().State();
        const vdev::EditorClient client(state.workspace->Application().Host(), 0);
        struct Cleanup {
            ~Cleanup() {
                source.visible = false;
                source.bytes.clear();
            }
        } cleanup;
        vdev::ObjectRef material{}, other{};
        ImVec2 min, max;
        IM_CHECK(App().Layout().ShownView(min, max));
        const auto pick = [&] {
            EditorValue x{EDITOR_TEXT("x"), EDITOR_REAL_VALUE}, y{EDITOR_TEXT("y"), EDITOR_REAL_VALUE};
            x.real = found.at.x - min.x;
            y.real = found.at.y - min.y;
            return client.Query("layout.drop-target", {x, y, vdev::TextArgument("kind", std::string("material")),
                                                       vdev::ObjectArgument("source", material)});
        };
        OnMainThread(ctx, [&] {
            const auto first = client.Command("object.create-material"),
                       second = client.Command("object.create-material");
            const auto *a = vdev::EditorClient::Find(first, "created"),
                       *b = vdev::EditorClient::Find(second, "created");
            IM_CHECK_NO_RET(first.status == EDITOR_OK && second.status == EDITOR_OK && a && b);
            material = a ? a->object : vdev::ObjectRef{};
            other = b ? b->object : vdev::ObjectRef{};
            bool hit = false;
            for (int y = 1; y < 16 && !hit; ++y)
                for (int x = 1; x < 20 && !hit; ++x) {
                    found.at = {min.x + (max.x - min.x) * x / 20, min.y + (max.y - min.y) * y / 16};
                    const auto info = pick();
                    const auto *mesh = vdev::EditorClient::Find(info, "mesh");
                    hit = info.status == EDITOR_OK && mesh && mesh->object;
                }
            IM_CHECK_NO_RET(hit);
        });
        source.visible = true;
        source.type = EDITOR_OBJECT_PAYLOAD;
        ctx->Yield(3);
        for (bool locked : {false, true}) {
            vdev::ObjectRef original{};
            size_t steps = 0;
            std::vector<vdev::ObjectRef> selected;
            OnMainThread(ctx, [&] {
                const auto changed = client.Command("selection.lock", {vdev::IntegerArgument("locked", locked)});
                IM_CHECK_NO_RET(changed.status == EDITOR_OK || changed.status == EDITOR_NO_CHANGE);
                const auto before = pick();
                const auto *value = vdev::EditorClient::Find(before, "material");
                IM_CHECK_NO_RET(before.status == EDITOR_OK && value && value->object);
                original = value ? value->object : vdev::ObjectRef{};
                selected = vdev::SceneObjects(client.Query("selection.objects"), "objects");
                steps = state.History().UndoCount();
            });
            source.bytes = vdev::WriteObjectDrag(material, {other, material});
            Drag(ctx, found.at);
            OnMainThread(ctx, [&] {
                const auto after = pick();
                const auto *value = vdev::EditorClient::Find(after, "material");
                IM_CHECK_NO_RET(value && value->object == material && state.History().UndoCount() == steps + 1);
                IM_CHECK_NO_RET(vdev::SceneObjects(client.Query("selection.objects"), "objects") == selected &&
                                bool(vdev::EditorClient::Integer(client.Query("selection.objects"), "locked")) ==
                                    locked);
                IM_CHECK_NO_RET(client.Command("history.undo").status == EDITOR_OK);
                const auto restored = pick();
                value = vdev::EditorClient::Find(restored, "material");
                IM_CHECK_NO_RET(value && value->object == original);
            });
            const vdev::ObjectRef invalid[] = {material, {}};
            source.bytes.resize(sizeof(invalid));
            std::memcpy(source.bytes.data(), invalid, sizeof(invalid));
            Drag(ctx, found.at);
            OnMainThread(ctx, [&] {
                const auto after = pick();
                const auto *value = vdev::EditorClient::Find(after, "material");
                IM_CHECK_NO_RET(value && value->object == original && state.History().UndoCount() == steps &&
                                state.History().CanRedo());
            });
        }
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(client.Command("selection.lock", {vdev::IntegerArgument("locked", 0)}).status == EDITOR_OK);
        });
    };

    test = IM_REGISTER_TEST(engine, "layout", "resource_image_modes_and_malformed_drags");
    test->GuiFunc = DropGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        namespace fs = std::filesystem;
        const auto folder =
            fs::temp_directory_path() /
            ("layout-drops-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        IM_CHECK(fs::create_directory(folder));
        struct Cleanup {
            fs::path folder;
            ~Cleanup() {
                source.visible = false;
                source.bytes.clear();
                std::error_code error;
                fs::remove_all(folder, error);
            }
        } cleanup{folder};
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK(SelectEntity(ctx, found));
        source.visible = true;
        ctx->Yield(3);
        const CK_CLASSID kinds[] = {CKCID_TEXTURE, CKCID_SPRITE3D, CKCID_SPRITE, CKCID_2DENTITY};
        for (unsigned mode = 0; mode < 8; ++mode) {
            const auto kind = mode & 3;
            const bool locked = mode >= 4;
            OnMainThread(ctx, [&] {
                const vdev::EditorClient client(App().State().workspace->Application().Host(), 0);
                const auto result = client.Command("selection.lock", {vdev::IntegerArgument("locked", locked)});
                IM_CHECK_NO_RET(result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE);
            });
            const auto path = folder / ("drop-" + std::to_string(mode) + ".bmp");
            Bitmap(path);
            source.type = EDITOR_RESOURCE_FILE_PAYLOAD;
            source.bytes = vdev::WriteResourceDrag(0, {{path.u8string(), CKCID_TEXTURE}});
            const auto before = Read<std::set<CK_ID>>(ctx, [&] { return LiveObjects(kinds[kind]); });
            std::vector<vdev::ObjectRef> selected;
            size_t history = 0;
            OnMainThread(ctx, [&] {
                selected = App().State().objectSelection.References();
                history = App().State().History().UndoCount();
            });
            if (kind & 1)
                ctx->KeyDown(ImGuiMod_Shift);
            if (kind & 2)
                ctx->KeyDown(ImGuiMod_Ctrl);
            Drag(ctx, found.at);
            if (kind & 2)
                ctx->KeyUp(ImGuiMod_Ctrl);
            if (kind & 1)
                ctx->KeyUp(ImGuiMod_Shift);
            const auto deadline = SDL_GetTicks() + 10000;
            bool loaded = false;
            while (SDL_GetTicks() < deadline) {
                loaded = Read<bool>(ctx, [&] {
                    return !App().State().runtimeOperation.Busy() &&
                           LiveObjects(kinds[kind]).size() == before.size() + 1;
                });
                if (loaded)
                    break;
                ctx->Sleep(0.01f);
            }
            IM_CHECK(loaded);
            OnMainThread(ctx, [&] {
                // Image resources retain their existing non-undoable adoption.
                IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
                IM_CHECK_NO_RET(App().State().objectSelection.References() == selected &&
                                App().State().objectSelection.Locked() == locked);
            });
        }
        // Receivers must validate every payload size before reading its fields.
        const char *types[] = {EDITOR_ATTRIBUTE_PAYLOAD, EDITOR_RESOURCE_FILE_PAYLOAD, EDITOR_BUILDING_BLOCK_PAYLOAD,
                               EDITOR_OBJECT_PAYLOAD};
        for (const auto *type : types) {
            source.type = type;
            source.bytes = {1};
            size_t history = 0;
            bool redo = false, dirty = false;
            std::vector<vdev::ObjectRef> selection;
            OnMainThread(ctx, [&] {
                history = App().State().History().UndoCount();
                redo = App().State().History().CanRedo();
                dirty = App().State().document->Dirty();
                selection = App().State().objectSelection.References();
            });
            Drag(ctx, found.at);
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(!App().State().document->EditOpen() && !App().State().runtimeOperation.Busy());
                IM_CHECK_NO_RET(App().State().History().UndoCount() == history &&
                                App().State().History().CanRedo() == redo);
                IM_CHECK_NO_RET(App().State().document->Dirty() == dirty &&
                                App().State().objectSelection.References() == selection);
            });
        }
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(App().State().workspace->Application().Host(), 0);
            IM_CHECK_NO_RET(client.Command("selection.lock", {vdev::IntegerArgument("locked", 0)}).status == EDITOR_OK);
        });
    };

    test = IM_REGISTER_TEST(engine, "layout", "locked_attribute_and_snapshot_drops_keep_selection_and_undo");
    test->GuiFunc = DropGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK(SelectEntity(ctx, found));
        auto &state = App().State();
        const vdev::EditorClient client(state.workspace->Application().Host(), 0);
        struct Cleanup {
            ~Cleanup() {
                source.visible = false;
                source.bytes.clear();
            }
        } cleanup;
        EditorAttributePayload attribute{};
        vdev::SnapshotDrag image{};
        vdev::SnapshotData snapshots;
        std::vector<vdev::ObjectRef> selected;
        uint64_t reads = 0;
        OnMainThread(ctx, [&] {
            selected = vdev::SceneObjects(client.Query("selection.objects"), "objects");
            IM_CHECK_NO_RET(
                client.Command("attribute.create-category", {vdev::TextArgument("name", std::string("Drop Tests"))})
                    .status == EDITOR_OK);
            const auto type =
                client.Command("attribute.create-type",
                               {vdev::TextArgument("name", std::string("Drop Value")),
                                vdev::IntegerArgument("parameter", (uint64_t(CKPGUID_INT.d1) << 32) | CKPGUID_INT.d2),
                                vdev::TextArgument("category", std::string("Drop Tests"))});
            IM_CHECK_NO_RET(type.status == EDITOR_OK);
            attribute.type = int32_t(vdev::EditorClient::Integer(type, "type"));
            attribute.catalog = vdev::EditorClient::Integer(client.Query("attribute.catalog"), "catalog");
            reads = App().Layout().View().ReadbackFrames();
            const auto taken = client.Command("snapshot.take");
            IM_CHECK_NO_RET(taken.status == EDITOR_OK && App().Layout().View().ReadbackFrames() == reads + 1);
            IM_CHECK_NO_RET(vdev::ReadSnapshots(client, snapshots));
            image.slot = uint32_t(vdev::EditorClient::Integer(taken, "slot"));
            image.revision = snapshots.Revision(image.slot);
        });
        source.visible = true;
        ctx->Yield(3);
        for (bool locked : {false, true}) {
            OnMainThread(ctx, [&] {
                const auto changed = client.Command("selection.lock", {vdev::IntegerArgument("locked", locked)});
                IM_CHECK_NO_RET(changed.status == EDITOR_OK || changed.status == EDITOR_NO_CHANGE);
            });
            for (bool bitmap : {false, true}) {
                size_t steps = 0;
                bool dirty = false;
                OnMainThread(ctx, [&] {
                    steps = state.History().UndoCount();
                    dirty = state.document->Dirty();
                });
                source.type = bitmap ? vdev::SNAPSHOT_PAYLOAD : EDITOR_ATTRIBUTE_PAYLOAD;
                const auto *data = bitmap ? static_cast<const void *>(&image) : static_cast<const void *>(&attribute);
                const auto size = bitmap ? sizeof(image) : sizeof(attribute);
                source.bytes.resize(size);
                std::memcpy(source.bytes.data(), data, size);
                Drag(ctx, found.at);
                OnMainThread(ctx, [&] {
                    IM_CHECK_NO_RET(state.History().UndoCount() == steps + 1);
                    IM_CHECK_NO_RET(vdev::SceneObjects(client.Query("selection.objects"), "objects") == selected &&
                                    bool(vdev::EditorClient::Integer(client.Query("selection.objects"), "locked")) ==
                                        locked);
                    if (!bitmap) {
                        vdev::ObjectAttributes attributes;
                        IM_CHECK_NO_RET(
                            vdev::QueryObjectAttributes(client, selected.front(), attributes) &&
                            std::any_of(attributes.rows.begin(), attributes.rows.end(),
                                        [&](const auto &row) { return row.type == uint64_t(attribute.type); }));
                    }
                    IM_CHECK_NO_RET(client.Command("history.undo").status == EDITOR_OK &&
                                    state.document->Dirty() == dirty);
                    IM_CHECK_NO_RET(App().Layout().View().ReadbackFrames() == reads + 1);
                });
                // Replacing a catalog or image version must refuse the complete
                // request and leave the Redo branch from Undo available.
                if (bitmap) {
                    auto stale = image;
                    --stale.revision;
                    std::memcpy(source.bytes.data(), &stale, sizeof(stale));
                } else {
                    auto stale = attribute;
                    ++stale.catalog;
                    std::memcpy(source.bytes.data(), &stale, sizeof(stale));
                }
                Drag(ctx, found.at);
                OnMainThread(ctx, [&] {
                    IM_CHECK_NO_RET(state.History().UndoCount() == steps && state.History().CanRedo() &&
                                    state.document->Dirty() == dirty);
                });
            }
        }
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(vdev::RemoveSnapshots(client, snapshots, {int(image.slot)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("selection.lock", {vdev::IntegerArgument("locked", 0)}).status == EDITOR_OK);
        });
    };

    test = IM_REGISTER_TEST(engine, "layout", "snapshot_drag_keeps_slot_revision_after_replacement");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK(SelectEntity(ctx, found));
        auto &state = App().State();
        const vdev::EditorClient client(state.workspace->Application().Host(), 0);
        vdev::SnapshotData snapshots;
        vdev::SnapshotDrag original{};
        std::vector<vdev::ObjectRef> selected;
        size_t steps = 0;
        bool dirty = false, redo = false;
        uint64_t reads = 0;
        OnMainThread(ctx, [&] {
            selected = vdev::SceneObjects(client.Query("selection.objects"), "objects");
            IM_CHECK_NO_RET(client.Command("selection.lock", {vdev::IntegerArgument("locked", 1)}).status == EDITOR_OK);
            reads = App().Layout().View().ReadbackFrames();
            const auto taken = client.Command("snapshot.take");
            IM_CHECK_NO_RET(taken.status == EDITOR_OK && vdev::ReadSnapshots(client, snapshots));
            original.slot = uint32_t(vdev::EditorClient::Integer(taken, "slot"));
            original.revision = snapshots.Revision(original.slot);
            steps = state.History().UndoCount();
            dirty = state.document->Dirty();
            redo = state.History().CanRedo();
        });
        ctx->ItemClick("//3D Layout/$$56/##tool");
        ctx->WindowMove("//Snapshot Buffer", {20, 20});
        ctx->MouseMove((std::string("//Snapshot Buffer/$$") + std::to_string(original.slot) + "/##slot").c_str());
        const auto from = ctx->UiContext->IO.MousePos;
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({from.x + 20, from.y + 10});
        ctx->Yield(2);
        OnMainThread(ctx, [&] {
            const auto *payload = ImGui::GetDragDropPayload();
            IM_CHECK_NO_RET(payload && payload->IsDataType(vdev::SNAPSHOT_PAYLOAD) &&
                            payload->DataSize == sizeof(original));
            IM_CHECK_NO_RET(vdev::RemoveSnapshots(client, snapshots, {int(original.slot)}).status == EDITOR_OK);
            const auto replacement = client.Command("snapshot.take");
            IM_CHECK_NO_RET(replacement.status == EDITOR_OK &&
                            vdev::EditorClient::Integer(replacement, "slot") == original.slot);
            IM_CHECK_NO_RET(vdev::ReadSnapshots(client, snapshots) &&
                            snapshots.Revision(original.slot) != original.revision);
        });
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            const auto *payload = ImGui::GetDragDropPayload();
            vdev::SnapshotDrag held{};
            if (payload && payload->DataSize == sizeof(held))
                std::memcpy(&held, payload->Data, sizeof(held));
            IM_CHECK_NO_RET(held.slot == original.slot && held.revision == original.revision);
            IM_CHECK_NO_RET(vdev::ApplySnapshot(client, held, selected.front(), false).status == EDITOR_STALE);
            IM_CHECK_NO_RET(state.History().UndoCount() == steps && state.document->Dirty() == dirty &&
                            state.History().CanRedo() == redo);
            IM_CHECK_NO_RET(vdev::SceneObjects(client.Query("selection.objects"), "objects") == selected &&
                            vdev::EditorClient::Integer(client.Query("selection.objects"), "locked"));
            IM_CHECK_NO_RET(App().Layout().View().ReadbackFrames() == reads + 2);
        });
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        ctx->ItemClick("//Snapshot Buffer/Close");
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(vdev::RemoveSnapshots(client, snapshots, {int(original.slot)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("selection.lock", {vdev::IntegerArgument("locked", 0)}).status == EDITOR_OK);
        });
    };

    test = IM_REGISTER_TEST(engine, "layout", "attribute_drag_keeps_catalog_version_after_catalog_change");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK(SelectEntity(ctx, found));
        auto &state = App().State();
        auto &workspace = *state.workspace;
        const vdev::EditorClient client(workspace.Application().Host(), 0);
        EditorView view = 0;
        std::vector<vdev::ObjectRef> selected;
        EditorAttributePayload original{};
        size_t steps = 0;
        bool dirty = false, redo = false;
        OnMainThread(ctx, [&] {
            selected = vdev::SceneObjects(client.Query("selection.objects"), "objects");
            IM_CHECK_NO_RET(client.Command("selection.lock", {vdev::IntegerArgument("locked", 1)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(
                client.Command("attribute.create-category", {vdev::TextArgument("name", std::string("Drag Catalog"))})
                    .status == EDITOR_OK);
            const auto made =
                client.Command("attribute.create-type",
                               {vdev::TextArgument("name", std::string("Drag Value")),
                                vdev::IntegerArgument("parameter", (uint64_t(CKPGUID_INT.d1) << 32) | CKPGUID_INT.d2),
                                vdev::TextArgument("category", std::string("Drag Catalog"))});
            IM_CHECK_NO_RET(made.status == EDITOR_OK);
            view = workspace.Open("scene.attributes");
            IM_CHECK_NO_RET(view);
        });
        ctx->Yield(4);
        const auto window = "//" + workspace.WindowName(view);
        ctx->WindowFocus(window.c_str());
        ctx->Yield(2);
        ctx->ItemOpen((window + "/**/Drag Catalog").c_str());
        ctx->MouseMove((window + "/**/Drag Value").c_str());
        const auto from = ctx->UiContext->IO.MousePos;
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({from.x + 20, from.y + 10});
        ctx->Yield(2);
        OnMainThread(ctx, [&] {
            const auto *payload = ImGui::GetDragDropPayload();
            IM_CHECK_NO_RET(payload && payload->IsDataType(EDITOR_ATTRIBUTE_PAYLOAD) &&
                            payload->DataSize == sizeof(original));
            if (payload && payload->DataSize == sizeof(original))
                std::memcpy(&original, payload->Data, sizeof(original));
            IM_CHECK_NO_RET(client
                                .Command("attribute.create-category",
                                         {vdev::TextArgument("name", std::string("Updated Drag Catalog"))})
                                .status == EDITOR_OK);
            IM_CHECK_NO_RET(vdev::EditorClient::Integer(client.Query("attribute.catalog"), "catalog") !=
                            original.catalog);
            steps = state.History().UndoCount();
            dirty = state.document->Dirty();
            redo = state.History().CanRedo();
        });
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            const auto *payload = ImGui::GetDragDropPayload();
            EditorAttributePayload held{};
            if (payload && payload->DataSize == sizeof(held))
                std::memcpy(&held, payload->Data, sizeof(held));
            IM_CHECK_NO_RET(held.type == original.type && held.value == original.value &&
                            held.catalog == original.catalog);
            IM_CHECK_NO_RET(client
                                .Command("attribute.add", {vdev::ObjectsArgument("objects", selected),
                                                           vdev::IntegerArgument("type", uint64_t(held.type)),
                                                           vdev::IntegerArgument("catalog", held.catalog),
                                                           vdev::ObjectArgument("source", held.value)})
                                .status == EDITOR_STALE);
            IM_CHECK_NO_RET(state.History().UndoCount() == steps && state.document->Dirty() == dirty &&
                            state.History().CanRedo() == redo);
        });
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(workspace.Close(view));
            IM_CHECK_NO_RET(client.Command("selection.lock", {vdev::IntegerArgument("locked", 0)}).status == EDITOR_OK);
        });
    };

    test = IM_REGISTER_TEST(engine, "layout", "queued_camera_creation_finishes_navigation");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        CK_ID camera = 0;
        std::set<CK_ID> before;
        bool gpu = false;
        std::uint64_t reads = 0, uploads = 0, creations = 0;
        OnMainThread(ctx, [&] {
            auto &editor = App().State().workspace->Application();
            before = LiveObjects(CKCID_TARGETCAMERA);
            auto &layout = App().Layout();
            gpu = layout.UsesGpuViewport();
            reads = layout.View().ReadbackFrames();
            uploads = layout.ViewportUploadsForTests();
            creations = layout.GpuViewportCreationsForTests();
            const auto history = App().State().History().UndoCount();
            editor.EnterDraw();
            App().CreateObject(vdev::LayoutObject::Camera);
            editor.LeaveDraw();
            IM_CHECK_NO_RET(LiveObjects(CKCID_TARGETCAMERA) == before &&
                            App().State().History().UndoCount() == history);
        });
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            const auto after = LiveObjects(CKCID_TARGETCAMERA);
            for (const auto id : after)
                if (!before.count(id))
                    camera = id;
            IM_CHECK_NO_RET(camera && after.size() == before.size() + 1);
            IM_CHECK_NO_RET(App().Layout().View().Camera() && App().Layout().View().Camera()->GetID() == camera);
            if (gpu) {
                IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == reads);
                IM_CHECK_NO_RET(App().Layout().ViewportUploadsForTests() == uploads &&
                                App().Layout().GpuViewportCreationsForTests() == creations);
            }
        });
        IM_CHECK(camera);
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, [&] { return !App().State().document->IsLive(camera); }));
    };

    // Permanent deletion remains the last Layout case: earlier cases need
    // the sample mesh for real picking and material drops.
    // Del on the selected entity: the Level Manager's Select Delete Options,
    // OK (Enter) destroys it, out of the selection, and clears the undo
    // steps (Dev). Last: the entity is gone.
    test = IM_REGISTER_TEST(engine, "layout", "delete");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        vdev::EditorState &state = App().State();
        IM_CHECK_EQ(state.objectSelection.Objects().size(), 1u);
        const size_t logged = state.eventLog.Entries().size();
        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(3);
        ImGuiWindow *dialog = ctx->GetWindowByRef("//###DeleteOptions");
        IM_CHECK_SILENT(dialog && dialog->WasActive);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        LogEditor(ctx, logged);
        IM_CHECK(!ObjectExists(ctx, found.entity));
        IM_CHECK(!state.objectSelection.Contains(found.entity));
        IM_CHECK(!state.History().CanUndo());
        IM_CHECK(state.document->Dirty());
    };
}
} // namespace vdevuitest

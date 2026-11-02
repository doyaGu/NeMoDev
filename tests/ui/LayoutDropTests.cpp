#include "tests/ui/UiTests.h"
#include "workspace/ResourceDrag.h"
#include "features/layout/SnapshotData.h"
#include "features/scene/SceneViewData.h"
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
void Drag(ImGuiTestContext *ctx, const ImVec2 &to) {
    ctx->MouseMove("//Layout Drop Source/Drag resource");
    ctx->MouseDown(0);
    const auto from = ctx->UiContext->IO.MousePos;
    ctx->MouseMoveToPos(ImVec2(from.x - 25, from.y - 20));
    ctx->MouseMoveToPos(to);
    ctx->Yield(3);
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
} // namespace

void RegisterLayoutDropTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "layout", "resource_image_modes_and_malformed_drags");
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
        for (unsigned mode = 0; mode < 4; ++mode) {
            const auto path = folder / ("drop-" + std::to_string(mode) + ".bmp");
            Bitmap(path);
            source.type = EDITOR_RESOURCE_FILE_PAYLOAD;
            source.bytes = vdev::WriteResourceDrag(0, {{path.u8string(), CKCID_TEXTURE}});
            const auto before = Read<std::set<CK_ID>>(ctx, [&] { return LiveObjects(kinds[mode]); });
            std::vector<vdev::ObjectRef> selected;
            size_t history = 0;
            OnMainThread(ctx, [&] {
                selected = App().State().objectSelection.References();
                history = App().State().History().UndoCount();
            });
            if (mode & 1)
                ctx->KeyDown(ImGuiMod_Shift);
            if (mode & 2)
                ctx->KeyDown(ImGuiMod_Ctrl);
            Drag(ctx, found.at);
            if (mode & 2)
                ctx->KeyUp(ImGuiMod_Ctrl);
            if (mode & 1)
                ctx->KeyUp(ImGuiMod_Shift);
            const auto deadline = SDL_GetTicks() + 10000;
            bool loaded = false;
            while (SDL_GetTicks() < deadline) {
                loaded = Read<bool>(ctx, [&] {
                    return !App().State().runtimeOperation.Busy() &&
                           LiveObjects(kinds[mode]).size() == before.size() + 1;
                });
                if (loaded)
                    break;
                ctx->Sleep(0.01f);
            }
            IM_CHECK(loaded);
            OnMainThread(ctx, [&] {
                // Image resources retain their existing non-undoable adoption.
                IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
                IM_CHECK_NO_RET(App().State().objectSelection.References() == selected);
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
}
} // namespace vdevuitest

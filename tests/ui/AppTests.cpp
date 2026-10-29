// "app": the editor on its new composition: its windows, the File menu,
// the dialogs of the Options and Help menus and their shortcuts.

#include "core/level/LevelCommands.h"
#include "core/layout/LayoutCommands.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "imgui_internal.h"
#include "tests/ui/UiTests.h"
#include "ui/app/DesktopFiles.h"
#include "ui/controls/DevTheme.h"
#include "ui/schematic/Snapshots.h"
#include <cmath>
#include <filesystem>
#include "core/document/FileCommands.h"
#include "core/level/AttributeQueries.h"
#include "core/level/ObjectQueries.h"

namespace vdevuitest {

namespace {

bool WindowShown(ImGuiTestContext *ctx, const char *name) {
    ImGuiWindow *window = ctx->GetWindowByRef(name);
    return window && window->WasActive;
}

} // namespace

void RegisterAppTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    t = IM_REGISTER_TEST(engine, "app", "hierarchy_projection_priority_and_selection_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        CK_ID object = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const auto made = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            object = made.created;
            IM_CHECK_NO_RET(vdev::RenameLevelObject(*state.document, state.document->Reference(object), "Projection UI Frame").ok);
            state.History().Clear();
            state.document->MarkSaved();
            state.History().MarkSavePoint();
            state.showHierarchy = true;
        });
        ctx->Yield(3);
        ctx->WindowFocus("//Hierarchy Manager");
        ctx->Yield(3);
        const auto rows = App().Managers().ShownHierarchyRows();
        const auto row = std::find_if(rows.begin(), rows.end(), [&](const auto &item) { return item.object == object; });
        IM_CHECK(row != rows.end() && row->priorityMax.x > row->priorityMin.x);
        ctx->MouseMoveToPos(Center(row->priorityMin, row->priorityMax));
        ctx->MouseDoubleClick(0);
        ctx->Yield(2);
        IM_CHECK(ctx->UiContext->ActiveId != 0);
        ctx->KeyCharsReplaceEnter("37");
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, [&] { return vdev::DescribeObject(*App().State().document, object).renderPriority; }) == 37);
        IM_CHECK(Read<bool>(ctx, [&] { return App().State().objectSelection.Contains(object); }));
        Undo(ctx);
        IM_CHECK(Read<int>(ctx, [&] { return vdev::DescribeObject(*App().State().document, object).renderPriority; }) == 0);
        IM_CHECK(!Read<bool>(ctx, [] { return App().State().document->Dirty(); }));
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            // The shared follower consumes document queries and excludes
            // graveyard objects while a clipboard can still retain them.
            vdev::SelectionFollower follower;
            vdev::ObjectSelection selected;
            std::set<CK_ID> own;
            follower.Pull(selected, own);
            own = {object};
            selected.locked = true;
            follower.Push(selected, own, state.document.get());
            IM_CHECK_NO_RET(selected.Objects().empty());
            selected.locked = false;
            own.clear(); follower.Push(selected, own, state.document.get());
            own.insert(object); follower.Push(selected, own, state.document.get());
            IM_CHECK_NO_RET(selected.Objects() == std::vector<CK_ID>{object});
            state.objectSelection.Set(*state.document, {});
            IM_CHECK_NO_RET(vdev::DestroyLevelObjects(*state.document, {object}).ok);
            state.showHierarchy = false;
            state.document->MarkSaved();
        });
    };

    t = IM_REGISTER_TEST(engine, "app", "attribute_projection_edit_copy_and_retained_source");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        CK_ID source = 0, target = 0;
        CKAttributeType type = -1;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &document = *state.document;
            IM_CHECK_NO_RET(vdev::CreateAttributeCategory(document, "Projection UI").ok);
            const auto created = vdev::CreateAttributeType(document, "Projection Value", CKPGUID_INT, "Projection UI");
            IM_CHECK_NO_RET(created.ok);
            type = static_cast<CKAttributeType>(created.created);
            const auto a = vdev::CreateLayoutObject(document, vdev::LayoutObject::Frame3D);
            const auto b = vdev::CreateLayoutObject(document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(a.ok && b.ok);
            source = a.created; target = b.created;
            IM_CHECK_NO_RET(vdev::RenameLevelObject(document, document.Reference(source), "Projection Source").ok);
            IM_CHECK_NO_RET(vdev::RenameLevelObject(document, document.Reference(target), "Projection Target").ok);
            IM_CHECK_NO_RET(vdev::AddAttribute(document, {source, target}, type).ok);
            state.History().Clear();
            document.MarkSaved(); state.History().MarkSavePoint();
            state.showAttributeManager = true;
        });
        ctx->Yield(3);
        ctx->WindowFocus("//Attribute Manager");
        ctx->SetRef("//Attribute Manager");
        ctx->Yield(2);
        ctx->ItemOpen("**/Projection UI");
        ctx->ItemClick("**/Projection Value");
        ctx->Yield(2);
        const auto valuePath = "**/$$" + std::to_string(source) + "/##value";
        ctx->ItemInputValue(valuePath.c_str(), "42");
        ctx->Yield(3);
        IM_CHECK(Read<std::string>(ctx, [&] { return vdev::ReadAttributeObjects(*App().State().document, {type}, {source}).front().value; }) == "42");
        Undo(ctx);
        IM_CHECK(Read<std::string>(ctx, [&] { return vdev::ReadAttributeObjects(*App().State().document, {type}, {source}).front().value; }) == "0");
        Redo(ctx);
        const auto beforeDialog = Read<size_t>(ctx, [] { return App().State().History().UndoCount(); });
        ctx->MouseMove(valuePath.c_str());
        ctx->MouseDoubleClick(0);
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//###AttributeManagerValue"));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//###AttributeManagerValue"));
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == beforeDialog);
        ctx->ItemClick(("**/$$" + std::to_string(source) + "/###row").c_str());
        ctx->ItemClick("copy/##bitmap");
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.clipboard.parameters.size() == 1);
            vdev::Edit edit(*state.document, "Remove copied owner");
            edit.Bury(source);
            IM_CHECK_NO_RET(edit.Commit().ok);
        });
        ctx->Yield(3);
        ctx->ItemClick(("**/$$" + std::to_string(target) + "/###row").c_str());
        ctx->ItemClick("paste/##bitmap");
        ctx->Yield(3);
        IM_CHECK(Read<std::string>(ctx, [&] { return vdev::ReadAttributeObjects(*App().State().document, {type}, {target}).front().value; }) == "42");
        Undo(ctx);
        IM_CHECK(Read<std::string>(ctx, [&] { return vdev::ReadAttributeObjects(*App().State().document, {type}, {target}).front().value; }) == "0");
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.History().Undo()); // restore source owner
            state.clipboard = {};
            IM_CHECK_NO_RET(vdev::DestroyLevelObjects(*state.document, {source, target}).ok);
            IM_CHECK_NO_RET(vdev::DeleteAttributeType(*state.document, type).ok);
            IM_CHECK_NO_RET(vdev::DeleteAttributeCategory(*state.document, "Projection UI").ok);
            state.showAttributeManager = false;
            state.document->MarkSaved();
        });
    };

    t = IM_REGISTER_TEST(engine, "app", "document_metadata_survives_ui_state_reset");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        OnMainThread(ctx, [] {
            auto &state = App().State();
            auto &document = *state.document;
            const auto frame = vdev::CreateLayoutObject(document, vdev::LayoutObject::Frame3D);
            IM_CHECK(frame.ok);
            IM_CHECK(vdev::SetFrameRepresentation(document, {frame.created}, 0).ok);
            const unsigned char pixel[4] = {11, 22, 33, 255};
            void *bitmap = vdev::BitmapFromPixels(pixel, 1, 1);
            IM_CHECK(bitmap && vdev::SetObjectPicture(document, frame.created, bitmap).ok);
            const auto layout = document.Layout();
            state.ClearDocumentState();
            IM_CHECK(document.Layout() == layout && document.Pictures().at(frame.created).get() == bitmap);
            IM_CHECK(document.History().Undo() && !document.Pictures().count(frame.created));
            IM_CHECK(document.History().Undo() && !document.Layout().representations.count(frame.created));
            IM_CHECK(document.History().Undo() && document.IsDiscarded(frame.created));
            document.History().Clear();
            document.MarkSaved();
        });
    };

    t = IM_REGISTER_TEST(engine, "app", "replaced_pictures_release_bitmaps_and_textures");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto baseline = Read<size_t>(ctx, [] { return vdev::SnapshotTextureCountForTests(); });
        std::weak_ptr<void> previous;
        for (int pass = 0; pass < 12; ++pass) {
            OnMainThread(ctx, [&] {
                auto &state = App().State();
                auto &document = *state.document;
                const CK_ID object = document.Context()->GetCurrentLevel()->GetID();
                const unsigned char pixel[4] = {static_cast<unsigned char>(pass), 90, 170, 255};
                void *bitmap = vdev::BitmapFromPixels(pixel, 1, 1);
                IM_CHECK(bitmap);
                vdev::SetObjectPicture(*state.document, object, bitmap);
                IM_CHECK(vdev::SnapshotTexture(bitmap, nullptr, document.BitmapOwner(bitmap)) != ImTextureID());
                if (pass) {
                    IM_CHECK(state.History().Undo());
                    IM_CHECK(state.History().Redo());
                    IM_CHECK(state.document->Pictures().at(object).get() == bitmap);
                }
                state.History().Clear();
                IM_CHECK(previous.expired());
                previous = document.BitmapOwner(bitmap);
                IM_CHECK(!previous.expired());
            });
            ctx->Yield(2);
            IM_CHECK(Read<size_t>(ctx, [] { return vdev::SnapshotTextureCountForTests(); }) <= baseline + 1);
        }
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            vdev::SetObjectPicture(*state.document, state.document->Context()->GetCurrentLevel()->GetID(), nullptr);
            state.History().Clear();
            IM_CHECK(previous.expired());
            state.document->MarkSaved();
        });
        ctx->Yield(2);
        IM_CHECK(Read<size_t>(ctx, [] { return vdev::SnapshotTextureCountForTests(); }) <= baseline);
    };

    t = IM_REGISTER_TEST(engine, "app", "native_identity_selection_clipboard_and_setup");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        CK_ID id = 0;
        vdev::ObjectRef original{};
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &document = *state.document;
            auto *object = document.Context()->CreateObject(CKCID_3DENTITY, "Identity UI target");
            IM_CHECK_NO_RET(object != nullptr);
            id = object->GetID(); original = document.Reference(id);
            state.objectSelection.Set(document, {id});
            state.objectSelection.locked = true;
            state.objectClipboard.SetObjects(document, {id});
            state.events.Post(vdev::ShowObjectSetup{original});
        });
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [id] { return ShowsObject(id); }));
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &document = *state.document;
            // A queued request must retain its original target, even when the
            // native ID names a different class before the next UI frame.
            state.events.Post(vdev::ShowObjectSetup{original});
            IM_CHECK_NO_RET(document.Context()->DestroyObject(id, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
            auto *replacement = document.Context()->CreateObject(CKCID_2DENTITY, "Replacement UI target");
            IM_CHECK_NO_RET(replacement && replacement->GetID() == id);
            IM_CHECK_NO_RET(!document.Resolve(original));
            IM_CHECK_NO_RET(state.objectClipboard.Objects(document).empty());
        });
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [&] {
            auto &state = App().State();
            return state.objectSelection.Objects().empty() && !state.objectSelection.locked && !ShowsObject(id);
        }));
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            state.objectSelection.Set(*state.document, {id});
            IM_CHECK_NO_RET(!state.objectSelection.Objects().empty());
            state.objectSelection.Set(*state.document, {});
            state.objectClipboard.SetObjects(*state.document, {});
            IM_CHECK_NO_RET(state.document->Context()->DestroyObject(id) == CK_OK);
        });
    };

    t = IM_REGISTER_TEST(engine, "app", "clipboard_survives_history_collection");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &document = *state.document;
            const auto script = vdev::CreateScript(document, document.Context()->GetCurrentLevel()->GetID(), "Clipboard lifetime");
            IM_CHECK(script.ok);
            const auto graph = vdev::CreateBehaviorGraph(document, script.created, script.created, "Copied graph", Point(240, 120));
            IM_CHECK(graph.ok);
            state.selection.Set(script.created, vdev::ElementRef{vdev::ElementKind::Behavior, script.created, graph.created});
            vdev::SchematicCanvas canvas(state);
            canvas.Copy();
            IM_CHECK(!state.clipboard.Empty());
            IM_CHECK(vdev::DeleteElements(document, script.created, state.selection.items).ok);
            document.History().Clear();
            const auto pasted = vdev::PasteBehaviors(document, state.clipboard.data, state.clipboard.behaviors,
                                                      state.clipboard.comments, script.created, script.created, Point(500, 200));
            IM_CHECK(pasted.ok && pasted.created);
            state.clipboard = {};
            state.selection.Clear();
            state.objectClipboard.SetScripts(document, {script.created});
            IM_CHECK(state.objectClipboard.Objects(*state.document).empty());
            IM_CHECK(vdev::DeleteScript(document, script.created).ok);
            document.History().Clear();
            IM_CHECK(document.Context()->GetObject(script.created));
            // Re-copying a detached source must retain it before replacing
            // the old clipboard, even after history no longer owns it.
            state.objectClipboard.SetScripts(document, state.objectClipboard.Scripts(*state.document));
            IM_CHECK(state.objectClipboard.Scripts(*state.document) == std::vector<CK_ID>{script.created});
            IM_CHECK(document.Context()->GetObject(script.created));
            state.objectClipboard.SetObjects(document, {document.Context()->GetCurrentLevel()->GetID()});
            IM_CHECK(!state.objectClipboard.Objects(*state.document).empty());
            IM_CHECK(state.objectClipboard.Scripts(*state.document).empty());
            IM_CHECK(!document.Context()->GetObject(script.created));
            state.objectClipboard.SetObjects(document, {});
            document.MarkSaved();
        });
    };

    t = IM_REGISTER_TEST(engine, "app", "display_scale_rebuilds_theme_sizes");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ImGuiStyle base, scaled;
        vdev::ApplyDevTheme(base);
        for (int pass = 0; pass < 8; ++pass) {
            for (float scale : {.75f, 1.25f, 1.5f, 2.0f, 1.0f}) {
                vdev::ApplyDevTheme(scaled, scale);
                ImGuiStyle expected = base;
                expected.ScaleAllSizes(scale);
                IM_CHECK_EQ(scaled.FramePadding.x, expected.FramePadding.x);
                IM_CHECK_EQ(scaled.WindowPadding.y, expected.WindowPadding.y);
                IM_CHECK_EQ(scaled.ItemSpacing.x, expected.ItemSpacing.x);
                IM_CHECK_EQ(scaled.ScrollbarSize, expected.ScrollbarSize);
                IM_CHECK_EQ(scaled.FontScaleDpi, scale);
                IM_CHECK_EQ(scaled.FontSizeBase, 14.0f);
            }
        }
        for (float invalid : {0.0f, -1.0f, NAN, INFINITY}) {
            vdev::ApplyDevTheme(scaled, invalid);
            IM_CHECK_EQ(scaled.FontScaleDpi, 1.0f);
            IM_CHECK_EQ(scaled.FramePadding.x, base.FramePadding.x);
        }
    };

    // Explicit status fonts keep their original proportions at every DPI.
    t = IM_REGISTER_TEST(engine, "app", "status_bar_text_scales_once");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const ImGuiStyle saved = ImGui::GetStyle();
        OnMainThread(ctx, [] { App().State().Log(vdev::LogLevel::Info, "Status text"); });
        for (float dpi : {1.0f, 1.5f, 2.0f, 1.0f}) {
            ctx->LogInfo("Status bar DPI scale %.2f", dpi);
            OnMainThread(ctx, [dpi] { ImGui::GetStyle().FontScaleDpi = dpi; });
            ctx->Yield(3);
            OnMainThread(ctx, [&] {
                ImGuiWindow *bar = ImGui::FindWindowByName("##StatusBar");
                IM_CHECK(bar && bar->WasActive);
                const float scale = ImGui::GetFontSize() / 13.0f;
                const ImU32 color = IM_COL32(176, 176, 176, 255);
                auto check = [&](float left, float right, float top, float bottom, const char *text, bool bold) {
                    const ImVec2 min(IM_ROUND(bar->Pos.x + bar->Size.x - left * scale),
                                     IM_ROUND(bar->Pos.y + top * scale));
                    const ImVec2 max(IM_ROUND(bar->Pos.x + bar->Size.x - right * scale),
                                     IM_ROUND(bar->Pos.y + bottom * scale));
                    // Plain labels use GPU clipping; ellipsis labels also clip
                    // glyph quads on the CPU. Compare their visible geometry.
                    auto visible = [&](ImVec2 point) {
                        return ImVec2(ImClamp(point.x, min.x, max.x), ImClamp(point.y, min.y, max.y));
                    };
                    ImDrawList expected(ImGui::GetDrawListSharedData());
                    expected._ResetForNewFrame();
                    expected.PushClipRectFullScreen();
                    expected.PushTexture(ImGui::GetIO().Fonts->TexRef);
                    const float height = vdev::VIFontHeight(bold);
                    vdev::DrawVIText(&expected, min, color, text, bold,
                                    ImGui::GetRoundedFontSize(height * scale) / height, &max);
                    std::vector<ImVec2> actual;
                    for (const ImDrawVert &vertex : bar->DrawList->VtxBuffer)
                        if (vertex.col == color && vertex.pos.x >= min.x - 1 && vertex.pos.x <= max.x + 1 &&
                            vertex.pos.y >= min.y - 1 && vertex.pos.y <= max.y + 1)
                            actual.push_back(visible(vertex.pos));
                    IM_CHECK(expected.VtxBuffer.Size > 0);
                    IM_CHECK_EQ(actual.size(), static_cast<size_t>(expected.VtxBuffer.Size));
                    for (size_t i = 0; i < actual.size(); ++i) {
                        const ImVec2 point = visible(expected.VtxBuffer[static_cast<int>(i)].pos);
                        IM_CHECK(std::fabs(actual[i].x - point.x) < .1f);
                        IM_CHECK(std::fabs(actual[i].y - point.y) < .1f);
                    }
                };
                check(154, 124, 4, 17, "FPS", true);
                check(185, 155, 7, 19, "NA", false);
                check(407, 207, 6, 18, "Status text", false);
            });
        }
        OnMainThread(ctx, [&] { ImGui::GetStyle() = saved; });
    };

    // Dev's main windows are there on a new composition.
    t = IM_REGISTER_TEST(engine, "app", "main_windows");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        IM_CHECK(App().State().HasDocument());
        for (const char *name : {"//3D Layout", "//Schematic", "//Level Manager", "//Building Blocks"})
            IM_CHECK_NO_RET(ctx->GetWindowByRef(name) != nullptr);
    };

    // File > New Composition: a new, unmodified composition without Scripts
    // and without undo steps.
    t = IM_REGISTER_TEST(engine, "app", "new_composition");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/File/New Composition");
        ctx->Yield(5);
        vdev::EditorState &state = App().State();
        IM_CHECK(state.HasDocument());
        IM_CHECK(!state.document->Dirty());
        IM_CHECK(state.document->Scripts().empty());
        IM_CHECK(!state.History().CanUndo());
    };

    // What waits for a composition ends with it: File > New Composition
    // closes the 3D Layout Explorer (without restoring the selection it
    // saved) and ends a flight without recording it.
    t = IM_REGISTER_TEST(engine, "app", "new_composition_ends_explorer_and_flight");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        // Y over the view flies; the toolbar's button (Dev command 41) opens the Explorer.
        ImVec2 min, max;
        IM_CHECK_SILENT(App().Layout().ShownView(min, max));
        ctx->MouseMoveToPos(ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f));
        ctx->KeyPress(ImGuiKey_Y);
        ctx->Yield(2);
        IM_CHECK_SILENT(App().Layout().Tool() == vdev::LayoutTool::Fly);
        ctx->ItemClick("//3D Layout/$$41/##tool");
        ctx->Yield(3);
        IM_CHECK_SILENT(WindowShown(ctx, "//3D Layout Explorer"));
        ctx->MenuClick("//##MainMenuBar/File/New Composition");
        ctx->Yield(5);
        IM_CHECK(!WindowShown(ctx, "//3D Layout Explorer"));
        IM_CHECK(App().Layout().Tool() == vdev::LayoutTool::Select);
        IM_CHECK(!App().State().History().CanUndo());
        IM_CHECK(!App().State().document->Dirty());
    };

    // Ctrl+P and Options > General Preferences open the General Preferences;
    // Cancel closes them.
    t = IM_REGISTER_TEST(engine, "app", "general_preferences");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_P);
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//General Preferences"));
        ctx->ItemClick("//General Preferences/Cancel");
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//General Preferences"));
        ctx->MenuClick("//##MainMenuBar/Options/General Preferences");
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//General Preferences"));
        ctx->ItemClick("//General Preferences/Cancel");
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//General Preferences"));
    };

    t = IM_REGISTER_TEST(engine, "app", "duplicate_names_cancel");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto loaded = std::make_shared<bool>(false);
        const auto file = std::filesystem::temp_directory_path() /
            ("duplicate-cancel-" + std::to_string(SDL_GetTicksNS()) + ".nmo");
        struct RemoveFile {
            std::filesystem::path path;
            ~RemoveFile() { std::error_code error; std::filesystem::remove(path, error); }
        } cleanup{file};
        CK_ID object = 0;
        size_t historyCount = 0, scriptCount = 0;
        std::set<CK_ID> created;
        std::vector<CK_ID> loadedObjects;
        std::string sourcePath;
        bool dirty = false;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const auto made = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            object = made.created;
            historyCount = state.History().UndoCount();
            created = state.document->CreatedObjects();
            loadedObjects = state.document->LoadedObjects();
            scriptCount = state.document->Scripts().size();
            sourcePath = state.document->SourcePath();
            dirty = state.document->Dirty();
            std::string error;
            IM_CHECK_NO_RET(vdev::SaveObjectsAs(*state.document, {object}, file.u8string(), &error));
            state.files.WithDuplicates(file.u8string(), [loaded](const std::vector<vdev::DuplicateName> *) { *loaded = true; });
        });
        OnMainThread(ctx, [] {});
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//###DuplicateNames"));
        ctx->ItemClick("//###DuplicateNames/Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK(!*loaded && Read<bool>(ctx, [] { return !App().State().files.PendingDuplicates(); }));
        IM_CHECK(!WindowShown(ctx, "//###DuplicateNames"));
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.document->IsLive(object));
            IM_CHECK_NO_RET(state.document->CreatedObjects() == created);
            IM_CHECK_NO_RET(state.document->LoadedObjects() == loadedObjects);
            IM_CHECK_EQ_NO_RET(state.document->Scripts().size(), scriptCount);
            IM_CHECK_NO_RET(state.document->SourcePath() == sourcePath);
            IM_CHECK_NO_RET(state.document->Dirty() == dirty);
            IM_CHECK_EQ_NO_RET(state.History().UndoCount(), historyCount);
            IM_CHECK_NO_RET(state.History().Undo());
            IM_CHECK_NO_RET(!state.document->IsLive(object));
            IM_CHECK_NO_RET(state.History().Redo());
            IM_CHECK_NO_RET(state.document->IsLive(object));
        });
    };

    // Local help is optional. Missing help disables its menus and makes
    // the reference and Building Block shortcuts quiet.
    t = IM_REGISTER_TEST(engine, "app", "optional_help");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Building Blocks");
        const bool available = vdev::HelpAvailable();
        ctx->LogInfo("Local help: %s", available ? "installed" : "absent");
        ctx->MenuClick("//##MainMenuBar/Help");
        IM_CHECK(((ctx->ItemInfo("//$FOCUSED/Online Reference").ItemFlags & ImGuiItemFlags_Disabled) == 0) == available);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->ItemInputValue("//Building Blocks/##filter", "Rotate");
        ctx->ItemClick("//Building Blocks/**/Rotate", ImGuiMouseButton_Right);
        IM_CHECK(((ctx->ItemInfo("//$FOCUSED/Html Help").ItemFlags & ImGuiItemFlags_Disabled) == 0) == available);
        ctx->KeyPress(ImGuiKey_Escape);
        if (!available) {
            const auto &state = App().State();
            const size_t logCount = state.log.size();
            const int repeats = logCount ? state.log.back().repeats : 0;
            const bool dirty = state.document->Dirty();
            ctx->WindowFocus("//Level Manager");
            ctx->MouseMoveToPos(ImVec2(10, 10));
            ctx->KeyPress(ImGuiKey_F1);
            ctx->MouseMove("//Building Blocks/**/Rotate");
            ctx->KeyPress(ImGuiKey_F1);
            ctx->ItemDoubleClick("//Building Blocks/**/Rotate");
            ctx->Yield(3);
            IM_CHECK_EQ(state.log.size(), logCount);
            if (logCount) IM_CHECK_EQ(state.log.back().repeats, repeats);
            IM_CHECK(state.document->Dirty() == dirty);
        }
    };

    t = IM_REGISTER_TEST(engine, "app", "editors_menu");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/Editors");
        IM_CHECK(!ctx->ItemExists("//$FOCUSED/Variable Manager"));
        IM_CHECK(!ctx->ItemExists("//$FOCUSED/VSL Script Manager"));
        for (const char *item : {"Attributes Manager", "Hierarchy Manager", "Path Manager", "Parameter Debugger",
                                 "Close All Setups and Managers"})
            IM_CHECK_NO_RET(ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
        ctx->KeyPress(ImGuiKey_Escape);
    };

    // Help > About Virtools..., closed with OK.
    t = IM_REGISTER_TEST(engine, "app", "about");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/Help/About Virtools...");
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//About Virtools"));
        ctx->ItemClick("//About Virtools/OK");
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//About Virtools"));
    };
}

} // namespace vdevuitest

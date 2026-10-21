// Create Web Page through the File menu, including invalid runtime metadata.

#include <filesystem>
#include <fstream>
#include <iterator>

#include "tests/ui/UiTests.h"
#include "core/document/FileCommands.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/SdkCompat.h"

namespace vdevuitest {

static std::string PageFileBytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static bool WritePageFile(const std::filesystem::path &path, const std::string &bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << bytes;
    file.close();
    return static_cast<bool>(file);
}

static void SubmitWebPage(ImGuiTestContext *ctx, bool cmo, bool overwrite) {
    ctx->MenuClick("//##MainMenuBar/File/Create Web Page...");
    ctx->Yield(3);
    // Enter is the dialog's OK shortcut; replace each field without submitting it.
    ctx->ItemInput("//###CreateWebPage/##title"); ctx->KeyCharsReplace("Exported composition");
    ctx->ItemInput("//###CreateWebPage/##width"); ctx->KeyCharsReplace("640");
    ctx->ItemInput("//###CreateWebPage/##height"); ctx->KeyCharsReplace("480");
    if (cmo) ctx->ItemClick("//###CreateWebPage/##cmo");
    ctx->ItemClick("//###CreateWebPage/OK/##dialogbutton");
    ctx->Yield(3);
    if (overwrite) {
        ctx->ItemClick("//###DevMessage/OK/##dialogbutton");
        ctx->Yield(3);
    }
}

void RegisterFileMenuTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = IM_REGISTER_TEST(engine, "filemenu", "save_failure_keeps_previous_file_and_save_point");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        namespace fs = std::filesystem;
        Settle(ctx);
        const auto root = fs::current_path() / ("save-menu-" + std::to_string(SDL_GetTicksNS()));
        IM_CHECK(fs::create_directory(root));
        const auto source = root / "source.cmo", version = root / "source_01.cmo";
        struct Cleanup {
            fs::path root, version;
            ~Cleanup() {
                std::error_code error;
                fs::permissions(version, fs::perms::owner_all, fs::perm_options::replace, error);
                fs::remove_all(root, error);
            }
        } cleanup{root, version};
        OnMainThread(ctx, [&] {
            vdev::SaveReport report;
            IM_CHECK_NO_RET(vdev::SaveDocumentAs(*App().State().document, source.u8string(), {}, report));
            App().DropFiles({vdev::Utf8ToWide(source.u8string())}, 0, 0);
        });
        ctx->Yield(4);
        fs::copy_file(source, version);
        const auto original = PageFileBytes(version);
        IM_CHECK(!original.empty());
        std::string objectName;
        CK_ID object = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.document->SourcePath() == source.u8string());
            const auto made = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            object = made.created;
            objectName = Object<CKObject>(object)->GetName();
            IM_CHECK_NO_RET(state.document->Dirty());
        });
        const auto permissions = fs::status(version).permissions();
        fs::permissions(version, fs::perms::owner_read, fs::perm_options::replace);
        const auto saveVersion = [&] {
            ctx->MenuClick("//##MainMenuBar/File/Save Version");
            ctx->Yield(3);
            ctx->ItemClick("//###DevMessage/OK/##dialogbutton");
            ctx->Yield(3);
        };
        saveVersion();
        IM_CHECK(PageFileBytes(version) == original);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().document->Dirty(); }));
        IM_CHECK(Read<bool>(ctx, [] {
            const auto &log = App().State().log;
            return std::any_of(log.begin(), log.end(), [](const auto &entry) {
                return entry.level == vdev::LogLevel::Error && entry.text.find("Save failed: ") == 0;
            });
        }));
        ctx->ItemClick("//Save report/OK");
        OnMainThread(ctx, [] { App().RequestQuit(); });
        ctx->Yield(3);
        ctx->ItemClick("//Unsaved changes/Cancel");
        IM_CHECK(Read<bool>(ctx, [] { return !App().ShouldQuit() && App().State().document->Dirty(); }));
        fs::permissions(version, permissions, fs::perm_options::replace);
        saveVersion();
        IM_CHECK(PageFileBytes(version) != original);
        IM_CHECK(!fs::exists(root / "source_02.cmo"));
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(!state.document->Dirty());
            IM_CHECK_NO_RET(state.History().Undo() && state.document->Dirty() && !state.document->IsLive(object));
            IM_CHECK_NO_RET(state.History().Redo() && !state.document->Dirty() && state.document->IsLive(object));
        });
        ctx->ItemClick("//Save report/OK");
        const auto savedVersion = PageFileBytes(version);
        IM_CHECK(PageFileBytes(source) == original);
        fs::permissions(version, fs::perms::owner_read, fs::perm_options::replace);
        CK_ID unsaved = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const auto made = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            unsaved = made.created;
            App().RequestQuit();
        });
        ctx->Yield(3);
        ctx->ItemClick("//Unsaved changes/Save");
        ctx->Yield(3);
        IM_CHECK(PageFileBytes(version) == savedVersion);
        IM_CHECK(Read<bool>(ctx, [&] {
            const auto &state = App().State();
            return !App().ShouldQuit() && state.document->Dirty() && state.document->IsLive(unsaved);
        }));
        ctx->ItemClick("//Save report/OK");
        fs::permissions(version, permissions, fs::perm_options::replace);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.History().Undo() && !state.document->Dirty() && !state.document->IsLive(unsaved));
        });
        OnMainThread(ctx, [&] { App().DropFiles({vdev::Utf8ToWide(version.u8string())}, 0, 0); });
        ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [&] {
            auto &state = App().State();
            return state.document->SourcePath() == version.u8string() && !state.document->Dirty() &&
                   state.host.Context()->GetObjectByNameAndClass(const_cast<CKSTRING>(objectName.c_str()), CKCID_3DENTITY);
        }));
        ctx->MenuClick("//##MainMenuBar/File/New Composition");
        ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().document->SourcePath().empty(); }));
    };
    t = IM_REGISTER_TEST(engine, "filemenu", "web_page_runtime_version");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        namespace fs = std::filesystem;
        Settle(ctx);
        const fs::path previous = fs::current_path();
        const fs::path root = previous / ("web-page-" + std::to_string(SDL_GetPerformanceCounter()));
        const fs::path metadata = fs::u8path(App().State().host.RuntimeDirectory()) / "BallancedRuntime.txt";
        const std::string original = PageFileBytes(metadata);
        IM_CHECK(!original.empty());
        IM_CHECK(fs::create_directory(root));
        // Always restore the test runtime and working directory, including on a failed check.
        struct Cleanup {
            fs::path previous, root, metadata;
            std::string original;
            ~Cleanup() {
                WritePageFile(metadata, original);
                std::error_code error;
                fs::current_path(previous, error);
                fs::remove_all(root, error);
            }
        } cleanup{previous, root, metadata, original};
        fs::current_path(root);
        CK_ID frame = 0, group = 0;
        size_t historyCount = 0;
        std::string frameName, source;
        bool dirty = false;
        int major = 0, minor = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const auto made = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            frame = made.created;
            const auto grouped = vdev::CreateGroup(*state.document, "Web Page Group", {frame});
            IM_CHECK_NO_RET(grouped.ok);
            group = grouped.created;
            frameName = Object<CKObject>(frame)->GetName();
            historyCount = state.History().UndoCount();
            source = state.document->SourcePath();
            dirty = state.document->Dirty();
            IM_CHECK_NO_RET(vdev::RuntimeVersion(state.host.RuntimeDirectory(), major, minor));
        });
        const fs::path html = root / "Virtools.htm", player = root / "Virtools.vmo", composition = root / "Virtools.cmo";
        for (bool cmo : {false, true}) {
            SubmitWebPage(ctx, cmo, false);
            const fs::path saved = cmo ? composition : player;
            IM_CHECK(fs::exists(html) && fs::exists(saved));
            const std::string page = PageFileBytes(html);
            IM_CHECK(page.find("<TITLE>Exported composition</TITLE>") != std::string::npos);
            IM_CHECK(page.find("WIDTH=\"640\" HEIGHT=\"480\"") != std::string::npos);
            IM_CHECK(page.find("Install" + std::to_string(major) + "." + std::to_string(minor) + "/") != std::string::npos);
            IM_CHECK(page.find(cmo ? "Virtools.cmo" : "Virtools.vmo") != std::string::npos);
            IM_CHECK(page.find('$') == std::string::npos);
            IM_CHECK(vdev::IsPlayerFile(saved.u8string()) == !cmo);
            OnMainThread(ctx, [&] {
                CKContext *context = nullptr;
                IM_CHECK_NO_RET(vdev::CreateCKContext(&context, nullptr) == CK_OK && context);
                if (!context) return;
                CKObjectArray *objects = CreateCKObjectArray();
                const std::string native = saved.string();
                IM_CHECK_NO_RET(context->Load(const_cast<CKSTRING>(native.c_str()), objects) == CK_OK);
                CKGroup *loaded = CKGroup::Cast(context->GetObjectByNameAndClass((CKSTRING) "Web Page Group", CKCID_GROUP));
                IM_CHECK_NO_RET(loaded && loaded->GetObjectCount() == 1);
                if (loaded && loaded->GetObjectCount() == 1) {
                    IM_CHECK_NO_RET(loaded->GetObject(0)->GetClassID() == CKCID_3DENTITY);
                    IM_CHECK_NO_RET(frameName == loaded->GetObject(0)->GetName());
                }
                DeleteCKObjectArray(objects);
                CKCloseContext(context);
            });
        }
        IM_CHECK(fs::remove(html) && fs::remove(player) && fs::remove(composition));
        // Missing, malformed, unrelated and mismatched metadata must fail before either write.
        const std::string invalid[] = {"", "BallancedSDK invalid", "OtherSDK 1 1", "BallancedSDK 1 1"};
        for (size_t i = 0; i < std::size(invalid); ++i) {
            if (i == 0) IM_CHECK(fs::remove(metadata));
            else IM_CHECK(WritePageFile(metadata, invalid[i]));
            const bool existing = i >= 2;
            if (existing) {
                IM_CHECK(WritePageFile(html, "existing HTML"));
                IM_CHECK(WritePageFile(player, "existing composition"));
            }
            const size_t logFrom = App().State().log.size();
            SubmitWebPage(ctx, false, existing);
            if (existing) {
                IM_CHECK(PageFileBytes(html) == "existing HTML");
                IM_CHECK(PageFileBytes(player) == "existing composition");
            } else {
                IM_CHECK(!fs::exists(html) && !fs::exists(player));
            }
            bool reported = false;
            // Repeated failures may be coalesced into the last log entry.
            const auto &log = App().State().log;
            for (size_t at = logFrom ? logFrom - 1 : 0; at < log.size(); ++at)
                if (log[at].level == vdev::LogLevel::Error && log[at].text.find("invalid Ballanced runtime metadata") != std::string::npos)
                    reported = true;
            IM_CHECK(reported);
        }
        IM_CHECK(WritePageFile(metadata, original) && PageFileBytes(metadata) == original);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.document->Dirty() == dirty && state.document->SourcePath() == source);
            IM_CHECK_EQ_NO_RET(state.History().UndoCount(), historyCount);
            CKGroup *kept = Object<CKGroup>(group);
            IM_CHECK_NO_RET(kept && kept->GetObjectCount() == 1 && kept->GetObject(0)->GetID() == frame);
            IM_CHECK_NO_RET(state.History().Undo());
            IM_CHECK_NO_RET(!state.document->IsLive(group) && state.document->IsLive(frame));
            IM_CHECK_NO_RET(state.History().Redo());
            kept = Object<CKGroup>(group);
            IM_CHECK_NO_RET(kept && kept->GetObjectCount() == 1 && kept->GetObject(0)->GetID() == frame);
        });
    };
}

} // namespace vdevuitest

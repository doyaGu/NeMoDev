#include "features/preferences/PreferencesCommands.h"
#include "features/preferences/PreferencesData.h"
#include "workspace/EditorClient.h"
#include "editor/ImGuiDrawing.h"
#include "core/document/Document.h"
#include "core/document/DocumentSaver.h"
#include "core/document/OutputFiles.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
const EditorField &Field(const EditorResponse &result, const char *key) {
    const auto *field = EditorClient::Find(result, key); Check(field != nullptr, "Missing preference projection"); return *field;
}
const EditorField &Field(const EditorField &result, const char *key) {
    for (const auto &field : result.fields) if (field.key == key) return field;
    throw std::runtime_error("Missing preference value");
}
std::string Bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}};
}
EditorField Integer(const char *key, int64_t value) { return {key, EDITOR_INTEGER_VALUE, {}, {}, uint64_t(value)}; }
struct RuntimeValues {
    CK_FILE_WRITEMODE mode;
    int compression, quantization, iterations;
    CK_TEXTURE_SAVEOPTIONS images;
    CK_SOUND_SAVEOPTIONS sounds;
    std::string format;
    explicit RuntimeValues(CKContext *context) : mode(context->GetFileWriteMode()), compression(context->GetCompressionLevel()),
        quantization(GetQuantizationSamplingFactor()), iterations(context->GetBehaviorManager()->GetBehaviorMaxIteration()),
        images(context->GetGlobalImagesSaveOptions()), sounds(context->GetGlobalSoundsSaveOptions()) {
        if (const auto *p = context->GetGlobalImagesSaveFormat())
            format.assign(reinterpret_cast<const char *>(p), size_t(p->m_Size));
    }
    bool operator==(const RuntimeValues &other) const {
        return mode == other.mode && compression == other.compression && quantization == other.quantization &&
            iterations == other.iterations && images == other.images && sounds == other.sounds && format == other.format;
    }
};
}
void TestPreferencesCommands(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    namespace fs = std::filesystem;
    Document document(runtime); std::string error; Check(document.New(&error), error.c_str());
    const auto directory = fs::temp_directory_path() / ("nemodev-preferences-" + std::to_string(document.Token().value));
    fs::create_directories(directory);
    struct Cleanup { fs::path directory; ~Cleanup() { std::error_code error; fs::remove_all(directory, error); } } cleanup{directory};
    Preferences preferences;
    const auto fixtures = fs::u8path(VIRTOOLSDEV_PREFERENCES_FIXTURES);
    Check(preferences.Save((directory / "default.ini").u8string()) && Bytes(directory / "default.ini") == Bytes(fixtures / "default.ini"),
          "default preferences no longer have the original bytes");
    Check(preferences.Load((fixtures / "custom.ini").u8string()) && preferences.Save((directory / "custom.ini").u8string()) &&
          Bytes(directory / "custom.ini") == Bytes(fixtures / "custom.ini"), "custom preferences no longer round-trip the original bytes");
    Check(preferences.pathsSaved && preferences.recentFiles.size() == 2 && !preferences.paths.empty() && !preferences.dataResources.empty(),
          "preference migration lost paths, recent files or resource libraries");
    preferences.settings["future.setting"] = "preserved";
    const auto source = preferences.Values();
    Preferences draft;
    Check(draft.ReadValues(source, error) && draft.Values() == source, "preferences did not read their copied projection");
    Check(draft.SetValues({Integer("Schematic\\Max Recent Building Block Count", 0)}, error) == false && draft.Values() == source,
          "invalid preference changed a value draft");
    const auto snapshot = preferences;
    for (int page = 0; page < int(Preferences::Pages().size()); ++page) {
        auto copy = preferences; copy.ResetPage(page, "first-engine");
        Check(copy.recentFiles == snapshot.recentFiles && copy.paths == snapshot.paths && copy.dataResources == snapshot.dataResources &&
              copy.defaultLinkDelay == snapshot.defaultLinkDelay && copy.autoLoopLinkDelay == snapshot.autoLoopLinkDelay &&
              copy.settings["future.setting"] == "preserved", "page defaults changed settings owned by another feature");
        if (page == 5) Check(!copy.automaticLink && !copy.strictMultiSelection && copy.maxRecentBlocks == 10 &&
              copy.tooltipDelayMs == 500 && copy.ioLinkColor == 0, "Schematic defaults did not restore its typed values");
        if (page == 6) Check(copy.Text("CK2 Settings\\Render Engine") == "first-engine" && copy.Int("File Options\\Compression Level") == 4,
              "Miscellaneous defaults did not select the first engine and original save options");
        if (page != 5) Check(copy.automaticLink == snapshot.automaticLink && copy.ioLinkColor == snapshot.ioLinkColor,
              "another page reset Schematic colors or input options");
    }
    bool available = true, failSave = false, throwSave = false, failDerived = false;
    unsigned saves = 0, derived = 0;
    auto *context = document.Context();
    EditorApplication editor([&] { return &document; }, [&] { return available; });
    const auto persisted = directory / "preferences.ini";
    Check(preferences.Save(persisted.u8string()), "seed preference file");
    PreferencesCommands commands(editor, runtime, preferences, [&] { return available; },
        [&](const Preferences &next, std::string &error) {
            SaveReport report; OutputFiles files(report); std::vector<std::string> written;
            const auto staged = files.Add(persisted.u8string());
            if (staged.empty() || !next.Save(staged)) return false;
            if (throwSave) throw std::runtime_error("Injected preference save exception");
            if (failSave) { error = "Injected preference publication refusal"; return false; }
            if (!files.Publish(written)) return false;
            ++saves; return true;
        }, [&] { ++derived; if (failDerived) { failDerived = false; throw std::runtime_error("Injected preference application failure"); } });
    Check(commands.Register(error) && editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto attach = [&] { editor.Attach(document.Token(), document.TopologyRevision(), true, true); };
    attach(); const auto view = editor.OpenScope(); const EditorClient client(editor.Host(), view), app(editor.Host(), 0);
    const auto owner = CreateLayoutObject(document, LayoutObject::Frame3D); Check(owner.ok, "create preference history target");
    const auto selected = document.Reference(owner.created); editor.SetSelection(selected);
    document.History().Clear(); document.MarkSaved(); document.History().MarkSavePoint();
    Check(RenameLevelObject(document, selected, "Preference history").ok && document.History().Undo(), "seed preference Redo"); attach();
    const auto token = document.Token(); const auto revision = document.ChangeRevision(); const auto topology = document.TopologyRevision();
    const auto undo = document.History().UndoCount(); const auto redo = document.History().CanRedo(); const auto dirty = document.Dirty();
    const auto query = [&] { return client.Query("preferences.values"); };
    const auto apply = [&](const EditorResponse &base, std::vector<EditorField> fields) {
        EditorField value{"value", EDITOR_RECORD}; value.fields = std::move(fields);
        const std::vector<EditorField> owned{value}; const auto borrowed = BorrowEditorFields(owned);
        return client.Command("preferences.apply", {IntegerArgument("revision", Field(base, "revision").integer), borrowed.values.front()});
    };
    auto values = query();
    Check(values.status == EDITOR_OK && Field(Field(values, "value"), "Schematic\\Max Recent Building Block Count").integer == 42,
          "General Preferences projection lost typed fields");
    const auto catalog = client.Query("preferences.catalog");
    Check(catalog.status == EDITOR_OK && Field(catalog, "version").integer == CKGetVersion() && !Field(catalog, "engines").fields.empty(),
          "General Preferences catalog lost runtime or render engine information");
    const auto unchanged = apply(values, Field(values, "value").fields);
    Check(unchanged.status == EDITOR_NO_CHANGE && !saves && !derived,
          "unchanged preferences wrote the file or reapplied native state");
    const auto beforeRuntime = RuntimeValues(context); const auto beforeFile = Bytes(persisted);
    const auto beforeValues = preferences.Values();
    const auto changes = std::vector<EditorField>{Integer("File Options\\Compression Level", 3), Integer("File Options\\Sounds", 1),
        Integer("File Options\\Textures", 1), Integer("CK2 Settings\\Quantization Sampling Factor", 23), Integer("Schematic\\Io Link Color", 0xFEDCBA)};
    for (int failure = 0; failure < 3; ++failure) {
        failSave = failure == 0; throwSave = failure == 1; failDerived = failure == 2;
        Check(apply(values, changes).status == EDITOR_ERROR && preferences.Values() == beforeValues &&
              RuntimeValues(context) == beforeRuntime && Bytes(persisted) == beforeFile && !saves,
              "preference failure did not restore native state, settings and original file");
    }
    failSave = throwSave = false;
    Check(apply(values, changes).status == EDITOR_OK && saves == 1 && context->GetCompressionLevel() == 3 &&
          context->GetGlobalImagesSaveOptions() == CKTEXTURE_EXTERNAL && context->GetGlobalSoundsSaveOptions() == CKSOUND_INCLUDEORIGINALFILE &&
          GetQuantizationSamplingFactor() == 23 && preferences.ioLinkColor == 0xFEDCBA,
          "preference command did not apply native save settings and schematic palette");
    Check(preferences.recentFiles == snapshot.recentFiles && preferences.paths == snapshot.paths && preferences.dataResources == snapshot.dataResources &&
          preferences.settings["future.setting"] == "preserved" && preferences.defaultLinkDelay == snapshot.defaultLinkDelay,
          "General Preferences overwrote unrelated configuration");
    Check(apply(values, changes).status == EDITOR_STALE, "stale preference draft was accepted");
    values = query();
    for (auto invalid : {Integer("File Options\\Compression Level", 0), Integer("File Options\\Sounds", 8),
                        Integer("File Options\\Textures", 3), Integer("CK2 Settings\\Sound Enabled", 2),
                        Integer("Schematic\\Io Link Color", 0x1000000), Integer("unknown.setting", 1)})
        Check(apply(values, {Integer("File Options\\Compression Level", 9), invalid}).status == EDITOR_INVALID &&
              context->GetCompressionLevel() == 3 && saves == 1, "invalid preference batch partially applied");
    const EditorField nan{"3D Layout\\Actions\\Camera Rotate Step", EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, std::numeric_limits<double>::quiet_NaN()};
    Check(apply(values, {nan}).status == EDITOR_INVALID, "non-finite preference was accepted");
    Check(apply(values, {{"3D Layout\\World Step", EDITOR_TEXT_VALUE, {}, "1|bad|3"}}).status == EDITOR_INVALID,
          "malformed vector preference was accepted");
    Check(apply(values, {{"CK2 Settings\\Render Engine", EDITOR_TEXT_VALUE, {}, "engine\ninjected"}}).status == EDITOR_INVALID,
          "preference text could inject another configuration line");
    const auto formatRuntime = RuntimeValues(context); const auto formatFile = Bytes(persisted);
    Check(apply(values, {{"File Options\\Texture Format", EDITOR_TEXT_VALUE, {}, "missing-format"}, Integer("File Options\\Textures", 2)}).status == EDITOR_ERROR &&
          RuntimeValues(context) == formatRuntime && Bytes(persisted) == formatFile,
          "unavailable image save format changed native settings or the original file");
    const auto dynamic = EditorClient::Integer(client.Query("preferences.dynamic-objects"), "checked");
    const auto toggleFile = Bytes(persisted); const auto toggleRuntime = RuntimeValues(context);
    failSave = true;
    Check(client.Command("preferences.toggle-dynamic-objects").status == EDITOR_ERROR &&
          EditorClient::Integer(client.Query("preferences.dynamic-objects"), "checked") == dynamic &&
          Bytes(persisted) == toggleFile && RuntimeValues(context) == toggleRuntime,
          "dynamic object toggle bypassed preference rollback");
    failSave = false;
    Check(client.Command("preferences.toggle-dynamic-objects").status == EDITOR_OK &&
          EditorClient::Integer(client.Query("preferences.dynamic-objects"), "checked") != dynamic &&
          client.Command("preferences.toggle-dynamic-objects").status == EDITOR_OK,
          "dynamic object toggle did not use the shared preference command");
    values = query();
    const auto engine = Field(Field(values, "value"), "CK2 Settings\\Render Engine").text;
    const auto generation = runtime.Generation();
    Check(apply(values, {{"CK2 Settings\\Render Engine", EDITOR_TEXT_VALUE, {}, "future-renderer"}}).status == EDITOR_OK &&
          runtime.Context() == context && runtime.Generation() == generation && preferences.Text("CK2 Settings\\Render Engine") == "future-renderer" &&
          Field(client.Query("preferences.catalog"), "engine").text == Field(catalog, "engine").text,
          "render engine preference recreated the active runtime instead of waiting for restart");
    Check(apply(query(), {{"CK2 Settings\\Render Engine", EDITOR_TEXT_VALUE, {}, engine}}).status == EDITOR_OK,
          "restore render engine preference");
    values = query();
    preferences.SetInt("File Options\\Compression Level", 6);
    Check(apply(values, changes).status == EDITOR_STALE && context->GetCompressionLevel() == 3, "an out-of-band setting change was overwritten");
    values = query();
    // The command's revalidation also publishes direct changes for a new read.
    Check(Field(Field(values, "value"), "File Options\\Compression Level").integer == 6, "preference revalidation did not publish the current values");
    const auto cached = client.Query("preferences.catalog");
    available = false; editor.NativeAvailable(false);
    Check(client.Query("preferences.catalog").fields == cached.fields && query().fields == values.fields &&
          !EditorClient::Integer(client.Query("preferences.state"), "available") && apply(values, changes).status == EDITOR_REFUSED,
          "busy preference access changed cached data or accepted a native mutation");
    available = true; editor.NativeAvailable(true);
    EditorStatus worker = EDITOR_OK; std::thread thread([&] { worker = query().status; }); thread.join();
    Check(worker == EDITOR_REFUSED, "preference query accepted a worker thread");
    commands.InvalidateCatalog();
    Check(apply(values, changes).status == EDITOR_STALE, "plugin catalog changes retained an actionable preference draft");
    Check(document.Token() == token && document.ChangeRevision() == revision && document.TopologyRevision() == topology &&
          document.History().UndoCount() == undo && document.History().CanRedo() == redo && document.Dirty() == dirty && editor.Selection() == selected,
          "General Preferences changed the document, selection or undo history");
    values = query();
    editor.EnterDraw(); auto pending = apply(values, {Integer("File Options\\Compression Level", 8)}); editor.LeaveDraw();
    Check(pending.status == EDITOR_QUEUED, "preference command did not queue during drawing");
    editor.CloseScope(view); editor.EndFrame();
    Check(app.Completion(pending.operation).status == EDITOR_STALE && context->GetCompressionLevel() == 3,
          "closed preference view applied a queued command");
    const auto nextView = editor.OpenScope(); const EditorClient next(editor.Host(), nextView);
    values = next.Query("preferences.values");
    Check(document.New(&error), error.c_str()); attach();
    const EditorValue compression = IntegerArgument("File Options\\Compression Level", 8);
    const EditorValue args[] = {IntegerArgument("revision", Field(values, "revision").integer),
        {EDITOR_TEXT("value"), EDITOR_RECORD, {}, {}, 0, {}, {}, {}, 0, {}, {&compression, 1}}};
    Check(editor.Execute("preferences.apply", {editor.Scope(nextView), args, 2}).status == EDITOR_STALE,
          "preference draft crossed runtime recreation");
    const std::string recentPath = u8"/资料 空格/Project.nmo";
    preferences.AddRecentFile(recentPath);
    const auto recent = app.Query("preferences.recent-files");
    Check(recent.status == EDITOR_OK && EditorClient::Find(recent, "paths") &&
              EditorClient::Find(recent, "paths")->texts.front() == recentPath,
          "recent file projection changed UTF-8 paths");
    preferences.AddRecentFile("/another.cmo");
    Check(EditorClient::Find(recent, "paths")->texts.front() == recentPath &&
              EditorClient::Find(app.Query("preferences.recent-files"), "paths")->texts.front() == "/another.cmo",
          "recent file projection borrowed the mutable preferences list");
    PreferencesData copied;
    const EditorClient display(editor.Host(), 0);
    Check(copied.Refresh(display) == EDITOR_OK && copied.Int("Schematic\\Default Link Delay") == preferences.defaultLinkDelay,
          "view preference projection differs from the owner");
    const auto ownedValues = copied.Values();
    preferences.defaultLinkDelay += 3;
    Check(copied.Values() == ownedValues, "view preferences borrowed mutable application data");
    commands.Poll();
    Check(copied.Refresh(display) == EDITOR_OK && copied.Int("Schematic\\Default Link Delay") == preferences.defaultLinkDelay,
          "view preference projection did not follow a published change");
    commands.Stop();
    Check(!EditorClient::Integer(app.Query("preferences.state"), "available"), "stopped preferences accepted new work");
}

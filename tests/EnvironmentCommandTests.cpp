#include "features/environment/EnvironmentCommands.h"
#include "workspace/EditorClient.h"
#include "editor/ImGuiDrawing.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/TextEncoding.h"
#include <filesystem>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
const EditorField &Field(const EditorResponse &response, const char *key) {
    const auto *field = EditorClient::Find(response, key); Check(field != nullptr, "missing environment field"); return *field;
}
const EditorField &Field(const EditorField &record, const char *key) {
    for (const auto &field : record.fields) if (field.key == key) return field;
    throw std::runtime_error("missing plugin field");
}
} // namespace
void TestEnvironmentCommands(vdev::CkRuntimeHost &runtime, const std::string &fixture) {
    using namespace vdev;
    namespace fs = std::filesystem;
    Document document(runtime); std::string error;
    Check(document.New(&error), error.c_str());
    bool available = true, saveFailure = false, saveThrows = false;
    unsigned saved = 0, catalogs = 0, reads = 0;
    PathCategories preferences;
    EditorApplication editor([&] { ++reads; return &document; }, [&] { return available; });
    EnvironmentCommands environment(editor, runtime, [&] { return available; },
        [&](const PathCategories &paths, std::string &error) {
            if (saveThrows) throw std::runtime_error("Injected preferences save exception");
            if (saveFailure) { error = "Injected preferences save refusal"; return false; }
            preferences = paths; ++saved; return true;
        }, [&] { ++catalogs; });
    EnvironmentCommands::LinkOptions linkOptions{4, true, runtime.Context()->GetBehaviorManager()->GetBehaviorMaxIteration()};
    const auto originalLinkOptions = linkOptions;
    unsigned linkSaves = 0;
    environment.ConfigureLinks([&] { return linkOptions; }, [&](const EnvironmentCommands::LinkOptions &next, std::string &error) {
        if (saveThrows) throw std::runtime_error("Injected link properties save exception");
        if (saveFailure) { error = "Injected link properties save refusal"; return false; }
        linkOptions = next; ++linkSaves; return true;
    });
    Check(environment.Register(error) && editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto attach = [&] { editor.Attach(document.Token(), document.TopologyRevision(), true, true); };
    attach();
    const auto one = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(one.ok, "create environment selection");
    const auto selected = document.Reference(one.created); editor.SetSelection(selected);
    document.History().Clear(); document.MarkSaved(); document.History().MarkSavePoint();
    Check(RenameLevelObject(document, selected, "Environment selected").ok && document.History().Undo(), "create environment Redo");
    attach(); const auto undo = document.History().UndoCount(); const auto topology = document.TopologyRevision();
    const bool dirty = document.Dirty(), redo = document.History().CanRedo();
    const auto token = document.Token();
    const auto scope = editor.OpenScope();
    const EditorClient client(editor.Host(), scope);
    const auto query = [&](const char *id, std::vector<EditorValue> args = {}) { return client.Query(id, args); };
    const auto command = [&](const char *id, std::vector<EditorValue> args = {}) { return client.Command(id, args); };
    auto paths = query("paths.list"); Check(paths.status == EDITOR_OK && !Field(paths, "categories").fields.empty(), "search path projection absent");
    const auto apply = [&](const EditorResponse &base, const std::vector<std::string> &values, uint64_t version = 0) {
        auto categories = Field(base, "categories"); categories.fields[0].fields[1].texts = values;
        const std::vector<EditorField> fields{categories};
        const auto borrowed = BorrowEditorFields(fields);
        return command("paths.apply", {IntegerArgument("revision", version ? version : Field(base, "revision").integer), borrowed.values.front()});
    };
    const auto original = paths;
    PathCategories startupPaths;
    for (const auto &category : Field(original, "categories").fields) {
        startupPaths.emplace_back(Utf8ToAnsi(Field(category, "name").text), std::vector<std::string>{});
        for (const auto &path : Field(category, "paths").texts) startupPaths.back().second.push_back(Utf8ToAnsi(path));
    }
    Check(startupPaths.size() > 1, "native path categories missing");
    startupPaths[0].second = {"startup-path", "startup-path"};
    startupPaths[1].second = {"after-duplicate"};
    Check(environment.InitializePaths(startupPaths, error), "duplicate saved path interrupted initialization");
    paths = query("paths.list");
    Check(Field(Field(paths, "categories").fields[0], "paths").texts == std::vector<std::string>{"startup-path"} &&
          Field(Field(paths, "categories").fields[1], "paths").texts == std::vector<std::string>{"after-duplicate"} && saved == 0,
          "saved paths did not retain native duplicate handling or wrote preferences during initialization");
    Check(apply(paths, {"search-a", "search-b"}).status == EDITOR_OK && saved == 1 &&
          preferences[0].second == std::vector<std::string>({"search-a", "search-b"}), "search paths did not reach preferences through their command");
    const auto oldRevision = Field(paths, "revision").integer;
    paths = query("paths.list");
    Check(Field(paths, "revision").integer != oldRevision &&
          Field(paths, "categories").fields[0].fields[1].texts == std::vector<std::string>({"search-a", "search-b"}), "path publication was not refreshed");
    Check(apply(paths, {"stale"}, oldRevision).status == EDITOR_STALE && saved == 1, "stale path draft overwrote newer settings");
    Check(apply(paths, {"same", "same"}).status == EDITOR_INVALID && apply(paths, {""}).status == EDITOR_INVALID &&
          apply(paths, {std::string("bad\0path", 8)}).status == EDITOR_INVALID && apply(paths, {"bad\npath"}).status == EDITOR_INVALID,
          "invalid search paths reached the native manager");
    for (int failure : {1, 2}) {
        saveFailure = failure == 1; saveThrows = failure == 2;
        const auto result = apply(paths, {"unpublished"});
        saveFailure = saveThrows = false;
        const auto after = query("paths.list");
        Check(result.status == EDITOR_ERROR && Field(after, "categories").fields == Field(paths, "categories").fields &&
              preferences[0].second == std::vector<std::string>({"search-a", "search-b"}) && saved == 1,
              "failed path publication did not restore native paths and preferences");
    }
    auto links = query("environment.link-options");
    Check(links.status == EDITOR_OK && Field(links, "delay").integer == 4, "behavior link properties query missing");
    const auto changeLinks = [&](const EditorResponse &base, uint64_t delay, uint64_t prevent, uint64_t iterations) {
        return command("environment.set-link-options", {IntegerArgument("revision", Field(base, "revision").integer),
            IntegerArgument("delay", delay), IntegerArgument("prevent", prevent), IntegerArgument("iterations", iterations)});
    };
    Check(changeLinks(links, 12, 0, 64).status == EDITOR_OK && linkOptions.delay == 12 && !linkOptions.preventLoop && linkSaves == 1 &&
          runtime.Context()->GetBehaviorManager()->GetBehaviorMaxIteration() == 64, "link properties did not update preferences and the behavior manager");
    Check(changeLinks(links, 3, 1, 32).status == EDITOR_STALE && linkSaves == 1, "stale link properties overwrote current preferences");
    links = query("environment.link-options");
    Check(changeLinks(links, UINT64_MAX, 1, 64).status == EDITOR_INVALID && changeLinks(links, 1, 2, 64).status == EDITOR_INVALID &&
          changeLinks(links, 1, 1, 0).status == EDITOR_INVALID, "invalid link properties reached native settings");
    for (int failure : {1, 2}) {
        saveFailure = failure == 1; saveThrows = failure == 2;
        const auto result = changeLinks(links, 18, 1, 99); saveFailure = saveThrows = false;
        Check(result.status == EDITOR_ERROR && query("environment.link-options").fields == links.fields &&
              linkOptions.delay == 12 && !linkOptions.preventLoop && linkSaves == 1 &&
              runtime.Context()->GetBehaviorManager()->GetBehaviorMaxIteration() == 64,
              "failed link settings save did not restore the runtime and preferences");
    }
    runtime.Context()->GetBehaviorManager()->SetBehaviorMaxIteration(65);
    Check(changeLinks(links, 18, 1, 32).status == EDITOR_STALE && runtime.Context()->GetBehaviorManager()->GetBehaviorMaxIteration() == 65,
          "native link setting changes were overwritten by an old dialog");
    links = query("environment.link-options");
    Check(changeLinks(links, originalLinkOptions.delay, originalLinkOptions.preventLoop, originalLinkOptions.iterations).status == EDITOR_OK,
          "restore native link properties");
    auto plugins = query("plugins.list"); Check(plugins.status == EDITOR_OK, "plugin projection missing");
    std::vector<EditorField> rows;
    uint64_t first = 0, version = Field(plugins, "revision").integer;
    for (;;) {
        const auto page = query("plugins.list", {IntegerArgument("first", first), IntegerArgument("revision", version)});
        Check(page.status == EDITOR_OK && Field(page, "rows").fields.size() <= 128, "plugin page changed while reading");
        rows.insert(rows.end(), Field(page, "rows").fields.begin(), Field(page, "rows").fields.end());
        const auto next = Field(page, "next").integer;
        if (next == Field(page, "total").integer) break;
        Check(next > first, "plugin paging did not advance"); first = next;
    }
    Check(!rows.empty() && Field(rows.front(), "category").text == "Bitmap Readers", "plugin category order changed");
    for (const auto &row : rows) {
        const auto *manager = [&]() -> const EditorField * { for (const auto &f : row.fields) if (f.key == "manager") return &f; return nullptr; }();
        if (!manager || !manager->integer) continue;
        const auto active = Field(row, "active").integer;
        const auto id = Field(row, "id").integer;
        Check(command("plugins.set-manager-active", {IntegerArgument("plugin", id), IntegerArgument("revision", version), IntegerArgument("active", !active)}).status == EDITOR_OK,
              "manager activation did not use its registered command");
        const auto state = query("plugins.list"); const auto nextVersion = Field(state, "revision").integer;
        Check(nextVersion != version && command("plugins.set-manager-active", {IntegerArgument("plugin", id), IntegerArgument("revision", version), IntegerArgument("active", active)}).status == EDITOR_STALE,
              "stale manager row was accepted");
        Check(command("plugins.set-manager-active", {IntegerArgument("plugin", id), IntegerArgument("revision", nextVersion), IntegerArgument("active", active)}).status == EDITOR_OK,
              "manager activity could not be restored"); break;
    }
    available = false; editor.NativeAvailable(false);
    const auto frozenPaths = query("paths.list"), frozenPlugins = query("plugins.list"); const auto nativeReads = reads;
    Check(query("paths.list").fields == frozenPaths.fields && query("plugins.list").fields == frozenPlugins.fields &&
          !Field(query("environment.state"), "available").integer && reads == nativeReads,
          "busy environment queries entered native state or changed their published data");
    Check(apply(paths, {"busy"}).status == EDITOR_REFUSED, "busy path command modified native settings");
    available = true; editor.NativeAvailable(true);
    EditorStatus threaded = EDITOR_OK;
    std::thread worker([&] { threaded = query("paths.list").status; }); worker.join();
    Check(threaded == EDITOR_REFUSED, "environment query accepted a worker thread");
    // Folder input is owned by the caller's scope. Late native completions
    // cannot reach a newly opened dialog, including after document replacement.
    auto choose = command("paths.choose-folder"); Check(choose.status == EDITOR_QUEUED && choose.operation, "folder input was not deferred");
    auto input = query("paths.folder-input"); const auto firstInput = Field(input, "input").integer;
    Check(command("paths.folder-response", {IntegerArgument("input", firstInput), TextArgument("choice", "accept"), TextArgument("path", "chosen-folder")}).status == EDITOR_OK &&
          EditorClient::Text(client.Completion(choose.operation), "path") == "chosen-folder", "folder completion lost its path");
    choose = command("paths.choose-folder"); input = query("paths.folder-input"); const auto closedInput = Field(input, "input").integer;
    Check(command("paths.folder-response", {IntegerArgument("input", firstInput), TextArgument("choice", "cancel")}).status == EDITOR_STALE, "late folder result reached a newer operation");
    editor.CloseScope(scope); available = false; editor.NativeAvailable(false); environment.Poll();
    Check(client.Completion(choose.operation).status == EDITOR_STALE, "closed scope remained usable");
    available = true; editor.NativeAvailable(true);
    const EditorClient app(editor.Host(), 0);
    Check(app.Completion(choose.operation).status == EDITOR_CANCELLED, "closed folder operation was not completed");
    Check(app.Command("paths.folder-response", {IntegerArgument("input", closedInput), TextArgument("choice", "accept"), TextArgument("path", "late")}).status == EDITOR_STALE,
          "closed folder input was accepted");
    // Use a tiny native extension with no managers, readers or document data.
    // Renaming its unloaded file causes an actual SDL/native loader failure.
    const auto directory = fs::temp_directory_path() / ("nemodev-environment-" + std::to_string(document.Token().value));
    fs::create_directories(directory);
    const auto file = directory / fs::u8path(fixture).filename(), held = directory / (file.filename().u8string() + ".held");
    fs::copy_file(fs::u8path(fixture), file, fs::copy_options::overwrite_existing);
    auto *native = CKGetPluginManager();
    Check(native->RegisterPlugin(file.u8string().c_str()) == CK_OK, "register native lifecycle fixture");
    struct Cleanup {
        fs::path file, held; CKPluginManager *native;
        ~Cleanup() {
            std::error_code error;
            if (fs::exists(held, error)) fs::rename(held, file, error);
            int index = -1; if (native->GetPluginDllInfo(file.u8string().c_str(), &index)) native->UnLoadPluginDll(index);
            fs::remove(file, error); fs::remove(held, error); fs::remove(file.parent_path(), error);
        }
    } cleanup{file, held, native};
    const auto pluginRow = [&]() {
        // A command forces a fresh native read; polling also sees fixture
        // additions without requiring document edits.
        Check(app.Command("environment.refresh").status == EDITOR_OK, "refresh native environment projection");
        auto result = app.Query("plugins.list", {IntegerArgument("first", 0)});
        uint64_t offset = 0;
        for (;;) {
            for (const auto &row : Field(result, "rows").fields)
                if (const auto *name = [&]() -> const EditorField * { for (const auto &f : row.fields) if (f.key == "name") return &f; return nullptr; }();
                    name && name->text == "Environment reload fixture") return std::make_pair(row, Field(result, "revision").integer);
            offset = Field(result, "next").integer;
            Check(offset < Field(result, "total").integer, "native fixture missing from plugin query");
            result = app.Query("plugins.list", {IntegerArgument("first", offset)});
        }
    };
    environment.Poll();
    auto fixtureScope = editor.OpenScope();
    auto fixtureClient = EditorClient(editor.Host(), fixtureScope);
    auto fixturePlugin = pluginRow();
    const auto mutate = [&](const EditorClient &origin, const char *id, const std::pair<EditorField, uint64_t> &row) {
        const auto before = catalogs;
        auto result = origin.Command(id, {IntegerArgument("plugin", Field(row.first, "id").integer), IntegerArgument("revision", row.second)});
        Check(catalogs == before + 1, "native plugin operation did not invalidate dependent catalogs");
        return result;
    };
    Check(mutate(fixtureClient, "plugins.unload", fixturePlugin).status == EDITOR_OK, "native temporary unload failed");
    fixturePlugin = pluginRow();
    Check(Field(fixturePlugin.first, "unloaded").integer && !Field(fixturePlugin.first, "loaded").integer, "unload state missing");
    Check(mutate(app, "plugins.reload", fixturePlugin).status == EDITOR_OK, "external caller could not reload a plugin");
    fixturePlugin = pluginRow();
    Check(mutate(fixtureClient, "plugins.unload", fixturePlugin).status == EDITOR_OK, "second native temporary unload failed");
    fs::rename(file, held);
    fixturePlugin = pluginRow();
    Check(mutate(app, "plugins.reload", fixturePlugin).status == EDITOR_ERROR, "missing plugin library did not refuse explicit reload");
    fixturePlugin = pluginRow();
    Check(Field(fixturePlugin.first, "unloaded").integer && !Field(fixturePlugin.first, "error").text.empty(),
          "explicit reload failure discarded recovery information");
    editor.CloseScope(fixtureScope);
    available = false; editor.NativeAvailable(false); environment.Poll();
    Check(Field(app.Query("environment.state"), "recoveries").integer == 1, "busy close discarded pending plugin recovery");
    available = true; editor.NativeAvailable(true); environment.Poll();
    fixturePlugin = pluginRow();
    Check(Field(fixturePlugin.first, "unloaded").integer && !Field(fixturePlugin.first, "error").text.empty(), "failed close recovery was discarded");
    fs::rename(held, file);
    Check(app.Command("plugins.retry-reloads").status == EDITOR_OK, "plugin recovery retry failed");
    fixturePlugin = pluginRow();
    Check(Field(fixturePlugin.first, "loaded").integer && !Field(fixturePlugin.first, "unloaded").integer && catalogs >= 5,
          "plugin recovery did not restore the library and refresh catalogs");
    Check(document.Token() == token && document.TopologyRevision() == topology && document.Dirty() == dirty &&
          document.History().UndoCount() == undo && document.History().CanRedo() == redo && editor.Selection() == selected,
          "environment operations changed the document, selection or undo history");
    // Original paths are restored through the same command before teardown.
    const auto currentPaths = app.Query("paths.list");
    const std::vector<EditorField> originalFields{Field(original, "categories")};
    const auto borrowed = BorrowEditorFields(originalFields);
    Check(app.Command("paths.apply", {IntegerArgument("revision", Field(currentPaths, "revision").integer), borrowed.values.front()}).status == EDITOR_OK,
          "restore native search paths");
    const auto beforeReplacement = app.Query("paths.list");
    const auto replacingScope = editor.OpenScope();
    const EditorClient replacingClient(editor.Host(), replacingScope);
    const auto pending = replacingClient.Command("paths.choose-folder");
    Check(pending.status == EDITOR_QUEUED, "prepare folder input before document replacement");
    const auto replacingInput = Field(app.Query("paths.folder-input"), "input").integer;
    // No plugin/path view is open. Context generation changes must still
    // invalidate the application projection before another view is opened.
    Check(document.New(&error), error.c_str()); attach(); environment.Poll();
    const auto afterReplacement = app.Query("paths.list");
    Check(Field(afterReplacement, "revision").integer != Field(beforeReplacement, "revision").integer &&
          Field(afterReplacement, "categories").fields == Field(beforeReplacement, "categories").fields,
          "document replacement retained a stale path revision or lost application search paths");
    Check(app.Completion(pending.operation).status == EDITOR_CANCELLED &&
          app.Command("paths.folder-response", {IntegerArgument("input", replacingInput), TextArgument("choice", "accept"), TextArgument("path", "old-document")}).status == EDITOR_STALE,
          "old document folder completion reached the replacement document");
    editor.CloseScope(replacingScope);
    fixtureScope = editor.OpenScope(); fixtureClient = EditorClient(editor.Host(), fixtureScope); fixturePlugin = pluginRow();
    Check(mutate(fixtureClient, "plugins.unload", fixturePlugin).status == EDITOR_OK, "prepare shutdown restoration");
    environment.Stop();
    int index = -1; const auto *restored = native->GetPluginDllInfo(file.u8string().c_str(), &index);
    Check(restored && restored->m_DllInstance, "shutdown did not reload a still-owned plugin");
}

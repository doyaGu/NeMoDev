#include "application/EditorProfile.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
std::string Encoded(const vdev::EditorProfile &profile) {
    std::string text, error; Check(profile.Encode(text, error), error); return text;
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("nemodev-profile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);
        const auto canonicalRoot = std::filesystem::canonical(root);
        const auto path = (canonicalRoot / "profile.json").u8string();
        vdev::Preferences preferences;
        preferences.strictMultiSelection = true; preferences.defaultLinkDelay = 7;
        preferences.recentFiles = {u8"/中文/scene.cmo"}; preferences.pathsSaved = true;
        preferences.paths = {{"Textures", {u8"/图片/资源"}}, {"Sounds", {"/sound files"}}};
        preferences.dataResources = {u8"/资源/data.rsc"};
        vdev::EditorProfile profile; std::string error;
        Check(profile.Load(path, error) == vdev::EditorProfile::ReadResult::Missing, error);
        const std::string initial =
            R"({"version":1,"future":{"number":1234567890123456789,"items":[true,null,"unicode"]},
            "preferences":{"custom":{"keep":42},"values":{"unknown.module.setting":{"raw":true}}},
            "modules":[{"id":"custom.viewer","enabled":false,"path":"module files/viewer.dylib","private":{"keep":"module data"}}],
            "providers":[{"kind":"view","id":"custom.view","provider":"custom.provider","module":"custom.viewer","private":"provider data"}],
            "bindings":[{"action":"custom.open","chord":"Ctrl+Shift+K","module":"custom.viewer","private":"binding data"}],
            "toolbar-items":[{"id":"custom.toolbar","location":"custom.tools","action":"custom.open","position":[1,2],"size":[50,19],"visible":false,"module":"custom.viewer","private":"toolbar data"}],
            "menus":[{"id":"custom.menu","location":"menu.tools","order":25,"module":"custom.viewer","private":"menu data"}]})";
        Check(profile.Decode(initial, path, error), error);
        Check(profile.Contributions().toolbarItems.empty(), "disabled module toolbar override was applied");
        Check(profile.Contributions().providers.empty(), "disabled module provider was applied");
        Check(profile.Contributions().bindings.empty() && profile.Contributions().menus.empty(), "disabled module input overrides were applied");
        Check(std::filesystem::u8path(profile.ResolvedModules().front().path) == canonicalRoot / "module files/viewer.dylib",
              "relative module path did not use the profile directory");
        Check(profile.Capture(preferences, {}, {}, [](const auto &) { return std::string{}; }, error), error);
        auto text = Encoded(profile);
        for (const auto *kept : {"1234567890123456789", "unknown.module.setting", "module data", "provider data",
                                 "binding data", "menu data", "toolbar data", "custom"})
            Check(text.find(kept) != std::string::npos, std::string("lost unknown data: ") + kept);
        vdev::EditorProfile restored;
        Check(restored.Decode(text, path, error) && restored.Settings().Values(false) == preferences.Values(false) &&
            restored.Settings().recentFiles == preferences.recentFiles && restored.Settings().paths == preferences.paths &&
            restored.Settings().pathsSaved && restored.Settings().dataResources == preferences.dataResources,
            "preferences did not round-trip through the profile: " + error);
        Check(restored.SetModule({"custom.viewer", true, "module files/viewer.dylib"}, error) &&
                  restored.Contributions().providers.size() == 1 && restored.Contributions().bindings.size() == 1 &&
                  restored.Contributions().menus.size() == 1 && restored.Contributions().toolbarItems.size() == 1,
              "enabling a module lost its deferred choices");
        Check(Encoded(restored).find("module data") != std::string::npos, "module edit lost private fields");
        const auto valid = Encoded(restored);
        for (
            const auto &bad : std::vector<std::string>{
                R"({"version":1,"toolbar-items":[{"id":"test.button","position":[1]}]})",
                R"({"version":1,"toolbar-items":[{"id":"test.button","size":[0,1]}]})",
                R"({"version":1,"toolbar-items":[{"id":"test.button"},{"id":"test.button"}]})", "{", "[]",
                R"({"version":2})", R"({"version":1,"version":1})", R"({"version":1,"modules":{}})",
                R"({"version":1,"modules":[{"id":"x.y","enabled":1}]})",
                R"({"version":1,"modules":[{"id":"x.y","enabled":true},{"id":"x.y","enabled":false}]})",
                R"({"version":1,"modules":[{"id":"x.y","enabled":true,"source":"builtin","path":"x"}]})",
                R"({"version":1,"modules":[{"id":"application.operations","enabled":false}]})",
                R"({"version":1,"providers":[{"kind":"tool","id":"x.y","provider":"x.z"}]})",
                R"({"version":1,"providers":[{"kind":"view","id":"x.y","provider":"x.z"},{"kind":"view","id":"x.y","provider":"x.q"}]})",
                R"({"version":1,"preferences":{"values":{"Schematic\\Default Link Delay":"not a number"}}})",
                R"({"version":1,"menus":[{"id":"x.y","location":"menu.tools","order":18446744073709551615}]})",
                R"({"version":1,"bindings":[{"action":"x.y","chord":"Ctrl+K","module":false}]})",
                std::string("{\"version\":1,\"bad\":\"") + char(0xff) + "\"}"}) {
            Check(!restored.Decode(bad, path, error) && !error.empty() && Encoded(restored) == valid,
                  "invalid input replaced the last valid profile");
        }
        std::string deep = "0"; for (int i = 0; i < 70; ++i) deep = "[" + deep + "]";
        Check(!restored.Decode("{\"version\":1,\"deep\":" + deep + "}", path, error) && Encoded(restored) == valid,
              "excessive nesting changed the profile");
        Check(!restored.SetModule({"application.operations", false}, error) && Encoded(restored) == valid,
              "kernel module was disabled");
        vdev::EditorConfiguration defaults, selected;
        defaults.providers.push_back({"view", "custom.view", "builtin.view"}); selected = defaults;
        selected.providers[0].provider = "other.view";
        Check(restored.Capture(preferences, selected, defaults, [](const auto &choice) {
            return choice.provider == "other.view" ? std::string("other.module") : std::string{};
        }, error), error);
        Check(restored.Contributions().providers.size() == 1 && restored.Contributions().providers[0].provider == "other.view",
              "new provider choice duplicated a deferred one");
        Check(restored.Capture(preferences, defaults, defaults, [](const auto &) { return std::string("other.module"); }, error) &&
            restored.Contributions().providers.empty(), "choosing a default kept the old override");
        defaults.bindings = {{"custom.open", "Ctrl+K"}, {"custom.open", "Ctrl+Shift+K"}};
        Check(restored.Capture(preferences, defaults, defaults, [](const auto &) { return std::string("other.module"); }, error) &&
            restored.Contributions().bindings == defaults.bindings, "multiple shortcuts for one action did not round-trip");
        vdev::EditorProfile shortcuts;
        Check(shortcuts.Decode(R"({"version":1,"bindings":[
            {"action":"custom.open","chord":"Ctrl+K","private":"first shortcut data"},
            {"action":"custom.open","chord":"Ctrl+Shift+K","private":"second shortcut data"}]})", path, error), error);
        Check(shortcuts.Capture(preferences, defaults, defaults, {}, error) &&
            Encoded(shortcuts).find("first shortcut data") != std::string::npos &&
            Encoded(shortcuts).find("second shortcut data") != std::string::npos, "shortcut metadata moved to another binding");
        Check(restored.AddSources({"relative module.so"}, {u8"中文 modules"}, error) &&
            restored.Files().front() == (canonicalRoot / "relative module.so").u8string() &&
            restored.Directories().front() == (canonicalRoot / std::filesystem::u8path(u8"中文 modules")).u8string(),
              "discovery inputs did not round-trip");
        std::ofstream(std::filesystem::u8path(path)) << Encoded(restored);
        vdev::EditorProfile loaded;
        Check(loaded.Load(path, error) == vdev::EditorProfile::ReadResult::Loaded && Encoded(loaded) == Encoded(restored), error);
        const auto retained = Encoded(loaded);
        Check(loaded.Load(root.u8string(), error) == vdev::EditorProfile::ReadResult::Failed && Encoded(loaded) == retained,
              "failed file read changed the profile");
        Check(loaded.Load((canonicalRoot / "new.json").u8string(), error) == vdev::EditorProfile::ReadResult::Missing &&
              loaded.Modules().empty() && loaded.Settings().Values(false) == vdev::Preferences{}.Values(false),
              "a new profile inherited settings from the previous file");
        for (const auto *name : {"default.ini", "custom.ini"}) {
            vdev::Preferences legacy; const auto ini = std::filesystem::path(PREFERENCE_FIXTURES) / name;
            std::ifstream input(ini, std::ios::binary);
            const std::string before{std::istreambuf_iterator<char>(input), {}};
            Check(legacy.Load(ini.u8string()), "legacy INI fixture did not load");
            Check(profile.Capture(legacy, {}, {}, {}, error) && restored.Decode(Encoded(profile), path, error) &&
                legacy.Values(false) == restored.Settings().Values(false) && legacy.paths == restored.Settings().paths &&
                legacy.recentFiles == restored.Settings().recentFiles, "legacy INI migration lost stored settings");
            std::ifstream check(ini, std::ios::binary);
            Check(before == std::string(std::istreambuf_iterator<char>(check), {}), "legacy INI migration changed the source");
        }
        vdev::WorkspaceState workspace;
        workspace.configuration.views.push_back({"custom.view", "custom.primary", "", true});
        vdev::WorkspaceViewState view;
        view.instance = 42;
        view.type = "custom.view";
        view.provider = "custom.provider";
        view.role = "custom.primary";
        view.title = u8"跨启动视图";
        view.version = 1;
        view.settings = {{"text", EDITOR_TEXT_VALUE, {}, u8"中文 navigation"}, {"numbers", EDITOR_REAL_LIST}};
        view.settings[1].reals = {.25, .5};
        workspace.views.push_back(view);
        workspace.focused = 42;
        workspace.layout = "[Window][Saved###editor-view-42]\nPos=10,20\nSize=300,200\n";
        Check(profile.SetWorkspace(workspace, error) && restored.Decode(Encoded(profile), path, error), error);
        Check(restored.Workspace() && restored.Workspace()->views.size() == 1 &&
                  restored.Workspace()->views[0].settings == view.settings &&
                  restored.Workspace()->layout == workspace.layout && restored.Workspace()->focused == 42,
              "workspace settings did not round-trip");
        const auto savedWorkspace = Encoded(restored);
        auto invalid = workspace;
        invalid.views[0].settings.push_back({"native", EDITOR_OBJECT});
        Check(!restored.SetWorkspace(invalid, error) && Encoded(restored) == savedWorkspace,
              "native identity entered persistent settings");
        invalid = workspace;
        invalid.views.push_back(invalid.views[0]);
        Check(!restored.SetWorkspace(invalid, error) && Encoded(restored) == savedWorkspace,
              "duplicate instance replaced saved workspace");
        invalid = workspace;
        invalid.views[0].width = -1;
        invalid.views[0].placed = true;
        Check(!restored.SetWorkspace(invalid, error), "invalid saved window position accepted");
        std::string unknown = savedWorkspace;
        const auto at = unknown.find("\"state-version\": 1");
        Check(at != std::string::npos, "saved state version missing");
        unknown.insert(at, "\"future-window-key\": {\"keep\": true}, ");
        Check(restored.Decode(unknown, path, error) && restored.SetWorkspace(workspace, error) &&
                  Encoded(restored).find("future-window-key") != std::string::npos,
              "workspace rewrite discarded unknown window fields");
        std::filesystem::remove_all(root);
        std::cout << "Profile round-trip, unknown values, migration values, module choices and invalid input passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n'; std::error_code ignored; std::filesystem::remove_all(root, ignored); return 1;
    }
}

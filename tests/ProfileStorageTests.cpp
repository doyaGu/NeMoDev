#include "ui/native/EditorProfileStorage.h"
#include "application/ProfileCommands.h"
#include "workspace/EditorClient.h"
#include "workspace/EditorWorkspace.h"
#include "core/document/SaveFileOperations.h"
#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace vdev;
namespace fs = std::filesystem;
void Check(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
std::string Bytes(const fs::path &path) {
    std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}};
}
std::string Encoded(const EditorProfile &profile) {
    std::string text, error; Check(profile.Encode(text, error), error); return text;
}
void Put(const fs::path &path, const std::string &text) {
    std::ofstream output(path, std::ios::binary); output << text; output.close(); Check(bool(output), "Cannot create test file");
}
struct Directory {
    fs::path root = fs::temp_directory_path() /
        ("nemodev-profile-storage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() { fs::create_directories(root / "user"); fs::create_directories(root / "app"); }
    ~Directory() { std::error_code error; fs::remove_all(root, error); }
};
struct Operations : SaveFileOperations {
    bool fail = false, throwing = false;
    unsigned writes = 0;
    std::error_code Replace(const fs::path &source, const fs::path &destination, bool overwrite) override {
        ++writes;
        if (throwing) throw std::runtime_error("Injected profile publication exception");
        if (fail) return std::make_error_code(std::errc::permission_denied);
        return SaveFileOperations::Replace(source, destination, overwrite);
    }
};
struct Provider;
struct View {
    Provider *provider;
    EditorView id;
    std::string topic = "original topic";
};
struct Provider {
    std::map<EditorView, View *> views;
    unsigned destroyed = 0;
};
EditorViewDesc Describe(Provider &provider, const char *name) {
    EditorViewDesc desc{};
    desc.size = sizeof(desc); desc.imguiVersion = EDITOR_IMGUI_VERSION;
    desc.id = EDITOR_TEXT("test.profile-view"); desc.provider = EditorLiteral(name);
    desc.title = EDITOR_TEXT("Profile view"); desc.area = EDITOR_WORKSPACE;
    desc.data = &provider; desc.singleInstance = 1; desc.stateVersion = 1;
    desc.create = [](void *data, const EditorHost *, EditorView id) -> void * {
        auto &provider = *static_cast<Provider *>(data);
        auto *view = new View{&provider, id};
        provider.views.emplace(id, view);
        return view;
    };
    desc.destroy = [](void *data) {
        auto *view = static_cast<View *>(data);
        view->provider->views.erase(view->id); ++view->provider->destroyed;
        delete view;
    };
    desc.closing = [](void *data, const EditorHost *, EditorView) { static_cast<View *>(data)->topic.clear(); };
    desc.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *) {};
    static const EditorParameter topic{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE};
    desc.fields = desc.settings = &topic; desc.fieldCount = desc.settingCount = 1;
    desc.inspect = desc.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const EditorValue value{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, {}, EditorText(static_cast<View *>(data)->topic)};
        return sink.write(sink.data, &value, 1);
    };
    desc.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        auto &view = *static_cast<View *>(data);
        for (uint32_t i = 0; i < request->count; ++i)
            if (std::string(request->values[i].key.data, request->values[i].key.size) == "topic")
                view.topic.assign(request->values[i].text.data, request->values[i].text.size);
        return EDITOR_OK;
    };
    return desc;
}
struct Fixture {
    Directory directory;
    Operations operations;
    Provider old, next;
    EditorApplication editor{[]() -> Document * { return nullptr; }};
    EditorWorkspace workspace{editor};
    bool refusePreparation = false, throwPreparation = false;
    EditorProfileStorage storage{editor, workspace, [this](std::string &error) {
        if (throwPreparation) throw std::runtime_error("Injected configuration preparation exception");
        if (refusePreparation) { error = "Injected configuration preparation refusal"; return false; }
        return true;
    }, &operations};
    ProfileCommands commands{editor, storage.Profile(), [this](EditorProfile &next, std::string &error) {
        return storage.Save(next, error);
    }};
    WorkspaceConfiguration defaults;
    std::string error;
    fs::path File() const { return directory.root / u8"中文 profile.json"; }
    EditorProfilePaths Paths(bool isolated = true) const {
        return {File().u8string(), (directory.root / "user").u8string(), (directory.root / "app").u8string(),
                directory.root.u8string(), isolated};
    }
    Fixture() {
        ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
        EditorViewDesc views[] = {Describe(old, "test.original"), Describe(next, "test.replacement")};
        EditorModuleDesc module{};
        module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.profile-view");
        module.views = views; module.viewCount = 2;
        defaults.contributions.providers.push_back({"view", "test.profile-view", "test.original"});
        defaults.views.push_back({"test.profile-view", "main.level", {}, false});
        Check(workspace.Register(error) && commands.Register(error) && editor.Register(module, error) &&
                  editor.Activate(EDITOR_IMGUI_VERSION, error, defaults.contributions), error);
        editor.Attach({}, 0, true, false);
    }
    ~Fixture() {
        storage.Stop(); workspace.CloseAll(); ImGui::DestroyContext();
    }
    void Load(bool isolated = true) { Check(storage.Load(Paths(isolated), error), error); }
    void Restore() {
        std::string diagnostic;
        Check(storage.RestoreWorkspace(defaults, diagnostic, error) && diagnostic.empty(), error + diagnostic);
        Check(workspace.Role("main.level"), "Default Level view did not open");
    }
    void Start() { Load(); Restore(); Check(storage.Start({}, {}, error), error); }
    EditorResponse SetModule(bool enabled) {
        EditorField value{"module", EDITOR_RECORD};
        value.fields = {{"id", EDITOR_TEXT_VALUE, {}, "test.profile-view"},
                        {"enabled", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(enabled)}};
        const std::vector<EditorField> fields{std::move(value)};
        const auto values = BorrowEditorFields(fields);
        return editor.Execute("profile.set-module", {editor.Scope(), values.data(), uint32_t(values.size())});
    }
    void Seed() { Put(File(), R"({"version":1,"future":{"keep":1234567890123456789}})"); }
    void CleanTemporaries() {
        for (const auto &entry : fs::directory_iterator(directory.root))
            Check(entry.is_directory() || entry.path() == File(), "Failed save left a temporary file");
    }
};

void LegacyPreferences(bool user) {
    Fixture f;
    Preferences base; base.defaultLinkDelay = 17;
    Check(base.Save((f.directory.root / "app" / "SchematicEditorPreferences.ini").u8string()), "Cannot seed old preferences");
    if (user) {
        base.defaultLinkDelay = 29;
        Check(base.Save((f.directory.root / "user" / "VirtoolsDevPreferences.ini").u8string()), "Cannot seed user preferences");
    }
    const auto legacy = user ? f.directory.root / "user" / "VirtoolsDevPreferences.ini" :
                               f.directory.root / "app" / "SchematicEditorPreferences.ini";
    const auto before = Bytes(legacy);
    f.Load(false);
    Check(f.storage.Settings().defaultLinkDelay == (user ? 29 : 17), "Wrong legacy preference priority");
    f.Restore(); Check(f.storage.Start({}, {}, f.error), f.error);
    Check(Bytes(legacy) == before, "Legacy preference file was modified");
    f.storage.Stop(); f.workspace.CloseAll();
    base.defaultLinkDelay = 99; Check(base.Save(legacy.u8string()), "Cannot change old preferences");
    f.Load(false);
    Check(f.storage.Settings().defaultLinkDelay == (user ? 29 : 17), "Old preferences replaced the saved JSON profile");
}
void Isolated() {
    Fixture f;
    Preferences old; old.defaultLinkDelay = 37;
    Check(old.Save((f.directory.root / "user" / "VirtoolsDevPreferences.ini").u8string()), "Cannot seed legacy preferences");
    auto paths = f.Paths(); paths.file.clear();
    Check(f.storage.Load(paths, f.error), f.error);
    Check(f.storage.Settings().defaultLinkDelay == Preferences{}.defaultLinkDelay, "Isolated startup imported legacy preferences");
    f.Restore(); Check(f.storage.Start({}, {}, f.error), f.error);
    auto next = f.storage.Settings(); next.defaultLinkDelay = 19;
    Check(f.storage.Save(next, f.error) && f.storage.Settings().defaultLinkDelay == 19, f.error);
    Check(!fs::exists(f.File()) && !fs::exists(f.directory.root / "user" / "NeMoDev.json") && !f.operations.writes,
          "Isolated preferences were written to the user directory");
}
void LoadFailure() {
    Fixture f; f.Seed(); f.Load();
    const auto before = Encoded(f.storage.Profile());
    f.storage.Settings().defaultLinkDelay = 73;
    Put(f.File(), "{broken profile");
    Check(!f.storage.Load(f.Paths(false), f.error) && !f.error.empty(), "Invalid profile did not fail startup");
    Check(Encoded(f.storage.Profile()) == before && f.storage.Settings().defaultLinkDelay == 73 &&
              Bytes(f.File()) == "{broken profile", "Failed startup replaced previous profile or preferences");
}
void LegacyLayout(bool invalid) {
    Fixture f;
    const std::string text = invalid ? "[Docking][Data]\nDockNode ID=0x1 Parent=0x2\nDockNode ID=0x2 Parent=0x1\n" :
                                      "[Window][LegacyProfileWindow]\nPos=21,34\nSize=600,400\nCollapsed=0\n";
    const auto path = f.directory.root / "user" / "VirtoolsDevInterface.ini";
    Put(path, text);
    f.Load(false);
    std::string diagnostic;
    Check(f.storage.RestoreWorkspace(f.defaults, diagnostic, f.error), f.error);
    Check(diagnostic.empty() != invalid && f.workspace.Role("main.level"), "Legacy layout failure did not keep default views");
    Check(f.storage.Start({}, {}, f.error), f.error);
    Check(Bytes(path) == text, "Legacy layout was modified");
    if (!invalid) Check(f.storage.Profile().Workspace()->layout.find("LegacyProfileWindow") != std::string::npos,
                        "Legacy layout did not migrate into the profile");
}
void PreferenceFailure(bool throwing) {
    Fixture f; f.Seed(); f.Start();
    const auto before = Bytes(f.File()), profile = Encoded(f.storage.Profile());
    const auto *address = &f.storage.Settings();
    const auto delay = address->defaultLinkDelay;
    auto next = *address; next.defaultLinkDelay = delay + 13;
    ImGui::GetIO().WantSaveIniSettings = true;
    f.operations.fail = !throwing; f.operations.throwing = throwing;
    Check(!f.storage.Save(next, f.error) && !f.error.empty(), "Failed preference publication succeeded");
    Check(f.storage.Settings().defaultLinkDelay == delay && &f.storage.Settings() == address &&
              Encoded(f.storage.Profile()) == profile && Bytes(f.File()) == before && ImGui::GetIO().WantSaveIniSettings,
          "Failed preference save lost live settings, saved JSON or pending layout");
    f.CleanTemporaries();
    f.operations.fail = f.operations.throwing = false;
    Check(f.storage.Save(next, f.error) && f.storage.Settings().defaultLinkDelay == delay + 13 &&
              &f.storage.Settings() == address && !ImGui::GetIO().WantSaveIniSettings, "Preference save could not retry");
    Check(Bytes(f.File()).find("1234567890123456789") != std::string::npos, "Saving removed unknown profile data");
}
void ModuleFailure(bool throwing) {
    Fixture f; f.Seed(); f.Start();
    const auto before = Bytes(f.File()), profile = Encoded(f.storage.Profile());
    const auto query = EditorClient(f.editor.Host(), 0).Query("profile.modules");
    uint64_t revision = 0;
    Check(f.editor.QueryRevision("profile.modules", revision) == EDITOR_OK, "Cannot read profile revision");
    f.operations.fail = !throwing; f.operations.throwing = throwing;
    ImGui::GetIO().WantSaveIniSettings = true;
    Check(f.SetModule(false).status == EDITOR_ERROR, "Module command ignored publication failure");
    const auto after = EditorClient(f.editor.Host(), 0).Query("profile.modules");
    uint64_t afterRevision = 0;
    Check(f.editor.QueryRevision("profile.modules", afterRevision) == EDITOR_OK, "Cannot reread profile revision");
    Check(Encoded(f.storage.Profile()) == profile && Bytes(f.File()) == before &&
              afterRevision == revision && after.fields == query.fields && ImGui::GetIO().WantSaveIniSettings,
          "Failed module save changed the profile or query revision");
    f.CleanTemporaries(); f.operations.fail = f.operations.throwing = false;
    Check(f.SetModule(false).status == EDITOR_OK && !f.storage.Profile().Modules().front().enabled,
          "Module configuration could not retry");
    Check(f.workspace.Role("main.level"), "Module configuration changed running views before restart");
}
void WorkspaceFailure(bool throwing) {
    Fixture f; f.Seed(); f.Start();
    const auto before = Bytes(f.File()), profile = Encoded(f.storage.Profile());
    const auto id = f.workspace.Role("main.level");
    Check(f.workspace.Focus(id), "Cannot focus original view");
    auto config = f.editor.Configuration(); config.providers.front().provider = "test.replacement";
    f.operations.fail = !throwing; f.operations.throwing = throwing;
    ImGui::GetIO().WantSaveIniSettings = true;
    Check(!f.workspace.Configure(config, f.error), "Provider replacement ignored publication failure");
    Check(f.workspace.Role("main.level") == id && f.editor.Scope(id).view == id &&
              f.editor.Configuration().providers.front().provider == "test.original" &&
              Encoded(f.storage.Profile()) == profile && Bytes(f.File()) == before && ImGui::GetIO().WantSaveIniSettings,
          "Failed workspace save lost the original view, provider or profile");
    Check(!f.old.destroyed && f.next.destroyed == 1 && f.next.views.empty(), "Failed replacement leaked candidate views");
    Check(f.workspace.Inspect(id).fields.front().text == "original topic", "Replacement discarded the original view settings");
    f.CleanTemporaries(); f.operations.fail = f.operations.throwing = false;
    Check(f.workspace.Configure(config, f.error) && f.workspace.Role("main.level") != id &&
              f.editor.Configuration().providers.front().provider == "test.replacement", "Provider replacement could not retry");
}
void PreparationFailure(bool throwing) {
    Fixture f; f.Seed(); f.Start();
    const auto before = Bytes(f.File()), profile = Encoded(f.storage.Profile());
    const auto writes = f.operations.writes;
    f.refusePreparation = !throwing; f.throwPreparation = throwing;
    ImGui::GetIO().WantSaveIniSettings = true;
    const auto result = EditorClient(f.editor.Host(), 0).Command("workspace.save");
    Check(result.status == EDITOR_ERROR && !result.message.empty() && f.operations.writes == writes &&
              Bytes(f.File()) == before && Encoded(f.storage.Profile()) == profile && ImGui::GetIO().WantSaveIniSettings,
          "Configuration preparation failure changed persisted state or lost pending work");
    f.refusePreparation = f.throwPreparation = false;
    Check(EditorClient(f.editor.Host(), 0).Command("workspace.save").status == EDITOR_OK &&
              !ImGui::GetIO().WantSaveIniSettings, "Configuration preparation could not retry");
}
void AutosaveRetry() {
    Fixture f; f.Start();
    const auto before = Bytes(f.File());
    const auto id = f.workspace.Role("main.level"); f.old.views.at(id)->topic = "changed topic";
    ImGui::GetIO().WantSaveIniSettings = true;
    WorkspaceState captured;
    Check(f.workspace.Save(captured, f.error) && ImGui::GetIO().WantSaveIniSettings, "Capturing layout cleared the pending save");
    f.operations.fail = true;
    Check(!f.storage.SaveWorkspaceIfNeeded(f.error) && !f.error.empty() && ImGui::GetIO().WantSaveIniSettings &&
              Bytes(f.File()) == before, "Autosave failure discarded pending work");
    const auto writes = f.operations.writes;
    Check(f.storage.SaveWorkspaceIfNeeded(f.error) && f.error.empty() && f.operations.writes == writes,
          "Failed autosave retried without a delay");
    f.operations.fail = false; GImGui->Time += 5.1;
    Check(f.storage.SaveWorkspaceIfNeeded(f.error) && !ImGui::GetIO().WantSaveIniSettings &&
              f.operations.writes == writes + 1 && Bytes(f.File()).find("changed topic") != std::string::npos,
          "Pending autosave did not recover after storage became writable");
}
void DelayedLayout() {
    Fixture f; f.Start();
    ImGui::GetIO().WantSaveIniSettings = false;
    GImGui->SettingsDirtyTimer = 3;
    WorkspaceState captured;
    Check(f.workspace.Save(captured, f.error) && GImGui->SettingsDirtyTimer == 3,
          "Capturing layout consumed the delayed autosave");
    const auto before = Bytes(f.File());
    f.operations.fail = true;
    Check(!f.storage.Save(f.error) && GImGui->SettingsDirtyTimer == 3 &&
              !ImGui::GetIO().WantSaveIniSettings && Bytes(f.File()) == before,
          "Failed profile save discarded the delayed layout save");
    f.operations.fail = false;
    Check(f.storage.Save(f.error) && GImGui->SettingsDirtyTimer == 0 && !ImGui::GetIO().WantSaveIniSettings,
          "Successful profile save left an unnecessary autosave scheduled");
}
void RelativePath() {
    Fixture f;
    struct WorkingDirectory {
        fs::path before = fs::current_path();
        ~WorkingDirectory() { std::error_code error; fs::current_path(before, error); }
    } working;
    fs::current_path(f.directory.root);
    auto paths = f.Paths(); paths.file = f.File().filename().u8string();
    Check(f.storage.Load(paths, f.error), f.error);
    fs::current_path(f.directory.root / "app");
    f.Restore(); Check(f.storage.Start({}, {}, f.error), f.error);
    Check(fs::is_regular_file(f.File()) && !fs::exists(fs::current_path() / f.File().filename()),
          "Runtime directory changes redirected the saved profile");
}
void SessionPreferences() {
    Fixture f; f.Start();
    const auto before = Bytes(f.File()), profile = Encoded(f.storage.Profile());
    // These values describe already completed file/resource operations. A
    // failed profile write must not undo their live session records.
    auto &settings = f.storage.Settings();
    settings.AddRecentFile(u8"/中文/opened.cmo");
    settings.dataResources = {u8"/中文/library.rsc"};
    settings.showPriorities = true;
    ImGui::GetIO().WantSaveIniSettings = true;
    f.operations.fail = true;
    Check(!f.storage.Save(f.error) && Bytes(f.File()) == before && Encoded(f.storage.Profile()) == profile &&
              settings.recentFiles.front() == u8"/中文/opened.cmo" && settings.dataResources.front() == u8"/中文/library.rsc" &&
              settings.showPriorities, "Failed profile save rolled back completed session operations");
    f.operations.fail = false;
    Check(f.storage.SaveWorkspaceIfNeeded(f.error) && !ImGui::GetIO().WantSaveIniSettings &&
              f.storage.Profile().Settings().recentFiles == settings.recentFiles &&
              f.storage.Profile().Settings().dataResources == settings.dataResources &&
              f.storage.Profile().Settings().showPriorities, "Retry did not retain current session preferences");
}
void BusySave() {
    Fixture f; f.Start();
    const auto before = f.storage.Profile().Workspace()->views.front().settings;
    const auto id = f.workspace.Role("main.level"); f.old.views.at(id)->topic = "busy view";
    f.editor.NativeAvailable(false); ImGui::GetIO().WantSaveIniSettings = true;
    auto next = f.storage.Settings(); next.defaultLinkDelay = 14;
    Check(f.storage.Save(next, f.error), f.error);
    Check(f.storage.Profile().Workspace()->views.front().settings == before && ImGui::GetIO().WantSaveIniSettings,
          "Saving preferences during a native task captured unavailable views or consumed pending layout");
    const auto writes = f.operations.writes;
    Check(f.storage.SaveWorkspaceIfNeeded(f.error) && f.operations.writes == writes, "Autosave entered an unavailable native task");
    f.editor.NativeAvailable(true);
    Check(f.storage.SaveWorkspaceIfNeeded(f.error) && !ImGui::GetIO().WantSaveIniSettings &&
              Bytes(f.File()).find("busy view") != std::string::npos, "Deferred workspace could not be saved");
}
void StartupFailure() {
    Fixture f; f.Seed(); f.Load(); f.Restore();
    const auto before = Bytes(f.File()), profile = Encoded(f.storage.Profile());
    f.operations.fail = true;
    Check(!f.storage.Start({}, {}, f.error) && Bytes(f.File()) == before && Encoded(f.storage.Profile()) == profile,
          "Failed startup save replaced the original profile");
    Check(EditorClient(f.editor.Host(), 0).Command("workspace.save").status == EDITOR_REFUSED,
          "Failed startup installed a configuration writer");
    f.operations.fail = false;
    Check(f.storage.Start({}, {}, f.error), f.error);
    f.storage.Stop();
    const auto writes = f.operations.writes;
    f.refusePreparation = true;
    Check(EditorClient(f.editor.Host(), 0).Command("workspace.save").status == EDITOR_REFUSED &&
              f.storage.Save(f.error) && f.operations.writes == writes, "Stopped configuration storage still writes");
}
void RestoreFailure() {
    Fixture f; f.Start();
    auto state = *f.storage.Profile().Workspace(); state.views.front().version = 999;
    auto broken = f.storage.Profile(); Check(broken.SetWorkspace(state, f.error), f.error);
    Put(f.File(), Encoded(broken));
    f.storage.Stop(); f.workspace.CloseAll(); f.Load();
    const auto before = Bytes(f.File());
    std::string diagnostic;
    Check(f.storage.RestoreWorkspace(f.defaults, diagnostic, f.error) && !diagnostic.empty() &&
              f.workspace.SavingBlocked() && f.workspace.Role("main.level"), "Invalid saved view did not retain recoverable state");
    Check(f.storage.Start({}, {}, f.error) && Bytes(f.File()) == before, "Startup overwrote an unrestored workspace");
    ImGui::GetIO().WantSaveIniSettings = true;
    auto next = f.storage.Settings(); next.defaultLinkDelay = 25;
    Check(!f.storage.Save(next, f.error) && Bytes(f.File()) == before, "Preferences overwrote an unrestored workspace");
    Check(f.storage.SaveWorkspaceIfNeeded(f.error) && ImGui::GetIO().WantSaveIniSettings && Bytes(f.File()) == before,
          "Blocked autosave consumed pending layout");
    Check(EditorClient(f.editor.Host(), 0).Command("workspace.discard-saved-state").status == EDITOR_OK,
          "Cannot discard invalid saved views");
    Check(f.storage.SaveWorkspaceIfNeeded(f.error) && !ImGui::GetIO().WantSaveIniSettings && Bytes(f.File()) != before,
          "Workspace could not recover after discarding unrestored state");
}
void Restart() {
    Fixture f; f.Seed(); f.Start();
    const auto id = f.workspace.Role("main.level"); f.old.views.at(id)->topic = "saved topic";
    Check(f.workspace.Focus(id), "Cannot save focused view");
    auto settings = f.storage.Settings(); settings.defaultLinkDelay = 64;
    Check(f.storage.Save(settings, f.error), f.error);
    const auto instance = f.storage.Profile().Workspace()->views.front().instance;
    f.storage.Stop(); f.workspace.CloseAll(); f.Load(); f.Restore();
    const auto restored = f.workspace.Role("main.level");
    Check(f.storage.Settings().defaultLinkDelay == 64 && f.workspace.Inspect(restored).fields.front().text == "saved topic",
          "Restart did not restore preferences and view settings");
    Check(f.storage.Start({}, {}, f.error) && f.storage.Profile().Workspace()->views.front().instance == instance,
          "Restart changed persistent view identity");
    Check(Bytes(f.File()).find("1234567890123456789") != std::string::npos, "Restart lost unknown profile data");
}
void Discovery(const std::string &module) {
    Fixture f; f.Load();
    const auto library = f.directory.root / fs::u8path(module).filename(); fs::copy_file(fs::u8path(module), library);
    Check(f.storage.Profile().SetModule({"test.external", false, library.u8string()}, f.error), f.error);
    Put(library, "disabled broken module");
    std::vector<EditorModuleSource> sources;
    Check(f.storage.DiscoverModules({library.u8string()}, {}, sources, f.error) && sources.empty(),
          "Discovery entered a disabled module");
    sources.push_back({"retained source", {}});
    Check(!f.storage.DiscoverModules({}, {(f.directory.root / "missing").u8string()}, sources, f.error) &&
              sources.size() == 1 && sources.front().name == "retained source", "Failed discovery changed assembled sources");
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc >= 2, "One profile storage test name is required");
        const std::string name = argv[1];
        if (name == "LegacyUser") LegacyPreferences(true);
        else if (name == "LegacyApplication") LegacyPreferences(false);
        else if (name == "Isolated") Isolated();
        else if (name == "LoadFailure") LoadFailure();
        else if (name == "LegacyLayout") LegacyLayout(false);
        else if (name == "LegacyLayoutInvalid") LegacyLayout(true);
        else if (name == "PreferenceRefusal") PreferenceFailure(false);
        else if (name == "PreferenceException") PreferenceFailure(true);
        else if (name == "ModuleRefusal") ModuleFailure(false);
        else if (name == "ModuleException") ModuleFailure(true);
        else if (name == "WorkspaceRefusal") WorkspaceFailure(false);
        else if (name == "WorkspaceException") WorkspaceFailure(true);
        else if (name == "PreparationRefusal") PreparationFailure(false);
        else if (name == "PreparationException") PreparationFailure(true);
        else if (name == "AutosaveRetry") AutosaveRetry();
        else if (name == "DelayedLayout") DelayedLayout();
        else if (name == "RelativePath") RelativePath();
        else if (name == "SessionPreferences") SessionPreferences();
        else if (name == "BusySave") BusySave();
        else if (name == "StartupFailure") StartupFailure();
        else if (name == "RestoreFailure") RestoreFailure();
        else if (name == "Restart") Restart();
        else if (name == "Discovery" && argc == 3) Discovery(argv[2]);
        else throw std::runtime_error("Unknown profile storage test: " + name);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

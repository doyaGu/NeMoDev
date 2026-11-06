#include "ui/app/EditorApp.h"
#include "application/EditorModules.h"
#include "ui/app/FileInputs.h"
#include "ui/app/OutputInputs.h"
#include "ui/app/EnvironmentInputs.h"
#include "ui/app/MessageDialogs.h"
#include "features/environment/EnvironmentViews.h"
#include "features/diagnostics/DiagnosticViews.h"
#include "features/preferences/PreferencesView.h"
#include "features/schematic/SchematicDiagnosticViews.h"
#include "core/document/OutputFiles.h"
#include "ui/panels/EventLogView.h"
#include <stdexcept>
#include "features/objects/ObjectViews.h"
#include "features/values/ValueEditors.h"
#include "features/arrays/ArrayTable.h"
#include "features/resources/BuildingBlockQueries.h"
#include "features/resources/BuildingBlockViews.h"
#include "features/resources/DataResourceViews.h"
#include "features/defaults/DefaultWorkspace.h"
#include "features/runtime/RuntimeViews.h"
#include "features/scene/SceneViewData.h"
#include "editor/ImGuiDrawing.h"
#include "core/level/SetupCommands.h"
#include "ui/schematic/Snapshots.h"


#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <iterator>
#include <fstream>

#include "imgui.h"
#include "imgui_internal.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "core/document/DocumentSaver.h"
#include "core/document/FileCommands.h"
#include "ui/controls/FileDialogs.h"
#include "ui/app/DesktopFiles.h"
#include "core/document/ObjectPictures.h"
#include "core/schematic/SchematicEdits.h"
#include "core/runtime/Resources.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/TopologyCommands.h"

namespace vdev {

static void ApplyColors(const Preferences &preferences, SchematicStyle &style) {
    style.behaviorLink = ColorRefToImU32(preferences.ioLinkColor);
    style.parameterLink = ColorRefToImU32(preferences.parameterLinkColor);
    style.highlightLink = ColorRefToImU32(preferences.exportedLinkColor);
    style.operationLink = ColorRefToImU32(preferences.operationLinkColor);
    style.validLink = ColorRefToImU32(preferences.validLinkColor);
    style.invalidLink = ColorRefToImU32(preferences.invalidLinkColor);
    style.obsoleteBorder = ColorRefToImU32(preferences.obsoleteColor);
    style.noPrototypeBorder = ColorRefToImU32(preferences.noPrototypeColor);
    style.missingBorder = ColorRefToImU32(preferences.noObjectColor);
    style.toReplaceBorder = ColorRefToImU32(preferences.toReplaceColor);
}

EditorApp::EditorApp()
    : m_Editor([this] { return m_Session.document.get(); },
               [this] { return m_Session.runtimeOperation.Available(); }),
      m_ObjectCommands([this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; }),
      m_Schematics(m_Workspace, m_Style), m_Panels(m_ParameterCopies, m_Workspace, m_Schematics) {
    m_Session.interactionActive = [this] { return m_Editor.HasEdits(); };
    m_Session.documentViews = this;
    m_Execution.changed = [this] { m_Schematics.RefreshValues(); };
    m_Editor.report = [this](const EditorResponse &result) {
        if (!result.message.empty() && result.status != EDITOR_NO_CHANGE)
            m_Session.Log(result.status == EDITOR_OK ? LogLevel::Info : LogLevel::Warning, result.message);
    };
    m_LayoutAuthoring.rememberBehavior = [this](uint64_t prototype) {
        RememberBlock(CKGUID(CKDWORD(prototype >> 32), CKDWORD(prototype)));
    };
    m_LayoutAuthoring.showBehavior = [this](ObjectRef script, ObjectRef block) {
        if (m_Session.HasDocument() && m_Session.document->Resolve(script) && m_Session.document->Resolve(block))
            ShowSchematic(ElementAddress::Capture(*m_Session.document, script.object,
                ElementRef{ElementKind::Behavior, script.object, block.object}), block);
    };
}

void EditorApp::HandleEvent(const SDL_Event &event) {
    if (m_Session.runtimeOperation.Busy()) return;
    m_Layouts.HandleEvent(event);
    if (!m_Window || event.type != SDL_EVENT_WINDOW_FOCUS_LOST || event.window.windowID != SDL_GetWindowID(m_Window) ||
        m_Layouts.FullScreen())
        return;
    m_Schematics.CancelInteraction();
    m_Layouts.CancelEdits();
    if (EditorClient::Integer(m_Editor.ReadQuery("runtime.state", {m_Editor.Scope()}), "state") == EDITOR_EXECUTION_RUNNING)
        Command("runtime.pause");
}


bool EditorApp::Initialize(SDL_Window * window, const std::string &runtimeDirectory, const std::string &fileToOpen, const std::vector<std::string> &modules, const std::vector<std::string> &moduleDirectories, const std::string &profilePath
#ifdef VIRTOOLSDEV_UI_TESTS
                           , const StartupOptions &startup
#endif
                           ) {
    m_Window = window;
    m_InitializationError.clear();
    m_ProfileReady = false;
    const auto fail = [this](const std::string &message) {
        m_InitializationError = message;
        m_Session.Log(LogLevel::Error, message);
        return false;
    };
#ifdef VIRTOOLSDEV_UI_TESTS
    m_Startup = startup;
    const bool isolated = startup.isolated;
#else
    const bool isolated = false;
#endif
    // The Event Log's first line (Dev.exe sub_4229D0, language pack 141).
    m_Session.Log(LogLevel::Info, "Virtools Started.");
    m_Session.eventLog.HideStatus();
    // CK text retains its native bytes; file paths remain UTF-8.
    SetLossyTextHandler([this](const std::string &text) {
        m_Session.RuntimeLog(LogLevel::Warning, "Text conversion failed; original CK bytes kept: " + text);
    });
    // Preferences: our file, else Dev's own settings (read-only), else Dev's
    // defaults (isolated: the defaults).
    Preferences &preferences = m_Settings;
    std::string error;
    m_ProfileFile = profilePath;
    if (m_ProfileFile.empty() && !isolated) {
        if (char *directory = SDL_GetPrefPath("Virtools", "VirtoolsDev")) {
            m_ProfileFile = (std::filesystem::u8path(directory) / "NeMoDev.json").u8string(); SDL_free(directory);
        } else return fail("Cannot locate the user profile directory");
    }
    auto profileStatus = EditorProfile::ReadResult::Missing;
    if (!m_ProfileFile.empty()) profileStatus = m_Profile.Load(m_ProfileFile, error);
    if (profileStatus == EditorProfile::ReadResult::Failed) return fail(m_ProfileFile + ": " + error);
    if (profileStatus == EditorProfile::ReadResult::Loaded) preferences = m_Profile.Settings();
    else if (!isolated) {
        const bool loaded = preferences.Load(PreferencesPath());
        if (!loaded) {
            const int imported = preferences.ImportFromDev() + preferences.ImportDevIni(runtimeDirectory);
            if (imported) m_Session.Log(LogLevel::Info, "Imported " + std::to_string(imported) + " Virtools Dev preferences");
        } else if (!preferences.settingsSaved) {
            preferences.ImportDevSettings(); preferences.ImportDevIni(runtimeDirectory);
        }
    }
    ApplyPreferences();
#ifdef VIRTOOLSDEV_UI_TESTS
    m_Schematics.Defaults().showAllScripts = startup.showAllScripts;
#endif
    m_Session.host.SetRenderEngine(preferences.Text("CK2 Settings\\Render Engine"));
    const bool started = m_Session.host.Start(runtimeDirectory, [this](LogLevel level, const std::string &message) {
        m_Session.RuntimeLog(level, message);
    }, &error, window);
    if (!started) return fail("Cannot start the Virtools runtime:\n" + error);
    m_Catalog.Refresh();
    m_Environment.ConfigureLinks([this] {
        const auto &p = m_Settings;
        return EnvironmentCommands::LinkOptions{p.defaultLinkDelay, p.autoLoopLinkDelay, p.Int("BehaviorManager\\MaxIteration")};
    }, [this](const EnvironmentCommands::LinkOptions &options, std::string &error) {
        Preferences next = m_Settings;
        next.defaultLinkDelay = options.delay; next.autoLoopLinkDelay = options.preventLoop;
        next.SetInt("BehaviorManager\\MaxIteration", options.iterations);
        return SaveSettings(std::move(next), error);
    });
    std::vector<EditorModuleDirectory> directories;
    std::vector<EditorModuleSource> external;
    std::vector<EditorModuleSetting> selectedModules;
    try {
        if (!isolated) {
            if (const char *base = SDL_GetBasePath()) directories.push_back({EditorModuleDirectoryAt(base)});
            if (char *preferences = SDL_GetPrefPath("Virtools", "VirtoolsDev")) {
                directories.push_back({(std::filesystem::u8path(preferences) / "modules").u8string()});
                SDL_free(preferences);
            }
        }
        for (const auto &directory : m_Profile.Directories()) directories.push_back({directory, true});
        for (const auto &directory : moduleDirectories) directories.push_back({directory, true});
        auto files = m_Profile.Files(); files.insert(files.end(), modules.begin(), modules.end());
        selectedModules = m_Profile.ResolvedModules();
        files.erase(std::remove_if(files.begin(), files.end(), [&](const auto &file) {
            const auto path = std::filesystem::weakly_canonical(std::filesystem::u8path(file)).u8string();
            return std::any_of(selectedModules.begin(), selectedModules.end(), [&](const auto &module) {
                return !module.enabled && !module.path.empty() && module.path == path;
            });
        }), files.end());
        if (!DiscoverEditorModules(directories, files, external, error)) {
            return fail(error);
        }
    } catch (const std::exception &failure) {
        return fail(std::string("Editor module paths: ") + failure.what());
    }
    std::vector<EditorModuleSource> sources{
        {"ProfileCommands.Register",
         [&](EditorApplication &, std::string &error) { return m_ProfileCommands.Register(error); }},
        {"RegisterBuildingBlockQueries",
         [&](EditorApplication &editor, std::string &error) {
             return RegisterBuildingBlockQueries(editor, m_Catalog, error);
         }},
        {"RegisterBuildingBlockViews",
         [&](EditorApplication &editor, std::string &error) { return RegisterBuildingBlockViews(editor, error); }},
        {"ResourceCommands.Register",
         [&](EditorApplication &, std::string &error) { return m_ResourceCommands.Register(error); }},
        {"RegisterDataResourceViews",
         [&](EditorApplication &editor, std::string &error) { return RegisterDataResourceViews(editor, error); }},
        {"RegisterResourceInputs",
         [&](EditorApplication &editor, std::string &error) { return RegisterResourceInputs(editor, window, error); }},
        {"RegisterDefaultWorkspace",
         [&](EditorApplication &editor, std::string &error) { return RegisterDefaultWorkspace(editor, error); }},
        {"RegisterDefaultAppearance",
         [](EditorApplication &editor, std::string &error) { return RegisterDefaultAppearance(editor, error); }},
        {"Workspace.Register", [&](EditorApplication &, std::string &error) { return m_Workspace.Register(error); }},
        {"Session.eventLog.Register",
         [&](EditorApplication &editor, std::string &error) { return m_Session.eventLog.Register(editor, error); }},
        {"StatusBar.Register",
         [&](EditorApplication &editor, std::string &error) { return m_StatusBar.Register(editor, error); }},
        {"RegisterEventLogView",
         [&](EditorApplication &editor, std::string &error) { return RegisterEventLogView(editor, error); }},
        {"Messages.Register",
         [&](EditorApplication &editor, std::string &error) { return m_Messages.Register(editor, error); }},
        {"RegisterMessageDialogs",
         [&](EditorApplication &editor, std::string &error) { return RegisterMessageDialogs(editor, error); }},
        {"RegisterHelpViews",
         [&](EditorApplication &editor, std::string &error) { return RegisterHelpViews(editor, &m_HtmlFonts, error); }},
        {"FileCommands.Register",
         [&](EditorApplication &, std::string &error) { return m_FileCommands.Register(error); }},
        {"RegisterFileInputs",
         [&](EditorApplication &editor, std::string &error) { return RegisterFileInputs(editor, window, error); }},
        {"ObjectCommands.Register",
         [&](EditorApplication &editor, std::string &error) { return m_ObjectCommands.Register(editor, error); }},
        {"SelectionCommands.Register",
         [&](EditorApplication &editor, std::string &error) { return m_SelectionCommands.Register(editor, error); }},
        {"Execution.Register", [&](EditorApplication &, std::string &error) { return m_Execution.Register(error); }},
        {"RegisterRuntimeViews",
         [&](EditorApplication &editor, std::string &error) { return RegisterRuntimeViews(editor, error); }},
        {"Environment.Register",
         [&](EditorApplication &, std::string &error) { return m_Environment.Register(error); }},
        {"RegisterEnvironmentViews",
         [&](EditorApplication &editor, std::string &error) { return RegisterEnvironmentViews(editor, error); }},
        {"RegisterEnvironmentInputs",
         [&](EditorApplication &editor, std::string &error) {
             return RegisterEnvironmentInputs(editor, window, error);
         }},
        {"Diagnostics.Register",
         [&](EditorApplication &, std::string &error) { return m_Diagnostics.Register(error); }},
        {"RegisterDiagnosticViews",
         [&](EditorApplication &editor, std::string &error) { return RegisterDiagnosticViews(editor, error); }},
        {"Preferences.Register",
         [&](EditorApplication &, std::string &error) { return m_Preferences.Register(error); }},
        {"RegisterPreferencesView",
         [&](EditorApplication &editor, std::string &error) { return RegisterPreferencesView(editor, error); }},
        {"Outputs.Register", [&](EditorApplication &, std::string &error) { return m_Outputs.Register(error); }},
        {"RegisterOutputInputs",
         [&](EditorApplication &editor, std::string &error) { return RegisterOutputInputs(editor, window, error); }},
        {"ParameterCommands.Register",
         [&](EditorApplication &editor, std::string &error) { return m_ParameterCommands.Register(editor, error); }},
        {"RegisterValueEditors",
         [&](EditorApplication &editor, std::string &error) { return RegisterValueEditors(editor, error); }},
        {"ArrayCommands.Register",
         [&](EditorApplication &editor, std::string &error) { return m_ArrayCommands.Register(editor, error); }},
        {"SetupCommands.Register",
         [&](EditorApplication &editor, std::string &error) { return m_SetupCommands.Register(editor, error); }},
        {"Setups.Register", [&](EditorApplication &, std::string &error) { return m_Setups.Register(error); }},
        {"SchematicCommands.Register",
         [&](EditorApplication &editor, std::string &error) { return m_SchematicCommands.Register(editor, error); }},
        {"SchematicGraph.Register",
         [&](EditorApplication &editor, std::string &error) { return m_SchematicGraph.Register(editor, error); }},
        {"RegisterSchematicDiagnosticViews",
         [&](EditorApplication &editor,
             std::string &error) { return RegisterSchematicDiagnosticViews(editor, error); }},
        {"RegisterArrayValueEditors",
         [&](EditorApplication &editor, std::string &error) { return RegisterArrayValueEditors(editor, error); }},
        {"RegisterObjectViews",
         [&](EditorApplication &editor, std::string &error) { return RegisterObjectViews(editor, error); }},
        {"SceneCommands.Register",
         [&](EditorApplication &, std::string &error) { return m_SceneCommands.Register(error); }},
        {"LayoutCommands.Register",
         [&](EditorApplication &, std::string &error) { return m_LayoutCommands.Register(error); }},
        {"LayoutAuthoring.Register",
         [&](EditorApplication &, std::string &error) { return m_LayoutAuthoring.Register(error); }},
        {"LayoutPresentation.Register",
         [&](EditorApplication &, std::string &error) { return m_LayoutPresentation.Register(error); }},
        {"Layout.Register", [&](EditorApplication &, std::string &error) { return m_Layouts.Register(error, window); }},
        {"Snapshots.Register", [&](EditorApplication &, std::string &error) { return m_Snapshots.Register(error); }},
        {"RegisterSnapshotInputs",
         [&](EditorApplication &editor, std::string &error) { return RegisterSnapshotInputs(editor, window, error); }},
        {"RegisterSceneViews",
         [&](EditorApplication &editor, std::string &error) { return RegisterSceneViews(editor, error); }},
        {"Schematics.Register",
         [&](EditorApplication &editor, std::string &error) { return m_Schematics.Register(editor, error); }},
        {"Panels.Register",
         [&](EditorApplication &editor, std::string &error) { return m_Panels.Register(editor, error); }},
        {"RegisterParameterDialogs",
         [&](EditorApplication &editor,
             std::string &error) { return RegisterParameterDialogs(editor, m_ParameterCopies, error); }},
        {"RegisterSceneNavigation",
         [&](EditorApplication &, std::string &error) { return RegisterSceneNavigation(error); }}};
    sources.insert(sources.end(), std::make_move_iterator(external.begin()), std::make_move_iterator(external.end()));
    auto configuration = DefaultWorkspaceConfiguration();
    const auto appearance = DefaultAppearanceConfiguration();
    configuration.contributions.providers.insert(configuration.contributions.providers.end(),
                                                 appearance.providers.begin(), appearance.providers.end());
    if (!m_Editor.Assemble(sources, EDITOR_IMGUI_VERSION, error, configuration.contributions, selectedModules, m_Profile.Contributions())) {
        return fail(error);
    }
    if (!PrepareUiFrame(ImGui::GetStyle().FontScaleDpi, error)) return fail(error);
    configuration = AvailableWorkspaceConfiguration(std::move(configuration), m_Editor);
    m_Session.document = std::make_unique<Document>(m_Session.host, m_Editor.DataTypes());
    m_Editor.Attach({}, 0, true, false);
    // Dev's paths (HKCU\Software\Virtools\Data Paths, read-only) until the
    // editor has its own.
    if (!isolated && !preferences.pathsSaved && preferences.ImportDevPaths())
        m_Session.Log(LogLevel::Info, "Path Manager: Virtools Dev's paths");
    if (!m_Editor.StartModules(error)) return fail(error);
    // The 3D Layout's render context (Dev makes it with its main window).
    m_Layouts.createObject = [this](EditorView source, LayoutObject kind) { CreateObject(kind, source); };

    const auto openResource = [&](const std::string &resource) {
        const auto result = Command("resource.open-library", resource);
#ifdef VIRTOOLSDEV_UI_TESTS
        if (!m_Startup.resourceFolder.empty()) m_StartupResourceOperation = result.operation;
#endif
    };
    for (const std::string &resource : std::vector<std::string>(preferences.dataResources)) openResource(resource);
#ifdef VIRTOOLSDEV_UI_TESTS
    for (const std::string &resource : m_Startup.resources) openResource(resource);
    if (m_Startup.newComposition) NewComposition(false);
#endif
    // Dev starts on a new composition (its 3D Layout shows the new scene's
    // gray), saying nothing on the status bar.
    if (!m_Session.HasDocument()) {
        NewComposition(false);
        m_Session.eventLog.HideStatus();
    }
    bool workspaceRestored = false;
    if (const auto *saved = m_Profile.Workspace()) {
        auto next = *saved;
        next.configuration.contributions = m_Editor.Configuration();
        workspaceRestored = m_Workspace.Restore(next, error);
        if (!workspaceRestored)
            m_Session.Log(LogLevel::Error, "Workspace: " + error);
    }
    if (!workspaceRestored) {
        if (!m_Workspace.Configure(configuration, error))
            return fail(error);
        OpenSceneView("level");
        if (!isolated && !m_Profile.Workspace()) {
            std::filesystem::path legacy;
            if (char *directory = SDL_GetPrefPath("Virtools", "VirtoolsDev")) {
                legacy = std::filesystem::u8path(directory) / "VirtoolsDevInterface.ini";
                SDL_free(directory);
            }
            std::error_code failure;
            if (!std::filesystem::exists(legacy, failure))
                legacy = std::filesystem::u8path(SDL_GetBasePath()) / "VirtoolsDevInterface.ini";
            if (std::filesystem::is_regular_file(legacy, failure) &&
                std::filesystem::file_size(legacy, failure) <= 1024 * 1024) {
                std::ifstream input(legacy, std::ios::binary);
                std::string text{std::istreambuf_iterator<char>(input), {}};
                if (input && !m_Workspace.LoadLayout(text, error))
                    m_Session.Log(LogLevel::Warning, "Workspace: " + error);
            }
        }
    }
#ifdef VIRTOOLSDEV_UI_TESTS
    if (fileToOpen.empty()) RunStartupActions(false);
    else { m_RunStartupAfterOpen = true; OpenFile(fileToOpen); }
#else
    if (!fileToOpen.empty()) OpenFile(fileToOpen);
#endif
    m_ResourceCommands.changed = [this] {
        m_Settings.dataResources = m_ResourceCommands.OpenLibraries();
        SavePreferences();
    };
    UpdateTitle();
    auto restored = m_Profile;
    if (!restored.AddSources(modules, moduleDirectories, error) ||
        (!m_Workspace.SavingBlocked() && !WriteProfile(restored, m_Settings, m_Editor.Configuration(), error)))
        return fail(m_ProfileFile + ": " + error);
    m_Profile = std::move(restored); m_ProfileReady = true;
    m_Workspace.ConfigurationWriter([this](const auto &configuration, const auto &workspace, auto &error) {
        return SaveConfiguration(configuration, error, &workspace);
    });
    return true;
}

std::string EditorApp::PreferencesPath() const {
    std::filesystem::path legacy;
    if (char *directory = SDL_GetPrefPath("Virtools", "VirtoolsDev")) {
        legacy = std::filesystem::u8path(directory) / "VirtoolsDevPreferences.ini"; SDL_free(directory);
    }
    std::error_code error;
    if (!legacy.empty() && std::filesystem::exists(legacy, error)) return legacy.u8string();
    if (const char *base = SDL_GetBasePath())
        for (const char *name : {"VirtoolsDevPreferences.ini", "SchematicEditorPreferences.ini"}) {
            const auto file = std::filesystem::u8path(base) / name;
            if (std::filesystem::exists(file, error)) return file.u8string();
        }
    return legacy.u8string();
}
void EditorApp::SaveWorkspaceIfNeeded() {
    auto &io = ImGui::GetIO();
    if (!io.WantSaveIniSettings || !m_ProfileReady || m_ProfileFile.empty() || m_Session.runtimeOperation.Busy() ||
        !m_Editor.NativeAvailable() || m_Editor.HasEdits() || m_Workspace.Interacting())
        return;
    if (!m_Workspace.SavingBlocked()) {
        const auto result = EditorClient(m_Editor.Host(), 0).Command("workspace.save");
        if (result.status != EDITOR_OK && !result.message.empty())
            m_Session.Log(LogLevel::Error, result.message);
    }
    io.WantSaveIniSettings = false;
}
void EditorApp::SavePreferences() {
    if (!m_ProfileReady) return;
    std::string error;
    if (!SaveConfiguration(m_Editor.Configuration(), error)) m_Session.Log(LogLevel::Error, error);
}
bool EditorApp::SaveSearchPaths(const PathCategories &paths, std::string &error) {
    Preferences next = m_Settings;
    next.paths = paths; next.pathsSaved = true;
    return SaveSettings(std::move(next), error);
}
bool EditorApp::SaveSettings(Preferences next, std::string &error) {
    if (!WriteSettings(next, error)) return false;
    m_Settings = std::move(next); return true;
}
bool EditorApp::WriteSettings(const Preferences &next, std::string &error) {
    if (!m_ProfileReady) return true;
    auto profile = m_Profile;
    if (!WriteProfile(profile, next, m_Editor.Configuration(), error)) return false;
    m_Profile = std::move(profile); return true;
}
bool EditorApp::WriteProfile(EditorProfile &profile, const Preferences &settings,
                             const EditorConfiguration &configuration, std::string &error,
                             const WorkspaceState *saved) {
    WorkspaceState workspace;
    if (!saved) {
        if (!m_Editor.NativeAvailable() && !m_Workspace.SavingBlocked()) {
            saved = profile.Workspace();
        } else {
            if (!m_Workspace.Save(workspace, error))
                return false;
            saved = &workspace;
        }
    }
    if (saved && !profile.SetWorkspace(*saved, error))
        return false;
    if (!profile.Capture(settings, configuration, m_Editor.DefaultConfiguration(),
                         [this](const auto &choice) { return m_Editor.ProviderModule(choice); }, error)) return false;
    if (m_ProfileFile.empty()) return true;
    std::string text;
    if (!profile.Encode(text, error)) return false;
    SaveReport report; OutputFiles files(report);
    const auto staged = files.Add(m_ProfileFile);
    std::vector<std::string> written;
    if (!staged.empty()) {
        std::ofstream output(std::filesystem::u8path(staged), std::ios::binary);
        output.write(text.data(), std::streamsize(text.size())); output.close();
        if (output && files.Publish(written)) return true;
    }
    error = "Profile could not be saved: " + m_ProfileFile;
    for (const auto &message : report.errors) error += "\n" + message;
    return false;
}
bool EditorApp::SaveConfiguration(const EditorConfiguration &configuration, std::string &error,
                                  const WorkspaceState *workspace) {
    if (!m_Appearance.Prepare(m_Editor, error)) return false;
    if (!m_ProfileReady) return true;
    auto next = m_Profile;
    if (!WriteProfile(next, m_Settings, configuration, error, workspace))
        return false;
    m_Profile = std::move(next); return true;
}

void EditorApp::ApplyPreferences() {
    const Preferences &p = m_Settings;
    DocumentCommands::Settings files;
    files.open.environment = m_Preferences.NewSceneEnvironment();
    files.open.testLinkTypes = p.Bool("Schematic\\Test Parameter Link after Loading");
    files.open.removeInvalidLinks = p.Bool("Schematic\\Remove Invalid Link after Loading");
    files.saveScriptSnapshots = p.Bool("Schematic\\Save Script Snapshot");
    m_FileCommands.Configure(files);
    m_Outputs.SaveScriptSnapshots(files.saveScriptSnapshots);
    ViewOptions view = m_Schematics.Defaults();
    view.autoLink = p.automaticLink;
    view.showControlPoints = p.showControlPoints;
    view.showLinkInfo = p.showLinkDelay;
    view.showPriorities = p.showPriorities;
    view.showHeaders = p.showScriptLeftPart;
    m_Schematics.ApplyOptions(view);
    ApplyColors(p, m_Style);
}

// The General Preferences values the CK context takes: Dev applies them
// with OK (Dev.exe sub_5AF2C0) and at start.
void EditorApp::ImportFile(Document::ImportMode mode, const std::string &path) {
    static const char *ids[] = {"document.import", "document.import-character", "document.import-place", "document.import-scene"};
    Command(ids[static_cast<unsigned>(mode)], path);
}

// Dev keeps its default windows (3D Layout, Building Blocks, Level Manager,
// Schematic, Data Resources).
void EditorApp::CloseSetupsAndManagers() {
    m_Setups.CloseAll();
    CloseSceneView("attributes");
    CloseSceneView("hierarchy");
    Command("runtime.close-tools");
    Command("environment.close-managers");
    Command("diagnostics.close-tools");
    Command("preferences.close");
}

bool EditorApp::PrepareUiFrame(float contentScale, std::string &error) {
    const auto revision = m_Appearance.Revision();
    if (!m_Appearance.Apply(m_Editor, contentScale, error)) return false;
    if (revision != m_Appearance.Revision()) {
        m_Style = CurrentDevTheme().schematic;
        ApplyColors(m_Settings, m_Style);
    }
    m_HtmlFonts = m_Appearance.Fonts();
    return true;
}

EditorApp::~EditorApp() { Shutdown(); }

void EditorApp::Shutdown() {
    if (m_Shutdown) return;
    m_Shutdown = true;
    m_Session.FinishRuntimeOperations();
    m_Editor.NativeAvailable(true);
    m_Session.DrainRuntimeLog();
    m_Session.fileDialogs.Close();
    // Toolbar toggles are preferences too.
    Preferences &p = m_Settings;
    const auto &view = m_Schematics.CurrentId() ? m_Schematics.Current().Navigation().options : m_Schematics.Defaults();
    p.automaticLink = view.autoLink;
    p.showControlPoints = view.showControlPoints;
    p.showLinkDelay = view.showLinkInfo;
    p.showPriorities = view.showPriorities;
    p.showScriptLeftPart = view.showHeaders;
    SavePreferences();
    m_Editor.StopModules();
    m_Workspace.ConfigurationWriter({});
    m_ProfileReady = false;
    if (m_Session.document) CloseDocumentViews();
    m_Session.documentViews = nullptr;
    m_Workspace.CloseAll();
    m_Appearance.Reset();
    // Entry-owned module data is released while the document, runtime and all
    // module libraries are still alive. Window data has already been released.
    m_Editor.NativeAvailable(true);
    m_Editor.Stop();
    if (m_Session.document) {
        m_Session.files.Close({});
        m_Session.FinishRuntimeOperations();
    }
    m_Session.document.reset();
    m_Session.host.Stop();
    // The textures every window draws with, before the device goes (Main.cpp).
    ReleaseDevBitmaps();
    ReleaseSnapshotTextures();
    ReleaseRetiredTextures();
    SetLossyTextHandler(nullptr);
}

bool EditorApp::OpenOutput(const std::string &path, std::string &error) {
    return OpenDesktopFile(path, &error);
}
void EditorApp::OutputFinished(const OutputCommands::Result &result) {
    if (!result.message.empty()) m_Session.Log(result.status == EDITOR_ERROR ? LogLevel::Error : LogLevel::Info, result.message);
    if (!result.print && result.status == EDITOR_OK) m_Session.Log(LogLevel::Info, "Web page written: " + result.path);
    if (!result.print && result.webFailure == DocumentFiles::WebResult::Failure::Composition && !m_Shutdown) {
        EditorClient(m_Editor.Host(), 0).Command("message.show", {TextArgument("text", "Unable to save CMO file !")});
    }
}

void EditorApp::RequestQuit() { Command("application.quit"); }





void EditorApp::FileFinished(const DocumentCommands::Result &result) {
    if (m_Shutdown) return;
    m_Editor.Attach(m_Session.HasDocument() ? m_Session.document->Token() : DocumentToken{},
                    m_Session.document ? m_Session.document->TopologyRevision() : 0, true, m_Session.HasDocument());
    m_Schematics.Validate();
    using A = DocumentCommands::Action;
    if (result.action == A::New && result.status == EDITOR_OK) {
        std::string error;
        if (!m_Layouts.Start(m_Window, &error))
            m_Session.Log(LogLevel::Warning, "3D Layout: " + error);
    }
    if (!result.message.empty())
        m_Session.Log(result.status == EDITOR_ERROR ? LogLevel::Error : LogLevel::Info, result.message);
    if ((result.action == A::Open || result.action == A::Revert) && result.opened.opened)
        CompleteOpen(result.path, result.opened);
    if (result.loaded.loaded) {
        m_Schematics.ClearSelections();
        m_Schematics.Invalidate();
        if (result.action == A::Merge || result.action == A::MergeScene) RememberRecentFile(result.path);
        for (const auto &script : m_Session.document->Scripts())
            if (std::find(result.loaded.objects.begin(), result.loaded.objects.end(), script.id) != result.loaded.objects.end()) {
                m_Schematics.Show(
                    ElementAddress::Capture(*m_Session.document, script.id, Selection::HeaderOf(script.id)), {}, false);
                break;
            }
    }
    for (const auto &item : result.resources.items) {
        m_Session.Log(item.loaded ? LogLevel::Info : LogLevel::Warning,
                    std::string(item.loaded ? "Loaded: " : "Load failed: ") + item.message + " (" + item.path + ")");
        if (item.loaded) {
            m_Schematics.Invalidate();
            if (result.action == A::LoadScripts && item.created && m_Session.document->Resolve(item.created))
                m_Schematics.Show(ElementAddress::Capture(*m_Session.document, item.created.object,
                                                         Selection::HeaderOf(item.created.object)));
        }
    }
    if ((result.action == A::ExportObjects || result.action == A::ExportBehaviors) && result.exported.saved)
        m_Session.Log(LogLevel::Info, "Saved " + result.path);
    if (result.status != EDITOR_CANCELLED && (result.saved.report.saved || !result.saved.report.errors.empty() ||
        result.action == A::Save || result.action == A::SaveAs || result.action == A::SaveVersion || result.action == A::Player)) {
        const auto &report = result.saved.report;
        const auto path = result.written.empty() ? result.path : result.written.front();
        std::string text = (result.saved.saved ? "Saved " : "Save failed: ") + path;
        text += "\nInterfaceChunks rewritten: " + std::to_string(report.chunksWritten) +
                "\nScripts verified in a new context: " + std::to_string(report.scriptsVerified) +
                "\nParameters verified: " + std::to_string(report.parametersVerified);
        for (const auto &error : report.errors) text += "\nError: " + AnsiToUtf8(error);
        for (const auto &note : report.notes) text += "\nNote: " + AnsiToUtf8(note);
        m_Session.Log(result.saved.saved ? LogLevel::Info : LogLevel::Error, text);
        if (result.action == A::Player) {
            if (!result.saved.saved) {
                EditorClient(m_Editor.Host(), 0).Command("message.show", {TextArgument("text",
                    "Save Level Error: " + (report.errors.empty() ? std::string() : AnsiToUtf8(report.errors.front())))});
            }
        } else EditorClient(m_Editor.Host(), 0).Command("message.show", {
            TextArgument("text", text), TextArgument("title", "Save report"), TextArgument("format", "report")});
        if (report.documentSaved) RememberRecentFile(path);
    }
#ifdef VIRTOOLSDEV_UI_TESTS
    if (m_RunStartupAfterOpen && result.action == A::Open) {
        m_RunStartupAfterOpen = false;
        RunStartupActions(result.opened.opened);
    }
#endif
    UpdateTitle();
}

bool EditorApp::OpenFile(const std::string &path) {
    return Command("document.open", path).status == EDITOR_QUEUED;
}

void EditorApp::CompleteOpen(const std::string &path, const DocumentFiles::OpenResult &result) {
    std::string layoutError;
    if (!m_Layouts.Start(m_Window, &layoutError))
        m_Session.Log(LogLevel::Warning, "3D Layout: " + AnsiToUtf8(layoutError));
    RememberRecentFile(path);
    // NMO opens are imports into a new composition. Keep its editor state,
    // just as Import File does, rather than restoring the library's layout.
    if (m_Session.document->CreatedFrom() == Document::Origin::LoadedComposition) {
        m_Layouts.Primary().ApplyLayoutData(result.layout, m_LayoutCommands.ProjectInterface(result.layout));
        ReopenSetups(result.layout);
        m_SchematicGraph.LoadedLinks(result.invalid);
        if (result.removed) m_Session.Log(LogLevel::Warning, std::to_string(result.removed) + " invalid link(s) removed after loading");
        if (!result.invalid.empty())
            m_Session.Log(LogLevel::Warning, std::to_string(result.invalid.size()) + " invalid link(s): see the Links with Errors Explorer");
    }
    // Dev's Event Log line (language pack [File IO]).
    m_Session.Log(LogLevel::Info, "File Loaded Successfully. (" + path + ")");
    // Dev refuses a file for the player ("This content can only be opened in
    // Players"); here its Scripts' Schematic is rebuilt from their graphs.
    if (IsPlayerFile(path))
        m_Session.Log(LogLevel::Warning, "A file for the player: the Scripts' Schematic is rebuilt from their graphs "
                                       "(no comments, colors or saved layout)");
    if (!m_Session.document->Scripts().empty()) {
        const auto script = m_Session.document->Scripts().front().id;
        m_Schematics.Show(ElementAddress::Capture(*m_Session.document, script, Selection::HeaderOf(script)), {}, false);
    }
    UpdateTitle();
}

void EditorApp::NewComposition(bool background) {
    Command("document.new");
    if (!background) {
        m_Session.FinishRuntimeOperations();
        m_Editor.Attach(m_Session.HasDocument() ? m_Session.document->Token() : DocumentToken{},
                        m_Session.document ? m_Session.document->TopologyRevision() : 0, true, m_Session.HasDocument());
    }
}

// Dev (sub_5C54C0) asks for the file, merges it and keeps the composition's
// undo history; the merge itself is not an undo step.
void EditorApp::MergeComposition(bool asScene, const std::string &path) {
    Command(asScene ? "document.merge-scene" : "document.merge", path);
}

void EditorApp::RememberRecentFile(const std::string &path) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(std::filesystem::u8path(path), ec);
    m_Settings.AddRecentFile(ec ? path : absolute.u8string());
    SavePreferences();
}

void EditorApp::CloseDocumentViews() {
    m_LayoutAuthoring.DocumentClosing();
    m_Workspace.DocumentChanging();
    m_Editor.NativeAvailable(false);
    m_Outputs.Stop();
    m_Panels.DocumentClosed();
    m_Layouts.DocumentClosed(); // its working copies while they still exist
    m_Layouts.Primary().ApplyLayoutData(LayoutInterfaceData(), {});
    m_Schematics.DocumentClosed();
    ReleaseSnapshotTextures(); // the document frees its bitmaps
    UpdateTitle();
}

void EditorApp::DetachDocument(bool replaceContext) {
    if (DocumentInteractionActive()) throw std::runtime_error("Finish the current document interaction");
    m_DetachedLayout = LayoutDataToSave();
    m_DetachedDocument = m_Session.document->Token();
    CloseDocumentViews();
    if (replaceContext)
        m_Layouts.Stop();
}

void EditorApp::RestoreDocument() {
    if (!m_DetachedDocument.value || m_Session.document->Token() != m_DetachedDocument) return;
    m_Editor.Attach(m_Session.HasDocument() ? m_Session.document->Token() : DocumentToken{},
                    m_Session.document->TopologyRevision(), true, m_Session.HasDocument());
    std::string error;
    if (!m_Layouts.Start(m_Window, &error))
        throw std::runtime_error("3D Layout: " + error);
    m_Layouts.Primary().ApplyLayoutData(m_DetachedLayout, m_LayoutCommands.ProjectInterface(m_DetachedLayout), false);
    ReopenSetups(m_DetachedLayout);
    m_Schematics.Invalidate();
    UpdateTitle();
    m_DetachedDocument = {};
}

bool EditorApp::DocumentInteractionActive() const {
    return m_Schematics.Busy() || m_Layouts.Busy() || m_Workspace.Interacting();
}



// The Setups of the 3D Layout's data open again (Dev.exe sub_4B46C0 ->
// sub_5CB9E0), in their order, the one in front last; a Curve Setup on its
// control point.
void EditorApp::ReopenSetups(const LayoutInterfaceData &data) {
    if (!data.present || !m_Session.HasDocument()) return;
    CKContext *context = m_Session.document->Context();
    std::vector<LayoutInterfaceData::Setup> setups = data.setups;
    std::stable_sort(setups.begin(), setups.end(),
                     [](const LayoutInterfaceData::Setup &a, const LayoutInterfaceData::Setup &b) { return a.values[1] < b.values[1]; });
    CK_ID front = 0;
    for (const LayoutInterfaceData::Setup &setup : setups) {
        CKObject *object = context->GetObject(setup.object);
        CKGUID guid;
        if (!object || !LayoutSetupGuid(object, guid)) continue;
        const CK_ID shown = CKCurve::Cast(object) && CKCurvePoint::Cast(context->GetObject(setup.other)) ? setup.other : setup.object;
        m_Setups.Open(m_Session.document->Reference(shown));
        if (setup.values[2]) front = shown;
    }
    if (front)
        m_Setups.Open(m_Session.document->Reference(front));
}

// The 3D Layout's data with the Setups open (sub_4B4E50: the GUID, the
// object, the Curve Setup's point, the window's state, its order, in front).
LayoutInterfaceData EditorApp::LayoutDataToSave() const {
    LayoutInterfaceData data = m_Layouts.Primary().LayoutData();
    if (!m_Session.HasDocument()) return data;
    CKContext *context = m_Session.document->Context();
    data.setups.clear();
    int order = 2;
    for (const SetupWindows::OpenSetup &open : m_Setups.OpenSetups()) {
        LayoutInterfaceData::Setup setup;
        const auto id = m_Session.document->NativeId(open.object);
        if (!LayoutSetupGuid(context->GetObject(id), setup.guid))
            continue;
        setup.object = id;
        setup.other = m_Session.document->NativeId(open.other);
        setup.values[0] = 2; // the docked state Dev's files hold most
        setup.values[1] = order++;
        setup.values[2] = open.front ? 1 : 0;
        data.setups.push_back(setup);
    }
    return data;
}









void EditorApp::DropFiles(const std::vector<std::wstring> &paths, float x, float y) {
    if (Busy()) {
        m_Session.Log(LogLevel::Info, "Dropped files not used: finish the dialog or the drag first");
        return;
    }
    for (const auto &wide : paths) {
        const std::string path = WideToUtf8(wide);
        std::wstring extension = Utf8ToWide(std::filesystem::u8path(path).extension().u8string());
        for (auto &c : extension) c = static_cast<wchar_t>(towlower(c));
        if (extension == L".cmo" || extension == L".nmo" || extension == L".vmo") {
            Command("document.open", path);
            return;
        }
        if (!m_Session.HasDocument() || !m_Schematics.DropFile(wide, path, float(x), float(y)))
            m_Session.Log(LogLevel::Info, "Dropped file not used: " + path +
                                            " (compositions open; images go on Script headers, .nms on graphs or bodies)");
    }
}

EditorResponse EditorApp::Command(const char *id, const std::string &path) {
    const EditorValue value{EditorLiteral("path"), EDITOR_TEXT_VALUE, {}, EditorText(path)};
    const EditorRequest request{m_Editor.Scope(), path.empty() ? nullptr : &value, path.empty() ? 0u : 1u};
    auto result = m_Editor.Execute(id, request);
    if (!result.message.empty()) m_Session.Log(result.status == EDITOR_OK ? LogLevel::Info : LogLevel::Warning, result.message);
    return result;
}





void EditorApp::UpdateTitle() {
    // Documentation "Menu Bar": the level and scene current in the title
    // bar, and the path of the composition loaded.
    std::wstring title = L"NeMoDev";
    const auto &document = m_Editor.ReadQuery("document.info", {m_Editor.Scope(), nullptr, 0});
    if (EditorClient::Integer(document, "open")) {
        const auto file = EditorClient::Text(document, "path");
        const std::wstring name = file.empty() ? L"New Composition" : Utf8ToWide(file);
        auto where = Utf8ToWide(EditorClient::Text(document, "level"));
        if (EditorClient::Integer(document, "separate-scene"))
            where += L" / " + Utf8ToWide(EditorClient::Text(document, "scene"));
        title += L" - " + (where.empty() ? L"" : L"[" + where + L"] ") + name +
                 (EditorClient::Integer(document, "dirty") ? L" *" : L"");
    }
    if (title != m_Title && m_Window) {
        SDL_SetWindowTitle(m_Window, WideToUtf8(title).c_str());
        m_Title = title;
    }
}

void EditorApp::StatusBar() {
    const EditorClient client(m_Editor.Host(), 0);
    StatusBarInfo info;
    const auto schematic = client.Inspect(m_Schematics.CurrentId());
    const auto count = EditorClient::Integer(schematic, "selection-count");
    if (count == 1) {
        info.selection = "1 element";
        if (const auto *target = EditorClient::Find(schematic, "selection-target"); target && target->object) {
            const auto object = client.Query("object.details", {ObjectArgument("target", target->object)});
            const auto *hasName = EditorClient::Find(object, "has-name");
            if (object.status == EDITOR_OK && (!hasName || hasName->integer))
                info.selection = EditorClient::Text(object, "name");
        }
    } else if (count > 1) info.selection = std::to_string(count) + " elements selected";
    const auto layoutAction = m_Layouts.Active().ActionText();
    if (layoutAction.empty()) info.action = EditorClient::Text(schematic, "action-text");
    else {
        info.action = layoutAction;
        info.selection = m_Layouts.Active().SelectionText();
    }
    info.information = m_Layouts.Active().InfoText();
    info.showCoordinates = m_Layouts.Active().Coordinates(info.coordinates);
    m_Editor.EnterDraw();
    m_StatusBar.Draw(client, info);
    m_Editor.LeaveDraw();
}

void EditorApp::MainMenu() {
    PushDarkChrome();
    if (!ImGui::BeginMainMenuBar()) {
        PopDarkChrome();
        return;
    }
    const auto &document = m_Editor.ReadQuery("document.info", {m_Editor.Scope(), nullptr, 0});
    const bool hasDocument = EditorClient::Integer(document, "open") != 0;
    auto unavailable = [](const char *label, const char *shortcut = nullptr, const char *why = "Not available yet") {
        ImGui::MenuItem(label, shortcut, false, false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why);
    };
    // Dev's File menu (labels and shortcuts of Dev 2.5).
    if (ImGui::BeginMenu("File")) {
        m_Workspace.CommandMenu("file.new");
        ImGui::Separator();
        m_Workspace.CommandMenu("file.document");
        // Source Control (Dev.exe menu 10, commands 0x8063..0x8068): without a
        // version control plugin Dev says the function is not available
        // (sub_5C9220 ...); this editor has none.
        if (ImGui::BeginMenu("Source Control")) {
            auto item = [this](const char *label, const char *function) {
                if (!ImGui::MenuItem(label)) return;
                EditorClient(m_Editor.Host(), 0).Command("message.show", {
                    TextArgument("text", std::string(function) + " function not available"), TextArgument("title", "NeMoDev")});
            };
            item("Get Latest Version...", "Get Latest Version");
            item("Check Out...", "Check Out");
            item("Check In...", "Check In");
            item("Undo Check Out...", "Undo Check Out");
            ImGui::Separator();
            item("Add to Source Control...", "Add to Source Control");
            item("Remove from Source Control...", "Remove from Source Control");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        m_Workspace.CommandMenu("file.history");
        ImGui::Separator();
        m_Workspace.CommandMenu("file.merge");
        ImGui::Separator();
        m_Workspace.CommandMenu("file.export");
        m_Workspace.CommandMenu("file.web");
        ImGui::Separator();
        m_Workspace.CommandMenu("file.print");
        const auto &recentData = m_Editor.ReadQuery("preferences.recent-files", {m_Editor.Scope(), nullptr, 0});
        const auto *paths = EditorClient::Find(recentData, "paths");
        if (paths && !paths->texts.empty()) {
            ImGui::Separator();
            const auto recent = paths->texts; // opening reorders the list
            for (size_t i = 0; i < recent.size(); ++i) {
                const std::string label = std::to_string(i + 1) + ") " + recent[i];
                if (ImGui::MenuItem(label.c_str())) Command("document.open", recent[i]);
            }
        }
        ImGui::Separator();
        m_Workspace.CommandMenu("file.close");
        ImGui::EndMenu();
    }
    // Dev's Resources menu (screenshot).
    if (ImGui::BeginMenu("Resources")) {
        m_Workspace.CommandMenu("resources.library");
        ImGui::Separator();
        m_Workspace.CommandMenu("resources.import");
        if (ImGui::BeginMenu("Import File As", hasDocument)) {
            m_Workspace.CommandMenu("resources.import-as");
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    // Dev's Editors menu.
    if (ImGui::BeginMenu("Editors")) {
        m_Workspace.CommandMenu("editors.managers");
        m_Workspace.CommandMenu("editors.paths");
        m_Workspace.CommandMenu("editors.diagnostics");
        ImGui::Separator();
        if (ImGui::MenuItem("Close All Setups and Managers")) CloseSetupsAndManagers();
        ImGui::EndMenu();
    }
    // Dev's Options menu (screenshot).
    if (ImGui::BeginMenu("Options")) {
        m_Workspace.CommandMenu("options.preferences");
        ImGui::Separator();
        const auto runtime = m_Editor.Query("runtime.state", {m_Editor.Scope(), nullptr, 0});
        if (ImGui::MenuItem(EditorClient::Integer(runtime, "trace") ? "Deactivate Trace Mode" : "Activate Trace Mode", m_Workspace.ShortcutLabel("runtime.toggle-trace"), false,
                            m_Editor.CommandState("runtime.toggle-trace", {m_Editor.Scope(), nullptr, 0}) == EDITOR_OK))
            Command("runtime.toggle-trace");
        // Hide/Show Dynamic Objects (documentation "Options Menu"): the
        // objects and Scripts Building Blocks create, in the Level Manager,
        // the Schematic and the Profiler (General Preferences' "Show Dynamic Objects").
        {
            const auto settings = m_Editor.Query("preferences.dynamic-objects", {m_Editor.Scope(), nullptr, 0});
            const bool dynamic = EditorClient::Integer(settings, "checked") != 0;
            if (ImGui::MenuItem(dynamic ? "Hide Dynamic Objects" : "Show Dynamic Objects", nullptr, false,
                m_Editor.CommandState("preferences.toggle-dynamic-objects", {m_Editor.Scope(), nullptr, 0}) == EDITOR_OK))
                Command("preferences.toggle-dynamic-objects");
        }
        m_Workspace.CommandMenu("options.unused");
        if (ImGui::MenuItem("Refresh Windows")) {
            Command("environment.refresh");
            // Documentation "Options Menu": refreshes the windows "and resets
            // the three main tabbed windows to their default size and position"
            // (the other windows docked stay in their tab rows).
            m_Schematics.Invalidate();
            if (hasDocument && m_Schematics.CurrentId()) m_Schematics.Current().Refresh();
            m_Panels.RefreshStack();
            m_ResetLayout = true;
            m_CloseSeparated = false;
        }
        // "resize all windows to their initial size, and close all separated windows".
        if (ImGui::MenuItem("Reset Interface")) {
            m_ResetLayout = true;
            m_CloseSeparated = true;
        }
        m_Workspace.CommandMenu("options.versions");
        ImGui::Separator();
        m_Workspace.CommandMenu("options.plugins");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        m_Workspace.CommandMenu("help.reference");
        m_Workspace.CommandMenu("help.about");
        ImGui::EndMenu();
    }
    m_Workspace.Menu();
    ImGui::EndMainMenuBar();
    PopDarkChrome();
}

#ifdef VIRTOOLSDEV_UI_TESTS
void EditorApp::LogUndoTop() {
    m_Session.Log(LogLevel::Info, m_Session.History().CanUndo() ? "Last undo step: " + m_Session.History().UndoLabel()
                                                       : std::string("Last undo step: (none)"));
}
#endif

void EditorApp::CreateObject(LayoutObject kind, EditorView source) {
    const EditorClient client(m_Editor.Host(), source);
    const auto creation = client.Query("layout.creation");
    if (creation.status != EDITOR_OK)
        return;
    ObjectRef selected{};
    for (const auto &field : m_Workspace.Inspect(m_Workspace.Role("main.level")).fields)
        if (field.key == "selection" && field.objects.size() == 1)
            selected = field.objects.front();
    if (!selected)
        if (const auto *target = EditorClient::Find(creation, "target"))
            selected = target->object;
    const auto *camera = EditorClient::Find(creation, "camera");
    static const char *commands[] = {"object.create-camera",   "object.create-light",   "object.create-frame3d",
                                     "object.create-curve",    "object.create-grid",    "object.create-frame2d",
                                     "object.create-material", "object.create-texture", "object.create-portal"};
    const auto index = static_cast<size_t>(kind);
    if (index >= sizeof(commands) / sizeof(commands[0])) return;
    const auto result = client.Command(
        commands[index],
        {ObjectArgument("target", selected), ObjectArgument("camera", camera ? camera->object : ObjectRef{}),
         IntegerArgument("show-cone",
                         EditorClient::Integer(creation, kind == LayoutObject::Camera ? "camera-cone" : "light-cone")),
         IntegerArgument("show-in-player", EditorClient::Integer(creation, "show-player"))});
    if (auto *window = source ? m_Layouts.Find(source) : &m_Layouts.Active())
        window->CreatedObject(result);
}

bool EditorApp::Busy() const {
    return m_Session.runtimeOperation.Busy() || ImGui::IsAnyItemActive() ||
           ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || m_Schematics.Busy() || m_Layouts.Busy();
}

void EditorApp::GlobalShortcuts() {
    if (ImGui::GetIO().WantTextInput || Busy()) return;
    m_Workspace.CommandShortcuts();
}

void EditorApp::ShowSchematicExplorer() { EditorClient(m_Editor.Host(), 0).Command("schematic.open-explorer"); }

void EditorApp::RefreshSchematicViews() {
    const auto scripts = m_Session.HasDocument() ? m_Session.document->ScriptsRevision() : 0;
    if (scripts != m_ScriptsSeen) {
        m_ScriptsSeen = scripts;
        m_Panels.RefreshStack();
    }
    if ((!m_Session.HasDocument() || !m_Session.document->EditOpen()) && !m_Editor.HasEdits())
        m_Schematics.ShowRequested();
}

void EditorApp::RuntimeOperationFrame() {
    // Keep docking assignments alive while runtime-backed windows are
    // suspended. This frame touches ImGui only, never the CK document.
    const ImGuiID dockspace = ImGui::GetID("SchematicEditorDockSpace");
    ImGui::DockSpaceOverViewport(dockspace, ImGui::GetMainViewport(), ImGuiDockNodeFlags_KeepAliveOnly);
    m_Editor.NativeAvailable(false);
    m_Workspace.Draw(false);
    m_Workspace.CommandShortcuts();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 95));
    ImGui::Begin("Working###RuntimeOperation", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    const char spinner[] = {'|', '/', '-', '\\'};
    ImGui::Text("%c  %s", spinner[static_cast<int>(ImGui::GetTime() * 8) % 4], m_Session.runtimeOperation.Label().c_str());
    ImGui::TextUnformatted("Please wait...");
    ImGui::End();
    ImGui::SetMouseCursor(ImGuiMouseCursor_Wait);
}

void EditorApp::Frame() {
    m_Session.PollRuntimeOperations();
#ifdef VIRTOOLSDEV_UI_TESTS
    if (m_StartupAfterLoads && !m_Session.runtimeOperation.Busy()) {
        const bool opened = m_StartupAfterLoads == 2;
        m_StartupAfterLoads = 0;
        RunStartupActions(opened);
    }
#endif
    m_Editor.NativeAvailable(!m_Session.runtimeOperation.Busy());
    m_Editor.UpdateModules(EDITOR_PREPARE_FRAME);
    m_Editor.NativeAvailable(!m_Session.runtimeOperation.Busy());
    if (m_Session.runtimeOperation.Busy()) { RuntimeOperationFrame(); return; }
    m_Session.DispatchFileRequests();
    if (m_Session.runtimeOperation.Busy()) { RuntimeOperationFrame(); return; }
    if (m_Session.HasDocument()) m_Session.objectSelection.Validate(*m_Session.document);
    m_Schematics.Validate();
    m_Editor.Attach(m_Session.HasDocument() ? m_Session.document->Token() : DocumentToken{},
                    m_Session.HasDocument() ? m_Session.document->TopologyRevision() : 0, true,
                    m_Session.HasDocument());
    ObjectRef selected{};
    if (!m_Session.objectSelection.References().empty()) selected = m_Session.objectSelection.References().front();
    else if (m_Session.HasDocument() && LevelSelection().size() == 1)
        selected = m_Session.document->Reference(*LevelSelection().begin());
    m_Editor.SetSelection(selected);
    // The current scene and the changes show in the title (cheap: set only when it changes).
    UpdateTitle();
    MainMenu();
    if (m_Session.runtimeOperation.Busy()) { RuntimeOperationFrame(); return; }
    // The editor's former name: the saved window layouts know this ID.
    const ImGuiID dockspace = ImGui::GetID("SchematicEditorDockSpace");
    StatusBar();
    const auto layout = m_Workspace.UpdateLayout(dockspace, m_ResetLayout, m_CloseSeparated);
    if (!layout.closeLegacy.empty()) DevCloseSeparated(layout.closeLegacy);
    if (layout.changed)
        m_FocusSchematic = layout.focus ? 0 : 3;
    m_CloseSeparated = false;
    m_ResetLayout = false;
    ImGui::DockSpaceOverViewport(dockspace, ImGui::GetMainViewport());

    RefreshSchematicViews();
    // Dev's main windows have no close button.
    // The canvas size is known only after a docked frame.
#ifdef VIRTOOLSDEV_UI_TESTS
    if (m_FitFrames > 0 && --m_FitFrames == 0 && m_Schematics.CurrentId()) m_Schematics.Current().FitAll();
#endif
#ifdef VIRTOOLSDEV_UI_TESTS
    // --tool print:DIR (the dialog, pages to DIR on OK), once the Schematic has its Scripts.
    if (!m_PendingStartupDialog.empty() && m_FocusSchematic <= 0 && ++m_PrintPagesFrames > 5) {
        OpenStartupDialog(m_PendingStartupDialog);
        m_PendingStartupDialog.clear();
    }
    if (!m_PendingPrintDialog.empty() && m_FocusSchematic <= 0 && ++m_PrintPagesFrames > 5) {
        Command("document.print", m_PendingPrintDialog);
        m_PendingPrintDialog.clear();
    }
    // --tool printpages:DIR, once the Schematic has its Scripts.
    if (!m_PendingPrintPages.empty() && m_FocusSchematic <= 0 && ++m_PrintPagesFrames > 5) {
        const auto print = Command("document.print", m_PendingPrintPages);
        m_PrintPagesOperation = print.status == EDITOR_QUEUED ? print.operation : 0;
        m_PendingPrintPages.clear();
    }
    if (m_PrintPagesOperation) {
        const auto input = m_Editor.Query("output.input", {m_Editor.Scope(), nullptr, 0});
        uint64_t id = 0, operation = 0, kind = 0;
        for (const auto &field : input.fields) {
            if (field.key == "input") id = field.integer;
            if (field.key == "operation") operation = field.integer;
            if (field.key == "kind") kind = field.integer;
        }
        if (operation == m_PrintPagesOperation && kind == EDITOR_PRINT_INPUT) {
            const EditorValue values[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, id},
                {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("accept")}};
            m_Editor.Execute("output.respond", {m_Editor.Scope(), values, 2});
            m_PrintPagesOperation = 0;
        } else if (input.status == EDITOR_NO_CHANGE) m_PrintPagesOperation = 0;
    }
#endif
    m_Editor.UpdateModules(EDITOR_BEFORE_VIEWS);
    m_Workspace.Draw();
    m_Layouts.UpdateInput();
    m_Editor.UpdateModules(EDITOR_AFTER_VIEWS);
#ifdef VIRTOOLSDEV_UI_TESTS
    const bool settled = m_FocusSchematic <= 0;
#endif
    if (m_FocusSchematic > 0) {
        // The front tabs of Dev's start (the Level Manager in front of the
        // Schematic, focused last); repeated because windows appearing later
        // take the focus.
        m_Workspace.Focus(m_Workspace.Role("main.layout"));
        m_Workspace.Focus(m_Workspace.Role("main.building-blocks"));
        m_Workspace.Focus(m_Workspace.Role("main.level"));
#ifdef VIRTOOLSDEV_UI_TESTS
        // Several windows (names separated by '|'): one a frame, the window
        // focused at the end of a frame being the one whose tab shows.
        std::vector<std::string> names;
        for (size_t at = 0; at < m_Startup.focusWindow.size();) {
            size_t end = m_Startup.focusWindow.find('|', at);
            if (end == std::string::npos) end = m_Startup.focusWindow.size();
            names.push_back(m_Startup.focusWindow.substr(at, end - at));
            at = end + 1;
        }
        if (!names.empty()) {
            auto name=names[m_FocusSchematic % names.size()];
            if(name=="Building Blocks")m_Workspace.Focus(m_Workspace.Role("main.building-blocks"));
            else if(name=="Schematic")m_Schematics.Focus();
            else if(name=="Level Manager")m_Workspace.Focus(m_Workspace.Role("main.level"));
            else if(name=="Hierarchy Manager")m_Workspace.Focus(m_Workspace.Role("main.hierarchy"));
            else if(name=="Attribute Manager")m_Workspace.Focus(m_Workspace.Role("main.attributes"));
            else ImGui::SetWindowFocus(name.c_str());
        }
#endif
        --m_FocusSchematic;
    }
#ifdef VIRTOOLSDEV_UI_TESTS
    StartupFrame(settled);
#endif

    GlobalShortcuts();
    UpdateTitle();
}

} // namespace vdev

namespace vdev {
EditorView EditorApp::OpenSceneView(const std::string &type) { return m_Workspace.OpenRole("main."+type); }
bool EditorApp::SceneViewOpen(const std::string &type) const { return m_Workspace.Role("main."+type)!=0; }
void EditorApp::CloseSceneView(const std::string &type) { m_Workspace.Close(m_Workspace.Role("main."+type)); }
std::string EditorApp::SceneWindow(const std::string &type) const { return m_Workspace.WindowName(m_Workspace.Role("main."+type)); }
std::set<CK_ID> EditorApp::LevelSelection() {
    std::set<CK_ID> selection;
    const auto &result=m_Workspace.Inspect(m_Workspace.Role("main.level"));
    if(m_Session.HasDocument())for(const auto &field:result.fields)if(field.key=="selection")for(auto ref:field.objects)if(auto id=m_Session.document->NativeId(ref))selection.insert(id);
    return selection;
}
bool EditorApp::RegisterSceneNavigation(std::string &error){
    const EditorParameter target{EDITOR_TEXT("target"),EDITOR_OBJECT};
    const EditorParameter show[]={target,{EDITOR_TEXT("focus"),EDITOR_INTEGER_VALUE,1}};
    const EditorParameter expand{EDITOR_TEXT("expand"),EDITOR_INTEGER_VALUE};
    EditorCommandDesc commands[]={
        {sizeof(EditorCommandDesc),EDITOR_TEXT("scene.show-object"),EDITOR_TEXT("builtin.navigation"),EDITOR_TEXT("Show in Level Manager"),&target,1,nullptr,this,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("script.show"),EDITOR_TEXT("builtin.navigation"),EDITOR_TEXT("Show Script"),show,2,nullptr,this,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("script.refresh"),EDITOR_TEXT("builtin.navigation"),EDITOR_TEXT("Refresh Scripts"),nullptr,0,nullptr,this,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("scene.expand-level"),EDITOR_TEXT("builtin.navigation"),EDITOR_TEXT("Expand Level Manager"),&expand,1,nullptr,this,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE}};
    commands[0].run = [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
        auto &app=*static_cast<EditorApp*>(data);
        return host->navigateView(host->data, EditorString EDITOR_TEXT("scene.level"), app.OpenSceneView("level"),
                                  request, sink);
    };
    commands[1].run = [](void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        auto &app=*static_cast<EditorApp*>(data);
        const auto id = app.m_Session.document->NativeId(request->values[0].object);
        if (!id)
            return EDITOR_STALE;
        const bool focus=request->values[1].integer!=0;
        return app.m_Schematics.Show(ElementAddress::Capture(*app.m_Session.document,id,Selection::HeaderOf(id)),
                                     {},focus,request->scope.view);
    };
    commands[2].run = [](void *data, const EditorHost *, const EditorRequest *, EditorSink) {
        static_cast<EditorApp *>(data)->m_Panels.RefreshStack();
        return EDITOR_OK;
    };
    commands[3].run = [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
        auto &app = *static_cast<EditorApp *>(data);
        return host->navigateView(host->data, EditorString EDITOR_TEXT("scene.level"), app.OpenSceneView("level"),
                                  request, sink);
    };
    const EditorParameter prototypes{EDITOR_TEXT("prototypes"), EDITOR_INTEGER_LIST};
    const EditorCommandDesc remember{sizeof(EditorCommandDesc),
                                     EDITOR_TEXT("building-block.remember"),
                                     EDITOR_TEXT("builtin.navigation"),
                                     EDITOR_TEXT("Remember Building Blocks"),
                                     &prototypes,
                                     1,
                                     [](void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
                                         auto &app = *static_cast<EditorApp *>(data);
                                         const auto &values = request->values[0].integers;
                                         for (uint32_t i = 0; i < values.count; ++i) {
                                             const CKGUID guid(CKDWORD(values.data[i] >> 32), CKDWORD(values.data[i]));
                                             if (!app.m_Catalog.Find(guid))
                                                 return EDITOR_INVALID;
                                         }
                                         for (uint32_t i = 0; i < values.count; ++i)
                                             app.RememberBlock(
                                                 CKGUID(CKDWORD(values.data[i] >> 32), CKDWORD(values.data[i])));
                                         return EDITOR_OK;
                                     },
                                     this,
                                     EDITOR_WORKSPACE,
                                     EDITOR_SESSION_CHANGE};
    const EditorQueryDesc recent{sizeof(EditorQueryDesc),
                                 EDITOR_TEXT("building-block.recent"),
                                 EDITOR_TEXT("builtin.navigation"),
                                 nullptr,
                                 0,
                                 [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
                                     const auto &app = *static_cast<EditorApp *>(data);
                                     std::vector<uint64_t> values;
                                     for (auto guid : app.m_RecentBlocks)
                                         values.push_back((uint64_t(guid.d1) << 32) | guid.d2);
                                     const EditorValue result{EDITOR_TEXT("prototypes"),
                                                              EDITOR_INTEGER_LIST,
                                                              {},
                                                              {},
                                                              0,
                                                              {},
                                                              {values.data(), uint32_t(values.size())}};
                                     return sink.write ? sink.write(sink.data, &result, 1) : EDITOR_INVALID;
                                 },
                                 this,
                                 EDITOR_QUERY_EACH_READ,
                                 EDITOR_WORKSPACE,
                                 &prototypes,
                                 1};
    std::vector<EditorCommandDesc> all(std::begin(commands), std::end(commands));
    all.push_back(remember);
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("scene.navigation");
    module.commands = all.data();
    module.commandCount = uint32_t(all.size());
    module.queries = &recent;
    module.queryCount = 1;
    return m_Editor.Register(module, error);
}
} // namespace vdev

namespace vdev {
namespace {
#ifdef VIRTOOLSDEV_UI_TESTS
class PngPages final : public PrintDestination {
  public:
    explicit PngPages(std::string directory) : directory(std::move(directory)) {}
    ~PngPages() override { Cancel(); }
    const PrinterSettings &Settings() const override { return settings; }
    bool ImageFiles() const override { return true; }
    std::string SubmittedMessage(const std::string &) const override {
        return std::to_string(written.size()) + " page(s) written to " + directory;
    }
    bool PreparePages(int count, int w, int h, std::string &error) override {
        if (uint64_t(w) * h * 4 > 512u * 1024u * 1024u) {
            error = "Print image is too large";
            return false;
        }
        width = w;
        height = h;
        std::error_code failure;
        std::filesystem::create_directories(std::filesystem::u8path(directory), failure);
        if (failure) {
            error = failure.message();
            return false;
        }
        files = std::make_unique<OutputFiles>(report);
        for (int i = 1; i <= count; ++i) {
            auto path =
                files->Add((std::filesystem::u8path(directory) / ("page" + std::to_string(i) + ".png")).u8string());
            if (path.empty()) {
                error = Errors();
                return false;
            }
            staged.push_back(std::move(path));
        }
        return true;
    }
    PrinterStartStatus BeginDocument(const std::string &, std::string &) override {
        return PrinterStartStatus::Started;
    }
    bool BeginPage(std::string &) override {
        pixels.assign(size_t(width) * height * 4, 255);
        return page < staged.size();
    }
    bool DrawTile(const unsigned char *bgra, int w, int h, int left, int top, int right, int bottom,
                  std::string &error) override {
        if (left < 0 || top < 0 || right > width || bottom > height || right - left != w || bottom - top != h) {
            error = "Invalid print tile";
            return false;
        }
        for (int y = 0; y < h; ++y)
            std::memcpy(pixels.data() + (size_t(top + y) * width + left) * 4, bgra + size_t(y) * w * 4, size_t(w) * 4);
        return true;
    }
    bool EndPage(std::string &error) override {
        if (page >= staged.size() || !WriteBgraPng(staged[page], width, height, pixels.data())) {
            error = "Cannot write the Schematic print page";
            return false;
        }
        std::vector<unsigned char> verified;
        int w = 0, h = 0;
        if (!ReadImageRgba(staged[page], verified, w, h) || w != width || h != height ||
            verified.size() != pixels.size()) {
            error = "Cannot verify the Schematic print page";
            return false;
        }
        ++page;
        return true;
    }
    bool Submit(std::string &job, std::string &error) override {
        if (page != staged.size() || !files || !files->Publish(written)) {
            error = Errors();
            return false;
        }
        job = directory;
        return true;
    }
    void Cancel() override {
        files.reset();
        pixels.clear();
    }
    std::vector<std::string> WrittenFiles() const override { return written; }

  private:
    std::string Errors() const {
        std::string text;
        for (const auto &item : report.errors) {
            if (!text.empty()) text += "; ";
            text += item;
        }
        for (const auto &item : report.notes) {
            if (!text.empty()) text += "; ";
            text += item;
        }
        return text.empty() ? "Cannot publish print pages" : text;
    }
    PrinterSettings settings;
    std::string directory;
    SaveReport report;
    std::unique_ptr<OutputFiles> files;
    std::vector<std::string> staged, written;
    std::vector<unsigned char> pixels;
    int width = 0, height = 0;
    size_t page = 0;
};
#endif
}
std::unique_ptr<SchematicPrintContent> EditorApp::CapturePrint(std::string &error) {
    if (!m_Schematics.CurrentId()) { error = "No visible Script to print"; return {}; }
    auto snapshot = m_Schematics.Current().CapturePrint();
    if (!snapshot) error = "No visible Script to print";
    return snapshot;
}
PrintChoice EditorApp::ChoosePrinter(const std::string &directory) {
#ifdef VIRTOOLSDEV_UI_TESTS
    if (!directory.empty()) return {PrinterSelectionStatus::Selected, std::make_shared<PngPages>(directory), {}};
#else
    if (!directory.empty()) return {PrinterSelectionStatus::Failed, {}, "Unsupported print destination"};
#endif
    auto selection = Printer::Select(m_Window);
    return {selection.status, std::move(selection.printer), std::move(selection.error)};
}
} // namespace vdev

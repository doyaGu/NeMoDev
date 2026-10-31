#include "ui/app/EditorApp.h"
#include "ui/app/FileInputs.h"
#include "ui/app/OutputInputs.h"
#include "ui/panels/EventLogView.h"
#include <stdexcept>
#include "features/objects/ObjectViews.h"
#include "features/values/ValueEditors.h"
#include "features/arrays/ArrayTable.h"
#include "features/resources/BuildingBlockQueries.h"
#include "features/resources/BuildingBlockViews.h"
#include "features/resources/DataResourceViews.h"
#include "features/defaults/DefaultWorkspace.h"
#include "features/scene/SceneViewData.h"
#include "editor/ImGuiDrawing.h"
#include "core/level/SetupCommands.h"
#include "ui/schematic/Snapshots.h"


#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>

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

EditorApp::EditorApp(HtmlFonts fonts)
    : m_Editor([this] { return m_State.HasDocument() ? m_State.document.get() : nullptr; },
               [this] { return FileOperationAvailable() && !m_ResourceCommands.Busy(); }),
      m_ObjectCommands([this] { return m_State.HasDocument() ? m_State.document.get() : nullptr; }),
      m_Canvas(m_State), m_Panels(m_State, m_Canvas), m_Tools(m_State, m_Canvas), m_HtmlFonts(fonts) {
    m_State.workspace = &m_Workspace;
    m_State.documentViews = this;
    m_State.files.Execution(&m_Execution);
    m_Editor.report = [this](const EditorResponse &result) {
        if (!result.message.empty() && result.status != EDITOR_NO_CHANGE)
            m_State.Log(result.status == EDITOR_OK ? LogLevel::Info : LogLevel::Warning, result.message);
    };

}

void EditorApp::HandleEvent(const SDL_Event &event) {
    if (m_State.runtimeOperation.Busy()) return;
    m_Layout.HandleEvent(event);
    if (!m_Window || event.type != SDL_EVENT_WINDOW_FOCUS_LOST ||
        event.window.windowID != SDL_GetWindowID(m_Window) || m_Layout.FullScreen()) return;
    if (m_State.debugger && m_State.debugger->GetState() == Debugger::State::Running)
        Command("runtime.pause");
}

namespace {

const char *SCHEMATIC_WINDOW = "Schematic";

} // namespace

bool EditorApp::Initialize(SDL_Window * window, const std::string &runtimeDirectory, const std::string &fileToOpen, const std::vector<std::string> &modules
#ifdef VIRTOOLSDEV_UI_TESTS
                           , const StartupOptions &startup
#endif
                           ) {
    m_Window = window;
#ifdef VIRTOOLSDEV_UI_TESTS
    m_Startup = startup;
    const bool isolated = startup.isolated;
#else
    const bool isolated = false;
#endif
    // The Event Log's first line (Dev.exe sub_4229D0, language pack 141).
    m_State.Log(LogLevel::Info, "Virtools Started.");
    m_State.eventLog.HideStatus();
    // CK text retains its native bytes; file paths remain UTF-8.
    SetLossyTextHandler([this](const std::string &text) {
        m_State.RuntimeLog(LogLevel::Warning, "Text conversion failed; original CK bytes kept: " + text);
    });
    // Preferences: our file, else Dev's own settings (read-only), else Dev's
    // defaults (isolated: the defaults).
    Preferences &preferences = m_State.preferences;
    const bool loaded = !isolated && preferences.Load(PreferencesPath());
    if (!isolated && !loaded) {
        const int imported = preferences.ImportFromDev() + preferences.ImportDevIni(runtimeDirectory);
        if (imported) m_State.Log(LogLevel::Info, "Imported " + std::to_string(imported) + " Virtools Dev preferences");
    } else if (loaded && !preferences.settingsSaved) {
        // A preferences file from before the General Preferences: Dev's values for the rest.
        const int imported = preferences.ImportDevSettings() + preferences.ImportDevIni(runtimeDirectory);
        if (imported) m_State.Log(LogLevel::Info, "Imported " + std::to_string(imported) + " Virtools Dev General Preferences");
    }
    ApplyPreferences();
#ifdef VIRTOOLSDEV_UI_TESTS
    m_State.view.showAllScripts = startup.showAllScripts;
#endif
    m_State.runtimeDirectory = runtimeDirectory;
    m_Tools.applyPreferences = [this] {
        ApplyPreferences();
        ApplyRuntimeSettings();
        SavePreferences();
    };
    std::string error;
    m_State.host.SetRenderEngine(preferences.Text("CK2 Settings\\Render Engine"));
    const bool started = m_State.host.Start(runtimeDirectory, [this](LogLevel level, const std::string &message) {
        m_State.RuntimeLog(level, message);
    }, &error, window);
    if (!started) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NeMoDev", ("Cannot start the Virtools runtime:\n" + error).c_str(), window);
        return false;
    }
    m_State.catalog.Refresh();
    if (!RegisterBuildingBlockQueries(m_Editor, m_State.catalog, error) ||
        !RegisterBuildingBlockViews(m_Editor, error) || !m_ResourceCommands.Register(error) ||
        !RegisterDataResourceViews(m_Editor, error) || !RegisterResourceInputs(m_Editor, window, error) ||
        !RegisterDefaultWorkspace(m_Editor, error) || !m_Workspace.Register(error) ||
        !m_State.eventLog.Register(m_Editor, error) || !RegisterEventLogView(m_Editor, error) ||
        !RegisterHelpViews(m_Editor, m_HtmlFonts, error) || !m_FileCommands.Register(error) ||
        !RegisterFileInputs(m_Editor, window, error) || !m_ObjectCommands.Register(m_Editor, error) ||
        !m_SelectionCommands.Register(m_Editor, error) || !m_Execution.Register(error) || !m_Outputs.Register(error) ||
        !RegisterOutputInputs(m_Editor, window, error) || !m_ParameterCommands.Register(m_Editor, error) ||
        !RegisterValueEditors(m_Editor, error) || !m_ArrayCommands.Register(m_Editor, error) ||
        !m_SetupCommands.Register(m_Editor, error) || !m_SchematicCommands.Register(m_Editor, error) ||
        !RegisterArrayValueEditors(m_Editor, error) || !RegisterObjectViews(m_Editor, error) ||
        !m_SceneCommands.Register(error) || !RegisterSceneViews(m_Editor, error) ||
        !RegisterParameterDialogs(m_Editor, m_State, error) || !RegisterSceneNavigation(error)) {
        m_State.Log(LogLevel::Error, error);
        return false;
    }
    for (const auto &module : modules)
        if (!m_Editor.Load(module, error)) { m_State.Log(LogLevel::Error, error); return false; }
    auto configuration = DefaultWorkspaceConfiguration();
    if (!m_Editor.Activate(EDITOR_IMGUI_VERSION, error, configuration.contributions)) {
        m_State.Log(LogLevel::Error, error);
        return false;
    }
    m_State.document = std::make_unique<Document>(m_State.host);
    m_Editor.Attach({}, 0, true, false);
    if (!m_Workspace.Configure(configuration, error)) {
        m_State.Log(LogLevel::Error, error);
        return false;
    }
    OpenSceneView("level");
    m_State.debugger = m_Execution.Bind(*m_State.document);
    // Dev's paths (HKCU\Software\Virtools\Data Paths, read-only) until the
    // editor has its own.
    if (!isolated && !preferences.pathsSaved && preferences.ImportDevPaths())
        m_State.Log(LogLevel::Info, "Path Manager: Virtools Dev's paths");
    ApplyRuntimeSettings();
    m_HideFps = m_State.preferences.Bool("Time Settings\\Hide Fps");
    // The 3D Layout's render context (Dev makes it with its main window).
    m_Layout.createObject = [this](LayoutObject kind) { CreateObject(kind); };

    std::string layoutError;
    if (!m_Layout.Start(window, &layoutError)) m_State.Log(LogLevel::Warning, "3D Layout: " + AnsiToUtf8(layoutError));
    EditorView lastResource = 0;
    const auto openResource = [&](const std::string &resource) {
        const auto result = Command("resource.open-library", resource);
        for (const auto &field : result.fields) if (field.key == "view") lastResource = field.integer;
    };
    for (const std::string &resource : std::vector<std::string>(preferences.dataResources)) openResource(resource);
#ifdef VIRTOOLSDEV_UI_TESTS
    for (const std::string &resource : m_Startup.resources) openResource(resource);
    if (lastResource && !m_Startup.resourceFolder.empty()) {
        const EditorValue values[] = {{EDITOR_TEXT("folder"), EDITOR_TEXT_VALUE, {}, EditorText(m_Startup.resourceFolder)},
            {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, lastResource}};
        m_Editor.Execute("resource.browse", {m_Editor.Scope(), values, 2});
    }
    if (m_Startup.newComposition) NewComposition(false);
#endif
    // Dev starts on a new composition (its 3D Layout shows the new scene's
    // gray), saying nothing on the status bar.
    if (!m_State.HasDocument()) {
        NewComposition(false);
        m_State.eventLog.HideStatus();
    }
#ifdef VIRTOOLSDEV_UI_TESTS
    if (fileToOpen.empty()) RunStartupActions(false);
    else { m_RunStartupAfterOpen = true; OpenFile(fileToOpen); }
#else
    if (!fileToOpen.empty()) OpenFile(fileToOpen);
#endif
    m_ResourceCommands.Poll();
    m_ResourceCommands.changed = [this] {
        m_State.preferences.dataResources = m_ResourceCommands.OpenLibraries();
        SavePreferences();
    };
    UpdateTitle();
    return true;
}

std::string EditorApp::PreferencesPath() const {
    char *preferencePath = SDL_GetPrefPath("Virtools", "VirtoolsDev");
    if (!preferencePath) return {};
    const std::filesystem::path directory = std::filesystem::u8path(preferencePath);
    SDL_free(preferencePath);
    const auto file = directory / "VirtoolsDevPreferences.ini";
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        const auto oldDirectory = std::filesystem::u8path(SDL_GetBasePath());
        for (const char *name : {"VirtoolsDevPreferences.ini", "SchematicEditorPreferences.ini"}) {
            const auto old = oldDirectory / name;
            if (std::filesystem::exists(old, error)) { std::filesystem::copy_file(old, file, error); break; }
        }
    }
    return file.u8string();
}

void EditorApp::SavePreferences() {
#ifdef VIRTOOLSDEV_UI_TESTS
    if (m_Startup.isolated) return;
#endif
    m_State.preferences.Save(PreferencesPath());
}

void EditorApp::ApplyPreferences() {
    const Preferences &p = m_State.preferences;
    DocumentCommands::Settings files;
    files.open.environment = p.NewSceneEnvironment();
    files.open.testLinkTypes = p.Bool("Schematic\\Test Parameter Link after Loading");
    files.open.removeInvalidLinks = p.Bool("Schematic\\Remove Invalid Link after Loading");
    files.saveScriptSnapshots = p.Bool("Schematic\\Save Script Snapshot");
    m_FileCommands.Configure(files);
    m_Outputs.SaveScriptSnapshots(files.saveScriptSnapshots);
    ViewOptions &view = m_State.view;
    view.autoLink = p.automaticLink;
    view.showControlPoints = p.showControlPoints;
    view.showLinkInfo = p.showLinkDelay;
    view.showPriorities = p.showPriorities;
    view.showHeaders = p.showScriptLeftPart;
    ApplyColors(p, m_State.style);
}

// The General Preferences values the CK context takes: Dev applies them
// with OK (Dev.exe sub_5AF2C0) and at start.
void EditorApp::ApplyRuntimeSettings() {
    CKContext *context = m_State.host.Context();
    if (!context) return;
    const Preferences &p = m_State.preferences;
    const EditorValue reengage[] = {
        {EDITOR_TEXT("behavior"), EDITOR_INTEGER_VALUE, {}, {}, p.behaviorBreakpointReengage ? 1u : 0u},
        {EDITOR_TEXT("link"), EDITOR_INTEGER_VALUE, {}, {}, p.linkBreakpointReengage ? 1u : 0u},
        {EDITOR_TEXT("parameter"), EDITOR_INTEGER_VALUE, {}, {}, p.parameterBreakpointReengage ? 1u : 0u}};
    m_Editor.Execute("runtime.set-breakpoint-reengagement", {m_Editor.Scope(), reengage, 3});
    const EditorValue redraw{EDITOR_TEXT("enabled"), EDITOR_INTEGER_VALUE, {}, {}, p.redrawOnlyAtBreakpoint ? 1u : 0u};
    m_Editor.Execute("runtime.set-redraw-at-breakpoint", {m_Editor.Scope(), &redraw, 1});
    // File saving (Miscellaneous Controls): whole-file compression and its
    // level, texture/sprite and sound save options, the image format.
    CK_FILE_WRITEMODE mode = context->GetFileWriteMode();
    mode = static_cast<CK_FILE_WRITEMODE>(p.Bool("File Options\\Compressed") ? (mode | CKFILE_WHOLECOMPRESSED)
                                                                              : (mode & ~CKFILE_WHOLECOMPRESSED));
    context->SetFileWriteMode(mode);
    context->SetCompressionLevel(std::max(1, std::min(9, p.Int("File Options\\Compression Level"))));
    context->SetGlobalImagesSaveOptions(static_cast<CK_TEXTURE_SAVEOPTIONS>(p.Int("File Options\\Textures")));
    context->SetGlobalSoundsSaveOptions(static_cast<CK_SOUND_SAVEOPTIONS>(p.Int("File Options\\Sounds")));
    const std::string format = p.Text("File Options\\Texture Format");
    if (!format.empty())
        {
            // The 2.1 readers give no default properties (core MakeSaveFormat).
            std::vector<char> bytes = MakeSaveFormat(context, format);
            if (!bytes.empty()) context->SetGlobalImagesSaveFormat(reinterpret_cast<CKBitmapProperties *>(bytes.data()));
        }
    SetQuantizationSamplingFactor(std::max(1, std::min(100, p.Int("CK2 Settings\\Quantization Sampling Factor"))));
    // Behavior Link Properties: Max Behavioral Iterations.
    if (CKBehaviorManager *behaviors = context->GetBehaviorManager())
        behaviors->SetBehaviorMaxIteration(std::max(1, p.Int("BehaviorManager\\MaxIteration")));
    // Path Manager: the saved categories and their paths.
    CKPathManager *paths = context->GetPathManager();
    for (const auto &category : paths ? p.paths : decltype(p.paths)()) {
        XString name(const_cast<CKSTRING>(category.first.c_str()));
        int index = paths->GetCategoryIndex(name);
        if (index < 0) index = paths->AddCategory(name);
        if (index < 0) continue;
        while (paths->GetPathCount(index) > 0) paths->RemovePath(index, 0);
        for (const std::string &path : category.second) {
            XString value(const_cast<CKSTRING>(path.c_str()));
            paths->AddPath(index, value);
        }
    }
}

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
    m_State.showDebugger = false;
    m_Tools.CloseManagers();
}

EditorApp::~EditorApp() { Shutdown(); }

void EditorApp::Shutdown() {
    if (m_Shutdown) return;
    m_Shutdown = true;
    m_State.FinishRuntimeOperations();
    m_FileCommands.Stop();
    m_Outputs.Stop();
    m_State.DrainRuntimeLog();
    m_State.fileDialogs.Close();
    // Toolbar toggles are preferences too.
    Preferences &p = m_State.preferences;
    p.automaticLink = m_State.view.autoLink;
    p.showControlPoints = m_State.view.showControlPoints;
    p.showLinkDelay = m_State.view.showLinkInfo;
    p.showPriorities = m_State.view.showPriorities;
    p.showScriptLeftPart = m_State.view.showHeaders;
    m_ResourceCommands.Poll();
    SavePreferences();
    if (m_State.document) {
        CloseDocumentViews();
        m_State.files.Close({});
        m_State.FinishRuntimeOperations();
    }
    m_State.debugger = nullptr;
    m_Execution.Stop();
    m_State.files.Execution(nullptr);
    m_State.documentViews = nullptr;
    m_Layout.Stop();
    m_Workspace.CloseAll();
    m_ResourceCommands.Stop();
    m_SceneCommands.Stop();
    m_State.document.reset();
    m_State.host.Stop();
    m_State.documentViews = nullptr;
    m_Workspace.CloseAll();
    m_Editor.Stop();
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
    if (!result.message.empty()) m_State.Log(result.status == EDITOR_ERROR ? LogLevel::Error : LogLevel::Info, result.message);
    if (!result.print && result.status == EDITOR_OK) m_State.Log(LogLevel::Info, "Web page written: " + result.path);
    if (!result.print && result.webFailure == DocumentFiles::WebResult::Failure::Composition && !m_Shutdown) {
        DevMessage message; message.text = "Unable to save CMO file !"; ShowMessage(std::move(message));
    }
}

void EditorApp::RequestQuit() { Command("application.quit"); }





void EditorApp::FileFinished(const DocumentCommands::Result &result) {
    if (m_Shutdown) return;
    m_Editor.Attach(m_State.HasDocument() ? m_State.document->Token() : DocumentToken{},
                    m_State.document ? m_State.document->TopologyRevision() : 0, true, m_State.HasDocument());
    m_Execution.Synchronize();
    using A = DocumentCommands::Action;
    if (result.action == A::New && result.status == EDITOR_OK) {
        std::string error;
        if (!m_Layout.Start(m_Window, &error)) m_State.Log(LogLevel::Warning, "3D Layout: " + error);
    }
    if (!result.message.empty())
        m_State.Log(result.status == EDITOR_ERROR ? LogLevel::Error : LogLevel::Info, result.message);
    if ((result.action == A::Open || result.action == A::Revert) && result.opened.opened)
        CompleteOpen(result.path, result.opened);
    if (result.loaded.loaded) {
        m_State.selection.Clear();
        m_Canvas.Invalidate();
        if (result.action == A::Merge || result.action == A::MergeScene) RememberRecentFile(result.path);
        for (const auto &script : m_State.document->Scripts())
            if (std::find(result.loaded.objects.begin(), result.loaded.objects.end(), script.id) != result.loaded.objects.end()) {
                m_Panels.ShowScript(script.id, ElementRef(), 0, false);
                break;
            }
    }
    for (const auto &item : result.resources.items) {
        m_State.Log(item.loaded ? LogLevel::Info : LogLevel::Warning,
                    std::string(item.loaded ? "Loaded: " : "Load failed: ") + item.message + " (" + item.path + ")");
        if (item.loaded) {
            m_Canvas.Invalidate();
            if (result.action == A::LoadScripts && item.created && m_State.document->Resolve(item.created))
                m_Panels.ShowScript(item.created.object);
        }
    }
    if ((result.action == A::ExportObjects || result.action == A::ExportBehaviors) && result.exported.saved)
        m_State.Log(LogLevel::Info, "Saved " + result.path);
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
        m_State.Log(result.saved.saved ? LogLevel::Info : LogLevel::Error, text);
        if (result.action == A::Player) {
            if (!result.saved.saved) {
                DevMessage message;
                message.text = "Save Level Error: " + (report.errors.empty() ? std::string() : AnsiToUtf8(report.errors.front()));
                ShowMessage(std::move(message));
            }
        } else { m_SaveReportText = std::move(text); m_OpenSaveReport = true; }
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
    if (!m_Layout.Start(m_Window, &layoutError)) m_State.Log(LogLevel::Warning, "3D Layout: " + AnsiToUtf8(layoutError));
    RememberRecentFile(path);
    // NMO opens are imports into a new composition. Keep its editor state,
    // just as Import File does, rather than restoring the library's layout.
    if (m_State.document->CreatedFrom() == Document::Origin::LoadedComposition) {
        m_Layout.ApplyLayoutData(result.layout);
        ReopenSetups(result.layout);
        m_State.invalidLinks = result.invalid;
        if (result.removed) m_State.Log(LogLevel::Warning, std::to_string(result.removed) + " invalid link(s) removed after loading");
        if (!result.invalid.empty())
            m_State.Log(LogLevel::Warning, std::to_string(result.invalid.size()) + " invalid link(s): see the Links with Errors Explorer");
    }
    // Dev's Event Log line (language pack [File IO]).
    m_State.Log(LogLevel::Info, "File Loaded Successfully. (" + path + ")");
    // Dev refuses a file for the player ("This content can only be opened in
    // Players"); here its Scripts' Schematic is rebuilt from their graphs.
    if (IsPlayerFile(path))
        m_State.Log(LogLevel::Warning, "A file for the player: the Scripts' Schematic is rebuilt from their graphs "
                                       "(no comments, colors or saved layout)");
    if (!m_State.document->Scripts().empty()) m_Panels.ShowScript(m_State.document->Scripts().front().id, ElementRef(), 0, false);
    UpdateTitle();
}

void EditorApp::NewComposition(bool background) {
    Command("document.new");
    if (!background) {
        m_State.FinishRuntimeOperations();
        m_Editor.Attach(m_State.HasDocument() ? m_State.document->Token() : DocumentToken{},
                        m_State.document ? m_State.document->TopologyRevision() : 0, true, m_State.HasDocument());
    }
}

// Dev (sub_5C54C0) asks for the file, merges it and keeps the composition's
// undo history; the merge itself is not an undo step.
void EditorApp::MergeComposition(bool asScene, const std::string &path) {
    Command(asScene ? "document.merge-scene" : "document.merge", path);
}

void EditorApp::RememberRecentFile(const std::string &path) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    m_State.preferences.AddRecentFile(ec ? path : absolute.string());
    SavePreferences();
}

void EditorApp::CloseDocumentViews() {
    m_Workspace.DocumentChanging();
    m_Editor.NativeAvailable(false);
    m_Outputs.Stop();
    m_Setups.CloseAll();
    m_Tools.DocumentClosed();
    m_Panels.DocumentClosed();
    m_Layout.DocumentClosed(); // its working copies while they still exist
    m_Layout.ApplyLayoutData(LayoutInterfaceData());
    m_Canvas.DocumentClosed();
    ReleaseSnapshotTextures(); // the document frees its bitmaps
    UpdateTitle();
}

void EditorApp::DetachDocument(bool replaceContext) {
    if (DocumentInteractionActive()) throw std::runtime_error("Finish the current document interaction");
    m_DetachedLayout = LayoutDataToSave();
    m_DetachedDocument = m_State.document->Token();
    CloseDocumentViews();
    if (replaceContext) m_Layout.Stop();
}

void EditorApp::RestoreDocument() {
    if (!m_DetachedDocument.value || m_State.document->Token() != m_DetachedDocument) return;
    m_Editor.Attach(m_State.HasDocument() ? m_State.document->Token() : DocumentToken{},
                    m_State.document->TopologyRevision(), true, m_State.HasDocument());
    std::string error;
    if (!m_Layout.Start(m_Window, &error)) throw std::runtime_error("3D Layout: " + error);
    m_Layout.ApplyLayoutData(m_DetachedLayout, false);
    ReopenSetups(m_DetachedLayout);
    m_Canvas.Invalidate();
    UpdateTitle();
    m_DetachedDocument = {};
}

bool EditorApp::DocumentInteractionActive() const {
    return m_Canvas.Busy() || m_Layout.Busy() || m_Setups.HasPendingEdits() || m_Workspace.Interacting();
}



// The Setups of the 3D Layout's data open again (Dev.exe sub_4B46C0 ->
// sub_5CB9E0), in their order, the one in front last; a Curve Setup on its
// control point.
void EditorApp::ReopenSetups(const LayoutInterfaceData &data) {
    if (!data.present || !m_State.HasDocument()) return;
    CKContext *context = m_State.document->Context();
    std::vector<LayoutInterfaceData::Setup> setups = data.setups;
    std::stable_sort(setups.begin(), setups.end(),
                     [](const LayoutInterfaceData::Setup &a, const LayoutInterfaceData::Setup &b) { return a.values[1] < b.values[1]; });
    CK_ID front = 0;
    for (const LayoutInterfaceData::Setup &setup : setups) {
        CKObject *object = context->GetObject(setup.object);
        CKGUID guid;
        if (!object || !LayoutSetupGuid(object, guid)) continue;
        const CK_ID shown = CKCurve::Cast(object) && CKCurvePoint::Cast(context->GetObject(setup.other)) ? setup.other : setup.object;
        m_Setups.Open(shown);
        if (setup.values[2]) front = shown;
    }
    if (front) m_Setups.Open(front);
}

// The 3D Layout's data with the Setups open (sub_4B4E50: the GUID, the
// object, the Curve Setup's point, the window's state, its order, in front).
LayoutInterfaceData EditorApp::LayoutDataToSave() const {
    LayoutInterfaceData data = m_Layout.LayoutData();
    if (!m_State.HasDocument()) return data;
    CKContext *context = m_State.document->Context();
    data.setups.clear();
    int order = 2;
    for (const SetupWindows::OpenSetup &open : m_Setups.OpenSetups()) {
        LayoutInterfaceData::Setup setup;
        if (!LayoutSetupGuid(context->GetObject(open.object), setup.guid)) continue;
        setup.object = open.object;
        setup.other = open.other;
        setup.values[0] = 2; // the docked state Dev's files hold most
        setup.values[1] = order++;
        setup.values[2] = open.front ? 1 : 0;
        data.setups.push_back(setup);
    }
    return data;
}









void EditorApp::DropFiles(const std::vector<std::wstring> &paths, float x, float y) {
    if (Busy()) {
        m_State.Log(LogLevel::Info, "Dropped files not used: finish the dialog or the drag first");
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
        if (!m_State.HasDocument() || !m_Canvas.DropFile(wide, path, ImVec2(x, y)))
            m_State.Log(LogLevel::Info, "Dropped file not used: " + path +
                                            " (compositions open; images go on Script headers, .nms on graphs or bodies)");
    }
}

EditorResponse EditorApp::Command(const char *id, const std::string &path) {
    const EditorValue value{EditorLiteral("path"), EDITOR_TEXT_VALUE, {}, EditorText(path)};
    const EditorRequest request{m_Editor.Scope(), path.empty() ? nullptr : &value, path.empty() ? 0u : 1u};
    auto result = m_Editor.Execute(id, request);
    if (!result.message.empty()) m_State.Log(result.status == EDITOR_OK ? LogLevel::Info : LogLevel::Warning, result.message);
    return result;
}





void EditorApp::UpdateTitle() {
    // Documentation "Menu Bar": the level and scene current in the title
    // bar, and the path of the composition loaded.
    std::wstring title = L"NeMoDev";
    if (m_State.HasDocument()) {
        const std::string file = CurrentFilePath();
        const std::wstring name = file.empty() ? L"New Composition" : Utf8ToWide(file);
        std::wstring where;
        if (CKLevel *level = m_State.document->Context()->GetCurrentLevel()) {
            where = AnsiToWide(level->GetName() ? level->GetName() : "Level");
            CKScene *scene = level->GetCurrentScene();
            if (scene && scene != level->GetLevelScene())
                where += L" / " + AnsiToWide(scene->GetName() ? scene->GetName() : "Scene");
        }
        title += L" - " + (where.empty() ? L"" : L"[" + where + L"] ") + name + (m_State.document->Dirty() ? L" *" : L"");
    }
    if (title != m_Title && m_Window) {
        SDL_SetWindowTitle(m_Window, WideToUtf8(title).c_str());
        m_Title = title;
    }
}

void EditorApp::MainMenu() {
    PushDarkChrome();
    if (!ImGui::BeginMainMenuBar()) {
        PopDarkChrome();
        return;
    }
    const bool hasDocument = m_State.HasDocument();
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
                DevMessage message;
                message.text = std::string(function) + " function not available";
                message.title = "NeMoDev";
                ShowMessage(std::move(message));
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
        if (!m_State.preferences.recentFiles.empty()) {
            ImGui::Separator();
            const std::vector<std::string> recent = m_State.preferences.recentFiles; // opening reorders the list
            for (size_t i = 0; i < recent.size(); ++i) {
                const std::string label = std::to_string(i + 1) + ") " + AnsiToUtf8(recent[i]);
                if (ImGui::MenuItem(label.c_str())) {
                    Command("document.open", recent[i]);
                }
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
        if (ImGui::MenuItem("Path Manager")) m_Tools.OpenPathManager();
        ImGui::MenuItem("Parameter Debugger", nullptr, &m_Tools.showParameterDebugger);
        ImGui::Separator();
        if (ImGui::MenuItem("Close All Setups and Managers")) CloseSetupsAndManagers();
        ImGui::EndMenu();
    }
    // Dev's Options menu (screenshot).
    if (ImGui::BeginMenu("Options")) {
        if (ImGui::MenuItem("General Preferences", "Ctrl+P")) m_Tools.OpenGeneralPreferences();
        ImGui::Separator();
        const Debugger *debugger = m_State.debugger;
        if (ImGui::MenuItem(debugger && debugger->trace ? "Deactivate Trace Mode" : "Activate Trace Mode", m_Workspace.ShortcutLabel("runtime.toggle-trace"), false,
                            m_Editor.CommandState("runtime.toggle-trace", {m_Editor.Scope(), nullptr, 0}) == EDITOR_OK))
            Command("runtime.toggle-trace");
        // Hide/Show Dynamic Objects (documentation "Options Menu"): the
        // objects and Scripts Building Blocks create, in the Level Manager,
        // the Schematic and the Profiler (General Preferences' "Show Dynamic Objects").
        {
            const bool dynamic = m_State.preferences.Bool("3D Layout\\Show Dynamic Objects");
            if (ImGui::MenuItem(dynamic ? "Hide Dynamic Objects" : "Show Dynamic Objects")) {
                m_State.preferences.Set("3D Layout\\Show Dynamic Objects", dynamic ? "0" : "1");
                SavePreferences();
                m_Panels.RefreshStack();
            }
        }
        if (ImGui::MenuItem("Unused Object Explorer", nullptr, false, hasDocument)) m_Tools.OpenUnusedObjects();
        if (ImGui::MenuItem("Refresh Windows")) {
            // Documentation "Options Menu": refreshes the windows "and resets
            // the three main tabbed windows to their default size and position"
            // (the other windows docked stay in their tab rows).
            m_Canvas.Invalidate();
            if (hasDocument) m_Canvas.Refresh();
            m_Panels.RefreshStack();
            m_ResetLayout = true;
            m_CloseSeparated = false;
        }
        // "resize all windows to their initial size, and close all separated windows".
        if (ImGui::MenuItem("Reset Interface")) {
            m_ResetLayout = true;
            m_CloseSeparated = true;
        }
        if (ImGui::MenuItem("Building Block Version Test", nullptr, false, hasDocument)) m_Tools.RunVersionTest();
        ImGui::Separator();
        if (ImGui::MenuItem("Installed Plugins")) m_Tools.OpenInstalledPlugins();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        m_Workspace.CommandMenu("help.reference");
        if (ImGui::MenuItem("About Virtools...")) m_Tools.OpenAbout();
        ImGui::EndMenu();
    }
    m_Workspace.Menu();
    ImGui::EndMainMenuBar();
    PopDarkChrome();
}

#ifdef VIRTOOLSDEV_UI_TESTS
void EditorApp::LogUndoTop() {
    m_State.Log(LogLevel::Info, m_State.History().CanUndo() ? "Last undo step: " + m_State.History().UndoLabel()
                                                       : std::string("Last undo step: (none)"));
}
#endif

void EditorApp::CreateObject(LayoutObject kind) {
    if (!m_State.HasDocument()) return;
    // Create Portal: around the object selected (the 3D Layout's selection is the Level Manager's).
    CK_ID selection = 0;
    if (LevelSelection().size() == 1) selection = *LevelSelection().begin();
    else if (m_State.objectSelection.Objects().size() == 1) selection = m_State.objectSelection.Objects().front();
    // Dev places new objects by the 3D Layout's camera.
    CKCamera *view = m_Layout.View().Camera();
    LayoutObjectOptions options;
    options.showCone = kind == LayoutObject::Camera ? m_State.preferences.Bool("3D Layout\\Auto Show Camera Cone")
                                                  : m_State.preferences.Bool("3D Layout\\Auto Show Light Cone");
    options.showInPlayerMode = m_State.preferences.Bool("3D Layout\\Auto Show Entities In Player Mode");
    static const char *commands[] = {"object.create-camera", "object.create-light", "object.create-frame3d",
                                     "object.create-curve", "object.create-grid", "object.create-frame2d",
                                     "object.create-material", "object.create-texture", "object.create-portal"};
    const auto index = static_cast<size_t>(kind);
    if (index >= sizeof(commands) / sizeof(commands[0])) return;
    const EditorValue values[] = {
        {EditorLiteral("target"), EDITOR_OBJECT, m_State.document->Reference(selection)},
        {EditorLiteral("camera"), EDITOR_OBJECT, m_State.document->Reference(view ? view->GetID() : 0)},
        {EditorLiteral("show-cone"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(options.showCone)},
        {EditorLiteral("show-in-player"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(options.showInPlayerMode)}};
    const auto result = m_Editor.Execute(commands[index], {m_Editor.Scope(), values, 4});
    if (result.status != EDITOR_OK) {
        if (result.status != EDITOR_NO_CHANGE && result.status != EDITOR_QUEUED)
            m_State.Log(LogLevel::Warning, "Create: " + result.message);
        return;
    }
    ObjectRef created{};
    for (const auto &field : result.fields) if (field.key == "created") created = field.object;
    if (!created) return;
    const EditorValue target{EditorLiteral("target"), EDITOR_OBJECT, created};
    const auto details = m_Editor.Query("object.details", {m_Editor.Scope(), &target, 1});
    if (details.status != EDITOR_OK) return;
    std::string name, type;
    for (const auto &field : details.fields) {
        if (field.key == "name") name = field.text;
        if (field.key == "class") type = field.text;
    }
    m_State.Log(LogLevel::Info, type + " '" + name + "' created.");
    for (const auto &field : result.fields)
        if (field.key == "camera" && field.integer) m_Layout.View().SetCamera(created.object);
    if (m_State.preferences.Bool("3D Layout\\Auto Show Setup")) m_State.events.Post(ShowObjectSetup{created});
}

bool EditorApp::Busy() const {
    return m_State.runtimeOperation.Busy() || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || m_Canvas.Busy() ||
           m_Layout.Busy();
}

void EditorApp::GlobalShortcuts() {
    const ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput || Busy()) return;
    // The Schematic takes F1 (a Building Block's help) and Ctrl+T itself;
    // the Level Manager F3 (its Find Item).
    const ImGuiWindow *focused = ImGui::GetCurrentContext()->NavWindow;
    const bool levelManager = focused && focused->RootWindow && m_Workspace.Focused() == m_Workspace.Role("main.level");
    const bool schematic = focused && focused->RootWindow && std::strcmp(focused->RootWindow->Name, SCHEMATIC_WINDOW) == 0;
    // F3 / Shift+F3: the Schematic's Find (once a text was asked), else the
    // Explorer's results;
    // Ctrl+F3 in the Schematic: the selected element's name.
    if (!levelManager && ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
        if (schematic && io.KeyCtrl) m_Canvas.FindSelected(io.KeyShift);
        else if (schematic && m_Canvas.Finding()) m_Canvas.FindNext(io.KeyShift);
        else if (!io.KeyCtrl) m_Panels.FindNext(io.KeyShift ? -1 : 1);
    }
    m_Workspace.CommandShortcuts();
    if (!io.KeyCtrl) return;
    // Ctrl+Alt+F12: All CK Object In Memory.
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F12, false)) m_Tools.OpenAllObjects();
    // Dev: Ctrl+P General Preferences, Ctrl+T Trace Mode.
    if (ImGui::IsKeyPressed(ImGuiKey_P, false)) m_Tools.OpenGeneralPreferences();
    // Dev: Ctrl+L Load Composition (Ctrl+O kept), Ctrl+Shift+S Save Version,
    // Ctrl+Shift+Z Revert to Saved.
    // Ctrl+Shift+F the Schematic Explorer; Ctrl+F the Schematic's Find (the
    // Level Manager's own there), else the Explorer.
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !levelManager) {
        if (schematic && !io.KeyShift) m_Canvas.OpenFind();
        else ShowSchematicExplorer();
    }
}



void EditorApp::SaveReportDialog() {
    if (m_OpenSaveReport) {
        ImGui::OpenPopup("Save report");
        m_OpenSaveReport = false;
    }
    if (!ImGui::BeginPopupModal("Save report", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(560.0f);
    ImGui::TextUnformatted(m_SaveReportText.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("OK", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::ShowSchematicExplorer() {
    m_ShowExplorer = true;
    m_Panels.FocusExplorer();
}

void EditorApp::DispatchEvents() {
    // The stack follows the Scripts (an undo bringing one back or taking it away) and the filters.
    const std::uint64_t scripts = m_State.HasDocument() ? m_State.document->ScriptsRevision() : 0;
    bool refresh = scripts != m_ScriptsSeen;
    for (const EditorEvent &event : m_State.events.Take()) {
        if (const ShowElement *show = std::get_if<ShowElement>(&event)) {
            m_Panels.ShowScript(show->script, show->ref, show->focusBehavior);
            m_ToFront = SCHEMATIC_WINDOW;
        } else if (const ShowObjectSetup *setup = std::get_if<ShowObjectSetup>(&event)) {
            if (m_State.HasDocument())
                if (const auto id = m_State.document->NativeId(setup->object)) m_Setups.Open(id);
        } else if (const ShowInLevelManager *row = std::get_if<ShowInLevelManager>(&event)) {
            if (m_State.HasDocument())
                if (const auto id = m_State.document->NativeId(row->object)) {
                    const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, row->object};
                    m_Editor.Execute("scene.show-object", {m_Editor.Scope(), &target, 1});
                }
        } else if (const OpenAddAttribute *add = std::get_if<OpenAddAttribute>(&event)) {
            const auto refs = m_State.document->References(add->targets);
            const EditorValue args[] = {ObjectsArgument("objects", refs), IntegerArgument("more", add->more)};
            m_Editor.Execute("attribute.choose", {m_Editor.Scope(), args, 2});
        } else if (const ConfirmDeleteObjects *remove = std::get_if<ConfirmDeleteObjects>(&event)) {
            const auto objects = ObjectsArgument("objects", remove->objects);
            m_Editor.Execute("object.confirm-delete", {m_Editor.Scope(), &objects, 1});
        } else if (const ShowGeneralPreferences *preferences = std::get_if<ShowGeneralPreferences>(&event)) {
            m_Tools.OpenGeneralPreferences(preferences->page);
        } else if (std::holds_alternative<FocusSchematic>(event)) {
            m_ToFront = SCHEMATIC_WINDOW;
        } else if (std::holds_alternative<OpenSchematicExplorer>(event)) {
            ShowSchematicExplorer();
        } else if (std::holds_alternative<OpenLinkProperties>(event)) {
            m_Tools.OpenLinkProperties();
        } else if (std::holds_alternative<RefreshScriptStack>(event)) {
            refresh = true;
        }
    }
    if (refresh) {
        m_ScriptsSeen = scripts;
        m_Panels.RefreshStack();
    }
    // Focusing a window closes the popups of the others: the window comes
    // forward once they are closed (the 3D Layout's drop asks for the
    // Building Block's parameters, then shows it in the Schematic).
    if (m_ToFront && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        ImGui::SetWindowFocus(m_ToFront);
        m_ToFront = nullptr;
    }
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
    ImGui::Text("%c  %s", spinner[static_cast<int>(ImGui::GetTime() * 8) % 4], m_State.runtimeOperation.Label().c_str());
    ImGui::TextUnformatted("Please wait...");
    ImGui::End();
    ImGui::SetMouseCursor(ImGuiMouseCursor_Wait);
}

void EditorApp::Frame() {
    m_State.PollRuntimeOperations();
#ifdef VIRTOOLSDEV_UI_TESTS
    if (m_StartupAfterLoads && !m_State.runtimeOperation.Busy()) {
        const bool opened = m_StartupAfterLoads == 2;
        m_StartupAfterLoads = 0;
        RunStartupActions(opened);
    }
#endif
    m_FileCommands.Poll();
    m_Outputs.Poll();
    m_ResourceCommands.Poll();
    if (m_State.runtimeOperation.Busy()) { RuntimeOperationFrame(); return; }
    m_State.DispatchFileRequests();
    if (m_State.runtimeOperation.Busy()) { RuntimeOperationFrame(); return; }
    if (m_State.HasDocument()) m_State.objectSelection.Validate(*m_State.document);
    m_Editor.Attach(m_State.HasDocument() ? m_State.document->Token() : DocumentToken{},
                    m_State.HasDocument() ? m_State.document->TopologyRevision() : 0, true,
                    m_State.HasDocument());
    ObjectRef selected{};
    if (!m_State.objectSelection.References().empty()) selected = m_State.objectSelection.References().front();
    else if (m_State.HasDocument() && LevelSelection().size() == 1)
        selected = m_State.document->Reference(*LevelSelection().begin());
    m_Editor.SetSelection(selected);
    // The current scene and the changes show in the title (cheap: set only when it changes).
    UpdateTitle();
    MainMenu();
    if (m_State.runtimeOperation.Busy()) { RuntimeOperationFrame(); return; }
    // The editor's former name: the saved window layouts know this ID.
    const ImGuiID dockspace = ImGui::GetID("SchematicEditorDockSpace");
    StatusBar();
    const auto layout = m_Workspace.UpdateLayout(dockspace, m_ResetLayout, m_CloseSeparated);
    if (!layout.closeLegacy.empty()) DevCloseSeparated(layout.closeLegacy);
    if (layout.changed) m_FocusSchematic = 3;
    m_CloseSeparated = false;
    m_ResetLayout = false;
    ImGui::DockSpaceOverViewport(dockspace, ImGui::GetMainViewport());

    DispatchEvents();
    // Dev's main windows have no close button.
    m_Canvas.Draw(SCHEMATIC_WINDOW, nullptr);
    // The canvas size is known only after a docked frame.
#ifdef VIRTOOLSDEV_UI_TESTS
    if (m_FitFrames > 0 && --m_FitFrames == 0) m_Canvas.FitAll();
#endif
    DuplicateNamesDialog();
    MessageDialog();
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
    if (m_ShowExplorer) m_Panels.ExplorerPanel(&m_ShowExplorer);
    {
        m_Layout.Draw(0, m_FocusSchematic <= 0);
        // Setup tabs dock with the Schematic (Dev's tab row).
        ImGuiWindow *schematic = ImGui::FindWindowByName(SCHEMATIC_WINDOW);
        m_Setups.Draw(schematic ? schematic->DockId : 0);
        m_Tools.Draw(m_Workspace.RegionDock("resources"), m_FocusSchematic <= 0);
    }
    const auto &execution = m_Execution.State();
    m_SceneCommands.Publish(m_Editor.Revision(), execution.frames, execution.state, m_State.preferences.Bool("3D Layout\\Show Dynamic Objects"));
    m_Workspace.Draw();
    if (m_State.showDebugger) m_Panels.DebuggerPanel(&m_State.showDebugger);
    if (m_Execution.Update(0.010)) m_Canvas.RefreshValues();
#ifdef VIRTOOLSDEV_UI_TESTS
    const bool settled = m_FocusSchematic <= 0;
#endif
    if (m_FocusSchematic > 0) {
        // The front tabs of Dev's start (the Level Manager in front of the
        // Schematic, focused last); repeated because windows appearing later
        // take the focus.
        ImGui::SetWindowFocus("3D Layout");
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
    SaveReportDialog();
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
    if(m_State.HasDocument())for(const auto &field:result.fields)if(field.key=="selection")for(auto ref:field.objects)if(auto id=m_State.document->NativeId(ref))selection.insert(id);
    return selection;
}
bool EditorApp::RegisterSceneNavigation(std::string &error){
    const EditorParameter target{EDITOR_TEXT("target"),EDITOR_OBJECT};
    const EditorParameter show[]={target,{EDITOR_TEXT("focus"),EDITOR_INTEGER_VALUE,1}};
    const EditorParameter drop[]={{EDITOR_TEXT("objects"),EDITOR_OBJECT_LIST},{EDITOR_TEXT("prototype"),EDITOR_INTEGER_VALUE}};
    const EditorParameter expand{EDITOR_TEXT("expand"),EDITOR_INTEGER_VALUE};
    EditorCommandDesc commands[]={
        {sizeof(EditorCommandDesc),EDITOR_TEXT("object.open-setup"),EDITOR_TEXT("builtin.navigation"),EDITOR_TEXT("Setup"),&target,1,nullptr,this,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("scene.show-object"),EDITOR_TEXT("builtin.navigation"),EDITOR_TEXT("Show in Level Manager"),&target,1,nullptr,this,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("script.show"),EDITOR_TEXT("builtin.navigation"),EDITOR_TEXT("Show Script"),show,2,nullptr,this,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("script.refresh"),EDITOR_TEXT("builtin.navigation"),EDITOR_TEXT("Refresh Scripts"),nullptr,0,nullptr,this,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("scene.drop-behavior"),EDITOR_TEXT("builtin.navigation"),EDITOR_TEXT("Drop Behavior"),drop,2,nullptr,this,EDITOR_DOCUMENT,EDITOR_DOCUMENT_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("scene.expand-level"),EDITOR_TEXT("builtin.navigation"),EDITOR_TEXT("Expand Level Manager"),&expand,1,nullptr,this,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE}};
    commands[0].run=[](void*data,const EditorHost*,const EditorRequest*request,EditorSink){
        auto &app=*static_cast<EditorApp*>(data);const auto id=app.m_State.document->NativeId(request->values[0].object);if(!id)return EDITOR_STALE;app.m_Setups.Open(id);return EDITOR_OK;};
    commands[1].run=[](void*data,const EditorHost*host,const EditorRequest*request,EditorSink sink){
        auto &app=*static_cast<EditorApp*>(data);return host->navigateView(host->data,EditorString EDITOR_TEXT("scene.level"),app.OpenSceneView("level"),request,sink);};
    commands[2].run=[](void*data,const EditorHost*,const EditorRequest*request,EditorSink){
        auto &app=*static_cast<EditorApp*>(data);const auto id=app.m_State.document->NativeId(request->values[0].object);if(!id)return EDITOR_STALE;
        const bool focus=request->values[1].integer!=0;app.m_Panels.ShowScript(id,{},0,focus);if(focus)app.m_ToFront=SCHEMATIC_WINDOW;return EDITOR_OK;};
    commands[3].run=[](void*data,const EditorHost*,const EditorRequest*,EditorSink){static_cast<EditorApp*>(data)->m_Panels.RefreshStack();return EDITOR_OK;};
    commands[4].run=[](void*data,const EditorHost*,const EditorRequest*request,EditorSink){
        auto &app=*static_cast<EditorApp*>(data);auto &state=app.m_State;std::vector<ObjectRef> targets;
        const auto &objects=request->values[0].objects;for(uint32_t i=0;i<objects.count;++i){if(!state.document->Resolve(objects.data[i]))return EDITOR_STALE;targets.push_back(objects.data[i]);}
        const auto prototype=request->values[1].integer;const CKGUID guid(CKDWORD(prototype>>32),CKDWORD(prototype));
        auto create=[&]()->std::pair<CK_ID,CK_ID>{CK_ID script=0,block=0;
            for(auto ref:targets){auto*object=state.document->Resolve(ref);EditResult result=EditResult::NoChange();
                if(auto*behavior=CKBehavior::Cast(object)){if(state.hiddenScripts.count(behavior->GetID())){state.Log(LogLevel::Warning,"Cannot drop on script with hidden graph!");continue;}script=behavior->GetID();result=DropBuildingBlockInto(*state.document,script,guid);}
                else if(object)result=DropBuildingBlockOn(*state.document,object->GetID(),guid,state.hiddenScripts,&script);
                if(result.ok)block=result.created;else if(!result.message.empty())state.Log(LogLevel::Warning,AnsiToUtf8(result.message));}
            return {script,block};};
        std::pair<CK_ID,CK_ID> made;
        if(targets.size()==1)made=app.m_Layout.DropAsking(create);else{EditGroup group(*state.document,"Drop Behavior");made=create();}
        if(!made.second)return EDITOR_REFUSED;state.RememberBlock(guid);state.events.Post(ShowElement{made.first,ElementRef{ElementKind::Behavior,made.first,made.second},made.second});return EDITOR_OK;};
    commands[5].run=[](void*data,const EditorHost*host,const EditorRequest*request,EditorSink sink){auto&app=*static_cast<EditorApp*>(data);return host->navigateView(host->data,EditorString EDITOR_TEXT("scene.level"),app.OpenSceneView("level"),request,sink);};
    EditorModuleDesc module{};module.size=sizeof(module);module.version=EDITOR_VERSION;module.id=EDITOR_TEXT("scene.navigation");module.commands=commands;module.commandCount=6;return m_Editor.Register(module,error);
}
} // namespace vdev

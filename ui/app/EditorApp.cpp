#include "ui/app/EditorApp.h"
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

void EditorApp::HandleEvent(const SDL_Event &event) {
    m_Layout.HandleEvent(event);
    if (!m_Window || event.type != SDL_EVENT_WINDOW_FOCUS_LOST ||
        event.window.windowID != SDL_GetWindowID(m_Window) || m_Layout.FullScreen()) return;
    if (m_State.debugger && m_State.debugger->GetState() == Debugger::State::Running)
        m_State.debugger->Pause();
}

namespace {

const char *SCHEMATIC_WINDOW = "Schematic";

const wchar_t *FILE_FILTER = L"Virtools compositions (*.cmo;*.nmo;*.vmo)\0*.cmo;*.nmo;*.vmo\0All files (*.*)\0*.*\0";

FileDialogOptions FileDialog(SDL_Window * owner, bool save, const std::string &initial, const wchar_t *filter = FILE_FILTER,
                       const wchar_t *extension = L"cmo", const wchar_t *title = nullptr) {
    FileDialogOptions options;
    options.save = save;
    options.owner = owner;
    options.filter = filter;
    options.extension = extension;
    options.title = title;
    options.initial = Utf8ToWide(initial);
    return options;
}

// Import File filter (Dev.exe sub_5D55F0 with the model readers): "All Known
// Types (*.a;*.b)" then each reader, Virtools compositions, scripts and
// player files left out.
std::wstring ImportFilter() {
    std::vector<std::pair<std::string, std::string>> readers; // description, extension
    if (CKPluginManager *plugins = CKGetPluginManager())
        for (int i = 0; i < plugins->GetPluginCount(CKPLUGIN_MODEL_READER); ++i) {
            CKPluginEntry *entry = plugins->GetPluginInfo(CKPLUGIN_MODEL_READER, i);
            if (!entry) continue;
            std::string extension = entry->m_PluginInfo.m_Extension.m_Data;
            for (char &c : extension) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (extension.empty() || extension == "vmo" || extension == "nms" || extension == "cmo") continue;
            readers.emplace_back(entry->m_PluginInfo.m_Description.CStr() ? entry->m_PluginInfo.m_Description.CStr() : "",
                                 extension);
        }
    std::string all;
    for (const auto &reader : readers) all += (all.empty() ? "*." : ";*.") + reader.second;
    std::string filter = "All Known Types (" + all + ")";
    filter.push_back('\0');
    filter += all;
    filter.push_back('\0');
    for (const auto &reader : readers) {
        filter += reader.first + " (*." + reader.second + ")";
        filter.push_back('\0');
        filter += "*." + reader.second;
        filter.push_back('\0');
    }
    filter += "All files (*.*)";
    filter.push_back('\0');
    filter += "*.*";
    filter.push_back('\0');
    return AnsiToWide(filter);
}

const wchar_t *RESOURCE_FILTER = L"Virtools Resources (*.rsc)\0*.rsc\0";

} // namespace

bool EditorApp::Initialize(SDL_Window * window, const std::string &runtimeDirectory, const std::string &fileToOpen,
                           const StartupOptions &startup) {
    m_Window = window;
    m_Startup = startup;
    // The Event Log's first line (Dev.exe sub_4229D0, language pack 141).
    m_State.Log(LogLevel::Info, "Virtools Started.");
    m_State.log.back().status = false;
    // CK text retains its native bytes; file paths remain UTF-8.
    SetLossyTextHandler([this](const std::string &text) {
        m_State.Log(LogLevel::Warning, "Text conversion failed; original CK bytes kept: " + text);
    });
    // Preferences: our file, else Dev's own settings (read-only), else Dev's
    // defaults (isolated: the defaults).
    Preferences &preferences = m_State.preferences;
    const bool loaded = !startup.isolated && preferences.Load(PreferencesPath());
    if (!startup.isolated && !loaded) {
        const int imported = preferences.ImportFromDev() + preferences.ImportDevIni(runtimeDirectory);
        if (imported) m_State.Log(LogLevel::Info, "Imported " + std::to_string(imported) + " Virtools Dev preferences");
    } else if (loaded && !preferences.settingsSaved) {
        // A preferences file from before the General Preferences: Dev's values for the rest.
        const int imported = preferences.ImportDevSettings() + preferences.ImportDevIni(runtimeDirectory);
        if (imported) m_State.Log(LogLevel::Info, "Imported " + std::to_string(imported) + " Virtools Dev General Preferences");
    }
    ApplyPreferences();
    m_State.view.showAllScripts = startup.showAllScripts;
    m_State.runtimeDirectory = runtimeDirectory;
    m_Tools.applyPreferences = [this] {
        ApplyPreferences();
        ApplyRuntimeSettings();
        SavePreferences();
    };
    m_Tools.levelTarget = [this]() -> CK_ID {
        return m_Panels.LevelSelection().size() == 1 ? *m_Panels.LevelSelection().begin() : 0;
    };
    std::string error;
    m_State.host.SetRenderEngine(preferences.Text("CK2 Settings\\Render Engine"));
    const bool started = m_State.host.Start(runtimeDirectory, [this](LogLevel level, const std::string &message) {
        m_State.Log(level, message);
    }, &error, window);
    if (!started) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NemoDev", ("Cannot start the Virtools runtime:\n" + error).c_str(), window);
        return false;
    }
    m_State.catalog.Refresh();
    m_State.document = std::make_unique<Document>(m_State.host);
    m_State.debugger = std::make_unique<Debugger>(*m_State.document);
    // Dev's paths (HKCU\Software\Virtools\Data Paths, read-only) until the
    // editor has its own.
    if (!startup.isolated && !preferences.pathsSaved && preferences.ImportDevPaths())
        m_State.Log(LogLevel::Info, "Path Manager: Virtools Dev's paths");
    ApplyRuntimeSettings();
    m_HideFps = m_State.preferences.Bool("Time Settings\\Hide Fps");
    // The 3D Layout's render context (Dev makes it with its main window).
    m_Layout.createObject = [this](LayoutObject kind) { CreateObject(kind); };
    m_Layout.play = [this](LayoutWindow::PlayCommand command) {
        Debugger *debugger = m_State.debugger.get();
        if (!m_State.HasDocument() || !debugger) return;
        switch (command) {
        case LayoutWindow::PlayCommand::PlayStop:
            if (debugger->GetState() == Debugger::State::Running) debugger->Pause();
            else debugger->Play();
            break;
        case LayoutWindow::PlayCommand::Step: debugger->StepFrame(); break;
        case LayoutWindow::PlayCommand::Reset: debugger->Reset(); break;
        case LayoutWindow::PlayCommand::Pause:
            if (debugger->GetState() == Debugger::State::Running) debugger->Pause();
            break;
        }
    };
    m_Panels.dropAsking = [this](const std::function<std::pair<CK_ID, CK_ID>()> &drop) { return m_Layout.DropAsking(drop); };
    std::string layoutError;
    if (!m_Layout.Start(window, &layoutError)) m_State.Log(LogLevel::Warning, "3D Layout: " + AnsiToUtf8(layoutError));
    for (const std::string &resource : std::vector<std::string>(preferences.dataResources))
        m_Tools.OpenDataResource(resource, false);
    for (const std::string &resource : m_Startup.resources) m_Tools.OpenDataResource(resource, false);
    if (!m_Startup.resourceFolder.empty()) m_Tools.SelectResourceFolder(m_Startup.resourceFolder);
    if (m_Startup.newComposition) NewComposition();
    const bool opened = !fileToOpen.empty() && OpenFile(fileToOpen);
    // Dev starts on a new composition (its 3D Layout shows the new scene's
    // gray), saying nothing on the status bar.
    if (!m_State.HasDocument()) {
        const size_t logged = m_State.log.size();
        NewComposition();
        for (size_t i = logged; i < m_State.log.size(); ++i) m_State.log[i].status = false;
    }
    RunStartupActions(opened);
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
    if (!m_Startup.isolated) m_State.preferences.Save(PreferencesPath());
}

void EditorApp::ApplyPreferences() {
    const Preferences &p = m_State.preferences;
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

void EditorApp::ImportFile(Document::ImportMode mode, const std::string &file) {
    if (!m_State.HasDocument()) return;
    static const wchar_t *const titles[] = {L"Import File", L"Import as Character", L"Import as Place", L"Import as Scene"};
    std::string path = file;
    if (path.empty()) {
        const std::wstring filter = ImportFilter();
        m_State.RequestFile(FileDialog(m_Window, false, {}, filter.c_str(), nullptr, titles[static_cast<int>(mode)]),
            [this, mode](const FileDialogResult &result) {
                if (result.status == FileDialogResult::Status::Selected) ImportFile(mode, result.paths.front());
            });
        return;
    }
    if (path.empty()) return;
    // Dev loads with CK_LOAD_DODIALOG: Duplicate Names Found first.
    m_State.LoadAskingDuplicates(path, [this, mode, path](const std::vector<DuplicateName> *duplicates) {
        if (!m_State.HasDocument()) return;
        std::string error;
        std::vector<CK_ID> imported;
        if (!m_State.document->Import(path, mode, &error, &imported, duplicates, m_State.preferences.NewSceneEnvironment())) {
            m_State.Log(LogLevel::Error, error);
            return;
        }
        m_State.selection.Clear();
        m_Canvas.Invalidate();
        for (const ScriptInfo &script : m_State.document->Scripts())
            if (std::find(imported.begin(), imported.end(), script.id) != imported.end()) {
                m_Panels.ShowScript(script.id, ElementRef(), 0, false);
                break;
            }
        UpdateTitle();
    });
}

void EditorApp::OpenDataResourceDialog() {
    m_State.fileDialogs.Request(FileDialog(m_Window, false, {}, RESOURCE_FILTER, L"rsc", L"Open Data Resource..."),
        [this](const FileDialogResult &result) {
            if (result.status == FileDialogResult::Status::Selected) m_Tools.OpenDataResource(result.paths.front());
            else if (result.status == FileDialogResult::Status::Error) m_State.Log(LogLevel::Error, result.error);
        });
}

void EditorApp::CreateDataResourceDialog() {
    m_State.fileDialogs.Request(FileDialog(m_Window, true, {}, RESOURCE_FILTER, L"rsc", L"Create Data Resource..."),
        [this](const FileDialogResult &result) {
            if (result.status == FileDialogResult::Status::Selected) m_Tools.CreateNewDataResource(result.paths.front());
            else if (result.status == FileDialogResult::Status::Error) m_State.Log(LogLevel::Error, result.error);
        });
}

void EditorApp::OpenOnlineReference() {
    std::string error;
    if (!OpenHelpTopic("VirtoolsDevHelp.html", &error)) m_State.Log(LogLevel::Warning, "Online Reference: " + error);
}

// Dev keeps its default windows (3D Layout, Building Blocks, Level Manager,
// Schematic, Data Resources).
void EditorApp::CloseSetupsAndManagers() {
    m_Setups.CloseAll();
    m_State.showAttributeManager = false;
    m_State.showHierarchy = false;
    m_State.showDebugger = false;
    m_Tools.CloseManagers();
}

void EditorApp::Shutdown() {
    m_State.fileDialogs.Close();
    // Toolbar toggles are preferences too.
    Preferences &p = m_State.preferences;
    p.automaticLink = m_State.view.autoLink;
    p.showControlPoints = m_State.view.showControlPoints;
    p.showLinkDelay = m_State.view.showLinkInfo;
    p.showPriorities = m_State.view.showPriorities;
    p.showScriptLeftPart = m_State.view.showHeaders;
    SavePreferences();
    if (m_State.document) CloseDocument();
    m_State.debugger.reset();
    m_Layout.Stop();
    m_State.document.reset();
    m_State.host.Stop();
    // The textures every window draws with, before the device goes (Main.cpp).
    ReleaseDevBitmaps();
    ReleaseSnapshotTextures();
    ReleaseRetiredTextures();
    SetLossyTextHandler(nullptr);
}

void EditorApp::RequestQuit() {
    BeginAction(PendingAction::Quit);
}

bool EditorApp::OpenFile(const std::string &path) {
    CloseDocument();
    std::string error;
    if (!m_State.document->Open(path, &error)) {
        m_State.Log(LogLevel::Error, error);
        return false;
    }
    m_LastSavedPath.clear();
    RememberRecentFile(path);
    const LayoutInterfaceData layout = ReadLayoutInterfaceData(m_State.document->Context());
    m_Layout.ApplyLayoutData(layout);
    ReopenSetups(layout);
    m_State.objectPictures = ReadObjectPictures(*m_State.document);
    m_State.objectPicturesChanged = false;
    // Dev's Event Log line (language pack [File IO]).
    m_State.Log(LogLevel::Info, "File Loaded Successfully. (" + path + ")");
    // Dev refuses a file for the player ("This content can only be opened in
    // Players"); here its Scripts' Schematic is rebuilt from their graphs.
    if (IsPlayerFile(path))
        m_State.Log(LogLevel::Warning, "A file for the player: the Scripts' Schematic is rebuilt from their graphs "
                                       "(no comments, colors or saved layout)");
    ValidateLinks();
    if (!m_State.document->Scripts().empty()) m_Panels.ShowScript(m_State.document->Scripts().front().id, ElementRef(), 0, false);
    m_State.History().MarkSavePoint(); // undone back to as it was opened: unmodified
    UpdateTitle();
    return true;
}

// Dev.exe ValidateAndFinalizeLinks after loading: the stored links that are
// not valid (Test Parameter Link after Loading also tests the parameter
// types) are removed (Remove Invalid Link after Loading) or kept in the
// Invalid Link Color and listed by the Links with Errors Explorer.
void EditorApp::ValidateLinks() {
    m_State.invalidLinks.clear();
    if (!m_State.HasDocument()) return;
    const Preferences &p = m_State.preferences;
    std::vector<InvalidLink> invalid =
        FindInvalidLinks(*m_State.document, p.Bool("Schematic\\Test Parameter Link after Loading"));
    if (invalid.empty()) return;
    if (p.Bool("Schematic\\Remove Invalid Link after Loading")) {
        const int removed = RemoveInvalidLinks(*m_State.document, invalid);
        m_Canvas.Invalidate();
        m_State.Log(LogLevel::Warning, std::to_string(removed) + " invalid link(s) removed after loading");
        return;
    }
    m_State.Log(LogLevel::Warning, std::to_string(invalid.size()) + " invalid link(s): see the Links with Errors Explorer");
    m_State.invalidLinks = std::move(invalid);
}

void EditorApp::NewComposition() {
    CloseDocument();
    std::string error;
    if (!m_State.document->New(&error, m_State.preferences.NewSceneEnvironment())) m_State.Log(LogLevel::Error, error);
    else m_State.History().MarkSavePoint();
    UpdateTitle();
}

// Dev (sub_5C54C0) asks for the file, merges it and keeps the composition's
// undo history; the merge itself is not an undo step.
void EditorApp::MergeComposition(bool asScene, const std::string &file) {
    if (!m_State.HasDocument()) return;
    if (file.empty()) {
        m_State.RequestFile(FileDialog(m_Window, false, {}), [this, asScene](const FileDialogResult &result) {
            if (result.status == FileDialogResult::Status::Selected) MergeComposition(asScene, result.paths.front());
        });
        return;
    }
    const std::string path = file;
    // Dev loads with CK_LOAD_DODIALOG: Duplicate Names Found first.
    m_State.LoadAskingDuplicates(path, [this, asScene, path](const std::vector<DuplicateName> *duplicates) {
        if (!m_State.HasDocument()) return;
        std::string error;
        std::vector<CK_ID> merged;
        if (!m_State.document->Merge(path, asScene, &error, &merged, duplicates)) {
            m_State.Log(LogLevel::Error, error);
            return;
        }
        m_State.selection.Clear();
        m_Canvas.Invalidate();
        RememberRecentFile(path);
        // The merged file's pictures join (Dev.exe sub_5C54C0 -> sub_5D1D60).
        std::map<CK_ID, void *> pictures = ReadObjectPictures(*m_State.document);
        for (const auto &picture : m_State.objectPictures) pictures[picture.first] = picture.second;
        m_State.objectPictures = std::move(pictures);
        m_State.objectPicturesChanged = true;
        // Show the first merged Script.
        for (const ScriptInfo &script : m_State.document->Scripts())
            if (std::find(merged.begin(), merged.end(), script.id) != merged.end()) {
                m_Panels.ShowScript(script.id, ElementRef(), 0, false);
                break;
            }
        UpdateTitle();
    });
}

void EditorApp::RememberRecentFile(const std::string &path) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    m_State.preferences.AddRecentFile(ec ? path : absolute.string());
    SavePreferences();
}

void EditorApp::CloseDocument() {
    EndPrint();
    m_Setups.CloseAll();
    m_Tools.DocumentClosed();
    m_Panels.DocumentClosed();
    m_Managers.DocumentClosed();
    m_Layout.DocumentClosed(); // its working copies while they still exist
    m_Layout.ApplyLayoutData(LayoutInterfaceData());
    m_State.ClearDocumentState();
    m_Canvas.DocumentClosed();
    ReleaseSnapshotTextures(); // the document frees its bitmaps
    if (m_State.debugger) m_State.debugger->Detach();
    if (m_State.document) m_State.document->Close();
    m_LastSavedPath.clear();
    UpdateTitle();
}

bool EditorApp::SaveTo(const std::string &path, bool allowOverwrite) {
    SaveOptions options;
    options.saveScriptSnapshots = m_State.preferences.Bool("Schematic\\Save Script Snapshot");
    options.allowOverwrite = allowOverwrite;
    SaveReport report;
    const LayoutInterfaceData layout = LayoutDataToSave();
    const bool saved = SaveComposition(*m_State.document, path, CompositionToSave(layout), options, report);
    std::string text = saved ? "Saved " + path : (report.saved ? "Written but NOT verified: " : "Save failed: ") + path;
    text += "\nInterfaceChunks rewritten: " + std::to_string(report.chunksWritten) +
            "\nScripts verified in a new context: " + std::to_string(report.scriptsVerified) +
            "\nParameters verified: " + std::to_string(report.parametersVerified);
    for (const auto &error : report.errors) text += "\nError: " + AnsiToUtf8(error);
    for (const auto &note : report.notes) text += "\nNote: " + AnsiToUtf8(note);
    m_SaveReportText = std::move(text);
    m_OpenSaveReport = true;
    m_State.Log(saved ? LogLevel::Info : LogLevel::Error, m_SaveReportText);
    if (saved) {
        m_LastSavedPath = path;
        RememberRecentFile(path);
        m_State.History().MarkSavePoint();
    }
    UpdateTitle();
    return saved;
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

// The objects' pictures go with the composition when they changed (Dev.exe
// DoSaveObjects -> sub_5D1C90); unchanged, the file's own object is kept.
// The 3D Layout's data goes with the composition (Dev.exe DoSaveObjects);
// the pictures only when they changed (else the file's are kept as they are),
// written into the composition now.
CompositionData EditorApp::CompositionToSave(const LayoutInterfaceData &layout) {
    CompositionData data;
    data.layout = &layout;
    if (m_State.objectPicturesChanged) data.pictures = &m_State.objectPictures;
    m_State.objectPicturesChanged = false;
    return data;
}

void EditorApp::SaveAs(std::function<void(bool)> completed) {
    if (!m_State.HasDocument()) { if (completed) completed(false); return; }
    std::filesystem::path suggestion = std::filesystem::u8path(m_State.document->SourcePath());
    if (suggestion.empty()) suggestion = "New Composition.cmo";
    else suggestion.replace_filename(suggestion.stem().u8string() + "-edited" + suggestion.extension().u8string());
    const auto options = FileDialog(m_Window, true, m_LastSavedPath.empty() ? suggestion.u8string() : m_LastSavedPath);
    m_State.RequestFile(options, [this, completed = std::move(completed)](const FileDialogResult &result) {
        const bool saved = result.status == FileDialogResult::Status::Selected && m_State.HasDocument() &&
                           SaveTo(result.paths.front(), true);
        if (completed) completed(saved && !m_State.document->Dirty());
    });
}

void EditorApp::Save(std::function<void(bool)> completed) {
    if (!m_State.HasDocument()) { if (completed) completed(false); return; }
    if (m_LastSavedPath.empty()) { SaveAs(std::move(completed)); return; }
    const bool saved = SaveTo(m_LastSavedPath, true);
    if (completed) completed(saved && !m_State.document->Dirty());
}

void EditorApp::Undo() {
    if (!m_State.HasDocument() || !m_State.History().CanUndo()) return;
    const std::string label = m_State.History().UndoLabel();
    if (!m_State.History().Undo())
        m_State.Log(LogLevel::Warning, "Undo " + label + " failed: the undo history is cleared");
    m_State.selection.Clear();
    UpdateTitle();
}

void EditorApp::Redo() {
    if (!m_State.HasDocument() || !m_State.History().CanRedo()) return;
    const std::string label = m_State.History().RedoLabel();
    if (!m_State.History().Redo())
        m_State.Log(LogLevel::Warning, "Redo " + label + " failed: the undo history is cleared");
    m_State.selection.Clear();
    UpdateTitle();
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
            m_PendingPath = path;
            BeginAction(PendingAction::OpenPath);
            return;
        }
        if (!m_State.HasDocument() || !m_Canvas.DropFile(wide, path, ImVec2(x, y)))
            m_State.Log(LogLevel::Info, "Dropped file not used: " + path +
                                            " (compositions open; images go on Script headers, .nms on graphs or bodies)");
    }
}

void EditorApp::BeginAction(PendingAction action) {
    if (m_State.HasDocument() && m_State.document->Dirty()) {
        m_Pending = action;
        m_OpenUnsavedDialog = true;
        return;
    }
    RunAction(action);
}

void EditorApp::RunAction(PendingAction action) {
    switch (action) {
    case PendingAction::New:
        NewComposition();
        break;
    case PendingAction::Open: {
        m_State.RequestFile(FileDialog(m_Window, false, {}), [this](const FileDialogResult &result) {
            if (result.status != FileDialogResult::Status::Selected) return;
            m_PendingPath = result.paths.front();
            BeginAction(PendingAction::OpenPath);
        });
        break;
    }
    case PendingAction::OpenPath:
        if (!m_PendingPath.empty()) OpenFile(m_PendingPath);
        m_PendingPath.clear();
        break;
    case PendingAction::Close:
        CloseDocument();
        break;
    case PendingAction::Quit:
        m_Quit = true;
        break;
    default:
        break;
    }
}

void EditorApp::UpdateTitle() {
    // Documentation "Menu Bar": the level and scene current in the title
    // bar, and the path of the composition loaded.
    std::wstring title = L"NemoDev";
    if (m_State.HasDocument()) {
        const std::string &file = m_LastSavedPath.empty() ? m_State.document->SourcePath() : m_LastSavedPath;
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
        if (ImGui::MenuItem("New Composition")) BeginAction(PendingAction::New);
        ImGui::Separator();
        if (ImGui::MenuItem("Load Composition...", "Ctrl+L")) BeginAction(PendingAction::Open);
        if (ImGui::MenuItem("Save Composition", "Ctrl+S", false, hasDocument)) Save();
        if (ImGui::MenuItem("Save As...", nullptr, false, hasDocument)) SaveAs();
        if (ImGui::MenuItem("Save Version", "Ctrl+Shift+S", false, hasDocument)) SaveVersion();
        const bool revertible = hasDocument && !CurrentFilePath().empty();
        if (ImGui::MenuItem("Revert to Saved", "Ctrl+Shift+Z", false, revertible)) RevertToSaved();
        // Source Control (Dev.exe menu 10, commands 0x8063..0x8068): without a
        // version control plugin Dev says the function is not available
        // (sub_5C9220 ...); this editor has none.
        if (ImGui::BeginMenu("Source Control")) {
            auto item = [this](const char *label, const char *function) {
                if (!ImGui::MenuItem(label)) return;
                DevMessage message;
                message.text = std::string(function) + " function not available";
                message.title = "NemoDev";
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
        if (ImGui::MenuItem("Undo (3d Layout/Schematic only)", "Ctrl+Z", false, m_State.History().CanUndo())) Undo();
        if (ImGui::MenuItem("Redo (3d Layout/Schematic only)", "Ctrl+Y", false, m_State.History().CanRedo())) Redo();
        if (ImGui::MenuItem("Purge Undo", nullptr, false, m_State.History().CanUndo() || m_State.History().CanRedo()))
            m_State.History().Clear();
        ImGui::Separator();
        if (ImGui::MenuItem("Merge Composition...", nullptr, false, hasDocument)) MergeComposition(false);
        if (ImGui::MenuItem("Merge Composition as New Scene...", nullptr, false, hasDocument)) MergeComposition(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Export to Virtools Player...", nullptr, false, hasDocument)) ExportToPlayer();
        if (ImGui::MenuItem("Create Web Page...", nullptr, false, hasDocument)) m_OpenWebPage = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Print Schematic...", nullptr, false, hasDocument)) PrintSchematic();
        if (!m_State.preferences.recentFiles.empty()) {
            ImGui::Separator();
            const std::vector<std::string> recent = m_State.preferences.recentFiles; // opening reorders the list
            for (size_t i = 0; i < recent.size(); ++i) {
                const std::string label = std::to_string(i + 1) + ") " + AnsiToUtf8(recent[i]);
                if (ImGui::MenuItem(label.c_str())) {
                    m_PendingPath = recent[i];
                    BeginAction(PendingAction::OpenPath);
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Close", nullptr, false, hasDocument)) BeginAction(PendingAction::Close);
        if (ImGui::MenuItem("Exit")) BeginAction(PendingAction::Quit);
        ImGui::EndMenu();
    }
    // Dev's Resources menu (screenshot).
    if (ImGui::BeginMenu("Resources")) {
        if (ImGui::MenuItem("Open Data Resource")) OpenDataResourceDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Create New Data Resource")) CreateDataResourceDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Import File", nullptr, false, hasDocument)) ImportFile(Document::ImportMode::File);
        if (ImGui::BeginMenu("Import File As", hasDocument)) {
            if (ImGui::MenuItem("Character")) ImportFile(Document::ImportMode::Character);
            if (ImGui::MenuItem("Place")) ImportFile(Document::ImportMode::Place);
            if (ImGui::MenuItem("Scene")) ImportFile(Document::ImportMode::Scene);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    // Dev's Editors menu (screenshot); VSL and the Variable Manager are not
    // part of this editor.
    if (ImGui::BeginMenu("Editors")) {
        ImGui::MenuItem("Attributes Manager", nullptr, &m_State.showAttributeManager, hasDocument);
        ImGui::MenuItem("Hierarchy Manager", nullptr, &m_State.showHierarchy, hasDocument);
        if (ImGui::MenuItem("Path Manager")) m_Tools.OpenPathManager();
        unavailable("Variable Manager", nullptr, "Not part of this editor");
        unavailable("VSL Script Manager", nullptr, "Not part of this editor");
        ImGui::MenuItem("Parameter Debugger", nullptr, &m_Tools.showParameterDebugger);
        ImGui::Separator();
        if (ImGui::MenuItem("Close All Setups and Managers")) CloseSetupsAndManagers();
        ImGui::EndMenu();
    }
    // Dev's Options menu (screenshot).
    if (ImGui::BeginMenu("Options")) {
        if (ImGui::MenuItem("General Preferences", "Ctrl+P")) m_Tools.OpenGeneralPreferences();
        ImGui::Separator();
        Debugger *debugger = m_State.debugger.get();
        if (ImGui::MenuItem(debugger && debugger->trace ? "Deactivate Trace Mode" : "Activate Trace Mode", "Ctrl+T", false,
                            debugger != nullptr))
            debugger->trace = !debugger->trace;
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
        if (ImGui::MenuItem("Online Reference", "F1")) OpenOnlineReference();
        if (ImGui::MenuItem("About Virtools...")) m_Tools.OpenAbout();
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
    PopDarkChrome();
}

void EditorApp::LogUndoTop() {
    m_State.Log(LogLevel::Info, m_State.History().CanUndo() ? "Last undo step: " + m_State.History().UndoLabel()
                                                       : std::string("Last undo step: (none)"));
}

void EditorApp::CreateObject(LayoutObject kind) {
    if (!m_State.HasDocument()) return;
    // Create Portal: around the object selected (the 3D Layout's selection is the Level Manager's).
    CK_ID selection = 0;
    if (m_Panels.LevelSelection().size() == 1) selection = *m_Panels.LevelSelection().begin();
    else if (m_State.objectSelection.objects.size() == 1) selection = m_State.objectSelection.objects.front();
    // Dev places new objects by the 3D Layout's camera.
    CKCamera *view = m_Layout.View().Camera();
    const EditResult result =
        CreateLayoutObject(*m_State.document, kind, selection, view ? view->GetID() : 0);
    if (!result.ok) {
        m_State.Log(LogLevel::Warning, "Create: " + AnsiToUtf8(result.message));
        return;
    }
    CKObject *object = m_State.document->Context()->GetObject(result.created);
    if (!object) return;
    // Dev status line "%s '%s' created."; Automatic Show Setup.
    const char *cls = CKClassIDToString(object->GetClassID());
    m_State.Log(LogLevel::Info, std::string(cls ? cls : "Object") + " '" + AnsiToUtf8(object->GetName() ? object->GetName() : "") +
                                    "' created.");
    // Dev.exe sub_4A9960 / sub_4A9E90: the new camera replaces the view's
    // camera; new cameras and lights show their cone (Auto Show Camera / Light Cone).
    if (CKCamera::Cast(object)) {
        m_Layout.View().SetCamera(object->GetID());
        if (m_State.preferences.Bool("3D Layout\\Auto Show Camera Cone")) m_State.layoutCones.insert(object->GetID());
    }
    if (CKLight::Cast(object) && m_State.preferences.Bool("3D Layout\\Auto Show Light Cone"))
        m_State.layoutCones.insert(object->GetID());
    // Dev.exe sub_4A9960, sub_4A9E90, sub_4AA200, sub_4AA530: Automatic show
    // entities in player mode lists new cameras, lights, curves and frames.
    CK3dEntity *entity = CK3dEntity::Cast(object);
    const bool helper = CKCamera::Cast(object) || CKLight::Cast(object) || CKCurve::Cast(object) ||
                        (entity && (entity->GetFlags() & (CK_3DENTITY_FRAME | CK_3DENTITY_PORTAL)));
    if (helper && m_State.preferences.Bool("3D Layout\\Auto Show Entities In Player Mode"))
        m_State.playerShown.insert(object->GetID());
    if (m_State.preferences.Bool("3D Layout\\Auto Show Setup")) m_State.events.Post(ShowObjectSetup{object->GetID()});
}

void EditorApp::BuildDefaultLayout(ImGuiID dockspace, bool closeSeparated) {
    // The other windows open, before their nodes go: docked again after, or
    // (separated, Reset Interface) closed.
    struct Redocked {
        std::string name;
        bool withBlocks;
    };
    std::vector<Redocked> redocked;
    std::vector<unsigned int> separated;
    for (ImGuiWindow *window : ImGui::GetCurrentContext()->Windows) {
        if (!window->WasActive || window->ParentWindow ||
            (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip |
                              ImGuiWindowFlags_NoDocking)))
            continue;
        const std::string name = window->Name;
        const bool resource = name.find("###resource") != std::string::npos;
        const bool withBlocks = resource || name == "Hierarchy Manager" || name == "Parameter Debugger";
        const bool withSchematic = name.find("###setup") != std::string::npos || name == "Attribute Manager";
        if (!withBlocks && !withSchematic) continue;
        if (closeSeparated && !resource && !window->DockIsActive) separated.push_back(window->ID);
        else redocked.push_back({name, withBlocks});
    }
    if (!separated.empty()) DevCloseSeparated(separated);
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
    // Dev 2.5 as it starts (screenshot): the 3D Layout top-left and the
    // Building Blocks top-right, each half of the width; the Schematic and
    // Level Manager tabs below (the Level Manager in front). The Hierarchy
    // Manager and Parameter Debugger join the Building Blocks when they open
    // (the Schematic Explorer is a dialog of its own, as Dev's).
    ImGuiID bottom = dockspace;
    ImGuiID top = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Up, 0.5f, nullptr, &bottom);
    ImGuiID topRight = ImGui::DockBuilderSplitNode(top, ImGuiDir_Right, 0.5f, nullptr, &top);
    ImGui::DockBuilderDockWindow("3D Layout", top);
    ImGui::DockBuilderDockWindow("Building Blocks", topRight);
    ImGui::DockBuilderDockWindow("Hierarchy Manager", topRight);
    ImGui::DockBuilderDockWindow(SCHEMATIC_WINDOW, bottom);
    ImGui::DockBuilderDockWindow("Level Manager", bottom);
    for (const Redocked &window : redocked) ImGui::DockBuilderDockWindow(window.name.c_str(), window.withBlocks ? topRight : bottom);
    ImGui::DockBuilderFinish(dockspace);
}

bool EditorApp::Busy() const {
    return ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || m_Canvas.Busy() ||
           m_Layout.Busy();
}

void EditorApp::GlobalShortcuts() {
    const ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput || Busy()) return;
    // The Schematic takes F1 (a Building Block's help) and Ctrl+T itself;
    // the Level Manager F3 (its Find Item).
    const ImGuiWindow *focused = ImGui::GetCurrentContext()->NavWindow;
    const bool levelManager = focused && focused->RootWindow && std::strcmp(focused->RootWindow->Name, "Level Manager") == 0;
    const bool schematic = focused && focused->RootWindow && std::strcmp(focused->RootWindow->Name, SCHEMATIC_WINDOW) == 0;
    // F3 / Shift+F3: the Schematic's Find (once a text was asked), else the
    // Explorer's results;
    // Ctrl+F3 in the Schematic: the selected element's name.
    if (!levelManager && ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
        if (schematic && io.KeyCtrl) m_Canvas.FindSelected(io.KeyShift);
        else if (schematic && m_Canvas.Finding()) m_Canvas.FindNext(io.KeyShift);
        else if (!io.KeyCtrl) m_Panels.FindNext(io.KeyShift ? -1 : 1);
    }
    const bool behaviorHelp = schematic && m_State.selection.items.size() == 1 &&
                              m_State.selection.items[0].kind == ElementKind::Behavior;
    if (!io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F1, false) && !behaviorHelp) OpenOnlineReference();
    // Dev's accelerators, whichever window has the focus: Ctrl+Space Play /
    // Pause, Alt+Space and Ctrl+Alt+Space one step, Ctrl+Backspace,
    // Alt+Backspace and Ctrl+Shift+Backspace the initial conditions back.
    if (m_Layout.play) {
        if (!io.KeyShift && (io.KeyCtrl || io.KeyAlt) && ImGui::IsKeyPressed(ImGuiKey_Space, false))
            m_Layout.play(io.KeyAlt ? LayoutWindow::PlayCommand::Step : LayoutWindow::PlayCommand::PlayStop);
        if ((io.KeyCtrl || (io.KeyAlt && !io.KeyShift)) && ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
            m_Layout.play(LayoutWindow::PlayCommand::Reset);
    }
    if (!io.KeyCtrl) return;
    // Ctrl+Alt+F12: All CK Object In Memory.
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F12, false)) m_Tools.OpenAllObjects();
    // Dev: Ctrl+P General Preferences, Ctrl+T Trace Mode.
    if (ImGui::IsKeyPressed(ImGuiKey_P, false)) m_Tools.OpenGeneralPreferences();
    if (ImGui::IsKeyPressed(ImGuiKey_T, false) && !schematic && m_State.debugger)
        m_State.debugger->trace = !m_State.debugger->trace;
    // Dev: Ctrl+L Load Composition (Ctrl+O kept), Ctrl+Shift+S Save Version,
    // Ctrl+Shift+Z Revert to Saved.
    if (ImGui::IsKeyPressed(ImGuiKey_O, false) || ImGui::IsKeyPressed(ImGuiKey_L, false)) BeginAction(PendingAction::Open);
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) io.KeyShift ? SaveVersion() : (void)Save();
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (!io.KeyShift) Undo();
        else if (m_State.HasDocument()) RevertToSaved();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
    // Ctrl+Shift+F the Schematic Explorer; Ctrl+F the Schematic's Find (the
    // Level Manager's own there), else the Explorer.
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !levelManager) {
        if (schematic && !io.KeyShift) m_Canvas.OpenFind();
        else ShowSchematicExplorer();
    }
}

void EditorApp::UnsavedChangesDialog() {
    if (m_OpenUnsavedDialog) {
        ImGui::OpenPopup("Unsaved changes");
        m_OpenUnsavedDialog = false;
    }
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    // Documentation "File Menu": "You will be prompted to save your work":
    // Save (the file saved last, else Save As), Save As, Discard, Cancel.
    ImGui::TextUnformatted("Save changes to the composition?");
    const PendingAction pending = m_Pending;
    if (ImGui::Button(m_LastSavedPath.empty() ? "Save..." : "Save", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
        Save([this, pending](bool saved) { if (saved) RunAction(pending); });
        m_Pending = PendingAction::None;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As...", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
        SaveAs([this, pending](bool saved) { if (saved) RunAction(pending); });
        m_Pending = PendingAction::None;
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
        m_Pending = PendingAction::None;
        RunAction(pending);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
        m_Pending = PendingAction::None;
    }
    ImGui::EndPopup();
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
            m_Setups.Open(setup->object);
        } else if (const ShowInLevelManager *row = std::get_if<ShowInLevelManager>(&event)) {
            m_Panels.FocusLevelRow(row->object);
            m_ToFront = "Level Manager";
        } else if (const OpenAddAttribute *add = std::get_if<OpenAddAttribute>(&event)) {
            m_Managers.RequestAddAttribute(add->targets, add->more);
        } else if (const ConfirmDeleteObjects *remove = std::get_if<ConfirmDeleteObjects>(&event)) {
            m_Panels.RequestDeleteObjects(remove->objects);
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

void EditorApp::Frame() {
    m_State.DispatchFileRequests();
    // The current scene and the changes show in the title (cheap: set only when it changes).
    UpdateTitle();
    MainMenu();
    // The editor's former name: the saved window layouts know this ID.
    const ImGuiID dockspace = ImGui::GetID("SchematicEditorDockSpace");
    StatusBar();
    if (!m_LayoutChecked || m_ResetLayout) {
        // First run (no saved layout) or View > Reset Layout: Dev arrangement.
        ImGuiDockNode *node = ImGui::DockBuilderGetNode(dockspace);
        if (m_ResetLayout || !node || node->IsLeafNode()) BuildDefaultLayout(dockspace, m_CloseSeparated);
        m_CloseSeparated = false;
        m_LayoutChecked = true;
        m_ResetLayout = false;
        m_FocusSchematic = 3;
    }
    ImGui::DockSpaceOverViewport(dockspace, ImGui::GetMainViewport());

    DispatchEvents();
    // Dev's main windows have no close button.
    m_Canvas.Draw(SCHEMATIC_WINDOW, nullptr);
    // The canvas size is known only after a docked frame.
    if (m_FitFrames > 0 && --m_FitFrames == 0) m_Canvas.FitAll();
    m_Panels.ScriptsPanel(nullptr);
    m_Panels.DeleteObjectsDialog(); // the Level Manager's and the 3D Layout's
    DuplicateNamesDialog();
    WebPageDialog();
    PrintParametersDialog();
    MessageDialog();
    // --tool print:DIR (the dialog, pages to DIR on OK), once the Schematic has its Scripts.
    if (!m_PendingStartupDialog.empty() && m_FocusSchematic <= 0 && ++m_PrintPagesFrames > 5) {
        OpenStartupDialog(m_PendingStartupDialog);
        m_PendingStartupDialog.clear();
    }
    if (!m_PendingPrintDialog.empty() && m_FocusSchematic <= 0 && ++m_PrintPagesFrames > 5) {
        PrintSchematic(m_PendingPrintDialog);
        m_PendingPrintDialog.clear();
    }
    // --tool printpages:DIR, once the Schematic has its Scripts.
    if (!m_PendingPrintPages.empty() && m_FocusSchematic <= 0 && ++m_PrintPagesFrames > 5) {
        PrintSchematic(m_PendingPrintPages);
        if (m_Print.open) PrintPages();
        EndPrint();
        m_PendingPrintPages.clear();
    }
    m_Panels.BuildingBlocksPanel(nullptr);
    if (m_ShowExplorer) m_Panels.ExplorerPanel(&m_ShowExplorer);
    if (m_ShowLog) m_Panels.LogPanel(&m_ShowLog);
    {
        m_Layout.Draw(0, m_FocusSchematic <= 0);
        // Setup tabs dock with the Schematic (Dev's tab row).
        ImGuiWindow *schematic = ImGui::FindWindowByName(SCHEMATIC_WINDOW);
        m_Setups.Draw(schematic ? schematic->DockId : 0);
        // The managers dock there too when they open for the first time, but
        // the Hierarchy Manager, which Dev docks with the Building Blocks.
        ImGuiWindow *blocks = ImGui::FindWindowByName("Building Blocks");
        m_Managers.hierarchyDock = blocks ? blocks->DockId : 0;
        m_Managers.Draw(schematic ? schematic->DockId : 0, m_FocusSchematic <= 0);
        // The Parameter Debugger and Data Resources join the Building Blocks (Dev's top tab row).
        m_Tools.Draw(blocks ? blocks->DockId : 0, m_FocusSchematic <= 0);
    }
    if (m_State.showDebugger) m_Panels.DebuggerPanel(&m_State.showDebugger);
    // The Script Debugger advances with the UI; shown values follow the run.
    if (m_State.debugger && m_State.HasDocument()) {
        Debugger &debugger = *m_State.debugger;
        debugger.reengageBehavior = m_State.preferences.behaviorBreakpointReengage;
        debugger.reengageLink = m_State.preferences.linkBreakpointReengage;
        debugger.reengageParameter = m_State.preferences.parameterBreakpointReengage;
        const int frames = debugger.Frames(), reached = debugger.Reached();
        const Debugger::State before = debugger.GetState();
        debugger.Update(0.010);
        const bool active = before != Debugger::State::Stopped || debugger.GetState() != Debugger::State::Stopped;
        // "Redraw Schematic Only When Breakpoint Reached": values refresh at breakpoints only.
        const bool refresh = m_State.preferences.redrawOnlyAtBreakpoint
                                 ? reached != debugger.Reached() || (before == Debugger::State::Running &&
                                                                     debugger.GetState() != Debugger::State::Running)
                                 : frames != debugger.Frames() || ++m_DebugRefresh % 10 == 0;
        if (active && refresh) m_Canvas.RefreshValues();
    }
    const bool settled = m_FocusSchematic <= 0;
    if (m_FocusSchematic > 0) {
        // The front tabs of Dev's start (the Level Manager in front of the
        // Schematic, focused last); repeated because windows appearing later
        // take the focus.
        ImGui::SetWindowFocus("3D Layout");
        ImGui::SetWindowFocus("Building Blocks");
        ImGui::SetWindowFocus("Level Manager");
        // Several windows (names separated by '|'): one a frame, the window
        // focused at the end of a frame being the one whose tab shows.
        std::vector<std::string> names;
        for (size_t at = 0; at < m_Startup.focusWindow.size();) {
            size_t end = m_Startup.focusWindow.find('|', at);
            if (end == std::string::npos) end = m_Startup.focusWindow.size();
            names.push_back(m_Startup.focusWindow.substr(at, end - at));
            at = end + 1;
        }
        if (!names.empty()) ImGui::SetWindowFocus(names[m_FocusSchematic % names.size()].c_str());
        --m_FocusSchematic;
    }
    StartupFrame(settled);

    GlobalShortcuts();
    UnsavedChangesDialog();
    SaveReportDialog();
    UpdateTitle();
}

} // namespace vdev

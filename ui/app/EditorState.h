#pragma once

#include "core/graph/Debugger.h"
#include "workspace/EditorWorkspace.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <set>
#include <vector>

#include "core/catalog/BuildingBlockCatalog.h"
#include "core/runtime/CkRuntimeHost.h"
#include "ui/app/Preferences.h"
#include "ui/app/Clipboard.h"
#include "ui/schematic/SchematicStyle.h"
#include "core/schematic/ElementRef.h"
#include "core/document/Edit.h"
#include "core/document/Document.h"
#include "core/document/LinkValidation.h"
#include "core/layout/SnapshotBuffer.h"
#include "core/document/UndoHistory.h"
#include "core/level/LevelCommands.h"
#include "ui/app/EditorEvents.h"
#include "ui/app/RuntimeOperation.h"
#include "ui/app/DocumentFiles.h"
#include "core/runtime/TextEncoding.h"
#include "ui/controls/FileDialogs.h"

namespace vdev {

// Drag-and-drop payload: the CKGUID of a Building Block prototype.
constexpr const char *BUILDING_BLOCK_PAYLOAD = "SCHEMATIC_BB";
// Drag-and-drop payload of a Data Resources file (ResourceFilePayload).
constexpr const char *RESOURCE_FILE_PAYLOAD = "SCHEMATIC_RESOURCE_FILE";
struct ResourceFilePayload {
    char path[1024]; // UTF-8
    CK_CLASSID cid;  // class of its folder
};
// Drag-and-drop payload of the Snapshot Buffer: the slot (an int) of
// EditorState::snapshots.
constexpr const char *SNAPSHOT_PAYLOAD = "SCHEMATIC_SNAPSHOT";
// Drag-and-drop payload of a Level Manager object: its CK_ID.
constexpr const char *LEVEL_OBJECT_PAYLOAD = "SCHEMATIC_LEVEL_OBJECT";
// Drag-and-drop payload of the Attribute Manager: an attribute type, with
// the value (an object's attribute parameter) dragged from its object list.
constexpr const char *ATTRIBUTE_PAYLOAD = "SCHEMATIC_ATTRIBUTE";
struct AttributePayload {
    CKAttributeType type;
    CK_ID value; // 0: the attribute's default value
};

struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string text; // UTF-8
    int repeats = 1;  // the same message logged again in a row (a running composition repeats its errors)
    // Shown on the status bar too (Dev's StatusBarOutput); the Event Log's
    // own lines of the start are not.
    bool status = true;
};

// Selection is confined to one Script at a time, but Script headers: with
// a header selected, Ctrl or Shift and a click on others' select them too
// (headers; Dev acts on "one or several scripts": Expand, Hide Selected,
// colors, Save As, Copy, Delete, initial conditions, Execute).
struct Selection {
    CK_ID script = 0;
    std::vector<ElementRef> items;
    std::vector<CK_ID> headers; // the other Scripts whose header is selected

    static ElementRef HeaderOf(CK_ID scriptId) { return {ElementKind::Script, scriptId, scriptId}; }
    bool Empty() const { return items.empty(); }
    // Only Script headers are selected.
    bool OnlyHeaders() const {
        if (items.empty()) return false;
        for (const ElementRef &item : items)
            if (item.kind != ElementKind::Script) return false;
        return true;
    }
    bool Contains(CK_ID inScript, const ElementRef &ref) const {
        if (ref.kind == ElementKind::Script && std::find(headers.begin(), headers.end(), inScript) != headers.end()) return true;
        return inScript == script && std::find(items.begin(), items.end(), ref) != items.end();
    }
    // The Scripts whose header is selected, the first one first.
    std::vector<CK_ID> Scripts() const {
        std::vector<CK_ID> scripts;
        for (const ElementRef &item : items)
            if (item.kind == ElementKind::Script && script) {
                scripts.push_back(script);
                break;
            }
        for (CK_ID other : headers)
            if (std::find(scripts.begin(), scripts.end(), other) == scripts.end()) scripts.push_back(other);
        return scripts;
    }
    void Clear() {
        script = 0;
        items.clear();
        headers.clear();
    }
    void Set(CK_ID inScript, const ElementRef &ref) {
        script = inScript;
        items.assign(1, ref);
        headers.clear();
    }
    void Add(CK_ID inScript, const ElementRef &ref) {
        if (inScript != script && ref.kind == ElementKind::Script && OnlyHeaders()) {
            if (std::find(headers.begin(), headers.end(), inScript) == headers.end()) headers.push_back(inScript);
            return;
        }
        if (inScript != script) Set(inScript, ref);
        else if (!Contains(inScript, ref)) items.push_back(ref);
    }
    void Toggle(CK_ID inScript, const ElementRef &ref) {
        if (inScript != script && ref.kind == ElementKind::Script && OnlyHeaders()) {
            auto found = std::find(headers.begin(), headers.end(), inScript);
            if (found != headers.end()) headers.erase(found);
            else headers.push_back(inScript);
            return;
        }
        if (inScript != script) {
            Set(inScript, ref);
            return;
        }
        auto found = std::find(items.begin(), items.end(), ref);
        if (found != items.end()) items.erase(found);
        else items.push_back(ref);
        if (items.empty()) {
            script = 0;
            // Another header selected takes its place.
            if (!headers.empty()) {
                script = headers.front();
                headers.erase(headers.begin());
                items.assign(1, HeaderOf(script));
            }
        }
    }
};

// Dev Schematic toolbar toggles (UI reference section 8.1).
struct ViewOptions {
    bool showLocalParams = true;
    bool showLinkInfo = true;
    bool showControlPoints = true;
    bool showGrid = false; // Dev's canvas is plain
    bool showAllScripts = true; // Dev shows every Script, in Header::scriptIndex order
    bool showPriorities = false;
    bool showHeaders = true;
    // Dev's Level / Scene script filter (L / S button): with S only Scripts of
    // the active scene are shown.
    bool sceneFilter = false;
    bool zoomMode = false; // Dev Zoom Mode: drag up/down to zoom
    // Dev Link Mode: with auto-link (A on, Z off) a click on an I/O starts a
    // link; L starts one while auto-link is off. Ctrl+click selects instead.
    bool autoLink = true;
};

struct FocusRequest {
    bool pending = false;
    CK_ID script = 0;
    ElementRef ref;
    CK_ID behavior = 0; // fallback: center on this behavior (or its visible ancestor)
};

// Dev Worksets: "an user interface only item" (Dev documentation, Level
// Manager name column) grouping objects and Scripts to show or hide their
// Scripts in the Schematic. Kept for the session (Dev's own interface data
// that would store them is not written by this editor).
struct Workset {
    int id = 0;
    int parent = 0; // 0: top level
    std::string name;
    std::vector<CK_ID> members;
};

// The Curve Setup's Add / Insert / Edit / Delete Points: the 3D Layout's
// curve tool (Dev.exe tools 128..131; 0: none) on curve; referential: the
// plane Add Points puts points on (0 Guide, 1 View Plane).
struct CurveTool {
    CK_ID curve = 0;
    int tool = 0;
    int referential = 0;
};

// The 3D Layout's selection (Dev.exe: the view's CKObjectArray at +680), the
// render objects the Level Manager shows selected too; revision counts its
// changes. Lock Selection keeps other editors from changing it.
class ObjectSelection {
public:
    bool locked = false;
    unsigned revision = 0;
    const std::vector<CK_ID> &Objects() const { return m_Ids; }
    const std::vector<ObjectRef> &References() const { return m_References; }
    bool Contains(CK_ID id) const { return std::find(m_Ids.begin(), m_Ids.end(), id) != m_Ids.end(); }
    void Set(const Document &document, std::vector<CK_ID> ids);
    void Validate(const Document &document);
private:
    std::vector<ObjectRef> m_References;
    std::vector<CK_ID> m_Ids; // derived view for existing render/input consumers
};

// A window's own selection kept with the shared one (Dev keeps one selection
// for its editors: the Level Manager's and the Hierarchy Manager's rows).
// Pull: the shared selection changed since (another window set it), the
// window's becomes it. Push: the window's changed since, its render objects
// become the shared selection when they differ (not while that is locked:
// the change stays the window's).
class SelectionFollower {
public:
    bool Pull(const ObjectSelection &objects, std::set<CK_ID> &selection) {
        if (objects.revision == m_Seen) return false;
        selection = std::set<CK_ID>(objects.Objects().begin(), objects.Objects().end());
        m_Seen = objects.revision;
        m_Pushed = selection;
        return true;
    }
    void Push(ObjectSelection &objects, const std::set<CK_ID> &selection, const Document *document);

private:
    unsigned m_Seen = ~0u;
    std::set<CK_ID> m_Pushed;
};

// The open document's state: everything naming its objects (CK gives their
// ids to the next document's objects), made anew when it is closed
// (EditorState::ClearDocumentState). The rest of EditorState is the
// session's (preferences, the Snapshot Buffer, the log, the Copy and Delete
// Options, the windows' requests).
struct DocumentSession {
    EditorEvents events; // what windows ask of each other (EditorApp::DispatchEvents)
    std::vector<CK_ID> visibleScripts; // stacked in the Schematic, top to bottom
    CK_ID stackScript = 0;             // the Script the stack was made for (EditorPanels::ShowScript)
    // Hidden with Dev's Hide All / Hide Selected / Hide Other (view state).
    std::set<CK_ID> hiddenScripts;
    std::vector<Workset> worksets;
    int nextWorkset = 1;
    // Stack order set by Dev's Sort By ... / Invert Order (view state);
    // empty: Header::scriptIndex.
    std::vector<CK_ID> scriptOrder;
    ObjectClipboard objectClipboard;
    Selection selection;
    ObjectSelection objectSelection;
    // The invalid links found after loading and kept (Dev's Links with
    // Errors Explorer; Remove Invalid Link after Loading removes them).
    std::vector<InvalidLink> invalidLinks;
    // A 3D Layout transformation is being dragged (RealTime Notifications).
    bool layoutTransforming = false;
    CurveTool curveTool;
    FocusRequest focus;
    Clipboard clipboard;

};

struct EditorState : DocumentSession {
    EditorWorkspace *workspace = nullptr; // attached by the editor; outlives its windows
    EditorState();
    ~EditorState();

    CkRuntimeHost host;
    std::unique_ptr<Document> document;
    FileDialogs fileDialogs;
    void DispatchFileRequests();
    void RequestFile(const FileDialogOptions &options, FileDialogs::Completion completion, bool multiple = false);
    BuildingBlockCatalog catalog;
    std::vector<LogEntry> log;
    // The windows answering the parameter UIs' target button (CK2UI sends
    // message 0x607 with a class to the window under the mouse; Dev.exe's
    // 3D Layout sub_4C18A0, Schematic sub_51E0C0, the Level and Hierarchy
    // Managers' trees sub_405700 / sub_40C0F0): by ImGui window name, the
    // object of the class at a screen point, 0 for none (or where the window
    // did not draw).
    std::map<std::string, std::function<CK_ID(float x, float y, CK_CLASSID cid)>> objectPick;
    std::unique_ptr<Debugger> debugger; // Script Debugger on the document
    bool showDebugger = false;
    bool showAttributeManager = false;
    bool showHierarchy = false;
    std::vector<std::string> layoutFrameShapes; // Virtools_Frame_ meshes less the prefix
    // Dev's lines for Add Attribute (Dev.exe sub_594BA0), each object's.
    void LogAttributeAdds(const char *attribute, const std::vector<std::pair<std::string, bool>> &each, bool ok) {
        const std::string name = AnsiToUtf8(attribute ? attribute : "");
        for (const auto &entry : each)
            if (entry.second && ok)
                Log(LogLevel::Info, "Attribute " + name + " added to object " + AnsiToUtf8(entry.first));
            else if (!entry.second)
                Log(LogLevel::Warning, "Can't set Attribute " + name + " on Object " + AnsiToUtf8(entry.first) +
                                           ", please check attribute compatibility");
    }
    // An attribute dropped on objects (the 3D Layout, a Level Manager row, a
    // Setup): DropAttribute, its lines logged.
    void DropAttributeOn(const std::vector<CK_ID> &objects, const AttributePayload &attribute);
    // The 3D Layout's Snapshot Buffer (Dev keeps it for the session).
    SnapshotBuffer snapshots;
    // Dev's Copy Options (the session's copy dependencies, Dev.exe
    // stru_647D7C): CK's default ones until the dialog changes them.
    CKDependencies copyDependencies;
    bool copyDependenciesSet = false;
    CKDependencies &CopyDependencies() {
        if (!copyDependenciesSet) {
            if (CKDependencies *defaults = CKGetDefaultClassDependencies(CK_DEPENDENCIES_COPY)) copyDependencies = *defaults;
            else copyDependencies.m_Flags = CK_DEPENDENCIES_CUSTOM;
            copyDependenciesSet = true;
        }
        return copyDependencies;
    }
    // Dev's Delete Options (Dev.exe sub_5BF7B0, "Select Delete Options"), the same way.
    CKDependencies deleteDependencies;
    bool deleteDependenciesSet = false;
    CKDependencies &DeleteDependencies() {
        if (!deleteDependenciesSet) {
            if (CKDependencies *defaults = CKGetDefaultClassDependencies(CK_DEPENDENCIES_DELETE)) deleteDependencies = *defaults;
            else deleteDependencies.m_Flags = CK_DEPENDENCIES_CUSTOM;
            deleteDependenciesSet = true;
        }
        return deleteDependencies;
    }
    ViewOptions view;
    // Building Blocks inserted recently, newest first (Dev Shift+right-click menu).
    std::vector<CKGUID> recentBlocks;
    Preferences preferences;
    SchematicStyle style;
    std::string runtimeDirectory;

    // The Building Blocks dragged together (Ctrl or Shift + click in the
    // Building Blocks); the drops take them all when the payload's is one.
    std::vector<CKGUID> draggedBlocks;
    // The Data Resources files dragged together (Ctrl or Shift + click).
    std::vector<ResourceFilePayload> draggedResources;
    std::vector<ResourceFilePayload> DraggedResources(const ResourceFilePayload &file) const {
        for (const ResourceFilePayload &each : draggedResources)
            if (std::strcmp(each.path, file.path) == 0) return draggedResources;
        return {file};
    }
    std::vector<CKGUID> DraggedBlocks(const CKGUID &guid) const {
        if (std::find(draggedBlocks.begin(), draggedBlocks.end(), guid) != draggedBlocks.end()) return draggedBlocks;
        return {guid};
    }

    void RememberBlock(const CKGUID &guid) {
        recentBlocks.erase(std::remove(recentBlocks.begin(), recentBlocks.end(), guid), recentBlocks.end());
        recentBlocks.insert(recentBlocks.begin(), guid);
        const size_t limit = static_cast<size_t>(std::max(1, preferences.maxRecentBlocks));
        if (recentBlocks.size() > limit) recentBlocks.resize(limit);
    }

    void Log(LogLevel level, const std::string &text) {
        if (!log.empty() && log.back().level == level && log.back().text == text) {
            ++log.back().repeats;
            log.back().status = true;
            return;
        }
        log.push_back({level, text});
        if (log.size() > 5000) log.erase(log.begin(), log.begin() + 1000);
    }
    // Runtime and text-conversion callbacks can log from the worker. Only
    // the UI thread appends to the log read by panels and the status bar.
    void RuntimeLog(LogLevel level, const std::string &text) {
        if (std::this_thread::get_id() == m_UiThread) { Log(level, text); return; }
        std::lock_guard<std::mutex> lock(m_RuntimeLogMutex);
        m_RuntimeLog.push_back({level, text});
    }
    void DrainRuntimeLog() {
        std::vector<LogEntry> entries;
        {
            std::lock_guard<std::mutex> lock(m_RuntimeLogMutex);
            entries.swap(m_RuntimeLog);
        }
        for (const auto &entry : entries) Log(entry.level, entry.text);
    }
    bool RunRuntime(const std::string &label, std::function<void()> work, std::function<void(bool)> completed = {});
    void PollRuntimeOperations();
    void FinishRuntimeOperations();
    bool HasDocument() const { return document && document->IsOpen(); }
    // The document's undo history (the document is made at startup and lives until shutdown).
    UndoHistory &History() { return document->History(); }
    // The document's state made anew (it is closed); the selection's
    // revision goes on so that the windows showing it update.
    void ClearDocumentState();

private:
    const std::thread::id m_UiThread = std::this_thread::get_id();
    std::mutex m_RuntimeLogMutex;
    std::vector<LogEntry> m_RuntimeLog;
public:
    DocumentFiles files{*this};
    // Declared last so its worker and captures go before files/document/host.
    RuntimeOperation runtimeOperation;
};

} // namespace vdev

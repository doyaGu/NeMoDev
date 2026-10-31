#pragma once

#include "core/graph/Debugger.h"
#include "workspace/EditorWorkspace.h"
#include "editor/Scene.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <set>
#include <vector>

#include "core/catalog/BuildingBlockCatalog.h"
#include "core/runtime/CkRuntimeHost.h"
#include "ui/app/Preferences.h"
#include "core/document/Clipboard.h"
#include "ui/schematic/SchematicStyle.h"
#include "core/schematic/ElementRef.h"
#include "core/document/Edit.h"
#include "core/document/Document.h"
#include "core/document/LinkValidation.h"
#include "core/layout/SnapshotBuffer.h"
#include "core/document/UndoHistory.h"
#include "application/ObjectSelection.h"
#include "application/EventLog.h"
#include "core/level/LevelCommands.h"
#include "ui/app/EditorEvents.h"
#include "application/RuntimeOperation.h"
#include "application/DocumentFiles.h"
#include "core/runtime/TextEncoding.h"
#include "ui/controls/FileDialogs.h"

namespace vdev {

// Drag-and-drop payload: the CKGUID of a Building Block prototype.
// Drag-and-drop payload of the Snapshot Buffer: the slot (an int) of
// EditorState::snapshots.
constexpr const char *SNAPSHOT_PAYLOAD = "SCHEMATIC_SNAPSHOT";
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

// The Curve Setup's Add / Insert / Edit / Delete Points: the 3D Layout's
// curve tool (Dev.exe tools 128..131; 0: none) on curve; referential: the
// plane Add Points puts points on (0 Guide, 1 View Plane).
struct CurveTool {
    CK_ID curve = 0;
    int tool = 0;
    int referential = 0;
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
        if (objects.Revision() == m_Seen) return false;
        selection = std::set<CK_ID>(objects.Objects().begin(), objects.Objects().end());
        m_Seen = objects.Revision();
        m_Pushed = selection;
        return true;
    }
    void Push(ObjectSelection &objects, const std::set<CK_ID> &selection, const Document *document, EditorApplication &editor);

private:
    unsigned m_Seen = ~0u;
    std::set<CK_ID> m_Pushed;
};

// Window state for the open document, reset on replacement. Application-owned
// object selection is reset alongside it. Preferences, the Snapshot Buffer,
// the log and Copy Options keep their application lifetime.
struct DocumentSession {
    EditorEvents events; // what windows ask of each other (EditorApp::DispatchEvents)
    std::vector<CK_ID> visibleScripts; // stacked in the Schematic, top to bottom
    CK_ID stackScript = 0;             // the Script the stack was made for (EditorPanels::ShowScript)
    // Hidden with Dev's Hide All / Hide Selected / Hide Other (view state).
    std::set<CK_ID> hiddenScripts;
    // Stack order set by Dev's Sort By ... / Invert Order (view state);
    // empty: Header::scriptIndex.
    std::vector<CK_ID> scriptOrder;
    ObjectClipboard objectClipboard;
    Selection selection;
    // The invalid links found after loading and kept (Dev's Links with
    // Errors Explorer; Remove Invalid Link after Loading removes them).
    std::vector<InvalidLink> invalidLinks;
    // A 3D Layout transformation is being dragged (RealTime Notifications).
    bool layoutTransforming = false;
    CurveTool curveTool;
    FocusRequest focus;
    Clipboard clipboard;

};

struct EditorState : DocumentSession, DocumentFiles::Observer {
    EditorWorkspace *workspace = nullptr; // attached by the editor; outlives its windows
    DocumentFiles::Views *documentViews = nullptr;
    void ReplacingDocument(bool replaceContext) override;
    void ReplacementCancelled() override;
    void DocumentReplaced() override;
    EditorState();
    ~EditorState();

    CkRuntimeHost host;
    std::unique_ptr<Document> document;
    ObjectSelection objectSelection;
    FileDialogs fileDialogs;
    void DispatchFileRequests();
    void RequestFile(const FileDialogOptions &options, FileDialogs::Completion completion, bool multiple = false, const void *requester = nullptr);
    BuildingBlockCatalog catalog;
    EventLog eventLog;
    // The windows answering the parameter UIs' target button (CK2UI sends
    // message 0x607 with a class to the window under the mouse; Dev.exe's
    // 3D Layout sub_4C18A0, Schematic sub_51E0C0, the Level and Hierarchy
    // Managers' trees sub_405700 / sub_40C0F0): by ImGui window name, the
    // object of the class at a screen point, 0 for none (or where the window
    // did not draw).
    std::map<std::string, std::function<CK_ID(float x, float y, CK_CLASSID cid)>> objectPick;
    const Debugger *debugger = nullptr; // read-only until the breakpoint views migrate; owned by ExecutionCommands
    bool showDebugger = false;
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
    void DropAttributeOn(const std::vector<CK_ID> &objects, const EditorAttributePayload &attribute);
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
    ViewOptions view;
    // Building Blocks inserted recently, newest first (Dev Shift+right-click menu).
    std::vector<CKGUID> recentBlocks;
    Preferences preferences;
    SchematicStyle style;
    std::string runtimeDirectory;

    void RememberBlock(const CKGUID &guid) {
        recentBlocks.erase(std::remove(recentBlocks.begin(), recentBlocks.end(), guid), recentBlocks.end());
        recentBlocks.insert(recentBlocks.begin(), guid);
        const size_t limit = static_cast<size_t>(std::max(1, preferences.maxRecentBlocks));
        if (recentBlocks.size() > limit) recentBlocks.resize(limit);
    }

    void Log(LogLevel level, const std::string &text) override { eventLog.Append(level, text); }
    void RuntimeLog(LogLevel level, const std::string &text) { eventLog.Post(level, text); }
    void DrainRuntimeLog() { eventLog.Drain(); }
    bool RunRuntime(const std::string &label, std::function<void()> work, std::function<void(bool)> completed = {});
    void PollRuntimeOperations();
    void FinishRuntimeOperations();
    bool HasDocument() const { return document && document->IsOpen(); }
    // The document's undo history (the document is made at startup and lives until shutdown).
    UndoHistory &History() { return document->History(); }
    // The document's state made anew (it is closed); the selection's
    // revision goes on so that the windows showing it update.
    void ClearDocumentState();

    DocumentFiles files{[this] { return document.get(); }, runtimeOperation, *this};
    // Declared last so its worker and captures go before files/document/host.
    RuntimeOperation runtimeOperation;
};

} // namespace vdev

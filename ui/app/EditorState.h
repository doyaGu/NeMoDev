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
#include "application/Preferences.h"
#include "core/document/Clipboard.h"
#include "ui/schematic/SchematicStyle.h"
#include "core/schematic/SchematicNavigation.h"
#include "core/document/Edit.h"
#include "core/document/Document.h"
#include "core/document/LinkValidation.h"
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
// Dev Worksets: "an user interface only item" (Dev documentation, Level
// Manager name column) grouping objects and Scripts to show or hide their
// Scripts in the Schematic. Kept for the session (Dev's own interface data
// that would store them is not written by this editor).

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
    ObjectClipboard objectClipboard;
    Clipboard clipboard;

};

class SchematicViews;

struct EditorState : DocumentSession, DocumentFiles::Observer {
    SchematicViews *schematics = nullptr;
    SchematicNavigation &Schematic() const;
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
    const Debugger *debugger = nullptr; // read-only until the breakpoint views migrate; owned by ExecutionCommands
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

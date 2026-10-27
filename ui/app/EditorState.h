#pragma once

#include "core/graph/Debugger.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <string>
#include <set>
#include <vector>

#include "core/catalog/BuildingBlockCatalog.h"
#include "core/runtime/CkRuntimeHost.h"
#include "ui/app/Preferences.h"
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

// Copied behaviors and Comments. data is the source Script's display data
// when copying, so pasting still works after the originals are deleted.
struct Clipboard {
    std::shared_ptr<ObjectReferences> objects;
    bool valid = false;
    InterfaceData data;
    std::vector<CK_ID> behaviors;
    std::vector<Comment> comments;
    // Parameters: the Schematic's locals, shortcut sources and outputs
    // (Ctrl+Shift+V), the Attribute Manager's and the Setups' attribute
    // values, the Array Setup's cells. Dev has one clipboard: a Copy of
    // parameters anywhere takes the place of what was copied before, and
    // they paste anywhere (Paste As Shortcut, the Parameter Debugger, the
    // attributes' and the arrays' Paste).
    std::vector<CK_ID> parameters;
    // The Schematic's parameter operations copied (pasted as new operations
    // when no behavior is).
    std::vector<CK_ID> operations;
    // Copied from a Setup's Attribute page: each parameter's attribute (by
    // name, ANSI) for Paste Attributes; empty otherwise.
    std::vector<std::string> attributes;
    // The parameters copied (a window's Copy of values).
    void SetParameters(Document &document, std::vector<CK_ID> ids, std::vector<std::string> names = {}) {
        Clipboard replacement;
        replacement.objects = document.RetainObjects(ids);
        replacement.parameters = std::move(ids);
        replacement.attributes = std::move(names);
        replacement.valid = !replacement.parameters.empty();
        *this = std::move(replacement);
    }
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
struct ObjectSelection {
    std::vector<CK_ID> objects;
    bool locked = false;
    unsigned revision = 0;

    bool Contains(CK_ID id) const { return std::find(objects.begin(), objects.end(), id) != objects.end(); }
    void Set(std::vector<CK_ID> ids) {
        objects = std::move(ids);
        if (objects.empty()) locked = false;
        ++revision;
    }
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
        selection = std::set<CK_ID>(objects.objects.begin(), objects.objects.end());
        m_Seen = objects.revision;
        m_Pushed = selection;
        return true;
    }
    void Push(ObjectSelection &objects, const std::set<CK_ID> &selection, CKContext *context) {
        if (selection == m_Pushed) return;
        m_Pushed = selection;
        if (objects.locked || !context) return;
        std::vector<CK_ID> rendered;
        for (CK_ID id : selection)
            if (CKRenderObject::Cast(context->GetObject(id))) rendered.push_back(id);
        if (rendered != objects.objects) objects.Set(std::move(rendered));
        m_Seen = objects.revision;
    }

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
    // Scripts copied from a header or the Level Manager (pasted on an object
    // with Ctrl+V in the Level Manager).
    std::vector<CK_ID> scriptClipboard;
    std::shared_ptr<ObjectReferences> scriptClipboardObjects;
    void SetScriptClipboard(Document &document, std::vector<CK_ID> scripts) {
        auto retained = document.RetainObjects(scripts);
        scriptClipboard = std::move(scripts);
        scriptClipboardObjects = std::move(retained);
        objectClipboard.clear();
        objectClipboardObjects.reset();
        objectsCopied = false;
    }
    // Dev has one clipboard: what the last Copy took, the objects
    // (objectClipboard: the 3D Layout's, the Level Manager's) or the Scripts.
    bool objectsCopied = false;
    Selection selection;
    ObjectSelection objectSelection;
    // Cameras and lights whose cone the 3D Layout draws (Dev: Camera Setup
    // Show cone, Light Setup Show influence; Auto Show Camera / Light Cone
    // turns it on for new ones).
    std::set<CK_ID> layoutCones;
    // Curves whose control points the 3D Layout does not draw (Curve Setup's
    // Show curve points off; Dev's 3D Layout list +180 lacks them).
    std::set<CK_ID> hiddenCurvePoints;
    // The frames, curves, lights and cameras shown in Player Mode although
    // their type is not (Setups' Show In Player Mode; Dev's 3D Layout lists
    // +188 to +200, Automatic show entities in player mode adds new ones).
    std::set<CK_ID> playerShown;
    // The invalid links found after loading and kept (Dev's Links with
    // Errors Explorer; Remove Invalid Link after Loading removes them).
    std::vector<InvalidLink> invalidLinks;
    // A 3D Layout transformation is being dragged (RealTime Notifications).
    bool layoutTransforming = false;
    // Each 3D Frame's representation in the 3D Layout (Dev.exe: its own
    // table, not object state; Change Representation, 3D Frame Setup): 0
    // none, n the n-th of layoutFrameShapes; frames not listed have 1.
    std::map<CK_ID, int> frameRepresentations;
    // Objects copied with Ctrl+C in the 3D Layout (pasted as copies with Ctrl+V).
    std::vector<CK_ID> objectClipboard;
    std::shared_ptr<ObjectReferences> objectClipboardObjects;
    void SetObjectClipboard(Document &document, std::vector<CK_ID> objects) {
        auto retained = document.RetainObjects(objects);
        objectClipboard = std::move(objects);
        objectClipboardObjects = std::move(retained);
        scriptClipboard.clear();
        scriptClipboardObjects.reset();
        objectsCopied = true;
    }
    CurveTool curveTool;
    // The objects' pictures (core ObjectPictures: Snapshot Buffer images
    // dropped on a Setup or a 3D Layout object; bitmap handles the document
    // keeps), written back when they changed.
    std::map<CK_ID, void *> objectPictures;
    bool objectPicturesChanged = false;
    FocusRequest focus;
    Clipboard clipboard;
    // Dev's Duplicate Names Found (CK_LOAD_DODIALOG, Dev.exe sub_59B3A0): a
    // load of a file bringing objects named as existing ones waits for the
    // choices; the dialog (EditorApp) runs it with them on OK and drops it
    // on Cancel (CK's CKERR_CANCELLED: nothing is loaded).
    struct DuplicateNamesRequest {
        std::string path;
        std::vector<DuplicateName> duplicates;
        std::function<void(const std::vector<DuplicateName> *)> load;
        unsigned serial = 0; // which request the dialog's tree was made for
    };
    std::vector<DuplicateNamesRequest> duplicateNames; // the first one is asked
    unsigned duplicateNamesAsked = 0;
};

struct EditorState : DocumentSession {
private:
    std::shared_ptr<int> m_DocumentIdentity = std::make_shared<int>(0);
public:
    ~EditorState() { FinishRuntimeOperations(); ClearDocumentState(); }

    CkRuntimeHost host;
    std::unique_ptr<Document> document;
    FileDialogs fileDialogs;
    void DispatchFileRequests() {
        if (!document || !document->EditOpen()) fileDialogs.Dispatch();
    }
    void RequestFile(const FileDialogOptions &options, FileDialogs::Completion completion, bool multiple = false) {
        const std::weak_ptr<int> documentIdentity = m_DocumentIdentity;
        fileDialogs.Request(options, [this, documentIdentity, completion = std::move(completion)](const FileDialogResult &result) {
            if (documentIdentity.expired()) return;
            if (result.status == FileDialogResult::Status::Error)
                Log(LogLevel::Error, result.error.empty() ? "File selection failed" : result.error);
            completion(result);
        }, multiple);
    }
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
    // Edits of these lists (the Setups' check boxes, Change Representation):
    // one undo step each, the composition modified (they are saved with it).
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
    void DropAttributeOn(const std::vector<CK_ID> &objects, const AttributePayload &attribute) {
        if (!HasDocument()) return;
        CKAttributeManager *manager = document->Context()->GetAttributeManager();
        const char *name = manager ? manager->GetAttributeNameByType(attribute.type) : nullptr;
        std::vector<std::pair<std::string, bool>> each;
        const EditResult result = DropAttribute(*document, objects, attribute.type, attribute.value, &each);
        LogAttributeAdds(name, each, result.ok);
        if (!result.ok && !result.Unchanged() && each.empty()) Log(LogLevel::Warning, "Add Attribute: " + AnsiToUtf8(result.message));
        else if (result.ok && !result.message.empty()) Log(LogLevel::Warning, AnsiToUtf8(result.message));
    }
    void SetInLayoutList(std::set<CK_ID> &list, CK_ID id, bool in, const std::string &label) {
        if (!HasDocument() || !id || (list.count(id) != 0) == in) return;
        std::set<CK_ID> *target = &list;
        auto set = [target, id](bool listed) {
            if (listed) target->insert(id);
            else target->erase(id);
            return true;
        };
        Edit edit(*document, label);
        edit.Do([set, in] { return set(in); }, [set, in] { return set(!in); });
        edit.Commit();
    }
    void SetFrameRepresentation(const std::vector<CK_ID> &frames, int representation) {
        if (!HasDocument()) return;
        std::vector<std::pair<CK_ID, int>> before; // -1: not listed (representation 1)
        for (CK_ID id : frames) {
            auto found = frameRepresentations.find(id);
            const int previous = found != frameRepresentations.end() ? found->second : -1;
            if ((previous < 0 ? 1 : previous) != representation) before.emplace_back(id, previous);
        }
        if (before.empty()) return;
        std::map<CK_ID, int> *table = &frameRepresentations;
        auto apply = [table, before, representation](bool after) {
            for (const auto &frame : before) {
                const int value = after ? representation : frame.second;
                if (value < 0) table->erase(frame.first);
                else (*table)[frame.first] = value;
            }
            return true;
        };
        Edit edit(*document, "Change Representation");
        edit.Do([apply] { return apply(true); }, [apply] { return apply(false); });
        edit.Commit();
    }
    // The 3D Layout's Snapshot Buffer (Dev keeps it for the session).
    SnapshotBuffer snapshots;
    // An object's picture from Snapshot Buffer slot (Dev.exe sub_5D1E20).
    void SetObjectPicture(CK_ID object, int slot);
    // The object's picture becomes bitmap (an SDK bitmap the document keeps;
    // null: none, Remove Snapshot); one undo step.
    void SetObjectPictureBitmap(CK_ID object, void *bitmap);
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
    bool RunRuntime(const std::string &label, std::function<void()> work, std::function<void(bool)> completed = {}) {
        if (runtimeOperation.Busy()) {
            std::weak_ptr<int> identity = m_DocumentIdentity;
            m_RuntimeRequests.push_back([this, identity, label, work = std::move(work), completed = std::move(completed)] {
                if (!identity.expired()) RunRuntime(label, work, completed);
                else if (completed) completed(false);
            });
            return true;
        }
        if (document && document->EditOpen()) {
            Log(LogLevel::Warning, "Finish the current operation before " + label);
            if (completed) completed(false);
            return false;
        }
        return runtimeOperation.Queue(label, [this, label, work = std::move(work)] {
            if (!GuardedCall(work, &host, label.c_str())) throw std::runtime_error(label + " failed");
        }, [this, completed = std::move(completed)](const std::string &error) {
            DrainRuntimeLog();
            if (!error.empty()) Log(LogLevel::Error, error);
            if (completed) completed(error.empty());
        });
    }
    void PollRuntimeOperations() {
        runtimeOperation.Poll();
        while (!runtimeOperation.Busy() && !m_RuntimeRequests.empty()) {
            auto next = std::move(m_RuntimeRequests.front());
            m_RuntimeRequests.pop_front();
            next();
        }
        DrainRuntimeLog();
    }
    void FinishRuntimeOperations() {
        do {
            runtimeOperation.Finish();
            PollRuntimeOperations();
        } while (runtimeOperation.Busy());
    }
    bool HasDocument() const { return document && document->IsOpen(); }
    // The document's undo history (the document is made at startup and lives until shutdown).
    UndoHistory &History() { return document->History(); }
    // The document's state made anew (it is closed); the selection's
    // revision goes on so that the windows showing it update.
    void ClearDocumentState() {
        m_DocumentIdentity = std::make_shared<int>(0);
        const unsigned revision = objectSelection.revision;
        static_cast<DocumentSession &>(*this) = DocumentSession();
        objectSelection.revision = revision + 1;
    }

    // Runs load at once (no choices) when the file brings no duplicate name,
    // else once the dialog has the choices.
    void LoadAskingDuplicates(const std::string &path, std::function<void(const std::vector<DuplicateName> *)> load) {
        auto found = std::make_shared<std::vector<DuplicateName>>();
        RunRuntime("Checking duplicate names", [this, path, found] {
            if (HasDocument()) *found = document->FindDuplicateNames(path);
        }, [this, path, found, load = std::move(load)](bool ok) mutable {
            if (!ok) return;
            if (found->empty()) load(nullptr);
            else duplicateNames.push_back({path, std::move(*found), std::move(load), ++duplicateNamesAsked});
        });
    }

private:
    const std::thread::id m_UiThread = std::this_thread::get_id();
    std::mutex m_RuntimeLogMutex;
    std::vector<LogEntry> m_RuntimeLog;
    std::deque<std::function<void()>> m_RuntimeRequests;
public:
    // Declared last so its worker and captures go before the document/host.
    RuntimeOperation runtimeOperation;
};

} // namespace vdev

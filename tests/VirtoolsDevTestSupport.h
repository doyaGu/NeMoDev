#pragma once

// VirtoolsDevTests' shared part: CHECK, the helpers several test files use,
// and the entry points main runs (VirtoolsDevTests.cpp). The tests:
// VirtoolsDevEditTests.cpp (edits and the undo history, undo round trips of
// each kind of command), VirtoolsDevTopologyTests.cpp (topology edits, the
// fuzz, a sample's edits),
// VirtoolsDevManagerTests.cpp (Level, Setup, Create, scenes, the debugger,
// Building Block callbacks, text encoding), VirtoolsDevFileTests.cpp (save,
// merge, tools, duplicate names, the 3D Layout's data and pictures, the File
// menu, links) and VirtoolsDevLayoutTests.cpp (the 3D Layout view).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The 2.5 SDK headers include it, lean; the 2.1 ones do not.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#include "core/catalog/BuildingBlockCatalog.h"
#include "core/runtime/CkArrays.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/Resources.h"
#include "core/document/DocumentSaver.h"
#include "core/document/Edit.h"
#include "core/interface/InterfaceDataDiff.h"
#include "core/catalog/ParameterCatalog.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicModel.h"
#include "core/graph/BehaviorCommands.h"
#include "core/level/SceneCommands.h"
#include "core/graph/ShortcutCommands.h"
#include "core/graph/NmsCommands.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/ExportCommands.h"
#include "core/document/FileCommands.h"
#include "core/graph/ReplaceCommands.h"
#include "core/document/ToolCommands.h"
#include "core/level/LevelCommands.h"
#include "core/level/HierarchyTree.h"
#include "core/layout/LayoutExplorer.h"
#include "core/layout/LayoutInterface.h"
#include "core/document/ObjectPictures.h"
#include "core/catalog/ParameterUIs.h"
#include "core/layout/GestureMath.h"
#include "core/layout/LayoutTools.h"
#include "core/layout/LayoutView.h"
#include "core/layout/ViewProjection.h"
#include "core/document/LinkValidation.h"
#include "core/layout/SnapshotBuffer.h"
#include "core/runtime/TextEncoding.h"
#include "core/runtime/GridCompat.h"
#include "core/graph/Debugger.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/ObjectProperties.h"
#include "core/level/SetupCommands.h"
#include "core/graph/LinkCommands.h"
#include "core/graph/OperationCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/ValueCommands.h"

using namespace vdev;

namespace vdevtest {

void ObjectEditFailures(CkRuntimeHost &host);
void ObjectEditCancellation(CkRuntimeHost &host);
void SaveFailures(CkRuntimeHost &host);
void SaveCallbacks(CkRuntimeHost &host);
void InitialConditionsScenes(CkRuntimeHost &host);
void InitialConditionsFailures(CkRuntimeHost &host);
void ParameterValueFailures(CkRuntimeHost &host);

// A check that failed: the case stops (CheckFailure), main reports it with
// the expression and its place and goes on with the next case. Checks with
// their own message write it first, then Fail.
struct CheckFailure {
    std::string what;
};
[[noreturn]] inline void Fail(const char *expression, const char *file, int line) {
    throw CheckFailure{std::string(file) + "(" + std::to_string(line) + "): " + expression};
}

} // namespace vdevtest

#define CHECK(condition) ((condition) ? (void)0 : ::vdevtest::Fail(#condition, __FILE__, __LINE__))

namespace vdevtest {

inline std::string RuntimeDirectory() {
    const char *path = std::getenv("VIRTOOLS_PATH");
    return path ? path : ".";
}

struct State {
    std::string topology;
    InterfaceData data;
};

// Canonical CK topology and local values for undo round-trip comparisons.
std::string TopologyFingerprint(CKBehavior *script);

inline State Capture(Document &document, CK_ID script) {
    State state;
    state.topology = TopologyFingerprint(CKBehavior::Cast(document.Context()->GetObject(script)));
    state.data = document.Session(script)->Data();
    state.data.observers.clear();
    return state;
}

inline bool Same(const State &a, const State &b) {
    if (a.topology != b.topology) return false;
    InterfaceDataDiff::Config config;
    return InterfaceDataDiff(config).Compare(a.data, b.data).identical;
}

inline void ExpectSame(const State &expected, const State &actual, const std::string &what) {
    if (Same(expected, actual)) return;
    std::cerr << what << ": state differs\n";
    if (expected.topology != actual.topology)
        std::cerr << "--- expected topology\n" << expected.topology << "--- actual topology\n" << actual.topology;
    else
        std::cerr << InterfaceDataDiff(InterfaceDataDiff::Config()).Compare(expected.data, actual.data).GetSummary();
    Fail("the check written above", __FILE__, __LINE__);
}

// Logic Building Blocks with at least one behavior input and output: no
// render or input device is needed to create them.
inline std::vector<CKGUID> LogicPrototypes(const BuildingBlockCatalog &catalog, size_t count) {
    std::vector<CKGUID> result;
    for (const auto &entry : catalog.Entries()) {
        if (entry.category.rfind("Logics", 0) != 0) continue;
        CKBehaviorPrototype *prototype = CKGetPrototypeFromGuid(entry.guid);
        if (!prototype || prototype->GetInputCount() < 1 || prototype->GetOutputCount() < 1) continue;
        result.push_back(entry.guid);
        if (result.size() == count) break;
    }
    return result;
}

inline CKGUID IntGuid() { return CKPGUID_INT; }

// Tracks the expected state at every undo depth.
class Recorder {
public:
    Recorder(Document &document, CK_ID script)
        : m_Document(document), m_Undo(document.History()), m_Script(script), m_Base(m_Undo.UndoCount()) {
        m_States.push_back(Capture(document, script));
        CKBehavior *runtime = CKBehavior::Cast(document.Context()->GetObject(script));
        m_Issues = RuntimeConsistencyIssues(document.Context(), document.Session(script)->Data(), runtime);
    }

    // Checks the result of an attempted edit.
    void After(const EditResult &result, const std::string &what) {
        const State now = Capture(m_Document, m_Script);
        if (!result.ok) {
            ExpectSame(m_States[Depth()], now, what + " (refused: " + result.message + ")");
        } else {
            m_States.resize(Depth());
            m_States.push_back(now);
            Trace("applied " + what + " -> depth " + std::to_string(Depth()));
            ++m_Applied;
        }
        CheckConsistency(what);
    }
    void Undo() {
        if (Depth() == 0) return;
        const std::string label = m_Undo.UndoLabel();
        CHECK(m_Undo.Undo());
        Trace("undo " + label + " -> depth " + std::to_string(Depth()));
        ExpectSame(m_States[Depth()], Capture(m_Document, m_Script), "undo " + m_Undo.RedoLabel());
        CheckConsistency("undo");
    }
    void Redo() {
        if (!m_Undo.CanRedo()) return;
        CHECK(m_Undo.Redo());
        Trace("redo " + m_Undo.UndoLabel() + " -> depth " + std::to_string(Depth()));
        ExpectSame(m_States[Depth()], Capture(m_Document, m_Script), "redo " + m_Undo.UndoLabel());
        CheckConsistency("redo");
    }
    void UndoAllRedoAll() {
        const size_t depth = Depth();
        while (Depth() > 0) Undo();
        ExpectSame(m_States[0], Capture(m_Document, m_Script), "undo all");
        // Commands undone by the random steps stay on the redo stack beyond depth.
        while (Depth() < depth) Redo();
        ExpectSame(m_States[depth], Capture(m_Document, m_Script), "redo all");
    }
    int Applied() const { return m_Applied; }

private:
    void Trace(const std::string &what) {
        if (!std::getenv("SCHEMATIC_FUZZ_TRACE")) return;
        CKBehavior *root = CKBehavior::Cast(m_Document.Context()->GetObject(m_Script));
        std::cout << what << " subs";
        for (int i = 0; root && i < root->GetSubBehaviorCount(); ++i) std::cout << " " << root->GetSubBehavior(i)->GetID();
        std::cout << std::endl;
    }
    // Commands applied since the recorder started.
    size_t Depth() const { return m_Undo.UndoCount() - m_Base; }
    void CheckConsistency(const std::string &what) {
        CKBehavior *runtime = CKBehavior::Cast(m_Document.Context()->GetObject(m_Script));
        for (const auto &issue :
             RuntimeConsistencyIssues(m_Document.Context(), m_Document.Session(m_Script)->Data(), runtime))
            if (!m_Issues.count(issue)) {
                std::cerr << what << ": new display/runtime mismatch: " << issue << '\n';
                Fail("the check written above", __FILE__, __LINE__);
            }
    }
    Document &m_Document;
    UndoHistory &m_Undo;
    CK_ID m_Script;
    size_t m_Base;
    std::vector<State> m_States;
    std::set<std::string> m_Issues;
    int m_Applied = 0;
};

// One undo step setting a value the test keeps (no object of the
// composition): redo sets it to after, undo back to what it was.
template <typename T> bool RecordValue(Document &document, const std::string &label, T &value, T after) {
    Edit edit(document, label);
    const T before = value;
    return edit.Do([&value, after] { value = after; return true; }, [&value, before] { value = before; return true; }) &&
           edit.Commit().ok;
}

// What undo has to bring back: the objects in the composition (not
// discarded) with their classes and names, the objects made by edits that it
// saves (the graveyard and the made ones only grow: undo discards rather than
// destroys), the listed Scripts with their topology and display, the watched
// objects' states, and whether the composition is modified.
struct CompositionState {
    std::map<CK_ID, std::string> objects;
    std::set<CK_ID> created;
    std::vector<std::string> scripts; // id, name, owner
    std::map<CK_ID, State> displays;  // the listed Scripts' with a session
    std::map<CK_ID, std::string> watched;
    bool dirty = false;
};

inline CompositionState CaptureComposition(Document &document, const std::vector<CK_ID> &watched = {}) {
    CompositionState state;
    CKContext *context = document.Context();
    const XObjectPointerArray &all = context->GetObjectListByType(CKCID_OBJECT, TRUE);
    for (int i = 0; i < all.Size(); ++i) {
        CKObject *object = all[i];
        if (!object || document.IsDiscarded(object->GetID())) continue;
        state.objects[object->GetID()] =
            std::to_string(object->GetClassID()) + " " + (object->GetName() ? object->GetName() : "");
    }
    for (CK_ID id : document.CreatedObjects())
        if (!document.IsDiscarded(id)) state.created.insert(id);
    for (const ScriptInfo &info : document.Scripts()) {
        state.scripts.push_back(std::to_string(info.id) + " " + info.name + " " + std::to_string(info.ownerId));
        if (document.ExistingSession(info.id)) state.displays[info.id] = Capture(document, info.id);
    }
    for (CK_ID id : watched) {
        std::string &bytes = state.watched[id];
        std::unique_ptr<CKStateChunk, decltype(&DeleteCKStateChunk)> chunk(
            CKSaveObjectState(context->GetObject(id)), &DeleteCKStateChunk);
        if (!chunk) continue;
        chunk->StartRead();
        if (const void *data = chunk->LockReadBuffer())
            bytes.assign(static_cast<const char *>(data), static_cast<size_t>(chunk->GetDataSize()));
    }
    state.dirty = document.Dirty();
    return state;
}

inline void ExpectSameComposition(const CompositionState &expected, const CompositionState &actual,
                                  const std::string &what) {
    std::vector<std::string> differences;
    if (expected.objects != actual.objects) {
        for (const auto &entry : expected.objects)
            if (!actual.objects.count(entry.first)) differences.push_back("object gone: " + entry.second);
        for (const auto &entry : actual.objects) {
            const auto found = expected.objects.find(entry.first);
            if (found == expected.objects.end()) differences.push_back("object added: " + entry.second);
            else if (found->second != entry.second) differences.push_back("object " + found->second + " now " + entry.second);
        }
    }
    if (expected.created != actual.created) differences.push_back("the objects made to save differ");
    if (expected.scripts != actual.scripts) differences.push_back("the Script list differs");
    for (const auto &entry : expected.displays) {
        const auto found = actual.displays.find(entry.first);
        if (found != actual.displays.end() && !Same(entry.second, found->second))
            differences.push_back("Script " + std::to_string(entry.first) + ": topology or display differs");
    }
    if (expected.watched != actual.watched) differences.push_back("a watched object's state differs");
    if (expected.dirty != actual.dirty)
        differences.push_back(std::string("the composition is ") + (actual.dirty ? "modified" : "unmodified"));
    if (differences.empty()) return;
    std::cerr << what << ":\n";
    for (const std::string &difference : differences) std::cerr << "  " << difference << '\n';
    Fail("the check written above", __FILE__, __LINE__);
}

// Runs edit, which must apply as one undo step, then undoes and redoes it
// twice: each undo brings the composition back as it was (unmodified when it
// was), each redo as the edit left it. Returns the edit's result.
inline EditResult ExpectUndoRoundTrip(Document &document, const std::string &what,
                                      const std::function<EditResult()> &edit,
                                      const std::vector<CK_ID> &watched = {}) {
    UndoHistory &undo = document.History();
    if (!document.Dirty()) undo.MarkSavePoint(); // as the editor does when it opens a composition
    const CompositionState before = CaptureComposition(document, watched);
    const size_t steps = undo.UndoCount();
    const EditResult result = edit();
    if (!result.ok) std::cerr << what << ": " << result.message << '\n';
    CHECK(result.ok);
    CHECK(undo.UndoCount() == steps + 1);
    const CompositionState after = CaptureComposition(document, watched);
    for (int pass = 1; pass <= 2; ++pass) {
        CHECK(undo.Undo());
        ExpectSameComposition(before, CaptureComposition(document, watched), what + ": undo " + std::to_string(pass));
        CHECK(undo.Redo());
        ExpectSameComposition(after, CaptureComposition(document, watched), what + ": redo " + std::to_string(pass));
    }
    return result;
}

inline SchematicModel BuildModel(Document &document, CK_ID script) {
    SchematicModel model;
    model.Build(*document.Session(script), document.Context());
    return model;
}

inline LinkEndpoint StartEndpoint(Document &document, CK_ID script) {
    CKBehavior *runtime = CKBehavior::Cast(document.Context()->GetObject(script));
    CKBehaviorIO *start = runtime && runtime->GetInputCount() > 0 ? runtime->GetInput(0) : nullptr;
    return LinkEndpoint(start ? start->GetID() : 0, 0, ENDPOINT_START_BIN);
}

inline const PinItem *FindPin(const SchematicModel &model, CK_ID owner, PinKind kind, int index = 0) {
    for (const auto &pin : model.pins)
        if (pin.owner == owner && pin.kind == kind && pin.index == index) return &pin;
    return nullptr;
}

// A 32-bit BGRA frame as a bottom-up BMP (layout frames for inspection).
inline void WriteFrame(const std::string &path, int width, int height, const std::vector<unsigned char> &bgra) {
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) return;
    const unsigned rowBytes = static_cast<unsigned>(width) * 4, imageBytes = rowBytes * height;
    unsigned char header[54] = {'B', 'M'};
    auto put = [&](int at, unsigned value, int bytes) {
        for (int i = 0; i < bytes; ++i) header[at + i] = static_cast<unsigned char>(value >> (8 * i));
    };
    put(2, 54 + imageBytes, 4);
    put(10, 54, 4);
    put(14, 40, 4);
    put(18, static_cast<unsigned>(width), 4);
    put(22, static_cast<unsigned>(height), 4);
    put(26, 1, 2);
    put(28, 32, 2);
    put(34, imageBytes, 4);
    std::fwrite(header, 1, sizeof(header), file);
    for (int y = height - 1; y >= 0; --y) std::fwrite(&bgra[static_cast<size_t>(y) * rowBytes], 1, rowBytes, file);
    std::fclose(file);
}

// The default suite's cases (main's table; --case NAME runs one).
void TextEncodingChecks();
void NativePathChecks(CkRuntimeHost &host);
void CompositionReadCompatibility(CkRuntimeHost &host);
void ObjectFileOpen(CkRuntimeHost &host);
void OpenFailurePreservation(CkRuntimeHost &host);
std::vector<unsigned char> ChunkBytes(CKStateChunk *chunk);
int CompareReloadedChunks(Document &document, const std::string &path);
void ResourceChecks();
void Catalogs(CKContext *context);
void Edits(CkRuntimeHost &host);
void UndoRoundTrips(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes);
void ViewProjections(CkRuntimeHost &host);
void LayoutGestures(CkRuntimeHost &host);
void BatchCommands(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes);
void ObjectPropertyUndo(CkRuntimeHost &host);
void CkArrays(CkRuntimeHost &host);
void SetupActions(CkRuntimeHost &host);
void NewSceneEnvironment(CkRuntimeHost &host);
void DeterministicEdits(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes);
void SyntheticFuzz(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes, unsigned seed = 20261003u,
                   int steps = 200);
void ThisParameter(CkRuntimeHost &host);
void UnreadableChunks(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes);
void ExportedParameters(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes);
void CrossGraphLinks(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes);
void ReplaceBehaviors(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes);
void OperationCommands(CkRuntimeHost &host);
void LinkAndExportCommands(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes);
void LevelManagerCommands(CkRuntimeHost &host);
void SetupManagerCommands(CkRuntimeHost &host);
void CreateAndSceneCommands(CkRuntimeHost &host);
void ScriptDebugger(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes);
void EditorCommands(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes);
void BuildingBlockCallbacks(CkRuntimeHost &host, const BuildingBlockCatalog &catalog);
void GuardedCalls(CkRuntimeHost &host);
void ManagersAfterClearAll(CkRuntimeHost &host);
void InputViewport(CkRuntimeHost &host);

// The modes on files (0 passed, 1 failed, 2 the file cannot be used).
int SampleFile(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes, const std::string &path,
               const std::string &outputDirectory);
int CreateSaveFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory);
int SpriteTextSaveFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory);
int SpriteTextCheckFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory);
int NewMergeFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory);
int ToolsFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory);
int DuplicatesFile(CkRuntimeHost &host, const std::string &path);
int LayoutInterfaceFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory);
int ObjectPicturesFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory);
int FileMenuFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory);
int LinksFiles(CkRuntimeHost &host, const std::vector<std::string> &paths);
int LayoutFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory);
int LayoutObjectsFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory);

} // namespace vdevtest

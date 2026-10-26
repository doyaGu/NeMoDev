#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "CKAll.h"

#include "core/runtime/FileDuplicates.h"
#include "core/document/ScriptSession.h"
#include "core/document/UndoHistory.h"
#include "core/level/LevelCommands.h"

namespace vdev {

class CkRuntimeHost;
class Edit;

SceneEnvironment DefaultNewSceneEnvironment();

struct ScriptInfo {
    CK_ID id = 0;
    std::string name;
    CK_ID ownerId = 0;
    std::string ownerName;
    bool hasInterfaceChunk = false;
};

// One opened composition. The CK context is the source of topology; each
// Script's display data lives in a lazily created ScriptSession.
class Document {
public:
    class PreparedOpen;
    explicit Document(CkRuntimeHost &host) : m_Host(host), m_History(*this) {}
    ~Document();

    // Immediate load into this context; interactive replacement uses PrepareOpen.
    // Opening .nmo is New + Import: an unsaved, modified composition using
    // the caller's new-scene environment. Other compositions load as saved.
    bool Open(const std::string &path, std::string *error,
              const SceneEnvironment &environment = DefaultNewSceneEnvironment());
    // Load in an independent context. Failure or discarding the candidate
    // leaves this document, its objects and undo history unchanged. Callers
    // release old UI/render state only once preparation succeeds, then Commit.
    std::unique_ptr<PreparedOpen> PrepareOpen(const std::string &path, std::string *error,
                                            const SceneEnvironment &environment = DefaultNewSceneEnvironment());
    // File > New Composition (Dev.exe sub_5C3BC0, sub_5CB640): an empty level
    // named "Level" as the current level, its level scene with Dev's default
    // environment supplied for the new composition.
    bool New(std::string *error, const SceneEnvironment &environment = DefaultNewSceneEnvironment());
    // File > Merge Composition (Dev.exe sub_5C54C0): loads a file into the
    // context and merges its level into the current one (CKLevel::Merge;
    // asScene: Merge Composition as New Scene), then destroys the loaded level.
    // Not undoable. merged: the objects added to the composition.
    // duplicates: the Duplicate Names Found choices (FindDuplicateNames);
    // without them the duplicates are loaded as CK does (Ignore).
    bool Merge(const std::string &path, bool asScene, std::string *error, std::vector<CK_ID> *merged = nullptr,
               const std::vector<DuplicateName> *duplicates = nullptr);
    // Resources > Import File / Import File As (Dev.exe sub_5C48C0): loads a
    // file (Virtools files, or any format a model reader takes) into the
    // current level. Character: loaded with CK_LOAD_ASCHARACTER; Place: a new
    // place named after the file becomes the parent of the loaded 3D
    // entities; Scene: a new scene named after the file holds the loaded
    // objects. Not undoable (as Merge). imported: the objects added.
    enum class ImportMode { File, Character, Place, Scene };
    bool Import(const std::string &path, ImportMode mode, std::string *error, std::vector<CK_ID> *imported = nullptr,
                const std::vector<DuplicateName> *duplicates = nullptr,
                const SceneEnvironment &environment = DefaultNewSceneEnvironment());
    // The objects of a Virtools file named as objects of their classes in the
    // context, which Merge and Import would bring (CKFile::OpenFile with
    // CK_LOAD_CHECKDUPLICATES reads the file's headers only; Dev loads with
    // CK_LOAD_DODIALOG). None for other formats or a file that cannot be read.
    std::vector<DuplicateName> FindDuplicateNames(const std::string &path) const;
    // Objects that joined the composition outside undo (Data Resources
    // files): saved with it from now on.
    void AdoptObjects(const std::vector<CK_ID> &objects);
    void Close();
    bool IsOpen() const { return m_Open; }

    CKContext *Context() const;
    CkRuntimeHost &Host() const { return m_Host; }
    const std::string &SourcePath() const { return m_SourcePath; }
    const std::vector<CK_ID> &LoadedObjects() const { return m_LoadedObjects; }
    const std::vector<ScriptInfo> &Scripts() const { return m_Scripts; }
    const ScriptInfo *FindScript(CK_ID id) const;

    // Opens the Script's session on first use. Returns null for unknown ids.
    ScriptSession *Session(CK_ID scriptId);
    ScriptSession *ExistingSession(CK_ID scriptId);
    const std::map<CK_ID, std::unique_ptr<ScriptSession>> &Sessions() const { return m_Sessions; }

    bool Dirty() const;
    void RefreshScripts();
    // Counts the changes of Scripts() (which Scripts, their names and
    // owners): an undo step bringing one back or taking it away changes it,
    // and the Schematic's stack is made again (EditorApp).
    std::uint64_t ScriptsRevision() const { return m_ScriptsRevision; }

    // Which of the context's objects are the composition's. An
    // object an edit removed is kept alive for undo (never saved, destroyed
    // when the document closes): it and the objects it holds (a deleted
    // Building Block's parameters and I/Os, a deleted Script's behaviors) are
    // discarded. They still reference the composition (their inputs their
    // sources) but are not part of it: whatever lists, counts or looks for
    // the composition's objects takes the live ones.
    bool IsDiscarded(CK_ID id) const;
    // The object exists and is not discarded.
    bool IsLive(CK_ID id) const;
    // The live objects of a class (derived: and of the classes derived from it).
    std::vector<CKObject *> LiveObjects(CK_CLASSID cid, bool derived = true) const;
    // The objects made since the composition was opened (by edits, Include):
    // saved with it unless discarded.
    const std::set<CK_ID> &CreatedObjects() const { return m_CreatedObjects; }
    // An object made outside an edit that is part of the composition from now
    // on (saved with it): the editor's own data objects, what a Building
    // Block's callback or an attribute makes. Marks nothing (AdoptObjects for
    // objects a load brought).
    void Include(CK_ID id) { m_CreatedObjects.insert(id); }
    // A change of the composition outside undo (loading into it, Data
    // Resources, attribute types, the debugger's Reset): it stays modified
    // until it is saved. The views redraw on either kind.
    void MarkTopologyChanged() {
        ++m_TopologyRevision;
        ++m_ChangesOutsideUndo;
    }
    // A change an undo step makes (its edit, undo or redo): undoing back to
    // the save point (UndoHistory::MarkSavePoint) leaves it unmodified.
    void MarkUndoableChange() { ++m_TopologyRevision; }
    // The changes outside undo so far, the document's and its Scripts'.
    std::uint64_t ChangesOutsideUndo() const;
    // The undo history: empty when a composition is opened,
    // made or closed.
    UndoHistory &History() { return m_History; }
    const UndoHistory &History() const { return m_History; }
    // The composition stopped matching the recorded undo steps, e.g. an edit
    // failed and could not be rolled back. Clears the history (a change
    // outside undo); true when it had steps.
    bool InvalidateHistory();
    // An edit is being built (Edit): the history is not undone or redone
    // meanwhile, and an edit begun now is nested in it.
    bool EditOpen() const { return m_OpenEdit != nullptr; }
    std::uint64_t TopologyRevision() const { return m_TopologyRevision; }
    bool TopologyDirty() const { return m_TopologyRevision != m_SavedTopologyRevision; }
    void MarkSaved();
    // SDK bitmap handles that display data borrows, such as loaded
    // Script snapshots; deleted when the document closes.
    void KeepBitmap(void *bitmap);

private:
    // Only edits make and discard objects; the saver reads the sets behind
    // the queries (what it writes).
    friend class Edit;
    friend class UndoStep;
    friend struct DocumentSaverAccess;
    void ListScripts();
    void CommitOpen(PreparedOpen &prepared);

    CkRuntimeHost &m_Host;
    bool m_Open = false;
    std::string m_SourcePath;
    std::vector<CK_ID> m_LoadedObjects;
    std::vector<ScriptInfo> m_Scripts;
    std::map<CK_ID, std::unique_ptr<ScriptSession>> m_Sessions;
    std::set<CK_ID> m_Graveyard;
    std::set<CK_ID> m_CreatedObjects;
    std::vector<std::shared_ptr<void>> m_Bitmaps;
    std::uint64_t m_TopologyRevision = 0;
    std::uint64_t m_SavedTopologyRevision = 0;
    std::uint64_t m_ChangesOutsideUndo = 0; // with those of the sessions gone
    std::uint64_t m_ScriptsRevision = 0;
    // Its steps hold objects and state chunks: cleared before the objects go
    // (Close).
    UndoHistory m_History;
    Edit *m_OpenEdit = nullptr; // the innermost
};

class Document::PreparedOpen {
public:
    ~PreparedOpen();
    // Consumes the loaded candidate. Context/object pointers from the old
    // document must not be used after this call; the Document itself stays.
    void Commit();

private:
    friend class Document;
    explicit PreparedOpen(Document &target);
    Document &m_Target;
    std::unique_ptr<CkRuntimeHost> m_Host;
    std::unique_ptr<Document> m_Document;
};

} // namespace vdev

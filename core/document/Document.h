#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "CKAll.h"

#include "core/runtime/FileDuplicates.h"
#include "editor/Identity.h"
#include "core/document/ScriptSession.h"
#include "core/document/UndoHistory.h"
#include "core/document/ObjectPictures.h"
#include "core/layout/LayoutInterface.h"
#include "core/level/LevelCommands.h"

namespace vdev {

class CkRuntimeHost;
class Edit;
class Document;

// Keeps runtime copy sources alive independently of Undo/Redo. Closing the
// document detaches outstanding references before its context is cleared.
class ObjectReferences {
public:
    ~ObjectReferences();
    ObjectReferences(const ObjectReferences &) = delete;
    ObjectReferences &operator=(const ObjectReferences &) = delete;
    bool IsValid() const;
    bool BelongsTo(const Document &document) const;
private:
    friend class Document;
    ObjectReferences(Document &document, std::set<ObjectRef> objects);
    Document *m_Document;
    std::set<ObjectRef> m_Objects;
};

SceneEnvironment DefaultNewSceneEnvironment();

struct ScriptInfo {
    ObjectRef reference{}, ownerReference{};
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
    enum class Origin {
        NewComposition,
        LoadedComposition,
        ImportedObjects,
    };
    class PreparedDocument;
    explicit Document(CkRuntimeHost &host);
    ~Document();
    // Construction attaches an editing scope to the host's current context.
    // Successful New/Open replaces it; failed candidates do not. Close ends
    // the scope. Viewport recreation is independent of this identity.
    DocumentToken Token() const;
    enum class ObjectScope { Live, Existing };
    ObjectRef Reference(CK_ID id) const;
    std::vector<ObjectRef> References(const std::vector<CK_ID> &ids) const;
    CKObject *Resolve(ObjectRef reference, CK_CLASSID cid = CKCID_OBJECT,
                      ObjectScope scope = ObjectScope::Live) const;
    CK_ID NativeId(ObjectRef reference, ObjectScope scope = ObjectScope::Live) const;

    // Immediate load into this context; interactive replacement uses PrepareOpen.
    // Opening .nmo is New + Import: an unsaved, modified composition using
    // the caller's new-scene environment. Other compositions load as saved.
    bool Open(const std::string &path, std::string *error,
              const SceneEnvironment &environment = DefaultNewSceneEnvironment());
    // Prepare in an independent context. Failure or discarding the candidate
    // leaves this document, its objects and undo history unchanged. Callers
    // release old UI/render state only once preparation succeeds, then Commit.
    std::unique_ptr<PreparedDocument> PrepareOpen(const std::string &path, std::string *error,
                                            const SceneEnvironment &environment = DefaultNewSceneEnvironment());
    std::unique_ptr<PreparedDocument> PrepareNew(std::string *error, const SceneEnvironment &environment);
    // File > New Composition (Dev.exe sub_5C3BC0, sub_5CB640): an empty level
    // named "Level" as the current level, its level scene with Dev's default
    // environment supplied for the new composition.
    bool New(std::string *error, const SceneEnvironment &environment = DefaultNewSceneEnvironment());
    // File > Merge Composition (Dev.exe sub_5C54C0): loads a file into the
    // context and merges its level into the current one (CKLevel::Merge;
    // asScene: Merge Composition as New Scene), then destroys the loaded level.
    // Not undoable. Failure restores the same objects, sessions and undo
    // history; merged is appended only on success.
    // duplicates: the Duplicate Names Found choices (FindDuplicateNames);
    // without them the duplicates are loaded as CK does (Ignore).
    bool Merge(const std::string &path, bool asScene, std::string *error, std::vector<CK_ID> *merged = nullptr,
               const std::vector<DuplicateName> *duplicates = nullptr);
    // Resources > Import File / Import File As (Dev.exe sub_5C48C0): loads a
    // file (Virtools files, or any format a model reader takes) into the
    // current level. Character: loaded with CK_LOAD_ASCHARACTER; Place: a new
    // place named after the file becomes the parent of the loaded 3D
    // entities; Scene: a new scene named after the file holds the loaded
    // objects. Not undoable (as Merge); failure leaves the current document
    // and imported unchanged. imported: the objects added.
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

    // SDK access for queries and runtime integration. Persistent changes
    // belong in commands using Edit (actions, objects and display together),
    // or in the document's explicit non-undo operations such as Import.
    CKContext *Context() const;
    CkRuntimeHost &Host() const { return m_Host; }
    const std::string &SourcePath() const { return m_SourcePath; }
    // How this composition was created; independent of its eventual save path.
    Origin CreatedFrom() const { return m_Origin; }
    std::vector<CK_ID> LoadedObjects() const;
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
    // once no undo/redo step needs it): it and the objects it holds (a deleted
    // Building Block's parameters and I/Os, a deleted Script's behaviors) are
    // discarded. They still reference the composition (their inputs their
    // sources) but are not part of it: whatever lists, counts or looks for
    // the composition's objects takes the live ones.
    bool IsDiscarded(CK_ID id) const;
    // The object exists and is not discarded.
    bool IsLive(CK_ID id) const;
    // The live objects of a class (derived: and of the classes derived from it).
    std::vector<CKObject *> LiveObjects(CK_CLASSID cid, bool derived = true) const;
    std::shared_ptr<ObjectReferences> RetainObjects(const std::vector<CK_ID> &objects);
    // The objects made since the composition was opened (by edits, Include):
    // saved with it unless discarded.
    std::set<CK_ID> CreatedObjects() const;
    // An object made outside an edit that is part of the composition from now
    // on (saved with it): the editor's own data objects, what a Building
    // Block's callback or an attribute makes. Marks nothing (AdoptObjects for
    // objects a load brought).
    void Include(CK_ID id);
    // A change of the composition outside undo (loading into it, Data
    // Resources, attribute types, the debugger's Reset): refreshes the Script
    // list and runtime display fields, then stays modified until saved.
    // Object ownership still belongs to Edit::Created or AdoptObjects.
    void MarkTopologyChanged();
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
    // Committed content, including Script display data. Live previews and
    // cancelled edits do not advance this version.
    std::uint64_t ChangeRevision() const { return m_ChangeRevision; }
    std::uint64_t TopologyRevision() const { return m_TopologyRevision; }
    bool TopologyDirty() const { return m_TopologyRevision != m_SavedTopologyRevision; }
    void MarkSaved();
    // Persistent editor data. Edits use the core's Layout/ObjectPicture commands;
    // selection, focus and open windows belong to the UI.
    const LayoutLists &Layout() const { return m_Layout; }
    const ObjectPictures &Pictures() const { return m_Pictures; }
    // Changes not yet installed in the runtime's picture chunks. A failed or
    // rejected save must leave them available to the next attempt.
    bool PicturesPending() const { return m_PicturesChanged; }
    // Adopt an SDK bitmap and return its shared owner (empty for nullptr).
    // Repeated adoption shares that owner. Display data/history and pictures
    // retain it; collection drops the document's unused staging reference.
    std::shared_ptr<void> KeepBitmap(void *bitmap);
    std::shared_ptr<void> BitmapOwner(void *bitmap) const;
    void CollectBitmaps();

private:
    // Only edits make and discard objects; the saver reads the sets behind
    // the queries (what it writes).
    friend class Edit;
    friend class UndoStep;
    friend class UndoHistory;
    friend class ScriptSession;
    friend class ObjectReferences;
    friend struct DocumentSaverAccess;
    friend class LoadTransaction;
    friend class LayoutListAction;
    friend class FrameRepresentationAction;
    friend class ObjectPictureAction;
    bool MergeInPlace(const std::string &path, bool asScene, std::string *error, std::vector<CK_ID> *merged,
                      const std::vector<DuplicateName> *duplicates);
    bool ImportInPlace(const std::string &path, ImportMode mode, std::string *error, std::vector<CK_ID> *imported,
                       const std::vector<DuplicateName> *duplicates, const SceneEnvironment &environment);
    // Only an Edit/UndoStep may claim that a change belongs to undo.
    void MarkUndoableChange() { ++m_TopologyRevision; }
    void Changed() { ++m_ChangeRevision; }
    void ListScripts();
    std::unique_ptr<PreparedDocument> PrepareDocument(const std::string *path, std::string *error,
                                                      const SceneEnvironment &environment);
    void CommitPrepared(PreparedDocument &prepared);
    // Run only after edits/replay have ended and discarded steps released
    // their actions/state chunks. Live objects never enter this collection.
    void CollectGraveyard();
    void RequestGraveyardCollection();
    void SetDiscarded(ObjectRef object, bool discarded);
    // Creating, discarding or restoring objects changes which editor records
    // belong in the saved composition, even when their values stay the same.
    void EditorObjectsChanged() {
        m_LayoutChanged = true;
        if (!m_Pictures.empty()) m_PicturesChanged = true;
    }

    static std::uint64_t NextToken();
    DocumentToken m_Token{};
    std::uint64_t m_ContextGeneration = 0;
    CkRuntimeHost &m_Host;
    bool m_Open = false;
    Origin m_Origin = Origin::NewComposition;
    std::string m_SourcePath;
    std::vector<CKObjectHandle> m_LoadedObjects;
    std::vector<ScriptInfo> m_Scripts;
    std::map<CK_ID, std::unique_ptr<ScriptSession>> m_Sessions;
    // Membership records carry SDK lifetimes. Reused IDs cannot inherit the
    // former object's ownership or discarded status.
    std::map<CK_ID, CKQWORD> m_Graveyard;
    std::set<ObjectReferences *> m_ObjectReferences;
    std::map<CK_ID, CKQWORD> m_CreatedObjects;
    std::unordered_map<void *, std::shared_ptr<void>> m_Bitmaps;
    LayoutLists m_Layout;
    ObjectPictures m_Pictures;
    bool m_LayoutChanged = false;
    bool m_PicturesChanged = false;
    std::uint64_t m_ChangeRevision = 0;
    std::uint64_t m_TopologyRevision = 0;
    std::uint64_t m_SavedTopologyRevision = 0;
    std::uint64_t m_ChangesOutsideUndo = 0; // with those of the sessions gone
    std::uint64_t m_ScriptsRevision = 0;
    // Its steps hold objects and state chunks: cleared before the objects go
    // (Close).
    UndoHistory m_History;
    Edit *m_OpenEdit = nullptr; // the innermost
    bool m_CollectingGraveyard = false;
    bool m_GraveyardCollectionPending = false;
};

class Document::PreparedDocument {
public:
    ~PreparedDocument();
    // Consumes the prepared composition. Context/object pointers from the old
    // document must not be used after this call; the Document itself stays.
    void Commit();
    Document &Candidate() { return *m_Document; }

private:
    friend class Document;
    explicit PreparedDocument(Document &target);
    Document &m_Target;
    std::unique_ptr<CkRuntimeHost> m_Host;
    std::unique_ptr<Document> m_Document;
};

} // namespace vdev

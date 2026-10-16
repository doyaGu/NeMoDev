#include "core/document/SpriteTextPersistence.h"

#include <set>
#include <vector>

#include "core/document/Document.h"
#include "core/runtime/SdkCompat.h"

namespace vdev {

namespace {

const CKGUID ALIGNMENTS_GUID(0x58C77A26, 0x61DF2D43);
constexpr CKDWORD ALIGNMENT_IDENTIFIER = 0x56445441;
constexpr int VERSION = 1;

CKObject *LoadedObject(CKFile &file, const CKFileObject &entry) {
    // Loading can destroy objects again; a keep-current choice has no new state.
    if (entry.Options == CKFileObject::CK_FO_DONTLOADOBJECT) return nullptr;
    CKObject *object = file.m_Context->GetObject(entry.CreatedObject);
    return object == entry.ObjPtr ? object : nullptr;
}

} // namespace

void WriteSpriteTextAlignments(Document &document) {
    CKContext *context = document.Context();
    std::vector<CKInterfaceObjectManager *> managers;
    for (CKObject *object : document.LiveObjects(CKCID_INTERFACEOBJECTMANAGER)) {
        auto *manager = InterfaceObjectManagerOf(object);
        if (manager->GetGuid() == ALIGNMENTS_GUID) managers.push_back(manager);
    }
    std::set<CK_ID> included(document.LoadedObjects().begin(), document.LoadedObjects().end());
    included.insert(document.CreatedObjects().begin(), document.CreatedObjects().end());
    std::vector<CKSpriteText *> sprites;
    for (CKObject *object : document.LiveObjects(CKCID_SPRITETEXT))
        if (included.count(object->GetID()) && !(object->GetObjectFlags() & CK_OBJECT_NOTTOBESAVED))
            sprites.push_back(CKSpriteText::Cast(object));
    if (managers.empty() && !sprites.empty()) {
        auto *manager = InterfaceObjectManagerOf(context->CreateObject(CKCID_INTERFACEOBJECTMANAGER));
        if (!manager) return;
        manager->SetGuid(ALIGNMENTS_GUID);
        document.Include(manager->GetID());
        managers.push_back(manager);
    }
    // A merge can bring another manager. Rebuild one live list, removing old
    // references too (an undone creation or a deleted text must not be saved).
    for (auto *manager : managers)
        while (manager->GetChunkCount()) manager->RemoveStateChunk(manager->GetChunk(0));
    for (CKSpriteText *text : sprites) {
        CKStateChunk *chunk = CreateCKStateChunk(1, nullptr);
        chunk->StartWrite();
        chunk->WriteIdentifier(ALIGNMENT_IDENTIFIER);
        chunk->WriteInt(VERSION);
        chunk->WriteObjectID(text->GetID());
        chunk->WriteDword(static_cast<CKDWORD>(SpriteTextAlign(text)));
        chunk->CloseChunk();
        managers.front()->AddStateChunk(chunk);
    }
}

void RestoreSpriteTextAlignments(CKFile &file, const std::vector<DuplicateName> *duplicates) {
    std::set<CK_ID> sprites;
    std::vector<CKInterfaceObjectManager *> managers;
    std::set<int> kept;
    // 2.1's duplicate callback leaves Use Current with Options == 1, which
    // the SDK calls CK_FO_RENAMEOBJECT. Use the actual requested choice too.
    if (duplicates)
        for (const auto &duplicate : *duplicates)
            if (duplicate.action == DuplicateAction::UseCurrent && duplicate.fileIndex >= 0 &&
                duplicate.fileIndex < file.m_FileObjects.Size()) {
                const auto &entry = file.m_FileObjects[duplicate.fileIndex];
                if (entry.ObjectCid == duplicate.cid && duplicate.name == (entry.Name ? entry.Name : ""))
                    kept.insert(duplicate.fileIndex);
            }
    for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
        if (kept.count(i)) continue;
        CKObject *object = LoadedObject(file, file.m_FileObjects[i]);
        if (!object) continue;
        if (CKSpriteText::Cast(object)) sprites.insert(object->GetID());
        if (auto *manager = InterfaceObjectManagerOf(object))
            if (manager->GetGuid() == ALIGNMENTS_GUID) managers.push_back(manager);
    }
    for (auto *manager : managers)
        for (int i = 0; i < manager->GetChunkCount(); ++i) {
            CKStateChunk *chunk = manager->GetChunk(i);
            if (!chunk || chunk->GetDataSize() < 5 * static_cast<int>(sizeof(CKDWORD))) continue;
            chunk->StartRead();
            if (!chunk->SeekIdentifier(ALIGNMENT_IDENTIFIER) || chunk->ReadInt() != VERSION) continue;
            const CK_ID id = chunk->ReadObjectID();
            const CKDWORD alignment = chunk->ReadDword();
            if (!sprites.count(id) || (alignment & ~CKDWORD(0x7F))) continue;
            if (auto *text = CKSpriteText::Cast(file.m_Context->GetObject(id)))
                text->SetAlign(static_cast<CKSPRITETEXT_ALIGNMENT>(alignment));
        }
}

std::map<CK_ID, int> CaptureSpriteTextAlignments(CKFile &file) {
    std::map<CK_ID, int> values;
    for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
        const auto &entry = file.m_FileObjects[i];
        // Object is a runtime ID while saving and the original saved ID on load.
        CKObject *object = file.m_Context->GetObject(entry.CreatedObject ? entry.CreatedObject : entry.Object);
        if (auto *text = CKSpriteText::Cast(object)) values[entry.Object] = static_cast<int>(SpriteTextAlign(text));
    }
    return values;
}

} // namespace vdev

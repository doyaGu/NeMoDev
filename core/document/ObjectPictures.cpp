#include "core/document/ObjectPictures.h"

#include <vector>

#include "core/document/Document.h"
#include "core/runtime/SdkCompat.h"

namespace vdev {

namespace {

constexpr CKDWORD PICTURE_IDENTIFIER = 2;

std::vector<CKInterfaceObjectManager *> PictureManagers(CKContext *context) {
    std::vector<CKInterfaceObjectManager *> managers;
    const XObjectPointerArray &list = context->GetObjectListByType(CKCID_INTERFACEOBJECTMANAGER, TRUE);
    for (int i = 0; i < list.Size(); ++i)
        if (CKInterfaceObjectManager *manager = InterfaceObjectManagerOf(list[i]))
            if (manager->GetGuid() == OBJECT_PICTURES_GUID) managers.push_back(manager);
    return managers;
}

} // namespace

std::map<CK_ID, void *> ReadObjectPictures(Document &document) {
    std::map<CK_ID, void *> pictures;
    CKContext *context = document.Context();
    if (!context) return pictures;
    // sub_5D1D60: each chunk with the identifier, its object and bitmap.
    for (CKInterfaceObjectManager *manager : PictureManagers(context))
        for (int i = 0; i < manager->GetChunkCount(); ++i) {
            CKStateChunk *chunk = manager->GetChunk(i);
            if (!chunk) continue;
            chunk->StartRead();
            if (!chunk->SeekIdentifier(PICTURE_IDENTIFIER)) continue;
            const CK_ID object = chunk->ReadObjectID();
            void *bitmap = chunk->ReadBitmap();
            if (!bitmap) continue;
            document.KeepBitmap(bitmap);
            pictures[object] = bitmap;
        }
    return pictures;
}

void WriteObjectPictures(Document &document, const std::map<CK_ID, void *> &pictures) {
    CKContext *context = document.Context();
    if (!context) return;
    std::vector<CKInterfaceObjectManager *> managers = PictureManagers(context);
    if (managers.empty()) {
        CKInterfaceObjectManager *manager =
            InterfaceObjectManagerOf(context->CreateObject(CKCID_INTERFACEOBJECTMANAGER));
        if (!manager) return;
        manager->SetGuid(OBJECT_PICTURES_GUID);
        document.Include(manager->GetID());
        managers.push_back(manager);
    }
    // One object holds them all (a merge may have brought another).
    for (CKInterfaceObjectManager *manager : managers)
        while (manager->GetChunkCount() > 0) manager->RemoveStateChunk(manager->GetChunk(0));
    // sub_5D1C90.
    for (const auto &picture : pictures) {
        if (!picture.second || !context->GetObject(picture.first)) continue;
        CKStateChunk *chunk = CreateCKStateChunk(1, nullptr);
        chunk->StartWrite();
        chunk->WriteIdentifier(PICTURE_IDENTIFIER);
        chunk->WriteObjectID(picture.first);
        chunk->WriteBitmap(picture.second, nullptr);
        chunk->CloseChunk();
        managers.front()->AddStateChunk(chunk);
    }
}

} // namespace vdev

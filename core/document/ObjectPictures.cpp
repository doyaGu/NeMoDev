#include "core/document/ObjectPictures.h"

#include <vector>

#include "core/document/Document.h"
#include "core/document/Edit.h"
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

ObjectPictures ReadObjectPictures(Document &document) {
    ObjectPictures pictures;
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
            if (!context->GetObject(object)) continue;
            void *bitmap = chunk->ReadBitmap();
            if (!bitmap) continue;
            pictures[object] = document.KeepBitmap(bitmap);
        }
    return pictures;
}

void WriteObjectPictures(Document &document, const ObjectPictures &pictures) {
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
        if (!picture.second || !document.IsLive(picture.first)) continue;
        CKStateChunk *chunk = CreateCKStateChunk(1, nullptr);
        chunk->StartWrite();
        chunk->WriteIdentifier(PICTURE_IDENTIFIER);
        chunk->WriteObjectID(picture.first);
        chunk->WriteBitmap(picture.second.get(), nullptr);
        chunk->CloseChunk();
        managers.front()->AddStateChunk(chunk);
    }
}

class ObjectPictureAction : public Action {
public:
    ObjectPictureAction(Document &document, CK_ID object, std::shared_ptr<void> before, std::shared_ptr<void> after)
        : m_Object(document.Reference(object)), m_Before(std::move(before)), m_After(std::move(after)) {}
    bool Redo(Document &document) override { return Apply(document, m_After); }
    bool Undo(Document &document) override { return Apply(document, m_Before); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        if (const auto id = document.NativeId(m_Object, Document::ObjectScope::Existing))
            objects.insert(id);
    }

private:
    bool Apply(Document &document, const std::shared_ptr<void> &picture) {
        const auto id = document.NativeId(m_Object, Document::ObjectScope::Existing);
        if (!id)
            return false;
        if (picture)
            document.m_Data.pictures[id] = picture;
        else
            document.m_Data.pictures.erase(id);
        document.m_Data.picturesChanged = true;
        return true;
    }
    ObjectRef m_Object;
    std::shared_ptr<void> m_Before, m_After;
};

EditResult SetObjectPicture(Document &document, CK_ID object, void *bitmap) {
    // Adopt before checking the target so a transferred bitmap is never leaked.
    auto after = document.KeepBitmap(bitmap);
    if (!document.IsOpen() || !document.IsLive(object)) {
        after.reset();
        document.CollectBitmaps();
        return EditResult::Refused("No live object");
    }
    const auto found = document.Pictures().find(object);
    auto before = found != document.Pictures().end() ? found->second : nullptr;
    if (before == after) return EditResult::NoChange();
    Edit edit(document, "Object Picture");
    if (!edit.Do(std::make_unique<ObjectPictureAction>(document, object, std::move(before), std::move(after))))
        return edit.Fail("Object Picture failed");
    return edit.Commit();
}

} // namespace vdev

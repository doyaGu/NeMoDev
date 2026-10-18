#include "VirtoolsDevTestSupport.h"
#include "ObjectEditTestObject.h"

#include <stdexcept>

#include "core/level/ObjectEditCommands.h"

namespace vdevtest {


void ObjectEditFailures(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *first = new PropertyTestObject(document.Context());
    auto *second = new PropertyTestObject(document.Context());
    document.Include(first->GetID());
    document.Include(second->GetID());
    document.MarkSaved();
    auto &history = document.History();
    history.MarkSavePoint();
    auto set = [](CKObject *object) { static_cast<PropertyTestObject *>(object)->value = 10; };

    first->failSave = first->saves + 1;
    int writes = 0;
    EditResult result = EditObjectState(document, first->GetID(), "Capture before", [&](CKObject *) { ++writes; });
    CHECK(result.Status() == EditStatus::Error && writes == 0);
    CHECK(first->value == 0 && !document.Dirty() && !history.CanUndo());

    first->throwSave = first->saves + 1;
    result = EditObjectState(document, first->GetID(), "Capture exception", [&](CKObject *) { ++writes; });
    CHECK(result.Status() == EditStatus::Error && writes == 0 && first->value == 0);

    // A failed capture after a write must restore the original value and
    // report Error; it must not become NoChange or leave an untracked edit.
    first->failSave = first->saves + 2;
    result = EditObjectState(document, first->GetID(), "Capture after", set);
    CHECK(result.Status() == EditStatus::Error);
    CHECK(first->value == 0 && !document.Dirty() && !history.CanUndo());

    second->failSave = second->saves + 1;
    writes = 0;
    result = EditObjectsState(document, {first->GetID(), second->GetID()}, "Batch capture before", [&](CKObject *) { ++writes; });
    CHECK(result.Status() == EditStatus::Error && writes == 0);
    CHECK(first->value == 0 && second->value == 0 && !document.Dirty());

    second->failSave = second->saves + 2;
    result = EditObjectsState(document, {first->GetID(), second->GetID()}, "Batch capture after", set);
    CHECK(result.Status() == EditStatus::Error);
    CHECK(first->value == 0 && second->value == 0 && !document.Dirty() && !history.CanUndo());

    result = EditObjectsState(document, {first->GetID(), second->GetID()}, "Setter exception", [](CKObject *object) {
        static_cast<PropertyTestObject *>(object)->value = 30;
        throw std::runtime_error("Property setter failed after writing");
    });
    CHECK(result.Status() == EditStatus::Error);
    CHECK(first->value == 0 && second->value == 0 && !document.Dirty() && !history.CanUndo());

    result = EditObjectState(document, first->GetID(), "Property", set);
    CHECK(result.ok && first->value == 10 && document.Dirty());
    CHECK(history.Undo() && first->value == 0 && !document.Dirty());
    CHECK(history.Redo() && first->value == 10 && document.Dirty());
    CHECK(history.Undo() && !document.Dirty());
    first->failSave = first->saves + 2;
    result = EditObjectState(document, first->GetID(), "Preserve redo", set);
    CHECK(result.Status() == EditStatus::Error && first->value == 0);
    CHECK(!document.Dirty() && history.CanRedo() && history.Redo() && first->value == 10);

    // A failed restoration has a different observable result: keep the
    // document modified and clear a history that can no longer be trusted.
    first->failSave = first->saves + 2;
    first->failLoad = first->loads + 1;
    result = EditObjectState(document, first->GetID(), "Recovery fails", [](CKObject *object) {
        static_cast<PropertyTestObject *>(object)->value = 20;
    });
    CHECK(result.Status() == EditStatus::Error && first->value == -100);
    CHECK(document.Dirty() && !history.CanUndo() && !history.CanRedo());
}

void ObjectEditCancellation(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *first = new PropertyTestObject(document.Context());
    auto *second = new PropertyTestObject(document.Context());
    document.Include(first->GetID());
    document.Include(second->GetID());
    auto *texture = CKTexture::Cast(document.Context()->CreateObject(CKCID_TEXTURE, "Cancel texture"));
    CHECK(texture && texture->Create(2, 2, 32, 0));
    texture->SetSlotCount(2);
    CHECK(texture->Create(2, 2, 32, 1));
    texture->SetPixel(0, 0, 0xFF123456, 0);
    texture->ReleaseSurfacePtr(0);
    texture->SetPixel(0, 0, 0xFF789ABC, 1);
    texture->ReleaseSurfacePtr(1);
    texture->SetCurrentSlot(1);
    document.Include(texture->GetID());
    auto *text = CKSpriteText::Cast(document.Context()->CreateObject(CKCID_SPRITETEXT, "Cancel text"));
    CHECK(text && text->Create(64, 16, 32, 0));
    text->SetText("Before");
    const auto align = static_cast<CKSPRITETEXT_ALIGNMENT>(CKSPRITETEXT_RIGHT | CKSPRITETEXT_BOTTOM);
    text->SetAlign(align);
    document.Include(text->GetID());
    document.MarkSaved();
    document.History().MarkSavePoint();

    ObjectsGesture edit;
    second->failSave = second->saves + 1;
    CHECK(!edit.Begin(document, {first->GetID(), second->GetID()}));
    CHECK(!edit.Active() && edit.Objects().empty() && first->value == 0);
    CHECK(edit.Begin(document, {first->GetID(), second->GetID(), first->GetID()}));
    CHECK(edit.Objects().size() == 2);
    first->value = 10;
    second->value = 20;
    CHECK(edit.Cancel(document));
    CHECK(!edit.Active() && first->value == 0 && second->value == 0);
    CHECK(!document.Dirty() && !document.History().CanUndo());

    CHECK(edit.Begin(document, {texture->GetID(), text->GetID()}));
    texture->SetSlotCount(3);
    texture->SetCurrentSlot(0);
    texture->SetPixel(0, 0, 0xFF000000, 0);
    texture->ReleaseSurfacePtr(0);
    text->SetText("Changed");
    text->SetAlign(CKSPRITETEXT_LEFT);
    CHECK(edit.Cancel(document));
    CHECK(texture->GetSlotCount() == 2 && texture->GetCurrentSlot() == 1);
    CHECK(texture->GetPixel(0, 0, 0) == 0xFF123456 && texture->GetPixel(0, 0, 1) == 0xFF789ABC);
    CHECK(properties::SpriteTextText.get(text) == "Before" && properties::SpriteTextAlignment.get(text) == align);
    CHECK(!document.Dirty() && !document.History().CanUndo());

    CHECK(edit.Begin(document, {first->GetID()}));
    CHECK(edit.Commit(document, "Unchanged").Unchanged());
    CHECK(!edit.Commit(document, "Already ended").Unchanged());
    CHECK(!document.Dirty() && !document.History().CanUndo());

    CHECK(edit.Begin(document, {first->GetID(), second->GetID()}));
    first->value = 10;
    second->value = 20;
    CHECK(edit.Commit(document, "Batch preview").ok);
    CHECK(document.History().Undo() && first->value == 0 && second->value == 0 && !document.Dirty());
    CHECK(document.History().Redo() && first->value == 10 && second->value == 20);
    first->failLoad = first->loads + 1;
    CHECK(!document.History().Undo());
    CHECK(first->value == 10 && second->value == 20); // Includes compensation of the failing object.
    CHECK(document.Dirty() && !document.History().CanUndo() && !document.History().CanRedo());

    CHECK(edit.Begin(document, {first->GetID(), second->GetID()}));
    first->value = 30;
    second->value = 40;
    second->failLoad = second->loads + 1;
    CHECK(!edit.Cancel(document));
    CHECK(first->value == 10 && second->value == -100 && document.Dirty());
    CHECK(!edit.Active() && !document.History().CanUndo());

    CHECK(edit.Begin(document, {first->GetID()}));
    document.Close();
    CHECK(document.New(nullptr));
    auto *replacement = new PropertyTestObject(document.Context());
    replacement->value = 99;
    const EditResult stale = edit.Commit(document, "Closed document");
    CHECK(stale.Status() == EditStatus::Error && replacement->value == 99);
    CHECK(!document.Dirty() && !document.History().CanUndo());
}

} // namespace vdevtest

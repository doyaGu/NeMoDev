#include "VirtoolsDevTestSupport.h"
#include "ObjectEditTestObject.h"

#include <cstddef>
#include <cstring>

#include "core/level/LevelActionRecord.h"
#include "core/level/ObjectEditCommands.h"
#include "core/graph/ValueCommands.h"

namespace vdevtest {
namespace {
// Force allocator-address reuse as well as native-ID reuse. A pointer/class
// comparison alone must not make the old reference name the replacement.
class ReusedObject : public PropertyTestObject {
public:
    explicit ReusedObject(CKContext *context) : PropertyTestObject(context) { SetName("Identity reuse"); }
    static void *operator new(std::size_t);
    static void operator delete(void *) noexcept {}
};
alignas(ReusedObject) unsigned char reusedStorage[sizeof(ReusedObject)];
void *ReusedObject::operator new(std::size_t) { return reusedStorage; }

}

void ObjectIdentities(CkRuntimeHost &host) {
    Document document(host);
    const auto attached = document.Token();
    CHECK(attached.value && document.Reference(0) == ObjectRef{});
    CHECK(document.New(nullptr));
    CHECK(document.Token() != attached);
    auto *context = document.Context();
    const auto token = document.Token();
    auto *object = new ReusedObject(context);
    const auto id = object->GetID();
    const auto reference = document.Reference(id);
    CHECK(token.value && reference.document == token && reference.object);
    CHECK(reference == document.Reference(id));
    CHECK(document.Resolve(reference) == object);
    CHECK(!document.Resolve(reference, CKCID_TEXTURE));
    CHECK(document.NativeId(reference) == id);
    CHECK(document.Reference(0) == ObjectRef{} && document.Reference(0xFFFFFFFF) == ObjectRef{});
    auto foreign = reference;
    ++foreign.document.value;
    CHECK(!document.Resolve(foreign));
    foreign = reference;
    ++foreign.object;
    CHECK(!document.Resolve(foreign));

    // This is a real native call, not a test-side registry notification.
    CHECK(GuardedCall([&] { CHECK(context->DestroyObject(id, CK_DESTROY_FREEID) == CK_OK); }, &host, "Identity deletion"));
    auto *replacement = new ReusedObject(context);
    CHECK(replacement == object && replacement->GetID() == id);
    const auto next = document.Reference(id);
    CHECK(next != reference && document.Resolve(next) == replacement);
    CHECK(!document.Resolve(reference));

    const auto first = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(first.ok);
    const auto live = document.Reference(first.created);
    auto retained = document.RetainObjects({first.created});
    {
        Edit edit(document, "Identity graveyard");
        edit.Bury(first.created);
        CHECK(edit.Commit().ok);
    }
    CHECK(!document.Resolve(live));
    CHECK(document.Resolve(live, CKCID_3DENTITY, Document::ObjectScope::Existing));
    CHECK(document.History().Undo());
    CHECK(document.Reference(first.created) == live && document.Resolve(live));
    CHECK(document.History().Redo());
    document.History().Clear();
    CHECK(document.Resolve(live, CKCID_OBJECT, Document::ObjectScope::Existing));
    retained.reset();
    CHECK(!document.Resolve(live, CKCID_OBJECT, Document::ObjectScope::Existing));

    CHECK(document.Token() == token);
    std::string error;
    CHECK(!document.PrepareOpen("/missing/object-identity.cmo", &error));
    CHECK(document.Token() == token && document.Resolve(next) == replacement);
    document.Close();
    CHECK(document.Token() == DocumentToken{} && !document.Resolve(next));
    CHECK(document.New(nullptr));
    CHECK(document.Token() != token && !document.Resolve(next));
    const auto current = document.Reference(document.Context()->GetCurrentLevel()->GetID());
    host.ClearAll();
    CHECK(!document.Resolve(current) && document.Token() == DocumentToken{});
    CHECK(document.New(nullptr));
    CHECK(document.Token() != current.document);
}

void SilentObjectDestruction(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *context = document.Context();
    auto *object = new ReusedObject(context);
    const auto id = object->GetID();
    const auto reference = document.Reference(id);
    CHECK(context->DestroyObject(object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
    auto *replacement = new ReusedObject(context);
    CHECK(replacement == object && replacement->GetID() == id);
    CHECK(!document.Resolve(reference));
    CHECK(document.Reference(id) != reference);

    auto *a = context->CreateObject(CKCID_OBJECT, "Identity batch");
    auto *b = context->CreateObject(CKCID_OBJECT, "Identity dynamic", CK_OBJECTCREATION_DYNAMIC);
    CHECK(a && b && b->IsDynamic());
    const auto ra = document.Reference(a->GetID()), rb = document.Reference(b->GetID());
    CK_ID ids[] = {a->GetID(), b->GetID()};
    CHECK(context->DestroyObjects(ids, 2, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
    CHECK(!document.Resolve(ra) && !document.Resolve(rb));
    CHECK(context->DestroyObject(CK_ID(0)) == CKERR_INVALIDPARAMETER);
}

void ObjectIdentityResources(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *context = document.Context();
    auto *object = new ReusedObject(context);
    const auto id = object->GetID();
    const auto ref = document.Reference(id);
    {
        Edit edit(document, "Identity-owned object");
        edit.Created(id);
        CHECK(edit.Commit().ok);
    }
    auto retained = document.RetainObjects({id});
    CHECK(retained && retained->IsValid() && document.CreatedObjects().count(id));
    {
        Edit edit(document, "Identity graveyard object");
        edit.Bury(id);
        CHECK(edit.Commit().ok);
    }
    CHECK(document.IsDiscarded(id));
    CHECK(context->DestroyObject(id, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
    auto *replacement = new ReusedObject(context);
    CHECK(replacement == object && replacement->GetID() == id);
    CHECK(!retained->IsValid() && !document.Resolve(ref, CKCID_OBJECT, Document::ObjectScope::Existing));
    CHECK(document.IsLive(id) && !document.IsDiscarded(id) && !document.CreatedObjects().count(id));
    document.History().Clear();
    retained.reset();
    CHECK(context->GetObject(id) == replacement && document.IsLive(id));
    document.Include(id);
    CHECK(document.CreatedObjects().count(id));
}

void ObjectIdentityReplay(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *context = document.Context();
    auto *object = new ReusedObject(context);
    const auto id = object->GetID();
    const auto ref = document.Reference(id);
    LevelActionRecord rename{document.Token(), std::vector<ObjectNameChange>{{ref, "Identity reuse", "Stale rename"}}};
    ObjectsGesture gesture;
    CHECK(gesture.Begin(document, {id}));
    object->value = 42;
    CHECK(gesture.Commit(document, "Identity state").ok);
    CHECK(context->DestroyObject(id, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
    auto *replacement = new ReusedObject(context);
    CHECK(replacement == object && replacement->GetID() == id);
    CHECK(!RenameLevelObject(document, ref, "Wrong object").ok);
    CHECK(!RestoreLevelAction(rename)->Redo(document));
    CHECK(!document.History().Undo());
    CHECK(replacement->value == 0 && std::string(replacement->GetName()) == "Identity reuse");

    // Parameters are created by the SDK factory; their constructors and
    // implementation vtables are not part of the Windows DLL interface.
    auto *parameter = context->CreateCKParameterOut("Identity parameter", CKPGUID_INT);
    CHECK(parameter);
    CHECK(parameter->GetGUID() == CKPGUID_INT);
    const auto parameterId = parameter->GetID();
    const auto parameterRef = document.Reference(parameterId);
    int value = 42;
    std::vector<unsigned char> bytes(sizeof(value));
    std::memcpy(bytes.data(), &value, sizeof(value));
    CHECK(SetParameterBytes(document, 0, parameterId, bytes).ok);
    CHECK(context->DestroyObject(parameterId, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
    auto *newParameter = context->CreateCKParameterOut("Identity parameter", CKPGUID_INT);
    CHECK(newParameter && newParameter->GetID() == parameterId);
    CHECK(document.Reference(parameterId) != parameterRef && !document.Resolve(parameterRef));
    CHECK(newParameter->GetGUID() == CKPGUID_INT);
    value = 73;
    CHECK(newParameter->SetValue(&value) == CK_OK);
    CHECK(!document.History().Undo());
    int actual = 0;
    CHECK(newParameter->GetValue(&actual, FALSE) == CK_OK && actual == value);
}

void ScriptIdentitySessions(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *context = document.Context();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, "Identical script"));
    CHECK(script);
    script->UseGraph(); script->SetType(CKBEHAVIORTYPE_SCRIPT);
    const auto id = script->GetID();
    document.AdoptObjects({id});
    auto *session = document.Session(id);
    CHECK(session && session->IsCurrent());
    CHECK(EditScriptData(document, id, "Old script display", [](InterfaceData &data) { data.header.vSize = 1739; }));
    const auto before = document.Reference(id);
    const auto revision = document.ScriptsRevision();
    ScriptSession old = *session;
    document.History().Clear();
    CHECK(context->DestroyObject(id, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
    auto *replacement = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, "Identical script"));
    CHECK(replacement && replacement->GetID() == id);
    replacement->UseGraph(); replacement->SetType(CKBEHAVIORTYPE_SCRIPT);
    CHECK(!old.IsCurrent() && !old.Assign(old.Data()) && !old.Reconstruct(context));
    document.RefreshScripts();
    CHECK(document.ScriptsRevision() > revision && document.Reference(id) != before);
    auto *current = document.Session(id);
    CHECK(current && current->IsCurrent() && current->Data().header.vSize != 1739);
    const auto loaded = document.LoadedObjects();
    CHECK(std::find(loaded.begin(), loaded.end(), id) == loaded.end());
}
} // namespace vdevtest

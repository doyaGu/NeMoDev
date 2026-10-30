#include "core/graph/ParameterValues.h"
#include "core/graph/ValueCommands.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "features/values/ParameterCommands.h"
#include <cstring>
#include <limits>
#include <stdexcept>

namespace vdev {
namespace {
void Check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
ParameterValueDescription Read(Document &document, CKParameter *parameter) {
    ParameterValueDescription value;
    Check(DescribeParameterValue(document, parameter, value), "describe selector value");
    return value;
}
} // namespace
void TestSelectionParameterEdits(Document &document, EditorApplication &app, EditorScope scope) {
    auto *context = document.Context();
    auto *manager = context->GetParameterManager();
    std::vector<CKParameter *> parameters;
    const auto make = [&](CKGUID type) {
        auto *parameter = context->CreateCKParameterLocal("Selector regression", type);
        Check(parameter, "create selector parameter");
        parameters.push_back(parameter);
        return parameter;
    };
    const auto saved = [&] {
        document.History().Clear();
        document.MarkSaved();
        document.History().MarkSavePoint();
    };
    const auto submit = [&](CKParameter *parameter, const char *property, const ParameterValue &value, bool accept = true) {
        EditorEdit edit = 0;
        Check(app.BeginEdit(property, scope, document.Reference(parameter->GetID()), edit) == EDITOR_OK, "begin selector edit");
        const std::vector<EditorField> fields{ParameterValueField(value)};
        const auto borrowed = BorrowEditorFields(fields);
        Check(app.UpdateEdit(edit, borrowed.values.front()) == EDITOR_OK, "update selector draft");
        const auto status = app.FinishEdit(edit, accept);
        if (status == EDITOR_QUEUED) app.EndFrame();
        return status;
    };
    const auto choices = [&](CKParameter *parameter, std::vector<EditorField> filters = {}) {
        std::vector<EditorField> arguments{{"target", EDITOR_OBJECT, document.Reference(parameter->GetID())}, {"filters", EDITOR_RECORD}};
        arguments.back().fields = std::move(filters);
        const auto borrowed = BorrowEditorFields(arguments);
        return app.Query("parameter.selection-choices", {scope, borrowed.data(), uint32_t(borrowed.size())});
    };
    auto *time = make(CKPGUID_TIME);
    float milliseconds = 61789.5f;
    time->SetValue(&milliseconds, 4);
    const auto before = Read(document, time);
    const auto beforeBytes = ParameterBytes(time);
    saved();
    Check(before.value.kind == ParameterValueKind::Time && before.value.real == milliseconds, "time lost native millisecond precision");
    EditorEdit edit = 0;
    Check(app.BeginEdit("parameter.time", scope, document.Reference(time->GetID()), edit) == EDITOR_OK, "begin time interaction");
    for (unsigned i = 1; i <= 40; ++i) {
        auto next = before.value;
        next.real += i * 1000;
        const std::vector<EditorField> fields{ParameterValueField(next)};
        const auto borrowed = BorrowEditorFields(fields);
        Check(app.UpdateEdit(edit, borrowed.values.front()) == EDITOR_OK && ParameterBytes(time) == beforeBytes &&
                  document.History().UndoCount() == 0,
              "time interaction wrote before acceptance");
    }
    Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "queue time acceptance");
    app.EndFrame();
    Check(document.History().UndoCount() == 1 && Read(document, time).value.real == double(milliseconds + 40000),
          "time interaction split undo");
    Check(document.History().Undo() && ParameterBytes(time) == beforeBytes && !document.Dirty(), "time undo changed native bytes");
    auto next = before.value;
    next.real = -789.5;
    Check(submit(time, "parameter.time", next, false) == EDITOR_CANCELLED && !document.Dirty() && document.History().CanRedo(),
          "time cancel discarded redo");
    Check(submit(time, "parameter.time", before.value) == EDITOR_QUEUED && document.History().UndoCount() == 0 &&
              document.History().CanRedo(),
          "unchanged time cleared history");
    next.real = std::numeric_limits<double>::infinity();
    Check(!SetParameterValue(document, document.Reference(time->GetID()), CKPGUID_TIME, next).ok && ParameterBytes(time) == beforeBytes,
          "nonfinite time accepted");
    auto *key = make(CKPGUID_KEY);
    int code = 0x1e;
    key->SetValue(&code, 4);
    auto keyBefore = Read(document, key);
    saved();
    Check(keyBefore.value.kind == ParameterValueKind::Key, "key is not typed");
    auto keyNext = keyBefore.value;
    keyNext.integer = 0x9c;
    Check(submit(key, "parameter.key", keyNext) == EDITOR_QUEUED && document.History().UndoCount() == 1 &&
              Read(document, key).value == keyNext && document.History().Undo() && Read(document, key).value == keyBefore.value,
          "key edit/undo failed");
    keyNext.integer = 256;
    Check(!SetParameterValue(document, document.Reference(key->GetID()), CKPGUID_KEY, keyNext).ok, "invalid scan code accepted");
    keyNext.integer = 0x30;
    Check(submit(key, "parameter.key", keyNext, false) == EDITOR_CANCELLED && document.History().CanRedo(),
          "key cancellation changed history");
    const CKGUID derivedTime(0x6e76616c, 0x00000460);
    auto derived = *manager->GetParameterTypeDescription(manager->ParameterGuidToType(CKPGUID_TIME));
    derived.Guid = derivedTime;
    derived.DerivedFrom = CKPGUID_TIME;
    derived.TypeName = "Derived selector time";
    Check(manager->RegisterParameterType(&derived) == CK_OK, "register derived time");
    auto *custom = make(derivedTime);
    Check(Read(document, custom).value.kind == ParameterValueKind::Time && Read(document, custom).type == derivedTime,
          "derived time lost its type");
    auto *message = make(CKPGUID_MESSAGE);
    int empty = -1;
    message->SetValue(&empty, 4);
    auto messageBefore = Read(document, message);
    saved();
    auto pending = messageBefore.value;
    pending.integer = -1;
    pending.text = "Deferred selector message";
    auto *messages = context->GetMessageManager();
    const auto messageCount = messages->GetMessageTypeCount();
    auto *work = context->CreateCKParameterOut("Message draft", CKPGUID_MESSAGE, TRUE);
    work->CopyValue(message, FALSE);
    Check(WriteParameterDraft(document, work, pending) && ParameterDraftValue(Read(document, work).value, pending) == pending &&
              messages->GetMessageTypeCount() == messageCount,
          "native message draft registered a type");
    Check(submit(message, "parameter.message", pending, false) == EDITOR_CANCELLED && messages->GetMessageTypeCount() == messageCount &&
              !document.Dirty(),
          "cancelled message registered a type");
    Check(submit(message, "parameter.message", pending) == EDITOR_QUEUED && messages->GetMessageTypeCount() == messageCount + 1 &&
              document.History().UndoCount() == 1,
          "accepted message did not register once");
    const auto registered = Read(document, message).value;
    Check(registered.text == pending.text && registered.integer >= 0 && document.History().Undo() &&
              Read(document, message).value == messageBefore.value && document.History().Redo() &&
              Read(document, message).value == registered && messages->GetMessageTypeCount() == messageCount + 1,
          "message undo/redo changed registration indices");
    messages->RenameMessageType(registered.integer,"Renamed selector message");
    Check(document.History().Undo() && document.History().Redo() && Read(document,message).value.integer==registered.integer &&
        std::string(messages->GetMessageTypeName(registered.integer))=="Renamed selector message",
        "parameter history changed a registered message name");
    messages->RenameMessageType(registered.integer,registered.text.c_str());
    const auto query = choices(message);
    Check(query.status == EDITOR_OK && query.fields[0].texts.size() == query.fields[2].integers.size(), "message choices lost indices");
    context->DestroyObject(work);
    auto *attributes = context->GetAttributeManager();
    const auto attribute =
        attributes->RegisterNewAttributeType("Selector regression attribute", CKPGUID_INT, CKCID_BEOBJECT, CK_ATTRIBUT_USER);
    Check(attribute >= 0, "register selector attribute");
    attributes->AddCategory("Selector category");
    attributes->SetAttributeCategory(attribute, "Selector category");
    auto *attributeParameter = make(CKPGUID_ATTRIBUTE);
    auto attributeValue = Read(document, attributeParameter).value;
    attributeValue.integer = attribute;
    saved();
    Check(submit(attributeParameter, "parameter.attribute", attributeValue) == EDITOR_QUEUED && document.History().UndoCount() == 1 &&
              document.History().Undo(),
          "attribute selection did not undo");
    const auto filtered = choices(attributeParameter, {{"category", EDITOR_TEXT_VALUE, {}, "Selector category"}});
    Check(filtered.status == EDITOR_OK && filtered.fields[0].texts.size() == 1 && filtered.fields[2].integers[0] == uint64_t(attribute),
          "attribute category projection failed");
    attributeValue.integer = INT32_MAX;
    Check(!SetParameterValue(document, document.Reference(attributeParameter->GetID()), CKPGUID_ATTRIBUTE, attributeValue).ok,
          "invalid attribute accepted");
    const auto ownerResult = CreateNamedObject(document, CKCID_3DOBJECT, "Selector script owner");
    Check(ownerResult.ok, "create selector owner");
    const auto scriptResult = CreateScript(document, ownerResult.created, "Selector script");
    Check(scriptResult.ok, "create selector script");
    auto *script = make(CKPGUID_SCRIPT);
    auto scriptValue = Read(document, script).value;
    scriptValue.object = document.Reference(scriptResult.created);
    saved();
    Check(submit(script, "parameter.script", scriptValue) == EDITOR_QUEUED && Read(document, script).value == scriptValue &&
              document.History().Undo(),
          "script selection failed");
    auto scripts = choices(script, {{"owner", EDITOR_OBJECT, document.Reference(ownerResult.created)}});
    Check(scripts.status == EDITOR_OK && scripts.fields[1].objects.size() == 1 && scripts.fields[1].objects[0] == scriptValue.object,
          "script owner filtering failed");
    saved();
    Check(submit(script, "parameter.script", scriptValue) == EDITOR_QUEUED, "prepare script clearing");
    const EditorValue omitted[] = {
        {EDITOR_TEXT("target"), EDITOR_OBJECT, document.Reference(script->GetID())},
        {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE, {}, {}, (uint64_t(CKPGUID_SCRIPT.d1) << 32) | CKPGUID_SCRIPT.d2}};
    Check(app.CommandState("parameter.script-set", {scope, omitted, 2}) == EDITOR_INVALID && Read(document, script).value == scriptValue,
          "omitting an object value cleared its reference");
    const EditorValue choiceTarget{EDITOR_TEXT("target"),EDITOR_OBJECT,document.Reference(script->GetID())};
    Check(app.Query("parameter.selection-choices",{scope,&choiceTarget,1}).status==EDITOR_OK,"omitted selector filters were refused");
    const auto defaultScriptChoices = choices(script);
    Check(defaultScriptChoices.status == EDITOR_OK && defaultScriptChoices.fields[8].object == document.Reference(ownerResult.created) &&
              defaultScriptChoices.fields[1].objects.size() == 1,
          "default script choices lost their owner");
    auto nullScript = scriptValue;
    nullScript.object = {};
    Check(submit(script, "parameter.script", nullScript) == EDITOR_QUEUED && !Read(document, script).value.object &&
              document.History().Undo() && Read(document, script).value == scriptValue,
          "script reference could not be cleared and restored");
    const auto animationResult = CreateNamedObject(document, CKCID_ANIMATION, "Selector animation");
    Check(animationResult.ok, "create selector animation");
    auto *animation = make(CKPGUID_ANIMATION);
    auto animationValue = Read(document, animation).value;
    animationValue.object = document.Reference(animationResult.created);
    saved();
    Check(submit(animation, "parameter.animation", animationValue) == EDITOR_QUEUED && document.History().Undo(),
          "animation selection failed");
    animationValue.object = document.Reference(ownerResult.created);
    Check(!SetParameterValue(document, document.Reference(animation->GetID()), CKPGUID_ANIMATION, animationValue).ok,
          "animation accepted wrong class");
    animationValue.object = document.Reference(animationResult.created);
    Check(submit(animation, "parameter.animation", animationValue) == EDITOR_QUEUED, "prepare animation clearing");
    auto nullAnimation = animationValue;
    nullAnimation.object = {};
    Check(submit(animation, "parameter.animation", nullAnimation) == EDITOR_QUEUED && !Read(document, animation).value.object &&
              document.History().Undo() && Read(document, animation).value == animationValue,
          "animation reference could not be cleared and restored");
    auto *ordinaryObject = make(CKPGUID_OBJECT);
    auto objectValue = Read(document, ordinaryObject).value;
    objectValue.object = document.Reference(ownerResult.created);
    Check(submit(ordinaryObject, "parameter.object", objectValue) == EDITOR_QUEUED, "prepare ordinary object clearing");
    objectValue.object = {};
    Check(submit(ordinaryObject, "parameter.object", objectValue) == EDITOR_QUEUED && !Read(document, ordinaryObject).value.object,
          "ordinary nullable object value regressed");
    auto *synchro = make(CKPGUID_SYNCHRO);
    auto syncBefore = Read(document, synchro);
    auto sync = syncBefore.value;
    sync.text = "Deferred selector synchro";
    sync.waiters = 5;
    sync.setWaiters = true;
    saved();
    Check(submit(synchro, "parameter.synchro", sync, false) == EDITOR_CANCELLED &&
              !context->GetObjectByNameAndClass(sync.text.c_str(), CKCID_SYNCHRO) && !document.Dirty(),
          "cancelled synchro created an object");
    Check(submit(synchro, "parameter.synchro", sync) == EDITOR_QUEUED && document.History().UndoCount() == 1,
          "synchro creation split history");
    const auto created = Read(document, synchro).value;
    Check(created.object && created.text.empty() && created.waiters == 5, "synchro creation lost users");
    Check(document.History().Undo() && Read(document, synchro).value == syncBefore.value && !document.IsLive(created.object.object) &&
              document.History().Redo() && Read(document, synchro).value == created,
          "synchro undo/redo lost object identity or value");
    saved();
    auto countValue = created;
    countValue.waiters = -2;
    countValue.setWaiters = true;
    work = context->CreateCKParameterOut("Synchro draft", CKPGUID_SYNCHRO, TRUE);
    work->CopyValue(synchro, FALSE);
    Check(WriteParameterDraft(document, work, countValue) && Read(document, synchro).value.waiters == 5 &&
              ParameterDraftValue(Read(document, work).value, countValue) == countValue,
          "native synchro draft modified its referenced object");
    Check(submit(synchro, "parameter.synchro", countValue) == EDITOR_QUEUED && Read(document, synchro).value.waiters == -2 &&
              document.History().UndoCount() == 1 && document.History().Undo() && Read(document, synchro).value == created,
          "signed native synchro users or undo changed");
    context->DestroyObject(work);
    auto unchangedUsers=created; unchangedUsers.setWaiters=true;
    Check(submit(synchro,"parameter.synchro",unchangedUsers)==EDITOR_QUEUED && document.History().CanRedo() &&
        document.History().UndoCount()==0 && !document.Dirty(),"unchanged synchro users discarded redo or marked the document");
    work = context->CreateCKParameterOut("Unedited synchro draft", CKPGUID_SYNCHRO, TRUE);
    work->CopyValue(synchro, FALSE);
    const auto untouched = Read(document, work).value;
    auto *nativeSynchro = CKSynchroObject::Cast(document.Resolve(created.object));
    nativeSynchro->SetRendezVousNumberOfWaiters(12);
    const auto refreshed = ParameterDraftValue(Read(document, work).value, untouched);
    Check(refreshed.waiters == 12 && !refreshed.setWaiters, "an untouched draft retained stale referenced users");
    nativeSynchro->SetRendezVousNumberOfWaiters(5);
    context->DestroyObject(work);
    const auto otherSync = CreateNamedObject(document, CKCID_SYNCHRO, "Selector other synchro");
    Check(otherSync.ok, "create other synchro");
    auto *otherNative = CKSynchroObject::Cast(context->GetObject(otherSync.created));
    otherNative->SetRendezVousNumberOfWaiters(3);
    auto referenceOnly = Read(document, synchro).value;
    referenceOnly.object = document.Reference(otherSync.created);
    referenceOnly.waiters = 3;
    saved();
    Check(submit(synchro, "parameter.synchro", referenceOnly) == EDITOR_QUEUED, "change synchro reference");
    otherNative->SetRendezVousNumberOfWaiters(12);
    nativeSynchro->SetRendezVousNumberOfWaiters(13);
    Check(document.History().Undo() && Read(document, synchro).value.object == created.object &&
              nativeSynchro->GetRendezVousNumberOfWaiters() == 13 && document.History().Redo() &&
              Read(document, synchro).value.object == referenceOnly.object && otherNative->GetRendezVousNumberOfWaiters() == 12,
          "reference history took ownership of an untouched rendezvous count");
    for (auto type : {CKPGUID_CRITICALSECTION, CKPGUID_STATE}) {
        auto *parameter = make(type);
        auto value = Read(document, parameter).value;
        value.text = type == CKPGUID_STATE ? "Deferred selector state" : "Deferred selector critical section";
        saved();
        Check(submit(parameter, "parameter.synchro", value) == EDITOR_QUEUED && Read(document, parameter).value.object &&
                  document.History().UndoCount() == 1 && document.History().Undo() && !Read(document, parameter).value.object,
              "state/critical-section selector failed");
    }
    const CKGUID recordType(0x6e76616c, 0x00000461);
    XArray<CKGUID> members;
    for (auto type : {CKPGUID_TIME, CKPGUID_KEY, CKPGUID_MESSAGE, CKPGUID_SYNCHRO})
        members.PushBack(type);
    Check(manager->RegisterNewStructure(recordType, "Selector record", "Time,Key,Message,Synchro", members) == CK_OK,
          "register selector record");
    auto *record = make(recordType);
    const auto recordBefore = Read(document, record);
    auto recordNext = recordBefore.value;
    recordNext.members[0].real = 1234.5;
    recordNext.members[1].integer = 0x30;
    recordNext.members[2].integer = -1;
    recordNext.members[2].text = "Nested selector message";
    recordNext.members[3].text = "Nested selector synchro";
    recordNext.members[3].waiters = 7;
    recordNext.members[3].setWaiters = true;
    saved();
    const auto identities = recordBefore.members;
    Check(submit(record, "parameter.structure", recordNext) == EDITOR_QUEUED && document.History().UndoCount() == 1,
          "nested selectors split history");
    Check(document.History().Undo() && Read(document, record).value == recordBefore.value &&
              Read(document, record).members[0].parameter == identities[0].parameter && document.History().Redo(),
          "nested selector history replaced members or lost values");
    document.History().Clear();
    for (auto *parameter : parameters)
        context->DestroyObject(parameter);
    manager->UnRegisterParameterType(recordType);
    manager->UnRegisterParameterType(derivedTime);
    attributes->UnRegisterAttribute(attribute);
    attributes->RemoveCategory("Selector category");
}
} // namespace vdev

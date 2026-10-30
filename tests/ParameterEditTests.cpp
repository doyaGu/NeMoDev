#include "application/EditorApplication.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/graph/ParameterValues.h"
#include "core/graph/ValueCommands.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "features/values/ParameterCommands.h"
#include "editor/ImGuiDrawing.h"
#include <cmath>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
struct Painter { std::vector<double> components; uint64_t integer = 0; uint32_t action = 0; bool update = false; unsigned calls = 0; };
uint32_t Paint(void *data, const EditorHost *, const EditorValueInput *input) {
    auto &painter = *static_cast<Painter *>(data);
    ++painter.calls;
    if (painter.update) {
        auto value = input->value;
        value.integer = painter.integer;
        if (!painter.components.empty()) value.reals = {painter.components.data(), uint32_t(painter.components.size())};
        Check(input->update.write(input->update.data, &value, 1) == EDITOR_OK, "typed renderer update rejected");
    }
    return painter.action;
}
CK_PARAMETERSTRINGFUNCTION nestedString = nullptr;
int nestedFailure = 0;
int NestedString(CKParameter *parameter, char *text, CKBOOL fromString) {
    const auto result = nestedString(parameter, text, fromString);
    if (fromString && nestedFailure) {
        const auto failure = nestedFailure; nestedFailure = 0;
        if (failure == 2) throw std::runtime_error("Nested setter failed after writing");
        return CKERR_INVALIDPARAMETER;
    }
    return result;
}
EditorValue Integer(int64_t value) { return {EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(value)}; }
EditorValue Real(double value) { return {EDITOR_TEXT("value"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, value}; }
EditorValue Object(ObjectRef object) { return {EDITOR_TEXT("value"), EDITOR_OBJECT, object}; }
}
void TestParameterEdits(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document document(runtime);
    std::string error;
    Check(document.New(&error), error.c_str());
    bool native = true;
    EditorApplication app([&] { return &document; }, [&] { return native; });
    ParameterCommands commands([&] { return &document; });
    Painter painter;
    const char *ids[] = {"value.boolean", "value.integer", "value.real", "value.enumeration", "value.object", "value.angle", "value.vector2", "value.vector3", "value.quaternion", "value.euler", "value.matrix", "value.color", "value.box", "value.rect", "value.string", "value.structure", "value.object-array", "value.flags"};
    const EditorValueType types[] = {EDITOR_INTEGER_VALUE, EDITOR_INTEGER_VALUE, EDITOR_REAL_VALUE, EDITOR_INTEGER_VALUE, EDITOR_OBJECT, EDITOR_REAL_VALUE, EDITOR_REAL_LIST, EDITOR_REAL_LIST, EDITOR_REAL_LIST, EDITOR_REAL_LIST, EDITOR_REAL_LIST, EDITOR_REAL_LIST, EDITOR_REAL_LIST, EDITOR_REAL_LIST, EDITOR_TEXT_VALUE, EDITOR_RECORD, EDITOR_OBJECT_LIST, EDITOR_INTEGER_VALUE};
    EditorValueEditorDesc editors[std::size(ids)]{};
    for (unsigned i = 0; i < std::size(ids); ++i)
        editors[i] = {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EditorLiteral(ids[i]), EDITOR_TEXT("test.values"),
            EDITOR_TEXT("Test value"), Paint, &painter, types[i]};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.parameter-editors");
    module.editors = editors; module.editorCount = uint32_t(std::size(editors));
    {
        EditorApplication invalid([&] { return &document; });
        ParameterCommands invalidCommands([&] { return &document; });
        std::vector<EditorValueEditorDesc> wrong(std::begin(editors), std::end(editors));
        wrong[1].type = EDITOR_REAL_VALUE;
        auto mismatched = module; mismatched.editors = wrong.data();
        Check(invalidCommands.Register(invalid, error) && invalid.Register(mismatched, error), "prepare mismatched value editor");
        Check(!invalid.Activate(EDITOR_IMGUI_VERSION, error), "integer property accepted a real editor");
    }
    Check(commands.Register(app, error) && app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    const auto owner = app.OpenScope();
    const auto scope = app.Scope(owner);
    auto *context = document.Context();
    auto *integer = context->CreateCKParameterLocal("Typed integer", CKPGUID_INT);
    auto *real = context->CreateCKParameterLocal("Typed real", CKPGUID_FLOAT);
    auto *boolean = context->CreateCKParameterLocal("Typed boolean", CKPGUID_BOOL);
    auto *reference = context->CreateCKParameterLocal("Typed object", CKPGUID_3DENTITY);
    auto *percentage = context->CreateCKParameterLocal("Typed percentage", CKPGUID_PERCENTAGE);
    Check(integer && real && boolean && reference && percentage, "create primitive parameters");
    const CKGUID enumerationType(0x6e76616c, 0x00000401);
    Check(context->GetParameterManager()->RegisterNewEnum(enumerationType, "Typed edit enum", "Negative=-7,Middle=2,Last=90") == CK_OK,
          "register sparse enum");
    auto *enumeration = context->CreateCKParameterLocal("Typed enum", enumerationType);
    Check(enumeration != nullptr, "create enum parameter");
    const auto frame = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(frame.ok, "create object choice");
    document.History().Clear();
    document.MarkSaved(); document.History().MarkSavePoint();
    const auto integerRef = document.Reference(integer->GetID());
    const auto read = [&](CKParameter *parameter) { ParameterValueDescription value; Check(DescribeParameterValue(document, parameter, value), "describe typed value"); return value; };
    const auto history = [&] { return document.History().UndoCount(); };
    EditorEdit edit = 0;
    for (int i = 1; i <= 40; ++i) {
        painter.integer = uint64_t(i);
        painter.update = true;
        painter.action = i == 40 ? EDITOR_INPUT_ACCEPT : EDITOR_INPUT_ACTIVE;
        app.EnterDraw();
        app.DrawProperty("parameter.integer", scope, integerRef, edit, EditorLiteral("value"));
        app.LeaveDraw();
        Check(read(integer).value.integer == 0 && history() == 0, "drag increment wrote a value before commit");
        app.EndFrame();
    }
    Check(!edit && read(integer).value.integer == 40 && history() == 1 && painter.calls == 40,
          "one drag did not produce exactly one undo step");
    Check(document.History().Undo() && read(integer).value.integer == 0, "undo did not restore entire drag");
    const auto redo = document.History().CanRedo();
    painter.integer = 99; painter.action = EDITOR_INPUT_CANCEL;
    app.EnterDraw(); app.DrawProperty("parameter.integer", scope, integerRef, edit, EditorLiteral("value")); app.LeaveDraw(); app.EndFrame();
    Check(!edit && read(integer).value.integer == 0 && history() == 0 && document.History().CanRedo() == redo,
          "cancel changed value or redo history");
    Check(app.BeginEdit("parameter.integer", scope, integerRef, edit) == EDITOR_OK, "begin typed integer draft");
    Check(app.UpdateEdit(edit, Real(2)) == EDITOR_INVALID, "integer draft accepted a real");
    Check(app.UpdateEdit(edit, Integer(0)) == EDITOR_OK && app.FinishEdit(edit, true) == EDITOR_QUEUED, "submit unchanged value");
    app.EndFrame();
    Check(history() == 0 && document.History().CanRedo() == redo, "unchanged edit discarded redo");
    edit = 0;
    auto apply = [&](CKParameter *parameter, const char *property, const EditorValue &value) {
        Check(app.BeginEdit(property, scope, document.Reference(parameter->GetID()), edit) == EDITOR_OK, "begin primitive draft");
        Check(app.UpdateEdit(edit, value) == EDITOR_OK, "update primitive draft");
        Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "commit primitive draft");
        edit = 0; app.EndFrame();
    };
    apply(boolean, "parameter.boolean", Integer(1));
    apply(real, "parameter.real", Real(2.5));
    apply(enumeration, "parameter.enumeration", Integer(-7));
    apply(reference, "parameter.object", Object(document.Reference(frame.created)));
    Check(history() == 4 && read(boolean).value.integer == 1 && read(real).value.real == 2.5 &&
          read(enumeration).value.integer == -7 && read(reference).value.object == document.Reference(frame.created),
          "primitive edits were not applied once each");
    const auto count = history();
    Check(app.BeginEdit("parameter.enumeration", scope, document.Reference(enumeration->GetID()), edit) == EDITOR_OK &&
          app.UpdateEdit(edit, Integer(3)) == EDITOR_OK && app.FinishEdit(edit, true) == EDITOR_REFUSED,
          "sparse enum accepted an unknown value");
    EditorField draft; EditorValueEditorDesc selected{};
    Check(app.EditState(edit, draft, selected) && draft.integer == 3 && history() == count, "invalid commit lost the draft or history");
    app.FinishEdit(edit, false); edit = 0;
    auto *temporary = context->CreateObject(CKCID_3DENTITY, "Temporary reference");
    Check(temporary != nullptr, "create transient object reference");
    const auto temporaryId = temporary->GetID();
    const auto oldReference = document.Reference(temporaryId);
    Check(app.BeginEdit("parameter.object", scope, document.Reference(reference->GetID()), edit) == EDITOR_OK &&
          app.UpdateEdit(edit, Object(oldReference)) == EDITOR_OK, "capture reference draft");
    Check(context->DestroyObject(temporaryId, CK_DESTROY_FREEID | CK_DESTROY_NONOTIFY) == CK_OK, "delete reference without notifications");
    auto *replacement = context->CreateObject(CKCID_3DENTITY, "Reused reference");
    Check(replacement && replacement->GetID() == temporaryId && document.Reference(temporaryId) != oldReference, "native ID was not reused");
    Check(app.FinishEdit(edit, true) == EDITOR_REFUSED && read(reference).value.object == document.Reference(frame.created) && history() == count,
          "old object reference was redirected to a reused native ID");
    app.FinishEdit(edit, false); edit = 0;
    context->DestroyObject(replacement);
    Check(app.BeginEdit("parameter.integer", scope, integerRef, edit) == EDITOR_OK && app.UpdateEdit(edit, Integer(77)) == EDITOR_OK,
          "capture type identity");
    Check(SetParameterType(document, integer->GetID(), CKPGUID_FLOAT).ok, "change native parameter type");
    Check(app.FinishEdit(edit, true) == EDITOR_STALE && !app.HasEdits() && history() == count + 1,
          "old typed draft changed a new parameter type");
    edit = 0;
    Check(document.History().Undo() && read(integer).value.kind == ParameterValueKind::Integer, "restore parameter type");
    Check(app.BeginEdit("parameter.real", scope, document.Reference(real->GetID()), edit) == EDITOR_OK, "begin finite value check");
    Check(app.UpdateEdit(edit, Real(std::numeric_limits<double>::infinity())) == EDITOR_INVALID &&
          app.UpdateEdit(edit, Real(std::numeric_limits<double>::quiet_NaN())) == EDITOR_INVALID, "non-finite draft accepted");
    app.FinishEdit(edit, false); edit = 0;
    ParameterValue ignoredFields;
    auto tagged = Integer(-7);
    tagged.real = std::numeric_limits<double>::quiet_NaN(); tagged.object = document.Reference(frame.created);
    Check(ParameterValueFromEditor(ParameterValueKind::Integer, tagged, ignoredFields) &&
          ignoredFields.integer == -7 && !ignoredFields.object && ignoredFields.real == 0,
          "integer conversion read fields belonging to another value type");
    ParameterValue parsed;
    const auto percent = read(percentage);
    Check(ParseParameterValue(percent, "25%", parsed) && parsed.real == .25, "percentage units were changed");
    Check(WriteParameterDraft(document, percentage, parsed) == false, "draft writer accepted a composition parameter");
    const auto intDesc = read(integer);
    for (const char *text : {"2147483648", "-2147483649", "1x", "", "nan"})
        Check(!ParseParameterValue(intDesc, text, parsed), "invalid integer parsed");
    for (const char *text : {"1e400", "nan", "inf", "1x", ""})
        Check(!ParseParameterValue(read(real), text, parsed), "invalid real parsed");
    struct Shape { CKGUID type; const char *property; unsigned count; };
    const Shape shapes[] = {
        {CKPGUID_2DVECTOR, "parameter.vector2", 2}, {CKPGUID_VECTOR, "parameter.vector3", 3},
        {CKPGUID_QUATERNION, "parameter.quaternion", 4}, {CKPGUID_EULERANGLES, "parameter.euler", 3},
        {CKPGUID_MATRIX, "parameter.matrix", 16}, {CKPGUID_COLOR, "parameter.color", 4},
        {CKPGUID_BOX, "parameter.box", 6}, {CKPGUID_RECT, "parameter.rect", 4}};
    for (const auto &shape : shapes) {
        auto *parameter = context->CreateCKParameterLocal("Compound value", shape.type);
        Check(parameter != nullptr, "create compound parameter");
        float original[16]{};
        for (unsigned i = 0; i < shape.count; ++i) original[i] = float(i + 1) * .25f;
        Check(parameter->SetValue(original, int(shape.count * sizeof(float))) == CK_OK, "initialize compound parameter");
        const auto description = read(parameter);
        Check(description.value.components.size() == shape.count && description.rows * description.columns == shape.count,
              "compound shape or component order was lost");
        const auto target = document.Reference(parameter->GetID());
        document.History().Clear(); document.MarkSaved(); document.History().MarkSavePoint();
        for (int frameIndex = 0; frameIndex < 40; ++frameIndex) {
            painter.components = description.value.components;
            for (auto &component : painter.components) component += frameIndex + 1;
            painter.action = frameIndex == 39 ? EDITOR_INPUT_ACCEPT : EDITOR_INPUT_ACTIVE;
            app.EnterDraw(); app.DrawProperty(shape.property, scope, target, edit, EditorLiteral("compound")); app.LeaveDraw();
            Check(read(parameter).value == description.value && history() == 0, "compound drag wrote native data before acceptance");
            app.EndFrame();
        }
        Check(!edit && history() == 1 && read(parameter).value.components == painter.components,
              "compound drag did not commit exactly once");
        Check(document.History().Undo() && read(parameter).value == description.value && document.History().CanRedo(), "compound undo lost component order");
        const auto redoCount = document.History().CanRedo();
        painter.action = EDITOR_INPUT_CANCEL;
        app.EnterDraw(); app.DrawProperty(shape.property, scope, target, edit, EditorLiteral("compound")); app.LeaveDraw(); app.EndFrame();
        Check(!edit && read(parameter).value == description.value && history() == 0 && document.History().CanRedo() == redoCount,
              "compound cancellation changed the value, dirty state or Redo");
        Check(!document.Dirty(), "compound cancel dirtied document");
        Check(app.BeginEdit(shape.property, scope, target, edit) == EDITOR_OK, "begin invalid compound draft");
        const std::vector<EditorField> fields{ParameterValueField(description.value)};
        const auto borrowed = BorrowEditorFields(fields);
        auto invalid = borrowed.values.front();
        invalid.reals.count = shape.count - 1;
        Check(app.UpdateEdit(edit, invalid) == EDITOR_OK && app.FinishEdit(edit, true) != EDITOR_QUEUED, "wrong component count was committed");
        invalid = borrowed.values.front();
        std::vector<double> nonfinite = description.value.components;
        nonfinite[0] = std::numeric_limits<double>::quiet_NaN(); invalid.reals = {nonfinite.data(), uint32_t(nonfinite.size())};
        Check(app.UpdateEdit(edit, invalid) == EDITOR_INVALID, "non-finite component entered a draft");
        nonfinite[0] = std::numeric_limits<double>::max();
        Check(app.UpdateEdit(edit, invalid) == EDITOR_OK && app.FinishEdit(edit, true) != EDITOR_QUEUED, "out-of-range component was committed");
        app.FinishEdit(edit, false); edit = 0;
        auto *work = context->CreateCKParameterOut("Compound working copy", shape.type, TRUE);
        Check(work && work->CopyValue(parameter, FALSE) == CK_OK, "copy compound parameter");
        auto changed = description.value; changed.components[0] += 7;
        Check(WriteParameterDraft(document, work, changed) && read(parameter).value == description.value && history() == 0,
              "working copy edit changed the document");
        Check(WriteParameterDraft(document, work, description.value) && read(work).value == description.value,
              "working copy cancellation did not restore all components");
        context->DestroyObject(work);
        painter.components.clear(); document.History().Clear(); context->DestroyObject(parameter);
    }
    // Containers own the complete draft, including nested strings and object
    // arrays. Neither a child change nor cancellation creates another history step.
    {
        auto *manager = context->GetParameterManager();
        const CKGUID textType(0x6e76616c, 0x00000420), innerType(0x6e76616c, 0x00000421), outerType(0x6e76616c, 0x00000422);
        auto text = *manager->GetParameterTypeDescription(manager->ParameterGuidToType(CKPGUID_STRING));
        nestedString = text.StringFunction; text.Guid = textType; text.DerivedFrom = CKPGUID_STRING;
        text.TypeName = "Nested text regression"; text.StringFunction = NestedString;
        Check(manager->RegisterParameterType(&text) == CK_OK, "register nested text type");
        XArray<CKGUID> inner; for (auto type : {CKPGUID_INT, CKPGUID_COLOR, CKPGUID_OBJECTARRAY, textType}) inner.PushBack(type);
        Check(manager->RegisterNewStructure(innerType, "Nested inner", "Count,Color,Objects,Text", inner) == CK_OK, "register inner record");
        XArray<CKGUID> outer; outer.PushBack(innerType); outer.PushBack(CKPGUID_VECTOR);
        Check(manager->RegisterNewStructure(outerType, "Nested outer", "Inner,Position", outer) == CK_OK, "register outer record");
        auto *parameter = context->CreateCKParameterLocal("Nested value", outerType);
        Check(parameter != nullptr, "create nested value");
        const auto original = read(parameter);
        const auto target = document.Reference(parameter->GetID());
        const auto identities = [&](const ParameterValueDescription &description) {
            std::vector<ObjectRef> refs;
            const auto visit = [&](const auto &self, const ParameterValueDescription &d) -> void {
                refs.push_back(d.parameter); for (const auto &member : d.members) self(self, member);
            };
            visit(visit, description); return refs;
        };
        const auto initialRefs = identities(original);
        auto changed = original.value;
        changed.members[0].members[0].integer = 13;
        changed.members[0].members[1].components = {.25, .5, .75, 1};
        changed.members[0].members[2].objects = {document.Reference(frame.created)};
        changed.members[0].members[3].text = "Nested string";
        changed.members[1].components = {4, 5, 6};
        document.History().Clear(); document.MarkSaved(); document.History().MarkSavePoint();
        Check(app.BeginEdit("parameter.structure", scope, target, edit) == EDITOR_OK, "begin nested draft");
        for (int i = 0; i < 40; ++i) {
            changed.members[0].members[0].integer = 13 + i;
            const std::vector<EditorField> owned{ParameterValueField(changed)}; const auto values = BorrowEditorFields(owned);
            Check(app.UpdateEdit(edit, values.values.front()) == EDITOR_OK && read(parameter).value == original.value && history() == 0,
                  "nested update modified a child before acceptance");
        }
        Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "submit nested value"); edit = 0; app.EndFrame();
        Check(history() == 1 && read(parameter).value == changed && identities(read(parameter)) == initialRefs,
              "nested commit did not preserve member identities or one undo step");
        Check(document.History().Undo() && read(parameter).value == original.value && identities(read(parameter)) == initialRefs,
              "nested undo did not restore the whole value");
        Check(document.History().Redo() && read(parameter).value == changed && identities(read(parameter)) == initialRefs,
              "nested redo replaced member parameters");
        Check(document.History().Undo(), "prepare nested cancellation");
        const std::vector<EditorField> owned{ParameterValueField(changed)}; const auto values = BorrowEditorFields(owned);
        Check(app.BeginEdit("parameter.structure", scope, target, edit) == EDITOR_OK && app.UpdateEdit(edit, values.values.front()) == EDITOR_OK &&
              app.FinishEdit(edit, false) == EDITOR_CANCELLED, "cancel nested draft"); edit = 0;
        Check(read(parameter).value == original.value && history() == 0 && document.History().CanRedo() && !document.Dirty(),
              "nested cancellation changed a value, Redo or dirty state");
        auto malformed = owned;
        malformed.front().fields.front().fields.pop_back();
        const auto bad = BorrowEditorFields(malformed);
        Check(app.BeginEdit("parameter.structure", scope, target, edit) == EDITOR_OK && app.UpdateEdit(edit, bad.values.front()) == EDITOR_OK &&
              app.FinishEdit(edit, true) != EDITOR_QUEUED, "incomplete nested record was committed");
        app.FinishEdit(edit, false); edit = 0;
        for (int failure : {1, 2}) {
            nestedFailure = failure;
            Check(!SetParameterValue(document, target, outerType, changed).ok && read(parameter).value == original.value &&
                  history() == 0 && document.History().CanRedo() && !document.Dirty(), "nested setter failure did not roll back earlier children");
        }
        auto *work = context->CreateCKParameterOut("Nested working copy", outerType, TRUE);
        Check(work && work->CopyValue(parameter, FALSE) == CK_OK, "copy nested working value");
        for (int failure : {1, 2}) {
            nestedFailure = failure;
            Check(!WriteParameterDraft(document, work, changed) && read(work).value == original.value && read(parameter).value == original.value &&
                  document.History().CanRedo(), "failed working-copy write damaged its value or the document history");
        }
        Check(WriteParameterDraft(document, work, changed) && WriteParameterDraft(document, work, original.value) &&
              read(work).value == original.value, "nested working-copy cancellation failed");
        context->DestroyObject(work);
        // Nested object-array references keep their original lifetimes too.
        auto *oldObject = context->CreateObject(CKCID_3DENTITY, "Nested array reference");
        const auto oldObjectRef = document.Reference(oldObject->GetID());
        auto withObject = changed; withObject.members[0].members[2].objects = {oldObjectRef};
        const std::vector<EditorField> refFields{ParameterValueField(withObject)}; const auto refValues = BorrowEditorFields(refFields);
        Check(app.BeginEdit("parameter.structure", scope, target, edit) == EDITOR_OK && app.UpdateEdit(edit, refValues.values.front()) == EDITOR_OK,
              "prepare nested object-array reference");
        Check(context->DestroyObject(oldObjectRef.object, CK_DESTROY_FREEID | CK_DESTROY_NONOTIFY) == CK_OK, "delete nested array reference");
        auto *newObject = context->CreateObject(CKCID_3DENTITY, "Replacement array reference");
        Check(newObject && newObject->GetID() == oldObjectRef.object && document.Reference(newObject->GetID()) != oldObjectRef,
              "reuse nested array object ID");
        auto reordered = withObject;
        reordered.members[0].members[2].objects = {document.Reference(frame.created), document.Reference(newObject->GetID())};
        Check(RememberParameterReferences(std::move(reordered), withObject).members[0].members[2].objects.back() == oldObjectRef,
              "reordered array forgot an object's original lifetime");
        Check(app.FinishEdit(edit, true) != EDITOR_QUEUED && read(parameter).value == original.value && document.History().CanRedo(),
              "nested array draft rebound a reused object ID");
        app.FinishEdit(edit, false); edit = 0; context->DestroyObject(newObject);
        // A stale schema must not write a newly allocated member with the same ID.
        Check(app.BeginEdit("parameter.structure", scope, target, edit) == EDITOR_OK && app.UpdateEdit(edit, values.values.front()) == EDITOR_OK,
              "prepare stale nested schema");
        const auto old = original.members[0].members[0].parameter;
        Check(context->DestroyObject(old.object, CK_DESTROY_FREEID | CK_DESTROY_NONOTIFY) == CK_OK, "destroy nested member");
        auto *replacement = context->CreateCKParameterOut("Replacement member", CKPGUID_INT);
        Check(replacement && replacement->GetID() == old.object && document.Reference(old.object) != old, "reuse nested member ID");
        const int sentinel = 77; replacement->SetValue(&sentinel, sizeof(sentinel));
        Check(app.FinishEdit(edit, true) != EDITOR_QUEUED && read(replacement).value.integer == 77 &&
              read(parameter).value.members[1] == original.value.members[1], "stale nested schema wrote a replacement member");
        app.FinishEdit(edit, false); edit = 0;
        document.History().Clear(); context->DestroyObject(parameter);
        manager->UnRegisterParameterType(outerType); manager->UnRegisterParameterType(innerType); manager->UnRegisterParameterType(textType);
        nestedString = nullptr;
    }
    // A deep structure's schema includes member metadata as well as values.
    // Both must fit the common query/draft representation.
    {
        auto *manager = context->GetParameterManager();
        std::vector<CKGUID> types;
        auto memberType = CKPGUID_INT;
        for (unsigned i = 0; i < 30; ++i) {
            const CKGUID type(0x6e76616c, 0x00000500 + i);
            XArray<CKGUID> members; members.PushBack(memberType);
            Check(manager->RegisterNewStructure(type, "Deep value", "Member", members) == CK_OK, "register deep structure");
            types.push_back(type); memberType = type;
        }
        auto *parameter = context->CreateCKParameterLocal("Deep value", memberType);
        Check(parameter != nullptr, "create deep structure");
        const auto original = read(parameter);
        auto changed = original.value;
        auto *leaf = &changed;
        while (leaf->kind == ParameterValueKind::Structure) leaf = &leaf->members.front();
        leaf->integer = 42;
        document.History().Clear(); document.MarkSaved(); document.History().MarkSavePoint();
        const std::vector<EditorField> fields{ParameterValueField(changed)}; const auto borrowed = BorrowEditorFields(fields);
        Check(app.BeginEdit("parameter.structure", scope, document.Reference(parameter->GetID()), edit) == EDITOR_OK &&
              app.UpdateEdit(edit, borrowed.values.front()) == EDITOR_OK && app.FinishEdit(edit, true) == EDITOR_QUEUED,
              "deep structure metadata did not fit the editing path");
        edit = 0; app.EndFrame();
        Check(history() == 1 && read(parameter).value == changed && document.History().Undo() && read(parameter).value == original.value,
              "deep structure did not undo as one value");
        document.History().Clear(); context->DestroyObject(parameter);
        for (auto i = types.rbegin(); i != types.rend(); ++i) manager->UnRegisterParameterType(*i);
    }
    // Native plugin values retain their copier and saved state. They enter
    // the same command path and outer transaction as portable containers.
    {
        const CKGUID type(0x6e76616c, 0x00000423);
        auto *manager = context->GetParameterManager();
        XArray<CKGUID> members; members.PushBack(CKPGUID_INT); members.PushBack(CKPGUID_2DCURVE);
        Check(manager->RegisterNewStructure(type, "Native value container", "Count,Curve", members) == CK_OK, "register native value container");
        auto *target = context->CreateCKParameterLocal("Native target", type);
        auto *source = context->CreateCKParameterOut("Native source", type, TRUE);
        std::vector<ParameterMember> before, copied;
        Check(target && source && ParameterMembers(document, target, before) && ParameterMembers(document, source, copied), "describe native members");
        document.History().Clear();
        Check(CopyParameterValue(document, document.Reference(target->GetID()), document.Reference(source->GetID())).Unchanged() && history() == 0,
              "unchanged native container added an undo step");
        auto *number = CKParameter::Cast(document.Resolve(copied[0].parameter, CKCID_PARAMETER));
        const int changed = 7; number->SetValue(&changed, sizeof(changed));
        auto curveOf = [&](ObjectRef member) {
            CK2dCurve *curve = nullptr;
            CKParameter::Cast(document.Resolve(member, CKCID_PARAMETER))->GetValue(&curve, FALSE);
            return curve;
        };
        auto *curve = curveOf(copied[1].parameter);
        if (!curve->GetControlPointCount()) curve->AddControlPoint(Vx2DVector(.25f, .75f));
        else curve->GetControlPoint(0)->SetPosition(Vx2DVector(.25f, .75f));
        const auto originalCount = curveOf(before[1].parameter)->GetControlPointCount();
        document.History().Clear(); document.MarkSaved(); document.History().MarkSavePoint();
        {
            Edit transaction(document, "Native container edit");
            const EditorValue args[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, document.Reference(target->GetID())},
                {EDITOR_TEXT("source"), EDITOR_OBJECT, document.Reference(source->GetID())}};
            Check(app.ApplyEdit("parameter.copy-value", {scope, args, 2}).status == EDITOR_OK && transaction.Commit().ok,
                  "native container copy did not join the enclosing edit");
        }
        Check(history() == 1 && read(CKParameter::Cast(document.Resolve(before[0].parameter, CKCID_PARAMETER))).value.integer == 7,
              "native container did not commit exactly once");
        std::vector<ParameterMember> after;
        Check(ParameterMembers(document, target, after) && after[0].parameter == before[0].parameter && after[1].parameter == before[1].parameter,
              "native copy replaced members");
        Check(document.History().Undo() && read(CKParameter::Cast(document.Resolve(before[0].parameter, CKCID_PARAMETER))).value.integer == 0 &&
              curveOf(before[1].parameter)->GetControlPointCount() == originalCount, "native container undo failed");
        Check(document.History().Redo() && curveOf(before[1].parameter)->GetControlPoint(0)->GetPosition().x == .25f,
              "native container redo failed");
        document.History().Clear(); context->DestroyObject(source); context->DestroyObject(target); manager->UnRegisterParameterType(type);
    }
    {
        auto *manager = context->GetParameterManager();
        const CKGUID flagsType(0x6e76616c, 0x00000440), recordType(0x6e76616c, 0x00000441);
        Check(manager->RegisterNewFlags(flagsType, "Typed flags", "First=1,Second=2,High=-2147483648,Both=3,Zero=0") == CK_OK,
              "register flags with high, combined and zero masks");
        auto *flags = context->CreateCKParameterLocal("Flags value", flagsType);
        Check(flags != nullptr, "create flags parameter");
        int32_t initial = 0x40000000;
        Check(flags->SetValue(&initial, sizeof(initial)) == CK_OK, "initialize unlisted flag bit");
        const auto original = read(flags);
        Check(original.value.kind == ParameterValueKind::Flags && original.value.integer == initial &&
              original.choices.size() == 5 && original.choices[2].first == INT32_MIN && original.choices[3].first == 3 &&
              original.choices[4].first == 0 && original.rows == 16 && original.columns == 2,
              "flag description lost masks or the highest bit's native layout");
        const auto target = document.Reference(flags->GetID());
        const EditorValue queryTarget{EDITOR_TEXT("target"), EDITOR_OBJECT, target};
        const auto projection = app.Query("parameter.flags-value", {scope, &queryTarget, 1});
        Check(projection.status == EDITOR_OK && projection.fields[3].texts.size() == 5 &&
              projection.fields[4].integers[2] == uint64_t(int64_t(INT32_MIN)), "flag projection lost signed mask bits");
        document.History().Clear(); document.MarkSaved(); document.History().MarkSavePoint();
        Check(app.BeginEdit("parameter.flags", scope, target, edit) == EDITOR_OK, "begin flags interaction");
        for (int i = 0; i < 40; ++i) {
            const int32_t mask = INT32_MIN | initial | (i % 2 ? 3 : 1);
            Check(app.UpdateEdit(edit, Integer(mask)) == EDITOR_OK && read(flags).value == original.value && history() == 0,
                  "flag update wrote document before acceptance");
        }
        Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "accept flags interaction"); edit = 0; app.EndFrame();
        Check(history() == 1 && read(flags).value.integer == (INT32_MIN | initial | 3), "flags did not commit once with all bits preserved");
        Check(document.History().Undo() && read(flags).value == original.value && !document.Dirty(), "flags undo lost unlisted bits or save point");
        Check(app.BeginEdit("parameter.flags", scope, target, edit) == EDITOR_OK &&
              app.UpdateEdit(edit, Integer(-1)) == EDITOR_OK && app.FinishEdit(edit, false) == EDITOR_CANCELLED,
              "cancel flags interaction"); edit = 0;
        Check(history() == 0 && document.History().CanRedo() && !document.Dirty() && read(flags).value == original.value,
              "flag cancellation changed value, Dirty or Redo");
        apply(flags, "parameter.flags", Integer(initial));
        Check(history() == 0 && document.History().CanRedo(), "unchanged flags discarded Redo");
        Check(app.BeginEdit("parameter.flags", scope, target, edit) == EDITOR_OK, "begin out-of-range mask");
        Check(app.UpdateEdit(edit, Integer(int64_t(INT32_MAX) + 1)) == EDITOR_OK && app.FinishEdit(edit, true) == EDITOR_INVALID,
              "flags silently truncated a mask outside native storage");
        app.FinishEdit(edit, false); edit = 0;
        Check(ParseParameterValue(original, "-2147483648", parsed) && parsed.integer == INT32_MIN &&
              ValidateParameterValue(document, original, parsed), "high flag bit was treated as an enum choice");
        // Corrupt native descriptions must fail before calling their formatter.
        auto *nativeFlags = manager->GetFlagsDescByType(flags->GetType());
        Check(nativeFlags != nullptr, "locate flags metadata");
        {
            const auto saved = *nativeFlags;
            struct Restore { CKFlagsStruct *target; CKFlagsStruct value; ~Restore() { *target = value; } } restore{nativeFlags, saved};
            auto unchanged = original;
            nativeFlags->NbData = -1;
            Check(!DescribeParameterValue(document, flags, unchanged) && unchanged.value == original.value, "negative flag count accepted");
            *nativeFlags = saved; nativeFlags->Vals = nullptr;
            Check(!DescribeParameterValue(document, flags, unchanged), "missing flag masks accepted");
            *nativeFlags = saved; nativeFlags->Desc = nullptr;
            Check(!DescribeParameterValue(document, flags, unchanged), "missing flag labels accepted");
            *nativeFlags = saved; nativeFlags->NbData = 65537;
            Check(!DescribeParameterValue(document, flags, unchanged), "unbounded flag count accepted");
        }
        XArray<CKGUID> types; types.PushBack(flagsType); types.PushBack(CKPGUID_INT);
        Check(manager->RegisterNewStructure(recordType, "Flags record", "Flags,Count", types) == CK_OK, "register nested flags");
        auto *record = context->CreateCKParameterLocal("Flags record", recordType);
        Check(record != nullptr, "create nested flags");
        const auto before = read(record);
        Check(before.value.members[0].kind == ParameterValueKind::Flags, "nested flags left typed editing");
        auto next = before.value; next.members[0].integer = INT32_MIN | 3; next.members[1].integer = 12;
        apply(record, "parameter.structure", BorrowEditorFields(std::vector<EditorField>{ParameterValueField(next)}).values.front());
        Check(history() == 1 && read(record).value == next && document.History().Undo() && read(record).value == before.value,
              "nested flags were not part of the parent undo record");
        Check(document.History().Redo() && read(record).members[0].parameter == before.members[0].parameter,
              "nested flags redo replaced its member instance");
        auto *work = context->CreateCKParameterOut("Flags draft", flagsType, TRUE);
        Check(work && work->CopyValue(flags, FALSE) == CK_OK, "create flag working copy");
        auto draft = read(work).value; draft.integer = -1;
        const auto count = history();
        Check(WriteParameterDraft(document, work, draft) && read(work).value == draft && read(flags).value == original.value && history() == count,
              "flag working copy modified the source or history");
        document.History().Clear(); context->DestroyObject(work); context->DestroyObject(record); context->DestroyObject(flags);
        manager->UnRegisterParameterType(recordType); manager->UnRegisterParameterType(flagsType);
    }
    const CKGUID derivedVectorType(0x6e76616c, 0x00000411);
    auto *manager = context->GetParameterManager();
    auto derived = *manager->GetParameterTypeDescription(manager->ParameterGuidToType(CKPGUID_VECTOR));
    derived.Guid = derivedVectorType; derived.DerivedFrom = CKPGUID_VECTOR; derived.TypeName = "Derived compound vector";
    Check(manager->RegisterParameterType(&derived) == CK_OK, "register inherited vector type");
    auto *custom = context->CreateCKParameterLocal("Inherited vector", derivedVectorType);
    Check(custom && ParameterUIOf(manager, custom->GetType()).ui == ParameterUI::Vector, "inherited vector lost its editor");
    const auto inherited = read(custom);
    Check(inherited.type == derivedVectorType && inherited.value.kind == ParameterValueKind::Vector3, "inherited type identity was replaced by its parent");
    auto changedVector = inherited.value; changedVector.components = {1, 2, 3};
    Check(SetParameterValue(document, document.Reference(custom->GetID()), derivedVectorType, changedVector).ok &&
          document.History().Undo() && read(custom).value == inherited.value, "inherited vector edit/undo failed");
    document.History().Clear(); context->DestroyObject(custom); manager->UnRegisterParameterType(derivedVectorType);
    Check(app.BeginEdit("parameter.integer", scope, integerRef, edit) == EDITOR_OK && app.UpdateEdit(edit, Integer(11)) == EDITOR_OK,
          "prepare busy draft");
    native = false; app.NativeAvailable(false);
    Check(app.UpdateEdit(edit, Integer(12)) == EDITOR_OK && app.FinishEdit(edit, true) == EDITOR_REFUSED,
          "busy runtime accepted a value commit");
    native = true; app.NativeAvailable(true);
    app.FinishEdit(edit, false); edit = 0;
    unsigned nestedCalls = 0;
    const EditorParameter arguments[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}};
    // A fresh application exercises an independently selected nested command
    // beyond the 256-result window, through the same native-dialog entry.
    {
        EditorApplication nested([&] { return &document; });
        ParameterCommands nestedCommands([&] { return &document; });
        const EditorCommandDesc wrapped{sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.integer-set"), EDITOR_TEXT("test.nested-integer"),
            EDITOR_TEXT("Nested integer"), arguments, 3,
            [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
                ++*static_cast<unsigned *>(data);
                auto next = *request; next.handler = EDITOR_TEXT("builtin.parameters");
                return host->execute(host->data, EditorString EDITOR_TEXT("parameter.integer-set"), &next, sink);
            }, &nestedCalls, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT};
        auto selectedModule = module;
        selectedModule.commands = &wrapped; selectedModule.commandCount = 1;
        Check(nestedCommands.Register(nested, error) && nested.Register(selectedModule, error), "register nested integer modifier");
        EditorConfiguration configuration;
        configuration.providers.push_back({"command", "parameter.integer-set", "test.nested-integer"});
        Check(nested.Activate(EDITOR_IMGUI_VERSION, error, configuration), error.c_str());
        nested.Attach(document.Token(), document.TopologyRevision(), true, true);
        const auto binding = *nested.Property("parameter.integer");
        const auto guid = integer->GetGUID();
        const std::vector<EditorField> identity{{"type", EDITOR_INTEGER_VALUE, {}, {}, (uint64_t(guid.d1) << 32) | guid.d2}};
        for (int i = 1; i <= 300; ++i) {
            Edit transaction(document, "Native value interaction");
            const auto result = nested.ApplyEdit(binding, nested.Scope(), integerRef, Integer(i), identity);
            Check(result.status == EDITOR_OK && result.operation && transaction.Commit().ok,
                  "nested native modifier leaked an unfinished operation");
        }
        Check(nestedCalls == 300 && read(integer).value.integer == 300, "native value interactions were repeated or dropped");
    }
    document.History().Clear();
    const auto finalCount = history();
    Check(app.BeginEdit("parameter.integer", scope, integerRef, edit) == EDITOR_OK, "prepare owner close");
    app.UpdateEdit(edit, Integer(123));
    app.CloseScope(owner);
    Check(app.FinishEdit(edit, true) == EDITOR_STALE && read(integer).value.integer == 300 && history() == finalCount,
          "closed owner submitted an old value");
    app.Stop();
    document.History().Clear();
    CKParameter *parameters[] = {integer, real, boolean, reference, percentage, enumeration};
    for (auto *parameter : parameters) context->DestroyObject(parameter);
    context->GetParameterManager()->UnRegisterParameterType(enumerationType);
}

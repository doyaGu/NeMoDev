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
struct Painter { uint64_t integer = 0; uint32_t action = 0; bool update = false; unsigned calls = 0; };
uint32_t Paint(void *data, const EditorHost *, const EditorValueInput *input) {
    auto &painter = *static_cast<Painter *>(data);
    ++painter.calls;
    if (painter.update) {
        auto value = input->value;
        value.integer = painter.integer;
        Check(input->update.write(input->update.data, &value, 1) == EDITOR_OK, "typed renderer update rejected");
    }
    return painter.action;
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
    const char *ids[] = {"value.boolean", "value.integer", "value.real", "value.enumeration", "value.object"};
    const EditorValueType types[] = {EDITOR_INTEGER_VALUE, EDITOR_INTEGER_VALUE, EDITOR_REAL_VALUE, EDITOR_INTEGER_VALUE, EDITOR_OBJECT};
    EditorValueEditorDesc editors[5]{};
    for (unsigned i = 0; i < 5; ++i)
        editors[i] = {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EditorLiteral(ids[i]), EDITOR_TEXT("test.values"),
            EDITOR_TEXT("Test value"), Paint, &painter, types[i]};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.parameter-editors");
    module.editors = editors; module.editorCount = 5;
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

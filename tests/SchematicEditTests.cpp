#include "application/EditorApplication.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/document/ScriptSession.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/schematic/SchematicEdits.h"
#include "editor/ImGuiDrawing.h"
#include "features/schematic/SchematicCommands.h"
#include "features/setup/SetupCommands.h"
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
EditorField Integer(const char *key, uint64_t value) {
    return {key, EDITOR_INTEGER_VALUE, {}, {}, value};
}
EditorField Objects(const char *key, const std::vector<ObjectRef> &refs) {
    EditorField field{key, EDITOR_OBJECT_LIST};
    field.objects = refs;
    return field;
}
} // namespace
void TestSchematicEdits(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    const auto level = doc.Context()->GetCurrentLevel()->GetID();
    const auto first = CreateScript(doc, level, "Schematic editing A");
    const auto second = CreateScript(doc, level, "Schematic editing B");
    Check(first.ok && second.ok, "create Schematic scripts");
    const auto a = doc.Reference(first.created), b = doc.Reference(second.created);
    Check(EditScriptData(doc, a.object, "Seed comments",
                         [&](InterfaceData &data) {
                             Comment comment;
                             comment.text = "Original";
                             data.rootBehavior.AddComment(comment);
                         }),
          "seed Schematic comment");
    doc.History().Clear();
    doc.MarkSaved();
    doc.History().MarkSavePoint();
    SchematicCommands schematic([&] { return &doc; });
    SetupCommands setup([&] { return &doc; });
    EditorApplication app([&] { return &doc; }, [] { return true; });
    const auto draw = [](void *, const EditorHost *, const EditorValueInput *) -> uint32_t { return 0; };
    const EditorValueEditorDesc editors[] = {
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.string"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Text"), draw, nullptr, EDITOR_TEXT_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.color"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Color"), draw, nullptr, EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.integer"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Integer"), draw, nullptr, EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.boolean"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Boolean"), draw, nullptr, EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.real"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Real"), draw, nullptr, EDITOR_REAL_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.vector3"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Vector"), draw, nullptr, EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.vector2"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Vector"), draw, nullptr, EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.rect"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Rect"), draw, nullptr, EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.object"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Object"), draw, nullptr, EDITOR_OBJECT},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.structure"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Structure"), draw, nullptr, EDITOR_RECORD},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.object-array"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Objects"), draw, nullptr, EDITOR_OBJECT_LIST}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.schematic-values");
    module.editors = editors;
    module.editorCount = std::size(editors);
    Check(schematic.Register(app, error) && setup.Register(app, error) && app.Register(module, error) &&
              app.Activate(EDITOR_IMGUI_VERSION, error),
          error.c_str());
    const auto sync = [&] { app.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    sync();
    // The Schematic's direct display edit must notify an external view too.
    app.EndFrame();
    const auto watching = app.OpenScope();
    unsigned notices = 0;
    EditorSubscription subscription = 0;
    Check(app.Host()->watch(app.Host()->data, watching,
        [](void *data, const EditorHost *, const EditorChange *) { ++*static_cast<unsigned *>(data); },
        &notices, &subscription) == EDITOR_OK, "watch Schematic display edits");
    Check(EditScriptData(doc, a.object, "Change Script color", [](InterfaceData &data) {
        data.header.color ^= 1;
    }), "direct Schematic display edit");
    sync();
    app.EndFrame();
    Check(notices == 1, "direct Schematic display edit did not notify external view");
    Check(doc.History().Undo(), "undo direct display edit");
    app.CloseScope(watching);
    doc.History().Clear();
    doc.MarkSaved();
    doc.History().MarkSavePoint();
    sync();
    const auto owner = app.OpenScope();
    const auto begin = [&](const char *id, const std::vector<EditorField> &fields) {
        const auto values = BorrowEditorFields(fields);
        EditorEdit edit = 0;
        Check(app.BeginEdit(id, {app.Scope(owner), values.data(), uint32_t(values.size())}, edit) == EDITOR_OK,
              "begin Schematic property");
        return edit;
    };
    const auto update = [&](EditorEdit edit, const EditorField &field) {
        const std::vector<EditorField> fields{field};
        const auto value = BorrowEditorFields(fields);
        Check(app.UpdateEdit(edit, value.values.front()) == EDITOR_OK, "update Schematic property");
    };
    const auto finish = [&](EditorEdit edit) {
        Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "confirm Schematic property");
        app.EndFrame();
        sync();
    };
    const std::vector<EditorField> commentArgs{
        {"target", EDITOR_OBJECT, a}, {"owner", EDITOR_OBJECT, a}, Integer("index", 0)};
    const auto text = [&] { return doc.Session(a.object)->Data().rootBehavior.comments[0].text; };
    auto edit = begin("schematic.comment", commentArgs);
    for (int i = 0; i < 40; ++i)
        update(edit, {"value", EDITOR_TEXT_VALUE, {}, "Draft " + std::to_string(i)});
    Check(text() == "Original" && !doc.Dirty() && !doc.History().CanUndo(), "comment draft touched document");
    finish(edit);
    Check(text() == "Draft 39" && doc.History().UndoCount() == 1, "comment did not form one undo step");
    Check(doc.History().Undo() && text() == "Original" && !doc.Dirty(), "undo comment");
    sync();
    edit = begin("schematic.comment", commentArgs);
    update(edit, {"value", EDITOR_TEXT_VALUE, {}, "Cancelled"});
    Check(app.FinishEdit(edit, false) == EDITOR_CANCELLED && text() == "Original" && doc.History().CanRedo() &&
              !doc.Dirty(),
          "comment cancellation changed document or Redo");
    edit = begin("schematic.comment", commentArgs);
    finish(edit);
    Check(doc.History().UndoCount() == 0 && doc.History().CanRedo() && !doc.Dirty(), "unchanged comment cleared Redo");

    // A comment slot cannot be reused after another display edit.
    edit = begin("schematic.comment", commentArgs);
    update(edit, {"value", EDITOR_TEXT_VALUE, {}, "Stale"});
    Check(EditScriptData(doc, a.object, "Replace comment",
                         [&](InterfaceData &data) { data.rootBehavior.comments[0].text = "Replacement"; }),
          "replace comment slot");
    Check(app.FinishEdit(edit, true) == EDITOR_STALE && text() == "Replacement", "stale comment overwrote reused slot");
    Check(doc.History().Undo() && text() == "Original", "undo external comment change");
    sync();

    const auto beforeA = doc.Session(a.object)->Data().header.color;
    const auto beforeB = doc.Session(b.object)->Data().header.color;
    const auto steps = doc.History().UndoCount();
    edit = begin("schematic.script-color", {{"target", EDITOR_OBJECT, a}, Objects("targets", {a, b})});
    EditorField color{"value", EDITOR_REAL_LIST};
    color.reals = {.2, .4, .6, 1};
    for (int i = 0; i < 20; ++i)
        update(edit, color);
    Check(doc.Session(a.object)->Data().header.color == beforeA &&
              doc.Session(b.object)->Data().header.color == beforeB,
          "unconfirmed color changed document");
    finish(edit);
    Check(doc.History().UndoCount() == steps + 1 && doc.Session(a.object)->Data().header.color == 0x996633 &&
              doc.Session(b.object)->Data().header.color == 0x996633,
          "script color batch was not atomic");
    const std::vector<EditorField> resetFields{{"target", EDITOR_OBJECT, a}, Objects("targets", {a, b})};
    const auto resetValues = BorrowEditorFields(resetFields);
    Check(app.Execute("schematic.reset-color", {app.Scope(owner), resetValues.data(), uint32_t(resetValues.size())})
                  .status == EDITOR_OK,
          "reset script colors");
    Check(doc.History().UndoCount() == steps + 2 && doc.Session(a.object)->Data().header.color == beforeA &&
              doc.Session(b.object)->Data().header.color == beforeB,
          "reset colors was not one undo step");
    Check(doc.History().Undo() && doc.Session(a.object)->Data().header.color == 0x996633 &&
              doc.Session(b.object)->Data().header.color == 0x996633,
          "undo reset colors");
    sync();
    Check(doc.History().Undo() && doc.Session(a.object)->Data().header.color == beforeA &&
              doc.Session(b.object)->Data().header.color == beforeB,
          "undo script colors");
    sync();

    // An unchanged first target must not skip later targets in a batch.
    auto *behaviorA = CKBehavior::Cast(doc.Resolve(a)), *behaviorB = CKBehavior::Cast(doc.Resolve(b));
    const auto priority = behaviorA->GetPriority();
    behaviorB->SetPriority(priority + 3);
    edit = begin("setup.behavior.priority", {{"target", EDITOR_OBJECT, a}, Objects("targets", {a, b})});
    finish(edit);
    Check(behaviorA->GetPriority() == priority && behaviorB->GetPriority() == priority &&
              doc.History().UndoCount() == steps + 1,
          "unchanged first priority skipped batch");
    Check(doc.History().Undo() && behaviorB->GetPriority() == priority + 3, "undo priority batch");
    sync();
    auto *orphan = doc.Context()->CreateObject(CKCID_BEHAVIOR, "Unattached behavior");
    Check(orphan, "create orphan behavior");
    doc.Include(orphan->GetID());
    edit = begin("setup.behavior.priority",
                 {{"target", EDITOR_OBJECT, a}, Objects("targets", {a, doc.Reference(orphan->GetID())})});
    update(edit, Integer("value", priority + 5));
    finish(edit);
    Check(behaviorA->GetPriority() == priority && doc.History().UndoCount() == steps && doc.History().CanRedo(),
          "failed priority batch kept partial changes or cleared Redo");

    const auto closing = app.OpenScope();
    const auto args = BorrowEditorFields(commentArgs);
    Check(app.BeginEdit("schematic.comment", {app.Scope(closing), args.data(), uint32_t(args.size())}, edit) ==
              EDITOR_OK,
          "begin closing comment");
    update(edit, {"value", EDITOR_TEXT_VALUE, {}, "Closed"});
    app.CloseScope(closing);
    Check(app.FinishEdit(edit, true) == EDITOR_STALE && text() == "Original", "closing view committed comment");
    edit = begin("schematic.comment", commentArgs);
    Check(doc.New(&error), error.c_str());
    sync();
    Check(app.FinishEdit(edit, true) == EDITOR_STALE && !doc.History().CanUndo(),
          "old Schematic draft reached new document");
    app.Stop();
}

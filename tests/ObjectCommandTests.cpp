#include "application/EditorApplication.h"
#include "application/ObjectSelection.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include "features/objects/ObjectCommands.h"
#include "features/objects/SelectionCommands.h"
#include "workspace/ObjectDrag.h"

#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
ObjectRef Created(const EditorResponse &response) {
    for (const auto &field : response.fields)
        if (field.key == "created")
            return field.object;
    return {};
}
void TestObjectDrags() {
    const ObjectRef a{{71}, 1, 4}, b{{71}, 2, 6}, otherDocument{{72}, 3, 9};
    auto bytes = WriteObjectDrag(b, {a, b, a});
    Check(ReadObjectDrag(bytes.data(), bytes.size()) == std::vector<ObjectRef>({b, a}),
          "object drag lost the grabbed row or duplicated the source selection");
    const auto copied = bytes;
    bytes.clear();
    Check(ObjectDragSource(copied.data(), copied.size()) == b, "single target used a different selected object");
    bytes = WriteObjectDrag(b, {a});
    Check(ReadObjectDrag(bytes.data(), bytes.size()) == std::vector<ObjectRef>{b},
          "unselected dragged row included another selection");
    Check(WriteObjectDrag({}, {a}).empty() && WriteObjectDrag(a, {a, otherDocument}).empty(),
          "invalid source or cross-document selection was serialized");
    Check(ReadObjectDrag(nullptr, sizeof(a)).empty() && ReadObjectDrag(&a, 0).empty() &&
              ReadObjectDrag(copied.data(), copied.size() - 1).empty(),
          "truncated object drag was accepted");
    for (const auto bad : {ObjectRef{}, otherDocument, a}) {
        const ObjectRef objects[] = {a, bad};
        Check(ReadObjectDrag(objects, sizeof(objects)).empty(), "malformed trailing object was ignored");
    }
    std::vector<unsigned char> unaligned(copied.size() + 1);
    std::memcpy(unaligned.data() + 1, copied.data(), copied.size());
    Check(ReadObjectDrag(unaligned.data() + 1, copied.size()) == std::vector<ObjectRef>({b, a}),
          "object drag required aligned UI storage");
    std::vector<ObjectRef> oversized(65537, a);
    Check(WriteObjectDrag(a, oversized).empty() &&
              ReadObjectDrag(oversized.data(), oversized.size() * sizeof(a)).empty(),
          "object drag exceeded the command object limit");
}
} // namespace

void TestObjectCommands(vdev::CkRuntimeHost &runtime, const std::string &modulePath) {
    using namespace vdev;
    TestObjectDrags();
    Document document(runtime);
    std::string error;
    Check(document.New(&error), error.c_str());
    bool native = true;
    EditorApplication editor([&] { return &document; }, [&] { return native; });
    ObjectSelection selection;
    ObjectCommands objects([&] { return &document; });
    SelectionCommands selections([&] { return &document; }, selection);
    Check(objects.Register(editor, error) && selections.Register(editor, error) && editor.Load(modulePath, error),
          error.c_str());
    auto configuration = EditorConfiguration{};
    configuration.providers.push_back({"command", "object.rename", "builtin.objects"});
    configuration.providers.push_back({"command", "object.create-frame3d", "builtin.objects"});
    configuration.providers.push_back({"editor", "text.single-line", "example.text"});
    Check(editor.Activate(EDITOR_IMGUI_VERSION, error, configuration), error.c_str());
    const auto attach = [&](bool writable = true) {
        editor.Attach(document.Token(), document.TopologyRevision(), true, writable);
    };
    attach();
    const auto execute = [&](const char *id, const std::vector<EditorValue> &values = {}) {
        return editor.Execute(id, {editor.Scope(), values.data(), static_cast<uint32_t>(values.size())});
    };
    const auto list = [](const std::vector<ObjectRef> &references) {
        return EditorValue{EditorLiteral("objects"),
                           EDITOR_OBJECT_LIST,
                           {},
                           {},
                           0,
                           {references.data(), static_cast<uint32_t>(references.size())}};
    };
    const auto frame = Created(execute("object.create-frame3d"));
    const auto second = Created(execute("object.create-frame3d"));
    Check(frame && second && document.Resolve(frame) && document.Resolve(second), "registered creation failed");
    auto history = document.History().UndoCount();
    Check(history == 2 && document.History().Undo(), "creation did not form one history step");
    Check(!document.Resolve(second) && document.Resolve(frame), "undo removed the wrong creation");
    Check(document.History().Redo() && document.Resolve(second), "redo lost creation identity");
    document.MarkSaved();
    document.History().MarkSavePoint();
    attach();
    const auto levelObjects = document.Context()->GetCurrentLevel()->GetLevelScene()->GetObjectCount();
    Check(execute("example.create-frame-throw").status == EDITOR_ERROR && !document.Dirty() && !document.EditOpen() &&
              document.History().UndoCount() == history &&
              document.Context()->GetCurrentLevel()->GetLevelScene()->GetObjectCount() == levelObjects,
          "failed module creation left an object or history behind");
    std::vector<ObjectRef> targets{frame};
    Check(execute("selection.replace", {list(targets)}).status == EDITOR_OK && selection.Contains(frame.object),
          "selection command failed");
    const auto revision = selection.Revision();
    Check(execute("selection.lock", {{EditorLiteral("locked"), EDITOR_INTEGER_VALUE, {}, {}, 1}}).status == EDITOR_OK,
          "lock refused");
    targets = {second};
    Check(execute("selection.replace", {list(targets)}).status == EDITOR_REFUSED && selection.Contains(frame.object) &&
              selection.Revision() == revision && !document.Dirty() && document.History().UndoCount() == history,
          "locked selection changed content or history");
    native = false;
    const auto projected = editor.Query("selection.objects", {editor.Scope(), nullptr, 0});
    Check(projected.status == EDITOR_OK && projected.fields[0].objects == std::vector<ObjectRef>{frame},
          "selection query accessed native state while busy");
    Check(execute("selection.replace", {list(targets)}).status == EDITOR_REFUSED, "busy native selection accepted");
    native = true;
    Check(execute("selection.lock", {{EditorLiteral("locked"), EDITOR_INTEGER_VALUE}}).status == EDITOR_OK,
          "unlock refused");
    std::vector<ObjectRef> largeSelection(50000, frame);
    largeSelection.back() = second;
    Check(execute("selection.replace", {list(largeSelection)}).status == EDITOR_OK &&
              selection.References() == std::vector<ObjectRef>({frame, second}) && !document.Dirty() &&
              document.History().UndoCount() == history,
          "large selection was truncated or changed document history");
    attach(false);
    Check(execute("object.destroy", {list(targets)}).status == EDITOR_REFUSED && document.Resolve(second),
          "read-only deletion accepted");
    Check(execute("selection.replace", {list(targets)}).status == EDITOR_OK, "read-only document blocked selection");
    attach();
    targets = {frame, second};
    editor.EnterDraw();
    const auto deferred = execute("example.create-scripts", {list(targets)});
    editor.LeaveDraw();
    Check(deferred.status == EDITOR_QUEUED && deferred.operation, "batch command was not deferred");
    targets.assign(1, {}); // request must own its original array
    editor.EndFrame();
    Check(document.History().UndoCount() == history + 1, "batch scripts split history");
    const EditorValue operation{EditorLiteral("operation"), EDITOR_INTEGER_VALUE, {}, {}, deferred.operation};
    const auto outcome = editor.Query("application.operation", {editor.Scope(), &operation, 1});
    uint64_t count = 0;
    for (const auto &field : outcome.fields) {
        if (field.key == "status")
            Check(field.integer == EDITOR_OK, "deferred batch failed after input buffer changed");
        if (field.key == "count")
            count = field.integer;
    }
    std::vector<ObjectRef> scripts;
    for (uint64_t index = 0; index < count; ++index) {
        const EditorValue values[] = {operation, {EditorLiteral("index"), EDITOR_INTEGER_VALUE, {}, {}, index}};
        const auto output = editor.Query("application.operation-output", {editor.Scope(), values, 2});
        Check(output.status == EDITOR_OK, "owned output was lost");
        if (output.fields[0].text == "objects")
            for (const auto &field : output.fields)
                if (field.key == "objects")
                    scripts = field.objects;
    }
    Check(scripts.size() == 2 && document.Resolve(scripts.front()) && document.Resolve(scripts.back()),
          "batch result array was not retained");
    Check(execute("script.delete", {list(scripts)}).status == EDITOR_OK && !document.Resolve(scripts.front()),
          "registered script deletion failed");
    Check(document.History().Undo() && document.Resolve(scripts.front()) && document.Resolve(scripts.back()),
          "script deletion was not one undo step");
    attach();
    const auto scene = Created(execute("object.create-scene", {list({frame, second})}));
    Check(scene && CKScene::Cast(document.Resolve(scene))->IsObjectHere(CKSceneObject::Cast(document.Resolve(frame))),
          "scene creation lost members");
    targets = {frame};
    const EditorValue sceneValue{EditorLiteral("scene"), EDITOR_OBJECT, scene};
    Check(execute("object.remove-from-scene",
                  {list(targets), sceneValue, {EditorLiteral("dependencies"), EDITOR_INTEGER_VALUE, {}, {}, 1}})
                  .status == EDITOR_OK,
          "registered scene removal failed");
    Check(document.History().Undo() &&
              CKScene::Cast(document.Resolve(scene))->IsObjectHere(CKSceneObject::Cast(document.Resolve(frame))),
          "scene removal lost undo");
    const auto group = Created(execute("object.create-group", {list(targets)}));
    Check(group && CKGroup::Cast(document.Resolve(group))->GetObjectCount() == 1, "group creation failed");
    Check(execute("object.remove-from-group", {list(targets), {EditorLiteral("group"), EDITOR_OBJECT, group}}).status ==
              EDITOR_OK,
          "registered group removal failed");
    Check(document.History().Undo() && CKGroup::Cast(document.Resolve(group))->GetObjectCount() == 1,
          "group removal lost undo");
    const auto place = Created(execute("object.create-place", {list(targets)}));
    const auto otherPlace = Created(execute("object.create-place"));
    Check(place && otherPlace, "place creation failed");
    const auto portal =
        Created(execute("object.create-automatic-portal", {{EditorLiteral("place"), EDITOR_OBJECT, place},
                                                           {EditorLiteral("other"), EDITOR_OBJECT, otherPlace}}));
    Check(portal && document.Resolve(portal) && CKPlace::Cast(document.Resolve(place))->GetPortalCount() == 1,
          "registered automatic portal failed");
    Check(document.History().Undo() && !document.Resolve(portal), "automatic portal lost undo");
    Check(execute("object.create-automatic-portals").status == EDITOR_NO_CHANGE && document.History().CanRedo(),
          "unchanged automatic portals cleared redo");
    attach();
    const auto beforeOptions = editor.Query("object.delete-options", {editor.Scope(), nullptr, 0});
    history = document.History().UndoCount();
    const uint64_t bad = std::numeric_limits<uint64_t>::max();
    auto invalidOptions = beforeOptions.fields[1].integers;
    Check(!invalidOptions.empty(), "native dependency classes are missing");
    invalidOptions[0] = bad;
    Check(execute("object.destroy", {list(targets),
                                     {EditorLiteral("custom"), EDITOR_INTEGER_VALUE, {}, {}, 1},
                                     {EditorLiteral("options"),
                                      EDITOR_INTEGER_LIST,
                                      {},
                                      {},
                                      0,
                                      {},
                                      {invalidOptions.data(), static_cast<uint32_t>(invalidOptions.size())}}})
                      .status == EDITOR_INVALID &&
              document.Resolve(frame) && document.History().UndoCount() == history,
          "invalid delete options changed the document");
    const auto afterOptions = editor.Query("object.delete-options", {editor.Scope(), nullptr, 0});
    Check(beforeOptions.fields[0].integer == afterOptions.fields[0].integer &&
              beforeOptions.fields[1].integers == afterOptions.fields[1].integers,
          "invalid options replaced session defaults");
    const uint64_t shortOptions[] = {0};
    Check(execute("object.destroy", {list(targets),
                                     {EditorLiteral("custom"), EDITOR_INTEGER_VALUE, {}, {}, 1},
                                     {EditorLiteral("options"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {shortOptions, 1}}})
                      .status == EDITOR_INVALID &&
              document.Resolve(frame) && document.History().UndoCount() == history,
          "short dependency array reached native deletion");
    Check(execute("object.destroy", {list(targets),
                                     {EditorLiteral("custom"), EDITOR_INTEGER_VALUE, {}, {}, 1},
                                     {EditorLiteral("flags"), EDITOR_INTEGER_VALUE, {}, {}, 4},
                                     {EditorLiteral("options"),
                                      EDITOR_INTEGER_LIST,
                                      {},
                                      {},
                                      0,
                                      {},
                                      {beforeOptions.fields[1].integers.data(),
                                       static_cast<uint32_t>(beforeOptions.fields[1].integers.size())}}})
                      .status == EDITOR_INVALID &&
              document.Resolve(frame) && document.History().UndoCount() == history,
          "unknown dependency flags reached native deletion");
    EditorValue invalid = list(targets);
    invalid.objects = {nullptr, 1};
    Check(execute("object.destroy", {invalid}).status == EDITOR_INVALID, "null object array accepted");
    invalid.objects = {targets.data(), 0xffffffffu};
    Check(execute("object.destroy", {invalid}).status == EDITOR_INVALID, "oversized object array accepted");
    auto *victim = document.Context()->CreateObject(CKCID_OBJECT, "Pending delete");
    Check(victim != nullptr, "native target creation failed");
    const auto old = document.Reference(victim->GetID());
    targets = {frame, old};
    editor.EnterDraw();
    const auto staleDeletion = execute("object.destroy", {list(targets)});
    editor.LeaveDraw();
    Check(staleDeletion.status == EDITOR_QUEUED, "valid deletion was not queued");
    document.Context()->DestroyObject(old.object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID);
    auto *replacement = document.Context()->CreateObject(CKCID_OBJECT, "Reused target");
    Check(replacement && replacement->GetID() == old.object && document.Reference(old.object) != old,
          "native reuse was not reproduced");
    Check(execute("object.destroy", {list(targets)}).status == EDITOR_STALE && document.Resolve(frame) &&
              document.Context()->GetObject(old.object) == replacement && document.History().UndoCount() == history,
          "stale batch deleted another object or cleared history");
    editor.EndFrame();
    const EditorValue staleOperation{EditorLiteral("operation"), EDITOR_INTEGER_VALUE, {}, {}, staleDeletion.operation};
    const auto staleResult = editor.Query("application.operation", {editor.Scope(), &staleOperation, 1});
    Check(staleResult.status == EDITOR_OK && staleResult.fields.front().integer == EDITOR_STALE &&
              document.Resolve(frame) && document.Context()->GetObject(old.object) == replacement &&
              document.History().UndoCount() == history,
          "queued deletion did not recheck every object lifetime");
    targets = {frame};
    Check(execute("selection.replace", {list(targets)}).status == EDITOR_OK, "deletion selection could not be prepared");
    EditorStatus worker = EDITOR_OK;
    std::thread thread([&] { worker = execute("object.destroy", {list(targets)}).status; });
    thread.join();
    Check(worker == EDITOR_REFUSED && document.Resolve(frame), "wrong-thread deletion accepted");
    Check(execute("object.destroy", {list(targets)}).status == EDITOR_OK && !document.Resolve(frame) &&
              !document.History().CanUndo() && !document.History().CanRedo(),
          "native deletion did not preserve history-clearing behavior");
    selection.Validate(document);
    Check(!selection.Contains(frame.object), "deleted selection remained live");
    const auto array = Created(execute("object.create-array"));
    Check(bool(array), "array creation failed");
    targets = {array};
    const std::vector<uint64_t> expectedOptions(static_cast<size_t>(CKGetClassCount()), 0);
    auto options = expectedOptions;
    editor.EnterDraw();
    const auto deletion =
        execute("object.destroy", {list(targets),
                                   {EditorLiteral("custom"), EDITOR_INTEGER_VALUE, {}, {}, 1},
                                   {EditorLiteral("flags"), EDITOR_INTEGER_VALUE, {}, {}, CK_DEPENDENCIES_NONE},
                                   {EditorLiteral("options"),
                                    EDITOR_INTEGER_LIST,
                                    {},
                                    {},
                                    0,
                                    {},
                                    {options.data(), static_cast<uint32_t>(options.size())}}});
    editor.LeaveDraw();
    options[0] = bad;
    editor.EndFrame();
    Check(deletion.status == EDITOR_QUEUED && !document.Resolve(array), "integer array request was not copied");
    const auto remembered = editor.Query("object.delete-options", {editor.Scope(), nullptr, 0});
    Check(remembered.status == EDITOR_OK && remembered.fields[1].integers == expectedOptions,
          "delete options lost their owned values");
}

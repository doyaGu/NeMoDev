#include "application/ObjectSelection.h"
#include "application/Preferences.h"
#include "application/RuntimeOperation.h"
#include "core/document/Document.h"
#include "core/layout/LayoutTools.h"
#include "core/layout/LayoutView.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include "features/layout/LayoutCommands.h"
#include "features/layout/LayoutPresentation.h"
#include "features/layout/SnapshotCommands.h"
#include "features/layout/SnapshotData.h"
#include "workspace/EditorClient.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
const EditorField &Field(const EditorResponse &response, const char *key) {
    auto *value = EditorClient::Find(response, key);
    Check(value, "missing viewport field");
    return *value;
}
EditorField Number(const char *key, uint64_t value) {
    return {key, EDITOR_INTEGER_VALUE, {}, {}, value};
}
EditorField Reals(const char *key, std::initializer_list<double> values) {
    EditorField field{key, EDITOR_REAL_LIST};
    field.reals = values;
    return field;
}
EditorField Motion(double x, double y) {
    EditorField field{"value", EDITOR_RECORD};
    field.fields = {Reals("position", {x, y}), Number("move", 1)};
    return field;
}
struct Viewport : LayoutViewport {
    int reads = 0;
    unsigned char color;
    explicit Viewport(unsigned char color) : color(color) {}
    bool CapturePixels(std::vector<unsigned char> &pixels, int &width, int &height) override {
        ++reads;
        width = 104;
        height = 80;
        pixels.assign(width * height * 4, color);
        return true;
    }
    bool EnterFullScreen(const LayoutScreenMode &, std::string &) override { return false; }
    bool ExitFullScreen(bool, std::string &) override { return true; }
};
} // namespace

void TestLayoutViewports(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    LayoutView left(runtime), right(runtime);
    left.CompositionChanged();
    right.CompositionChanged();
    const auto leftCamera = left.ViewCamera(LayoutViewCamera::Perspective);
    const auto rightCamera = right.ViewCamera(LayoutViewCamera::Perspective);
    Check(leftCamera && rightCamera && leftCamera != rightCamera, "default cameras are shared between viewports");
    const auto leftCameras = left.Cameras(), rightCameras = right.Cameras();
    Check(std::find(leftCameras.begin(), leftCameras.end(), rightCamera) == leftCameras.end() &&
              std::find(rightCameras.begin(), rightCameras.end(), leftCamera) == rightCameras.end(),
          "another viewport's private camera appeared in the camera list");
    std::vector<CKObjectHandle> rightObjects;
    const auto &nativeObjects = doc.Context()->GetObjectListByType(CKCID_OBJECT, TRUE);
    for (int i = 0; i < nativeObjects.Size(); ++i) {
        auto *object = nativeObjects[i];
        if (object && right.IsInterfaceObject(object->GetID())) {
            Check(!left.IsInterfaceObject(object->GetID()), "viewports share an owned interface object");
            rightObjects.push_back(doc.Context()->GetObjectHandle(object->GetID()));
        }
    }
    Check(!rightObjects.empty(), "missing second viewport interface resources");
    ObjectSelection selection;
    Preferences preferences;
    EditorApplication editor([&] { return &doc; });
    EditorView a = 0, b = 0;
    Viewport first(17), second(31);
    LayoutTargetLookup lookup = [&](EditorView id) {
        return id == b && b ? LayoutTarget{b, right, &second} : LayoutTarget{a, left, &first};
    };
    LayoutCommands commands(editor, [&] { return &doc; }, lookup, selection);
    LayoutPresentation presentation(editor, [&] { return &doc; }, lookup, selection, preferences, [] { return false; });
    RuntimeOperation operations;
    SnapshotCommands snapshots(
        editor, operations, [&](EditorView id) -> LayoutViewport & { return *lookup(id).viewport; },
        [&] { return &doc; });
    Check(commands.Register(error) && presentation.Register(error) && snapshots.Register(error) &&
              editor.Activate(EDITOR_IMGUI_VERSION, error),
          error.c_str());
    auto attach = [&] { editor.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    attach();
    a = editor.OpenScope();
    b = editor.OpenScope();
    const EditorClient l(editor.Host(), a), r(editor.Host(), b);
    auto scene = [](const EditorClient &client, uint64_t revision = 0) {
        return client.Query("layout.scene",
                            {TextArgument("section", "cameras"), IntegerArgument("revision", revision)});
    };
    const auto lscene = scene(l), rscene = scene(r);
    Check(lscene.status == EDITOR_OK && rscene.status == EDITOR_OK &&
              Field(lscene, "camera").object.object == leftCamera &&
              Field(rscene, "camera").object.object == rightCamera,
          "viewport queries used the wrong camera");
    const auto version = EditorClient::Integer(lscene, "revision");
    Check(r.Command("layout.set-camera", {ObjectArgument("target", doc.Reference(leftCamera))}).status ==
              EDITOR_REFUSED,
          "another viewport's private camera was attached");
    Check(r.Command("layout.set-camera",
                    {ObjectArgument("target", doc.Reference(right.ViewCamera(LayoutViewCamera::Top)))})
                  .status == EDITOR_OK,
          "select second viewport camera");
    Check(scene(l, version).status == EDITOR_OK && left.Camera()->GetID() == leftCamera &&
              right.Camera()->GetID() == right.ViewCamera(LayoutViewCamera::Top),
          "reading or changing another viewport invalidated a camera page");
    Check(r.Command("layout.set-camera", {ObjectArgument("target", doc.Reference(rightCamera))}).status == EDITOR_OK,
          "restore second perspective camera");
    const auto camera = CreateLayoutObject(doc, LayoutObject::Camera);
    Check(camera.ok, "create shared document camera");
    attach();
    const auto cameraRef = doc.Reference(camera.created);
    Check(l.Command("layout.set-camera", {ObjectArgument("target", cameraRef)}).status == EDITOR_OK &&
              r.Command("layout.set-camera", {ObjectArgument("target", cameraRef)}).status == EDITOR_OK,
          "document camera could not be selected in two viewports");
    Check(l.Command("layout.set-camera", {ObjectArgument("target", doc.Reference(leftCamera))}).status == EDITOR_OK &&
              right.Camera()->GetID() == camera.created,
          "changing one attachment changed another viewport");
    Check(r.Command("layout.set-camera", {ObjectArgument("target", doc.Reference(rightCamera))}).status == EDITOR_OK,
          "restore private camera after shared camera selection");
    const auto curve = CreateLayoutObject(doc, LayoutObject::Curve);
    Check(curve.ok, "create viewport curve");
    attach();
    Check(l.Command("layout.set-curve-tool", {ObjectArgument("target", doc.Reference(curve.created)),
                                              IntegerArgument("tool", 128), IntegerArgument("plane", 1)})
                  .status == EDITOR_OK,
          "choose first viewport curve tool");
    Check(EditorClient::Integer(l.Query("layout.curve-tool"), "tool") == 128 &&
              !EditorClient::Integer(r.Query("layout.curve-tool"), "tool"),
          "curve tool leaked to another viewport");
    doc.History().Clear();
    doc.MarkSaved();
    doc.History().MarkSavePoint();
    attach();
    const auto revision = doc.ChangeRevision(), topology = doc.TopologyRevision();
    const auto leftMatrix = left.Camera()->GetWorldMatrix(), rightMatrix = right.Camera()->GetWorldMatrix();
    const auto input = l.Query("layout.input");
    EditorField options{"options", EDITOR_RECORD};
    options.fields = {Number("axes", 5), Number("referential", 1), Number("translate-sticky", 0)};
    const std::vector<EditorField> fields{Field(input, "target"),
                                          Number("kind", 0),
                                          Number("action", unsigned(CameraAction::Pan)),
                                          {"objects", EDITOR_OBJECT_LIST},
                                          Reals("start", {100, 100}),
                                          options,
                                          Motion(100, 100)};
    const auto values = BorrowEditorFields(fields);
    EditorEdit edit = 0;
    Check(l.BeginCommand("layout.edit-gesture", {values.data(), values.data() + values.size()}, edit) == EDITOR_OK,
          "begin first viewport camera gesture");
    const std::vector<EditorField> motion{Motion(160, 130)};
    const auto value = BorrowEditorFields(motion);
    Check(editor.UpdateEdit(edit, *value.data()) == EDITOR_OK &&
              std::memcmp(&leftMatrix, &left.Camera()->GetWorldMatrix(), sizeof(VxMatrix)) != 0 &&
              std::memcmp(&rightMatrix, &right.Camera()->GetWorldMatrix(), sizeof(VxMatrix)) == 0,
          "camera gesture did not stay in its viewport");
    Check(EditorClient::Integer(l.Query("layout.input"), "active") &&
              !EditorClient::Integer(r.Query("layout.input"), "active"),
          "gesture appeared active in both viewports");
    editor.CloseScope(a);
    Check(std::memcmp(&leftMatrix, &left.Camera()->GetWorldMatrix(), sizeof(VxMatrix)) == 0 &&
              !doc.History().UndoCount() && !doc.Dirty() && doc.ChangeRevision() == revision &&
              doc.TopologyRevision() == topology,
          "closing viewport did not cancel its camera gesture");
    Check(l.Query("layout.input").status == EDITOR_STALE, "closed viewport scope resolved to another view");
    a = editor.OpenScope();
    commands.Poll(b);
    Check(!EditorClient::Integer(EditorClient(editor.Host(), a).Query("layout.curve-tool"), "tool") &&
              !EditorClient::Integer(r.Query("layout.curve-tool"), "tool"),
          "reopened viewport inherited a curve tool");
    Check(r.Command("snapshot.take").status == EDITOR_OK && first.reads == 0 && second.reads == 1,
          "snapshot read the wrong viewport");
    SnapshotData data;
    Check(ReadSnapshots(r, data) && data.Has(0) && data.Pixels(0).front() == second.color,
          "snapshot contains another viewport's pixels");
    Check(EditorClient(editor.Host(), a).Command("snapshot.take").status == EDITOR_OK && first.reads == 1 &&
              second.reads == 1,
          "first viewport snapshot did not use its own readback");
    const auto oldCamera = doc.Context()->GetObjectHandle(leftCamera);
    Check(doc.Context()->DestroyObject(leftCamera, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK,
          "delete private camera");
    auto *replacement = doc.Context()->CreateObject(CKCID_TARGETCAMERA, const_cast<char *>("Perspective View"));
    Check(replacement && replacement->GetID() == leftCamera, "private camera ID was not reused");
    replacement->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
    const auto replacementHandle = doc.Context()->GetObjectHandle(replacement->GetID());
    Check(!left.InterfaceAlive() && !doc.Context()->GetObjectByHandle(oldCamera),
          "reused camera revived an old viewport");
    left.Stop();
    Check(doc.Context()->GetObjectByHandle(replacementHandle) && right.InterfaceAlive() &&
              right.Camera()->GetID() == rightCamera,
          "stopping one viewport destroyed another object's lifetime");
    for (const auto handle : rightObjects)
        Check(doc.Context()->GetObjectByHandle(handle),
              "closing one viewport destroyed another viewport's icon resources");
    editor.Stop();
}

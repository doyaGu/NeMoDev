#include "ObjectEditTestObject.h"
#include "application/EditorApplication.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/graph/ParameterValues.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/ObjectProperties.h"
#include "core/level/SceneCommands.h"
#include "core/level/SetupCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/GridCompat.h"
#include "editor/ImGuiDrawing.h"
#include "features/setup/SetupCommands.h"
#include "features/setup/SetupViewData.h"
#include "workspace/EditorClient.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>

void TestSetupPages(vdev::Document &, vdev::EditorApplication &);
void TestSetupProjections(vdev::Document &, vdev::EditorApplication &);
bool RegisterSetupProjectionTests(vdev::EditorApplication &, std::string &);

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
const EditorValue *Arg(const EditorRequest &r, const char *key) {
    for (uint32_t i = 0; i < r.count; ++i)
        if (std::string(r.values[i].key.data, r.values[i].key.size) == key)
            return r.values + i;
    return nullptr;
}
EditorField Int(int64_t v) { return {"value", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(v)}; }
EditorField Real(double v) {
    EditorField f{"value", EDITOR_REAL_VALUE};
    f.real = v;
    return f;
}
EditorField Numbers(std::initializer_list<double> v) {
    EditorField f{"value", EDITOR_REAL_LIST};
    f.reals = v;
    return f;
}
struct Replacement {
    unsigned reads = 0, writes = 0;
    static EditorStatus Read(void *p, const EditorHost *host, const EditorRequest *r, EditorSink sink) {
        ++static_cast<Replacement *>(p)->reads;
        auto request = *r;
        request.handler = EditorString EDITOR_TEXT("builtin.setup");
        return host->query(host->data, EditorString EDITOR_TEXT("setup.camera.fov-value"), &request, sink);
    }
    static EditorStatus Write(void *p, const EditorHost *host, const EditorRequest *r, EditorSink sink) {
        ++static_cast<Replacement *>(p)->writes;
        auto request = *r;
        request.handler = EditorString EDITOR_TEXT("builtin.setup");
        return host->execute(host->data, EditorString EDITOR_TEXT("setup.camera.fov-set"), &request, sink);
    }
    bool Register(EditorApplication &app, std::string &error) {
        const EditorParameter args[] = {
            {EDITOR_TEXT("target"), EDITOR_OBJECT},          {EDITOR_TEXT("value"), EDITOR_REAL_VALUE},
            {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("indices"), EDITOR_INTEGER_LIST},
            {EDITOR_TEXT("relative"), EDITOR_OBJECT, 0, 1},  {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST}};
        const EditorParameter queryArgs[] = {args[0],
                                             {EDITOR_TEXT("indices"), EDITOR_INTEGER_LIST, 1},
                                             {EDITOR_TEXT("relative"), EDITOR_OBJECT, 1, 1},
                                             {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, 1}};
        const EditorParameter fields[] = {args[1], args[2], args[3],
                                          args[4], args[5], {EDITOR_TEXT("schema"), EDITOR_RECORD}};
        const EditorCommandDesc command{sizeof(command),
                                        EDITOR_TEXT("setup.camera.fov-set"),
                                        EDITOR_TEXT("test.setup"),
                                        EDITOR_TEXT("Custom camera"),
                                        args,
                                        6,
                                        Write,
                                        this,
                                        EDITOR_DOCUMENT,
                                        EDITOR_DOCUMENT_EDIT};
        const EditorQueryDesc query{sizeof(query),
                                    EDITOR_TEXT("setup.camera.fov-value"),
                                    EDITOR_TEXT("test.setup"),
                                    queryArgs,
                                    4,
                                    Read,
                                    this,
                                    EDITOR_QUERY_EACH_READ,
                                    EDITOR_DOCUMENT,
                                    fields,
                                    6};
        EditorModuleDesc module{};
        module.size = sizeof(module);
        module.version = EDITOR_VERSION;
        module.id = EDITOR_TEXT("test.setup");
        module.commands = &command;
        module.commandCount = 1;
        module.queries = &query;
        module.queryCount = 1;
        return app.Register(module, error);
    }
};
struct FailureProvider {
    Document &doc;
    bool fail = false;
    int captures = 0, cancels = 0;
    struct Preview {
        ObjectsGesture gesture;
    };
    static EditorStatus Read(void *p, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &data = *static_cast<FailureProvider *>(p);
        auto *o = static_cast<vdevtest::PropertyTestObject *>(data.doc.Resolve(Arg(*r, "target")->object));
        const EditorValue value{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(o->value)};
        return sink.write(sink.data, &value, 1);
    }
    static EditorStatus Run(void *p, const EditorHost *, const EditorRequest *r, EditorSink) {
        auto &data = *static_cast<FailureProvider *>(p);
        auto result =
            EditObjectState(data.doc, Arg(*r, "target")->object.object, "Failure test", [&](CKObject *object) {
                static_cast<vdevtest::PropertyTestObject *>(object)->value = int(Arg(*r, "value")->integer);
            });
        return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_ERROR;
    }
    static EditorStatus PreviewValue(void *p, const EditorHost *host, const EditorRequest *r,
                                     EditorPreviewAction action, void **state) {
        auto &data = *static_cast<FailureProvider *>(p);
        if (action == EDITOR_PREVIEW_CANCEL) {
            std::unique_ptr<Preview> saved(static_cast<Preview *>(*state));
            *state = nullptr;
            if (!saved)
                return EDITOR_OK;
            ++data.cancels;
            return saved->gesture.Cancel(data.doc) ? EDITOR_OK : EDITOR_ERROR;
        }
        // A preview may neither run another command nor finish itself recursively.
        Check(host->execute(host->data, EditorString EDITOR_TEXT("test.preview-set"), r, {}) == EDITOR_REFUSED,
              "preview reentered mutation");
        auto *o = static_cast<vdevtest::PropertyTestObject *>(data.doc.Resolve(Arg(*r, "target")->object));
        if (!*state) {
            auto saved = std::make_unique<Preview>();
            if (!saved->gesture.Begin(data.doc, {o->GetID()}))
                return EDITOR_ERROR;
            *state = saved.release();
            ++data.captures;
        }
        ++o->writes;
        o->value = int(Arg(*r, "value")->integer);
        if (data.fail)
            throw std::runtime_error("Setter failed after writing");
        return EDITOR_OK;
    }
    bool Register(EditorApplication &app, std::string &error) {
        const EditorParameter qargs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT}};
        const EditorParameter fields[] = {{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE}};
        const EditorParameter args[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT}, fields[0]};
        const EditorCommandDesc command{sizeof(command),
                                        EDITOR_TEXT("test.preview-set"),
                                        EDITOR_TEXT("test.preview"),
                                        EDITOR_TEXT("Failure test"),
                                        args,
                                        2,
                                        Run,
                                        this,
                                        EDITOR_DOCUMENT,
                                        EDITOR_DOCUMENT_EDIT,
                                        nullptr,
                                        PreviewValue};
        const EditorQueryDesc query{sizeof(query),
                                    EDITOR_TEXT("test.preview-value"),
                                    EDITOR_TEXT("test.preview"),
                                    qargs,
                                    1,
                                    Read,
                                    this,
                                    EDITOR_QUERY_EACH_READ,
                                    EDITOR_DOCUMENT,
                                    fields,
                                    1};
        const EditorPropertyDesc property{
            sizeof(property), EDITOR_TEXT("test.preview"), EDITOR_TEXT("Failure test"), query.id, EDITOR_TEXT("value"),
            command.id,       EDITOR_TEXT("value"),        EDITOR_TEXT("value.integer")};
        EditorModuleDesc module{};
        module.size = sizeof(module);
        module.version = EDITOR_VERSION;
        module.id = EDITOR_TEXT("test.preview");
        module.commands = &command;
        module.commandCount = 1;
        module.queries = &query;
        module.queryCount = 1;
        module.properties = &property;
        module.propertyCount = 1;
        return app.Register(module, error);
    }
};
} // namespace
void TestSetupEdits(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    SetupCommands commands([&] { return &doc; });
    FailureProvider failure{doc};
    Replacement replacement;
    EditorApplication app([&] { return &doc; }, [] { return true; });
    const char *ids[] = {"value.boolean", "value.integer",   "value.real",        "value.string",
                         "value.color",   "value.vector3",   "value.rect",        "value.vector2",
                         "value.object",  "value.structure", "value.object-array"};
    const EditorValueType types[] = {EDITOR_INTEGER_VALUE, EDITOR_INTEGER_VALUE, EDITOR_REAL_VALUE, EDITOR_TEXT_VALUE,
                                     EDITOR_REAL_LIST,     EDITOR_REAL_LIST,     EDITOR_REAL_LIST,  EDITOR_REAL_LIST,
                                     EDITOR_OBJECT,        EDITOR_RECORD,        EDITOR_OBJECT_LIST};
    EditorValueEditorDesc editors[std::size(ids)]{};
    for (size_t i = 0; i < std::size(ids); ++i)
        editors[i] = {sizeof(EditorValueEditorDesc),
                      EDITOR_IMGUI_VERSION,
                      EditorLiteral(ids[i]),
                      EDITOR_TEXT("test.setup-values"),
                      EDITOR_TEXT("Value"),
                      [](void *, const EditorHost *, const EditorValueInput *) -> uint32_t { return 0; },
                      nullptr,
                      types[i]};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.setup-values");
    module.editors = editors;
    module.editorCount = uint32_t(std::size(editors));
    EditorConfiguration defaults;
    defaults.providers = {{"command", "setup.camera.fov-set", "builtin.setup"},
                          {"query", "setup.camera.fov-value", "builtin.setup"},
                          {"query", "setup.objects", "builtin.setup"},
                          {"query", "setup.transform", "builtin.setup"}};
    Check(commands.Register(app, error) && RegisterSetupProjectionTests(app, error) && failure.Register(app, error) &&
              replacement.Register(app, error) && app.Register(module, error) &&
              app.Activate(EDITOR_IMGUI_VERSION, error, defaults),
          error.c_str());
    const auto sync = [&] { app.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    sync();
    TestSetupProjections(doc, app);
    TestSetupPages(doc, app);
    sync();
    auto owner = app.OpenScope();
    auto scope = app.Scope(owner);
    const auto create = [&](CK_CLASSID cid) {
        auto *o = doc.Context()->CreateObject(cid, "Setup editing");
        Check(o, "create Setup object");
        doc.Include(o->GetID());
        return o;
    };
    auto *camera = CKCamera::Cast(create(CKCID_CAMERA));
    auto *material = CKMaterial::Cast(create(CKCID_MATERIAL));
    auto *text = CKSpriteText::Cast(create(CKCID_SPRITETEXT));
    auto *mesh = CKMesh::Cast(create(CKCID_MESH));
    auto *otherMesh = CKMesh::Cast(create(CKCID_MESH));
    auto *entity = CK3dEntity::Cast(create(CKCID_3DENTITY));
    entity->AddMesh(mesh); // Setup chooses the current mesh from this list.
    auto *scene = InitialConditionsScene(doc.Context());
    Check(scene, "create scene");
    auto *grid = CKGrid::Cast(create(CKCID_GRID));
    grid->SetDimensions(4, 4, 4, 4);
    auto layerType = RegisterGridLayerType(doc, "Setup type");
    auto secondType = RegisterGridLayerType(doc, "Setup other type");
    Check(layerType.ok && secondType.ok && AddGridLayer(doc, grid->GetID(), int(layerType.created)).ok &&
              AddGridLayer(doc, grid->GetID(), int(secondType.created)).ok,
          "create Grid layers");
    sync();
    const auto saved = [&] {
        doc.History().Clear();
        doc.MarkSaved();
        doc.History().MarkSavePoint();
        sync();
    };
    saved();
    const auto count = [&] { return doc.History().UndoCount(); };
    EditorEdit edit = 0;
    std::string activeProperty;
    const auto begin = [&](const char *id, CKObject *object, std::vector<EditorField> context = {}) {
        activeProperty = id;
        context.insert(context.begin(), {"target", EDITOR_OBJECT, doc.Reference(object->GetID())});
        const auto values = BorrowEditorFields(context);
        return app.BeginEdit("setup." + std::string(id), {scope, values.data(), uint32_t(values.size())}, edit);
    };
    const auto update = [&](EditorField field) {
        const std::vector<EditorField> fields{std::move(field)};
        const auto values = BorrowEditorFields(fields);
        return app.UpdateEdit(edit, values.values.front());
    };
    const auto accept = [&] {
        Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "submit Setup draft");
        app.EndFrame();
        sync();
    };
    const auto cancel = [&] {
        Check(app.FinishEdit(edit, false) == EDITOR_CANCELLED, ("cancel Setup draft: " + activeProperty).c_str());
    };
    const float fov = camera->GetFov();
    Check(begin("camera.fov", camera) == EDITOR_OK, "begin camera");
    for (int i = 1; i <= 40; ++i)
        Check(update(Real(fov + i * .001)) == EDITOR_OK, "preview camera");
    Check(camera->GetFov() != fov && count() == 0 && !doc.Dirty(), "preview wrote history or dirty state");
    accept();
    Check(count() == 1 && doc.History().Undo() && camera->GetFov() == fov, "camera interaction not one undo");
    sync();
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .2)) == EDITOR_OK, "begin cancellation");
    cancel();
    Check(camera->GetFov() == fov && count() == 0 && doc.History().CanRedo() && !doc.Dirty(),
          "cancel lost native value or redo");
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov)) == EDITOR_OK, "unchanged camera");
    accept();
    Check(count() == 0 && doc.History().CanRedo(), "unchanged preview lost redo");
    // A registered window can finish one control and start another in the same
    // draw. Re-read the next property's metadata after the first queued commit.
    app.EnterDraw();
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .2)) == EDITOR_OK &&
              app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "queue first control");
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .4)) == EDITOR_OK &&
              app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "queue next one-shot control");
    Check(camera->GetFov() == fov && count() == 0, "queued controls mutated during drawing");
    app.LeaveDraw();
    app.EndFrame();
    sync();
    Check(std::fabs(camera->GetFov() - (fov + .4)) < 1e-5 && count() == 2 && doc.History().Undo() &&
              std::fabs(camera->GetFov() - (fov + .2)) < 1e-5 && doc.History().Undo() && camera->GetFov() == fov,
          "same-frame controls lost ordering or merged undo steps");
    app.EnterDraw();
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .2)) == EDITOR_OK &&
              app.FinishEdit(edit, true) == EDITOR_QUEUED,
          "queue before drag");
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .3)) == EDITOR_OK, "start next drag");
    const EditorValue blockedTarget = ObjectArgument("target", doc.Reference(camera->GetID()));
    Check(app.Execute("setup.reset-world-matrix", {scope, &blockedTarget, 1}).status == EDITOR_REFUSED,
          "staged drag admitted another native action");
    app.LeaveDraw();
    app.EndFrame();
    sync();
    Check(count() == 1 && std::fabs(camera->GetFov() - (fov + .3)) < 1e-5, "next drag preview was lost");
    cancel();
    Check(std::fabs(camera->GetFov() - (fov + .2)) < 1e-5 && doc.History().Undo() && camera->GetFov() == fov,
          "cancelled next drag undid the preceding control");
    app.EnterDraw();
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .2)) == EDITOR_OK &&
              app.FinishEdit(edit, true) == EDITOR_QUEUED && begin("camera.fov", camera) == EDITOR_OK &&
              update(Real(fov + .3)) == EDITOR_OK,
          "stage closing controls");
    app.CloseScope(owner);
    app.LeaveDraw();
    app.EndFrame();
    Check(count() == 0 && camera->GetFov() == fov && !app.HasEdits(), "closed view committed deferred controls");
    owner = app.OpenScope();
    scope = app.Scope(owner);
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .4)) == EDITOR_OK, "close owner preview");
    app.CloseScope(owner);
    Check(camera->GetFov() == fov && !app.HasEdits(), "closing owner did not restore preview");
    owner = app.OpenScope();
    scope = app.Scope(owner);
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .3)) == EDITOR_OK, "invalid update preview");
    Check(update(Real(std::numeric_limits<double>::quiet_NaN())) == EDITOR_INVALID && app.HasEdits(),
          "malformed value discarded valid draft");
    cancel();
    Check(camera->GetFov() == fov, "invalid input cancellation");
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .3)) == EDITOR_OK &&
              update(Real(std::numeric_limits<double>::max())) == EDITOR_ERROR && camera->GetFov() == fov &&
              !app.HasEdits(),
          "failed preview left partial value");
    saved();
    Check(begin("material.effect", material) == EDITOR_OK && update(Int(1)) == EDITOR_OK, "material effect preview");
    accept();
    Check(count() == 1 && doc.History().Undo() && material->GetEffect() == VXEFFECT_NONE && doc.History().Redo() &&
              int(material->GetEffect()) == 1,
          "owned effect parameter lifetime blocked replay");
    sync();
    saved();
    auto *effect = material->GetEffectParameter();
    Check(effect && effect->GetGUID() == CKPGUID_TEXGENEFFECT, "effect parameter fixture");
    int previous = 0;
    effect->GetValue(&previous);
    const int changed = previous == 2 ? 3 : 2;
    Check(EditObjectState(doc, material->GetID(), "Effect options",
                          [&](CKObject *o) { CKMaterial::Cast(o)->GetEffectParameter()->SetValue(&changed); })
              .ok,
          "effect parameter value edit");
    Check(doc.History().Undo(), "effect parameter value Undo");
    int effectValue = 0;
    material->GetEffectParameter()->GetValue(&effectValue);
    Check(effectValue == previous, "effect parameter value not restored");
    Check(doc.History().Redo(), "effect parameter value Redo");
    material->GetEffectParameter()->GetValue(&effectValue);
    Check(effectValue == changed, "effect parameter value lost on Redo");
    saved();
    Check(begin("material.effect", material) == EDITOR_OK && update(Int(0)) == EDITOR_OK, "effect cancellation");
    cancel();
    material->GetEffectParameter()->GetValue(&effectValue);
    Check(int(material->GetEffect()) == 1 && effectValue == changed && count() == 0,
          "effect cancellation lost owned parameter data");
    effect = material->GetEffectParameter();
    ParameterValueDescription effectDescription;
    Check(DescribeParameterValue(doc, effect, effectDescription), "describe owned effect value");
    const auto oldEffectParameter = doc.Reference(effect->GetID());
    const auto oldEffectValue = effectDescription.value;
    auto newEffectValue = oldEffectValue;
    newEffectValue.integer = oldEffectValue.integer == 2 ? 3 : 2;
    Check(SetParameterValue(doc, oldEffectParameter, effect->GetGUID(), newEffectValue).ok,
          "owned effect parameter command");
    sync();
    Check(begin("material.effect", material) == EDITOR_OK && update(Int(0)) == EDITOR_OK, "remove edited effect");
    accept();
    Check(count() == 2 && doc.History().Undo() && !doc.Resolve(oldEffectParameter),
          "effect parameter was not recreated");
    Check(doc.History().Undo(), "parameter Undo lost its material target");
    Check(DescribeParameterValue(doc, material->GetEffectParameter(), effectDescription) &&
              effectDescription.value == oldEffectValue,
          "owned parameter history restored wrong value");
    Check(doc.History().Redo() && doc.History().Redo() && material->GetEffect() == VXEFFECT_NONE,
          "owned parameter/effect Redo sequence");
    sync();
    saved();
    const VxColor color = material->GetDiffuse();
    Check(begin("material.diffuse", material) == EDITOR_OK && update(Numbers({.2, .4, .6, color.a})) == EDITOR_OK,
          "preview color");
    cancel();
    Check(material->GetDiffuse().GetRGBA() == color.GetRGBA(), "cancel color");
    const auto transform = entity->GetWorldMatrix();
    Check(begin("entity3d.world-position", entity) == EDITOR_OK && update(Numbers({4, 5, 6})) == EDITOR_OK,
          "preview transform");
    cancel();
    Check(std::memcmp(&transform, &entity->GetWorldMatrix(), sizeof(VxMatrix)) == 0, "cancel transform lost matrix");
    Check(begin("entity3d.current-mesh", entity) == EDITOR_OK &&
              update({"value", EDITOR_OBJECT, doc.Reference(mesh->GetID())}) == EDITOR_OK,
          "preview mesh reference");
    accept();
    Check(entity->GetCurrentMesh() == mesh && count() == 1 && doc.History().Undo() && !entity->GetCurrentMesh() &&
              doc.History().Redo() && entity->GetCurrentMesh() == mesh,
          "reference undo");
    sync();
    saved();
    const int meshesBefore = entity->GetMeshCount();
    Check(begin("entity3d.meshes", entity) == EDITOR_OK, "begin mesh list");
    EditorField list{"value", EDITOR_OBJECT_LIST};
    list.objects = {doc.Reference(mesh->GetID()), doc.Reference(otherMesh->GetID())};
    Check(update(list) == EDITOR_OK, "preview mesh list");
    cancel();
    if (entity->GetMeshCount() != meshesBefore || entity->GetCurrentMesh() != mesh)
        throw std::runtime_error("cancel mesh list: before=" + std::to_string(meshesBefore) +
                                 " after=" + std::to_string(entity->GetMeshCount()) + " current=" +
                                 std::to_string(entity->GetCurrentMesh() ? entity->GetCurrentMesh()->GetID() : 0) +
                                 " expected=" + std::to_string(mesh->GetID()));
    SpriteTextFont font;
    ReadSpriteTextFont(text, &font);
    auto fontValue = EditorField{"value", EDITOR_RECORD};
    fontValue.fields = {{"face", EDITOR_TEXT_VALUE, {}, "Arial"},
                        {"size", EDITOR_INTEGER_VALUE, {}, {}, 25},
                        {"weight", EDITOR_INTEGER_VALUE, {}, {}, 700},
                        {"italic", EDITOR_INTEGER_VALUE, {}, {}, 1},
                        {"underline", EDITOR_INTEGER_VALUE, {}, {}, 0}};
    Check(begin("spritetext.font", text) == EDITOR_OK && update(fontValue) == EDITOR_OK, "preview font");
    cancel();
    SpriteTextFont restored;
    ReadSpriteTextFont(text, &restored);
    Check(restored.face == font.face && restored.size == font.size && restored.weight == font.weight &&
              restored.italic == font.italic,
          "cancel font");
    Check(begin("spritetext.text", text) == EDITOR_OK &&
              update({"value", EDITOR_TEXT_VALUE, {}, std::string(4096, 'x')}) == EDITOR_OK,
          "long sprite text");
    accept();
    Check(count() == 1 && doc.History().Undo(), "long text undo");
    sync();
    saved();
    const auto environment = ReadSceneEnvironment(scene);
    Check(begin("scene.fog-start", scene) == EDITOR_OK && update(Real(environment.fogStart + 50)) == EDITOR_OK,
          "scene preview");
    cancel();
    Check(ReadSceneEnvironment(scene) == environment, "cancel scene environment");
    Check(begin("scene.background", scene) == EDITOR_OK && update(Numbers({.3, .5, .7, 1})) == EDITOR_OK,
          "scene color");
    accept();
    Check(count() == 1 && doc.History().Undo() && ReadSceneEnvironment(scene) == environment, "scene environment undo");
    sync();
    saved();
    auto *background = CKTexture::Cast(create(CKCID_TEXTURE));
    scene->SetBackgroundTexture(background);
    Check(begin("scene.fog-start", scene) == EDITOR_OK && update(Real(environment.fogStart + 1)) == EDITOR_OK,
          "scene dependency lifetime preview");
    doc.Context()->DestroyObject(background, CK_DESTROY_NONOTIFY);
    scene->SetBackgroundTexture(nullptr);
    create(CKCID_TEXTURE);
    Check(app.FinishEdit(edit, false) == EDITOR_ERROR && scene->GetBackgroundTexture() == nullptr &&
              !doc.History().CanUndo(),
          "scene cancellation rebound a retired dependency");
    ApplySceneEnvironment(doc.Context(), scene, environment);
    saved();
    auto *first = GridGetLayer(grid, int(layerType.created)), *second = GridGetLayer(grid, int(secondType.created));
    Check(first && second, "Grid layers");
    EditorField targets{"targets", EDITOR_OBJECT_LIST};
    targets.objects = {doc.Reference(first->GetID()), doc.Reference(second->GetID())};
    EditorField cells{"value", EDITOR_RECORD};
    cells.fields = {{"cells", EDITOR_INTEGER_LIST}};
    cells.fields.front().integers = {0, 0, 100, 1, 0, 200};
    Check(begin("layer.cells", first, {targets}) == EDITOR_OK && update(cells) == EDITOR_OK, "multi-layer stroke");
    int a = 0, b = 0;
    first->GetValue2(1, 0, &a);
    second->GetValue2(1, 0, &b);
    Check(a == 200 && b == 200 && count() == 0, "Grid stroke preview");
    accept();
    Check(count() == 1 && doc.History().Undo(), "multi-layer stroke not one undo");
    first->GetValue2(1, 0, &a);
    second->GetValue2(1, 0, &b);
    Check(a == 0 && b == 0, "Grid Undo lost layer values");
    sync();
    saved();
    Check(begin("layer.cells", first, {targets}) == EDITOR_OK && update(cells) == EDITOR_OK, "Grid cancellation");
    cancel();
    first->GetValue2(0, 0, &a);
    second->GetValue2(0, 0, &b);
    Check(a == 0 && b == 0 && count() == 0, "Grid cancellation left layer values");
    EditorField indices{"indices", EDITOR_INTEGER_LIST};
    indices.integers = {uint64_t(layerType.created)};
    GridPropertyValue old;
    Check(ReadGridProperty(doc, GridProperty::LayerColor, int(layerType.created), old), "read layer color");
    Check(begin("grid.layer-color", grid, {indices}) == EDITOR_OK && update(Numbers({.1, .2, .3, 1})) == EDITOR_OK,
          "layer type color");
    cancel();
    GridPropertyValue now;
    ReadGridProperty(doc, GridProperty::LayerColor, int(layerType.created), now);
    Check(now.color.GetRGBA() == old.color.GetRGBA() && count() == 0, "cancel layer color");
    Check(begin("grid.layer-name", grid, {indices}) == EDITOR_OK &&
              update({"value", EDITOR_TEXT_VALUE, {}, "Renamed Setup type"}) == EDITOR_OK,
          "rename layer type");
    accept();
    Check(count() == 1 && doc.History().Undo(), "layer name undo");
    ReadGridProperty(doc, GridProperty::LayerName, int(layerType.created), now);
    Check(now.text == "Setup type", "layer name not restored");
    sync();
    saved();
    // Indexed channel and image settings use the same preview/commit path.
    Check(mesh->AddChannel(material, TRUE) >= 0, "create mesh channel");
    EditorField channel{"indices", EDITOR_INTEGER_LIST};
    channel.integers = {0};
    const bool lit = mesh->IsChannelLit(0) != FALSE;
    Check(begin("mesh.channel-lit", mesh, {channel}) == EDITOR_OK && update(Int(!lit)) == EDITOR_OK, "channel preview");
    cancel();
    Check((mesh->IsChannelLit(0) != FALSE) == lit, "cancel channel flag");
    Check(begin("mesh.channels", mesh) == EDITOR_OK, "begin remove channel");
    EditorField noChannels{"value", EDITOR_OBJECT_LIST};
    Check(update(noChannels) == EDITOR_OK, "remove channel preview");
    cancel();
    Check(mesh->GetChannelCount() == 1 && mesh->GetChannelMaterial(0) == material &&
              (mesh->IsChannelLit(0) != FALSE) == lit,
          "cancel channel collection");
    auto *queuedTexture = CKTexture::Cast(create(CKCID_TEXTURE));
    Check(queuedTexture && queuedTexture->Create(2, 2, 32), "create queued image fixture");
    queuedTexture->SetPixel(0, 0, 0xff102030);
    const auto queuedTarget = doc.Reference(queuedTexture->GetID());
    const bool wasTransparent = queuedTexture->IsTransparent();
    saved();
    app.EnterDraw();
    Check(begin("texture.transparent", queuedTexture) == EDITOR_OK && update(Int(!wasTransparent)) == EDITOR_OK,
          "preview before indexed action");
    EditorOperation beforeAction = 0;
    Check(app.FinishEdit(edit, true, &beforeAction) == EDITOR_QUEUED && beforeAction, "queue indexed action's edit");
    const EditorValue indexedArgs[] = {ObjectArgument("target", queuedTarget), IntegerArgument("slot", 0),
                                       IntegerArgument("document-revision", doc.ChangeRevision())};
    std::string revisionField = "document-revision";
    EditorRequest indexedRequest{scope, indexedArgs, 3, {}, 0, beforeAction, EditorText(revisionField)};
    auto otherScope = app.OpenScope();
    indexedRequest.scope = app.Scope(otherScope);
    ++indexedRequest.scope.document.value;
    Check(app.Execute("setup.slot-invert", indexedRequest).status == EDITOR_STALE, "edit dependency crossed documents");
    indexedRequest.scope = app.Scope(otherScope);
    const auto indexedAction = app.Execute("setup.slot-invert", indexedRequest);
    Check(indexedAction.status == EDITOR_QUEUED, "queue another window's indexed action after edit");
    revisionField.assign("replaced after submission");
    app.LeaveDraw();
    app.EndFrame();
    sync();
    Check(count() == 2 && queuedTexture->GetPixel(0, 0) == 0xffefdfcf &&
              (queuedTexture->IsTransparent() != FALSE) != wasTransparent &&
              EditorClient(app.Host(), 0).Completion(indexedAction.operation).status == EDITOR_OK,
          "indexed action used the pre-commit document revision");
    app.CloseScope(otherScope);
    Check(doc.History().Undo() && queuedTexture->GetPixel(0, 0) == 0xff102030 && doc.History().Undo() &&
              (queuedTexture->IsTransparent() != FALSE) == wasTransparent,
          "dependent action and property lost separate undo steps");
    saved();
    app.EnterDraw();
    Check(begin("texture.transparent", queuedTexture) == EDITOR_OK && update(Int(!wasTransparent)) == EDITOR_OK &&
              app.FinishEdit(edit, true, &beforeAction) == EDITOR_QUEUED,
          "edit before delayed image input");
    app.LeaveDraw();
    app.EndFrame();
    sync();
    const EditorValue delayedArgs[] = {ObjectArgument("target", queuedTarget), IntegerArgument("slot", 0),
                                       IntegerArgument("document-revision", 0)};
    const EditorClient sourceClient(app.Host(), owner);
    Check(sourceClient.CommandAfterEdit(beforeAction, "setup.slot-invert", {delayedArgs, delayedArgs + 3},
                                        "document-revision")
                      .status == EDITOR_OK &&
              count() == 2,
          "delayed image input could not use its completed edit revision");
    Check(sourceClient.CommandAfterEdit(beforeAction, "setup.slot-invert", {delayedArgs, delayedArgs + 3},
                                        "document-revision")
                      .status == EDITOR_STALE &&
              count() == 2,
          "delayed action ignored a change after its source edit");
    Check(doc.History().Undo() && doc.History().Undo(), "undo delayed image input");
    saved();
    auto *texture = CKTexture::Cast(create(CKCID_TEXTURE));
    auto format = MakeSaveFormat(doc.Context(), "bmp");
    Check(!format.empty(), "bitmap format fixture");
    std::vector<char> legacyFormat(format.begin(), format.begin() + sizeof(CKBitmapProperties) - 4);
    reinterpret_cast<CKBitmapProperties *>(legacyFormat.data())->m_Size = int(legacyFormat.size());
    texture->SetSaveFormat(reinterpret_cast<CKBitmapProperties *>(legacyFormat.data()));
    sync();
    std::vector<std::pair<ObjectPropertyCheck, std::string>> imageBefore;
    for (auto check : ObjectPropertyChecks())
        if (CKIsChildClassOf(texture, check.cid))
            imageBefore.emplace_back(check, check.text(texture));
    const int mipBefore = texture->GetMipmapCount();
    Check(begin("texture.mipmap", texture) == EDITOR_OK && update(Int(-1)) == EDITOR_OK, "automatic mipmap preview");
    cancel();
    Check(texture->GetMipmapCount() == mipBefore, "automatic mipmap cancellation");
    Check(begin("texture.mipmap", texture) == EDITOR_OK && update(Int(-1)) == EDITOR_OK, "automatic mipmap accept");
    accept();
    Check(count() == 1 && texture->GetMipmapCount() == -1 && doc.History().Undo() &&
              texture->GetMipmapCount() == mipBefore,
          "automatic mipmap Undo");
    sync();
    saved();
    const int emptySlot = texture->GetCurrentSlot();
    const bool transparent = texture->IsTransparent() != FALSE;
    Check(begin("texture.transparent", texture) == EDITOR_OK && update(Int(!transparent)) == EDITOR_OK,
          "empty bitmap flag preview");
    cancel();
    Check(texture->GetCurrentSlot() == emptySlot && (texture->IsTransparent() != FALSE) == transparent && count() == 0,
          "empty bitmap cancellation normalized unrelated state");
    Check(begin("texture.transparent", texture) == EDITOR_OK && update(Int(!transparent)) == EDITOR_OK,
          "empty bitmap flag accept");
    accept();
    Check(count() == 1 && doc.History().Undo() && texture->GetCurrentSlot() == emptySlot &&
              (texture->IsTransparent() != FALSE) == transparent,
          "empty bitmap value Undo");
    sync();
    saved();
    Check(begin("bitmap.save-format", texture) == EDITOR_OK &&
              update({"value", EDITOR_TEXT_VALUE, {}, "tga"}) == EDITOR_OK,
          "image format preview");
    const auto cancelledFormat = app.FinishEdit(edit, false);
    for (const auto &[check, original] : imageBefore)
        if (!check.same(original, check.text(texture)))
            throw std::runtime_error(std::string("image cancellation changed ") + check.label + ": " + original +
                                     " -> " + check.text(texture));
    Check(cancelledFormat == EDITOR_CANCELLED, "image cancel status");
    Check(std::string(texture->GetSaveFormat()->m_Ext.m_Data) == "bmp", "cancel image format");
    Check(begin("bitmap.save-format", texture) == EDITOR_OK &&
              update({"value", EDITOR_TEXT_VALUE, {}, "nosuchformat"}) == EDITOR_ERROR &&
              std::string(texture->GetSaveFormat()->m_Ext.m_Data) == "bmp",
          "invalid image format rollback");
    Check(begin("bitmap.save-format", texture) == EDITOR_OK &&
              update({"value", EDITOR_TEXT_VALUE, {}, "tga"}) == EDITOR_OK,
          "accept image format");
    accept();
    Check(count() == 1 && doc.History().Undo() && std::string(texture->GetSaveFormat()->m_Ext.m_Data) == "bmp",
          "image format undo");
    sync();
    saved();
    Check(!ReadBitmapOptions(doc, texture), "legacy format options read beyond their stored header");
    texture->SetSaveFormat(reinterpret_cast<CKBitmapProperties *>(format.data()));
    saved();
    auto *options = ReadBitmapOptions(doc, texture);
    Check(options, "read bitmap reader options");
    std::vector<ParameterMember> members;
    Check(ParameterMembers(doc, options, members) && !members.empty(), "bitmap option members");
    auto *depth = CKParameter::Cast(doc.Resolve(members.front().parameter));
    int bits = 0;
    depth->GetValue(&bits);
    const int otherBits = bits == 24 ? 32 : 24;
    Check(depth->SetValue(&otherBits) == CK_OK, "bitmap option working value");
    BitmapFormat formatBefore;
    Check(ReadBitmapFormat(texture, formatBefore), "capture reader options");
    const EditorField optionValue{"value", EDITOR_OBJECT, doc.Reference(options->GetID())};
    Check(begin("bitmap.format-options", texture) == EDITOR_OK && update(optionValue) == EDITOR_OK,
          "format option preview");
    cancel();
    BitmapFormat formatAfter;
    ReadBitmapFormat(texture, formatAfter);
    Check(formatAfter.bytes == formatBefore.bytes && count() == 0, "cancel format options");
    Check(begin("bitmap.format-options", texture) == EDITOR_OK && update(optionValue) == EDITOR_OK,
          "format option accept");
    accept();
    Check(count() == 1 && doc.History().Undo(), "format options Undo");
    ReadBitmapFormat(texture, formatAfter);
    Check(formatAfter.bytes == formatBefore.bytes, "format option bytes not restored");
    sync();
    saved();
    auto *wrongOptions = doc.Context()->CreateCKParameterOut("Wrong reader options", CKPGUID_INT);
    Check(wrongOptions, "invalid format option fixture");
    Check(begin("bitmap.format-options", texture) == EDITOR_OK &&
              update({"value", EDITOR_OBJECT, doc.Reference(wrongOptions->GetID())}) == EDITOR_ERROR,
          "format options accepted wrong parameter type");
    ReadBitmapFormat(texture, formatAfter);
    Check(formatAfter.bytes == formatBefore.bytes, "rejected options changed image settings");
    BitmapFormat truncated = formatBefore;
    truncated.bytes.resize(sizeof(CKBitmapProperties));
    reinterpret_cast<CKBitmapProperties *>(truncated.bytes.data())->m_Size = int(truncated.bytes.size());
    Check(ApplyBitmapFormat(texture, truncated), "truncated options fixture");
    Check(!ReadBitmapOptions(doc, texture) && !WriteBitmapOptions(doc, texture, options),
          "format options exceeded stored byte count");
    Check(ApplyBitmapFormat(texture, formatBefore), "restore format fixture");
    doc.Context()->DestroyObject(options);
    doc.Context()->DestroyObject(wrongOptions);
    saved();
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .2)) == EDITOR_OK, "busy preview");
    app.NativeAvailable(false);
    Check(update(Real(fov + .3)) == EDITOR_REFUSED && app.HasEdits() && camera->GetFov() == float(fov + .2),
          "busy update changed or discarded preview");
    app.NativeAvailable(true);
    cancel();
    Check(camera->GetFov() == fov, "resume cancellation");
    // Position metadata is captured once and invalidated by topology changes.
    Check(begin("layer.cells", first, {targets}) == EDITOR_OK && update(cells) == EDITOR_OK, "stale Grid preview");
    doc.MarkTopologyChanged();
    sync();
    Check(app.FinishEdit(edit, true) == EDITOR_STALE, "stale Grid draft committed");
    first->GetValue2(0, 0, &a);
    second->GetValue2(0, 0, &b);
    Check(a == 0 && b == 0 && count() == 0, "stale Grid draft not restored");
    saved();
    auto custom = defaults;
    for (auto &provider : custom.providers)
        if (provider.id == "setup.camera.fov-value" || provider.id == "setup.camera.fov-set")
            provider.provider = "test.setup";
    Check(app.Configure(custom, error), error.c_str());
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .5)) == EDITOR_OK && camera->GetFov() == fov,
          "replacement inherited builtin preview");
    Check(!app.Configure(defaults, error), "changed provider during active preview");
    accept();
    SetupDetails selectedProperties;
    Check(ReadSetupProperties(EditorClient(app.Host(), 0), doc.Reference(camera->GetID()), {"camera.fov"},
                              selectedProperties) &&
              std::abs(selectedProperties.Values().Real("camera.fov") - camera->GetFov()) < 1e-6,
          "batched property query bypassed selected provider");
    Check(replacement.reads == 2 && replacement.writes == 1 && count() == 1 && doc.History().Undo() &&
              camera->GetFov() == fov,
          "replacement bypassed selected command or added multiple Undo steps");
    sync();
    Check(app.Configure(defaults, error), error.c_str());
    saved();
    // Framework cleanup uses the same registered draft, including native failures.
    auto *object = new vdevtest::PropertyTestObject(doc.Context());
    doc.Include(object->GetID());
    const auto ref = doc.Reference(object->GetID());
    const auto failureBegin = [&] { return app.BeginEdit("test.preview", scope, ref, edit); };
    object->failSave = object->saves + 1;
    Check(failureBegin() == EDITOR_OK && update(Int(10)) == EDITOR_ERROR && object->writes == 0 && !app.HasEdits(),
          "failed capture allowed writes");
    Check(failureBegin() == EDITOR_OK && update(Int(20)) == EDITOR_OK, "begin failing commit");
    object->failSave = object->saves + 2;
    accept();
    Check(object->value == 0 && count() == 0 && !doc.Dirty(), "failed final capture kept preview");
    failure.fail = true;
    Check(failureBegin() == EDITOR_OK && update(Int(30)) == EDITOR_ERROR && object->value == 0 && !app.HasEdits(),
          "throwing preview not restored");
    failure.fail = false;
    Check(failureBegin() == EDITOR_OK && update(Int(40)) == EDITOR_OK, "failure cancel");
    cancel();
    Check(object->value == 0 && failure.captures == failure.cancels, "preview state leaked or restored twice");
    Check(failureBegin() == EDITOR_OK && update(Int(50)) == EDITOR_OK, "failed recovery");
    object->failLoad = object->loads + 1;
    Check(app.FinishEdit(edit, false) == EDITOR_ERROR && object->value == -100 && doc.Dirty() &&
              !doc.History().CanUndo(),
          "incomplete recovery not reported");
    object->value = 0;
    saved();
    Check(begin("camera.fov", camera) == EDITOR_OK, "wrong-thread preview");
    EditorStatus threadStatus = EDITOR_OK;
    std::thread worker([&] {
        const EditorValue value{EDITOR_TEXT("value"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, fov + .1};
        threadStatus = app.UpdateEdit(edit, value);
    });
    worker.join();
    Check(threadStatus == EDITOR_REFUSED && camera->GetFov() == fov, "preview touched runtime from another thread");
    cancel();
    auto *retired = CKCamera::Cast(create(CKCID_CAMERA));
    const auto retiredRef = doc.Reference(retired->GetID());
    Check(begin("camera.fov", retired) == EDITOR_OK && update(Real(fov + .2)) == EDITOR_OK, "retired target preview");
    doc.Context()->DestroyObject(retired, CK_DESTROY_NONOTIFY);
    auto *replacementCamera = CKCamera::Cast(create(CKCID_CAMERA));
    const float replacementFov = replacementCamera->GetFov();
    sync();
    Check(!app.HasEdits() && !doc.Resolve(retiredRef) && replacementCamera->GetFov() == replacementFov,
          "retired draft wrote another lifetime");
    saved();
    Check(begin("camera.fov", camera) == EDITOR_OK && update(Real(fov + .1)) == EDITOR_OK,
          "document replacement preview");
    Check(doc.New(&error), error.c_str());
    sync();
    scope = app.Scope(owner);
    Check(!app.HasEdits(), "document replacement kept preview data");
    auto *newCamera = CKCamera::Cast(create(CKCID_CAMERA));
    const float newFov = newCamera->GetFov();
    Check(begin("camera.fov", newCamera) == EDITOR_OK && update(Real(newFov + .1)) == EDITOR_OK, "stop preview");
    app.Stop();
    Check(newCamera->GetFov() == newFov && !doc.Dirty(), "stop did not restore preview");
}

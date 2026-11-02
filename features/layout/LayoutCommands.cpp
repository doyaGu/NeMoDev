#include "features/layout/LayoutCommands.h"
#include "core/catalog/ClassInfo.h"
#include "features/layout/LayoutSceneData.h"
#include "features/layout/LayoutGestures.h"
#include "application/ObjectSelection.h"
#include "core/document/Document.h"
#include "core/layout/LayoutCommands.h"
#include "core/layout/LayoutExplorer.h"
#include "core/layout/LayoutTools.h"
#include "core/layout/LayoutView.h"
#include "core/level/SceneCommands.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <tuple>

namespace vdev {
namespace {
const EditorValue *Arg(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return request.values + i;
    return nullptr;
}
uint64_t Number(const EditorRequest &request, const char *key, uint64_t fallback = 0) {
    const auto *value = Arg(request, key); return value ? value->integer : fallback;
}
std::string Text(const EditorRequest &request, const char *key) {
    const auto *value = Arg(request, key); return value ? std::string(value->text.data, value->text.size) : std::string();
}
ObjectRef Object(const EditorRequest &request, const char *key) {
    const auto *value = Arg(request, key); return value ? value->object : ObjectRef{};
}
std::string Lower(std::string value) {
    for (char &c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
EditorField Row(Document &doc, CKObject *object, const char *kind = "", bool includeScripts = false) {
    EditorField row{"", EDITOR_RECORD};
    EditorField scripts{"scripts", EDITOR_RECORD};
    if (auto *owner = includeScripts ? CKBeObject::Cast(object) : nullptr)
        for (int i = 0; i < owner->GetScriptCount(); ++i)
            if (auto *script = owner->GetScript(i)) {
                EditorField scriptRow{std::to_string(i), EDITOR_RECORD};
                scriptRow.fields = {{"object", EDITOR_OBJECT, doc.Reference(script->GetID())},
                    {"name", EDITOR_TEXT_VALUE, {}, DevObjectName(script)}};
                scripts.fields.push_back(std::move(scriptRow));
            }
    row.fields = {{"object", EDITOR_OBJECT, doc.Reference(object->GetID())},
        {"name", EDITOR_TEXT_VALUE, {}, DevObjectName(object)}, {"class", EDITOR_TEXT_VALUE, {}, DevClassName(object)},
        {"cid", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(object->GetClassID())},
        {"line-icon", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(ClassIconKindOf(object->GetClassID()))},
        {"kind", EDITOR_TEXT_VALUE, {}, kind}, std::move(scripts),
        {"is-curve", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(CKCurve::Cast(object) != nullptr)}};
    return row;
}
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
EditorStatus Result(Document &doc, const EditResult &result, EditorSink sink) {
    const std::vector<EditorField> fields{{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(result.message)},
        {"created", EDITOR_OBJECT, doc.Reference(result.created)}};
    if (sink.write) { const auto status = Write(sink, fields); if (status != EDITOR_OK) return status; }
    return result.Unchanged() ? EDITOR_NO_CHANGE : result.ok ? EDITOR_OK : result.Status() == EditStatus::Refused ? EDITOR_REFUSED : EDITOR_ERROR;
}
} // namespace

struct LayoutCommands::Data {
    EditorApplication &editor;
    std::function<Document *()> current;
    std::function<LayoutView &()> view;
    ObjectSelection &selection;
    LayoutGestures gestures;
    using QueryState = std::tuple<DocumentToken, uint64_t, uint64_t, unsigned, bool, ObjectRef, ObjectRef, CKDWORD>;
    QueryState queryState{};
    uint64_t queryRevision = 1;
    enum Action { Camera, ResetCamera, FrameRepresentation, CreateGroup, ChooseGroup, RestoreInterface, Copy };
    struct Entry { Data *owner; Action action; } entries[7];
    Data(EditorApplication &editor, std::function<Document *()> current, std::function<LayoutView &()> view,
         ObjectSelection &selection)
        : editor(editor), current(std::move(current)), view(std::move(view)), selection(selection),
          gestures(this->current, this->view, selection),
          entries{{this, Camera},      {this, ResetCamera}, {this, FrameRepresentation},
                  {this, CreateGroup}, {this, ChooseGroup}, {this, RestoreInterface},
                  {this, Copy}} {}
    Document *DocumentOf(const EditorRequest &request) const {
        auto *doc = current();
        return doc && doc->IsOpen() && doc->Token() == request.scope.document ? doc : nullptr;
    }
    uint64_t Version(Document &doc, LayoutView &view) {
        auto *scene = view.Scene(); auto *camera = view.Camera(); auto *time = doc.Context()->GetTimeManager();
        const QueryState next{doc.Token(), doc.ChangeRevision(), doc.TopologyRevision(), selection.Revision(), selection.Locked(),
            scene ? doc.Reference(scene->GetID()) : ObjectRef{}, camera ? doc.Reference(camera->GetID()) : ObjectRef{},
            time ? time->GetMainTickCount() : 0};
        if (next != queryState) { queryState = next; ++queryRevision; }
        return queryRevision;
    }
    static EditorStatus Read(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request);
        if (!doc) return EDITOR_STALE;
        auto &view = self.view(); auto *context = doc->Context(); auto *scene = view.Scene();
        const auto section = Text(*request, "section");
        if (section != "groups" && section != "cameras" && section != "context" && section != "explorer") return EDITOR_INVALID;
        if (Number(*request, "shown", EXPLORER_ALL) > EXPLORER_ALL) return EDITOR_INVALID;
        const auto revision = self.Version(*doc, view);
        if (const auto expected = Number(*request, "revision"); expected && expected != revision) return EDITOR_STALE;
        std::vector<EditorField> fields{{"scene", EDITOR_OBJECT, scene ? doc->Reference(scene->GetID()) : ObjectRef{}},
            {"camera", EDITOR_OBJECT, view.Camera() ? doc->Reference(view.Camera()->GetID()) : ObjectRef{}},
            {"saved-camera", EDITOR_OBJECT}, {"frame", EDITOR_OBJECT}, {"initial-target", EDITOR_OBJECT},
            {"initial", EDITOR_INTEGER_VALUE}, {"matching-group", EDITOR_OBJECT}, {"selected-types", EDITOR_INTEGER_VALUE},
            {"revision", EDITOR_INTEGER_VALUE, {}, {}, revision}, {"rows", EDITOR_RECORD}};
        if (view.Camera() && !view.IsViewCamera(view.Camera()->GetID())) fields[2].object = doc->Reference(view.Camera()->GetID());
        auto &rows = fields.back().fields;
        if (section == "groups") {
            const auto groups = SceneGroups(context, scene);
            fields[6].object = doc->Reference(MatchingGroup(context, groups, self.selection.Objects()));
            for (auto id : groups) if (auto *group = CKGroup::Cast(context->GetObject(id)); group && !doc->IsDiscarded(id)) {
                auto row = Row(*doc, group);
                const auto ids = GroupSelection(context, id);
                row.fields.push_back({"members", EDITOR_OBJECT_LIST}); row.fields.back().objects = doc->References(ids);
                rows.push_back(std::move(row));
            }
        } else if (section == "cameras") {
            for (auto id : view.Cameras()) if (auto *camera = context->GetObject(id); camera && !doc->IsDiscarded(id)) {
                auto row = Row(*doc, camera);
                row.fields.push_back({"view", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(view.IsViewCamera(id))});
                rows.push_back(std::move(row));
            }
        } else if (section == "explorer") {
            const auto filter = Lower(Text(*request, "filter")); std::vector<CK_ID> selected;
            for (const auto &entry : ExplorerObjects(context, scene, unsigned(Number(*request, "shown", EXPLORER_ALL)))) {
                if (doc->IsDiscarded(entry.first) || view.IsInterfaceObject(entry.first)) continue;
                auto *object = context->GetObject(entry.first); if (!object) continue;
                if (!filter.empty() && Lower(DevObjectName(object)).find(filter) == std::string::npos) continue;
                auto row = Row(*doc, object);
                if (self.selection.Contains(entry.first)) selected.push_back(entry.first);
                rows.push_back(std::move(row));
            }
            std::stable_sort(rows.begin(), rows.end(), [](const EditorField &a, const EditorField &b) {
                return Lower(a.fields[1].text) < Lower(b.fields[1].text);
            });
            fields[7].integer = SelectedTypes(context, selected);
        } else if (scene) {
            const auto x = Number(*request, "x"), y = Number(*request, "y");
            if (x > uint64_t(std::numeric_limits<int>::max()) || y > uint64_t(std::numeric_limits<int>::max())) return EDITOR_INVALID;
            const auto picked = view.PickInfo(int(x), int(y));
            auto add = [&](CKObject *object, const char *kind = "") { if (object) rows.push_back(Row(*doc, object, kind, true)); };
            auto initial = [&](CKObject *object) {
                auto *initialScene = InitialConditionsScene(context);
                if (auto *sceneObject = CKSceneObject::Cast(object))
                    if (initialScene && initialScene->GetObjectInitialValue(sceneObject)) fields[5].integer = 1;
            };
            auto *sprite = context->GetObject(picked.entity2d), *entity = context->GetObject(picked.entity3d),
                 *character = context->GetObject(picked.character);
            if (sprite) { add(sprite); initial(sprite); }
            if (entity) {
                if (!character) initial(entity);
                if (auto *point = CKCurvePoint::Cast(entity)) add(point->GetCurve());
                else { add(entity); if (CKGrid::Cast(entity)) add(entity, "Grid 3D"); }
            }
            if (character) { add(character); initial(character); }
            auto *mesh = context->GetObject(picked.mesh);
            if (!mesh || !mesh->IsPrivate()) {
                add(mesh);
                for (auto id : {picked.material, picked.texture})
                    if (auto *object = context->GetObject(id); object && !object->IsPrivate()) add(object);
            }
            if (auto *placed = CK3dEntity::Cast(entity)) add(placed->GetReferencePlace());
            if (fields[2].object) add(view.Camera(), "Current Camera");
            if (auto *frame = CK3dEntity::Cast(entity)) if (frame->GetFlags() & CK_3DENTITY_FRAME) fields[3].object = doc->Reference(frame->GetID());
            if (!rows.empty()) {
                fields[4].object = rows.front().fields.front().object;
                if (CK3dEntity::Cast(doc->Resolve(fields[4].object)) && rows.size() > 1 &&
                    CKCharacter::Cast(doc->Resolve(rows[1].fields.front().object))) fields[4].object = rows[1].fields.front().object;
            }
        }
        const auto total = rows.size(); const auto first = Number(*request, "first");
        if (first > total) return EDITOR_INVALID;
        const auto end = std::min<uint64_t>(total, first + 128);
        std::vector<EditorField> page(rows.begin() + first, rows.begin() + end); rows = std::move(page);
        for (size_t i = 0; i < rows.size(); ++i) rows[i].key = std::to_string(first + i);
        fields.push_back({"total", EDITOR_INTEGER_VALUE, {}, {}, total}); fields.push_back({"next", EDITOR_INTEGER_VALUE, {}, {}, end});
        fields.push_back({"frame-shapes", EDITOR_TEXT_LIST});
        if (section == "context") fields.back().texts = view.FrameShapeNames();
        return Write(sink, fields);
    }
    static EditorStatus Run(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
        auto &entry = *static_cast<Entry *>(data); auto &self = *entry.owner; auto *doc = self.DocumentOf(*request);
        if (!doc) return EDITOR_STALE;
        auto &view = self.view();
        switch (entry.action) {
        case Copy: {
            const auto *source = Arg(*request, "objects"); if (!source) return EDITOR_INVALID;
            std::vector<ObjectRef> objects;
            for (uint32_t i = 0; i < source->objects.count; ++i) {
                auto *object = doc->Resolve(source->objects.data[i]); if (!object) return EDITOR_STALE;
                if (!CKCurvePoint::Cast(object)) objects.push_back(source->objects.data[i]);
            }
            if (objects.empty()) {
                const EditorRequest clear{request->scope, nullptr, 0, {}, request->operation};
                return host->execute(host->data, EditorString EDITOR_TEXT("scene.clear-clipboard"), &clear, sink);
            }
            const EditorValue value{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}};
            const EditorRequest copy{request->scope, &value, 1, {}, request->operation};
            return host->execute(host->data, EditorString EDITOR_TEXT("scene.copy"), &copy, sink);
        }
        case Camera: {
            auto *camera = CKCamera::Cast(doc->Resolve(Object(*request, "target")));
            if (!camera) return EDITOR_STALE;
            view.SetCamera(camera->GetID()); self.Version(*doc, view); return EDITOR_OK;
        }
        case ResetCamera: {
            auto *camera = view.Camera(); if (!camera) return EDITOR_NO_CHANGE;
            const auto expected = Object(*request, "target");
            if (expected && doc->Resolve(expected) != camera) return EDITOR_STALE;
            std::vector<CK_ID> moved{camera->GetID()};
            if (auto *targeted = CKTargetCamera::Cast(camera)) if (auto *target = targeted->GetTarget()) moved.push_back(target->GetID());
            ObjectsGesture gesture; if (!gesture.Begin(doc->Context(), moved)) return EDITOR_ERROR;
            view.ResetCamera(); return Result(*doc, gesture.Commit(*doc, "Reset Current Camera Settings", !view.IsViewCamera(camera->GetID())), sink);
        }
        case FrameRepresentation: {
            const auto *objects = Arg(*request, "objects"); const auto representation = Number(*request, "value");
            if (!objects || representation > view.FrameShapeNames().size()) return EDITOR_INVALID;
            const auto target = Object(*request, "target");
            auto *targetFrame = CK3dEntity::Cast(doc->Resolve(target));
            if (!targetFrame || !(targetFrame->GetFlags() & CK_3DENTITY_FRAME)) return EDITOR_STALE;
            bool selected = false;
            std::vector<CK_ID> frames;
            for (uint32_t i = 0; i < objects->objects.count; ++i) {
                auto *object = doc->Resolve(objects->objects.data[i]); if (!object) return EDITOR_STALE;
                selected = selected || objects->objects.data[i] == target;
                if (auto *frame = CK3dEntity::Cast(object)) if (frame->GetFlags() & CK_3DENTITY_FRAME) frames.push_back(frame->GetID());
            }
            if (!selected) frames.assign(1, targetFrame->GetID());
            return Result(*doc, SetFrameRepresentation(*doc, frames, int(representation)), sink);
        }
        case CreateGroup: {
            auto *scene = view.Scene(); const auto name = Text(*request, "name");
            if (!scene || name.empty() || name.find('\0') != std::string::npos) return EDITOR_INVALID;
            for (auto id : SceneGroups(doc->Context(), scene))
                if (auto *group = doc->Context()->GetObject(id); group && Lower(DevObjectName(group)) == Lower(name)) {
                    return Result(*doc, EditResult::Refused("A selection group with that name already exists"), sink);
                }
            const auto *objects = Arg(*request, "objects");
            std::vector<CK_ID> members;
            if (!objects) return EDITOR_INVALID;
            for (uint32_t i = 0; i < objects->objects.count; ++i) {
                auto *object = doc->Resolve(objects->objects.data[i], CKCID_BEOBJECT); if (!object) return EDITOR_STALE;
                members.push_back(object->GetID());
            }
            if (members.empty()) return EDITOR_NO_CHANGE;
            return Result(*doc, CreateSelectionGroup(*doc, scene->GetID(), Utf8ToAnsi(name), members), sink);
        }
        case ChooseGroup: {
            if (self.selection.Locked()) return EDITOR_REFUSED;
            auto *group = CKGroup::Cast(doc->Resolve(Object(*request, "target"))); if (!group) return EDITOR_STALE;
            const auto objects = doc->References(GroupSelection(doc->Context(), group->GetID()));
            if (objects.empty()) return EDITOR_NO_CHANGE;
            const EditorValue values[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}},
                {EDITOR_TEXT("render-only"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
            const EditorRequest select{request->scope, values, 2, {}, request->operation};
            const auto status = host->execute(host->data, EditorString EDITOR_TEXT("selection.replace"), &select, sink);
            self.Version(*doc, view); return status;
        }
        case RestoreInterface: {
            const auto restore = Number(*request, "restore-selection"), locked = Number(*request, "locked");
            if (restore > 1 || locked > 1) return EDITOR_INVALID;
            const auto group = Object(*request, "group"), camera = Object(*request, "camera");
            if ((group && !doc->Resolve(group, CKCID_GROUP)) || (camera && !doc->Resolve(camera, CKCID_CAMERA))) return EDITOR_STALE;
            if (restore) {
                const auto *objects = Arg(*request, "objects"); if (!objects) return EDITOR_INVALID;
                const EditorValue values[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, objects->objects},
                    {EDITOR_TEXT("locked"), EDITOR_INTEGER_VALUE, {}, {}, locked}};
                const EditorRequest select{request->scope, values, 2, {}, request->operation};
                const auto status = host->execute(host->data, EditorString EDITOR_TEXT("selection.restore"), &select, {});
                if (status != EDITOR_OK && status != EDITOR_NO_CHANGE) return status;
            }
            if (camera) view.SetCamera(camera.object);
            self.Version(*doc, view);
            const std::vector<EditorField> fields{{"created", EDITOR_OBJECT, group}};
            return Write(sink, fields);
        }
        }
        return EDITOR_INVALID;
    }
    static EditorStatus Ready(void *data, const EditorHost *, const EditorRequest *) {
        return static_cast<Entry *>(data)->owner->editor.NativeAvailable() ? EDITOR_OK : EDITOR_REFUSED;
    }
};
LayoutCommands::LayoutCommands(EditorApplication &editor, std::function<Document *()> current,
                               std::function<LayoutView &()> view, ObjectSelection &selection)
    : m_Data(std::make_unique<Data>(editor, std::move(current), std::move(view), selection)) {}
LayoutCommands::~LayoutCommands() = default;
void LayoutCommands::Poll() {
    if (!m_Data->editor.NativeAvailable() || m_Data->editor.HasEdits()) return;
    if (auto *doc = m_Data->current(); doc && doc->IsOpen() && !doc->EditOpen()) m_Data->Version(*doc, m_Data->view());
}
LayoutRestoreData LayoutCommands::ProjectInterface(const LayoutInterfaceData &data) const {
    LayoutRestoreData result; auto *doc = m_Data->current();
    if (!data.present || !doc || !doc->IsOpen()) return result;
    auto *context = doc->Context(); std::vector<CK_ID> ids;
    if (auto *group = CKGroup::Cast(context->GetObject(data.group)); group && !doc->IsDiscarded(data.group)) {
        result.group = doc->Reference(data.group);
        for (int i = 0; i < group->GetObjectCount(); ++i) if (auto *member = group->GetObject(i)) ids.push_back(member->GetID());
    } else for (auto id : data.selection) if (context->GetObject(id) && !doc->IsDiscarded(id)) ids.push_back(id);
    result.selection = doc->References(ids); result.locked = data.locked;
    if (!data.cameras.empty() && CKCamera::Cast(context->GetObject(data.cameras.front()))) result.camera = doc->Reference(data.cameras.front());
    return result;
}

const std::vector<ProviderChoice> &LayoutCommands::DefaultProviders() {
    static const std::vector<ProviderChoice> choices{{"query", "layout.scene", "builtin.layout"},
                                                     {"command", "layout.set-camera", "builtin.layout"},
                                                     {"command", "layout.reset-camera", "builtin.layout"},
                                                     {"command", "layout.frame-representation", "builtin.layout"},
                                                     {"command", "layout.create-selection-group", "builtin.layout"},
                                                     {"command", "layout.choose-group", "builtin.layout"},
                                                     {"command", "layout.restore-interface", "builtin.layout"},
                                                     {"command", "layout.copy", "builtin.layout"},
                                                     {"query", "layout.input", "builtin.layout-gestures"},
                                                     {"command", "layout.edit-gesture", "builtin.layout-gestures"},
                                                     {"query", "layout.curve-tool", "builtin.layout-gestures"},
                                                     {"command", "layout.wheel-zoom", "builtin.layout-gestures"},
                                                     {"command", "layout.zoom-on", "builtin.layout-gestures"},
                                                     {"command", "layout.player-click", "builtin.layout-gestures"},
                                                     {"command", "layout.curve-click", "builtin.layout-gestures"},
                                                     {"command", "layout.set-curve-tool", "builtin.layout-gestures"}};
    return choices;
}
bool LayoutCommands::Register(std::string &error) {
    const EditorParameter args[] = {{EDITOR_TEXT("section"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("x"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("y"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("shown"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter fields[] = {{EDITOR_TEXT("scene"), EDITOR_OBJECT}, {EDITOR_TEXT("camera"), EDITOR_OBJECT},
        {EDITOR_TEXT("saved-camera"), EDITOR_OBJECT}, {EDITOR_TEXT("frame"), EDITOR_OBJECT}, {EDITOR_TEXT("initial-target"), EDITOR_OBJECT},
        {EDITOR_TEXT("initial"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("matching-group"), EDITOR_OBJECT}, {EDITOR_TEXT("selected-types"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("rows"), EDITOR_RECORD}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("frame-shapes"), EDITOR_TEXT_LIST}};
    const EditorQueryDesc query{sizeof(EditorQueryDesc), EDITOR_TEXT("layout.scene"), EDITOR_TEXT("builtin.layout"), args, 7,
        Data::Read, m_Data.get(), EDITOR_QUERY_VERSIONED, EDITOR_DOCUMENT, fields, 13,
        [](void *data, const EditorHost *) { return static_cast<Data *>(data)->queryRevision; }};
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorParameter optionalTarget{EDITOR_TEXT("target"), EDITOR_OBJECT, 1};
    const EditorParameter frameArgs[] = {target, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE}};
    const EditorParameter groupArgs[] = {{EDITOR_TEXT("name"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}};
    const EditorParameter restore[] = {{EDITOR_TEXT("restore-selection"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("group"), EDITOR_OBJECT, 0, 1}, {EDITOR_TEXT("camera"), EDITOR_OBJECT, 0, 1},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("locked"), EDITOR_INTEGER_VALUE}};
    const EditorParameter objects{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
    EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.set-camera"), EDITOR_TEXT("builtin.layout"), EDITOR_TEXT("Select Camera"), &target, 1, Data::Run, &m_Data->entries[0], EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.reset-camera"), EDITOR_TEXT("builtin.layout"), EDITOR_TEXT("Reset Current Camera Settings"), &optionalTarget, 1, Data::Run, &m_Data->entries[1], EDITOR_DOCUMENT, EDITOR_DOCUMENT_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.frame-representation"), EDITOR_TEXT("builtin.layout"), EDITOR_TEXT("Change Representation"), frameArgs, 3, Data::Run, &m_Data->entries[2], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.create-selection-group"), EDITOR_TEXT("builtin.layout"), EDITOR_TEXT("Selection Groups"), groupArgs, 2, Data::Run, &m_Data->entries[3], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.choose-group"), EDITOR_TEXT("builtin.layout"), EDITOR_TEXT("Select Group"), &target, 1, Data::Run, &m_Data->entries[4], EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.restore-interface"), EDITOR_TEXT("builtin.layout"), EDITOR_TEXT("Restore Layout"), restore, 5, Data::Run, &m_Data->entries[5], EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.copy"), EDITOR_TEXT("builtin.layout"), EDITOR_TEXT("Copy Objects"), &objects, 1, Data::Run, &m_Data->entries[6], EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE}};
    for (auto &command : commands) command.state = Data::Ready;
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("layout.scene");
    module.commands = commands; module.commandCount = 7; module.queries = &query; module.queryCount = 1;
    return m_Data->editor.Register(module, error) && m_Data->gestures.Register(m_Data->editor, error);
}
} // namespace vdev

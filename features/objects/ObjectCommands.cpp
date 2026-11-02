#include "features/objects/ObjectCommands.h"

#include "application/EditorApplication.h"
#include "core/document/Document.h"
#include "core/graph/ScriptCommands.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectQueries.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <deque>
#include <limits>
#include <set>

namespace vdev {
namespace {
const EditorValue *Argument(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key)
            return &request.values[i];
    return nullptr;
}
} // namespace
struct ObjectCommands::State {
    enum Action {
        Layout,
        Scene,
        Group,
        Place,
        Array,
        Scripts,
        Destroy,
        DeleteScripts,
        RemoveScene,
        RemoveGroup,
        AutomaticPortal,
        AutomaticPortals
    };
    struct Command {
        ObjectCommands *owner;
        Action action;
        LayoutObject kind = LayoutObject::Frame3D;
    };
    std::deque<Command> commands;
    std::unique_ptr<CKDependencies> deleteOptions;
    CKDependencies &Options() {
        if (!deleteOptions) {
            deleteOptions = std::make_unique<CKDependencies>();
            if (auto *defaults = CKGetDefaultClassDependencies(CK_DEPENDENCIES_DELETE))
                *deleteOptions = *defaults;
            else
                deleteOptions->m_Flags = CK_DEPENDENCIES_CUSTOM;
        }
        const auto classes = CKGetClassCount();
        const auto old = deleteOptions->Size();
        if (old < classes) {
            deleteOptions->Resize(classes);
            for (int i = old; i < classes; ++i)
                (*deleteOptions)[i] = 0;
        }
        return *deleteOptions;
    }
};
ObjectCommands::ObjectCommands(std::function<Document *()> current)
    : m_State(std::make_unique<State>()), m_Current(std::move(current)) {}
ObjectCommands::~ObjectCommands() = default;

EditorStatus ObjectCommands::DeleteOptions(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    auto &options = static_cast<ObjectCommands *>(data)->m_State->Options();
    std::vector<uint64_t> values;
    for (int i = 0; i < options.Size(); ++i)
        values.push_back(options[i]);
    const EditorValue fields[] = {{EDITOR_TEXT("flags"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(options.m_Flags)},
                                  {EDITOR_TEXT("options"),
                                   EDITOR_INTEGER_LIST,
                                   {},
                                   {},
                                   0,
                                   {},
                                   {values.data(), static_cast<uint32_t>(values.size())}}};
    return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
}
EditorStatus ObjectCommands::Execute(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    const auto &command = *static_cast<State::Command *>(data);
    auto &self = *command.owner;
    auto *document = self.m_Current();
    if (!document || document->Token() != request->scope.document)
        return EDITOR_STALE;
    auto object = [&](const char *key) {
        const auto *v = Argument(*request, key);
        return v ? document->NativeId(v->object) : 0;
    };
    auto integer = [&](const char *key) {
        const auto *v = Argument(*request, key);
        return v ? v->integer : 0;
    };
    std::vector<CK_ID> targets;
    std::set<CK_ID> included;
    if (const auto *list = Argument(*request, "objects"))
        for (uint32_t i = 0; i < list->objects.count; ++i) {
            const auto id = document->NativeId(list->objects.data[i]);
            if (!id)
                return EDITOR_STALE;
            if (included.insert(id).second)
                targets.push_back(id);
        }
    std::string name;
    if (const auto *value = Argument(*request, "name"))
        name = Utf8ToAnsi(std::string(value->text.data ? value->text.data : "", value->text.size));
    std::vector<EditResult> results;
    std::vector<CK_ID> portals;
    if (command.action == State::Layout) {
        if (integer("show-cone") > 1 || integer("show-in-player") > 1)
            return EDITOR_INVALID;
        results.push_back(CreateLayoutObject(*document, command.kind, object("target"), object("camera"),
                                             {integer("show-cone") != 0, integer("show-in-player") != 0}));
    } else
        switch (command.action) {
        case State::Scene:
            results.push_back(CreateScene(*document, name, targets));
            break;
        case State::Group: {
            auto scene = object("scene");
            if (!scene && document->Context()->GetCurrentScene())
                scene = document->Context()->GetCurrentScene()->GetID();
            results.push_back(CreateGroup(*document, name, targets, scene));
            break;
        }
        case State::Place:
            results.push_back(CreatePlace(*document, name, targets));
            break;
        case State::Array:
            results.push_back(CreateArray(*document, name));
            break;
        case State::AutomaticPortal:
            results.push_back(CreateAutomaticPortal(*document, object("place"), object("other")));
            break;
        case State::AutomaticPortals:
            results.push_back(CreateAutomaticPortals(*document, &portals));
            break;
        case State::Scripts:
            results = CreateScriptsOn(*document, targets);
            break;
        case State::DeleteScripts:
            for (auto id : targets)
                results.push_back(DeleteScript(*document, id));
            break;
        case State::RemoveScene:
            if (integer("dependencies") > 1)
                return EDITOR_INVALID;
            results.push_back(RemoveFromScene(*document, targets, object("scene"), integer("dependencies") != 0));
            break;
        case State::RemoveGroup:
            results.push_back(RemoveGroupMembers(*document, object("group"), targets));
            break;
        case State::Destroy: {
            if (integer("custom") > 1 || (integer("flags") & ~uint64_t(CK_DEPENDENCIES_NONE | CK_DEPENDENCIES_FULL)))
                return EDITOR_INVALID;
            auto options = self.m_State->Options();
            if (integer("custom")) {
                const auto *v = Argument(*request, "options");
                // CKDependencies indexes directly by class ID, including
                // dependency classes not present in the selected targets.
                if (v->integers.count < static_cast<uint32_t>(CKGetClassCount()))
                    return EDITOR_INVALID;
                options.Resize(static_cast<int>(v->integers.count));
                options.m_Flags = static_cast<CK_DEPENDENCIES_FLAGS>(integer("flags"));
                for (uint32_t i = 0; i < v->integers.count; ++i) {
                    if (v->integers.data[i] > std::numeric_limits<CKDWORD>::max())
                        return EDITOR_INVALID;
                    options[i] = static_cast<CKDWORD>(v->integers.data[i]);
                }
                self.m_State->Options() = options;
            }
            results.push_back(DestroyLevelObjects(*document, targets, &options));
            break;
        }
        default:
            return EDITOR_INVALID;
        }
    std::vector<ObjectRef> created;
    for (auto id : portals)
        created.push_back(document->Reference(id));
    std::string message;
    bool applied = false, failed = false, refused = false;
    for (const auto &result : results) {
        applied |= result.ok;
        failed |= result.Status() == EditStatus::Error;
        refused |= result.Status() == EditStatus::Refused;
        if (result.ok && result.created)
            created.push_back(document->Reference(result.created));
        if (!result.message.empty() && !result.Unchanged()) {
            if (!message.empty())
                message += "\n";
            message += AnsiToUtf8(result.message);
        }
    }
    const ObjectRef last = created.empty() ? ObjectRef{} : created.back();
    if (message.empty() && results.size() == 1)
        message = AnsiToUtf8(results.front().message);
    const EditorValue values[] = {{EDITOR_TEXT("created"), EDITOR_OBJECT, last},
                                  {EDITOR_TEXT("objects"),
                                   EDITOR_OBJECT_LIST,
                                   {},
                                   {},
                                   0,
                                   {created.data(), static_cast<uint32_t>(created.size())}},
                                  {EDITOR_TEXT("camera"),
                                   EDITOR_INTEGER_VALUE,
                                   {},
                                   {},
                                   uint64_t(command.action == State::Layout && command.kind == LayoutObject::Camera)},
                                  {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)}};
    if (sink.write)
        sink.write(sink.data, values, 4);
    // Batch creation preserves the existing per-owner refusals and successful
    // results in one Edit. Native failures still roll the document edit back.
    return failed ? EDITOR_ERROR : applied ? EDITOR_OK : refused ? EDITOR_REFUSED : EDITOR_NO_CHANGE;
}
EditorStatus ObjectCommands::Read(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = static_cast<ObjectCommands *>(data)->m_Current();
    auto *target = Argument(*request, "target");
    auto *native = document && document->Token() == request->scope.document && target ? document->Resolve(target->object) : nullptr;
    if (!native) return EDITOR_STALE;
    const bool hasName = native->GetName() != nullptr;
    const auto object = DescribeObject(*document, document->NativeId(target->object));
    const auto name = AnsiToUtf8(object.name);
    const auto type = AnsiToUtf8(CKClassIDToString(object.cid));
    const EditorValue fields[] = {{EditorLiteral("name"), EDITOR_TEXT_VALUE, {}, EditorText(name)},
                                  {EditorLiteral("class"), EDITOR_TEXT_VALUE, {}, EditorText(type)},
                                  {EditorLiteral("object"), EDITOR_OBJECT, object.reference, {}},
                                  {EDITOR_TEXT("has-name"), EDITOR_INTEGER_VALUE, {}, {},
                                   uint64_t(hasName)}};
    return sink.write ? sink.write(sink.data, fields, 4) : EDITOR_INVALID;
}
EditorStatus ObjectCommands::Rename(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto &self = *static_cast<ObjectCommands *>(data);
    auto *document = self.m_Current();
    auto *target = Argument(*request, "target");
    auto *name = Argument(*request, "name");
    if (!document || document->Token() != request->scope.document || !target || !name ||
        !document->Resolve(target->object))
        return EDITOR_STALE;
    const auto result =
        RenameLevelObject(*document, target->object, Utf8ToAnsi(std::string(name->text.data, name->text.size)));
    const auto message = AnsiToUtf8(result.message);
    if (sink.write && !message.empty()) {
        const EditorValue value{EditorLiteral("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)};
        sink.write(sink.data, &value, 1);
    }
    return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_REFUSED;
}
bool ObjectCommands::Register(EditorApplication &editor, std::string &error) {
    static const EditorParameter renameArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                                 {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE}};
    static const EditorParameter queryArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT}};
    const EditorCommandDesc rename = {sizeof(rename),
                                      EDITOR_TEXT("object.rename"),
                                      EDITOR_TEXT("builtin.objects"),
                                      EDITOR_TEXT("Rename"),
                                      renameArgs,
                                      2,
                                      &Rename,
                                      this};
    const EditorParameter fields[] = {{EDITOR_TEXT("name"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("class"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("object"), EDITOR_OBJECT},
                                      {EDITOR_TEXT("has-name"), EDITOR_INTEGER_VALUE}};
    EditorQueryDesc details = {
        sizeof(details), EDITOR_TEXT("object.details"), EDITOR_TEXT("builtin.objects"), queryArgs, 1, &Read, this};
    details.refresh = EDITOR_QUERY_EACH_READ;
    details.fields = fields;
    details.fieldCount = uint32_t(std::size(fields));
    const EditorPropertyDesc name = {
        sizeof(name),        EDITOR_TEXT("object.name"),   EDITOR_TEXT("Name"), EDITOR_TEXT("object.details"),
        EDITOR_TEXT("name"), EDITOR_TEXT("object.rename"), EDITOR_TEXT("name"), EDITOR_TEXT("text.single-line")};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EditorLiteral("objects.editing");
    std::vector<EditorCommandDesc> commands{rename};
    const EditorParameter layoutArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, 1},
                                          {EDITOR_TEXT("camera"), EDITOR_OBJECT, 1},
                                          {EDITOR_TEXT("show-cone"), EDITOR_INTEGER_VALUE, 1},
                                          {EDITOR_TEXT("show-in-player"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter createArgs[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, 1},
                                          {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, 1},
                                          {EDITOR_TEXT("scene"), EDITOR_OBJECT, 1}};
    const EditorParameter objectArgs{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
    const EditorParameter destroyArgs[] = {objectArgs,
                                           {EDITOR_TEXT("custom"), EDITOR_INTEGER_VALUE, 1},
                                           {EDITOR_TEXT("flags"), EDITOR_INTEGER_VALUE, 1},
                                           {EDITOR_TEXT("options"), EDITOR_INTEGER_LIST, 1}};
    const EditorParameter sceneArgs[] = {
        objectArgs, {EDITOR_TEXT("scene"), EDITOR_OBJECT}, {EDITOR_TEXT("dependencies"), EDITOR_INTEGER_VALUE}};
    const EditorParameter groupArgs[] = {objectArgs, {EDITOR_TEXT("group"), EDITOR_OBJECT}};
    const EditorParameter portalArgs[] = {{EDITOR_TEXT("place"), EDITOR_OBJECT}, {EDITOR_TEXT("other"), EDITOR_OBJECT}};
    const auto add = [&](const char *id, const char *title, State::Action action, const EditorParameter *parameters,
                         uint32_t count, LayoutObject kind = LayoutObject::Frame3D) {
        m_State->commands.push_back({this, action, kind});
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(id), EDITOR_TEXT("builtin.objects"),
                            EditorLiteral(title), parameters, count, Execute, &m_State->commands.back(),
                            EDITOR_DOCUMENT, action == State::Destroy ? EDITOR_DOCUMENT_CHANGE : EDITOR_DOCUMENT_EDIT});
    };
    struct LayoutCreation {
        const char *id, *title;
        LayoutObject kind;
    };
    const LayoutCreation creations[] = {{"object.create-camera", "Create Camera", LayoutObject::Camera},
                                        {"object.create-light", "Create Light", LayoutObject::Light},
                                        {"object.create-frame3d", "Create 3D Frame", LayoutObject::Frame3D},
                                        {"object.create-curve", "Create Curve", LayoutObject::Curve},
                                        {"object.create-grid", "Create Grid", LayoutObject::Grid},
                                        {"object.create-frame2d", "Create 2D Frame", LayoutObject::Frame2D},
                                        {"object.create-material", "Create Material", LayoutObject::Material},
                                        {"object.create-texture", "Create Texture", LayoutObject::Texture},
                                        {"object.create-portal", "Create Portal", LayoutObject::Portal}};
    for (const auto &creation : creations)
        add(creation.id, creation.title, State::Layout, layoutArgs, 4, creation.kind);
    add("object.create-scene", "Create Scene", State::Scene, createArgs, 2);
    add("object.create-group", "Create Group", State::Group, createArgs, 3);
    add("object.create-place", "Create Place", State::Place, createArgs, 2);
    add("object.create-array", "Create Array", State::Array, createArgs + 1, 1);
    add("object.create-automatic-portal", "Automatic Portal", State::AutomaticPortal, portalArgs, 2);
    add("object.create-automatic-portals", "Automatic Portals", State::AutomaticPortals, nullptr, 0);
    add("script.create", "Create Scripts", State::Scripts, &objectArgs, 1);
    add("object.destroy", "Delete Objects", State::Destroy, destroyArgs, 4);
    add("script.delete", "Delete Scripts", State::DeleteScripts, &objectArgs, 1);
    add("object.remove-from-scene", "Remove From Scene", State::RemoveScene, sceneArgs, 3);
    add("object.remove-from-group", "Remove From Group", State::RemoveGroup, groupArgs, 2);
    module.commands = commands.data();
    module.commandCount = static_cast<uint32_t>(commands.size());
    const EditorParameter optionFields[] = {{EDITOR_TEXT("flags"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("options"), EDITOR_INTEGER_LIST}};
    const EditorQueryDesc queries[] = {details,
                                       {sizeof(EditorQueryDesc), EDITOR_TEXT("object.delete-options"),
                                        EDITOR_TEXT("builtin.objects"), nullptr, 0, DeleteOptions, this,
                                        EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, optionFields, 2}};
    module.queries = queries;
    module.queryCount = 2;
    module.properties = &name;
    module.propertyCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev

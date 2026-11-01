#include "features/layout/LayoutAuthoring.h"
#include "application/ObjectSelection.h"
#include "application/Preferences.h"
#include "core/document/Document.h"
#include "core/layout/LayoutView.h"
#include "core/layout/LayoutExplorer.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace vdev {
namespace {
const EditorValue &Arg(const EditorRequest &r, const char *key) {
    for (uint32_t i = 0; i < r.count; ++i)
        if (std::string(r.values[i].key.data, r.values[i].key.size) == key)
            return r.values[i];
    static const EditorValue empty{};
    return empty;
}
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
EditorStatus Result(const EditResult &result, EditorSink sink) {
    const auto status = Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(result.message)}});
    if (status != EDITOR_OK)
        return status;
    return result.Unchanged()                       ? EDITOR_NO_CHANGE
           : result.ok                              ? EDITOR_OK
           : result.Status() == EditStatus::Refused ? EDITOR_REFUSED
                                                    : EDITOR_ERROR;
}
CK_ID BlockTarget(CKContext *context, LayoutView &view, const LayoutPickInfo &info, CK_CLASSID cid, bool ctrl) {
    const CK_ID character = ctrl ? 0 : info.character;
    CK_ID target = 0;
    switch (cid) {
    case CKCID_SCENE:
        target = view.Scene() ? view.Scene()->GetID() : 0;
        break;
    case CKCID_LEVEL:
        target = context->GetCurrentLevel() ? context->GetCurrentLevel()->GetID() : 0;
        break;
    case CKCID_2DENTITY:
    case CKCID_SPRITE:
        target = info.entity2d;
        break;
    case CKCID_MATERIAL:
        target = info.material;
        break;
    case CKCID_TEXTURE:
        target = info.texture;
        break;
    case CKCID_MESH:
    case CKCID_PATCHMESH:
        target = info.mesh;
        break;
    case CKCID_BEOBJECT:
    case CKCID_RENDEROBJECT:
        target = character ? character : info.entity3d ? info.entity3d : info.entity2d;
        break;
    case CKCID_3DENTITY:
    case CKCID_3DOBJECT:
    case CKCID_BODYPART:
        target = character ? character : info.entity3d;
        break;
    case CKCID_CHARACTER:
        target = info.character;
        break;
    default:
        target = info.entity3d;
        break;
    }
    for (const auto id : {info.entity3d, info.character, info.entity2d, info.mesh, info.material, info.texture})
        if (!target)
            target = id;
    return target;
}
struct BehaviorDropTargets {
    CK_ID target = 0, highlight = 0;
    bool multiple = false;
    std::vector<CK_ID> objects;
};
BehaviorDropTargets BehaviorReceivers(Document &doc, LayoutView &view, const LayoutPickInfo &info, CK_CLASSID cid,
                                      bool ctrl, const std::vector<ObjectRef> &selected) {
    const auto contains = [&](CK_ID id) {
        const auto ref = doc.Reference(id);
        return ref && std::find(selected.begin(), selected.end(), ref) != selected.end();
    };
    BehaviorDropTargets result;
    result.multiple = selected.size() > 1 && (contains(info.entity3d) || contains(info.character));
    result.target = BlockTarget(doc.Context(), view, info, cid, ctrl);
    result.highlight = result.multiple ? info.entity3d : result.target;
    if (result.multiple) {
        for (const auto ref : selected)
            if (auto *object = doc.Resolve(ref); object && CKIsChildClassOf(object, cid))
                result.objects.push_back(ref.object);
    } else if (doc.Resolve(doc.Reference(result.target)))
        result.objects.push_back(result.target);
    return result;
}
} // namespace

struct LayoutAuthoring::Data {
    EditorApplication &editor;
    std::function<Document *()> current;
    std::function<LayoutView &()> view;
    ObjectSelection &selection;
    const Preferences &preferences;
    Document *Doc(const EditorRequest &r) const {
        auto *doc = current();
        return doc && doc->IsOpen() && doc->Token() == r.scope.document ? doc : nullptr;
    }
    std::vector<ObjectRef> Selected(Document &doc) const {
        std::vector<ObjectRef> result;
        for (const auto ref : selection.References())
            if (doc.Resolve(ref))
                result.push_back(ref);
        return result;
    }
    static EditorStatus Creation(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.Doc(*r);
        if (!doc || self.view().Context() != doc->Context())
            return EDITOR_STALE;
        auto *camera = self.view().Camera();
        const auto &p = self.preferences;
        const auto selected =
            self.selection.References().size() == 1 ? self.selection.References().front() : ObjectRef{};
        return Write(
            sink, {{"camera", EDITOR_OBJECT, camera ? doc->Reference(camera->GetID()) : ObjectRef{}},
                   {"target", EDITOR_OBJECT, doc->Resolve(selected) ? selected : ObjectRef{}},
                   {"camera-cone", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(p.Bool("3D Layout\\Auto Show Camera Cone"))},
                   {"light-cone", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(p.Bool("3D Layout\\Auto Show Light Cone"))},
                   {"show-player",
                    EDITOR_INTEGER_VALUE,
                    {},
                    {},
                    uint64_t(p.Bool("3D Layout\\Auto Show Entities In Player Mode"))},
                   {"show-setup", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(p.Bool("3D Layout\\Auto Show Setup"))}});
    }
    static EditorStatus Pick(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.Doc(*r);
        if (!doc || self.view().Context() != doc->Context())
            return EDITOR_STALE;
        const auto x = Arg(*r, "x").real, y = Arg(*r, "y").real;
        const auto type = Arg(*r, "kind").text;
        const std::string_view kind(type.data ? type.data : "", type.size);
        const auto ctrl = Arg(*r, "ctrl").integer;
        if (!std::isfinite(x) || !std::isfinite(y) || x < INT32_MIN || x > INT32_MAX || y < INT32_MIN ||
            y > INT32_MAX || (kind != "behavior" && kind != "resource" && kind != "attribute" && kind != "material") ||
            ctrl > 1)
            return EDITOR_INVALID;
        const auto info = self.view().PickInfo(int(x), int(y));
        auto *context = doc->Context();
        const auto selected = self.Selected(*doc);
        const auto contains = [&](CK_ID id) {
            const auto ref = doc->Reference(id);
            return ref && std::find(selected.begin(), selected.end(), ref) != selected.end();
        };
        CK_ID target = 0, highlight = 0;
        std::vector<CK_ID> targets;
        std::string text;
        bool multiple = false;
        if (kind == "behavior") {
            const auto prototype = Arg(*r, "prototype").integer;
            auto *declaration = CKGetObjectDeclarationFromGuid(CKGUID(CKDWORD(prototype >> 32), CKDWORD(prototype)));
            if (!declaration)
                return EDITOR_INVALID;
            auto receivers =
                BehaviorReceivers(*doc, self.view(), info, declaration->GetCompatibleClassId(), ctrl != 0, selected);
            target = receivers.target;
            highlight = receivers.highlight;
            multiple = receivers.multiple;
            targets = std::move(receivers.objects);
            if (multiple)
                text = "Drop Behavior on Multi-Selection";
            else if (auto *object = doc->Resolve(doc->Reference(target)))
                text = "Drop Behavior on " + DevObjectName(object);
        } else if (kind == "resource") {
            target = info.character ? info.character : info.entity3d ? info.entity3d : info.entity2d;
            highlight = info.character ? info.character : info.entity3d;
            if (target)
                targets.push_back(target);
        } else if (kind == "attribute") {
            target = info.entity2d ? info.entity2d : info.character ? info.character : info.entity3d;
            multiple = target && selected.size() > 1 && contains(target);
            if (multiple) {
                for (auto ref : selected)
                    if (doc->Resolve(ref))
                        targets.push_back(ref.object);
                text = "Drop Attribute on Multi-Selection";
            } else if (auto *object = context->GetObject(target)) {
                targets.push_back(target);
                text = "Drop Attribute on " + DevObjectName(object);
            }
            highlight = target;
        } else {
            auto *source = CKMaterial::Cast(doc->Resolve(Arg(*r, "source").object));
            auto *entity = CK3dEntity::Cast(context->GetObject(info.entity3d));
            if (source && entity && entity->GetCurrentMesh() && info.material)
                highlight = info.entity3d;
        }
        return Write(sink,
                     {{"target", EDITOR_OBJECT, doc->Reference(target)},
                      {"highlight", EDITOR_OBJECT, doc->Reference(highlight)},
                      {"objects", EDITOR_OBJECT_LIST, {}, {}, 0, doc->References(targets)},
                      {"mesh", EDITOR_OBJECT, doc->Reference(kind == "material" && highlight ? info.mesh : 0)},
                      {"material", EDITOR_OBJECT, doc->Reference(kind == "material" && highlight ? info.material : 0)},
                      {"text", EDITOR_TEXT_VALUE, {}, text},
                      {"multiple", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(multiple)}});
    }
    static EditorStatus Highlight(void *data, const EditorHost *, const EditorRequest *r, EditorSink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.Doc(*r);
        if (!doc || self.view().Context() != doc->Context())
            return EDITOR_STALE;
        const auto ref = Arg(*r, "target").object;
        if (ref && !doc->Resolve(ref))
            return EDITOR_STALE;
        self.view().SetDropTarget(ref.object);
        return EDITOR_OK;
    }
    static EditorStatus Material(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.Doc(*r);
        if (!doc || !doc->Resolve(Arg(*r, "mesh").object, CKCID_MESH) ||
            !doc->Resolve(Arg(*r, "from").object, CKCID_MATERIAL) ||
            !doc->Resolve(Arg(*r, "to").object, CKCID_MATERIAL))
            return EDITOR_STALE;
        return Result(ReplaceMeshMaterial(*doc, Arg(*r, "mesh").object.object, Arg(*r, "from").object.object,
                                          Arg(*r, "to").object.object),
                      sink);
    }
};

LayoutAuthoring::LayoutAuthoring(EditorApplication &editor, std::function<Document *()> current,
                                 std::function<LayoutView &()> view, ObjectSelection &selection,
                                 const Preferences &preferences)
    : m_Data(std::make_unique<Data>(Data{editor, std::move(current), std::move(view), selection, preferences})) {}
LayoutAuthoring::~LayoutAuthoring() = default;
const std::vector<ProviderChoice> &LayoutAuthoring::DefaultProviders() {
    static const std::vector<ProviderChoice> providers{
        {"query", "layout.creation", "builtin.layout-authoring"},
        {"query", "layout.drop-target", "builtin.layout-authoring"},
        {"command", "layout.highlight-drop", "builtin.layout-authoring"},
        {"command", "layout.replace-material", "builtin.layout-authoring"}};
    return providers;
}
bool LayoutAuthoring::Register(std::string &error) {
    const EditorParameter creationFields[] = {{EDITOR_TEXT("camera"), EDITOR_OBJECT},
                                              {EDITOR_TEXT("target"), EDITOR_OBJECT},
                                              {EDITOR_TEXT("camera-cone"), EDITOR_INTEGER_VALUE},
                                              {EDITOR_TEXT("light-cone"), EDITOR_INTEGER_VALUE},
                                              {EDITOR_TEXT("show-player"), EDITOR_INTEGER_VALUE},
                                              {EDITOR_TEXT("show-setup"), EDITOR_INTEGER_VALUE}};
    const EditorParameter pickArgs[] = {
        {EDITOR_TEXT("x"), EDITOR_REAL_VALUE},          {EDITOR_TEXT("y"), EDITOR_REAL_VALUE},
        {EDITOR_TEXT("kind"), EDITOR_TEXT_VALUE},       {EDITOR_TEXT("prototype"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("ctrl"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("source"), EDITOR_OBJECT, 1}};
    const EditorParameter pickFields[] = {
        {EDITOR_TEXT("target"), EDITOR_OBJECT},         {EDITOR_TEXT("highlight"), EDITOR_OBJECT},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},   {EDITOR_TEXT("mesh"), EDITOR_OBJECT},
        {EDITOR_TEXT("material"), EDITOR_OBJECT},       {EDITOR_TEXT("text"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("multiple"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("layout.creation"), EDITOR_TEXT("builtin.layout-authoring"), nullptr, 0,
         Data::Creation, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, creationFields, 6},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("layout.drop-target"), EDITOR_TEXT("builtin.layout-authoring"), pickArgs,
         6, Data::Pick, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, pickFields, 7}};
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT, 0, 1};
    const EditorParameter material[] = {
        {EDITOR_TEXT("mesh"), EDITOR_OBJECT}, {EDITOR_TEXT("from"), EDITOR_OBJECT}, {EDITOR_TEXT("to"), EDITOR_OBJECT}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.highlight-drop"), EDITOR_TEXT("builtin.layout-authoring"),
         EDITOR_TEXT("Drop Target"), &target, 1, Data::Highlight, m_Data.get(), EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.replace-material"), EDITOR_TEXT("builtin.layout-authoring"),
         EDITOR_TEXT("Replace Material"), material, 3, Data::Material, m_Data.get(), EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_EDIT}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("layout.authoring");
    module.commands = commands;
    module.commandCount = 2;
    module.queries = queries;
    module.queryCount = 2;
    return m_Data->editor.Register(module, error);
}
} // namespace vdev

#include "features/layout/LayoutAuthoring.h"
#include "application/ObjectSelection.h"
#include "application/Preferences.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/graph/ParameterValues.h"
#include "core/graph/ValueCommands.h"
#include "core/layout/LayoutView.h"
#include "core/layout/LayoutExplorer.h"
#include "core/level/LevelCommands.h"
#include "core/schematic/SchematicNavigation.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <cmath>
#include <set>
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
std::vector<CKParameter *> Inputs(CKBehavior *block) {
    std::vector<CKParameter *> inputs;
    const auto add = [&](CKParameterIn *input) {
        if (!input || (input->GetObjectFlags() & CK_PARAMETERIN_DISABLED))
            return;
        auto *holder = EditableValueOf(input);
        if (holder && !holder->IsMyselfParameter())
            inputs.push_back(holder);
    };
    if (block->IsUsingTarget())
        add(block->GetTargetParameter());
    for (int i = 0; i < block->GetInputParameterCount(); ++i)
        add(block->GetInputParameter(i));
    return inputs;
}
} // namespace

struct LayoutAuthoring::Data {
    LayoutAuthoring &owner;
    EditorApplication &editor;
    std::function<Document *()> current;
    std::function<LayoutView &()> view;
    ObjectSelection &selection;
    const Preferences &preferences;
    std::function<SchematicNavigation &()> navigation;
    DocumentToken document{};
    EditorView source = 0;
    ObjectRef block{}, script{};
    std::vector<ObjectRef> parameters;
    std::unique_ptr<Edit> edit;
    uint64_t drop = 0, nextDrop = 1, revision = 1;
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
    void Clear() {
        edit.reset();
        block = {};
        script = {};
        parameters.clear();
        document = {};
        source = 0;
        drop = 0;
        ++revision;
    }
    void Close(bool commit) {
        if (edit && commit)
            edit->Commit();
        Clear();
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
    static EditorStatus Drop(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.Doc(*r);
        if (!doc)
            return EDITOR_STALE;
        const auto ask = Arg(*r, "ask").integer;
        const auto &position = Arg(*r, "position").reals;
        if (ask > 2 || Arg(*r, "ctrl").integer > 1 ||
            (position.count &&
             (position.count != 2 || !std::isfinite(position.data[0]) || !std::isfinite(position.data[1]) ||
              position.data[0] < INT32_MIN || position.data[0] > INT32_MAX || position.data[1] < INT32_MIN ||
              position.data[1] > INT32_MAX)))
            return EDITOR_INVALID;
        std::vector<uint64_t> prototypes;
        const auto &list = Arg(*r, "prototypes").integers;
        if (list.count > 4096)
            return EDITOR_INVALID;
        if (list.count)
            prototypes.assign(list.data, list.data + list.count);
        else
            prototypes.push_back(Arg(*r, "prototype").integer);
        if (prototypes.size() > 4096)
            return EDITOR_INVALID;
        for (auto prototype : prototypes)
            if (!CKGetObjectDeclarationFromGuid(CKGUID(CKDWORD(prototype >> 32), CKDWORD(prototype))))
                return EDITOR_INVALID;
        const auto &objects = Arg(*r, "objects").objects;
        if (objects.count > 65536 || (objects.count && position.count) || (list.count && Arg(*r, "prototype").integer))
            return EDITOR_INVALID;
        std::vector<ObjectRef> targets;
        std::set<ObjectRef> included;
        for (uint32_t i = 0; i < objects.count; ++i) {
            auto *object = doc->Resolve(objects.data[i]);
            if (!object)
                return EDITOR_STALE;
            if (included.insert(objects.data[i]).second)
                targets.push_back(objects.data[i]);
        }
        LayoutPickInfo info;
        std::vector<ObjectRef> selected;
        if (position.count) {
            if (self.view().Context() != doc->Context())
                return EDITOR_STALE;
            info = self.view().PickInfo(int(position.data[0]), int(position.data[1]));
            selected = self.Selected(*doc);
        }
        std::vector<std::vector<ObjectRef>> receivers;
        size_t total = 0;
        for (auto prototype : prototypes) {
            auto picked = targets;
            if (position.count) {
                auto *declaration =
                    CKGetObjectDeclarationFromGuid(CKGUID(CKDWORD(prototype >> 32), CKDWORD(prototype)));
                picked = doc->References(BehaviorReceivers(*doc, self.view(), info, declaration->GetCompatibleClassId(),
                                                           Arg(*r, "ctrl").integer != 0, selected)
                                             .objects);
            }
            total += picked.size();
            if (total > 65536)
                return EDITOR_INVALID;
            receivers.push_back(std::move(picked));
        }
        if (!total)
            return Result(EditResult::Refused("Incompatible Class, cannot add behavior to selection."), sink);
        size_t lastReceiver = receivers.size() - 1;
        while (receivers[lastReceiver].empty())
            --lastReceiver;
        if (self.view().Context() == doc->Context())
            self.view().SetDropTarget(0);
        std::string messages;
        ObjectRef lastBlock{}, lastScript{};
        for (size_t index = 0; index < prototypes.size(); ++index) {
            const auto prototype = prototypes[index];
            const auto &targets = receivers[index];
            auto edit = std::make_unique<Edit>(*doc, "Drop Behavior", Edit::Marks::Nested);
            CK_ID script = 0, block = 0;
            for (auto ref : targets) {
                auto *object = doc->Resolve(ref);
                if (!object)
                    return Result(edit->Fail("Drop target is no longer available"), sink);
                if (CKBehavior::Cast(object) && self.navigation().hiddenScripts.count(ref)) {
                    if (!messages.empty())
                        messages += '\n';
                    messages += "Cannot drop on script with hidden graph!";
                    continue;
                }
                const auto id = object->GetID();
                CK_ID into = 0;
                const CKGUID guid(CKDWORD(prototype >> 32), CKDWORD(prototype));
                const auto result = CKBehavior::Cast(object)
                                        ? (into = id, DropBuildingBlockInto(*doc, id, guid))
                                        : DropBuildingBlockOn(*doc, id, guid, self.navigation().HiddenIds(*doc), &into);
                if (result.ok) {
                    script = into;
                    block = result.created;
                } else if (!result.message.empty()) {
                    if (!messages.empty())
                        messages += '\n';
                    messages += AnsiToUtf8(result.message);
                }
                if (result.Status() == EditStatus::Error)
                    return Result(edit->Fail(result.message), sink);
            }
            if (!block)
                continue;
            lastBlock = doc->Reference(block);
            lastScript = doc->Reference(script);
            auto *behavior = CKBehavior::Cast(doc->Resolve(lastBlock));
            const bool asking = index == lastReceiver && ask != 2 && (ask == 1 || targets.size() == 1) &&
                                (behavior->GetInputParameterCount() > 0 || behavior->IsUsingTarget());
            if (asking) {
                self.document = doc->Token();
                self.source = r->scope.view;
                self.script = lastScript;
                self.block = lastBlock;
                for (auto *input : Inputs(behavior))
                    self.parameters.push_back(doc->Reference(input->GetID()));
                self.drop = self.nextDrop++;
                ++self.revision;
                self.edit = std::move(edit);
            } else
                edit->Commit();
            if (self.owner.rememberBehavior)
                self.owner.rememberBehavior(prototype);
            if (self.owner.showBehavior)
                self.owner.showBehavior(lastScript, lastBlock);
        }
        const auto status = Write(sink, {{"script", EDITOR_OBJECT, lastScript},
                                         {"created", EDITOR_OBJECT, lastBlock},
                                         {"message", EDITOR_TEXT_VALUE, {}, messages}});
        return status != EDITOR_OK ? status : lastBlock ? EDITOR_OK : EDITOR_REFUSED;
    }
    static EditorStatus Parameters(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.Doc(*r);
        auto *block = doc ? CKBehavior::Cast(doc->Resolve(self.block)) : nullptr;
        if (!doc || doc->Token() != self.document || !block || !self.edit)
            return EDITOR_NO_CHANGE;
        EditorField inputs{"inputs", EDITOR_RECORD};
        for (const auto ref : self.parameters) {
            auto *parameter = doc->Resolve(ref, CKCID_PARAMETER);
            if (!parameter)
                return EDITOR_STALE;
            EditorField row{std::to_string(inputs.fields.size()), EDITOR_RECORD};
            row.fields = {
                {"object", EDITOR_OBJECT, ref},
                {"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(parameter->GetName() ? parameter->GetName() : "")}};
            inputs.fields.push_back(std::move(row));
        }
        return Write(sink, {{"drop", EDITOR_INTEGER_VALUE, {}, {}, self.drop},
                            {"block", EDITOR_OBJECT, self.block},
                            {"script", EDITOR_OBJECT, self.script},
                            {"title",
                             EDITOR_TEXT_VALUE,
                             {},
                             AnsiToUtf8(block->GetName() ? block->GetName() : "") + " - Edit Parameters"},
                            std::move(inputs)});
    }
    static EditorStatus Finish(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.Doc(*r);
        if (!doc || doc->Token() != self.document || !self.edit || Arg(*r, "drop").integer != self.drop)
            return EDITOR_STALE;
        if (Arg(*r, "accept").integer > 1)
            return EDITOR_INVALID;
        if (!Arg(*r, "accept").integer) {
            const bool restored = self.edit->Cancel();
            self.Clear();
            return restored ? EDITOR_OK : EDITOR_ERROR;
        }
        const auto &values = Arg(*r, "values").objects;
        auto *block = CKBehavior::Cast(doc->Resolve(self.block));
        if (values.count != self.parameters.size() || !block)
            return EDITOR_STALE;
        const auto inputs = Inputs(block);
        if (inputs.size() != self.parameters.size())
            return EDITOR_STALE;
        for (uint32_t i = 0; i < values.count; ++i) {
            auto *target = CKParameter::Cast(doc->Resolve(self.parameters[i]));
            auto *source = CKParameter::Cast(doc->Resolve(values.data[i]));
            if (!target || !source || target != inputs[i] || target->GetGUID() != source->GetGUID())
                return EDITOR_STALE;
        }
        for (uint32_t i = 0; i < values.count; ++i) {
            const auto copied = CopyParameterValue(*doc, self.parameters[i], values.data[i], self.script.object);
            if (!copied.ok && !copied.Unchanged()) {
                const auto failed = self.edit->Fail(copied.message);
                self.Clear();
                return Result(failed, sink);
            }
        }
        const auto result = self.edit->Commit();
        self.Clear();
        return Result(result, sink);
    }
};

LayoutAuthoring::LayoutAuthoring(EditorApplication &editor, std::function<Document *()> current,
                                 std::function<LayoutView &()> view, ObjectSelection &selection,
                                 const Preferences &preferences, std::function<SchematicNavigation &()> navigation)
    : m_Data(std::make_unique<Data>(
          Data{*this, editor, std::move(current), std::move(view), selection, preferences, std::move(navigation)})) {}
LayoutAuthoring::~LayoutAuthoring() = default;
void LayoutAuthoring::DocumentClosing() { m_Data->Close(true); }
void LayoutAuthoring::Poll() {
    auto &d = *m_Data;
    auto *doc = d.current();
    if (d.edit &&
        (!doc || doc->Token() != d.document || !doc->Resolve(d.block) || (d.source && !d.editor.Scope(d.source).view)))
        d.Close(false);
}
const std::vector<ProviderChoice> &LayoutAuthoring::DefaultProviders() {
    static const std::vector<ProviderChoice> providers{
        {"query", "layout.creation", "builtin.layout-authoring"},
        {"query", "layout.drop-target", "builtin.layout-authoring"},
        {"query", "layout.drop-parameters", "builtin.layout-authoring"},
        {"command", "layout.highlight-drop", "builtin.layout-authoring"},
        {"command", "layout.replace-material", "builtin.layout-authoring"},
        {"command", "scene.drop-behavior", "builtin.layout-authoring"},
        {"command", "layout.finish-drop", "builtin.layout-authoring"}};
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
    const EditorParameter parameterFields[] = {{EDITOR_TEXT("drop"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("block"), EDITOR_OBJECT},
                                               {EDITOR_TEXT("script"), EDITOR_OBJECT},
                                               {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE},
                                               {EDITOR_TEXT("inputs"), EDITOR_RECORD}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("layout.creation"), EDITOR_TEXT("builtin.layout-authoring"), nullptr, 0,
         Data::Creation, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, creationFields, 6},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("layout.drop-target"), EDITOR_TEXT("builtin.layout-authoring"), pickArgs,
         6, Data::Pick, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, pickFields, 7},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("layout.drop-parameters"), EDITOR_TEXT("builtin.layout-authoring"),
         nullptr, 0, Data::Parameters, m_Data.get(), EDITOR_QUERY_VERSIONED, EDITOR_DOCUMENT, parameterFields, 5,
         [](void *data, const EditorHost *) { return static_cast<Data *>(data)->revision; }}};
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT, 0, 1};
    const EditorParameter material[] = {
        {EDITOR_TEXT("mesh"), EDITOR_OBJECT}, {EDITOR_TEXT("from"), EDITOR_OBJECT}, {EDITOR_TEXT("to"), EDITOR_OBJECT}};
    const EditorParameter drop[] = {
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, 1},     {EDITOR_TEXT("prototype"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("prototypes"), EDITOR_INTEGER_LIST, 1}, {EDITOR_TEXT("ask"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("position"), EDITOR_REAL_LIST, 1},      {EDITOR_TEXT("ctrl"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter finish[] = {{EDITOR_TEXT("drop"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("accept"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("values"), EDITOR_OBJECT_LIST, 1}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.highlight-drop"), EDITOR_TEXT("builtin.layout-authoring"),
         EDITOR_TEXT("Drop Target"), &target, 1, Data::Highlight, m_Data.get(), EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.replace-material"), EDITOR_TEXT("builtin.layout-authoring"),
         EDITOR_TEXT("Replace Material"), material, 3, Data::Material, m_Data.get(), EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("scene.drop-behavior"), EDITOR_TEXT("builtin.layout-authoring"),
         EDITOR_TEXT("Drop Behavior"), drop, 6, Data::Drop, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.finish-drop"), EDITOR_TEXT("builtin.layout-authoring"),
         EDITOR_TEXT("Drop Behavior"), finish, 3, Data::Finish, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("layout.authoring");
    module.commands = commands;
    module.commandCount = 4;
    module.queries = queries;
    module.queryCount = 3;
    return m_Data->editor.Register(module, error);
}
} // namespace vdev

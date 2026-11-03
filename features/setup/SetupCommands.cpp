#include "features/setup/SetupCommands.h"
#include "features/setup/SetupQueries.h"
#include "features/setup/SetupViewData.h"
#include "core/level/SceneCommands.h"
#include <algorithm>
#include <cstring>
#include "core/document/Document.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/ValueCommands.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/ObjectProperties.h"
#include "core/level/SetupCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/GridCompat.h"
#include "core/runtime/SdkCompat.h"
#include "core/runtime/TextEncoding.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <type_traits>

namespace vdev {
namespace {
const EditorValue *Arg(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key)
            return request.values + i;
    return nullptr;
}
template <class T> EditorField Field(T value) {
    EditorField field;
    field.key = "value";
    if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int>) {
        field.type = EDITOR_INTEGER_VALUE;
        field.integer = uint64_t(int64_t(value));
    } else if constexpr (std::is_same_v<T, float>) {
        field.type = EDITOR_REAL_VALUE;
        field.real = value;
    } else if constexpr (std::is_same_v<T, std::string>) {
        field.type = EDITOR_TEXT_VALUE;
        field.text = AnsiToUtf8(value);
    } else {
        field.type = EDITOR_REAL_LIST;
        if constexpr (std::is_same_v<T, VxColor>)
            field.reals = {value.r, value.g, value.b, value.a};
        else if constexpr (std::is_same_v<T, VxVector>)
            field.reals = {value.x, value.y, value.z};
        else if constexpr (std::is_same_v<T, VxRect>)
            field.reals = {value.left, value.top, value.right, value.bottom};
        else
            field.reals = {value.x, value.y};
    }
    return field;
}
template <class T> bool Parse(const EditorValue &field, T &value) {
    if constexpr (std::is_same_v<T, bool>) {
        if (field.type != EDITOR_INTEGER_VALUE || field.integer > 1)
            return false;
        value = field.integer != 0;
    } else if constexpr (std::is_same_v<T, int>) {
        if (field.type != EDITOR_INTEGER_VALUE || int64_t(field.integer) < std::numeric_limits<int>::min() ||
            int64_t(field.integer) > std::numeric_limits<int>::max())
            return false;
        value = int(int64_t(field.integer));
    } else if constexpr (std::is_same_v<T, float>) {
        if (field.type != EDITOR_REAL_VALUE || !std::isfinite(field.real) ||
            std::fabs(field.real) > std::numeric_limits<float>::max())
            return false;
        value = float(field.real);
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (field.type != EDITOR_TEXT_VALUE)
            return false;
        std::string text(field.text.data ? field.text.data : "", field.text.size);
        if (text.find('\0') != std::string::npos)
            return false;
        value = Utf8ToAnsi(text);
    } else {
        constexpr uint32_t count = (std::is_same_v<T, VxColor> || std::is_same_v<T, VxRect>) ? 4
                                   : std::is_same_v<T, VxVector>                             ? 3
                                                                                             : 2;
        if (field.type != EDITOR_REAL_LIST || field.reals.count != count || !field.reals.data)
            return false;
        for (uint32_t i = 0; i < count; ++i)
            if (!std::isfinite(field.reals.data[i]) ||
                std::fabs(field.reals.data[i]) > std::numeric_limits<float>::max())
                return false;
        if constexpr (std::is_same_v<T, VxColor>)
            value = VxColor(float(field.reals.data[0]), float(field.reals.data[1]), float(field.reals.data[2]),
                            float(field.reals.data[3]));
        else if constexpr (std::is_same_v<T, VxVector>)
            value = VxVector(float(field.reals.data[0]), float(field.reals.data[1]), float(field.reals.data[2]));
        else if constexpr (std::is_same_v<T, VxRect>)
            value = VxRect(float(field.reals.data[0]), float(field.reals.data[1]), float(field.reals.data[2]),
                           float(field.reals.data[3]));
        else
            value = Vx2DVector(float(field.reals.data[0]), float(field.reals.data[1]));
    }
    return true;
}
EditorStatus Result(const EditResult &result, EditorSink sink) {
    if (!result.ok && !result.Unchanged() && sink.write) {
        const auto message = AnsiToUtf8(result.message);
        const EditorValue field{EditorLiteral("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)};
        sink.write(sink.data, &field, 1);
    }
    return result.ok                                ? EDITOR_OK
           : result.Unchanged()                     ? EDITOR_NO_CHANGE
           : result.Status() == EditStatus::Refused ? EDITOR_REFUSED
                                                    : EDITOR_ERROR;
}
bool IsFrame(CKObject *object) {
    auto *entity = CK3dEntity::Cast(object);
    if (!entity)
        return false;
    if (CKPlace::Cast(object))
        return true;
    const auto cid = object->GetClassID();
    return (cid == CKCID_3DENTITY || cid == CKCID_3DOBJECT) &&
           (entity->GetFlags() & (CK_3DENTITY_FRAME | CK_3DENTITY_PORTAL));
}
std::string SetupName(CKObject *object) {
    return object && object->GetName() && *object->GetName() ? AnsiToUtf8(object->GetName()) : "<Unnamed>";
}
SetupKind KindOf(CKObject *object) {
    if (!object)
        return SetupKind::Other;
    const CK_CLASSID cid = object->GetClassID();
    if (cid == CKCID_LEVEL || CKIsChildClassOf(cid, CKCID_SCENE))
        return SetupKind::Scene;
    if (CKIsChildClassOf(cid, CKCID_GRID))
        return SetupKind::Grid;
    if (CKIsChildClassOf(cid, CKCID_CURVE))
        return SetupKind::Curve;
    if (CKIsChildClassOf(cid, CKCID_CHARACTER))
        return SetupKind::Character;
    if (CKIsChildClassOf(cid, CKCID_SPRITE3D))
        return SetupKind::Sprite3D;
    if (CKIsChildClassOf(cid, CKCID_SPRITETEXT))
        return SetupKind::SpriteText;
    if (CKIsChildClassOf(cid, CKCID_SPRITE))
        return SetupKind::Sprite2D;
    if (CKIsChildClassOf(cid, CKCID_ANIMATION))
        return SetupKind::Animation;
    if (CKIsChildClassOf(cid, CKCID_OBJECTANIMATION))
        return SetupKind::ObjectAnimation;
    if (CKIsChildClassOf(cid, CKCID_MIDISOUND))
        return SetupKind::MidiSound;
    if (CKIsChildClassOf(cid, CKCID_CAMERA))
        return SetupKind::Camera;
    if (CKIsChildClassOf(cid, CKCID_LIGHT))
        return SetupKind::Light;
    if (CKIsChildClassOf(cid, CKCID_3DENTITY))
        return SetupKind::Entity3D;
    if (CKIsChildClassOf(cid, CKCID_2DENTITY))
        return SetupKind::Entity2D;
    if (CKIsChildClassOf(cid, CKCID_MATERIAL))
        return SetupKind::Material;
    if (CKIsChildClassOf(cid, CKCID_MESH))
        return SetupKind::Mesh;
    if (CKIsChildClassOf(cid, CKCID_TEXTURE))
        return SetupKind::Texture;
    if (CKIsChildClassOf(cid, CKCID_WAVESOUND))
        return SetupKind::Sound;
    if (CKIsChildClassOf(cid, CKCID_DATAARRAY))
        return SetupKind::Array;
    if (CKIsChildClassOf(cid, CKCID_GROUP))
        return SetupKind::Group;
    if (CKIsChildClassOf(cid, CKCID_KINEMATICCHAIN))
        return SetupKind::KinematicChain;
    if (CKIsChildClassOf(cid, CKCID_LAYER))
        return SetupKind::Layer;
    if (CKIsChildClassOf(cid, CKCID_PARAMETER))
        return SetupKind::Parameter;
    if (CKIsChildClassOf(cid, CKCID_PARAMETERIN))
        return SetupKind::ParameterIn;
    if (CKIsChildClassOf(cid, CKCID_PARAMETEROPERATION))
        return SetupKind::Operation;
    if (CKIsChildClassOf(cid, CKCID_BEHAVIOR))
        return SetupKind::Behavior;
    if (CKIsChildClassOf(cid, CKCID_BEHAVIORIO))
        return SetupKind::BehaviorIO;
    if (CKIsChildClassOf(cid, CKCID_BEHAVIORLINK))
        return SetupKind::BehaviorLink;
    if (CKIsChildClassOf(cid, CKCID_SYNCHRO))
        return SetupKind::Synchro;
    if (CKIsChildClassOf(cid, CKCID_STATE))
        return SetupKind::State;
    if (CKIsChildClassOf(cid, CKCID_CRITICALSECTION))
        return SetupKind::CriticalSection;
    if (CKIsChildClassOf(cid, CKCID_INTERFACEOBJECTMANAGER))
        return SetupKind::InterfaceObjects;
    return SetupKind::Other;
}

std::string TitleOf(CKObject *object) {
    // Dev titles: "3D Object Setup", "Target Camera Setup", "Point Light Setup", ...
    if (CKLight *light = CKLight::Cast(object)) {
        const VXLIGHT_TYPE type = light->GetType();
        const char *kind = type == VX_LIGHTSPOT ? "Spot" : type == VX_LIGHTDIREC ? "Directional" : "Point";
        return std::string(CKTargetLight::Cast(object) ? "Target " : "") + kind + " Light Setup";
    }
    const CK_CLASSID cid = object ? object->GetClassID() : 0;
    if (cid == CKCID_3DENTITY && (CK3dEntity::Cast(object)->GetFlags() & CK_3DENTITY_PORTAL))
        return "Portal Setup";
    if (IsFrame(object))
        return "3D Frame Setup";
    if (CKBehavior *behavior = CKBehavior::Cast(object))
        return behavior->GetType() == CKBEHAVIORTYPE_SCRIPT     ? "Script Setup"
               : behavior->GetType() == CKBEHAVIORTYPE_BEHAVIOR ? "Behavior Graph Setup"
                                                                : "Building Block Setup";
    if (CKParameterLocal *local = CKParameterLocal::Cast(object))
        return local->IsMyselfParameter() ? "This Parameter Setup" : "Local Parameter Setup";
    switch (cid) {
    case CKCID_PLACE:
        return "Place Setup";
    case CKCID_BODYPART:
        return "Body Part Setup";
    case CKCID_3DOBJECT:
    case CKCID_3DENTITY:
        return "3D Object Setup";
    case CKCID_2DENTITY:
        return "2D Frame Setup";
    case CKCID_TARGETCAMERA:
        return "Target Camera Setup";
    case CKCID_CAMERA:
        return "Camera Setup";
    case CKCID_CURVE:
        return "Curve Setup";
    case CKCID_CHARACTER:
        return "Character Setup";
    case CKCID_WAVESOUND:
        return "Sound Setup";
    case CKCID_DATAARRAY:
        return "Array Setup";
    case CKCID_GROUP:
        return "Group Setup";
    case CKCID_LEVEL:
        return "Level Setup";
    case CKCID_SPRITE3D:
        return "3D Sprite Setup";
    case CKCID_SPRITE:
        return "2D Sprite Setup";
    case CKCID_SCENE:
        return "Scene Setup";
    case CKCID_SPRITETEXT:
        return "Sprite Text Setup";
    case CKCID_ANIMATION:
        return "Animation Setup";
    case CKCID_KEYEDANIMATION:
        return "Keyed Animation Setup";
    case CKCID_OBJECTANIMATION:
        return "Object Animation Setup";
    case CKCID_MIDISOUND:
        return "Midi Sound Setup";
    case CKCID_KINEMATICCHAIN:
        return "Kinematic Chain Setup";
    case CKCID_LAYER:
        return "Layer Setup";
    case CKCID_PARAMETEROUT:
        return "Output Parameter Setup";
    case CKCID_PARAMETER:
        return "Parameter Setup";
    case CKCID_PARAMETERIN:
        return "Input Parameter Setup";
    case CKCID_PARAMETEROPERATION:
        return "Parameter Operation Setup";
    case CKCID_BEHAVIORIO:
        return "Behavior IO Setup";
    case CKCID_BEHAVIORLINK:
        return "Behavior Link Setup";
    case CKCID_SYNCHRO:
        return "Synchro Setup";
    case CKCID_STATE:
        return "State Setup";
    case CKCID_CRITICALSECTION:
        return "Critical Section Setup";
    case CKCID_INTERFACEOBJECTMANAGER:
        return "Interface Objects Setup";
    default:
        break;
    }
    const char *name = cid ? CKClassIDToString(cid) : nullptr;
    return std::string(name ? name : "Object") + " Setup";
}

} // namespace
struct SetupCommands::Data {
    enum class Storage { Object, Scene, Bitmap, Grid, Script };
    struct Entry {
        Data *owner;
        std::string id, label, query, command, editor;
        CK_CLASSID cid;
        EditorValueType type;
        std::function<EditorField(Document &, CKObject *, const EditorRequest &)> read;
        std::function<bool(Document &, CKObject *, const EditorRequest &, const EditorValue &)> write;
        Storage storage = Storage::Object;
        bool live = true;
        const char *property = nullptr;
        bool bitmapProperty = false;
        GridProperty registry = GridProperty::None;
    };
    std::function<Document *()> current;
    std::vector<std::unique_ptr<Entry>> entries;
    struct Preview {
        DocumentToken document;
        ObjectRef target;
        ObjectsGesture objects;
        SceneEnvironment environment;
        std::vector<CKObjectHandle> environmentObjects;
        BitmapFormat bitmap;
        bool valueOnly = false;
        std::vector<ObjectPropertyState> values;
        int index = -1;
        GridPropertyValue grid;
    };
    explicit Data(std::function<Document *()> current) : current(std::move(current)) {}
    static Document *DocumentFor(void *data, const EditorRequest &request) {
        auto *doc = static_cast<Data *>(data)->current();
        return doc && doc->Token() == request.scope.document ? doc : nullptr;
    }
    static CKObject *SetupTarget(Document *doc, const EditorRequest &request) {
        const auto *target = Arg(request, "target");
        return doc && target ? doc->Resolve(target->object) : nullptr;
    }
    static EditorStatus OpenTarget(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto *doc = DocumentFor(data, *request);
        auto *object = SetupTarget(doc, *request);
        if (!object)
            return EDITOR_STALE;
        int point = -1;
        if (auto *curvePoint = CKCurvePoint::Cast(object))
            if (auto *curve = curvePoint->GetCurve()) {
                for (int i = 0; i < curve->GetControlPointCount(); ++i)
                    if (curve->GetControlPoint(i) == curvePoint)
                        point = i;
                object = curve;
            }
        if (auto *entity = CK3dEntity::Cast(object)) {
            const auto flags = entity->GetFlags();
            const CK_CLASSID owner = flags & CK_3DENTITY_TARGETCAMERA  ? CKCID_TARGETCAMERA
                                     : flags & CK_3DENTITY_TARGETLIGHT ? CKCID_TARGETLIGHT
                                                                       : 0;
            if (owner)
                for (auto *candidate : doc->LiveObjects(owner, true)) {
                    auto *camera = CKTargetCamera::Cast(candidate);
                    auto *light = CKTargetLight::Cast(candidate);
                    if ((camera && camera->GetTarget() == entity) || (light && light->GetTarget() == entity)) {
                        object = candidate;
                        break;
                    }
                }
        }
        const EditorValue fields[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, doc->Reference(object->GetID())},
                                      {EDITOR_TEXT("point"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(point))}};
        return sink.write(sink.data, fields, 2);
    }
    static EditorStatus Page(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto *doc = DocumentFor(data, *request);
        auto *object = SetupTarget(doc, *request);
        if (!object)
            return EDITOR_STALE;
        auto *context = doc->Context();
        auto *level = context->GetCurrentLevel();
        auto *current = context->GetCurrentScene();
        auto *sceneObject = CKSceneObject::Cast(object);
        auto *scene = InitialConditionsScene(context);
        const bool here = scene && sceneObject && scene->IsObjectHere(sceneObject);
        const auto kind = KindOf(object);
        auto *curve = CKCurve::Cast(object);
        const auto *index = Arg(*request, "point");
        const int64_t point = index ? int64_t(index->integer) : -1;
        if (point < -1 || point > INT32_MAX)
            return EDITOR_INVALID;
        auto *selected =
            curve && point >= 0 && point < curve->GetControlPointCount() ? curve->GetControlPoint(int(point)) : nullptr;
        SetupFileKind fileKind = SetupFileKind::None;
        const char *filename = nullptr;
        if (auto *texture = CKTexture::Cast(object)) {
            fileKind = SetupFileKind::Texture;
            filename = texture->GetSlotFileName(texture->GetCurrentSlot());
        } else if (auto *sprite = CKSprite::Cast(object); sprite && !CKSpriteText::Cast(object)) {
            fileKind = SetupFileKind::Sprite;
            filename = sprite->GetSlotFileName(sprite->GetCurrentSlot());
        } else if (auto *sound = CKWaveSound::Cast(object)) {
            fileKind = SetupFileKind::Wave;
            filename = sound->GetSoundFileName();
        } else if (auto *midi = CKMidiSound::Cast(object)) {
            fileKind = SetupFileKind::Midi;
            filename = midi->GetSoundFileName();
        }
        CKGrid *grid = nullptr;
        int layer = -1;
        if (auto *value = CKLayer::Cast(object)) {
            grid = CKGrid::Cast(context->GetObject(value->GetOwner()));
            for (int i = 0; grid && i < grid->GetLayerCount(); ++i)
                if (grid->GetLayerByIndex(i) == value) {
                    layer = i;
                    break;
                }
        }
        const auto title = TitleOf(object), name = SetupName(object), file = AnsiToUtf8(filename ? filename : "");
        const bool behavioral = CKBeObject::Cast(object) != nullptr;
        const EditorValue fields[] = {
            {EDITOR_TEXT("target"), EDITOR_OBJECT, doc->Reference(object->GetID())},
            {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(kind)},
            {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(object->GetClassID())},
            {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE, {}, EditorText(title)},
            {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, {}, EditorText(name)},
            {EDITOR_TEXT("point"), EDITOR_OBJECT, doc->Reference(selected ? selected->GetID() : 0)},
            {EDITOR_TEXT("grid"), EDITOR_OBJECT, doc->Reference(grid ? grid->GetID() : 0)},
            {EDITOR_TEXT("layer"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(layer))},
            {EDITOR_TEXT("file-kind"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(fileKind)},
            {EDITOR_TEXT("filename"), EDITOR_TEXT_VALUE, {}, EditorText(file)},
            {EDITOR_TEXT("behavioral"), EDITOR_INTEGER_VALUE, {}, {}, behavioral},
            {EDITOR_TEXT("frame"), EDITOR_INTEGER_VALUE, {}, {}, IsFrame(object)},
            {EDITOR_TEXT("attribute-drop"), EDITOR_INTEGER_VALUE, {}, {}, behavioral && !CKBehavior::Cast(object)},
            {EDITOR_TEXT("scene-scoped"),
             EDITOR_INTEGER_VALUE,
             {},
             {},
             sceneObject && level && !CKScene::Cast(object) && !CKPlace::Cast(object) && !CKLevel::Cast(object)},
            {EDITOR_TEXT("current-scene"), EDITOR_INTEGER_VALUE, {}, {}, current != nullptr},
            {EDITOR_TEXT("in-current-scene"),
             EDITOR_INTEGER_VALUE,
             {},
             {},
             current && sceneObject && sceneObject->IsInScene(current)},
            {EDITOR_TEXT("initial-visible"),
             EDITOR_INTEGER_VALUE,
             {},
             {},
             kind != SetupKind::Scene && !CKPlace::Cast(object) && sceneObject},
            {EDITOR_TEXT("initial-here"), EDITOR_INTEGER_VALUE, {}, {}, here},
            {EDITOR_TEXT("initial-value"),
             EDITOR_INTEGER_VALUE,
             {},
             {},
             here && scene->GetObjectInitialValue(sceneObject)}};
        return sink.write(sink.data, fields, uint32_t(std::size(fields)));
    }
    static EditorStatus SetupObjects(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto *doc = DocumentFor(data, *request);
        auto *object = SetupTarget(doc, *request);
        if (!object)
            return EDITOR_STALE;
        const auto *filter = Arg(*request, "current-scene");
        if (filter && filter->integer > 1)
            return EDITOR_INVALID;
        auto *level = doc->Context()->GetCurrentLevel();
        auto *current = doc->Context()->GetCurrentScene();
        auto *sceneObject = CKSceneObject::Cast(object);
        const bool scoped =
            sceneObject && level && !CKScene::Cast(object) && !CKPlace::Cast(object) && !CKLevel::Cast(object);
        auto *scene = level ? level->GetLevelScene() : nullptr;
        if (scoped && filter && filter->integer && current && sceneObject->IsInScene(current))
            scene = current;
        auto objects = doc->LiveObjects(object->GetClassID(), false);
        objects.erase(std::remove_if(objects.begin(), objects.end(),
                                     [&](CKObject *other) {
                                         auto *candidate = CKSceneObject::Cast(other);
                                         return other->IsPrivate() || other->IsDynamic() ||
                                                (scoped && scene && other != object && candidate &&
                                                 !candidate->IsInScene(scene));
                                     }),
                      objects.end());
        std::sort(objects.begin(), objects.end(), [](CKObject *a, CKObject *b) {
            return _stricmp(a->GetName() ? a->GetName() : "", b->GetName() ? b->GetName() : "") < 0;
        });
        std::vector<ObjectRef> refs;
        std::vector<std::string> names;
        for (auto *other : objects) {
            refs.push_back(doc->Reference(other->GetID()));
            names.push_back(SetupName(other));
        }
        std::vector<EditorString> texts;
        for (const auto &name : names)
            texts.push_back(EditorText(name));
        const EditorValue fields[] = {
            {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {refs.data(), uint32_t(refs.size())}},
            {EDITOR_TEXT("names"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {texts.data(), uint32_t(texts.size())}}};
        return sink.write(sink.data, fields, 2);
    }
    static EditorStatus Transform(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto *doc = DocumentFor(data, *request);
        auto *entity = CK3dEntity::Cast(SetupTarget(doc, *request));
        if (!entity)
            return EDITOR_STALE;
        double world[16], local[16], box[6];
        const auto &wm = entity->GetWorldMatrix();
        const auto &lm = entity->GetLocalMatrix();
        const auto bounds = entity->GetBoundingBox(FALSE);
        for (int i = 0; i < 16; ++i) {
            world[i] = wm[i / 4][i % 4];
            local[i] = lm[i / 4][i % 4];
        }
        for (int i = 0; i < 3; ++i) {
            box[i] = bounds.Min[i];
            box[i + 3] = bounds.Max[i];
        }
        EditorValue fields[] = {{EDITOR_TEXT("world"), EDITOR_REAL_LIST},
                                {EDITOR_TEXT("local"), EDITOR_REAL_LIST},
                                {EDITOR_TEXT("box"), EDITOR_REAL_LIST}};
        fields[0].reals = {world, 16};
        fields[1].reals = {local, 16};
        fields[2].reals = {box, 6};
        return sink.write(sink.data, fields, 3);
    }
    template <class T> void Add(const char *id, const ObjectProperty<T> &property) {
        // Sound files use native filesystem paths and deferred loading in AddSpecial.
        if (std::strcmp(id, "sound.file") == 0)
            return;
        auto entry = std::make_unique<Entry>();
        entry->owner = this;
        entry->property = id;
        entry->bitmapProperty = property.cid == CKCID_TEXTURE || property.cid == CKCID_SPRITE;
        entry->id = "setup." + std::string(id);
        entry->label = property.label;
        entry->cid = property.cid;
        entry->type = std::is_same_v<T, CK_ID>                              ? EDITOR_OBJECT
                      : (std::is_same_v<T, bool> || std::is_same_v<T, int>) ? EDITOR_INTEGER_VALUE
                      : std::is_same_v<T, float>                            ? EDITOR_REAL_VALUE
                      : std::is_same_v<T, std::string>                      ? EDITOR_TEXT_VALUE
                                                                            : EDITOR_REAL_LIST;
        entry->editor = std::is_same_v<T, CK_ID>         ? "value.object"
                        : std::is_same_v<T, bool>        ? "value.boolean"
                        : std::is_same_v<T, int>         ? "value.integer"
                        : std::is_same_v<T, float>       ? "value.real"
                        : std::is_same_v<T, std::string> ? "value.string"
                        : std::is_same_v<T, VxColor>     ? "value.color"
                        : std::is_same_v<T, VxVector>    ? "value.vector3"
                        : std::is_same_v<T, VxRect>      ? "value.rect"
                                                         : "value.vector2";
        entry->read = [&property](Document &doc, CKObject *o, const EditorRequest &) {
            if constexpr (std::is_same_v<T, CK_ID>)
                return EditorField{"value", EDITOR_OBJECT, doc.Reference(property.get(o))};
            else
                return Field(property.get(o));
        };
        entry->write = [&property](Document &doc, CKObject *o, const EditorRequest &, const EditorValue &field) {
            if constexpr (std::is_same_v<T, CK_ID>) {
                if (field.type != EDITOR_OBJECT ||
                    (field.object && !doc.Resolve(field.object, property.referenceClass)))
                    return false;
                const auto id = doc.NativeId(field.object);
                if (id == o->GetID())
                    return false;
                property.set(o, id);
                return true;
            } else {
                T value{};
                if (!Parse(field, value))
                    return false;
                property.set(o, value);
                return true;
            }
        };
        entries.push_back(std::move(entry));
    }
    using ReadValue = std::function<EditorField(Document &, CKObject *, const EditorRequest &)>;
    using WriteValue = std::function<bool(Document &, CKObject *, const EditorRequest &, const EditorValue &)>;
    Entry &Extra(const char *id, const char *label, CK_CLASSID cid, EditorValueType type, const char *editor,
                 ReadValue read, WriteValue write, Storage storage = Storage::Object) {
        auto e = std::make_unique<Entry>();
        e->owner = this;
        e->id = "setup." + std::string(id);
        e->label = label;
        e->cid = cid;
        e->type = type;
        e->editor = editor;
        e->read = std::move(read);
        e->write = std::move(write);
        e->storage = storage;
        entries.push_back(std::move(e));
        return *entries.back();
    }
    static int Index(const EditorRequest &r) {
        const auto *v = Arg(r, "indices");
        return v && v->integers.count == 1 && v->integers.data[0] <= uint64_t(std::numeric_limits<int>::max())
                   ? int(v->integers.data[0])
                   : -1;
    }
    template <class T, class Get, class Set> void Scene(const char *id, const char *label, Get get, Set set) {
        Extra(
            id, label, CKCID_SCENE,
            (std::is_same_v<T, bool> || std::is_same_v<T, int>) ? EDITOR_INTEGER_VALUE
            : std::is_same_v<T, float>                          ? EDITOR_REAL_VALUE
                                                                : EDITOR_REAL_LIST,
            std::is_same_v<T, bool>    ? "value.boolean"
            : std::is_same_v<T, int>   ? "value.integer"
            : std::is_same_v<T, float> ? "value.real"
                                       : "value.color",
            [get](Document &, CKObject *o, const EditorRequest &) { return Field(get(CKScene::Cast(o))); },
            [set](Document &, CKObject *o, const EditorRequest &, const EditorValue &v) {
                T value{};
                if (!Parse(v, value))
                    return false;
                set(CKScene::Cast(o), value);
                return true;
            },
            Storage::Scene);
    }
    void AddSpecial() {
        Scene<bool>(
            "scene.use-environment", "Use settings at Scene Start",
            [](CKScene *s) { return s->EnvironmentSettings() != FALSE; },
            [](CKScene *s, bool v) { s->UseEnvironmentSettings(v); });
        Scene<int>(
            "scene.fog-mode", "Fog Type", [](CKScene *s) { return int(s->GetFogMode()); },
            [](CKScene *s, int v) { s->SetFogMode(VXFOG_MODE(v)); });
        Scene<float>(
            "scene.fog-start", "Fog Start", [](CKScene *s) { return s->GetFogStart(); },
            [](CKScene *s, float v) { s->SetFogStart(v); });
        Scene<float>(
            "scene.fog-end", "Fog End", [](CKScene *s) { return s->GetFogEnd(); },
            [](CKScene *s, float v) { s->SetFogEnd(v); });
        Scene<float>(
            "scene.fog-density", "Fog Density", [](CKScene *s) { return s->GetFogDensity(); },
            [](CKScene *s, float v) { s->SetFogDensity(v); });
        const char *colors[] = {"ambient", "fog", "background"};
        const char *colorLabels[] = {"Ambient", "Fog", "Background"};
        CKDWORD (CKScene::*getColors[])() = {&CKScene::GetAmbientLight, &CKScene::GetFogColor,
                                             &CKScene::GetBackgroundColor};
        void (CKScene::*setColors[])(CKDWORD) = {&CKScene::SetAmbientLight, &CKScene::SetFogColor,
                                                 &CKScene::SetBackgroundColor};
        for (unsigned i = 0; i < 3; ++i) {
            const auto id = "scene." + std::string(colors[i]);
            auto get = getColors[i];
            auto set = setColors[i];
            Scene<VxColor>(
                id.c_str(), colorLabels[i], [get](CKScene *s) { return VxColor((s->*get)()); },
                [set](CKScene *s, VxColor v) { (s->*set)(v.GetRGBA()); });
        }
        const char *refs[] = {"background-image", "starting-camera"};
        const char *refLabels[] = {"Background Image", "Starting Camera"};
        for (unsigned i = 0; i < 2; ++i)
            Extra(("scene." + std::string(refs[i])).c_str(), refLabels[i], CKCID_SCENE, EDITOR_OBJECT, "value.object",
                  [i](Document &doc, CKObject *o, const EditorRequest &) {
                      auto *s = CKScene::Cast(o);
                      CKObject *value = i ? static_cast<CKObject *>(s->GetStartingCamera())
                                          : static_cast<CKObject *>(s->GetBackgroundTexture());
                      return EditorField{"value", EDITOR_OBJECT, value ? doc.Reference(value->GetID()) : ObjectRef{}};
                  },
                  [i](Document &doc, CKObject *o, const EditorRequest &, const EditorValue &v) {
                      if (v.type != EDITOR_OBJECT)
                          return false;
                      auto *value = v.object ? doc.Resolve(v.object, i ? CKCID_CAMERA : CKCID_TEXTURE) : nullptr;
                      if (v.object && !value)
                          return false;
                      if (i)
                          CKScene::Cast(o)->SetStartingCamera(CKCamera::Cast(value));
                      else
                          CKScene::Cast(o)->SetBackgroundTexture(CKTexture::Cast(value));
                      return true;
                  },
                  Storage::Scene);
        Extra(
            "spritetext.font", "Font", CKCID_SPRITETEXT, EDITOR_RECORD, "value.structure",
            [](Document &, CKObject *o, const EditorRequest &) {
                SpriteTextFont font;
                ReadSpriteTextFont(CKSpriteText::Cast(o), &font);
                EditorField v{"value", EDITOR_RECORD};
                v.fields = {{"face", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(font.face)},
                            {"size", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(font.size))},
                            {"weight", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(font.weight))},
                            {"italic", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(font.italic)},
                            {"underline", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(font.underline)}};
                return v;
            },
            [](Document &, CKObject *o, const EditorRequest &, const EditorValue &v) {
                if (v.type != EDITOR_RECORD || v.fields.count != 5)
                    return false;
                EditorRequest r{{}, v.fields.data, v.fields.count};
                const auto *face = Arg(r, "face"), *size = Arg(r, "size"), *weight = Arg(r, "weight"),
                           *italic = Arg(r, "italic"), *underline = Arg(r, "underline");
                SpriteTextFont f;
                if (!face || !size || !weight || !italic || !underline || !Parse(*face, f.face) ||
                    !Parse(*size, f.size) || !Parse(*weight, f.weight) || !Parse(*italic, f.italic) ||
                    !Parse(*underline, f.underline))
                    return false;
                CKSpriteText::Cast(o)->SetFont(f.face.c_str(), f.size, f.weight, f.italic, f.underline);
                return true;
            });
        Extra(
            "entity3d.meshes", "Object Meshes", CKCID_3DENTITY, EDITOR_OBJECT_LIST, "value.object-array",
            [](Document &doc, CKObject *o, const EditorRequest &) {
                auto *e = CK3dEntity::Cast(o);
                EditorField f{"value", EDITOR_OBJECT_LIST};
                for (int i = 0; i < e->GetMeshCount(); ++i)
                    if (auto *mesh = e->GetMesh(i))
                        f.objects.push_back(doc.Reference(mesh->GetID()));
                return f;
            },
            [](Document &doc, CKObject *o, const EditorRequest &, const EditorValue &v) {
                if (v.type != EDITOR_OBJECT_LIST)
                    return false;
                auto *e = CK3dEntity::Cast(o);
                std::vector<CKMesh *> meshes;
                for (uint32_t i = 0; i < v.objects.count; ++i) {
                    auto *m = CKMesh::Cast(doc.Resolve(v.objects.data[i], CKCID_MESH));
                    if (!m)
                        return false;
                    meshes.push_back(m);
                }
                for (int i = e->GetMeshCount(); i-- > 0;)
                    if (std::find(meshes.begin(), meshes.end(), e->GetMesh(i)) == meshes.end())
                        e->RemoveMesh(e->GetMesh(i));
                for (auto *m : meshes)
                    e->AddMesh(m);
                return true;
            });
        Extra(
            "mesh.channels", "Channels", CKCID_MESH, EDITOR_OBJECT_LIST, "value.object-array",
            [](Document &doc, CKObject *o, const EditorRequest &) {
                auto *m = CKMesh::Cast(o);
                EditorField f{"value", EDITOR_OBJECT_LIST};
                for (int i = 0; i < m->GetChannelCount(); ++i)
                    if (auto *material = m->GetChannelMaterial(i))
                        f.objects.push_back(doc.Reference(material->GetID()));
                return f;
            },
            [](Document &doc, CKObject *o, const EditorRequest &, const EditorValue &v) {
                if (v.type != EDITOR_OBJECT_LIST)
                    return false;
                auto *m = CKMesh::Cast(o);
                std::vector<CKMaterial *> values;
                for (uint32_t i = 0; i < v.objects.count; ++i) {
                    auto *material = CKMaterial::Cast(doc.Resolve(v.objects.data[i], CKCID_MATERIAL));
                    if (!material)
                        return false;
                    values.push_back(material);
                }
                for (int i = m->GetChannelCount(); i-- > 0;)
                    if (std::find(values.begin(), values.end(), m->GetChannelMaterial(i)) == values.end())
                        m->RemoveChannelByMaterial(m->GetChannelMaterial(i));
                for (auto *material : values) {
                    bool found = false;
                    for (int i = 0; i < m->GetChannelCount(); ++i)
                        found |= m->GetChannelMaterial(i) == material;
                    if (!found && m->AddChannel(material, TRUE) < 0)
                        return false;
                }
                return true;
            });
        const char *channelFields[] = {"active", "lit", "source-blend", "dest-blend"};
        const char *channelLabels[] = {"Channel Active", "Channel Lit", "Channel Source Blend",
                                       "Channel Destination Blend"};
        for (unsigned kind = 0; kind < 4; ++kind)
            Extra(("mesh.channel-" + std::string(channelFields[kind])).c_str(), channelLabels[kind], CKCID_MESH,
                  EDITOR_INTEGER_VALUE, kind < 2 ? "value.boolean" : "value.integer",
                  [kind](Document &, CKObject *o, const EditorRequest &r) {
                      auto *m = CKMesh::Cast(o);
                      const int i = Index(r);
                      if (i < 0 || i >= m->GetChannelCount())
                          throw std::runtime_error("No such channel");
                      int value = kind == 0   ? m->IsChannelActive(i)
                                  : kind == 1 ? m->IsChannelLit(i)
                                  : kind == 2 ? int(m->GetChannelSourceBlend(i))
                                              : int(m->GetChannelDestBlend(i));
                      return Field(value);
                  },
                  [kind](Document &, CKObject *o, const EditorRequest &r, const EditorValue &v) {
                      auto *m = CKMesh::Cast(o);
                      const int i = Index(r);
                      int value = 0;
                      if (i < 0 || i >= m->GetChannelCount() || !Parse(v, value) ||
                          (kind < 2 && (value < 0 || value > 1)))
                          return false;
                      if (kind == 0)
                          m->ActivateChannel(i, value);
                      else if (kind == 1)
                          m->LitChannel(i, value);
                      else if (kind == 2)
                          m->SetChannelSourceBlend(i, VXBLEND_MODE(value));
                      else
                          m->SetChannelDestBlend(i, VXBLEND_MODE(value));
                      return true;
                  });
        Extra(
            "material.effect-texture", "Effect Texture", CKCID_MATERIAL, EDITOR_OBJECT, "value.object",
            [](Document &doc, CKObject *o, const EditorRequest &r) {
                const int index = Index(r);
                if (index < 1 || index > 8)
                    throw std::runtime_error("No such effect texture");
                auto *t = CKMaterial::Cast(o)->GetTexture(index);
                return EditorField{"value", EDITOR_OBJECT, t ? doc.Reference(t->GetID()) : ObjectRef{}};
            },
            [](Document &doc, CKObject *o, const EditorRequest &r, const EditorValue &v) {
                const int i = Index(r);
                auto *t = v.object ? CKTexture::Cast(doc.Resolve(v.object, CKCID_TEXTURE)) : nullptr;
                if (i < 1 || i > 8 || v.type != EDITOR_OBJECT || (v.object && !t))
                    return false;
                CKMaterial::Cast(o)->SetTexture(i, t);
                return true;
            });
        Extra(
            "sound.file", "Change File", CKCID_SOUND, EDITOR_TEXT_VALUE, "value.string",
            [](Document &, CKObject *o, const EditorRequest &) {
                return Field(properties::SoundFile.get(o));
            },
            [](Document &, CKObject *o, const EditorRequest &, const EditorValue &v) {
                if (v.type != EDITOR_TEXT_VALUE)
                    return false;
                std::string utf8(v.text.data ? v.text.data : "", v.text.size);
                if (utf8.find('\0') != std::string::npos)
                    return false;
                std::string file = std::filesystem::u8path(utf8).string();
                if (!CKWaveSound::Cast(o) && !CKMidiSound::Cast(o))
                    return false;
                properties::SoundFile.set(o, std::move(file));
                return true;
            })
            .live = false;
        Extra(
            "behavior.priority", "Set Priority", CKCID_BEHAVIOR, EDITOR_INTEGER_VALUE, "value.integer",
            [](Document &, CKObject *o, const EditorRequest &) { return Field(CKBehavior::Cast(o)->GetPriority()); },
            [](Document &, CKObject *o, const EditorRequest &, const EditorValue &v) {
                int value;
                if (!Parse(v, value))
                    return false;
                CKBehavior::Cast(o)->SetPriority(value);
                return true;
            });
        entries.back()->storage = Storage::Script;
        entries.back()->live = false;
        Extra(
            "behavior-link.delay", "Set Link Delay", CKCID_BEHAVIORLINK, EDITOR_INTEGER_VALUE, "value.integer",
            [](Document &, CKObject *o, const EditorRequest &) {
                return Field(CKBehaviorLink::Cast(o)->GetInitialActivationDelay());
            },
            [](Document &, CKObject *o, const EditorRequest &, const EditorValue &v) {
                int value;
                if (!Parse(v, value) || value < 0 || value >= 32765)
                    return false;
                CKBehaviorLink::Cast(o)->SetInitialActivationDelay(value);
                return true;
            });
        entries.back()->storage = Storage::Script;
        entries.back()->live = false;
        const std::pair<const char *, GridProperty> gridProperties[] = {
            {"grid.layer-name", GridProperty::LayerName},
            {"grid.layer-color", GridProperty::LayerColor},
            {"grid.layer-linker", GridProperty::LayerLinker},
            {"grid.classification-name", GridProperty::ClassificationName}};
        for (const auto &setting : gridProperties) {
            const auto *id = setting.first;
            const auto property = setting.second;
            const bool color = property == GridProperty::LayerColor, boolean = property == GridProperty::LayerLinker;
            const char *label = property == GridProperty::LayerColor    ? "Layer Type Color"
                                : property == GridProperty::LayerLinker ? "Layer Parameter Type"
                                : property == GridProperty::LayerName   ? "Rename Layer Type"
                                                                        : "Rename Classification";
            auto &entry = Extra(
                id, label, CKCID_GRID,
                color     ? EDITOR_REAL_LIST
                : boolean ? EDITOR_INTEGER_VALUE
                          : EDITOR_TEXT_VALUE,
                color     ? "value.color"
                : boolean ? "value.boolean"
                          : "value.string",
                [property](Document &doc, CKObject *, const EditorRequest &r) {
                    GridPropertyValue v;
                    if (!ReadGridProperty(doc, property, Index(r), v))
                        throw std::runtime_error("No such grid setting");
                    return property == GridProperty::LayerColor    ? Field(v.color)
                           : property == GridProperty::LayerLinker ? Field(v.linker)
                                                                   : Field(v.text);
                },
                [property](Document &doc, CKObject *, const EditorRequest &r, const EditorValue &v) {
                    GridPropertyValue value;
                    if (!ParseGrid(property, v, value))
                        return false;
                    return WriteGridProperty(doc, property, Index(r), value);
                });
            entry.registry = property;
            entry.storage = Storage::Grid;
        }
        Extra(
            "layer.cells", "Grid Values", CKCID_LAYER, EDITOR_RECORD, "value.structure",
            [](Document &, CKObject *, const EditorRequest &) {
                EditorField v{"value", EDITOR_RECORD};
                v.fields = {{"cells", EDITOR_INTEGER_LIST}};
                return v;
            },
            [](Document &doc, CKObject *o, const EditorRequest &, const EditorValue &v) {
                auto *layer = CKLayer::Cast(o);
                auto *grid = CKGrid::Cast(doc.Context()->GetObject(layer->GetOwner()));
                if (v.type != EDITOR_RECORD || v.fields.count != 1)
                    return false;
                const auto &cells = v.fields.data[0];
                if (!grid || cells.type != EDITOR_INTEGER_LIST || cells.integers.count % 3)
                    return false;
                for (uint32_t i = 0; i < cells.integers.count; i += 3)
                    if (cells.integers.data[i] >= uint64_t(grid->GetWidth()) ||
                        cells.integers.data[i + 1] >= uint64_t(grid->GetLength()) || cells.integers.data[i + 2] > 255)
                        return false;
                for (uint32_t i = 0; i < cells.integers.count; i += 3) {
                    int value = int(cells.integers.data[i + 2]);
                    layer->SetValue2(int(cells.integers.data[i]), int(cells.integers.data[i + 1]), &value);
                }
                return true;
            });
        Extra(
            "bitmap.save-format", "Image Format", CKCID_BEOBJECT, EDITOR_TEXT_VALUE, "value.string",
            [](Document &, CKObject *o, const EditorRequest &) {
                auto *t = CKTexture::Cast(o);
                auto *s = CKSprite::Cast(o);
                auto *format = t ? t->GetSaveFormat() : s ? s->GetSaveFormat() : nullptr;
                if (!t && !s)
                    throw std::runtime_error("Not an image");
                return EditorField{"value", EDITOR_TEXT_VALUE, {}, format ? format->m_Ext.m_Data : ""};
            },
            [](Document &doc, CKObject *o, const EditorRequest &, const EditorValue &v) {
                std::string ext;
                if (!Parse(v, ext))
                    return false;
                auto bytes = MakeSaveFormat(doc.Context(), ext);
                if (bytes.empty())
                    return false;
                auto *format = reinterpret_cast<CKBitmapProperties *>(bytes.data());
                if (auto *t = CKTexture::Cast(o))
                    t->SetSaveFormat(format);
                else if (auto *s = CKSprite::Cast(o))
                    s->SetSaveFormat(format);
                else
                    return false;
                return true;
            })
            .storage = Storage::Bitmap;
        Extra(
            "bitmap.format-options", "Format Options", CKCID_BEOBJECT, EDITOR_OBJECT, "value.object",
            [](Document &, CKObject *, const EditorRequest &) { return EditorField{"value", EDITOR_OBJECT}; },
            [](Document &doc, CKObject *o, const EditorRequest &, const EditorValue &v) {
                if (v.type != EDITOR_OBJECT)
                    return false;
                auto *options = CKParameterOut::Cast(doc.Resolve(v.object, CKCID_PARAMETEROUT));
                if (!options)
                    return false;
                return WriteBitmapOptions(doc, o, options);
            })
            .storage = Storage::Bitmap;
        Extra(
            "mesh.global-material", "Apply Material", CKCID_MESH, EDITOR_OBJECT, "value.object",
            [](Document &doc, CKObject *o, const EditorRequest &) {
                auto *m = CKMesh::Cast(o);
                auto *material = m->GetMaterialCount() ? m->GetMaterial(0) : nullptr;
                return EditorField{"value", EDITOR_OBJECT, material ? doc.Reference(material->GetID()) : ObjectRef{}};
            },
            [](Document &doc, CKObject *o, const EditorRequest &, const EditorValue &v) {
                auto *material = CKMaterial::Cast(doc.Resolve(v.object, CKCID_MATERIAL));
                if (v.type != EDITOR_OBJECT || !material)
                    return false;
                CKMesh::Cast(o)->ApplyGlobalMaterial(material);
                return true;
            });
    }
    static bool ParseGrid(GridProperty property, const EditorValue &v, GridPropertyValue &value) {
        return property == GridProperty::LayerColor    ? Parse(v, value.color)
               : property == GridProperty::LayerLinker ? Parse(v, value.linker)
                                                       : Parse(v, value.text);
    }
    static CKObject *Target(Entry &entry, Document *doc, const EditorRequest &request) {
        const auto *target = Arg(request, "target");
        return doc && doc->Token() == request.scope.document && target ? doc->Resolve(target->object, entry.cid)
                                                                       : nullptr;
    }
    static std::vector<CK_ID> Targets(Entry &entry, Document &doc, const EditorRequest &request) {
        std::vector<CK_ID> ids;
        auto *target = Target(entry, &doc, request);
        if (!target)
            return ids;
        ids.push_back(target->GetID());
        if (const auto *targets = Arg(request, "targets"))
            for (uint32_t i = 0; i < targets->objects.count; ++i) {
                auto *object = doc.Resolve(targets->objects.data[i], entry.cid);
                if (!object)
                    return {};
                if (std::find(ids.begin(), ids.end(), object->GetID()) == ids.end())
                    ids.push_back(object->GetID());
            }
        return ids;
    }
    static bool EmptyBitmap(CKObject *object) {
        if (auto *texture = CKTexture::Cast(object))
            return texture->GetSlotCount() == 0;
        if (auto *sprite = CKSprite::Cast(object))
            return sprite->GetSlotCount() == 0;
        return false;
    }
    static bool ValueOnly(Entry &entry, Document &doc, const EditorRequest &request) {
        const auto ids = Targets(entry, doc, request);
        return entry.bitmapProperty && !ids.empty() &&
               std::all_of(ids.begin(), ids.end(), [&](auto id) { return EmptyBitmap(doc.Context()->GetObject(id)); });
    }
    static EditorField FontSchema(ObjectRef target) {
        EditorField schema{"schema", EDITOR_RECORD};
        for (const auto *key : {"face", "size", "weight", "italic", "underline"}) {
            const bool text = std::string(key) == "face",
                       boolean = std::string(key) == "italic" || std::string(key) == "underline";
            EditorField member{key, EDITOR_RECORD};
            member.fields = {{"name", EDITOR_TEXT_VALUE, {}, key},
                             {"editor",
                              EDITOR_TEXT_VALUE,
                              {},
                              text      ? "value.string"
                              : boolean ? "value.boolean"
                                        : "value.integer"},
                             {"target", EDITOR_OBJECT, target},
                             {"choices", EDITOR_TEXT_VALUE}};
            schema.fields.push_back(std::move(member));
        }
        return schema;
    }
    static EditorStatus Read(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &entry = *static_cast<Entry *>(data);
        auto *doc = entry.owner->current();
        auto *target = Target(entry, doc, *request);
        if (!target)
            return EDITOR_STALE;
        auto fields = std::vector<EditorField>{entry.read(*doc, target, *request),
                                               {"revision", EDITOR_INTEGER_VALUE, {}, {}, doc->TopologyRevision()},
                                               {"indices", EDITOR_INTEGER_LIST},
                                               {"relative", EDITOR_OBJECT},
                                               {"targets", EDITOR_OBJECT_LIST}};
        // Copy optional context once; it is retained by the host's property draft.
        for (const auto *key : {"indices", "relative", "targets"})
            if (const auto *arg = Arg(*request, key)) {
                std::vector<EditorField> owned;
                if (CopyEditorFields(owned, arg, 1) != EDITOR_OK)
                    return EDITOR_INVALID;
                for (auto &f : fields)
                    if (f.key == key)
                        f = std::move(owned.front());
            }
        fields.push_back(entry.id == "setup.spritetext.font" ? FontSchema(doc->Reference(target->GetID()))
                                                             : EditorField{"schema", EDITOR_RECORD});
        const auto values = BorrowEditorFields(fields);
        return sink.write(sink.data, values.data(), uint32_t(values.values.size()));
    }
    static EditorStatus Check(void *data, const EditorHost *, const EditorRequest *request) {
        auto &entry = *static_cast<Entry *>(data);
        auto *doc = entry.owner->current();
        if (!Target(entry, doc, *request))
            return EDITOR_STALE;
        const auto targets = Targets(entry, *doc, *request);
        if (targets.empty())
            return EDITOR_STALE;
        if (targets.size() > 1 && entry.storage != Storage::Object && entry.storage != Storage::Script)
            return EDITOR_INVALID;
        if (entry.bitmapProperty && targets.size() > 1) {
            const auto count = std::count_if(targets.begin(), targets.end(),
                                             [&](auto id) { return EmptyBitmap(doc->Context()->GetObject(id)); });
            if (count && size_t(count) != targets.size())
                return EDITOR_INVALID;
        }
        const auto *revision = Arg(*request, "revision"), *value = Arg(*request, "value");
        return revision && value && value->type == entry.type && revision->integer == doc->TopologyRevision()
                   ? EDITOR_OK
                   : EDITOR_STALE;
    }
    static EditorStatus Write(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
        const auto allowed = Check(data, host, request);
        if (allowed != EDITOR_OK)
            return allowed;
        auto &entry = *static_cast<Entry *>(data);
        auto &doc = *entry.owner->current();
        auto *object = Target(entry, &doc, *request);
        const auto *value = Arg(*request, "value");
        if (entry.storage == Storage::Script) {
            int v;
            if (!Parse(*value, v))
                return EDITOR_INVALID;
            Edit edit(doc, entry.label);
            for (auto id : Targets(entry, doc, *request)) {
                auto *target = doc.Context()->GetObject(id);
                CKBehavior *owner = CKBehavior::Cast(target);
                if (auto *link = CKBehaviorLink::Cast(target))
                    owner = link->GetInBehaviorIO()    ? link->GetInBehaviorIO()->GetOwner()
                            : link->GetOutBehaviorIO() ? link->GetOutBehaviorIO()->GetOwner()
                                                       : nullptr;
                auto *script =
                    owner ? (owner->GetType() == CKBEHAVIORTYPE_SCRIPT ? owner : owner->GetOwnerScript()) : nullptr;
                if (!script || !doc.FindScript(script->GetID())) return EDITOR_REFUSED;
                const auto result = entry.id == "setup.behavior.priority"
                                        ? SetBehaviorPriority(doc, script->GetID(), id, v)
                                        : SetLinkDelay(doc, script->GetID(), id, v);
                if (!result.ok && !result.Unchanged()) return Result(result, sink);
            }
            return Result(edit.Commit(), sink);
        }
        if (entry.storage == Storage::Grid) {
            GridPropertyValue v;
            if (!ParseGrid(entry.registry, *value, v))
                return EDITOR_INVALID;
            return Result(EditGridProperty(doc, entry.registry, Index(*request), v), sink);
        }
        if (entry.storage == Storage::Bitmap) {
            BitmapFormat before;
            if (!ReadBitmapFormat(object, before))
                return EDITOR_ERROR;
            if (!GuardedCall(
                    [&] {
                        if (!entry.write(doc, object, *request, *value))
                            throw std::runtime_error("Invalid image settings");
                    },
                    &doc.Host(), entry.label.c_str())) {
                if (!ApplyBitmapFormat(object, before))
                    doc.InvalidateHistory();
                return EDITOR_ERROR;
            }
            return Result(CommitBitmapFormat(doc, object->GetID(), before, entry.label), sink);
        }
        if (entry.storage == Storage::Scene) {
            auto *scene = CKScene::Cast(object);
            const auto before = ReadSceneEnvironment(scene);
            if (!GuardedCall(
                    [&] {
                        if (!entry.write(doc, object, *request, *value))
                            throw std::runtime_error("Invalid property value");
                    },
                    &doc.Host(), entry.label.c_str())) {
                CancelSceneEnvironment(doc, object->GetID(), before);
                return EDITOR_ERROR;
            }
            return Result(CommitSceneEnvironment(doc, object->GetID(), before, entry.label), sink);
        }
        if (ValueOnly(entry, doc, *request)) {
            std::vector<ObjectPropertyState> before;
            if (!ReadObjectProperty(doc, entry.property, Targets(entry, doc, *request), before))
                return EDITOR_ERROR;
            if (!GuardedCall(
                    [&] {
                        for (auto id : Targets(entry, doc, *request))
                            if (!entry.write(doc, doc.Context()->GetObject(id), *request, *value))
                                throw std::runtime_error("Invalid property value");
                    },
                    &doc.Host(), entry.label.c_str())) {
                if (!RestoreObjectProperty(doc, entry.property, before))
                    doc.InvalidateHistory();
                return EDITOR_ERROR;
            }
            return Result(CommitObjectProperty(doc, entry.property, before, entry.label), sink);
        }
        return Result(EditObjectsState(doc, Targets(entry, doc, *request), entry.label,
                                       [&](CKObject *object) {
                                           if (!entry.write(doc, object, *request, *value))
                                               throw std::runtime_error("Invalid property value");
                                       }),
                      sink);
    }
    static EditorStatus PreviewValue(void *data, const EditorHost *, const EditorRequest *request,
                                     EditorPreviewAction action, void **state) {
        auto &entry = *static_cast<Entry *>(data);
        auto *doc = entry.owner->current();
        if (action == EDITOR_PREVIEW_CANCEL) {
            std::unique_ptr<Preview> preview(static_cast<Preview *>(*state));
            *state = nullptr;
            if (!preview)
                return EDITOR_OK;
            if (!doc || doc->Token() != preview->document)
                return EDITOR_STALE;
            if (entry.storage != Storage::Grid && !doc->Resolve(preview->target, entry.cid))
                return EDITOR_STALE;
            for (auto handle : preview->environmentObjects)
                if (!doc->Context()->GetObjectByHandle(handle))
                    return EDITOR_ERROR;
            const bool restored = preview->valueOnly ? RestoreObjectProperty(*doc, entry.property, preview->values)
                                  : entry.storage == Storage::Bitmap
                                      ? ApplyBitmapFormat(doc->Resolve(preview->target, entry.cid), preview->bitmap)
                                  : entry.storage == Storage::Grid
                                      ? WriteGridProperty(*doc, entry.registry, preview->index, preview->grid)
                                  : entry.storage == Storage::Scene
                                      ? CancelSceneEnvironment(*doc, preview->target.object, preview->environment)
                                      : preview->objects.Cancel(*doc);
            if (!restored)
                doc->InvalidateHistory();
            return restored ? EDITOR_OK : EDITOR_ERROR;
        }
        auto *object = Target(entry, doc, *request);
        const auto *value = Arg(*request, "value");
        if (!object || !value)
            return EDITOR_STALE;
        if (!*state) {
            auto preview = std::make_unique<Preview>();
            preview->document = doc->Token();
            preview->target = doc->Reference(object->GetID());
            preview->index = Index(*request);
            if (entry.storage == Storage::Bitmap) {
                if (!ReadBitmapFormat(object, preview->bitmap))
                    return EDITOR_ERROR;
            } else if (entry.storage == Storage::Grid) {
                if (!ReadGridProperty(*doc, entry.registry, preview->index, preview->grid))
                    return EDITOR_STALE;
            } else if (entry.storage == Storage::Scene) {
                preview->environment = ReadSceneEnvironment(CKScene::Cast(object));
                for (auto id : {preview->environment.backgroundTexture, preview->environment.startingCamera})
                    if (id)
                        preview->environmentObjects.push_back(doc->Context()->GetObjectHandle(id));
            } else if (ValueOnly(entry, *doc, *request)) {
                preview->valueOnly = true;
                if (!ReadObjectProperty(*doc, entry.property, Targets(entry, *doc, *request), preview->values))
                    return EDITOR_ERROR;
            } else if (!preview->objects.Begin(*doc, Targets(entry, *doc, *request)))
                return EDITOR_ERROR;
            *state = preview.release();
        }
        bool written = true;
        return GuardedCall(
                   [&] {
                       for (auto id : Targets(entry, *doc, *request)) {
                           auto *target = doc->Context()->GetObject(id);
                           if (!target || !entry.write(*doc, target, *request, *value)) {
                               written = false;
                               break;
                           }
                       }
                   },
                   &doc->Host(), entry.label.c_str()) &&
                       written
                   ? EDITOR_OK
                   : EDITOR_ERROR;
    }
};
SetupCommands::SetupCommands(std::function<Document *()> current)
    : m_Data(std::make_unique<Data>(std::move(current))) {}
const std::vector<ProviderChoice> &SetupCommands::DefaultProviders() {
    static const auto providers = [] {
        Data data({});
        ForEachObjectProperty([&](const char *id, const auto &property) { data.Add(id, property); });
        data.AddSpecial();
        std::vector<ProviderChoice> result;
        for (const auto &entry : data.entries) {
            result.push_back({"command", entry->id + "-set", "builtin.setup"});
            result.push_back({"query", entry->id + "-value", "builtin.setup"});
        }
        for (const auto *id : {"setup.target", "setup.page", "setup.objects", "setup.transform", "setup.details",
                               "setup.choices", "setup.properties"})
            result.push_back({"query", id, "builtin.setup"});
        std::vector<EditorCommandDesc> commands;
        AppendSetupPageCommands(commands, nullptr);
        for (const auto &command : commands)
            result.push_back({"command", std::string(command.id.data, command.id.size), "builtin.setup"});
        return result;
    }();
    return providers;
}
SetupCommands::~SetupCommands() = default;
bool SetupCommands::Register(EditorApplication &app, std::string &error) {
    auto &data = *m_Data;
    ForEachObjectProperty([&](const char *id, const auto &property) { data.Add(id, property); });
    data.AddSpecial();
    std::vector<EditorCommandDesc> commands;
    std::vector<EditorQueryDesc> queries;
    std::vector<EditorPropertyDesc> properties;
    std::vector<std::vector<EditorParameter>> arguments(data.entries.size()), fields(data.entries.size());
    const EditorParameter queryArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                         {EDITOR_TEXT("indices"), EDITOR_INTEGER_LIST, 1},
                                         {EDITOR_TEXT("relative"), EDITOR_OBJECT, 1, 1},
                                         {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, 1}};
    for (size_t i = 0; i < data.entries.size(); ++i) {
        auto &entry = *data.entries[i];
        entry.query = entry.id + "-value";
        entry.command = entry.id + "-set";
        arguments[i] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                        {EDITOR_TEXT("value"), entry.type, 0, entry.type == EDITOR_OBJECT ? 1u : 0u},
                        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                        {EDITOR_TEXT("indices"), EDITOR_INTEGER_LIST},
                        {EDITOR_TEXT("relative"), EDITOR_OBJECT, 0, 1},
                        {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST}};
        fields[i] = {arguments[i][1], arguments[i][2], arguments[i][3],
                     arguments[i][4], arguments[i][5], {EDITOR_TEXT("schema"), EDITOR_RECORD}};
        commands.push_back({sizeof(EditorCommandDesc), EditorText(entry.command), EDITOR_TEXT("builtin.setup"),
                            EditorText(entry.label), arguments[i].data(), uint32_t(arguments[i].size()), Data::Write,
                            &entry, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT, Data::Check,
                            entry.live ? Data::PreviewValue : nullptr});
        queries.push_back({sizeof(EditorQueryDesc), EditorText(entry.query), EDITOR_TEXT("builtin.setup"), queryArgs, 4,
                           Data::Read, &entry, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields[i].data(),
                           uint32_t(fields[i].size())});
        properties.push_back({sizeof(EditorPropertyDesc), EditorText(entry.id), EditorText(entry.label),
                              EditorText(entry.query), EDITOR_TEXT("value"), EditorText(entry.command),
                              EDITOR_TEXT("value"), EditorText(entry.editor)});
    }
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorParameter targetFields[] = {target, {EDITOR_TEXT("point"), EDITOR_INTEGER_VALUE}};
    const EditorParameter pageArgs[] = {target, {EDITOR_TEXT("point"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter pageFields[] = {target,
                                          {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE},
                                          {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE},
                                          {EDITOR_TEXT("point"), EDITOR_OBJECT, 0, 1},
                                          {EDITOR_TEXT("grid"), EDITOR_OBJECT, 0, 1},
                                          {EDITOR_TEXT("layer"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("file-kind"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("filename"), EDITOR_TEXT_VALUE},
                                          {EDITOR_TEXT("behavioral"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("frame"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("attribute-drop"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("scene-scoped"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("current-scene"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("in-current-scene"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("initial-visible"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("initial-here"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("initial-value"), EDITOR_INTEGER_VALUE}};
    const EditorParameter objectArgs[] = {target, {EDITOR_TEXT("current-scene"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter objectFields[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
                                            {EDITOR_TEXT("names"), EDITOR_TEXT_LIST}};
    const EditorParameter transformFields[] = {{EDITOR_TEXT("world"), EDITOR_REAL_LIST},
                                               {EDITOR_TEXT("local"), EDITOR_REAL_LIST},
                                               {EDITOR_TEXT("box"), EDITOR_REAL_LIST}};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("setup.target"), EDITOR_TEXT("builtin.setup"), &target, 1,
                       Data::OpenTarget, &data, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, targetFields, 2});
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("setup.page"), EDITOR_TEXT("builtin.setup"), pageArgs, 2,
                       Data::Page, &data, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, pageFields,
                       uint32_t(std::size(pageFields))});
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("setup.objects"), EDITOR_TEXT("builtin.setup"), objectArgs,
                       2, Data::SetupObjects, &data, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, objectFields, 2});
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("setup.transform"), EDITOR_TEXT("builtin.setup"), &target,
                       1, Data::Transform, &data, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, transformFields, 3});
    AppendSetupPageQueries(queries, &data.current);
    AppendSetupPageCommands(commands, &data.current);
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("setup.editing");
    module.commands = commands.data();
    module.commandCount = uint32_t(commands.size());
    module.queries = queries.data();
    module.queryCount = uint32_t(queries.size());
    module.properties = properties.data();
    module.propertyCount = uint32_t(properties.size());
    return app.Register(module, error);
}
} // namespace vdev

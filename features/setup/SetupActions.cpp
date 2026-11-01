#include "features/setup/SetupQueries.h"
#include "core/level/SetupCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/LevelCommands.h"
#include "core/layout/LayoutCommands.h"
#include <algorithm>
#include "core/runtime/SdkCompat.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {
namespace {
enum class Action {
    SoundPlay,
    SoundPause,
    SoundStop,
    SynchroReset,
    CriticalReset,
    ResetWorld,
    ScaleUnit,
    Consolidate,
    CurveLinear,
    PlayerShown,
    CurvePoints,
    Cone,
    BodyAdd,
    BodyRemove,
    PlaceLink,
    SlotAdd,
    SlotReplace,
    SlotAlpha,
    SlotReload,
    SlotRemove,
    SlotResize,
    SlotSave,
    SlotSaveAlpha,
    InvertColors,
    NormalMap,
    BumpMap,
    AlphaOpaque,
    AlphaTranslucent,
    AlphaTransparent,
    InvertAlpha,
    MeshReplace,
    GridResize,
    GridLayerAdd,
    GridLayerRemove,
    GridClassAdd,
    GridClassRemove,
    GridRegisterLayer,
    GridRegisterClass
};
const EditorValue *Arg(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key)
            return request.values + i;
    return nullptr;
}
EditorStatus Result(const EditResult &result, EditorSink sink) {
    if (!result.ok && !result.Unchanged() && sink.write) {
        const auto text = AnsiToUtf8(result.message);
        const EditorValue message{EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(text)};
        sink.write(sink.data, &message, 1);
    }
    if (result.ok && result.created && sink.write) {
        const EditorValue created{EDITOR_TEXT("created"), EDITOR_INTEGER_VALUE, {}, {}, result.created};
        const auto status = sink.write(sink.data, &created, 1);
        if (status != EDITOR_OK)
            return status;
    }
    return result.ok                                ? EDITOR_OK
           : result.Unchanged()                     ? EDITOR_NO_CHANGE
           : result.Status() == EditStatus::Refused ? EDITOR_REFUSED
                                                    : EDITOR_ERROR;
}
template <Action action>
EditorStatus Run(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = (*static_cast<std::function<Document *()> *>(data))();
    auto *object = document && document->Token() == request->scope.document
                       ? document->Resolve(Arg(*request, "target")->object)
                       : nullptr;
    if (!object)
        return EDITOR_STALE;
    if (const auto *revision = Arg(*request, "document-revision"))
        if (revision->integer != document->ChangeRevision())
            return EDITOR_STALE;
    const auto number = [&](const char *key) {
        const auto *arg = Arg(*request, key);
        return arg ? int64_t(arg->integer) : int64_t(0);
    };
    const auto text = [&](const char *key) {
        const auto &arg = *Arg(*request, key);
        return Utf8ToAnsi(std::string(arg.text.data, arg.text.size));
    };
    if constexpr (action == Action::SlotAdd || action == Action::SlotReplace || action == Action::SlotAlpha ||
                  action == Action::SlotReload || action == Action::SlotRemove || action == Action::SlotResize ||
                  action == Action::SlotSave || action == Action::SlotSaveAlpha ||
                  (action >= Action::InvertColors && action <= Action::InvertAlpha)) {
        auto *texture = CKTexture::Cast(object);
        auto *sprite = CKSprite::Cast(object);
        if (!texture && !sprite)
            return EDITOR_STALE;
        const auto *requested = Arg(*request, "slot");
        const int slot = texture ? texture->GetCurrentSlot() : sprite->GetCurrentSlot();
        const int slots = texture ? texture->GetSlotCount() : sprite->GetSlotCount();
        if (requested && (requested->integer >= uint64_t(slots) || int(requested->integer) != slot))
            return EDITOR_STALE;
        if constexpr (action == Action::SlotAdd) {
            std::vector<std::string> paths;
            const auto &values = Arg(*request, "paths")->texts;
            if (!values.count)
                return EDITOR_INVALID;
            for (uint32_t i = 0; i < values.count; ++i)
                paths.push_back(Utf8ToAnsi(std::string(values.data[i].data, values.data[i].size)));
            return Result(AddSlotImages(*document, object->GetID(), paths), sink);
        }
        if constexpr (action == Action::SlotReplace)
            return Result(ReplaceSlotImage(*document, object->GetID(), text("path"), slot), sink);
        if constexpr (action == Action::SlotAlpha) {
            const auto channels = number("channels");
            if (channels != 2 && channels != 4 && channels != 8 && channels != 14)
                return EDITOR_INVALID;
            return Result(ReplaceSlotAlpha(*document, object->GetID(), text("path"), int(channels), slot), sink);
        }
        if constexpr (action == Action::SlotReload)
            return Result(ReloadSlotImage(*document, object->GetID()), sink);
        if constexpr (action == Action::SlotRemove)
            return Result(RemoveCurrentSlot(*document, object->GetID()), sink);
        if constexpr (action == Action::SlotResize) {
            if (number("width") <= 0 || number("width") > 32768 || number("height") <= 0 || number("height") > 32768)
                return EDITOR_INVALID;
            return Result(ResizeSlotImages(*document, object->GetID(), int(number("width")), int(number("height"))),
                          sink);
        }
        if constexpr (action == Action::SlotSave || action == Action::SlotSaveAlpha)
            return SaveSlotImage(object, text("path"), action == Action::SlotSaveAlpha, slot) ? EDITOR_OK
                                                                                              : EDITOR_ERROR;
        if constexpr (action >= Action::InvertColors && action <= Action::InvertAlpha)
            return Result(
                EditSlotImage(*document, object->GetID(), SlotImageOp(int(action) - int(Action::InvertColors))), sink);
    }
    if constexpr (action == Action::MeshReplace) {
        if (!CKMesh::Cast(object))
            return EDITOR_STALE;
        auto *from = document->Resolve(Arg(*request, "from")->object, CKCID_MATERIAL);
        auto *to = document->Resolve(Arg(*request, "to")->object, CKCID_MATERIAL);
        if (!from || !to)
            return EDITOR_STALE;
        return Result(ReplaceMeshMaterial(*document, object->GetID(), from->GetID(), to->GetID()), sink);
    }
    if constexpr (action >= Action::GridResize && action <= Action::GridRegisterClass) {
        if (!CKGrid::Cast(object))
            return EDITOR_STALE;
        if constexpr (action == Action::GridResize) {
            if (number("side") < 0 || number("side") > 3 || number("add") < 0 || number("add") > 1)
                return EDITOR_INVALID;
            return Result(ResizeGrid(*document, object->GetID(), int(number("side")), number("add") != 0), sink);
        }
        if constexpr (action == Action::GridRegisterLayer)
            return Result(RegisterGridLayerType(*document, text("name")), sink);
        if constexpr (action == Action::GridRegisterClass)
            return Result(RegisterGridClassification(*document, text("name")), sink);
        const auto type = number("type");
        if (type < 0 || type > INT32_MAX)
            return EDITOR_INVALID;
        if constexpr (action == Action::GridLayerAdd)
            return Result(AddGridLayer(*document, object->GetID(), int(type)), sink);
        if constexpr (action == Action::GridLayerRemove)
            return Result(RemoveGridLayer(*document, object->GetID(), int(type)), sink);
        if constexpr (action == Action::GridClassAdd)
            return Result(AddGridClassification(*document, object->GetID(), int(type)), sink);
        if constexpr (action == Action::GridClassRemove)
            return Result(RemoveGridClassification(*document, object->GetID(), int(type)), sink);
    }
    if constexpr (action == Action::ResetWorld)
        return Result(ResetWorldMatrix(*document, object->GetID()), sink);
    if constexpr (action == Action::ScaleUnit)
        return Result(SetScaleToUnit(*document, object->GetID()), sink);
    if constexpr (action == Action::Consolidate)
        return Result(ConsolidateMesh(*document, object->GetID()), sink);
    if constexpr (action == Action::CurveLinear) {
        auto *curve = CKCurve::Cast(object);
        const auto *value = Arg(*request, "value");
        if (!curve)
            return EDITOR_STALE;
        if (value->integer > 1)
            return EDITOR_INVALID;
        std::vector<CK_ID> points;
        for (int i = 0; i < curve->GetControlPointCount(); ++i)
            if (auto *point = curve->GetControlPoint(i))
                points.push_back(point->GetID());
        const bool linear = value->integer != 0;
        return Result(EditObjectsState(*document, points, linear ? "Linear Curve" : "Spline Curve",
                                       [linear](CKObject *point) { CKCurvePoint::Cast(point)->SetLinear(linear); }),
                      sink);
    }
    if constexpr (action == Action::PlayerShown || action == Action::CurvePoints || action == Action::Cone) {
        const auto value = Arg(*request, "value")->integer;
        if (value > 1)
            return EDITOR_INVALID;
        if (!CK3dEntity::Cast(object) || (action == Action::CurvePoints && !CKCurve::Cast(object)))
            return EDITOR_STALE;
        const auto list = action == Action::CurvePoints ? LayoutList::HiddenCurvePoints
                          : action == Action::Cone      ? LayoutList::Cones
                                                        : LayoutList::PlayerShown;
        return Result(SetLayoutMembership(*document, list, object->GetID(),
                                          action == Action::CurvePoints ? !value : value != 0,
                                          action == Action::CurvePoints ? "Show curve points"
                                          : action == Action::Cone      ? "Show cone"
                                                                        : "Show In Player Mode"),
                      sink);
    }
    if constexpr (action == Action::BodyAdd || action == Action::BodyRemove) {
        if (!CKCharacter::Cast(object))
            return EDITOR_STALE;
        const auto &refs = Arg(*request, "objects")->objects;
        std::vector<CK_ID> ids;
        for (uint32_t i = 0; i < refs.count; ++i) {
            auto *part = document->Resolve(refs.data[i], CKCID_BODYPART);
            if (!part)
                return EDITOR_STALE;
            ids.push_back(part->GetID());
        }
        return Result(action == Action::BodyAdd ? AddBodyParts(*document, object->GetID(), ids)
                                                : RemoveBodyParts(*document, object->GetID(), ids),
                      sink);
    }
    if constexpr (action == Action::PlaceLink) {
        auto *place = CKPlace::Cast(object);
        if (!place)
            return EDITOR_STALE;
        const auto native = [&](const char *key, CK_CLASSID cid) -> CK_ID {
            const auto ref = Arg(*request, key)->object;
            return !ref ? 0 : document->Resolve(ref, cid) ? ref.object : CK_ID(-1);
        };
        const auto from = native("from-place", CKCID_PLACE), oldPortal = native("from-portal", CKCID_3DENTITY),
                   to = native("to-place", CKCID_PLACE), portal = native("to-portal", CKCID_3DENTITY);
        if (from == CK_ID(-1) || oldPortal == CK_ID(-1) || to == CK_ID(-1) || portal == CK_ID(-1))
            return EDITOR_STALE;
        if (to == place->GetID())
            return EDITOR_REFUSED;
        if (portal && !(CK3dEntity::Cast(document->Context()->GetObject(portal))->GetFlags() & CK_3DENTITY_PORTAL))
            return EDITOR_INVALID;
        auto links = PlaceLinksOf(place);
        auto found = std::find_if(links.begin(), links.end(), [&](const auto &link) {
            return from && link.place == from && link.portal == oldPortal;
        });
        if (from && found == links.end())
            return EDITOR_STALE;
        if (found != links.end()) {
            if (to)
                *found = {to, portal};
            else
                links.erase(found);
        } else if (to)
            links.push_back({to, portal});
        const char *title = !from        ? "Attach New Place"
                            : !to        ? "Remove Place"
                            : from != to ? "Visible Place"
                                         : "Through Portal";
        return Result(SetPlaceLinks(*document, place->GetID(), links, title), sink);
    }
    bool accepted = true;
    const auto completed = GuardedCall(
        [&] {
            if constexpr (action == Action::SynchroReset) {
                if (auto *synchro = CKSynchroObject::Cast(object))
                    synchro->Reset();
                else
                    accepted = false;
            } else if constexpr (action == Action::CriticalReset) {
                if (auto *section = CKCriticalSectionObject::Cast(object))
                    section->Reset();
                else
                    accepted = false;
            } else if (auto *wave = CKWaveSound::Cast(object)) {
                if (!wave->GetSource()) {
                    accepted = false;
                    return;
                }
                if constexpr (action == Action::SoundPlay)
                    wave->Play();
                if constexpr (action == Action::SoundPause) {
                    if (wave->IsPaused())
                        wave->Resume();
                    else
                        wave->Pause();
                }
                if constexpr (action == Action::SoundStop)
                    wave->Stop();
            } else if (auto *midi = CKMidiSound::Cast(object)) {
                const auto *file = midi->GetSoundFileName();
                if (!document->Context()->GetManagerByGuid(MIDI_MANAGER_GUID) || !file || !*file) {
                    accepted = false;
                    return;
                }
                if constexpr (action == Action::SoundPlay) {
                    if (midi->IsPlaying())
                        midi->Pause(!midi->IsPaused());
                    else
                        midi->Play();
                }
                if constexpr (action == Action::SoundPause)
                    midi->Pause(!midi->IsPaused());
                if constexpr (action == Action::SoundStop)
                    midi->Stop();
            } else
                accepted = false;
        },
        &document->Host(), "Setup action");
    return !completed ? EDITOR_ERROR : accepted ? EDITOR_OK : EDITOR_REFUSED;
}
} // namespace
void AppendSetupPageCommands(std::vector<EditorCommandDesc> &commands, std::function<Document *()> *current) {
    static const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const auto add = [&](const char *id, const char *title, EditorCommand run, EditorEffect effect) {
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(id), EDITOR_TEXT("builtin.setup"),
                            EditorLiteral(title), &target, 1, run, current, EDITOR_DOCUMENT, effect});
    };
    static const EditorParameter valueArgs[] = {target, {EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE}};
    static const EditorParameter objectArgs[] = {target, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}};
    static const EditorParameter linkArgs[] = {target,
                                               {EDITOR_TEXT("from-place"), EDITOR_OBJECT, 0, 1},
                                               {EDITOR_TEXT("from-portal"), EDITOR_OBJECT, 0, 1},
                                               {EDITOR_TEXT("to-place"), EDITOR_OBJECT, 0, 1},
                                               {EDITOR_TEXT("to-portal"), EDITOR_OBJECT, 0, 1}};
    const auto arguments = [&](const char *id, const char *title, EditorCommand run, const EditorParameter *args,
                               uint32_t count) {
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(id), EDITOR_TEXT("builtin.setup"),
                            EditorLiteral(title), args, count, run, current, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT});
    };
    arguments("setup.curve-linear", "Change Curve Type", Run<Action::CurveLinear>, valueArgs, 2);
    arguments("setup.player-shown", "Show In Player Mode", Run<Action::PlayerShown>, valueArgs, 2);
    arguments("setup.curve-points-shown", "Show Curve Points", Run<Action::CurvePoints>, valueArgs, 2);
    arguments("setup.cone-shown", "Show Cone", Run<Action::Cone>, valueArgs, 2);
    arguments("setup.character-add-parts", "Add Body Parts", Run<Action::BodyAdd>, objectArgs, 2);
    arguments("setup.character-remove-parts", "Remove Body Parts", Run<Action::BodyRemove>, objectArgs, 2);
    arguments("setup.place-link", "Edit Place Link", Run<Action::PlaceLink>, linkArgs, 5);
    add("setup.sound-play", "Play Sound", Run<Action::SoundPlay>, EDITOR_RUNTIME_CONTROL);
    add("setup.sound-pause", "Pause Sound", Run<Action::SoundPause>, EDITOR_RUNTIME_CONTROL);
    add("setup.sound-stop", "Stop Sound", Run<Action::SoundStop>, EDITOR_RUNTIME_CONTROL);
    add("setup.synchro-reset", "Reset Rendez-Vous", Run<Action::SynchroReset>, EDITOR_RUNTIME_CONTROL);
    add("setup.critical-section-reset", "Reset Critical Section", Run<Action::CriticalReset>, EDITOR_RUNTIME_CONTROL);
    add("setup.reset-world-matrix", "Reset World Matrix", Run<Action::ResetWorld>, EDITOR_DOCUMENT_EDIT);
    add("setup.scale-unit", "Set as Unit", Run<Action::ScaleUnit>, EDITOR_DOCUMENT_EDIT);
    add("setup.mesh-consolidate", "Consolidate Mesh", Run<Action::Consolidate>, EDITOR_DOCUMENT_EDIT);
    static const EditorParameter revision{EDITOR_TEXT("document-revision"), EDITOR_INTEGER_VALUE};
    static const EditorParameter slot{EDITOR_TEXT("slot"), EDITOR_INTEGER_VALUE};
    static const EditorParameter path{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE};
    static const EditorParameter paths[] = {target, {EDITOR_TEXT("paths"), EDITOR_TEXT_LIST}, revision};
    static const EditorParameter file[] = {target, path, slot, revision};
    static const EditorParameter alpha[] = {
        target, path, slot, {EDITOR_TEXT("channels"), EDITOR_INTEGER_VALUE}, revision};
    static const EditorParameter slotArgs[] = {target, slot, revision};
    static const EditorParameter size[] = {
        target, {EDITOR_TEXT("width"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("height"), EDITOR_INTEGER_VALUE}, revision};
    static const EditorParameter replace[] = {
        target, {EDITOR_TEXT("from"), EDITOR_OBJECT}, {EDITOR_TEXT("to"), EDITOR_OBJECT}, revision};
    static const EditorParameter resize[] = {
        target, {EDITOR_TEXT("side"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("add"), EDITOR_INTEGER_VALUE}, revision};
    static const EditorParameter type[] = {target, {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}, revision};
    static const EditorParameter name[] = {target, {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE}, revision};
    arguments("setup.slot-add", "Add New Slot", Run<Action::SlotAdd>, paths, 3);
    arguments("setup.slot-replace", "Replace Slot", Run<Action::SlotReplace>, file, 4);
    arguments("setup.slot-replace-alpha", "Replace Alpha", Run<Action::SlotAlpha>, alpha, 5);
    arguments("setup.slot-reload", "Reload Slot", Run<Action::SlotReload>, slotArgs, 3);
    arguments("setup.slot-remove", "Remove Slot", Run<Action::SlotRemove>, slotArgs, 3);
    arguments("setup.slot-resize", "Resize Slot", Run<Action::SlotResize>, size, 4);
    commands.push_back({sizeof(EditorCommandDesc), EDITOR_TEXT("setup.slot-save"), EDITOR_TEXT("builtin.setup"),
                        EDITOR_TEXT("Save Slot"), file, 4, Run<Action::SlotSave>, current, EDITOR_DOCUMENT,
                        EDITOR_FILE_OPERATION});
    commands.push_back({sizeof(EditorCommandDesc), EDITOR_TEXT("setup.slot-save-alpha"), EDITOR_TEXT("builtin.setup"),
                        EDITOR_TEXT("Save Alpha Channel"), file, 4, Run<Action::SlotSaveAlpha>, current,
                        EDITOR_DOCUMENT, EDITOR_FILE_OPERATION});
    arguments("setup.mesh-replace-material", "Replace Material", Run<Action::MeshReplace>, replace, 4);
    arguments("setup.grid-resize", "Resize Grid", Run<Action::GridResize>, resize, 4);
    arguments("setup.grid-add-layer", "Add Layer", Run<Action::GridLayerAdd>, type, 3);
    arguments("setup.grid-remove-layer", "Remove Layer", Run<Action::GridLayerRemove>, type, 3);
    arguments("setup.grid-add-classification", "Add Classification", Run<Action::GridClassAdd>, type, 3);
    arguments("setup.grid-remove-classification", "Remove Classification", Run<Action::GridClassRemove>, type, 3);
    commands.push_back({sizeof(EditorCommandDesc), EDITOR_TEXT("setup.grid-register-layer"),
                        EDITOR_TEXT("builtin.setup"), EDITOR_TEXT("New Layer Type"), name, 3,
                        Run<Action::GridRegisterLayer>, current, EDITOR_DOCUMENT, EDITOR_DOCUMENT_CHANGE});
    commands.push_back({sizeof(EditorCommandDesc), EDITOR_TEXT("setup.grid-register-classification"),
                        EDITOR_TEXT("builtin.setup"), EDITOR_TEXT("New Classification"), name, 3,
                        Run<Action::GridRegisterClass>, current, EDITOR_DOCUMENT, EDITOR_DOCUMENT_CHANGE});
    arguments("setup.slot-invert", "Invert Slot", Run<Action::InvertColors>, slotArgs, 3);
    arguments("setup.slot-normal-map", "HeightMap => NormalMap", Run<Action::NormalMap>, slotArgs, 3);
    arguments("setup.slot-bump-map", "HeightMap => BumpMap", Run<Action::BumpMap>, slotArgs, 3);
    arguments("setup.slot-alpha-opaque", "Set Alpha to Opaque", Run<Action::AlphaOpaque>, slotArgs, 3);
    arguments("setup.slot-alpha-translucent", "Set Alpha to Translucent", Run<Action::AlphaTranslucent>, slotArgs, 3);
    arguments("setup.slot-alpha-transparent", "Set Alpha to Transparent", Run<Action::AlphaTransparent>, slotArgs, 3);
    arguments("setup.slot-invert-alpha", "Invert Alpha", Run<Action::InvertAlpha>, slotArgs, 3);
}
} // namespace vdev

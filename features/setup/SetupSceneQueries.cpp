#include "features/setup/SetupQueries.h"
#include "core/level/SceneCommands.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>

namespace vdev {
bool DescribeSetupScene(CKObject *object, const std::string &section, const std::vector<uint64_t> &indices,
                        SetupDescription &out) {
    auto *context = out.document.Context();
    const auto id = object->GetID();
    if (section == "curve") {
        auto *curve = CKCurve::Cast(object);
        if (!curve)
            return false;
        const int count = curve->GetControlPointCount();
        const int index = count ? std::min(count - 1, indices.empty() ? 0 : int(indices[0])) : 0;
        auto *point = count ? curve->GetControlPoint(index) : nullptr;
        out.Integer("count", count);
        out.Integer("index", index);
        out.Link("point", point);
        out.Real("length", curve->GetLength());
        out.Real("point-length", point ? point->GetLength() : 0);
        out.Integer("player-shown", out.document.Layout().playerShown.count(id) != 0);
        out.Integer("points-shown", out.document.Layout().hiddenCurvePoints.count(id) == 0);
        bool linear = count > 0;
        for (int i = 0; i < count; ++i)
            if (auto *p = curve->GetControlPoint(i))
                if (!p->IsLinear())
                    linear = false;
        out.Integer("linear", linear);
        return true;
    }
    if (section == "character") {
        auto *character = CKCharacter::Cast(object);
        if (!character)
            return false;
        out.Link("root", character->GetRootBodyPart());
        out.Link("active", character->GetActiveAnimation());
        std::vector<EditorField> bodies, available, animations;
        for (int i = 0; i < character->GetBodyPartCount(); ++i) {
            auto *part = character->GetBodyPart(i);
            if (!part)
                continue;
            auto row = out.Row(part);
            SetupDescription info{out.document};
            info.Link("parent", part->GetParent());
            auto *mesh = part->GetCurrentMesh();
            info.Link("mesh", mesh);
            info.Integer("vertices", mesh ? mesh->GetVertexCount() : 0);
            row.fields.insert(row.fields.end(), info.fields.begin(), info.fields.end());
            bodies.push_back(std::move(row));
        }
        auto candidates = out.document.LiveObjects(CKCID_BODYPART, true);
        std::sort(candidates.begin(), candidates.end(), [](auto *a, auto *b) {
            return _stricmp(a->GetName() ? a->GetName() : "", b->GetName() ? b->GetName() : "") < 0;
        });
        for (auto *candidate : candidates)
            if (!candidate->IsPrivate() && !candidate->IsDynamic() && !CKBodyPart::Cast(candidate)->GetCharacter())
                available.push_back(out.Row(candidate));
        for (int i = 0; i < character->GetAnimationCount(); ++i)
            if (auto *animation = character->GetAnimation(i))
                animations.push_back(out.Row(animation));
        out.Rows("bodies", std::move(bodies));
        out.Rows("available", std::move(available));
        out.Rows("animations", std::move(animations));
        return true;
    }
    if (section == "sound") {
        auto *sound = CKWaveSound::Cast(object);
        if (!sound)
            return false;
        CKWaveFormat format{};
        sound->GetSoundFormat(format);
        out.Integer("sample-rate", format.nSamplesPerSec);
        out.Integer("sample-bits", format.wBitsPerSample);
        out.Integer("channels", format.nChannels);
        out.Integer("length", sound->GetSoundLength());
        const bool source = sound->GetSource() != nullptr;
        out.Integer("source", source);
        out.Integer("paused", sound->IsPaused());
        if (source && sound->GetType() == CK_WAVESOUND_POINT) {
            VxVector position, direction;
            float distance = 0;
            sound->GetSound3DInformation(position, direction, distance);
            out.Numbers("listener-position", {position.x, position.y, position.z});
            out.Real("distance", distance);
        }
        out.Link("attached", sound->GetAttachedEntity());
        return true;
    }
    if (section == "scene") {
        auto *scene = CKScene::Cast(object);
        if (auto *level = CKLevel::Cast(object))
            scene = level->GetLevelScene();
        if (!scene)
            return false;
        out.Link("scene", scene);
        if (scene) {
            out.Link("background", scene->GetBackgroundTexture());
            out.Link("camera", scene->GetStartingCamera());
        }
        return true;
    }
    if (section == "frame") {
        auto *frame = CK3dEntity::Cast(object);
        if (!frame)
            return false;
        out.Link("parent", frame->GetParent());
        out.Integer("place", CKPlace::Cast(frame) != nullptr);
        out.Integer("portal", !CKPlace::Cast(frame) && (frame->GetFlags() & CK_3DENTITY_PORTAL) != 0);
        out.Integer("player-shown", out.document.Layout().playerShown.count(id) != 0);
        const auto found = out.document.Layout().representations.find(id);
        out.Integer("representation", found != out.document.Layout().representations.end() ? found->second : 1);
        return true;
    }
    if (section == "place") {
        auto *place = CKPlace::Cast(object);
        if (!place)
            return false;
        auto *scene = InitialConditionsScene(context);
        std::vector<EditorField> links, places, portals;
        const auto choices = [&](CK_CLASSID cid, auto accept, std::vector<EditorField> &rows) {
            auto candidates = out.document.LiveObjects(cid, true);
            std::sort(candidates.begin(), candidates.end(), [](auto *a, auto *b) {
                return _stricmp(a->GetName() ? a->GetName() : "", b->GetName() ? b->GetName() : "") < 0;
            });
            for (auto *candidate : candidates)
                if (!candidate->IsPrivate() && !candidate->IsDynamic() && accept(candidate))
                    rows.push_back(out.Row(candidate));
        };
        choices(CKCID_PLACE, [&](auto *candidate) { return candidate != place; }, places);
        choices(
            CKCID_3DENTITY,
            [](auto *candidate) { return (CK3dEntity::Cast(candidate)->GetFlags() & CK_3DENTITY_PORTAL) != 0; },
            portals);
        for (int i = 0; i < place->GetPortalCount(); ++i) {
            CK3dEntity *portal = nullptr;
            auto *other = place->GetPortal(i, &portal);
            SetupDescription row{out.document};
            row.Link("place", other);
            row.Link("portal", portal);
            row.Integer("active", portal && scene && (scene->GetObjectFlags(portal) & CK_SCENEOBJECT_START_ACTIVATE));
            row.Integer("active-enabled", portal && scene && scene->IsObjectHere(portal));
            EditorField value{std::to_string(i), EDITOR_RECORD};
            value.fields = std::move(row.fields);
            links.push_back(std::move(value));
        }
        out.Rows("links", std::move(links));
        out.Rows("places", std::move(places));
        out.Rows("portals", std::move(portals));
        out.Link("scene", scene);
        return true;
    }
    if (section == "sprite3d") {
        auto *sprite = CKSprite3D::Cast(object);
        if (!sprite)
            return false;
        out.Link("material", sprite->GetMaterial());
        return true;
    }
    return false;
}
} // namespace vdev

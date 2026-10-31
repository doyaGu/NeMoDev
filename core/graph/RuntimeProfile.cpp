#include "core/graph/RuntimeProfile.h"
#include "core/document/Document.h"
#include "core/document/ToolCommands.h"
#include "core/runtime/TextEncoding.h"
#include <set>

namespace vdev {
namespace {
std::string Name(CKObject *object) {
    return object && object->GetName() && *object->GetName() ? AnsiToUtf8(object->GetName()) : std::string("<Unnamed>");
}
uint64_t VideoBytes(const VxImageDescEx &description) {
    return uint64_t(description.BytesPerLine) * description.Height;
}
} // namespace
void ReadRuntimeGeometry(Document &document, uint64_t &faces, uint64_t &vertices) {
    faces = vertices = 0;
    auto *context = document.Context();
    for (auto cid : {CKCID_MESH, CKCID_PATCHMESH}) {
        const int count = context->GetObjectsCountByClassID(cid);
        const auto *ids = context->GetObjectsListByClassID(cid);
        for (int i = 0; i < count; ++i)
            if (auto *mesh = CKMesh::Cast(context->GetObject(ids[i])); mesh && !mesh->IsPrivate()) {
                faces += mesh->GetFaceCount();
                vertices += mesh->GetVertexCount();
            }
    }
}
RuntimeProfile ReadRuntimeProfile(Document &document, bool details) {
    RuntimeProfile sample;
    auto *context = document.Context();
    context->GetProfileStats(&sample.stats);
    auto *level = context->GetCurrentLevel();
    if (auto *render = level ? level->GetRenderContext(0) : nullptr) render->GetStats(&sample.render);
    for (auto cid : {CKCID_3DENTITY, CKCID_SPRITE3D, CKCID_3DOBJECT, CKCID_BODYPART, CKCID_CHARACTER})
        sample.objects3d += context->GetObjectsCountByClassID(cid);
    sample.textures = context->GetObjectsCountByClassID(CKCID_TEXTURE);
    sample.sprites = context->GetObjectsCountByClassID(CKCID_SPRITE);
    sample.behaviors = context->GetObjectsCountByClassID(CKCID_BEHAVIOR);
    const auto *textures = context->GetObjectsListByClassID(CKCID_TEXTURE);
    for (int i = 0; i < sample.textures; ++i) {
        auto *texture = CKTexture::Cast(context->GetObject(textures[i]));
        VxImageDescEx description;
        if (!texture || !texture->GetVideoTextureDesc(description)) continue;
        ++sample.videoTextures;
        auto bytes = VideoBytes(description);
        if (texture->GetMipmapCount())
            for (auto mip = bytes; mip; mip >>= 2) sample.textureBytes += mip;
        else sample.textureBytes += bytes;
    }
    const auto *sprites = context->GetObjectsListByClassID(CKCID_SPRITE);
    for (int i = 0; i < sample.sprites; ++i) {
        auto *sprite = CKSprite::Cast(context->GetObject(sprites[i]));
        VxImageDescEx description;
        if (!sprite || !sprite->GetVideoTextureDesc(description)) continue;
        ++sample.videoSprites;
        sample.spriteBytes += VideoBytes(description);
    }
    if (!details) return sample;
    auto row = [&](std::string name, double time, int parent = -1, CKObject *object = nullptr,
                   CKBehavior *script = nullptr, bool dynamic = false) {
        sample.rows.push_back({std::move(name), time, parent,
            document.Reference(object ? object->GetID() : 0), document.Reference(script ? script->GetID() : 0), dynamic});
        return int(sample.rows.size() - 1);
    };
    double userTime = 0;
    for (int i = 0; i < 8; ++i) userTime += context->GetLastUserProfileTime(i);
    const auto user = row("User Profile Times", userTime);
    for (int i = 0; i < 8; ++i) row("Profile " + std::to_string(i), context->GetLastUserProfileTime(i), user);
    const auto render = row("Render Profile Times", sample.stats.RenderTime);
    const auto &r = sample.render;
    row("Pre render callbacks", r.DevicePreCallbacks, render);
    row("Hierarchical culling", r.SceneTraversalTime, render);
    row("Transparent objects sort", r.TransparentObjectsSortTime, render);
    row("3D Objects rendering", r.ObjectsRenderTime, render);
    row("3D Objects callbacks", r.ObjectsCallbacksTime, render);
    row("Skins evaluation", r.SkinTime, render);
    row("2D Entities rendering", r.SpriteTime, render);
    row("2D Entities callbacks", r.SpriteCallbacksTime, render);
    row("Post render callbacks", r.DevicePostCallbacks, render);
    row("Back To Front", r.BackToFrontTime, render);
    double managerTime = 0;
    for (int i = 0; i < context->GetManagerCount(); ++i)
        if (auto *manager = context->GetManager(i)) managerTime += manager->m_ProcessingTime;
    const auto managers = row("Managers", managerTime);
    for (int i = 0; i < context->GetManagerCount(); ++i)
        if (auto *manager = context->GetManager(i))
            row(manager->GetName() ? AnsiToUtf8(manager->GetName()) : std::string(), manager->m_ProcessingTime, managers);
    std::set<CK_ID> seen;
    const auto behaviorRow = [&](auto &&self, CKBehavior *behavior, CKBehavior *script, int parent, bool dynamic) -> void {
        if (!behavior || !seen.insert(behavior->GetID()).second) return;
        const auto index = row(Name(behavior), behavior->GetLastExecutionTime(), parent, behavior, script, dynamic);
        for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i)
            self(self, behavior->GetSubBehavior(i), script, index, dynamic);
    };
    for (auto id : BehavioralObjects(context)) {
        auto *object = CKBeObject::Cast(context->GetObject(id));
        if (!object || !object->GetScriptCount()) continue;
        const bool dynamic = object->IsDynamic() != 0;
        const auto index = row(Name(object), object->GetLastExecutionTime(), -1, object, nullptr, dynamic);
        for (int i = 0; i < object->GetScriptCount(); ++i) {
            auto *script = object->GetScript(i);
            behaviorRow(behaviorRow, script, script, index, dynamic);
        }
    }
    return sample;
}
} // namespace vdev

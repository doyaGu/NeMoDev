#include "features/setup/SetupQueries.h"
#include "core/graph/RuntimeGraph.h"
#include "core/level/SetupCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/SdkCompat.h"
#include "core/runtime/TextEncoding.h"
#include "core/runtime/Bitmaps.h"
#include "core/runtime/GridCompat.h"
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <memory>
#include <filesystem>

namespace vdev {
namespace {
std::vector<CKObject *> Choices(Document &document, CK_CLASSID cid) {
    auto objects = document.LiveObjects(cid, true);
    objects.erase(
        std::remove_if(objects.begin(), objects.end(), [](auto *o) { return o->IsPrivate() || o->IsDynamic(); }),
        objects.end());
    std::sort(objects.begin(), objects.end(), [](auto *a, auto *b) {
        return _stricmp(a->GetName() ? a->GetName() : "", b->GetName() ? b->GetName() : "") < 0;
    });
    return objects;
}
const char *FormatName(VX_PIXELFORMAT format) {
    const auto *name = VxPixelFormat2String(format);
    return name && *name ? name : "Unknown";
}
bool DriverTakes(CKContext *context, VX_PIXELFORMAT format) {
    auto *renderer = context->GetRenderManager();
    if (!renderer)
        return false;
    int driver = 0;
    if (renderer->GetRenderContextCount() && renderer->GetRenderContext(0))
        driver = renderer->GetRenderContext(0)->GetDriverIndex();
    auto *description = driver >= 0 && driver < renderer->GetRenderDriverCount()
                            ? renderer->GetRenderDriverDescription(driver)
                            : nullptr;
    if (!description)
        return false;
    for (int i = 0; i < description->TextureFormats.Size(); ++i)
        if (VxImageDesc2PixelFormat(description->TextureFormats[i]) == format)
            return true;
    return false;
}
void MeshInfo(CKMesh *mesh, SetupDescription &out) {
    out.Link("mesh", mesh);
    out.Integer("vertices", mesh ? mesh->GetVertexCount() : 0);
    out.Integer("faces", mesh ? mesh->GetFaceCount() : 0);
    out.Integer("channel-count", mesh ? mesh->GetChannelCount() : 0);
    out.Integer("patch", CKPatchMesh::Cast(mesh) != nullptr);
}
struct ReaderRelease {
    void operator()(CKBitmapReader *reader) const {
        if (reader)
            reader->Release();
    }
};
struct ChunkRelease {
    void operator()(CKStateChunk *chunk) const {
        if (chunk)
            DeleteCKStateChunk(chunk);
    }
};
void ImageDescription(const char *prefix, bool present, bool described, const VxImageDescEx &desc,
                      SetupDescription &out) {
    const std::string key(prefix);
    out.Integer((key + "-present").c_str(), present);
    out.Integer((key + "-described").c_str(), described);
    out.Integer((key + "-width").c_str(), described ? desc.Width : 0);
    out.Integer((key + "-height").c_str(), described ? desc.Height : 0);
    out.Integer((key + "-bits").c_str(), described ? desc.BitsPerPixel : 0);
    out.Text((key + "-format").c_str(), described ? FormatName(VxImageDesc2PixelFormat(desc)) : "Unknown");
}
} // namespace
bool DescribeSetupRender(CKObject *object, const std::string &section, const std::vector<uint64_t> &indices,
                         SetupDescription &out) {
    auto *context = out.document.Context();
    const auto id = object->GetID();
    if (section == "entity3d") {
        auto *entity = CK3dEntity::Cast(object);
        if (!entity)
            return false;
        std::vector<EditorField> meshes;
        for (int i = 0; i < entity->GetMeshCount(); ++i)
            if (auto *mesh = entity->GetMesh(i))
                meshes.push_back(out.Row(mesh));
        out.Rows("meshes", std::move(meshes));
        MeshInfo(entity->GetCurrentMesh(), out);
        auto *part = CKBodyPart::Cast(entity);
        out.Integer("body-part", part != nullptr);
        out.Link("character", part ? part->GetCharacter() : nullptr);
        out.Link("parent", entity->GetParent());
        return true;
    }
    if (section == "camera" || section == "light") {
        CK3dEntity *target = nullptr;
        bool targeted = false;
        if (section == "camera") {
            auto *camera = CKCamera::Cast(object);
            if (!camera)
                return false;
            targeted = CKTargetCamera::Cast(camera) != nullptr;
            if (targeted)
                target = camera->GetTarget();
        } else {
            auto *light = CKLight::Cast(object);
            if (!light)
                return false;
            targeted = CKTargetLight::Cast(light) != nullptr;
            if (targeted)
                target = light->GetTarget();
        }
        out.Integer("targeted", targeted);
        out.Link("target", target);
        if (target) {
            VxVector from, to;
            CK3dEntity::Cast(object)->GetPosition(&from);
            target->GetPosition(&to);
            out.Real("distance", Magnitude(to - from));
            out.Numbers("target-position", {to.x, to.y, to.z});
        }
        out.Integer("player-shown", out.document.Layout().playerShown.count(id) != 0);
        out.Integer("cone", out.document.Layout().cones.count(id) != 0);
        return true;
    }
    if (section == "entity2d") {
        auto *entity = CK2dEntity::Cast(object);
        if (!entity)
            return false;
        out.Link("material", entity->GetMaterial());
        out.Link("parent", entity->GetParent());
        return true;
    }
    if (section == "bitmap" || section == "bitmap-pixels" || section == "bitmap-pixel" || section == "bitmap-format" ||
        section == "bitmap-encoding") {
        auto *texture = CKTexture::Cast(object);
        auto *sprite = CKSprite::Cast(object);
        if (!texture && !sprite)
            return false;
        const auto describe = [&](auto *bitmap) -> bool {
            const int width = bitmap->GetWidth(), height = bitmap->GetHeight(), current = bitmap->GetCurrentSlot();
            if (section == "bitmap-pixels" || section == "bitmap-pixel") {
                const int slot = indices.empty() ? current : int(indices[0]);
                if (slot < 0 || slot >= bitmap->GetSlotCount())
                    return false;
                if (section == "bitmap-pixel") {
                    if (indices.size() != 3 || indices[1] >= uint64_t(std::max(0, width)) ||
                        indices[2] >= uint64_t(std::max(0, height)))
                        return false;
                    out.Integer("pixel", bitmap->GetPixel(int(indices[1]), int(indices[2]), slot));
                    return true;
                }
                const uint64_t total = uint64_t(std::max(0, width)) * uint64_t(std::max(0, height));
                const uint64_t first = indices.size() > 1 ? indices[1] : 0;
                if (first > total)
                    return false;
                const auto next = std::min(total, first + 65536);
                // ReleaseSurfacePtr marks pixel writes for GPU reload; borrowed read access needs no release.
                auto *pixels = bitmap->LockSurfacePtr(slot);
                if (!pixels && total)
                    return false;
                EditorField values{"pixels", EDITOR_INTEGER_LIST};
                for (auto i = first; i < next; ++i) {
                    uint32_t pixel = 0;
                    std::memcpy(&pixel, pixels + i * 4, 4);
                    values.integers.push_back(pixel);
                }
                out.Integer("width", width);
                out.Integer("height", height);
                out.Integer("total", total);
                out.Integer("next", next);
                out.fields.push_back(std::move(values));
                return true;
            }
            if (section == "bitmap") {
                out.Integer("texture", texture != nullptr);
                out.Integer("width", width);
                out.Integer("height", height);
                out.Integer("slots", bitmap->GetSlotCount());
                out.Integer("slot", current);
                const auto *movie = texture ? texture->GetMovieFileName() : nullptr;
                out.Integer("movie", movie && *movie);
                out.Text("name", SetupObjectName(object));
                VxImageDescEx system;
                const bool described = texture ? texture->GetSystemTextureDesc(system) != FALSE
                                               : sprite->GetSystemTextureDesc(system) != FALSE;
                auto *pixels = bitmap->LockSurfacePtr(-1);
                const bool systemPresent = pixels != nullptr;
                ImageDescription("system", systemPresent, described, system, out);
                VxImageDescEx video;
                const bool present = texture ? texture->IsInVideoMemory() != FALSE : sprite->IsInVideoMemory() != FALSE;
                const bool videoDescription = present && (texture ? texture->GetVideoTextureDesc(video) != FALSE
                                                                  : sprite->GetVideoTextureDesc(video) != FALSE);
                ImageDescription("video", present, videoDescription, video, out);
                EditorField formats{"formats", EDITOR_TEXT_LIST};
                for (int f = 0; f <= 23; ++f)
                    formats.texts.push_back(std::string(FormatName(VX_PIXELFORMAT(f))) +
                                            (DriverTakes(context, VX_PIXELFORMAT(f)) ? " *" : ""));
                out.fields.push_back(std::move(formats));
                std::vector<EditorField> users;
                if (texture)
                    for (auto *candidate : Choices(out.document, CKCID_MATERIAL))
                        for (int i = 0; i < 4; ++i)
                            if (CKMaterial::Cast(candidate)->GetTexture(i) == texture) {
                                users.push_back(out.Row(candidate));
                                break;
                            }
                out.Rows("users", std::move(users));
                return true;
            }
            auto *plugins = CKGetPluginManager();
            auto *format = texture ? texture->GetSaveFormat() : sprite->GetSaveFormat();
            BitmapFormat savedFormat;
            const bool nativeFormat =
                ReadBitmapFormat(object, savedFormat) && savedFormat.bytes.size() >= sizeof(CKBitmapProperties);
            auto *readerFormat =
                nativeFormat ? reinterpret_cast<CKBitmapProperties *>(savedFormat.bytes.data()) : nullptr;
            const std::string extension = format ? format->m_Ext.m_Data : "";
            if (section == "bitmap-format") {
                out.Text("extension", extension);
                EditorField extensions{"extensions", EDITOR_TEXT_LIST}, labels{"labels", EDITOR_TEXT_LIST};
                CKGUID optionsType;
                for (int i = 0; plugins && i < plugins->GetPluginCount(CKPLUGIN_BITMAP_READER); ++i) {
                    auto *reader = plugins->GetPluginInfo(CKPLUGIN_BITMAP_READER, i);
                    if (!reader || !reader->m_PluginInfo.m_Extension.m_Data[0])
                        continue;
                    if (reader->m_ReadersInfo &&
                        _stricmp(reader->m_PluginInfo.m_Extension.m_Data, extension.c_str()) == 0)
                        optionsType = reader->m_ReadersInfo->m_SettingsParameterGuid;
                    if (reader->m_ReadersInfo && !(reader->m_ReadersInfo->m_ReaderFlags & CK_DATAREADER_FILESAVE))
                        continue;
                    extensions.texts.emplace_back(reader->m_PluginInfo.m_Extension.m_Data);
                    labels.texts.push_back(AnsiToUtf8(std::string(reader->m_PluginInfo.m_Summary.CStr()
                                                                      ? reader->m_PluginInfo.m_Summary.CStr()
                                                                      : "") +
                                                      " (" + reader->m_PluginInfo.m_Extension.m_Data + ")"));
                }
                out.fields.push_back(std::move(extensions));
                out.fields.push_back(std::move(labels));
                out.Integer("present", format && plugins);
                out.Integer("options", format && plugins && optionsType != CKGUID());
                bool alpha = false;
                if (readerFormat && plugins) {
                    CKFileExtension ext(const_cast<char *>(extension.c_str()));
                    if (auto *reader = plugins->GetBitmapReader(ext)) {
                        alpha = reader->IsAlphaSaved(readerFormat) != FALSE;
                        reader->Release();
                    }
                }
                out.Integer("alpha", alpha);
                out.Text("encoding-key", extension + "/" + std::to_string(current) + "/" + std::to_string(width) + "/" +
                                             std::to_string(out.document.ChangeRevision()));
                return true;
            }
            std::string info;
            if (readerFormat && plugins) {
                CKFileExtension ext(const_cast<char *>(extension.c_str()));
                std::unique_ptr<CKBitmapReader, ReaderRelease> reader(plugins->GetBitmapReader(ext));
                VxImageDescEx desc;
                const bool described =
                    texture ? texture->GetImageDesc(desc) != FALSE : sprite->GetSystemTextureDesc(desc) != FALSE;
                auto *pixels = bitmap->LockSurfacePtr(current);
                if (reader && described && pixels) {
                    desc.Image = pixels;
                    const auto sized = [](int bytes) {
                        return bytes > 1024 ? std::to_string(bytes >> 10) + " KBytes"
                                            : std::to_string(bytes) + " Bytes";
                    };
                    char line[192];
                    std::snprintf(line, sizeof(line), "%d x %d x %d (%s)", desc.Width, desc.Height, desc.BitsPerPixel,
                                  sized(desc.BytesPerLine * desc.Height).c_str());
                    info = line;
                    std::unique_ptr<CKStateChunk, ChunkRelease> chunk(CreateCKStateChunk(1234, nullptr));
                    if (!chunk)
                        return false;
                    VxTimeProfiler timer;
                    chunk->StartWrite();
                    chunk->WriteIdentifier(1234);
                    chunk->WriteReaderBitmap(desc, reader.get(), readerFormat);
                    chunk->CloseChunk();
                    const auto save = timer.Current();
                    const int bytes = chunk->GetDataSize();
                    std::vector<CKBYTE> back(size_t(desc.BytesPerLine) * desc.Height);
                    auto read = desc;
                    read.Image = back.data();
                    timer.Reset();
                    chunk->StartRead();
                    chunk->SeekIdentifier(1234);
                    chunk->ReadReaderBitmap(read);
                    const auto load = timer.Current();
                    std::snprintf(line, sizeof(line), "%s  (Save:%.1f ms  Read: %.1f ms)", sized(bytes).c_str(), save,
                                  load);
                    info += std::string("\n") + line;
                }
            }
            out.Text("info", info);
            return true;
        };
        return texture ? describe(texture) : describe(sprite);
    }
    if (section == "material") {
        auto *material = CKMaterial::Cast(object);
        if (!material)
            return false;
        out.Link("texture", material->GetTexture(0));
        std::vector<EditorField> users;
        for (auto *candidate : Choices(out.document, CKCID_MESH)) {
            auto *mesh = CKMesh::Cast(candidate);
            for (int i = 0; i < mesh->GetMaterialCount(); ++i)
                if (mesh->GetMaterial(i) == material) {
                    users.push_back(out.Row(candidate));
                    break;
                }
        }
        for (auto *candidate : Choices(out.document, CKCID_2DENTITY))
            if (CK2dEntity::Cast(candidate)->GetMaterial() == material)
                users.push_back(out.Row(candidate));
        for (auto *candidate : Choices(out.document, CKCID_SPRITE3D))
            if (CKSprite3D::Cast(candidate)->GetMaterial() == material)
                users.push_back(out.Row(candidate));
        out.Rows("users", std::move(users));
        auto *render = context->GetRenderManager();
        const int count = render ? render->GetEffectCount() : 0;
        static const char *const defaults[] = {"None", "TexGen",     "TexGen Ref", "Bump Env",
                                               "DP3",  "2 Textures", "3 Textures"};
        EditorField names{"effects", EDITOR_TEXT_LIST};
        for (int i = 0; i < count; ++i) {
            const auto &text = render->GetEffectDescription(i).Summary;
            names.texts.push_back(text.Length() ? AnsiToUtf8(text.CStr())
                                  : i < 7       ? defaults[i]
                                                : "Effect " + std::to_string(i));
        }
        if (names.texts.empty())
            names.texts.assign(defaults, defaults + 7);
        out.fields.push_back(std::move(names));
        out.Integer("effect-count", count);
        const int effect = indices.empty() ? int(material->GetEffect()) : int(indices[0]);
        std::vector<EditorField> textures;
        if (render && effect > 0 && effect < count) {
            const auto &description = render->GetEffectDescription(effect);
            const XString *words[] = {&description.Tex1Description, &description.Tex2Description,
                                      &description.Tex3Description};
            for (int i = 0; i < std::min(3, description.MaxTextureCount); ++i) {
                auto row = out.Row(material->GetTexture(i + 1));
                row.key = std::to_string(i + 1);
                row.fields.push_back(
                    {"label",
                     EDITOR_TEXT_VALUE,
                     {},
                     words[i]->Length() ? AnsiToUtf8(words[i]->CStr()) : "Texture " + std::to_string(i + 1)});
                textures.push_back(std::move(row));
            }
            auto *parameter = material->GetEffectParameter();
            out.Link("parameter", parameter);
            out.Integer("has-parameter", parameter && description.ParameterType != CKGUID());
            out.Text("parameter-text", parameter ? AnsiToUtf8(ParameterText(parameter)) : "");
            out.Text(
                "parameter-description",
                AnsiToUtf8(description.ParameterDescription.CStr() ? description.ParameterDescription.CStr() : ""));
            out.Text("description", AnsiToUtf8(description.Description.CStr() ? description.Description.CStr() : ""));
            const auto image = description.DescImage.Length()
                                   ? (std::filesystem::u8path(out.document.Host().RuntimeDirectory()) /
                                      "InterfacePlugins" / std::filesystem::path(description.DescImage.CStr()))
                                         .u8string()
                                   : std::string();
            out.Text("image-path", image);
        }
        out.Rows("effect-textures", std::move(textures));
        return true;
    }
    if (section == "material-effect-image") {
        auto *material = CKMaterial::Cast(object);
        auto *render = context->GetRenderManager();
        const int effect = indices.empty() || !material ? 0 : int(indices[0]);
        if (!material || !render || effect <= 0 || effect >= render->GetEffectCount())
            return false;
        const auto &image = render->GetEffectDescription(effect).DescImage;
        if (!image.Length())
            return false;
        const auto path = (std::filesystem::u8path(out.document.Host().RuntimeDirectory()) / "InterfacePlugins" /
                           std::filesystem::path(image.CStr()))
                              .string();
        void *bitmap = CKLoadBitmap(const_cast<char *>(path.c_str()));
        if (!bitmap)
            return false;
        struct Bitmap {
            void *value;
            ~Bitmap() { VxDeleteBitmap(value); }
        } owned{bitmap};
        VxImageDescEx converted;
        std::unique_ptr<XBYTE[]> pixels(VxConvertBitmap(bitmap, converted));
        const int width = converted.Width, height = converted.Height;
        if (!pixels || width <= 0 || height <= 0 || converted.BitsPerPixel != 32 || converted.BytesPerLine < width * 4)
            return false;
        const uint64_t total = uint64_t(width) * uint64_t(height), first = indices.size() > 1 ? indices[1] : 0;
        if (first > total)
            return false;
        const auto next = std::min(total, first + 65536);
        EditorField values{"pixels", EDITOR_INTEGER_LIST};
        for (auto i = first; i < next; ++i) {
            uint32_t pixel = 0;
            std::memcpy(&pixel,
                        pixels.get() + (i / uint64_t(width)) * converted.BytesPerLine + (i % uint64_t(width)) * 4, 4);
            values.integers.push_back(pixel);
        }
        out.Integer("width", width);
        out.Integer("height", height);
        out.Integer("total", total);
        out.Integer("next", next);
        out.fields.push_back(std::move(values));
        return true;
    }
    if (section == "mesh") {
        auto *mesh = CKMesh::Cast(object);
        if (!mesh)
            return false;
        MeshInfo(mesh, out);
        out.Integer("lines", mesh->GetLineCount());
        out.Real("radius", mesh->GetRadius());
        const auto box = mesh->GetLocalBox();
        VxVector bary;
        mesh->GetBaryCenter(&bary);
        out.Numbers("box-min", {box.Min.x, box.Min.y, box.Min.z});
        out.Numbers("box-max", {box.Max.x, box.Max.y, box.Max.z});
        out.Numbers("barycenter", {bary.x, bary.y, bary.z});
        if (auto *patch = CKPatchMesh::Cast(mesh)) {
            out.Integer("control-points", patch->GetVertCount());
            out.Integer("vectors", patch->GetVecCount());
            out.Integer("edges", patch->GetEdgeCount());
            out.Integer("patches", patch->GetPatchCount());
            out.Integer("texture-vertices", patch->GetTVCount());
        }
        std::vector<EditorField> users, channels, materials;
        for (auto *candidate : Choices(out.document, CKCID_3DENTITY)) {
            auto *entity = CK3dEntity::Cast(candidate);
            for (int i = 0; i < entity->GetMeshCount(); ++i)
                if (entity->GetMesh(i) == mesh) {
                    users.push_back(out.Row(entity));
                    break;
                }
        }
        for (int i = 0; i < mesh->GetChannelCount(); ++i)
            channels.push_back(out.Row(mesh->GetChannelMaterial(i)));
        for (int i = 0; i < mesh->GetMaterialCount(); ++i)
            if (auto *material = mesh->GetMaterial(i))
                materials.push_back(out.Row(material));
        out.Rows("users", std::move(users));
        out.Rows("channels", std::move(channels));
        out.Rows("materials", std::move(materials));
        return true;
    }
    if (section == "grid") {
        auto *grid = CKGrid::Cast(object);
        if (!grid)
            return false;
        auto *manager = static_cast<CKGridManager *>(context->GetManagerByGuid(GRID_MANAGER_GUID));
        auto *attributes = context->GetAttributeManager();
        auto *parameters = context->GetParameterManager();
        const int width = grid->GetWidth(), length = grid->GetLength();
        out.Integer("width", width);
        out.Integer("length", length);
        out.Integer("manager", manager != nullptr);
        out.Integer("attributes", attributes != nullptr);
        std::vector<EditorField> types, layers, classifications, choices;
        for (int type = 1; manager && type < manager->GetLayerTypeCount(); ++type) {
            const auto *name = manager->GetTypeName(type);
            if (!name)
                continue;
            SetupDescription row{out.document};
            row.Integer("type", type);
            row.Text("name", AnsiToUtf8(name));
            VxColor color(1, 1, 0, 1);
            manager->GetAssociatedColor(type, &color);
            row.Numbers("color", {color.r, color.g, color.b, color.a});
            const auto guid = manager->GetAssociatedParam(type);
            const bool linker = guid == CKPGUID_LINKERGRAPH_ENUM;
            const auto parameterType = parameters ? parameters->ParameterGuidToType(guid) : -1;
            auto *description =
                parameters && parameterType >= 0 ? parameters->GetParameterTypeDescription(parameterType) : nullptr;
            row.Integer("linker", linker);
            row.Text("parameter", linker        ? "Linker"
                                  : description ? AnsiToUtf8(description->TypeName.CStr())
                                                : "Integer");
            EditorField value{std::to_string(type), EDITOR_RECORD};
            value.fields = std::move(row.fields);
            types.push_back(std::move(value));
        }
        for (int i = 0; i < grid->GetLayerCount(); ++i) {
            auto *layer = grid->GetLayerByIndex(i);
            SetupDescription row{out.document};
            row.Link("target", layer);
            row.Integer("type", layer ? layer->GetType() : 0);
            row.Integer("visible", layer && layer->IsVisible());
            EditorField squares{"squares", EDITOR_INTEGER_LIST};
            for (int y = 0; y < length; ++y)
                for (int x = 0; x < width; ++x) {
                    int value = 0;
                    if (layer)
                        layer->GetValue2(x, y, &value);
                    squares.integers.push_back(uint64_t(int64_t(value)));
                }
            row.fields.push_back(std::move(squares));
            EditorField value{std::to_string(i), EDITOR_RECORD};
            value.fields = std::move(row.fields);
            layers.push_back(std::move(value));
        }
        const int category = manager ? GridClassificationCategory(manager) : -1;
        for (int type = 0; attributes && type < attributes->GetAttributeCount(); ++type)
            if (attributes->IsAttributeIndexValid(type) && attributes->GetAttributeCategoryIndex(type) == category) {
                SetupDescription row{out.document};
                row.Integer("type", type);
                const auto *name = attributes->GetAttributeNameByType(type);
                row.Text("name", name ? AnsiToUtf8(name) : "?");
                row.Integer("held", grid->HasAttribute(type));
                EditorField value{std::to_string(type), EDITOR_RECORD};
                value.fields = std::move(row.fields);
                classifications.push_back(std::move(value));
            }
        auto *enumeration =
            parameters ? parameters->GetEnumDescByType(parameters->ParameterGuidToType(CKPGUID_LINKERGRAPH_ENUM))
                       : nullptr;
        for (int i = 0; enumeration && i < enumeration->GetNumEnums(); ++i) {
            SetupDescription row{out.document};
            row.Text("name", AnsiToUtf8(enumeration->GetEnumDescription(i)));
            row.Integer("value", enumeration->GetEnumValue(i));
            EditorField value{std::to_string(i), EDITOR_RECORD};
            value.fields = std::move(row.fields);
            choices.push_back(std::move(value));
        }
        out.Rows("types", std::move(types));
        out.Rows("layers", std::move(layers));
        out.Rows("classifications", std::move(classifications));
        out.Rows("linker-values", std::move(choices));
        return true;
    }
    return false;
}
} // namespace vdev

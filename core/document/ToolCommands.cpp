#include "core/document/ToolCommands.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>

#include "core/runtime/CkRuntimeHost.h"
#include "core/level/LevelCommands.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/graph/NmsCommands.h"
#include "core/document/ScriptSession.h"
#include "core/runtime/TextEncoding.h"
#include "core/document/UndoHistory.h"

namespace vdev {

namespace fs = std::filesystem;

namespace {

std::string Lower(std::string text) {
    for (char &c : text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return text;
}

// Dev's marker: "%d\n%s\n", hidden (Dev.exe sub_57AB50).
bool WriteMarker(const fs::path &folder, CK_CLASSID cid, const char *name) {
    const fs::path marker = folder / "_marker_";
    FILE *file = fopen(marker.u8string().c_str(), "wt");
    if (!file) return false;
    std::fprintf(file, "%d\n%s\n", static_cast<int>(cid), name);
    std::fclose(file);
    #ifdef _WIN32
    SetFileAttributesW(marker.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
    return true;
}

void ReadFolder(DataResourceFolder &folder) {
    folder.cid = DataResourceFolderClass(folder.path);
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(folder.path), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().u8string();
        if (name == "_marker_" || name == "_nemo.dir") continue;
        // The behavior files' descriptions (Dev.exe sub_561DD0).
        if (!it->is_directory(ec) && _stricmp(it->path().extension().u8string().c_str(), ".dsc") == 0) continue;
        if (it->is_directory(ec)) {
            DataResourceFolder child;
            child.name = name;
            child.path = it->path().u8string();
            ReadFolder(child);
            folder.folders.push_back(std::move(child));
        } else {
            folder.files.push_back(name);
        }
    }
    auto byName = [](const std::string &a, const std::string &b) { return _stricmp(a.c_str(), b.c_str()) < 0; };
    std::sort(folder.files.begin(), folder.files.end(), byName);
    std::sort(folder.folders.begin(), folder.folders.end(),
              [&](const DataResourceFolder &a, const DataResourceFolder &b) { return byName(a.name, b.name); });
}

int AddFolderPaths(CKPathManager *paths, const DataResourceFolder &folder) {
    int added = 0;
    if (folder.cid) {
        // Dev's categories: 0 bitmaps, 1 data, 2 sounds.
        int category = 1;
        if (folder.cid == CKCID_SOUND || folder.cid == CKCID_WAVESOUND || folder.cid == CKCID_MIDISOUND) category = 2;
        else if (folder.cid == CKCID_SPRITE || folder.cid == CKCID_TEXTURE || folder.cid == CKCID_SPRITE3D) category = 0;
        XString path(const_cast<CKSTRING>(folder.path.c_str()));
        if (category < paths->GetCategoryCount() && paths->GetPathIndex(category, path) < 0 &&
            paths->AddPath(category, path) >= 0)
            ++added;
    }
    for (const auto &child : folder.folders) added += AddFolderPaths(paths, child);
    return added;
}

std::string FileStem(const std::string &path) {
    return fs::u8path(path).stem().u8string();
}

bool IsMovie(const std::string &path) {
    const std::string extension = Lower(fs::u8path(path).extension().u8string());
    return extension == ".avi" || extension == ".mpg" || extension == ".mov";
}

} // namespace

bool CreateDataResource(const std::string &file, std::string *error) {
    // Dev: the folder is the file name without ".rsc".
    fs::path rsc = fs::u8path(file);
    if (Lower(rsc.extension().u8string()) != ".rsc") rsc += ".rsc";
    fs::path root = rsc;
    root.replace_extension();
    std::error_code ec;
    fs::create_directories(root, ec);
    FILE *out = fopen(rsc.u8string().c_str(), "wt");
    if (!out) {
        if (error) *error = "Cannot write " + rsc.u8string();
        return false;
    }
    std::fprintf(out, "Virtools RSC File 1.0\n");
    std::fclose(out);
    // Dev's folders ([CK]<Class Names 2>), in Dev's order, with their markers
    // ([CK]<Class Names> for the marker text).
    struct Folder {
        const char *path;
        CK_CLASSID cid;
        const char *className;
    };
    static const Folder folders[] = {
        {"2D Sprites", CKCID_SPRITE, "2D Sprite"},
        {"Behavior Graphs", CKCID_BEHAVIOR, "Behavior"},
        {"Characters", CKCID_CHARACTER, "Character"},
        {"Characters/Animations", CKCID_ANIMATION, "Animation"},
        {"3D Entities", CKCID_3DENTITY, "3D Entity"},
        {"3D Entities/Animations", CKCID_ANIMATION, "Animation"},
        {"3D Sprites", CKCID_SPRITE3D, "3D Sprite"},
        {"Textures", CKCID_TEXTURE, "Texture"},
        {"Sounds", CKCID_SOUND, "Sound"},
    };
    for (const Folder &folder : folders) {
        const fs::path path = root / folder.path;
        fs::create_directories(path, ec);
        if (!WriteMarker(path, folder.cid, folder.className)) {
            if (error) *error = "Cannot write the marker of " + path.u8string();
            return false;
        }
    }
    return true;
}

CK_CLASSID DataResourceFolderClass(const std::string &folder) {
    for (const char *name : {"_marker_", "_nemo.dir"}) {
        FILE *file = fopen((fs::u8path(folder) / name).u8string().c_str(), "rt");
        if (!file) continue;
        int cid = 0;
        if (std::fscanf(file, "%d", &cid) != 1) cid = 0;
        std::fclose(file);
        return static_cast<CK_CLASSID>(cid);
    }
    return 0;
}

std::string BehaviorDescriptionPath(const std::string &nms) {
    return (nms.size() >= 3 ? nms.substr(0, nms.size() - 3) : nms) + "dsc";
}

bool ReadBehaviorDescription(const std::string &nms, BehaviorDescription &description) {
    FILE *file = fopen(BehaviorDescriptionPath(nms).c_str(), "rb");
    if (!file) return false;
    BehaviorDescription read;
    int cid = 0;
    if (std::fread(&cid, 4, 1, file) != 1 || std::fread(&read.version, 4, 1, file) != 1) {
        std::fclose(file);
        return false;
    }
    read.cid = static_cast<CK_CLASSID>(cid);
    auto line = [file](std::string &text) {
        for (int c; (c = std::fgetc(file)) != EOF && c != '\n';) text += static_cast<char>(c);
    };
    line(read.name);
    line(read.author);
    std::string rest;
    for (int c; (c = std::fgetc(file)) != EOF;) rest += static_cast<char>(c);
    std::fclose(file);
    if (!rest.empty()) rest.pop_back();
    for (char c : rest)
        if (c != '\r') read.description += c;
    description = read;
    return true;
}

bool WriteBehaviorDescription(const std::string &nms, const BehaviorDescription &description) {
    FILE *file = fopen(BehaviorDescriptionPath(nms).c_str(), "wb");
    if (!file) return false;
    const int cid = static_cast<int>(description.cid);
    std::fwrite(&cid, 4, 1, file);
    std::fwrite(&description.version, 4, 1, file);
    std::string text;
    for (char c : description.description) {
        if (c == '\n') text += '\r';
        if (c != '\r') text += c;
    }
    std::fprintf(file, "%s\n%s\n%s\n", description.name.c_str(), description.author.c_str(), text.c_str());
    return std::fclose(file) == 0;
}

bool ReadDataResource(const std::string &file, DataResource &resource, std::string *error) {
    resource = DataResource();
    FILE *in = fopen(file.c_str(), "rt");
    char header[64] = "";
    const bool read = in && std::fgets(header, sizeof(header), in);
    if (in) std::fclose(in);
    if (!read || std::strncmp(header, "Virtools RSC File", 17) != 0) {
        if (error) *error = file + " is not a Virtools Data Resource";
        return false;
    }
    fs::path root = fs::u8path(file);
    root.replace_extension();
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        if (error) *error = "The Data Resource folder " + root.u8string() + " is missing";
        return false;
    }
    resource.file = file;
    resource.root.name = root.filename().u8string();
    resource.root.path = root.u8string();
    ReadFolder(resource.root);
    return true;
}

int AddDataResourcePaths(CKContext *context, const DataResource &resource) {
    CKPathManager *paths = context ? context->GetPathManager() : nullptr;
    return paths ? AddFolderPaths(paths, resource.root) : 0;
}

bool ResourceFileIsImported(CK_CLASSID cid) {
    if (cid == CKCID_2DENTITY) return false; // an image dropped as a 2D Entity
    for (CK_CLASSID own : {CKCID_BEHAVIOR, CKCID_ANIMATION, CKCID_SPRITE3D, CKCID_SPRITE, CKCID_TEXTURE, CKCID_SOUND})
        if (CKIsChildClassOf(cid, own)) return false;
    return true;
}

EditResult LoadResourceFile(Document &document, const std::string &path, CK_CLASSID cid,
                            CK_ID target, std::vector<CK_ID> *loaded, const std::vector<DuplicateName> *duplicates) {
    CKContext *context = document.Context();
    CKLevel *level = context ? context->GetCurrentLevel() : nullptr;
    if (!document.IsOpen() || !level) return EditResult::Refused("No composition is open");
    CkRuntimeHost &host = document.Host();
    const std::string name = FileStem(path);
    std::vector<CK_ID> created;
    auto create = [&](CK_CLASSID kind) {
        std::string ckName;
        try { ckName = Utf8ToAnsi(name); }
        catch (const TextEncodingError &) {
            // The path remains usable even when its suggested object name
            // does not fit the session encoding. CK generates its own name.
            host.Log(LogLevel::Info, "The file name cannot be used as a CK object name in " +
                                      std::string("the ANSI code page; using a generated name"));
        }
        CKObject *object = context->CreateObject(kind, ckName.empty() ? nullptr : const_cast<CKSTRING>(ckName.c_str()));
        if (object) created.push_back(object->GetID());
        return object;
    };
    auto discard = [&](CKObject *object) {
        created.erase(std::remove(created.begin(), created.end(), object->GetID()), created.end());
        GuardedCall([&] { context->DestroyObject(object); }, &host, "CKContext::DestroyObject");
    };
    auto finish = [&](const std::string &what) {
        for (CK_ID id : created)
            if (CKObject *object = context->GetObject(id)) level->AddObject(object);
        document.AdoptObjects(created);
        if (loaded) loaded->insert(loaded->end(), created.begin(), created.end());
        return EditResult::Applied(what, created.empty() ? 0 : created.front());
    };
    if (CKIsChildClassOf(cid, CKCID_BEHAVIOR)) {
        // Behavior graph files: their Scripts go to an object.
        if (!target) return EditResult::Refused("Select the object that receives the Scripts");
        return LoadScripts(document, path, target);
    }
    if (CKIsChildClassOf(cid, CKCID_ANIMATION)) {
        CKCharacter *character = CKCharacter::Cast(context->GetObject(target));
        if (!character) return EditResult::Refused("Select the character that receives the animations");
        CKObjectArray *objects = CreateCKObjectArray();
        CKERROR result = CKERR_INVALIDFILE;
        GuardedCall([&] { result = context->LoadAnimationOnCharacter(const_cast<CKSTRING>(path.c_str()), objects, character); },
                    &host, "CKContext::LoadAnimationOnCharacter");
        for (objects->Reset(); !objects->EndOfList(); objects->Next()) created.push_back(objects->GetDataId());
        DeleteCKObjectArray(objects);
        if (result != CK_OK) {
            host.Log(LogLevel::Warning, "Cannot load " + path + " (CKERROR " + std::to_string(result) + ")");
            return EditResult::Refused("Cannot load animation (CKERROR " + std::to_string(result) + ")");
        }
        return finish("Animations of");
    }
    if (CKIsChildClassOf(cid, CKCID_SPRITE3D)) {
        CKTexture *texture = CKTexture::Cast(create(CKCID_TEXTURE));
        CKBOOL ok = FALSE;
        if (texture)
            GuardedCall([&] { ok = IsMovie(path) ? texture->LoadMovie(const_cast<CKSTRING>(path.c_str()))
                                                 : texture->LoadImage(const_cast<CKSTRING>(path.c_str()), 0); },
                        &host, "CKTexture::LoadImage");
        if (!ok) {
            if (texture) discard(texture);
            return EditResult::Refused("Cannot load Sprite 3D!");
        }
        CKSprite3D *sprite = CKSprite3D::Cast(create(CKCID_SPRITE3D));
        CKMaterial *material = CKMaterial::Cast(create(CKCID_MATERIAL));
        if (sprite && material) {
            material->SetSpecular(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
            material->SetDiffuse(VxColor(1.0f, 1.0f, 1.0f, 1.0f));
            sprite->SetMaterial(material);
            material->SetTexture0(texture);
        }
        return finish("3D Sprite");
    }
    if (CKIsChildClassOf(cid, CKCID_SPRITE)) {
        CKSprite *sprite = CKSprite::Cast(create(CKCID_SPRITE));
        CKBOOL ok = FALSE;
        if (sprite)
            GuardedCall([&] { ok = IsMovie(path) ? sprite->LoadMovie(const_cast<CKSTRING>(path.c_str()), 0, 0, 16)
                                                 : sprite->LoadImage(const_cast<CKSTRING>(path.c_str()), 0); },
                        &host, "CKSprite::LoadImage");
        if (!ok) {
            if (sprite) discard(sprite);
            return EditResult::Refused("Cannot load Sprite!");
        }
        return finish("2D Sprite");
    }
    if (cid == CKCID_2DENTITY) {
        // An image dropped on the 3D Layout with Shift and Ctrl (Dev.exe
        // sub_4BF320): a texture, a 2D Entity of its size and a material
        // showing it (specular black, diffuse white, no Z write, Z test always).
        CKTexture *texture = CKTexture::Cast(create(CKCID_TEXTURE));
        CKBOOL ok = FALSE;
        if (texture)
            GuardedCall([&] { ok = IsMovie(path) ? texture->LoadMovie(const_cast<CKSTRING>(path.c_str()))
                                                 : texture->LoadImage(const_cast<CKSTRING>(path.c_str()), 0); },
                        &host, "CKTexture::LoadImage");
        if (!ok) {
            if (texture) discard(texture);
            return EditResult::Refused("Cannot load Texture!");
        }
        CK2dEntity *entity = CK2dEntity::Cast(create(CKCID_2DENTITY));
        CKMaterial *material = CKMaterial::Cast(create(CKCID_MATERIAL));
        if (entity && material) {
            entity->SetSize(Vx2DVector(static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight())));
            entity->SetMaterial(material);
            material->SetSpecular(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
            material->SetDiffuse(VxColor(1.0f, 1.0f, 1.0f, 1.0f));
            material->SetTexture0(texture);
            material->EnableZWrite(FALSE);
            material->SetZFunc(VXCMP_ALWAYS);
        }
        return finish("2D Entity");
    }
    if (CKIsChildClassOf(cid, CKCID_TEXTURE)) {
        CKTexture *texture = CKTexture::Cast(create(CKCID_TEXTURE));
        CKBOOL ok = FALSE;
        if (texture)
            GuardedCall([&] { ok = IsMovie(path) ? texture->LoadMovie(const_cast<CKSTRING>(path.c_str()))
                                                 : texture->LoadImage(const_cast<CKSTRING>(path.c_str()), 0); },
                        &host, "CKTexture::LoadImage");
        if (!ok) {
            if (texture) discard(texture);
            return EditResult::Refused("Cannot load Texture!");
        }
        return finish("Texture");
    }
    if (CKIsChildClassOf(cid, CKCID_SOUND)) {
        // A wave sound (streamed from the file); a MIDI file becomes a MIDI sound.
        CKWaveSound *wave = CKWaveSound::Cast(create(CKCID_WAVESOUND));
        CKERROR result = CKERR_INVALIDFILE;
        if (wave)
            GuardedCall([&] { result = wave->Create(TRUE, const_cast<CKSTRING>(path.c_str())); }, &host,
                        "CKWaveSound::Create");
        if (wave && result == CK_OK) {
            wave->SetMinMaxDistance(10.0f, 100.0f, FALSE);
            return finish("Sound");
        }
        if (wave) discard(wave);
        if (Lower(fs::u8path(path).extension().u8string()) == ".mid") {
            CKMidiSound *midi = CKMidiSound::Cast(create(CKCID_MIDISOUND));
            if (midi) midi->SetSoundFileName(const_cast<CKSTRING>(path.c_str()));
            return finish("MIDI Sound");
        }
        return EditResult::Refused("Cannot load Sound!");
    }
    // 3D entities, characters and the rest: the file is imported.
    const Document::ImportMode mode = CKIsChildClassOf(cid, CKCID_CHARACTER)
                                                   ? Document::ImportMode::Character
                                                   : Document::ImportMode::File;
    std::string error;
    std::vector<CK_ID> imported;
    if (!document.Import(path, mode, &error, &imported, duplicates)) {
        host.Log(LogLevel::Warning, error);
        return EditResult::Refused("Cannot import the resource file");
    }
    if (loaded) loaded->insert(loaded->end(), imported.begin(), imported.end());
    return EditResult::Applied("Imported", imported.empty() ? 0 : imported.front());
}

namespace {

// Below a Script's top-level behaviors, at their left (or right of the
// Start when there is none): where a dropped graph file goes.
Point FreeSpot(Document &document, CK_ID script) {
    ScriptSession *session = document.Session(script);
    if (!session) return Point(160.0f, 60.0f);
    const InterfaceData &data = session->Data();
    float left = 0.0f, bottom = 0.0f;
    bool any = false;
    for (const BehaviorData &record : data.behaviors) {
        if (record.parentId != data.rootBehavior.id) continue;
        left = any ? std::min(left, record.rect.hPos) : record.rect.hPos;
        bottom = any ? std::max(bottom, record.rect.Bottom()) : record.rect.Bottom();
        any = true;
    }
    if (!any) return Point(data.rootBehavior.rect.hPos + 160.0f, data.rootBehavior.rect.vPos + 60.0f);
    return Point(left, bottom + 40.0f);
}

EditResult DropBehaviorFile(Document &document, const std::string &path, CK_ID row) {
    CKContext *context = document.Context();
    if (!row) return EditResult::Refused("Cannot drop behavior on folders !");
    CKObject *object = context->GetObject(row);
    CKBehavior *script = CKBehavior::Cast(object);
    CKBeObject *owner = script ? script->GetOwner() : CKBeObject::Cast(object);
    if (!owner) return EditResult::Refused("Can't assign behavior: drop target is not an entity !");
    auto editable = [&](CKBehavior *behavior) {
        ScriptSession *session = behavior && document.FindScript(behavior->GetID()) ? document.Session(behavior->GetID()) : nullptr;
        return session && !session->ReadOnly();
    };
    if (script && !editable(script)) return EditResult::Refused("Cannot drop on script with hidden graph!");
    // The file's Scripts go to the object.
    EditResult scripts = LoadScripts(document, path, owner->GetID());
    if (scripts.ok || scripts.message.rfind(NO_SCRIPT_IN_FILE, 0) != 0) return scripts;
    // Its graphs and Building Blocks go into a Script of the object.
    CK_ID into = script ? script->GetID() : 0;
    for (int i = 0; !into && i < owner->GetScriptCount(); ++i)
        if (editable(owner->GetScript(i))) into = owner->GetScript(i)->GetID();
    // A Script made for it and the import: one undo step.
    Edit edit(document, "Load Behavior Graph");
    if (!into) {
        const EditResult created = CreateScript(document, owner->GetID(), std::string());
        if (!created.ok) return created;
        into = created.created;
    }
    EditResult imported = ImportBehaviorGraph(document, into, into, path, FreeSpot(document, into));
    if (!imported.ok) return imported;
    const EditResult committed = edit.Commit(imported.message, imported.created);
    return committed.ok ? imported : committed;
}

} // namespace

EditResult DropResourceFile(Document &document, const std::string &path, CK_CLASSID cid,
                            const LevelDropTarget &target, const std::vector<DuplicateName> *duplicates) {
    CKContext *context = document.Context();
    CKLevel *level = context ? context->GetCurrentLevel() : nullptr;
    if (!document.IsOpen() || !level) return EditResult::Refused("No composition is open");
    if (cid <= 0 || CKIsChildClassOf(cid, CKCID_BEHAVIOR)) return DropBehaviorFile(document, path, target.row);
    CkRuntimeHost &host = document.Host();
    // The row's object, else the nearest above it, of a class (sub_571440).
    auto nearest = [&](CK_CLASSID kind) -> CKObject * {
        if (CKObject *object = context->GetObject(target.row))
            if (CKIsChildClassOf(object, kind)) return object;
        for (CK_ID id : target.above)
            if (CKObject *object = context->GetObject(id))
                if (CKIsChildClassOf(object, kind)) return object;
        return nullptr;
    };
    if (CKIsChildClassOf(cid, CKCID_ANIMATION)) {
        CK3dEntity *entity = CK3dEntity::Cast(nearest(CKCID_3DENTITY));
        if (CKCharacter::Cast(entity)) return LoadResourceFile(document, path, cid, entity->GetID());
        // CK_LOAD_ANIMATION only (LoadNmo with 1).
        CKObjectArray *objects = CreateCKObjectArray();
        CKERROR result = CKERR_INVALIDFILE;
        GuardedCall([&] { result = context->Load(const_cast<CKSTRING>(path.c_str()), objects, CK_LOAD_ANIMATION); },
                    &host, "CKContext::Load");
        std::vector<CK_ID> loaded;
        for (objects->Reset(); !objects->EndOfList(); objects->Next()) loaded.push_back(objects->GetDataId());
        DeleteCKObjectArray(objects);
        if (result != CK_OK) return EditResult::Refused("Cannot load Animation!");
        const std::string name = entity && entity->GetName() ? std::string(entity->GetName()) + " Animation" : std::string();
        for (CK_ID id : loaded) {
            CKObject *object = context->GetObject(id);
            if (entity) {
                if (CKObjectAnimation *animation = CKObjectAnimation::Cast(object)) {
                    animation->Set3dEntity(entity);
                    if (!animation->GetName()) animation->SetName(const_cast<CKSTRING>(name.c_str()));
                }
            } else if (CKAnimation *animation = CKAnimation::Cast(object)) {
                level->AddObject(animation);
            }
        }
        document.AdoptObjects(loaded);
        document.MarkTopologyChanged();
        return EditResult::Applied("Animations", loaded.empty() ? 0 : loaded.front());
    }
    const bool imported = ResourceFileIsImported(cid);
    if (!imported && !CKIsChildClassOf(cid, CKCID_SPRITE3D) && !CKIsChildClassOf(cid, CKCID_SPRITE) &&
        !CKIsChildClassOf(cid, CKCID_TEXTURE) && !CKIsChildClassOf(cid, CKCID_SOUND))
        return EditResult::Refused("Nothing to load from this folder");
    std::vector<CK_ID> loaded;
    EditResult result = LoadResourceFile(document, path, cid, 0, &loaded, duplicates);
    if (!result.ok) return result;
    CKScene *sectionScene = CKScene::Cast(context->GetObject(target.scene));
    if (sectionScene == level->GetLevelScene()) sectionScene = nullptr;
    CKObject *row = context->GetObject(target.row);
    auto toSection = [&](CKObject *object) {
        if (sectionScene && CKSceneObject::Cast(object) && !CKScene::Cast(object) && !CKLevel::Cast(object))
            sectionScene->AddObjectToScene(CKSceneObject::Cast(object), TRUE);
    };
    if (!imported) {
        // sub_56B510: in the level (LoadResourceFile) and the section's scene.
        for (CK_ID id : loaded) toSection(context->GetObject(id));
    } else if (CKScene *scene = CKScene::Cast(row); scene && !CKLevel::Cast(row)) {
        for (CK_ID id : loaded) {
            CKObject *object = context->GetObject(id);
            if (CKBeObject::Cast(object) && !CKScene::Cast(object) && !CKLevel::Cast(object))
                scene->AddObjectToScene(CKSceneObject::Cast(object), TRUE);
        }
    } else if (CKPlace *place = CKPlace::Cast(row)) {
        XObjectPointerArray entities;
        for (CK_ID id : loaded)
            if (CKObject *object = context->GetObject(id))
                if (CKIsChildClassOf(object, CKCID_3DENTITY) && object != place) entities.PushBack(object);
        GuardedCall([&] { place->AddChildren(entities, TRUE); }, &host, "CK3dEntity::AddChildren");
        for (CK_ID id : loaded) toSection(context->GetObject(id));
    } else if (CKGroup *group = CKGroup::Cast(row)) {
        for (CK_ID id : loaded) {
            CKObject *object = context->GetObject(id);
            if (CKBeObject::Cast(object) && !CKScene::Cast(object) && !CKLevel::Cast(object) && object != group)
                group->AddObject(CKBeObject::Cast(object));
            toSection(object);
        }
    }
    document.MarkTopologyChanged();
    return result;
}

std::vector<UnusedObject> UnusedObjects(Document &document, std::string *error) {
    std::vector<UnusedObject> result;
    CKContext *context = document.Context();
    if (!document.IsOpen() || !context) {
        if (error) *error = "No composition is open";
        return result;
    }
    // CK2's CKFillObjectsUnused (0x2403B304) starts from the player render
    // context, which the editor does not have (it would crash); the same
    // walk here. Used: the dependencies of what a render context holds - the
    // render objects of the current scene - and of every data array, group
    // and behavior. Unused: the rest, but render contexts, interface
    // objects, scenes and levels; scene objects only when they are in the
    // current scene. CK2 collects the dependencies in a CKDependenciesContext
    // (build mode, full dependencies); its tables are allocated by CK2's
    // runtime and cannot be freed here, so a CKFile saving the same objects
    // (written to a temporary file) collects them instead.
    CKLevel *level = context->GetCurrentLevel();
    CKScene *scene = level ? level->GetCurrentScene() : nullptr;
    if (!scene && level) scene = level->GetLevelScene();
    std::set<CK_ID> used;
    CKObjectArray *seeds = CreateCKObjectArray();
    if (scene)
        for (CKSceneObjectIterator it = scene->GetObjectIterator(); !it.End(); it++)
            if (CKRenderObject::Cast(context->GetObject(it.GetObjectID()))) seeds->InsertRear(it.GetObjectID());
    for (CK_CLASSID cid : {CKCID_DATAARRAY, CKCID_GROUP, CKCID_BEHAVIOR}) {
        const XObjectPointerArray &list = context->GetObjectListByType(cid, FALSE);
        for (int i = 0; i < list.Size(); ++i)
            if (list[i] && !document.IsDiscarded(list[i]->GetID())) seeds->InsertRear(list[i]->GetID());
    }
    // Every step has to succeed: what the walk misses would be listed unused.
    std::string failure;
    std::string path;
#ifdef _WIN32
    wchar_t directory[MAX_PATH] = L"", temporary[MAX_PATH] = L"";
    if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"unu", 0, temporary))
        failure = "Cannot make a temporary file";
    else path = WideToUtf8(temporary);
#else
    std::error_code temporaryError;
    const fs::path directory = fs::temp_directory_path(temporaryError);
    if (temporaryError) failure = "Cannot locate the temporary folder";
    else {
        std::string pattern = (directory / "virtoolsdev-unused-XXXXXX").u8string();
        const int descriptor = mkstemp(&pattern[0]);
        if (descriptor < 0) failure = "Cannot make a temporary file";
        else { close(descriptor); path = pattern; }
    }
#endif
    if (failure.empty()) {
        const int seedCount = seeds->GetCount();
        CKFile *file = context->CreateCKFile();
        CKERROR saved = CKERR_INVALIDFILE;
        const bool completed = GuardedCall(
            [&] {
                if ((saved = file->StartSave(const_cast<CKSTRING>(path.c_str()))) != CK_OK) return;
                seeds->Reset();
                file->SaveObjects(seeds);
                for (int i = 0; i < file->m_FileObjects.Size(); ++i)
                    if (CKObject *object = file->m_FileObjects[i].ObjPtr) used.insert(object->GetID());
                file->EndSave();
            },
            &document.Host(), "unused objects");
        context->DeleteCKFile(file);
        if (!completed) failure = "Collecting the dependencies raised an exception";
        else if (saved != CK_OK) failure = "Collecting the dependencies failed (CKERROR " + std::to_string(saved) + ")";
        else if (seedCount > 0 && used.empty()) failure = "Collecting the dependencies found none";
    }
    DeleteCKObjectArray(seeds);
    if (!path.empty()) { std::error_code ignored; fs::remove(fs::u8path(path), ignored); }
    if (!failure.empty()) {
        if (error) *error = failure;
        return result;
    }
    std::vector<CKObject *> unused;
    const XObjectPointerArray &all = context->GetObjectListByType(CKCID_OBJECT, TRUE);
    for (int i = 0; i < all.Size(); ++i) {
        CKObject *object = all[i];
        if (!object || used.count(object->GetID())) continue;
        if (CKIsChildClassOf(object, CKCID_RENDERCONTEXT) || CKIsChildClassOf(object, CKCID_INTERFACEOBJECTMANAGER) ||
            CKIsChildClassOf(object, CKCID_SCENE) || CKIsChildClassOf(object, CKCID_LEVEL))
            continue;
        if (CKSceneObject *sceneObject = CKSceneObject::Cast(object))
            if (!scene || !sceneObject->IsInScene(scene)) continue;
        unused.push_back(object);
    }
    for (CKObject *object : unused) {
        if (!object || object->IsPrivate() || document.IsDiscarded(object->GetID())) continue;
        const CK_CLASSID cid = object->GetClassID();
        // Dev lists these classes only.
        if (cid != CKCID_MESH && cid != CKCID_MATERIAL && cid != CKCID_TEXTURE && cid != CKCID_ANIMATION &&
            !CKIsChildClassOf(cid, CKCID_OBJECTANIMATION) && cid != CKCID_CURVEPOINT)
            continue;
        result.push_back({object->GetID(), cid, object->GetName() ? object->GetName() : ""});
    }
    return result;
}

VersionTestResult TestBuildingBlockVersions(Document &document) {
    VersionTestResult result;
    CKContext *context = document.Context();
    if (!document.IsOpen() || !context) return result;
    std::string &report = result.report;
    report = "-----Building Blocks Version Test-----\n";
    std::set<std::pair<CKDWORD, CKDWORD>> reported;
    bool missingReported = false;
    // Script of every behavior.
    std::map<CK_ID, CK_ID> scriptOf;
    for (const auto &script : document.Scripts()) {
        std::vector<CKBehavior *> pending{CKBehavior::Cast(context->GetObject(script.id))};
        while (!pending.empty()) {
            CKBehavior *behavior = pending.back();
            pending.pop_back();
            if (!behavior) continue;
            scriptOf[behavior->GetID()] = script.id;
            for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) pending.push_back(behavior->GetSubBehavior(i));
        }
    }
    const int count = context->GetObjectsCountByClassID(CKCID_BEHAVIOR);
    const CK_ID *ids = context->GetObjectsListByClassID(CKCID_BEHAVIOR);
    for (int i = 0; i < count; ++i) {
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(ids[i]));
        if (!behavior || !(behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK)) continue;
        if (document.IsDiscarded(behavior->GetID())) continue; // also inside a deleted Script or graph
        const CKGUID guid = behavior->GetPrototypeGuid();
        CKBehaviorPrototype *prototype = CKGetPrototypeFromGuid(guid);
        CKObjectDeclaration *declaration = CKGetObjectDeclarationFromGuid(guid);
        const std::string name = behavior->GetName() ? behavior->GetName() : "";
        if (!prototype || !declaration) {
            ++result.withoutPrototype;
            if (!missingReported) {
                report += "Building Block " + name + " has no prototype (Dll not here)\n";
                missingReported = true;
            }
            continue;
        }
        const CKDWORD flags = behavior->GetFlags();
        const bool same =
            behavior->GetVersion() == declaration->GetVersion() &&
            ((flags & (CKBEHAVIOR_INTERNALLYCREATEDINPUTS | CKBEHAVIOR_VARIABLEINPUTS)) ||
             prototype->GetInputCount() == behavior->GetInputCount()) &&
            ((flags & (CKBEHAVIOR_INTERNALLYCREATEDOUTPUTS | CKBEHAVIOR_VARIABLEOUTPUTS)) ||
             prototype->GetOutputCount() == behavior->GetOutputCount()) &&
            ((flags & (CKBEHAVIOR_INTERNALLYCREATEDINPUTPARAMS | CKBEHAVIOR_VARIABLEPARAMETERINPUTS)) ||
             prototype->GetInParameterCount() == behavior->GetInputParameterCount()) &&
            ((flags & (CKBEHAVIOR_INTERNALLYCREATEDOUTPUTPARAMS | CKBEHAVIOR_VARIABLEPARAMETEROUTPUTS)) ||
             prototype->GetOutParameterCount() == behavior->GetOutputParameterCount()) &&
            ((flags & CKBEHAVIOR_INTERNALLYCREATEDLOCALPARAMS) ||
             prototype->GetLocalParameterCount() == behavior->GetLocalParameterCount());
        const bool first = reported.insert({guid.d1, guid.d2}).second;
        if (same) {
            if (prototype->GetFlags() & CK_BEHAVIORPROTOTYPE_OBSOLETE) {
                ++result.obsolete;
                if (first) report += "Building Block's Prototype " + name + " is obsolete\n";
            }
            continue;
        }
        auto script = scriptOf.find(behavior->GetID());
        if (script != scriptOf.end()) result.changed.emplace_back(script->second, behavior->GetID());
        if (first) report += "Building Block " + name + " - Version Differences\n";
    }
    if (result.changed.empty() && !result.obsolete && !result.withoutPrototype) report += "No error found\n";
    return result;
}

std::vector<CK_ID> BehavioralObjects(CKContext *context) {
    std::vector<CK_ID> objects;
    CKBehaviorManager *behaviors = context ? context->GetBehaviorManager() : nullptr;
    for (int i = 0; behaviors && i < behaviors->GetObjectsCount(); ++i)
        if (CKBeObject *object = behaviors->GetObject(i)) objects.push_back(object->GetID());
    return objects;
}

std::vector<PluginCategory> InstalledPlugins(CKContext *context) {
    std::vector<PluginCategory> categories;
    CKPluginManager *plugins = CKGetPluginManager();
    if (!plugins) return categories;
    static const int order[] = {0, 6, 1, 2, 5, 3, 4, 7};
    const int count = plugins->GetCategoryCount();
    for (int position = 0; position < count; ++position) {
        const int category = position < 8 ? order[position] : position;
        if (category >= count) continue;
        PluginCategory entry;
        const char *categoryName = plugins->GetCategoryName(category);
        entry.name = categoryName ? categoryName : "";
        for (int i = 0; i < plugins->GetPluginCount(category); ++i) {
            CKPluginEntry *plugin = plugins->GetPluginInfo(category, i);
            if (!plugin) continue;
            PluginRow row;
            const CKPluginInfo &info = plugin->m_PluginInfo;
            row.name = info.m_Summary.CStr() ? info.m_Summary.CStr() : "";
            row.description = info.m_Description.CStr() ? info.m_Description.CStr() : "";
            row.author = info.m_Author.CStr() ? info.m_Author.CStr() : "";
            row.version = static_cast<int>(info.m_Version);
            row.dllIndex = plugin->m_PluginDllIndex;
            if (CKPluginDll *dll = plugins->GetPluginDllInfo(plugin->m_PluginDllIndex))
                row.dll = fs::u8path(dll->m_DllFileName.CStr() ? dll->m_DllFileName.CStr() : "").stem().u8string();
            if (category == CKPLUGIN_MANAGER_DLL) {
                // Managers: their activity in this context; the interface
                // manager cannot be switched off.
                CKBaseManager *manager =
                    context ? context->GetManagerByName(const_cast<CKSTRING>(row.name.c_str())) : nullptr;
                if (manager && !(manager->GetGuid().d1 == INTERFACE_MANAGER_GUID1 && manager->GetGuid().d2 == 0)) {
                    row.active = context->IsManagerActive(manager) ? 1 : 0;
                    for (int m = 0; m < context->GetManagerCount(); ++m)
                        if (context->GetManager(m) == manager) row.managerIndex = m;
                }
            } else {
                row.active = plugin->m_Active ? 1 : 0;
            }
            entry.plugins.push_back(row);
        }
        categories.push_back(std::move(entry));
    }
    return categories;
}

} // namespace vdev

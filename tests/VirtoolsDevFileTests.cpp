// The modes on files: save, New Composition and Merge, tools, Duplicate Names, the 3D Layout's data and the objects' pictures, the File menu, links.
#include "VirtoolsDevTestSupport.h"
#include <SDL3/SDL.h>

namespace vdevtest {

namespace {

std::vector<int> SpriteTextAlignments() {
    std::vector<int> values{CKSPRITETEXT_CENTER};
    for (int h : {CKSPRITETEXT_LEFT, CKSPRITETEXT_HCENTER, CKSPRITETEXT_RIGHT})
        for (int v : {CKSPRITETEXT_TOP, CKSPRITETEXT_VCENTER, CKSPRITETEXT_BOTTOM}) values.push_back(h | v);
    return values;
}

std::string AlignmentName(size_t index) { return "Persistent Alignment " + std::to_string(index); }

bool CheckSpriteTextAlignments(Document &document, const std::vector<int> &values = SpriteTextAlignments()) {
    int failures = 0;
    for (size_t i = 0; i < values.size(); ++i) {
        const std::string name = AlignmentName(i);
        auto *text = CKSpriteText::Cast(document.Context()->GetObjectByNameAndClass(
            const_cast<CKSTRING>(name.c_str()), CKCID_SPRITETEXT));
        const int actual = text ? properties::SpriteTextAlignment.get(text) : -1;
        if (actual != values[i]) {
            std::cerr << name << ": alignment " << actual << ", expected " << values[i] << '\n';
            ++failures;
        }
    }
    return failures == 0;
}

} // namespace

int SpriteTextSaveFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory) {
    std::filesystem::create_directories(outputDirectory);
    Document document(host);
    std::string error;
    CHECK(document.Open(path, &error));
    CKContext *context = document.Context();
    const auto values = SpriteTextAlignments();
    for (size_t i = 0; i < values.size(); ++i) {
        const std::string name = AlignmentName(i);
        auto *text = CKSpriteText::Cast(context->CreateObject(CKCID_SPRITETEXT, const_cast<CKSTRING>(name.c_str())));
        CHECK(text && text->Create(128, 64, 32, 0));
        text->SetFont((CKSTRING) "Arial", 12);
        text->SetText((CKSTRING) "Persistent text");
        text->SetAlign(static_cast<CKSPRITETEXT_ALIGNMENT>(values[i]));
        context->GetCurrentLevel()->AddObject(text);
        document.Include(text->GetID());
    }
    CHECK(CheckSpriteTextAlignments(document));
    SaveOptions options;
    options.allowOverwrite = true;
    SaveReport report;
    const std::string saved = (std::filesystem::path(outputDirectory) / "aligned.cmo").string();
    const bool ok = SaveDocumentAs(document, saved, options, report);
    for (const auto &message : report.errors) std::cerr << message << '\n';
    CHECK(ok);
    CHECK(report.spriteTextsVerified == static_cast<int>(values.size()));
    std::cout << "Saved ten Sprite Text alignments to " << saved << '\n';
    return 0;
}

int SpriteTextCheckFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory) {
    std::filesystem::create_directories(outputDirectory);
    std::string error;
    // Deliberately occupy IDs before loading: references in the metadata must
    // follow CK's remapping, rather than using IDs from the saving process.
    CKContext *context = host.Context();
    CKFile *inspection = context->CreateCKFile();
    CHECK(inspection->OpenFile(const_cast<CKSTRING>(path.c_str()), CK_LOAD_DEFAULT) == CK_OK);
    std::map<std::string, CK_ID> savedIds;
    for (int i = 0; i < inspection->m_FileObjects.Size(); ++i) {
        const auto &entry = inspection->m_FileObjects[i];
        if (entry.ObjectCid == CKCID_SPRITETEXT && entry.Name) savedIds[entry.Name] = entry.Object;
    }
    context->DeleteCKFile(inspection);
    for (int i = 0; i < 256; ++i) CHECK(context->CreateObject(CKCID_OBJECT));
    Document document(host);
    CHECK(document.Open(path, &error));
    CHECK(CheckSpriteTextAlignments(document));
    CHECK(!document.Dirty());
    bool remapped = false;
    for (const auto &entry : savedIds) {
        CKObject *text = context->GetObjectByNameAndClass(const_cast<CKSTRING>(entry.first.c_str()), CKCID_SPRITETEXT);
        CHECK(text);
        remapped |= text->GetID() != entry.second;
    }
    CHECK(remapped);
    SaveOptions options;
    options.allowOverwrite = true;
    SaveReport report;
    const std::string saved = (std::filesystem::path(outputDirectory) / "resaved.cmo").string();
    const bool ok = SaveDocumentAs(document, saved, options, report);
    for (const auto &message : report.errors) std::cerr << message << '\n';
    CHECK(ok);
    CHECK(report.spriteTextsVerified == 10);
    document.Close();
    Document reopened(host);
    CHECK(reopened.Open(saved, &error));
    CHECK(CheckSpriteTextAlignments(reopened));
    CHECK(!reopened.Dirty());
    const std::string changedName = AlignmentName(1);
    auto *changed = CKSpriteText::Cast(context->GetObjectByNameAndClass(
        const_cast<CKSTRING>(changedName.c_str()), CKCID_SPRITETEXT));
    auto expected = SpriteTextAlignments();
    expected[1] = CKSPRITETEXT_RIGHT | CKSPRITETEXT_BOTTOM;
    CHECK(EditObjectState(reopened, changed->GetID(), "Text Alignment", [&](CKObject *object) {
        properties::SpriteTextAlignment.set(object, expected[1]);
    }).ok);
    CHECK(reopened.History().Undo() && CheckSpriteTextAlignments(reopened));
    CHECK(reopened.History().Redo() && CheckSpriteTextAlignments(reopened, expected));
    const std::string changedFile = (std::filesystem::path(outputDirectory) / "changed.cmo").string();
    CHECK(SaveDocumentAs(reopened, changedFile, options, report));
    CHECK(report.spriteTextsVerified == 10);
    CHECK(reopened.Open(changedFile, &error) && CheckSpriteTextAlignments(reopened, expected));
    // Merge must restore new sprites without replaying the older metadata of
    // this document or overwriting an object for which Use Current was chosen.
    const std::string keptName = AlignmentName(0);
    auto *kept = CKSpriteText::Cast(context->GetObjectByNameAndClass(
        const_cast<CKSTRING>(keptName.c_str()), CKCID_SPRITETEXT));
    const CK_ID keptId = kept->GetID();
    properties::SpriteTextAlignment.set(kept, CKSPRITETEXT_RIGHT | CKSPRITETEXT_TOP);
    auto duplicates = reopened.FindDuplicateNames(path);
    CHECK(!duplicates.empty());
    for (auto &duplicate : duplicates)
        duplicate.action = duplicate.cid == CKCID_SPRITETEXT && duplicate.name == keptName
                               ? DuplicateAction::UseCurrent : DuplicateAction::Rename;
    std::vector<CK_ID> merged;
    CHECK(reopened.Merge(path, false, &error, &merged, &duplicates));
    CHECK(properties::SpriteTextAlignment.get(CKSpriteText::Cast(context->GetObject(keptId))) ==
          (CKSPRITETEXT_RIGHT | CKSPRITETEXT_TOP));
    int importedTexts = 0;
    const auto original = SpriteTextAlignments();
    for (CK_ID id : merged) {
        auto *text = CKSpriteText::Cast(context->GetObject(id));
        if (!text || id == keptId) continue;
        const std::string name = text->GetName() ? text->GetName() : "";
        for (size_t i = 1; i < original.size(); ++i)
            if (name.rfind(AlignmentName(i), 0) == 0) {
                CHECK(properties::SpriteTextAlignment.get(text) == original[i]);
                ++importedTexts;
            }
    }
    CHECK(importedTexts == 9);
    // Old chunks must not resurrect a removed text on a subsequent save.
    const std::string removedName = AlignmentName(9);
    CKObject *removed = context->GetObjectByNameAndClass(const_cast<CKSTRING>(removedName.c_str()), CKCID_SPRITETEXT);
    CHECK(removed && DestroyLevelObjects(reopened, {removed->GetID()}).ok);
    const std::string mergedFile = (std::filesystem::path(outputDirectory) / "merged.cmo").string();
    CHECK(SaveDocumentAs(reopened, mergedFile, options, report));
    CHECK(report.spriteTextsVerified == 18);
    CHECK(reopened.Open(mergedFile, &error));
    CHECK(!context->GetObjectByNameAndClass(const_cast<CKSTRING>(removedName.c_str()), CKCID_SPRITETEXT));
    kept = CKSpriteText::Cast(context->GetObjectByNameAndClass(const_cast<CKSTRING>(keptName.c_str()), CKCID_SPRITETEXT));
    CHECK(kept && properties::SpriteTextAlignment.get(kept) == (CKSPRITETEXT_RIGHT | CKSPRITETEXT_TOP));
    std::cout << "Ten Sprite Text alignments survived a new process, remapped IDs, edit/undo/redo and resave; merge/delete passed\n";
    return 0;
}


// --create-save <file> <outdir>: every creation tool and a scene setting on a
// real composition, saved (DocumentSaver checks), reopened: the objects and
// the setting are there.
int CreateSaveFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory) {
    namespace fs = std::filesystem;
    fs::create_directories(outputDirectory);
    Document document(host);
    UndoHistory &undo = document.History();
    undo.SetLimit(100);
    std::string error;
    if (!document.Open(path, &error)) {
        std::cerr << error << '\n';
        return 2;
    }
    CKContext *context = host.Context();
    const LayoutObject kinds[] = {LayoutObject::Camera, LayoutObject::Light, LayoutObject::Frame3D,
                                  LayoutObject::Curve, LayoutObject::Grid, LayoutObject::Frame2D,
                                  LayoutObject::Material, LayoutObject::Texture, LayoutObject::Portal};
    std::vector<std::pair<std::string, CK_CLASSID>> made;
    for (LayoutObject kind : kinds) {
        EditResult result = CreateLayoutObject(document, kind);
        CKObject *object = context->GetObject(result.created);
        if (!result.ok || !object) {
            std::cerr << "create failed: " << result.message << '\n';
            return 1;
        }
        made.emplace_back(object->GetName(), object->GetClassID());
        std::cout << "created " << CKClassIDToString(object->GetClassID()) << " '" << object->GetName() << "'\n";
    }
    CKScene *levelScene = context->GetCurrentLevel()->GetLevelScene();
    SceneEnvironment environment = ReadSceneEnvironment(levelScene);
    environment.fogMode = 3;
    environment.fogEnd = 123.0f;
    if (!SetSceneEnvironment(document, levelScene->GetID(), environment, "Fog").ok) return 1;
    const std::string saved = (fs::path(outputDirectory) / "created.cmo").string();
    SaveOptions options;
    options.allowOverwrite = true;
    SaveReport report;
    if (!SaveDocumentAs(document, saved, options, report)) {
        for (const auto &message : report.errors) std::cerr << message << '\n';
        return 1;
    }
    if (!document.Open(saved, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    int missing = 0;
    for (const auto &entry : made) {
        CKObject *object = context->GetObjectByNameAndClass(const_cast<CKSTRING>(entry.first.c_str()), entry.second);
        if (!object) {
            std::cerr << "missing after reload: " << entry.first << '\n';
            ++missing;
        }
    }
    CKScene *reloaded = context->GetCurrentLevel()->GetLevelScene();
    if (reloaded->GetFogMode() != VXFOG_LINEAR || reloaded->GetFogEnd() != 123.0f) {
        std::cerr << "scene settings not saved: fog " << reloaded->GetFogMode() << " end " << reloaded->GetFogEnd()
                  << " use " << reloaded->EnvironmentSettings() << '\n';
        ++missing;
    }
    if (missing) return 1;
    std::cout << "Created objects saved and reloaded (" << made.size() << ")\n";
    return 0;
}

// File > New Composition, Merge Composition and Merge Composition as New
// Scene; the merged composition saves and reloads with every Script.
int NewMergeFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory) {
    namespace fs = std::filesystem;
    fs::create_directories(outputDirectory);
    CKContext *context = host.Context();
    Document document(host);
    std::string error;
    if (!document.Open(path, &error)) {
        std::cerr << error << '\n';
        return 2;
    }
    const size_t fileScripts = document.Scripts().size();
    int fileLevelScripts = 0; // Scripts of the file's level
    for (const ScriptInfo &script : document.Scripts())
        if (CKLevel::Cast(context->GetObject(script.ownerId))) ++fileLevelScripts;
    int failures = 0;
    auto check = [&](bool condition, const std::string &what) {
        if (!condition) {
            std::cerr << "new / merge: " << what << '\n';
            ++failures;
        }
    };
    check(document.New(&error), "new: " + error);
    CKLevel *level = context->GetCurrentLevel();
    check(level && level->GetName() && std::string(level->GetName()) == "Level" && document.Scripts().empty() &&
              context->GetObjectsCountByClassID(CKCID_LEVEL) == 1,
          "new composition: one empty level named Level");
    if (failures) return 1;
    CKScene *levelScene = level->GetLevelScene();
    std::cout << "new: ambient " << std::hex << levelScene->GetAmbientLight() << " background "
              << levelScene->GetBackgroundColor() << std::dec << " fog " << levelScene->GetFogMode() << '\n';
    SaveOptions options;
    options.allowOverwrite = true;
    SaveReport report;
    check(SaveDocumentAs(document, (fs::path(outputDirectory) / "new.cmo").string(), options, report),
          "an empty composition saves");
    for (const auto &message : report.errors) std::cerr << "  " << message << '\n';
    // Merge Composition: the file's level Scripts and objects join the current level.
    std::vector<CK_ID> merged;
    check(document.Merge(path, false, &error, &merged), "merge: " + error);
    std::cout << "merge: " << merged.size() << " objects, " << document.Scripts().size() << " Scripts, level Scripts "
              << level->GetScriptCount() << '\n';
    check(context->GetCurrentLevel() == level && context->GetObjectsCountByClassID(CKCID_LEVEL) == 1,
          "merge keeps one level, the current one");
    check(document.Scripts().size() == fileScripts && level->GetScriptCount() == fileLevelScripts,
          "merge brings the file's Scripts, its level Scripts to the level");
    check(document.Dirty(), "merge marks the composition modified");
    int inLevelScene = 0;
    for (CK_ID id : merged)
        if (CKSceneObject *object = CKSceneObject::Cast(context->GetObject(id)))
            if (levelScene->IsObjectHere(object)) ++inLevelScene;
    check(inLevelScene > 0, "merged objects join the level scene");
    // Merge Composition as New Scene: a scene "Merged Scene" holding the
    // file's level Scripts and objects.
    const int scenesBefore = level->GetSceneCount();
    check(document.Merge(path, true, &error), "merge as new scene: " + error);
    CKScene *mergedScene = level->GetSceneCount() > scenesBefore ? level->GetScene(level->GetSceneCount() - 1) : nullptr;
    std::cout << "merge as scene: " << document.Scripts().size() << " Scripts, scene '"
              << (mergedScene && mergedScene->GetName() ? mergedScene->GetName() : "") << "' objects "
              << (mergedScene ? mergedScene->GetObjectCount() : 0) << " Scripts "
              << (mergedScene ? mergedScene->GetScriptCount() : 0) << ", level scene Scripts "
              << levelScene->GetScriptCount() << '\n';
    check(mergedScene && mergedScene->GetName() && std::string(mergedScene->GetName()) == "Merged Scene",
          "merge as new scene adds the scene Merged Scene");
    check(document.Scripts().size() == 2 * fileScripts, "merge as new scene brings the Scripts again");
    check(mergedScene && mergedScene->GetScriptCount() == fileLevelScripts && levelScene->GetScriptCount() == 0,
          "the file's level Scripts belong to the new scene");
    const std::string saved = (fs::path(outputDirectory) / "merged.cmo").string();
    if (!SaveDocumentAs(document, saved, options, report)) {
        for (const auto &message : report.errors) std::cerr << message << '\n';
        return 1;
    }
    const int savedScenes = level->GetSceneCount();
    check(document.Open(saved, &error), "reopen: " + error);
    std::cout << "reopened: " << document.Scripts().size() << " Scripts, scenes "
              << context->GetCurrentLevel()->GetSceneCount() << '\n';
    check(document.Scripts().size() == 2 * fileScripts, "the merged Scripts are saved");
    check(context->GetCurrentLevel()->GetSceneCount() == savedScenes, "the merged scenes are saved");
    if (failures) return 1;
    std::cout << "New / merge passed\n";
    return 0;
}

// Resources, Editors and Options tools: Data Resources (created in the build
// tree, the runtime's own read), Import File (As), resource files, Unused
// Object Explorer, Building Block Version Test, Installed Plugins; the
// composition with the imports saves and reloads.
int ToolsFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove_all(fs::path(outputDirectory) / "Created", ec);
    fs::create_directories(outputDirectory);
    CKContext *context = host.Context();
    int failures = 0;
    auto check = [&](bool condition, const std::string &what) {
        if (!condition) {
            std::cerr << "tools: " << what << '\n';
            ++failures;
        }
    };
    std::string error;
    // Create New Data Resource: the .rsc, its folder and Dev's marked folders.
    const std::string created = (fs::path(outputDirectory) / "Created.rsc").string();
    check(CreateDataResource(created, &error), "create data resource: " + error);
    DataResource resource;
    check(ReadDataResource(created, resource, &error), "read the created resource: " + error);
    std::map<std::string, CK_CLASSID> top;
    for (const auto &folder : resource.root.folders) top[folder.name] = folder.cid;
    check(top.size() == 7 && top["2D Sprites"] == CKCID_SPRITE && top["Behavior Graphs"] == CKCID_BEHAVIOR &&
              top["Characters"] == CKCID_CHARACTER && top["3D Entities"] == CKCID_3DENTITY &&
              top["3D Sprites"] == CKCID_SPRITE3D && top["Textures"] == CKCID_TEXTURE && top["Sounds"] == CKCID_SOUND,
          "Dev's folders with their markers");
    check(DataResourceFolderClass((fs::path(outputDirectory) / "Created" / "Characters" / "Animations").string()) ==
              CKCID_ANIMATION,
          "Characters\\Animations marked as animations");
    check(!ReadDataResource(path, resource, &error), "a composition is not a Data Resource");
    // Behavior Information: the .dsc beside a behavior file, not listed.
    {
        const fs::path graphs = fs::path(outputDirectory) / "Created" / "Behavior Graphs";
        const std::string nms = (graphs / "Described.nms").string();
        { std::ofstream(nms, std::ios::binary) << "x"; }
        BehaviorDescription written;
        written.cid = CKCID_3DENTITY;
        written.version = 1.5f;
        written.name = "Described";
        written.author = "Someone";
        written.description = "First line\nSecond line";
        check(WriteBehaviorDescription(nms, written), "write a .dsc");
        check(fs::exists(graphs / "Described.dsc"), "the .dsc beside the .nms");
        BehaviorDescription read;
        check(ReadBehaviorDescription(nms, read) && read.cid == CKCID_3DENTITY && read.version == 1.5f &&
                  read.name == "Described" && read.author == "Someone" && read.description == written.description,
              "the .dsc read back");
        BehaviorDescription none;
        check(!ReadBehaviorDescription((graphs / "None.nms").string(), none), "no .dsc");
        DataResource listed;
        check(ReadDataResource(created, listed, &error), "read the resource again");
        for (const auto &folder : listed.root.folders)
            if (folder.name == "Behavior Graphs")
                check(folder.files.size() == 1 && folder.files.front() == "Described.nms", "the .dsc is not listed");
    }

    if (!fs::exists(path)) return 2;
    Document document(host);
    if (!document.Open(path, &error)) {
        std::cerr << error << '\n';
        return 2;
    }
    CKLevel *level = context->GetCurrentLevel();
    UndoHistory &undo = document.History();
    undo.SetLimit(1000); // deep enough to count the steps
    // The runtime's Data Resource: its folders join the path manager.
    const fs::path runtimeResource = fs::path(RuntimeDirectory()) / "Documentation" / "VirtoolsResources.rsc";
    const fs::path root = fs::path(RuntimeDirectory()) / "Documentation" / "VirtoolsResources";
    if (fs::exists(runtimeResource)) {
        DataResource dev;
        check(ReadDataResource(runtimeResource.string(), dev, &error), "read VirtoolsResources.rsc: " + error);
        const int added = AddDataResourcePaths(context, dev);
        CKPathManager *paths = context->GetPathManager();
        XString textures(const_cast<CKSTRING>((root / "Textures").string().c_str()));
        std::cout << "data resource: " << dev.root.folders.size() << " folders, " << added << " paths added\n";
        check(added > 0 && paths->GetPathIndex(0, textures) >= 0, "texture folders join the bitmap paths");
        check(AddDataResourcePaths(context, dev) == 0, "a path is added once");
        // Import File (As).
        const std::string cube = (root / "3D Entities" / "Primitives" / "Cube.nmo").string();
        std::vector<CK_ID> imported;
        const int levels = context->GetObjectsCountByClassID(CKCID_LEVEL);
        check(document.Import(cube, Document::ImportMode::File, &error, &imported), "import: " + error);
        int entities = 0;
        for (CK_ID id : imported)
            if (CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(id)))
                if (level->GetLevelScene()->IsObjectHere(entity)) ++entities;
        std::cout << "import: " << imported.size() << " objects, " << entities << " 3D entities in the level scene\n";
        check(entities > 0, "imported entities join the level");
        check(context->GetObjectsCountByClassID(CKCID_LEVEL) == levels,
              "an import adds no level (" + std::to_string(levels) + " before, " +
                  std::to_string(context->GetObjectsCountByClassID(CKCID_LEVEL)) + " after)");
        imported.clear();
        check(document.Import(cube, Document::ImportMode::Place, &error, &imported), "import as place: " + error);
        CKPlace *place = nullptr;
        for (CK_ID id : imported)
            if (CKPlace *candidate = CKPlace::Cast(context->GetObject(id))) place = candidate;
        check(place && place->GetName() && std::string(place->GetName()) == "Cube" && place->GetChildrenCount() > 0 &&
                  level->GetPlaceCount() > 0,
              "import as place: a place Cube parent of the entities");
        imported.clear();
        const int scenes = level->GetSceneCount();
        check(document.Import(cube, Document::ImportMode::Scene, &error, &imported), "import as scene: " + error);
        CKScene *scene = level->GetSceneCount() > scenes ? level->GetScene(level->GetSceneCount() - 1) : nullptr;
        check(scene && scene->GetName() && std::string(scene->GetName()) == "Cube" && scene->GetObjectCount() > 0,
              "import as scene: a scene Cube holding the objects");
        const std::string jane = (root / "Characters" / "Skin Characters" / "Jane.nmo").string();
        if (fs::exists(jane)) {
            const int characters = context->GetObjectsCountByClassID(CKCID_CHARACTER);
            check(document.Import(jane, Document::ImportMode::Character, &error), "import as character: " + error);
            check(context->GetObjectsCountByClassID(CKCID_CHARACTER) > characters, "import as character makes a character");
        }
        // Resource files by folder class.
        std::vector<CK_ID> loaded;
        EditResult result = LoadResourceFile(document, (root / "Textures" / "Misc" / "Blue.bmp").string(),
                                             CKCID_TEXTURE, 0, &loaded);
        CKTexture *texture = CKTexture::Cast(context->GetObject(result.created));
        check(result.ok && texture && std::string(texture->GetName()) == "Blue" && texture->GetWidth() > 0,
              "texture file: " + result.message);
        result = LoadResourceFile(document, (root / "Textures" / "Misc" / "Blue.bmp").string(), CKCID_SPRITE3D, 0,
                                  &loaded);
        check(result.ok && context->GetObjectsCountByClassID(CKCID_SPRITE3D) > 0, "3D sprite file: " + result.message);
        // Shift+Ctrl on the 3D Layout (Dev.exe sub_4BF320): a 2D Entity of the
        // texture's size with a material showing it.
        check(!ResourceFileIsImported(CKCID_2DENTITY), "an image as a 2D Entity is not imported");
        result = LoadResourceFile(document, (root / "Textures" / "Misc" / "Blue.bmp").string(), CKCID_2DENTITY, 0,
                                  &loaded);
        CK2dEntity *entity2d = nullptr;
        for (CK_ID id : loaded)
            if (CK2dEntity *candidate = CK2dEntity::Cast(context->GetObject(id)))
                if (!CKSprite::Cast(candidate)) entity2d = candidate;
        CKMaterial *entityMaterial = entity2d ? entity2d->GetMaterial() : nullptr;
        CKTexture *entityTexture = entityMaterial ? entityMaterial->GetTexture() : nullptr;
        Vx2DVector entitySize;
        if (entity2d) entity2d->GetSize(entitySize);
        check(result.ok && entity2d && std::string(entity2d->GetName()) == "Blue" && entityTexture &&
                  entitySize.x == static_cast<float>(entityTexture->GetWidth()) &&
                  entitySize.y == static_cast<float>(entityTexture->GetHeight()) && !entityMaterial->ZWriteEnabled() &&
                  entityMaterial->GetZFunc() == VXCMP_ALWAYS && entityMaterial->GetDiffuse().r == 1.0f &&
                  entityMaterial->GetSpecular().r == 0.0f,
              "2D entity file: " + result.message);
        result = LoadResourceFile(document, (root / "Sounds" / "SFX" / "Birds.wav").string(), CKCID_SOUND, 0, &loaded);
        check(result.ok && CKWaveSound::Cast(context->GetObject(result.created)), "sound file: " + result.message);
        result = LoadResourceFile(document, (root / "Textures" / "Misc" / "Blue.bmp").string(), CKCID_ANIMATION, 0);
        check(!result.ok, "an animation needs a character");
        for (CK_ID id : loaded)
            if (CKSceneObject *object = CKSceneObject::Cast(context->GetObject(id)))
                check(level->GetLevelScene()->IsObjectHere(object), "loaded files join the level");
        // Files dropped onto Level Manager rows (Dev.exe sub_56B380, sub_56E210).
        const std::string blue = (root / "Textures" / "Misc" / "Blue.bmp").string();
        LevelDropTarget inScene;
        inScene.scene = scene ? scene->GetID() : 0;
        result = DropResourceFile(document, blue, CKCID_TEXTURE, inScene);
        CKTexture *dropped = CKTexture::Cast(context->GetObject(result.created));
        check(result.ok && dropped && scene && scene->IsObjectHere(dropped) && level->GetLevelScene()->IsObjectHere(dropped),
              "a texture dropped in a scene's section joins the level and that scene: " + result.message);
        check(CreateGroup(document, "Drop Group", {}).ok, "a group to drop onto");
        CKGroup *group = CKGroup::Cast(context->GetObjectByNameAndClass((CKSTRING) "Drop Group", CKCID_GROUP));
        LevelDropTarget onGroup;
        onGroup.row = group ? group->GetID() : 0;
        const int members = group ? group->GetObjectCount() : 0;
        result = DropResourceFile(document, cube, CKCID_3DENTITY, onGroup);
        check(result.ok && group && group->GetObjectCount() > members, "a file dropped onto a group joins it: " + result.message);
        LevelDropTarget onPlace;
        onPlace.row = place ? place->GetID() : 0;
        const int children = place ? place->GetChildrenCount() : 0;
        result = DropResourceFile(document, cube, CKCID_3DENTITY, onPlace);
        check(result.ok && place && place->GetChildrenCount() > children,
              "a file dropped onto a place gives it children: " + result.message);
        LevelDropTarget onScene;
        onScene.row = scene ? scene->GetID() : 0;
        const int inside = scene ? scene->GetObjectCount() : 0;
        result = DropResourceFile(document, cube, CKCID_3DENTITY, onScene);
        check(result.ok && scene && scene->GetObjectCount() > inside, "a file dropped onto a scene joins it: " + result.message);
        // Behavior files: a Script, then a Building Block, onto objects
        // without Scripts and onto a Script.
        CKBehavior *source = nullptr, *block = nullptr;
        for (const ScriptInfo &info : document.Scripts()) {
            CKBehavior *script = CKBehavior::Cast(context->GetObject(info.id));
            for (int i = 0; script && !block && i < script->GetSubBehaviorCount(); ++i)
                if (script->GetSubBehavior(i)->GetType() == CKBEHAVIORTYPE_BASE) {
                    source = script;
                    block = script->GetSubBehavior(i);
                }
        }
        const std::string scriptFile = (fs::path(outputDirectory) / "drop-script.nms").string();
        const std::string blockFile = (fs::path(outputDirectory) / "drop-block.nms").string();
        check(source && SaveBehaviorsAsNms(document, {source->GetID()}, scriptFile, &error), "save a Script: " + error);
        // Hide script representation: the file's InterfaceChunk flagged hidden, the composition's not.
        if (source) {
            const std::string hiddenFile = (fs::path(outputDirectory) / "hidden-script.nms").string();
            check(SaveBehaviorsAsNms(document, {source->GetID()}, hiddenFile, &error, true), "save a Script hidden: " + error);
            InterfaceData own;
            check(!source->GetInterfaceChunk() || own.LoadFromChunk(source, source->GetInterfaceChunk()) != CK_OK ||
                      !(own.rootBehavior.flags & 0x20000),
                  "the composition's chunk is not flagged hidden");
            CKObjectArray *read = CreateCKObjectArray();
            check(context->Load(const_cast<CKSTRING>(hiddenFile.c_str()), read, CK_LOAD_DEFAULT) == CK_OK, "load the hidden file");
            bool hidden = false;
            XObjectArray loadedObjects;
            for (read->Reset(); !read->EndOfList(); read->Next()) {
                loadedObjects.PushBack(read->GetDataId());
                CKBehavior *behavior = CKBehavior::Cast(read->GetData(context));
                InterfaceData data;
                if (behavior && (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) && behavior->GetInterfaceChunk() &&
                    data.LoadFromChunk(behavior, behavior->GetInterfaceChunk()) == CK_OK)
                    hidden = hidden || (data.rootBehavior.flags & 0x20000) != 0;
            }
            DeleteCKObjectArray(read);
            context->DestroyObjects(loadedObjects.Begin(), loadedObjects.Size());
            check(hidden, "the file's Script is flagged hidden");
            // The Level Manager's Save as... of the Script's owner (.nmo),
            // Hide script representation checked: the same in the file.
            if (CKBeObject *owner = source->GetOwner()) {
                const std::string hiddenObjects = (fs::path(outputDirectory) / "hidden-owner.nmo").string();
                check(SaveObjectsAs(document, {owner->GetID()}, hiddenObjects, &error, true), "save an owner hidden: " + error);
                InterfaceData after;
                check(!source->GetInterfaceChunk() || after.LoadFromChunk(source, source->GetInterfaceChunk()) != CK_OK ||
                          !(after.rootBehavior.flags & 0x20000),
                      "the composition's chunk stays unflagged after Save as");
                CKObjectArray *again = CreateCKObjectArray();
                check(context->Load(const_cast<CKSTRING>(hiddenObjects.c_str()), again, CK_LOAD_DEFAULT) == CK_OK,
                      "load the hidden objects");
                bool flagged = false;
                XObjectArray loadedAgain;
                for (again->Reset(); !again->EndOfList(); again->Next()) {
                    loadedAgain.PushBack(again->GetDataId());
                    CKBehavior *behavior = CKBehavior::Cast(again->GetData(context));
                    InterfaceData data;
                    if (behavior && (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) && behavior->GetInterfaceChunk() &&
                        data.LoadFromChunk(behavior, behavior->GetInterfaceChunk()) == CK_OK)
                        flagged = flagged || (data.rootBehavior.flags & 0x20000) != 0;
                }
                DeleteCKObjectArray(again);
                context->DestroyObjects(loadedAgain.Begin(), loadedAgain.Size());
                check(flagged, "the .nmo's Script is flagged hidden");
            }
        }
        check(block && SaveBehaviorsAsNms(document, {block->GetID()}, blockFile, &error), "save a Building Block: " + error);
        std::vector<CK3dEntity *> bare;
        for (CK_ID id : imported)
            if (CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(id)))
                if (!CKPlace::Cast(entity) && entity->GetScriptCount() == 0) bare.push_back(entity);
        check(bare.size() >= 1, "objects without Scripts");
        if (bare.size() >= 1 && source && block) {
            LevelDropTarget onObject;
            onObject.row = bare[0]->GetID();
            result = DropResourceFile(document, scriptFile, CKCID_BEHAVIOR, onObject);
            check(result.ok && bare[0]->GetScriptCount() == 1, "a Script file dropped onto an object: " + result.message);
            check(undo.Undo() && bare[0]->GetScriptCount() == 0, "undo the Script dropped");
            result = DropResourceFile(document, blockFile, CKCID_BEHAVIOR, onObject);
            CKBehavior *made = bare[0]->GetScriptCount() == 1 ? bare[0]->GetScript(0) : nullptr;
            check(result.ok && made && made->GetSubBehaviorCount() == 1,
                  "a Building Block file dropped onto an object: a new Script holding it: " + result.message);
            const size_t steps = undo.UndoCount(); // the history is deep enough to count
            LevelDropTarget onScript;
            onScript.row = made ? made->GetID() : 0;
            result = DropResourceFile(document, blockFile, CKCID_BEHAVIOR, onScript);
            check(result.ok && made && made->GetSubBehaviorCount() == 2 && (undo.UndoCount() - steps) == 1,
                  "a Building Block file dropped onto a Script goes into it: " + result.message);
            check(undo.Undo() && made->GetSubBehaviorCount() == 1, "undo the Building Block dropped");
            check(undo.Undo() && bare[0]->GetScriptCount() == 0, "the new Script and its block: one undo step");
        }
        LevelDropTarget onFolder;
        check(!DropResourceFile(document, blockFile, CKCID_BEHAVIOR, onFolder).ok, "behaviors are not dropped on folders");
    }
    // Unused Object Explorer: a new material nothing uses is listed.
    CKMaterial *unused = CKMaterial::Cast(context->CreateObject(CKCID_MATERIAL, (CKSTRING) "Unused Material"));
    level->AddObject(unused);
    document.AdoptObjects({unused->GetID()});
    bool listed = false;
    std::string walk;
    for (const UnusedObject &object : UnusedObjects(document, &walk)) listed = listed || object.id == unused->GetID();
    check(walk.empty() && listed, "the unused material is listed: " + walk);
    check(DestroyLevelObjects(document, {unused->GetID()}).ok && !context->GetObject(unused->GetID()),
          "Delete destroys it");
    // Building Block Version Test compares the file with the loaded prototypes.
    const VersionTestResult test = TestBuildingBlockVersions(document);
    std::cout << "version test: " << test.report;
    const bool noError = test.report.find("No error found") != std::string::npos;
    check(test.changed.empty() == noError, "the differences listed to replace");
    // Installed Plugins in Dev's order.
    const std::vector<PluginCategory> plugins = InstalledPlugins(context);
    check(plugins.size() >= 8 && plugins[0].name == "Bitmap Readers" && plugins[1].name == "Movie Readers",
          "plugin categories in Dev's order");
    int managers = 0;
    for (const auto &category : plugins)
        for (const auto &plugin : category.plugins) managers += plugin.managerIndex >= 0;
    check(managers > 0, "managers can be switched");
    // The composition with the imports saves and reloads.
    SaveOptions options;
    options.allowOverwrite = true;
    SaveReport report;
    const std::string saved = (fs::path(outputDirectory) / "tools.cmo").string();
    check(SaveDocumentAs(document, saved, options, report), "save");
    for (const auto &message : report.errors) std::cerr << "  " << message << '\n';
    const int places = level->GetPlaceCount(), scenes = level->GetSceneCount();
    check(document.Open(saved, &error), "reopen: " + error);
    check(context->GetCurrentLevel()->GetPlaceCount() == places && context->GetCurrentLevel()->GetSceneCount() == scenes,
          "imported places and scenes are saved");
    if (failures) return 1;
    std::cout << "Tools passed\n";
    return 0;
}


// --layout <file> <outdir>: the 3D Layout view (a render context on a hidden
// window): Dev's views, the frame copied back, the reference grid and world
// axis drawn, the composition's cameras listed, the interface objects
// neither saved nor kept by ClearAll, the render context kept.
// Dev's link validation after loading (LinkValidation): the invalid links of
// each file, endpoints and behavior links, then with the parameter types.
// Saved compositions have none; with --remove, removing them leaves none.
// Dev's Duplicate Names Found: a file merged into itself lists its named
// objects as duplicates; Ignore adds another object of the name, Rename one
// with a number added, Use Current keeps the existing object and its data.
int DuplicatesFile(CkRuntimeHost &host, const std::string &path) {
    CKContext *context = host.Context();
    Document document(host);
    std::string error;
    if (!document.Open(path, &error)) {
        std::cerr << error << '\n';
        return 2;
    }
    int failures = 0;
    auto check = [&](bool condition, const std::string &what) {
        if (!condition) {
            std::cerr << "duplicate names: " << what << '\n';
            ++failures;
        }
    };
    auto count = [&](CK_CLASSID cid, const std::string &name, bool prefix) {
        int found = 0;
        const XObjectPointerArray &list = context->GetObjectListByType(cid, FALSE);
        for (int i = 0; i < list.Size(); ++i) {
            const std::string other = list[i] && list[i]->GetName() ? list[i]->GetName() : "";
            if (prefix ? other != name && other.rfind(name, 0) == 0 : other == name) ++found;
        }
        return found;
    };
    std::vector<DuplicateName> duplicates = document.FindDuplicateNames(path);
    check(!duplicates.empty(), "the file's objects are duplicates of themselves");
    check(document.FindDuplicateNames(path).size() == duplicates.size(), "listed again the same");
    check(document.FindDuplicateNames(path + ".bmp").empty(), "other formats have none");
    // One object of each choice: named behavioral objects alone of their
    // name and class in the file and in the composition.
    const DuplicateAction actions[] = {DuplicateAction::Ignore, DuplicateAction::Rename, DuplicateAction::UseCurrent};
    struct Chosen {
        DuplicateName duplicate;
        CK_ID existing = 0;
    };
    std::vector<Chosen> chosen;
    const size_t choices = std::size(actions);
    for (DuplicateName &duplicate : duplicates) {
        if (chosen.size() == choices) break;
        if (!CKIsChildClassOf(duplicate.cid, CKCID_BEOBJECT) || CKIsChildClassOf(duplicate.cid, CKCID_LEVEL) ||
            CKIsChildClassOf(duplicate.cid, CKCID_SCENE) || duplicate.name.empty())
            continue;
        int same = 0;
        for (const DuplicateName &other : duplicates) same += other.cid == duplicate.cid && other.name == duplicate.name;
        if (same != 1 || count(duplicate.cid, duplicate.name, false) != 1 || count(duplicate.cid, duplicate.name, true) != 0)
            continue;
        duplicate.action = actions[chosen.size()];
        CKObject *existing = context->GetObjectByNameAndClass(const_cast<CKSTRING>(duplicate.name.c_str()), duplicate.cid);
        chosen.push_back({duplicate, existing ? existing->GetID() : 0});
    }
    check(chosen.size() == choices, "an object to choose for each choice");
    for (const Chosen &c : chosen)
        std::cout << "  " << CKClassIDToString(c.duplicate.cid) << " '" << c.duplicate.name << "': "
                  << static_cast<int>(c.duplicate.action) << '\n';
    if (failures) return 1;
    // Use Current must retain content and relationships absent from the file.
    const EditResult keptScript = CreateScript(document, chosen[2].existing, "Duplicate test script");
    CHECK(keptScript.ok);
    const EditResult keptValue = AddLocalParameter(document, keptScript.created, keptScript.created,
                                                   "Duplicate test value", CKPGUID_INT, Point(200, 60));
    CHECK(keptValue.ok && SetParameterText(document, keptScript.created, keptValue.created, "137").ok);
    ScriptSession *session = document.Session(keptScript.created);
    auto checkKept = [&](int expected) {
        CKBehavior *script = CKBehavior::Cast(context->GetObject(keptScript.created));
        CKParameterLocal *parameter = CKParameterLocal::Cast(context->GetObject(keptValue.created));
        CHECK(script && script->GetOwner() && script->GetOwner()->GetID() == chosen[2].existing);
        CHECK(parameter && parameter->GetOwner() == script && script->GetLocalParameterPosition(parameter) >= 0);
        int value = 0;
        CHECK(parameter->GetValue(&value, FALSE) == CK_OK && value == expected);
        CHECK(document.Session(keptScript.created) == session);
    };
    UndoHistory &undo = document.History();
    bool applied = false;
    RecordValue(document, "Before", applied, true);
    std::vector<CK_ID> merged;
    check(document.Merge(path, false, &error, &merged, &duplicates), "merge: " + error);
    check(undo.CanUndo(), "no object replaced: the undo history is kept");
    checkKept(137);
    CHECK(undo.Undo() && !applied);
    CHECK(undo.Redo() && applied);
    for (const Chosen &c : chosen) {
        const DuplicateName &d = c.duplicate;
        const int exact = count(d.cid, d.name, false), numbered = count(d.cid, d.name, true);
        switch (d.action) {
        case DuplicateAction::Ignore: check(exact == 2 && numbered == 0, "Ignore adds one of the same name: " + d.name); break;
        case DuplicateAction::Rename: check(exact == 1 && numbered == 1, "Rename adds one numbered: " + d.name); break;
        case DuplicateAction::UseCurrent:
            check(exact == 1 && numbered == 0 && context->GetObject(c.existing),
                  "Use Current keeps the existing object only: " + d.name);
            break;
        }
    }
    CHECK(SetParameterText(document, keptScript.created, keptValue.created, "271").ok);
    CHECK(undo.Undo());
    checkKept(137);
    CHECK(undo.Redo());
    checkKept(271);
    const CompositionState beforeCancel = CaptureComposition(document, {keptValue.created});
    const size_t historyCount = undo.UndoCount();
    {
        Edit edit(document, "Cancelled duplicate value");
        CHECK(SetParameterText(document, keptScript.created, keptValue.created, "999").ok);
        checkKept(999);
        CHECK(edit.Cancel());
    }
    ExpectSameComposition(beforeCancel, CaptureComposition(document, {keptValue.created}), "cancel after merge");
    CHECK(undo.UndoCount() == historyCount);
    std::vector<CK_ID> imported;
    CHECK(document.Import(path, Document::ImportMode::File, &error, &imported, &duplicates));
    CHECK(!imported.empty() && undo.UndoCount() == historyCount);
    checkKept(271);
    for (const Chosen &c : chosen) {
        const DuplicateName &d = c.duplicate;
        check(count(d.cid, d.name, false) == (d.action == DuplicateAction::Ignore ? 3 : 1),
              "Import applies the duplicate choice: " + d.name);
        check(count(d.cid, d.name, true) == (d.action == DuplicateAction::Rename ? 2 : 0),
              "Import preserves numbering: " + d.name);
    }
    // Without the choices CK keeps them all (Ignore).
    const DuplicateName &first = chosen.front().duplicate;
    const int before = count(first.cid, first.name, false);
    check(document.Merge(path, false, &error), "merge without choices: " + error);
    check(count(first.cid, first.name, false) == before + 1, "without choices the duplicates are loaded");
    checkKept(271);
    const CompositionState beforeFailure = CaptureComposition(document, {keptValue.created});
    CHECK(!document.Merge(path + ".missing", false, &error, nullptr, &duplicates));
    CHECK(!document.Import(path + ".missing.cmo", Document::ImportMode::File, &error, nullptr, &duplicates));
    ExpectSameComposition(beforeFailure, CaptureComposition(document, {keptValue.created}), "failed duplicate load");
    CHECK(undo.UndoCount() == historyCount);
    namespace fs = std::filesystem;
    const fs::path root = fs::current_path() / ("duplicates-" + std::to_string(SDL_GetPerformanceCounter()));
    CHECK(root.parent_path() == fs::current_path() && fs::create_directory(root));
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code error; fs::remove_all(root, error); } } cleanup{root};
    SaveOptions options;
    SaveReport report;
    CHECK(SaveDocumentAs(document, (root / "roundtrip.cmo").u8string(), options, report));
    document.Close();
    CHECK(document.Open((root / "roundtrip.cmo").u8string(), &error) && !document.Dirty());
    CKBehavior *script = CKBehavior::Cast(context->GetObjectByNameAndClass((CKSTRING) "Duplicate test script", CKCID_BEHAVIOR));
    CHECK(script && script->GetOwner() && script->GetOwner()->GetClassID() == chosen[2].duplicate.cid);
    CHECK(chosen[2].duplicate.name == script->GetOwner()->GetName());
    CHECK(script->GetLocalParameterCount() == 1);
    CKParameterLocal *parameter = script->GetLocalParameter(0);
    int value = 0;
    CHECK(parameter && std::string(parameter->GetName()) == "Duplicate test value");
    CHECK(parameter->GetOwner() == script && parameter->GetValue(&value, FALSE) == CK_OK && value == 271);
    document.Close();
    if (failures) return 1;
    std::cout << "Duplicate names: " << duplicates.size() << " found, three choices, values, history and round trip checked\n";
    return 0;
}

// The 3D Layout's interface data (Dev.exe sub_4B4E50 / sub_4B46C0): read
// from a file, the selection group, the selection and the lock changed,
// saved and read back with the rest kept; a new composition gets the object.
int LayoutInterfaceFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory) {
    namespace fs = std::filesystem;
    fs::create_directories(outputDirectory);
    CKContext *context = host.Context();
    host.ClearAll(); // main's level: the file alone, as the editor opens it
    Document document(host);
    std::string error;
    if (!document.Open(path, &error)) {
        std::cerr << error << '\n';
        return 2;
    }
    int failures = 0;
    auto check = [&](bool condition, const std::string &what) {
        if (!condition) {
            std::cerr << "layout interface: " << what << '\n';
            ++failures;
        }
    };
    auto describe = [&](const char *what, const LayoutInterfaceData &data) {
        std::cout << what << ": present " << data.present << " group " << data.group << " selection "
                  << data.selection.size() << " locked " << data.locked << " lists";
        for (const auto &list : data.lists) std::cout << ' ' << list.size();
        std::cout << " flagged " << data.flagged.size() << " cameras " << data.cameras.size() << " setups "
                  << data.setups.size() << " last " << data.last.size() << '\n';
    };
    const LayoutInterfaceData read = ReadLayoutInterfaceData(context);
    describe("file", read);
    // +180 in Dev's file: curves only (those that entered a scene while Dev
    // edited the file, not necessarily all of them).
    {
        bool curves = true;
        for (CK_ID id : read.lists[LAYOUT_DRAWN_CURVES]) curves = curves && CKCurve::Cast(context->GetObject(id));
        check(curves, "Dev's +180 lists curves only");
    }
    // Dev's Setup GUIDs (sub_5CB9E0) by the object's class.
    auto setupGuid = [&](CK_CLASSID cid) {
        const XObjectPointerArray &objects = context->GetObjectListByType(cid, TRUE);
        CKGUID guid;
        return objects.Size() > 0 && LayoutSetupGuid(objects[0], guid) ? guid : CKGUID();
    };
    check(setupGuid(CKCID_CAMERA) == CKGUID(0x023E7DEE, 0x46053B01) && setupGuid(CKCID_LIGHT) == CKGUID(0x023E7DEE, 0x46053B02) &&
              setupGuid(CKCID_CURVE) == CKGUID(0x023E7DEE, 0x46053B03) && setupGuid(CKCID_MATERIAL) == CKGUID(0x023E7DEE, 0x46053B08) &&
              setupGuid(CKCID_LEVEL) == CKGUID(0x023E7DEE, 0x46053B0E),
          "the Setups' GUIDs");
    auto save = [&](const std::string &name) {
        const std::string saved = (fs::path(outputDirectory) / name).string();
        SaveOptions options;
        options.allowOverwrite = true;
        SaveReport report;
        check(SaveDocumentAs(document, saved, options, report), "save " + name);
        for (const auto &message : report.errors) std::cerr << "  " << message << '\n';
        check(document.Open(saved, &error), "reopen " + name + ": " + error);
    };
    // A group of two 3D entities (an ordinary group, saved), selected and locked.
    std::vector<CK_ID> entities;
    const XObjectPointerArray &list = context->GetObjectListByType(CKCID_3DENTITY, TRUE);
    for (int i = 0; i < list.Size() && entities.size() < 2; ++i)
        if (list[i] && !list[i]->IsPrivate()) entities.push_back(list[i]->GetID());
    check(entities.size() == 2, "two 3D entities");
    UndoHistory &undo = document.History();
    check(CreateGroup(document, "Layout Interface Group", entities).ok, "a group");
    CKGroup *group = CKGroup::Cast(context->GetObjectByNameAndClass((CKSTRING) "Layout Interface Group", CKCID_GROUP));
    LayoutInterfaceData data = read;
    data.group = group ? group->GetID() : 0;
    data.locked = true;
    WriteLayoutInterfaceData(document, data);
    save("layout-interface-group.cmo");
    LayoutInterfaceData back = ReadLayoutInterfaceData(context);
    describe("group", back);
    CKObject *groupBack = context->GetObject(back.group);
    check(back.present && groupBack && groupBack->GetName() && std::string(groupBack->GetName()) == "Layout Interface Group" &&
              back.locked && back.selection.empty(),
          "the group and the lock come back, no selection with a group");
    for (int i = 0; i < 6; ++i) check(back.lists[i].size() == read.lists[i].size(), "list " + std::to_string(i) + " kept");
    check(back.flagged.size() == read.flagged.size() && back.flags == read.flags, "the flags kept");
    check(back.setups.size() == read.setups.size() && back.last.size() == read.last.size(), "the Setups and the last list kept");
    // Without a group: the selection.
    std::vector<std::string> names;
    data = back;
    data.group = 0;
    data.locked = false;
    data.selection.clear();
    const XObjectPointerArray &again = context->GetObjectListByType(CKCID_3DENTITY, TRUE);
    for (int i = 0; i < again.Size() && data.selection.size() < 3; ++i)
        if (again[i] && !again[i]->IsPrivate() && again[i]->GetName()) {
            data.selection.push_back(again[i]->GetID());
            names.push_back(again[i]->GetName());
        }
    WriteLayoutInterfaceData(document, data);
    save("layout-interface-selection.cmo");
    back = ReadLayoutInterfaceData(context);
    describe("selection", back);
    bool same = back.selection.size() == names.size() && !back.group && !back.locked;
    for (size_t i = 0; same && i < names.size(); ++i) {
        CKObject *object = context->GetObject(back.selection[i]);
        same = object && object->GetName() && names[i] == object->GetName();
    }
    check(same, "the selection comes back without a group");
    // A new composition has no such object; one is made.
    check(document.New(&error), "new: " + error);
    check(!ReadLayoutInterfaceData(context).present, "a new composition has no 3D Layout data");
    LayoutInterfaceData fresh;
    fresh.locked = true;
    WriteLayoutInterfaceData(document, fresh);
    save("layout-interface-new.cmo");
    back = ReadLayoutInterfaceData(context);
    describe("new", back);
    check(back.present && back.locked && !back.group, "a new composition's data comes back");
    // The 3D Layout's lists (core Read / WriteLayoutLists): each object in its
    // class's list, every frame saved in the frames' table, read back by name.
    {
        auto make = [&](CK_CLASSID cid, const char *name, bool frame = false) -> CK_ID {
            CKObject *object = context->CreateObject(cid, (CKSTRING) name);
            if (!object) return 0;
            document.Include(object->GetID());
            if (CKSceneObject *member = CKSceneObject::Cast(object)) context->GetCurrentLevel()->AddObject(member);
            if (CK3dEntity *entity = CK3dEntity::Cast(object); entity && frame)
                entity->SetFlags(entity->GetFlags() | CK_3DENTITY_FRAME);
            return object->GetID();
        };
        const CK_ID camera = make(CKCID_CAMERA, "Lists Camera"), light = make(CKCID_LIGHT, "Lists Light"),
                    curve = make(CKCID_CURVE, "Lists Curve"), sprite = make(CKCID_2DENTITY, "Lists 2D"),
                    frame = make(CKCID_3DENTITY, "Lists Frame", true), plain = make(CKCID_3DENTITY, "Lists Plain Frame", true),
                    excluded = make(CKCID_3DENTITY, "Lists Excluded Frame", true), entity = make(CKCID_3DENTITY, "Lists Entity"),
                    hidden = make(CKCID_CURVE, "Lists Hidden Curve");
        check(camera && light && curve && sprite && frame && plain && excluded && entity && hidden, "the lists' objects");
        LayoutLists lists;
        lists.cones = {camera, light, curve}; // a curve has no cone
        lists.playerShown = {camera, light, curve, sprite, frame};
        lists.representations = {{frame, 0}, {entity, 3}}; // not a frame: not in the table
        lists.hiddenCurvePoints = {hidden};                 // Show curve points off: not in +180
        LayoutInterfaceData data = ReadLayoutInterfaceData(context);
        WriteLayoutLists(context, lists, [&](CK_ID id) { return id != excluded; }, data);
        auto flagsOf = [&](CK_ID id) -> long long {
            for (size_t i = 0; i < data.flagged.size(); ++i)
                if (data.flagged[i] == id) return data.flags[i];
            return -1;
        };
        check(data.lists[0] == std::vector<CK_ID>{light} && data.lists[1] == std::vector<CK_ID>{camera} &&
                  data.lists[2] == std::vector<CK_ID>{camera} && data.lists[3] == std::vector<CK_ID>{light} &&
                  data.lists[4] == std::vector<CK_ID>{curve} && data.last == std::vector<CK_ID>{sprite},
              "each object in its class's list");
        check(flagsOf(frame) == FRAME_PLAYER_SHOWN && flagsOf(plain) == 1 && flagsOf(excluded) < 0 && flagsOf(entity) < 0,
              "every frame saved in the frames' table");
        // +180: the curve made here is listed, after the ones read; nothing else.
        const std::vector<CK_ID> &drawn = data.lists[LAYOUT_DRAWN_CURVES];
        bool onlyCurves = true;
        for (CK_ID id : drawn) onlyCurves = onlyCurves && CKCurve::Cast(context->GetObject(id));
        check(!drawn.empty() && drawn.back() == curve && onlyCurves &&
                  std::count(drawn.begin(), drawn.end(), curve) == 1 && std::count(drawn.begin(), drawn.end(), hidden) == 0,
              "the curve made here is in +180, the hidden one not");
        const std::string saved = (fs::path(outputDirectory) / "layout-interface-lists.cmo").string();
        SaveOptions options;
        options.allowOverwrite = true;
        SaveReport report;
        CompositionData composition;
        composition.layout = &data;
        check(SaveComposition(document, saved, composition, options, report), "save the lists");
        check(document.Open(saved, &error), "reopen the lists: " + error);
        auto names = [&](const std::set<CK_ID> &ids) {
            std::set<std::string> named;
            for (CK_ID id : ids)
                if (CKObject *object = context->GetObject(id)) named.insert(object->GetName() ? object->GetName() : "");
            return named;
        };
        const LayoutLists read = ReadLayoutLists(context, ReadLayoutInterfaceData(context));
        std::map<std::string, int> representations;
        for (const auto &entry : read.representations)
            if (CKObject *object = context->GetObject(entry.first)) representations[object->GetName()] = entry.second;
        check(names(read.cones) == std::set<std::string>{"Lists Camera", "Lists Light"} &&
                  names(read.playerShown) ==
                      std::set<std::string>{"Lists Camera", "Lists Light", "Lists Curve", "Lists 2D", "Lists Frame"} &&
                  representations == std::map<std::string, int>{{"Lists Frame", 0}},
              "the lists come back");
        std::set<std::string> drawnBack;
        for (CK_ID id : ReadLayoutInterfaceData(context).lists[LAYOUT_DRAWN_CURVES])
            if (CKObject *object = context->GetObject(id)) drawnBack.insert(object->GetName() ? object->GetName() : "");
        check(drawnBack.count("Lists Curve") == 1, "the curve made here comes back in +180");
        check(names(read.hiddenCurvePoints).count("Lists Hidden Curve") == 1 && names(read.hiddenCurvePoints).count("Lists Curve") == 0,
              "the hidden curve comes back without its points");
    }
    document.Close();
    if (failures) return 1;
    std::cout << "Layout interface data passed\n";
    return 0;
}

// The objects' pictures (Dev.exe sub_5D1C90 / sub_5D1D60): one given to an
// entity, saved, read back after a reopen with its pixels; a save without a
// change keeps them; a new composition gets the pictures object.
int ObjectPicturesFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory) {
    namespace fs = std::filesystem;
    fs::create_directories(outputDirectory);
    host.ClearAll();
    Document document(host);
    std::string error;
    if (!document.Open(path, &error)) {
        std::cerr << error << '\n';
        return 2;
    }
    CKContext *context = host.Context();
    int failures = 0;
    auto check = [&](bool condition, const std::string &what) {
        if (!condition) {
            std::cerr << "object pictures: " << what << '\n';
            ++failures;
        }
    };
    // A 52 x 40 picture of a known pattern.
    constexpr int WIDTH = 52, HEIGHT = 40;
    auto picture = [&](CKDWORD seed) {
#ifdef _WIN32
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = WIDTH;
        info.bmiHeader.biHeight = -HEIGHT;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bitmap && bits)
            for (int i = 0; i < WIDTH * HEIGHT; ++i) static_cast<CKDWORD *>(bits)[i] = 0xFF000000 | (seed * 2654435761u + i * 40503u);
        document.KeepBitmap(bitmap);
        return static_cast<void *>(bitmap);
#else
        std::vector<CKDWORD> values(WIDTH * HEIGHT);
        for (int i = 0; i < WIDTH * HEIGHT; ++i) values[i] = 0xFF000000 | (seed * 2654435761u + i * 40503u);
        VxImageDescEx description;
        description.Width = WIDTH; description.Height = HEIGHT;
        description.BitsPerPixel = 32; description.BytesPerLine = WIDTH * 4;
        description.RedMask = 0x00FF0000; description.GreenMask = 0x0000FF00;
        description.BlueMask = 0x000000FF; description.AlphaMask = 0xFF000000;
        description.Image = reinterpret_cast<XBYTE *>(values.data());
        BITMAP_HANDLE bitmap = VxCreateBitmap(description);
        document.KeepBitmap(bitmap);
        return bitmap;
#endif
    };
    auto pixels = [&](void *bitmap) {
        std::vector<CKDWORD> result(WIDTH * HEIGHT);
#ifdef _WIN32
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = WIDTH;
        info.bmiHeader.biHeight = -HEIGHT;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAP description = {};
        const bool sized = bitmap && GetObject(static_cast<HBITMAP>(bitmap), sizeof(description), &description) &&
                           description.bmWidth == WIDTH && std::abs(description.bmHeight) == HEIGHT;
        if (!sized || GetDIBits(dc, static_cast<HBITMAP>(bitmap), 0, HEIGHT, result.data(), &info, DIB_RGB_COLORS) != HEIGHT)
            result.clear();
        DeleteDC(dc);
#else
        VxImageDescEx description;
        const bool sized = bitmap && VxConvertBitmap(bitmap, description) && description.Image &&
                           description.Width == WIDTH && description.Height == HEIGHT;
        if (sized) std::memcpy(result.data(), description.Image, WIDTH * HEIGHT * 4);
        else result.clear();
        delete[] description.Image;
#endif
        for (CKDWORD &pixel : result) pixel |= 0xFF000000;
        return result;
    };
    check(ReadObjectPictures(document).empty(), "the sample has no picture");
    CK3dEntity *entity = nullptr;
    const XObjectPointerArray &list = context->GetObjectListByType(CKCID_3DENTITY, TRUE);
    for (int i = 0; i < list.Size() && !entity; ++i)
        if (list[i] && !list[i]->IsPrivate() && list[i]->GetName()) entity = CK3dEntity::Cast(list[i]);
    check(entity != nullptr, "an entity");
    if (!entity) return 1;
    const std::string name = entity->GetName();
    void *given = picture(7);
    const std::vector<CKDWORD> expected = pixels(given);
    // Saved with the composition (SaveComposition writes them first); none: the composition's kept.
    auto save = [&](const std::string &file, const std::map<CK_ID, void *> *pictures = nullptr) {
        const std::string saved = (fs::path(outputDirectory) / file).string();
        SaveOptions options;
        options.allowOverwrite = true;
        SaveReport report;
        CompositionData data;
        data.pictures = pictures;
        check(SaveComposition(document, saved, data, options, report), "save " + file);
        for (const auto &message : report.errors) std::cerr << "  " << message << '\n';
        check(document.Open(saved, &error), "reopen " + file + ": " + error);
    };
    auto pictureOf = [&](const std::string &object) -> void * {
        const std::map<CK_ID, void *> read = ReadObjectPictures(document);
        for (const auto &entry : read) {
            CKObject *owner = context->GetObject(entry.first);
            if (owner && owner->GetName() && object == owner->GetName()) return entry.second;
        }
        return nullptr;
    };
    const std::map<CK_ID, void *> givenPictures{{entity->GetID(), given}};
    save("object-pictures.cmo", &givenPictures);
    void *back = pictureOf(name);
    check(back && !expected.empty() && pixels(back) == expected, "the picture comes back with its pixels");
    // Saved again without a change: the file's pictures object is kept.
    save("object-pictures-again.cmo");
    back = pictureOf(name);
    check(back && pixels(back) == expected, "a save without a change keeps the picture");
    // A new composition: the object is made.
    check(document.New(&error), "new: " + error);
    CKObject *made = context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Pictured");
    if (made) {
        document.Include(made->GetID());
        context->GetCurrentLevel()->AddObject(made);
    }
    void *other = picture(9);
    const std::vector<CKDWORD> otherPixels = pixels(other);
    const std::map<CK_ID, void *> otherPictures{{made ? made->GetID() : 0, other}};
    save("object-pictures-new.cmo", &otherPictures);
    back = pictureOf("Pictured");
    check(back && pixels(back) == otherPixels, "a new composition's picture comes back");
    document.Close();
    if (failures) return 1;
    std::cout << "Object pictures passed\n";
    return 0;
}

// The File menu's commands: Save Version and Revert to Saved's names
// (Dev.exe sub_5C8BC0, sub_5C8ED0), Export to Player's file for the player
// (sub_5C6400) and Create Web Page's page and composition (sub_598700).
int FileMenuFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory) {
    namespace fs = std::filesystem;
    fs::create_directories(outputDirectory);
    int failures = 0;
    auto check = [&](bool condition, const std::string &what) {
        if (!condition) {
            std::cerr << "file menu: " << what << '\n';
            ++failures;
        }
    };
    const struct {
        const char *path;
        int step;
        const char *versioned;
    } versions[] = {{"C:\\Work\\Scene.cmo", 1, "C:\\Work\\Scene_01.cmo"},
                    {"C:\\Work\\Scene_01.cmo", 1, "C:\\Work\\Scene_02.cmo"},
                    {"C:\\Work\\Scene_09.cmo", 1, "C:\\Work\\Scene_10.cmo"},
                    {"C:\\Work\\Scene_10.cmo", -1, "C:\\Work\\Scene_09.cmo"},
                    {"C:\\Work\\Scene3.cmo", 1, "C:\\Work\\Scene_04.cmo"},
                    {"C:\\Work\\Scene_099.cmo", 1, "C:\\Work\\Scene_100.cmo"},
                    {"C:\\Work\\Scene.cmo", -1, "C:\\Work\\Scene_-1.cmo"},
                    {"C:\\Work\\Scene", 1, ""}};
    for (const auto &version : versions)
        check(VersionedPath(version.path, version.step) == version.versioned,
              std::string(version.path) + " gives " + VersionedPath(version.path, version.step));
    check(WebPageCompositionPath("C:\\Web\\Page.htm", false) == "C:\\Web\\Page.vmo" &&
              WebPageCompositionPath("C:\\Web\\Page.htm", true) == "C:\\Web\\Page.cmo",
          "the web page's composition");
    int major = 0, minor = 0;
    check(RuntimeVersion(host.RuntimeDirectory(), major, minor) && major == 2 && minor >= 1,
          "the runtime's version: " + std::to_string(major) + "." + std::to_string(minor));
    const std::string page = WebPageHtml("A Title", "C:\\Web\\Page.vmo", 320, 240, major, minor);
    const std::string page21 = WebPageHtml("A Title", "C:\\Web\\Page.vmo", 320, 240, 2, 1);
    check(page21.find("Install2.1/Installer.exe#Version=2,1,0,1") != std::string::npos &&
              page21.find("playernetscape2.1.asp") != std::string::npos && page21.find("2.5") == std::string::npos,
          "the web page names the runtime's player");
    check(page.find("<TITLE>A Title</TITLE>") != std::string::npos &&
              page.find("<PARAM NAME=\"SRC\" VALUE=\"Page.vmo\">") != std::string::npos &&
              page.find("SRC=\"Page.vmo\"") != std::string::npos &&
              page.find("WIDTH=\"320\" HEIGHT=\"240\"") != page.rfind("WIDTH=\"320\" HEIGHT=\"240\"") &&
              page.find('$') == std::string::npos,
          "the web page's template filled");

    host.ClearAll();
    Document document(host);
    std::string error;
    if (!document.Open(path, &error)) {
        std::cerr << error << '\n';
        return 2;
    }
    CKContext *context = host.Context();
    // An edit, kept unsaved by the exports.
    std::vector<CK_ID> entities;
    const XObjectPointerArray &list = context->GetObjectListByType(CKCID_BEOBJECT, TRUE);
    for (int i = 0; i < list.Size() && entities.empty(); ++i)
        if (list[i] && !list[i]->IsPrivate() && !CKLevel::Cast(list[i]) && !CKScene::Cast(list[i]))
            entities.push_back(list[i]->GetID());
    UndoHistory &undo = document.History();
    check(CreateGroup(document, "File Menu Group", entities).ok, "a group");
    const bool dirty = document.Dirty();
    const CK_FILE_WRITEMODE writeMode = context->GetFileWriteMode();
    auto save = [&](const std::string &name, unsigned int mode) {
        const std::string saved = (fs::path(outputDirectory) / name).string();
        SaveOptions options;
        options.allowOverwrite = true;
        options.writeMode = mode;
        options.markSaved = false;
        SaveReport report;
        check(SaveDocumentAs(document, saved, options, report), "save " + name);
        for (const auto &message : report.errors) std::cerr << "  " << message << '\n';
        std::cout << name << ": " << fs::file_size(saved) << " bytes, " << report.parametersVerified
                  << " parameters verified, " << report.scriptsVerified << " Scripts\n";
        check(context->GetFileWriteMode() == writeMode, name + ": the write mode restored");
        check(document.Dirty() == dirty, name + ": the composition not marked saved");
        return saved;
    };
    // Export to Player: CKFILE_FORVIEWER | CKFILE_WHOLECOMPRESSED.
    const std::string player = save("file-menu-export.vmo", CKFILE_FORVIEWER | CKFILE_WHOLECOMPRESSED);
    // Create Web Page with Save as Cmo: CKFILE_WHOLECOMPRESSED only.
    const std::string htmlPath = (fs::path(outputDirectory) / "file-menu-page.htm").string();
    const std::string composition = save(fs::path(WebPageCompositionPath(htmlPath, true)).filename().string(),
                                         CKFILE_WHOLECOMPRESSED);
    const size_t scripts = document.Scripts().size();
    check(IsPlayerFile(player) && !IsPlayerFile(composition) && !IsPlayerFile(path), "a file for the player is told apart");
    document.Close();
    // The file for the player has its Scripts without their Schematic.
    Document reopened(host);
    check(reopened.Open(player, &error), "the export reopens: " + error);
    size_t chunks = 0;
    for (const ScriptInfo &script : reopened.Scripts()) chunks += script.hasInterfaceChunk;
    check(reopened.Scripts().size() == scripts && chunks == 0, "the export's Scripts have no InterfaceChunk");
    reopened.Close();
    check(reopened.Open(composition, &error), "the web page's composition reopens: " + error);
    chunks = 0;
    for (const ScriptInfo &script : reopened.Scripts()) chunks += script.hasInterfaceChunk;
    check(reopened.Scripts().size() == scripts && chunks == scripts, "the web page's composition keeps its InterfaceChunks");
    reopened.Close();
    if (failures) return 1;
    std::cout << "File menu passed\n";
    return 0;
}

int LinksFiles(CkRuntimeHost &host, const std::vector<std::string> &paths) {
    int failures = 0;
    size_t found = 0; // invalid links in all the samples: none, nothing was tested
    for (const std::string &path : paths) {
        Document document(host);
        std::string error;
        if (!document.Open(path, &error)) {
            std::cerr << "links: cannot open " << path << ": " << error << '\n';
            ++failures;
            continue;
        }
        const std::vector<InvalidLink> plain = FindInvalidLinks(document, false);
        const std::vector<InvalidLink> typed = FindInvalidLinks(document, true);
        std::cout << "links: " << path << ": " << plain.size() << " invalid, " << typed.size() << " with the types\n";
        found += typed.size();
        for (const InvalidLink &link : typed) {
            CKObject *script = document.Context()->GetObject(link.script);
            std::cout << "  " << (script && script->GetName() ? script->GetName() : "?") << " graph " << link.graph
                      << " link " << link.slot << ": " << link.reason << '\n';
        }
        if (!typed.empty()) {
            const int removed = RemoveInvalidLinks(document, typed);
            const size_t left = FindInvalidLinks(document, true).size();
            if (removed != static_cast<int>(typed.size()) || left != 0) {
                std::cerr << "links: " << path << ": removed " << removed << ", " << left << " left\n";
                ++failures;
            }
        }
        failures += plain.size() > typed.size() ? 1 : 0; // the type test only adds links
        document.Close();
    }
    if (!found) {
        std::cerr << "links: no invalid link in the samples, nothing was tested\n";
        ++failures;
    }
    if (failures) return 1;
    std::cout << "Links passed\n";
    return 0;
}

// The images the editor draws (resources/images.txt) are in the program
// (generated ResourceImages.cpp, built into this one too, as named in
// ui/controls/DevBitmaps.cpp), the data files in data beside it.
void NativePathChecks(CkRuntimeHost &host) {
    namespace fs = std::filesystem;
    const fs::path parent = fs::current_path();
    const fs::path root = parent / ("native-paths-" + std::to_string(SDL_GetPerformanceCounter()));
    CHECK(root.parent_path() == parent);
    CHECK(fs::create_directory(root));
    struct Cleanup {
        fs::path root;
        ~Cleanup() { std::error_code error; fs::remove_all(root, error); }
    } cleanup{root};
    const char *names[] = {"plain", u8"caf\u00E9", u8"\u4E2D\u6587", u8"\U0001F642"};
    const std::string rawName = "legacy-\xE9\x9C\xD6\xD0\xCE\xC4";
    for (const char *name : names) {
        const fs::path directory = root / fs::u8path(name);
        CHECK(fs::create_directory(directory));
        const std::string resource = (directory / "Data.rsc").u8string();
        std::string error;
        CHECK(CreateDataResource(resource, &error));
        DataResource folders;
        CHECK(ReadDataResource(resource, folders, &error));
        CHECK(folders.root.folders.size() == 7);
        BehaviorDescription description;
        description.name = rawName; description.author = rawName; description.description = rawName;
        description.cid = CKCID_3DENTITY; description.version = 1.5f;
        const std::string behavior = (directory / "behavior.nms").u8string();
        CHECK(WriteBehaviorDescription(behavior, description));
        BehaviorDescription read;
        CHECK(ReadBehaviorDescription(behavior, read));
        CHECK(read.name == description.name && read.author == description.author && read.description == description.description);
        CHECK(read.cid == description.cid && read.version == description.version);

        Document document(host);
        CHECK(document.New(&error));
        const EditResult made = CreateLayoutObject(document, LayoutObject::Frame3D);
        CHECK(made.ok && RenameLevelObject(document, made.created, rawName).ok);
        const fs::path imageFile = directory / fs::u8path(std::string(name) + ".bmp");
        CKDWORD colors[] = {0xFF123456, 0xFFABCDEF, 0xFF765432, 0xFF010203};
        SDL_Surface *surface = SDL_CreateSurfaceFrom(2, 2, SDL_PIXELFORMAT_ARGB8888, colors, 8);
        CHECK(surface);
        const bool written = SDL_SaveBMP(surface, imageFile.u8string().c_str());
        SDL_DestroySurface(surface);
        CHECK(written);
        bool representable = true;
        try { (void)imageFile.string(); } catch (const std::system_error &) { representable = false; }
        const size_t createdCount = document.CreatedObjects().size();
        const EditResult image = LoadResourceFile(document, imageFile.u8string(), CKCID_TEXTURE, 0);
        CHECK(image.ok == representable);
        std::string imageName, imageReference;
        auto checkPixels = [&](CKTexture *texture) {
            CHECK(texture && texture->GetWidth() == 2 && texture->GetHeight() == 2);
            const CKBYTE *pixels = texture->LockSurfacePtr(0);
            CHECK(pixels);
            CKDWORD actual[4];
            std::memcpy(actual, pixels, sizeof(actual));
            texture->ReleaseSurfacePtr(0);
            CHECK(std::memcmp(actual, colors, sizeof(colors)) == 0);
        };
        if (representable) {
            CKTexture *texture = CKTexture::Cast(host.Context()->GetObject(image.created));
            checkPixels(texture);
            CHECK(texture->GetName() && texture->GetSlotFileName(0));
            CHECK(imageFile.stem().string() == texture->GetName());
            imageName = texture->GetName();
            imageReference = texture->GetSlotFileName(0);
            CHECK(imageReference == imageFile.string());
        } else CHECK(document.CreatedObjects().size() == createdCount);
        SaveOptions options;
        SaveReport report;
        const fs::path saved = directory / "roundtrip.cmo";
        const bool dirty = document.Dirty();
        CHECK(SaveDocumentAs(document, saved.u8string(), options, report) == representable);
        CHECK(report.saved == representable);
        if (!representable) {
            CHECK(!report.errors.empty() && !fs::exists(saved) && document.Dirty() == dirty);
            CHECK(rawName == host.Context()->GetObject(made.created)->GetName());
            continue;
        }
        document.Close();
        CHECK(document.Open(saved.u8string(), &error) && !document.Dirty());
        CKObject *object = host.Context()->GetObjectByNameAndClass(const_cast<CKSTRING>(rawName.c_str()), CKCID_3DENTITY);
        CHECK(object && rawName == object->GetName());
        CKTexture *texture = CKTexture::Cast(host.Context()->GetObjectByNameAndClass(const_cast<CKSTRING>(imageName.c_str()), CKCID_TEXTURE));
        checkPixels(texture);
        // CKBitmapData saves CKJustFile: keep the native filename bytes.
        CHECK(texture->GetSlotFileName(0));
        CHECK(imageFile.filename().string() == texture->GetSlotFileName(0));
    }
    std::cout << "Native resource paths, metadata bytes and composition round trips passed\n";
}

void CompositionReadCompatibility(CkRuntimeHost &host) {
    namespace fs = std::filesystem;
    const fs::path root = fs::current_path() / ("composition-formats-" + std::to_string(SDL_GetPerformanceCounter()));
    CHECK(root.parent_path() == fs::current_path() && fs::create_directory(root));
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code error; fs::remove_all(root, error); } } cleanup{root};
    const struct { CKDWORD version; int extra; } formats[] = {{0x0D, 0}, {0x10, 1}, {0x12, 2}, {0x16, 3}};
    const std::string comment = "Stored comment \xE9\x9C\xD6\xD0";
    for (const auto &format : formats) {
        Document document(host);
        std::string error;
        CHECK(document.New(&error));
        const EditResult owner = CreateLayoutObject(document, LayoutObject::Frame3D);
        CHECK(owner.ok);
        const EditResult made = CreateScript(document, owner.created, "Stored Script");
        CHECK(made.ok);
        const EditResult local = AddLocalParameter(document, made.created, made.created,
                                                   "Stored value", CKPGUID_INT, Point(200, 60));
        CHECK(local.ok && SetParameterText(document, made.created, local.created, "137").ok);
        CKBehavior *script = CKBehavior::Cast(document.Context()->GetObject(made.created));
        InterfaceData data;
        data.version = format.version;
        data.useDevSectionedLayout = InterfaceChunkFormat::VersionUsesSections(format.version);
        data.rootBehavior.id = made.created;
        data.rootBehavior.rect = Rect(0, 0, 600, 240);
        data.rootBehavior.folded = false;
        data.rootBehavior.localParams.emplace_back(local.created, 10, 3, PARAM_STYLE_NAME);
        data.rootBehavior.comments.emplace_back(0, 200, 120, 180, 60, comment);
        data.extraDataVersion = format.extra;
        if (format.extra) {
            ExtraData breakpoint;
            breakpoint.type = EXTRA_DATA_BEHAVIOR;
            breakpoint.id1 = made.created;
            data.extraData.push_back(breakpoint);
        }
        CKStateChunk *chunk = GenerateInterfaceChunk(data, script);
        CHECK(chunk);
        script->SetInterfaceChunk(chunk);
        document.Session(made.created)->Assign(data);
        document.Session(made.created)->MarkSaved();
        const fs::path source = root / (std::to_string(format.version) + "-source.cmo");
        const fs::path saved = root / (std::to_string(format.version) + "-saved.cmo");
        SaveOptions options;
        SaveReport report;
        CHECK(SaveDocumentAs(document, source.u8string(), options, report));
        document.Close();
        for (const auto &path : {source, saved}) {
            CHECK(document.Open(path.u8string(), &error) && !document.Dirty());
            CHECK(document.Scripts().size() == 1);
            const CK_ID id = document.Scripts().front().id;
            ScriptSession *session = document.Session(id);
            CHECK(session && session->Origin() == SessionOrigin::LoadedChunk && !session->Dirty());
            CHECK(session->Data().version == format.version && session->Data().extraDataVersion == format.extra);
            CHECK(session->Data().rootBehavior.comments.size() == 1);
            CHECK(session->Data().rootBehavior.comments.front().text == comment);
            CHECK(session->Data().extraData.size() == (format.extra ? 1u : 0u));
            if (format.extra) CHECK(session->Data().extraData.front().id1 == id);
            script = CKBehavior::Cast(document.Context()->GetObject(id));
            CHECK(script && script->GetOwner() && script->GetLocalParameterCount() == 1);
            CKParameterLocal *parameter = script->GetLocalParameter(0);
            int value = 0;
            CHECK(parameter && parameter->GetOwner() == script && parameter->GetValue(&value, FALSE) == CK_OK && value == 137);
            if (path == source) {
                const auto original = ChunkBytes(script->GetInterfaceChunk());
                CHECK(!original.empty());
                CHECK(SaveDocumentAs(document, saved.u8string(), options, report));
                CHECK(ChunkBytes(script->GetInterfaceChunk()) == original);
                CHECK(CompareReloadedChunks(document, saved.string()) == 1);
            }
            document.Close();
        }
    }
    std::cout << "Sectioned and inline compositions preserve values, raw comments, breakpoints and InterfaceChunk bytes\n";
}

void ResourceChecks() {
    std::ifstream manifest(std::string(VIRTOOLSDEV_RESOURCES_DIR) + "/images.txt");
    CHECK(manifest);
    const std::vector<ResourceEntry> entries = ReadResourceManifest(manifest);
    CHECK(!entries.empty());
    for (const ResourceEntry &entry : entries) {
        const std::string name = ResourceName(entry.group, entry.kind, entry.id);
        const EmbeddedImage *image = FindEmbeddedImage(entry.group.c_str(), entry.kind.c_str(), entry.id);
        if (!image || !image->rgba || image->width <= 0 || image->height <= 0)
            Fail(("no " + name + " in the program").c_str(), __FILE__, __LINE__);
        if (entry.size) CHECK(image->width == entry.size && image->height == entry.size);
    }
    CHECK(std::filesystem::exists(DataFilePath(INTERFACE_FILE)));
#if VIRTOOLSDEV_HAS_HELP
    CHECK(std::filesystem::exists(DataFilePath(HELP_FILE)));
    CHECK(std::filesystem::exists(DataFilePath("help/VirtoolsDevHelp.html")));
#endif
    std::cout << "Resources: " << entries.size() << " in the program\n";
}


} // namespace vdevtest

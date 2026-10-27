#include <clocale>
#include <cwchar>
#include <climits>
// Level Manager, Setups, managers, the Create menu, scenes, the Script debugger, Building Block callbacks, text encoding.
#include "VirtoolsDevTestSupport.h"
#include <chrono>
#include <thread>
#include <SDL3/SDL.h>

namespace vdevtest {

// Building Block callbacks: Construct on a variable Building Block
// (CKM_BEHAVIOREDITED) and Edit Settings (CKM_BEHAVIORSETTINGSEDITED);
// undo and redo must return to the recorded topology.
void BuildingBlockCallbacks(CkRuntimeHost &host, const BuildingBlockCatalog &catalog) {
    CKContext *context = host.Context();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Callback Script"));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    const CK_ID id = script->GetID();
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(1000);
    CHECK(document.Session(id) && EditScriptData(document, id, "Open Script",
                                                  [](InterfaceData &data) { SetScriptOpen(data, true); }));
    Recorder recorder(document, id);
    int constructed = 0, edited = 0;
    float x = 300.0f;
    for (const auto &entry : catalog.Entries()) {
        if (entry.category.rfind("Logics", 0) != 0) continue;
        CKBehaviorPrototype *prototype = CKGetPrototypeFromGuid(entry.guid);
        if (!prototype) continue;
        const CKDWORD flags = prototype->GetBehaviorFlags();
        const bool variable = (flags & CKBEHAVIOR_VARIABLEPARAMETERINPUTS) != 0;
        const bool settings = prototype->GetLocalParameterCount() > 0;
        if (!(variable && constructed < 3) && !(settings && edited < 6)) continue;
        EditResult inserted = InsertBuildingBlock(document, id, id, entry.guid, Point(x, 100));
        recorder.After(inserted, "insert " + entry.name);
        if (!inserted.ok) continue;
        x += 200.0f;
        CKBehavior *block = CKBehavior::Cast(context->GetObject(inserted.created));
        if (variable && constructed < 3) {
            EditResult pin = AddBuildingBlockPin(document, id, inserted.created, PinKind::ParamIn, "Extra",
                                                 CKPGUID_FLOAT);
            recorder.After(pin, "construct " + entry.name);
            if (pin.ok) ++constructed;
        }
        if (settings && edited < 6) {
            // Flip the first Boolean setting, else set the first Integer one to 2.
            std::map<int, std::string> values;
            for (const auto &setting : BehaviorSettings(block)) {
                if (setting.type == "Boolean") values[setting.slot] = setting.value == "TRUE" ? "FALSE" : "TRUE";
                else if (setting.type == "Integer" && values.empty()) values[setting.slot] = "2";
                if (!values.empty()) break;
            }
            if (!values.empty()) {
                EditResult changed = EditBehaviorSettings(document, id, inserted.created, values);
                recorder.After(changed, "settings " + entry.name);
                if (changed.ok) ++edited;
            }
        }
    }
    // Parameter operation link: a local of another type into an input of a
    // Building Block, through an undisplayed conversion; then deleted.
    bool converted = false;
    const CKGUID types[] = {CKPGUID_STRING, CKPGUID_FLOAT, CKPGUID_INT, CKPGUID_VECTOR, CKPGUID_BOOL};
    for (int sub = 0; sub < script->GetSubBehaviorCount() && !converted; ++sub) {
        CKBehavior *block = script->GetSubBehavior(sub);
        if (!block) continue;
        for (int input = 0; input < block->GetInputParameterCount() && !converted; ++input)
            for (const CKGUID &type : types) {
                EditResult local = AddLocalParameter(document, id, id, "Converted", type, Point(100, 300));
                recorder.After(local, "add local to convert");
                if (!local.ok) continue;
                SchematicModel model = BuildModel(document, id);
                int slot = -1;
                for (const auto &param : model.params)
                    if (param.parameter == local.created) slot = param.slot;
                const LinkEndpoint from(id, slot, ENDPOINT_PLOCAL);
                const LinkEndpoint to(block->GetID(), input, ENDPOINT_PIN);
                const auto candidates = ParameterLinkConversions(document, id, from, to);
                if (candidates.empty()) {
                    recorder.After(DeleteElements(document, id, {ElementRef{ElementKind::LocalParam, id, local.created, slot}}),
                                   "delete unused local");
                    continue;
                }
                EditResult linked = CreateParameterLink(document, id, from, to, &candidates.front());
                recorder.After(linked, "parameter operation link " + candidates.front().label);
                CHECK(linked.ok);
                model = BuildModel(document, id);
                const LinkItem *conversionLink = nullptr;
                for (const auto &link : model.links)
                    if (!link.conversion.empty() && link.start.type == ENDPOINT_PLOCAL && link.start.index == slot)
                        conversionLink = &link;
                CHECK(conversionLink && conversionLink->operationLink);
                const ElementRef linkRef = conversionLink->ref;
                EditResult deleted = DeleteElements(document, id, {linkRef});
                recorder.After(deleted, "delete parameter operation link");
                CHECK(deleted.ok && block->GetInputParameter(input)->GetDirectSource() == nullptr);
                converted = true;
                break;
            }
    }
    std::cout << "Parameter operation link " << (converted ? "passed" : "skipped (no conversion found)") << "\n";

    // Convert to Shortcut Source, then Set Shortcut Source.
    {
        CKBehavior *block = nullptr;
        for (int sub = 0; sub < script->GetSubBehaviorCount() && !block; ++sub)
            for (int i = 0; i < script->GetSubBehavior(sub)->GetInputParameterCount(); ++i)
                if (script->GetSubBehavior(sub)->GetInputParameter(i)->GetGUID() == CKPGUID_INT) block = script->GetSubBehavior(sub);
        int input = -1;
        for (int i = 0; block && i < block->GetInputParameterCount(); ++i)
            if (block->GetInputParameter(i)->GetGUID() == CKPGUID_INT) input = i;
        if (block && input >= 0) {
            EditResult a = AddLocalParameter(document, id, id, "Shared", CKPGUID_INT, Point(100, 400));
            recorder.After(a, "local a");
            EditResult b = AddLocalParameter(document, id, id, "Shared", CKPGUID_INT, Point(100, 440));
            recorder.After(b, "local b");
            EditResult c = AddLocalParameter(document, id, id, "Other", CKPGUID_INT, Point(100, 480));
            recorder.After(c, "local c");
            SchematicModel model = BuildModel(document, id);
            int slotB = -1;
            for (const auto &param : model.params)
                if (param.parameter == b.created) slotB = param.slot;
            recorder.After(CreateParameterLink(document, id, LinkEndpoint(id, slotB, ENDPOINT_PLOCAL),
                                               LinkEndpoint(block->GetID(), input, ENDPOINT_PIN)),
                           "link b");
            EditResult shortcut = ShortcutizeLocals(document, id, a.created);
            recorder.After(shortcut, "convert to shortcut source");
            CHECK(shortcut.ok && block->GetInputParameter(input)->GetDirectSource() == context->GetObject(a.created));
            EditResult retarget = RetargetShortcuts(document, id, a.created, c.created);
            recorder.After(retarget, "set shortcut source");
            CHECK(retarget.ok && block->GetInputParameter(input)->GetDirectSource() == context->GetObject(c.created));
            std::cout << "Shortcut source commands passed\n";
        }
    }

    // .nms: a Building Block saved, then imported back (outputs in the temp directory).
    {
        CKBehavior *block = script->GetSubBehaviorCount() ? script->GetSubBehavior(0) : nullptr;
        const std::string path = (std::filesystem::temp_directory_path() / "VirtoolsDevTests.nms").string();
        std::string error;
        if (const char *external = std::getenv("SCHEMATIC_IMPORT_NMS")) {
            EditResult imported = ImportBehaviorGraph(document, id, id, external, Point(600, 800));
            std::cout << "import " << external << ": " << (imported.ok ? "ok" : imported.message) << std::endl;
            recorder.After(imported, "import external");
        }
        if (block && SaveBehaviorsAsNms(document, {block->GetID()}, path, &error)) {
            const int before = script->GetSubBehaviorCount();
            EditResult imported = ImportBehaviorGraph(document, id, id, path, Point(600, 600));
            recorder.After(imported, "import behavior graph");
            if (!imported.ok) std::cout << "import refused: " << imported.message << std::endl;
            CHECK(imported.ok && script->GetSubBehaviorCount() == before + 1);
            CKBehavior *copy = script->GetSubBehavior(script->GetSubBehaviorCount() - 1);
            CHECK(copy && copy != block && copy->GetPrototypeGuid() == block->GetPrototypeGuid());
            std::filesystem::remove(path);
            std::cout << "Behavior file save/import passed\n";
        } else {
            std::cout << "Behavior file save failed: " << error << "\n";
            CHECK(false);
        }
    }
    recorder.UndoAllRedoAll();
    std::cout << "Building Block callbacks passed (" << constructed << " constructed, " << edited
              << " settings edited)\n";
    CHECK(constructed > 0 && edited > 0);
}

// Level Manager commands: Scripts, scenes, groups, places, names.
void LevelManagerCommands(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    CKLevel *level = context->GetCurrentLevel();
    CHECK(level);
    auto *entity = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Level Entity"));
    auto *other = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Other Entity"));
    CHECK(entity && other);
    level->AddObject(entity);
    level->AddObject(other);
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(100);
    auto check = [&](bool condition, const char *what) {
        if (!condition) {
            std::cerr << "Level Manager commands: " << what << std::endl;
            Fail("the check written above", __FILE__, __LINE__);
        }
    };
    // Create Script: an empty Script with Start, shown by the Schematic.
    EditResult created = CreateScript(document, entity->GetID(), "");
    check(created.ok && entity->GetScriptCount() == 1, "create script");
    CKBehavior *script = CKBehavior::Cast(context->GetObject(created.created));
    check(script && script->GetInputCount() == 1 && document.FindScript(script->GetID()) &&
              document.Session(script->GetID()),
          "new Script displayed");
    check(level->GetLevelScene()->IsObjectHere(script), "new Script in the owner's scene");
    check(undo.Undo() && entity->GetScriptCount() == 0 && document.IsDiscarded(script->GetID()),
          "undo create script");
    check(undo.Redo() && entity->GetScriptCount() == 1 && !document.IsDiscarded(script->GetID()),
          "redo create script");
    // Load Script: a Script saved alone comes back on another object.
    const std::string path = (std::filesystem::temp_directory_path() / "SchematicLevelScript.nms").string();
    std::string error;
    check(SaveBehaviorsAsNms(document, {script->GetID()}, path, &error), "save script");
    EditResult loaded = LoadScripts(document, path, other->GetID());
    if (!loaded.ok) std::cerr << "load: " << loaded.message << std::endl;
    check(loaded.ok && other->GetScriptCount() == 1 && other->GetScript(0)->GetID() == loaded.created, "load script");
    check(entity->GetScriptCount() == 1 && document.Session(loaded.created), "loaded Script displayed");
    check(undo.Undo() && other->GetScriptCount() == 0, "undo load script");
    check(undo.Redo() && other->GetScriptCount() == 1, "redo load script");
    // Scenes.
    const int scenes = level->GetSceneCount();
    EditResult scene = CreateScene(document, "", {entity->GetID()});
    CKScene *created_scene = CKScene::Cast(context->GetObject(scene.created));
    check(scene.ok && level->GetSceneCount() == scenes + 1 && created_scene->IsObjectHere(entity) &&
              !created_scene->IsObjectHere(other),
          "create scene");
    check(SendToScene(document, {other->GetID()}, scene.created).ok && created_scene->IsObjectHere(other),
          "send to scene");
    check(undo.Undo() && !created_scene->IsObjectHere(other), "undo send to scene");
    check(undo.Undo() && level->GetSceneCount() == scenes, "undo create scene");
    check(undo.Redo() && level->GetSceneCount() == scenes + 1, "redo create scene");
    // Delete in a scene's section: the entity leaves the scene; undo puts
    // it back with its flags.
    {
        CKScene *again = CKScene::Cast(context->GetObject(scene.created));
        check(again && again->IsObjectHere(entity), "the scene again");
        again->SetObjectFlags(entity, CK_SCENEOBJECT_FLAGS(CK_SCENEOBJECT_START_DEACTIVATE));
        check(RemoveFromScene(document, {entity->GetID()}, scene.created, true).ok && !again->IsObjectHere(entity),
              "remove from scene");
        check(undo.Undo() && again->IsObjectHere(entity) &&
                  (again->GetObjectFlags(entity) & CK_SCENEOBJECT_START_DEACTIVATE),
              "undo remove from scene: flags kept");
        check(!RemoveFromScene(document, {other->GetID()}, scene.created, true).ok, "not in the scene: refused");
    }
    // Groups.
    EditResult group = CreateGroup(document, "", {entity->GetID()});
    CKGroup *created_group = CKGroup::Cast(context->GetObject(group.created));
    check(group.ok && entity->IsInGroup(created_group), "create group");
    check(SendToGroup(document, {other->GetID()}, group.created).ok && other->IsInGroup(created_group),
          "send to group");
    check(undo.Undo() && !other->IsInGroup(created_group), "undo send to group");
    // Delete on a member row: it leaves the group; undo puts it back first.
    check(SendToGroup(document, {other->GetID()}, group.created).ok && created_group->GetObject(0) == entity,
          "send to group again");
    check(RemoveGroupMembers(document, group.created, {entity->GetID()}).ok && created_group->GetObjectCount() == 1 &&
              created_group->GetObject(0) == other,
          "remove group member");
    check(undo.Undo() && created_group->GetObjectCount() == 2 && created_group->GetObject(0) == entity,
          "undo remove group member: its place");
    check(undo.Undo() && !other->IsInGroup(created_group), "undo send to group again");
    // Character Setup: a body part of no character joins one and leaves it;
    // undo takes back each.
    {
        auto *character = CKCharacter::Cast(context->CreateObject(CKCID_CHARACTER, (CKSTRING) "Test Character"));
        auto *part = CKBodyPart::Cast(context->CreateObject(CKCID_BODYPART, (CKSTRING) "Test Part"));
        check(character && part, "character and body part");
        level->AddObject(character);
        level->AddObject(part);
        const int parts = character->GetBodyPartCount();
        check(AddBodyParts(document, character->GetID(), {part->GetID()}).ok && part->GetCharacter() == character &&
                  character->GetBodyPartCount() == parts + 1,
              "add body part");
        check(!AddBodyParts(document, character->GetID(), {part->GetID()}).ok, "a character's body part: refused");
        check(RemoveBodyParts(document, character->GetID(), {part->GetID()}).ok && !part->GetCharacter() &&
                  character->GetBodyPartCount() == parts,
              "remove body part");
        check(undo.Undo() && part->GetCharacter() == character, "undo remove body part");
        check(undo.Undo() && !part->GetCharacter() && character->GetBodyPartCount() == parts, "undo add body part");
    }
    // Drops: a Script moves to an object its class takes; refused, named,
    // for another class; undo gives it back.
    {
        const int otherScripts = other->GetScriptCount();
        std::vector<std::string> refused;
        check(MoveScripts(document, {script->GetID()}, other->GetID(), &refused).ok && script->GetOwner() == other &&
                  other->GetScriptCount() == otherScripts + 1 && entity->GetScriptCount() == 0 && refused.empty(),
              "move script");
        check(undo.Undo() && script->GetOwner() == entity && other->GetScriptCount() == otherScripts, "undo move script");
        const CK_CLASSID compatible = script->GetCompatibleClassID();
        script->SetCompatibleClassID(CKCID_CAMERA);
        check(!MoveScripts(document, {script->GetID()}, other->GetID(), &refused).ok && refused.size() == 1 &&
                  script->GetOwner() == entity,
              "incompatible script refused");
        script->SetCompatibleClassID(compatible);
        // Level Manager paste on the Script's own owner: "<name>.Copy".
        const int scripts = entity->GetScriptCount();
        const std::vector<EditResult> pasted = PasteScriptsOn(document, {script->GetID()}, {entity->GetID()}, true);
        CKBehavior *copy = pasted.size() == 1 && pasted[0].ok ? CKBehavior::Cast(context->GetObject(pasted[0].created)) : nullptr;
        check(copy && entity->GetScriptCount() == scripts + 1 &&
                  std::string(copy->GetName()) == std::string(script->GetName()) + ".Copy",
              "paste on the owner: .Copy");
        check(undo.Undo() && entity->GetScriptCount() == scripts, "undo paste");
    }
    // Places.
    EditResult place = CreatePlace(document, "", {entity->GetID()});
    check(place.ok && entity->GetParent() == context->GetObject(place.created), "create place");
    check(RemoveFromPlace(document, {entity->GetID()}).ok && !entity->GetParent(), "remove from place");
    check(undo.Undo() && entity->GetParent() == context->GetObject(place.created), "undo remove from place");
    check(undo.Undo() && !entity->GetParent(), "undo create place");
    // Rename.
    check(RenameLevelObject(document, other->GetID(), "Renamed").ok &&
              std::string(other->GetName()) == "Renamed",
          "rename");
    check(undo.Undo() && std::string(other->GetName()) == "Other Entity", "undo rename");
    {
        const size_t steps = undo.UndoCount();
        const EditResult same = RenameLevelObject(document, other->GetID(), "Other Entity");
        check(same.Unchanged() && same.Status() == EditStatus::NoChange && (undo.UndoCount() - steps) == 0,
              "the same name: nothing to do, not refused");
        check(RenameLevelObject(document, 0, "Nobody").Status() == EditStatus::Refused, "no object: refused");
    }
    // Arrays, 3D Layout visibility, positions.
    EditResult array = CreateArray(document, "");
    check(array.ok && CKDataArray::Cast(context->GetObject(array.created)), "create array");
    check(undo.Undo() && document.IsDiscarded(array.created), "undo create array");
    const CKBOOL visible = entity->IsVisible();
    check(ShowObjects(document, {entity->GetID()}, !visible).ok && entity->IsVisible() != visible, "show/hide");
    check(undo.Undo() && entity->IsVisible() == visible, "undo show/hide");
    // The Visible cell's third state: hidden with the hierarchy.
    check(ShowObjectsAs(document, {entity->GetID()}, CKHIERARCHICALHIDE).ok && !entity->IsVisible() &&
              entity->IsHierarchicallyHide(),
          "hierarchical hide");
    check(undo.Undo() && entity->IsVisible() == visible && !entity->IsHierarchicallyHide(), "undo hierarchical hide");
    // The Priority cell.
    {
        const int before = entity->GetPriority();
        check(SetPriorities(document, {entity->GetID()}, before + 7).ok && entity->GetPriority() == before + 7, "set priority");
        check(SetPriorities(document, {entity->GetID()}, before + 7).Unchanged(), "the same priority: nothing to do");
        check(undo.Undo() && entity->GetPriority() == before, "undo priority");
    }
    VxVector position(1.0f, 2.0f, 3.0f), read;
    check(EditObjectState(document, entity->GetID(), "Position",
                          [&position](CKObject *object) { CK3dEntity::Cast(object)->SetPosition(&position); })
              .ok,
          "set position");
    entity->GetPosition(&read);
    check(read.x == 1.0f && read.z == 3.0f, "position set");
    check(undo.Undo(), "undo position");
    // Attributes: the first one a 3D entity accepts.
    CKAttributeManager *attributes = context->GetAttributeManager();
    CKAttributeType attribute = -1;
    for (int i = 0; attributes && i < attributes->GetAttributeCount() && attribute < 0; ++i) {
        const CK_CLASSID cid = attributes->GetAttributeCompatibleClassId(i);
        if (attributes->IsAttributeIndexValid(i) && (!cid || CKIsChildClassOf(entity, cid))) attribute = i;
    }
    if (attribute >= 0) {
        check(AddAttribute(document, {entity->GetID()}, attribute).ok && entity->HasAttribute(attribute), "add attribute");
        check(RemoveAttribute(document, entity->GetID(), attribute).ok && !entity->HasAttribute(attribute),
              "remove attribute");
        check(undo.Undo() && entity->HasAttribute(attribute), "undo remove attribute");
        check(undo.Undo() && !entity->HasAttribute(attribute), "undo add attribute");
        // Dropped from the Attribute Manager: added (one step); again: nothing to do.
        check(DropAttribute(document, {entity->GetID()}, attribute, 0).ok && entity->HasAttribute(attribute), "drop attribute");
        check(DropAttribute(document, {entity->GetID()}, attribute, 0).Unchanged(), "dropped on an object having it");
        check(undo.Undo() && !entity->HasAttribute(attribute), "undo drop attribute");
    }
    // Setup edits: object state before / after is the undo step.
    VxVector moved(4.0f, 5.0f, 6.0f), now;
    check(EditObjectState(document, entity->GetID(), "Set Position", [&](CKObject *o) {
              CK3dEntity::Cast(o)->SetPosition(&moved);
          }).ok,
          "setup edit");
    check(undo.Undo(), "undo setup edit");
    entity->GetPosition(&now);
    check(now.x != 4.0f, "setup edit undone");
    check(undo.Redo(), "redo setup edit");
    entity->GetPosition(&now);
    check(now.x == 4.0f && now.z == 6.0f, "setup edit redone");
    check(!EditObjectState(document, entity->GetID(), "No change", [](CKObject *) {}).ok, "no-change edit refused");
    // Group order: Move Up / Move Down (CKGroup::MoveObjectUp/Down).
    EditResult ordered = CreateGroup(document, "", {entity->GetID(), other->GetID()});
    CKGroup *orderedGroup = CKGroup::Cast(context->GetObject(ordered.created));
    check(ordered.ok && orderedGroup->GetObject(1) == other, "group order");
    check(MoveInGroup(document, ordered.created, other->GetID(), -1).ok && orderedGroup->GetObject(0) == other,
          "move up in group");
    check(undo.Undo() && orderedGroup->GetObject(1) == other, "undo move up in group");
    // The editor's own saved data is one undo step marking the composition
    // modified; a step that fails clears the history.
    {
        int value = 0;
        check(RecordValue(document, "Value", value, 1) && value == 1, "session data applied");
        const std::uint64_t revision = document.TopologyRevision();
        check(undo.Undo() && value == 0 && document.TopologyRevision() != revision && undo.Redo() &&
                  value == 1,
              "session data undone and redone");
        {
            Edit failing(document, "Failing");
            failing.Done([] { return true; }, [] { return false; });
            failing.Commit();
        }
        check(!undo.Undo() && !undo.CanUndo() && !undo.CanRedo(), "a failed undo clears the history");
        // The save point: undone back to it the composition is unmodified,
        // unless it was changed outside undo; a new edit replaces a save point undone.
        auto edit = [&](const char *label) { return RecordValue(document, label, value, value + 1); };
        document.MarkSaved();
        undo.MarkSavePoint();
        check(edit("After saving") && document.Dirty(), "an edit after saving");
        check(undo.Undo() && !document.Dirty(), "undone back to the save point");
        check(undo.Redo() && document.Dirty() && undo.Undo() && !document.Dirty(), "redone and undone");
        document.MarkTopologyChanged();
        check(undo.Redo() && undo.Undo() && document.Dirty(), "a change outside undo stays");
        document.MarkSaved();
        undo.MarkSavePoint();
        check(edit("Saved") && undo.Undo() && document.Dirty() == false && undo.Redo(), "saved");
        document.MarkSaved();
        undo.MarkSavePoint(); // after "Saved"
        check(undo.Undo() && document.Dirty() && edit("Instead") && undo.Undo() && document.Dirty(),
              "a new edit replaces the save point undone");
        undo.Clear(); // the steps write value
        // A Script created after saving (its layout rebuilt from the runtime
        // graph to show it: not a change), undone back to the save point.
        document.MarkSaved();
        undo.MarkSavePoint();
        const EditResult made = CreateScript(document, other->GetID(), "Save Point Script");
        check(made.ok && document.Session(made.created) && document.Dirty(), "a Script created after saving");
        check(undo.Undo() && !document.Dirty(), "the new Script undone back to the save point");
    }
    // Save Objects as, then Delete (not undoable).
    const std::string objects = (std::filesystem::temp_directory_path() / "SchematicLevelObjects.nmo").string();
    std::string saveError;
    check(SaveObjectsAs(document, {other->GetID()}, objects, &saveError), "save objects");
    auto *doomed = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Doomed Entity"));
    level->AddObject(doomed);
    EditResult doomedScript = CreateScript(document, doomed->GetID(), "");
    check(doomedScript.ok && document.Session(doomedScript.created), "doomed script");
    // The history names the objects: it is cleared once something is destroyed, kept when nothing is.
    check(!DestroyLevelObjects(document, {level->GetID()}).ok && undo.CanUndo(), "delete refused keeps undo");
    check(DestroyLevelObjects(document, {doomed->GetID()}).ok && !undo.CanUndo() &&
              !context->GetObject(doomedScript.created) && !document.FindScript(doomedScript.created) &&
              !document.ExistingSession(doomedScript.created),
          "delete object");
    std::cout << "Level Manager commands passed\n";
}

// Setups and managers: attribute types, curve points, hierarchy, arrays.
void SetupManagerCommands(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    CKLevel *level = context->GetCurrentLevel();
    CHECK(level);
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(100);
    auto check = [&](bool condition, const char *what) {
        if (!condition) {
            std::cerr << "Setup / manager commands: " << what << std::endl;
            Fail("the check written above", __FILE__, __LINE__);
        }
    };
    // Attribute Manager: a user category and type, then Remove All User Attributes.
    CKAttributeManager *attributes = context->GetAttributeManager();
    check(CreateAttributeCategory(document, "Schematic Test Category").ok, "create category");
    check(!CreateAttributeCategory(document, "Schematic Test Category").ok, "duplicate category refused");
    CKParameterManager *parameters = context->GetParameterManager();
    EditResult type = CreateAttributeType(document, "Schematic Test Attribute", CKPGUID_FLOAT, "Schematic Test Category");
    CKAttributeType created = static_cast<CKAttributeType>(type.created);
    check(type.ok && attributes->IsAttributeIndexValid(created) &&
              (attributes->GetAttributeFlags(created) & CK_ATTRIBUT_USER) &&
              std::string(attributes->GetAttributeCategory(created)) == "Schematic Test Category" &&
              attributes->GetAttributeParameterType(created) == parameters->ParameterGuidToType(CKPGUID_FLOAT),
          "create attribute type");
    auto *holder = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Attribute Holder"));
    level->AddObject(holder);
    check(AddAttribute(document, {holder->GetID()}, created).ok && holder->HasAttribute(created), "user attribute added");
    check(SetAttributeText(document, holder->GetID(), created, "2.5").ok, "user attribute value");
    // The Attribute Manager's menus (Dev.exe sub_55D080 / sub_55D200) and the
    // Setup's Attribute page (sub_4436E0).
    {
        const int category = attributes->GetCategoryByName((CKSTRING) "Schematic Test Category");
        check(category >= 0 && (attributes->GetCategoryFlags(category) & (CK_ATTRIBUT_CAN_MODIFY | CK_ATTRIBUT_CAN_DELETE)) ==
                                   (CK_ATTRIBUT_CAN_MODIFY | CK_ATTRIBUT_CAN_DELETE),
              "a user category may be renamed and deleted");
        check(RenameAttributeCategory(document, "Schematic Test Category", "Schematic Test Renamed").ok &&
                  std::string(attributes->GetAttributeCategory(created)) == "Schematic Test Renamed",
              "rename category");
        const EditResult second = CreateAttributeType(document, "Schematic Test Second", CKPGUID_INT, "");
        check(second.ok && !RenameAttributeType(document, created, "Schematic Test Second").ok, "a name used is refused");
        check(RenameAttributeType(document, created, "Schematic Test Renamed Attribute").ok &&
                  attributes->GetAttributeTypeByName((CKSTRING) "Schematic Test Renamed Attribute") == created,
              "rename attribute");
        auto *other = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Attribute Other"));
        level->AddObject(other);
        // Copied from the holder's Attribute page, pasted on the other one.
        CKParameterOut *value = holder->GetAttributeParameter(created);
        check(value && PasteAttributes(document, other->GetID(), {"Schematic Test Renamed Attribute"}, {value->GetID()}).ok &&
                  other->HasAttribute(created),
              "paste attributes");
        float pasted = 0.0f;
        if (other->GetAttributeParameter(created)) other->GetAttributeParameter(created)->GetValue(&pasted);
        check(pasted == 2.5f, "pasted value");
        check(undo.Undo() && !other->HasAttribute(created), "undo paste attributes");
        check(undo.Redo() && other->HasAttribute(created), "redo paste attributes");
        check(RemoveAttributeFromObjects(document, {holder->GetID(), other->GetID()}, created).ok &&
                  !holder->HasAttribute(created) && !other->HasAttribute(created),
              "remove attribute from objects");
        check(undo.Undo() && holder->HasAttribute(created) && other->HasAttribute(created), "undo remove from objects");
        check(RemoveAttributes(document, other->GetID(), {created}).ok && !other->HasAttribute(created), "remove attributes");
        check(undo.Undo() && other->HasAttribute(created), "undo remove attributes");
        check(DeleteAttributeCategory(document, "Schematic Test Renamed").ok &&
                  attributes->GetCategoryByName((CKSTRING) "Schematic Test Renamed") < 0 && attributes->IsAttributeIndexValid(created),
              "delete category keeps its types");
        check(DeleteAttributeType(document, created).ok && !attributes->IsAttributeIndexValid(created) &&
                  !holder->HasAttribute(created) && !undo.CanUndo(),
              "delete attribute type");
        // A type again for Remove All User Attributes below.
        type = CreateAttributeType(document, "Schematic Test Attribute", CKPGUID_FLOAT, "Schematic Test Category");
        created = static_cast<CKAttributeType>(type.created);
        check(type.ok && AddAttribute(document, {holder->GetID()}, created).ok, "attribute added again");
    }
    check(RemoveUserAttributes(document).ok && !undo.CanUndo() && !attributes->IsAttributeIndexValid(created) &&
              !holder->HasAttribute(created),
          "remove user attributes");
    int step = 0;
    RecordValue(document, "Step", step, 1);
    check(!RemoveUserAttributes(document).ok && undo.CanUndo(), "nothing to remove keeps undo");
    undo.Clear();

    // Curve points: Add / Insert / Delete with undo.
    CKCurve *curve = CKCurve::Cast(context->CreateObject(CKCID_CURVE, (CKSTRING) "Test Curve"));
    level->AddObject(curve);
    EditResult first = AddCurvePoint(document, curve->GetID(), -1);
    EditResult second = AddCurvePoint(document, curve->GetID(), -1);
    EditResult third = AddCurvePoint(document, curve->GetID(), -1);
    check(first.ok && second.ok && third.ok && curve->GetControlPointCount() == 3, "add points");
    EditResult inserted = AddCurvePoint(document, curve->GetID(), 0);
    check(inserted.ok && curve->GetControlPointCount() == 4 && curve->GetControlPoint(1)->GetID() == inserted.created,
          "insert point");
    VxVector a, b, m;
    curve->GetControlPoint(0)->GetPosition(&a);
    curve->GetControlPoint(2)->GetPosition(&b);
    curve->GetControlPoint(1)->GetPosition(&m);
    check(std::fabs(m.x - (a.x + b.x) / 2) < 1e-3f, "inserted between its neighbors");
    check(undo.Undo() && curve->GetControlPointCount() == 3 && document.IsDiscarded(inserted.created),
          "undo insert point");
    check(RemoveCurvePoint(document, curve->GetID(), 0).ok && curve->GetControlPointCount() == 2 &&
              curve->GetControlPoint(0)->GetID() == second.created && document.IsDiscarded(first.created),
          "delete point");
    check(undo.Undo() && curve->GetControlPointCount() == 3 && curve->GetControlPoint(0)->GetID() == first.created &&
              !document.IsDiscarded(first.created),
          "undo delete point");
    check(undo.Redo() && curve->GetControlPointCount() == 2, "redo delete point");
    check(undo.Undo(), "undo delete point again");
    // Several objects in one step: every point linear.
    std::vector<CK_ID> points;
    for (int i = 0; i < curve->GetControlPointCount(); ++i) points.push_back(curve->GetControlPoint(i)->GetID());
    check(EditObjectsState(document, points, "Linear Curve", [](CKObject *o) { CKCurvePoint::Cast(o)->SetLinear(TRUE); }).ok,
          "linear curve");
    check(curve->GetControlPoint(0)->IsLinear() && curve->GetControlPoint(2)->IsLinear(), "points linear");
    check(undo.Undo() && !curve->GetControlPoint(0)->IsLinear() && !curve->GetControlPoint(2)->IsLinear(),
          "undo linear curve");
    // Hierarchy Manager: reparenting with undo, cycles refused.
    auto *parent = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Hierarchy Parent"));
    auto *child = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Hierarchy Child"));
    level->AddObject(parent);
    level->AddObject(child);
    check(SetHierarchyParent(document, child->GetID(), parent->GetID()).ok && child->GetParent() == parent, "set parent");
    check(!SetHierarchyParent(document, parent->GetID(), child->GetID()).ok, "cycle refused");
    check(undo.Undo() && !child->GetParent(), "undo set parent");
    // Dev.exe sub_565DD0: entities of different depths are not moved together;
    // a character's body parts keep their hierarchy.
    {
        auto *third = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Hierarchy Third"));
        level->AddObject(third);
        check(SetHierarchyParent(document, child->GetID(), parent->GetID()).ok, "child under parent");
        const std::vector<EditResult> mixed = SetHierarchyParents(document, {child->GetID(), parent->GetID()}, third->GetID());
        check(mixed.size() == 2 && !mixed[0].ok && !mixed[1].ok && child->GetParent() == parent && !parent->GetParent(),
              "different depths refused");
        check(undo.Undo() && !child->GetParent(), "undo child under parent");
        auto *part = CKBodyPart::Cast(context->CreateObject(CKCID_BODYPART, (CKSTRING) "Hierarchy Body Part"));
        level->AddObject(part);
        check(part && !SetHierarchyParent(document, part->GetID(), parent->GetID()).ok && !part->GetParent(),
              "body part refused");
        context->DestroyObject(part);
        context->DestroyObject(third);
    }
    auto *sprite = CK2dEntity::Cast(context->CreateObject(CKCID_2DENTITY, (CKSTRING) "Hierarchy 2D"));
    level->AddObject(sprite);
    const bool back = sprite->IsBackground() != FALSE;
    check(SetHierarchyParent(document, sprite->GetID(), 0, back ? 0 : 1).ok && (sprite->IsBackground() != FALSE) != back,
          "2D root change");
    check(undo.Undo() && (sprite->IsBackground() != FALSE) == back, "undo 2D root change");
    // Render Priority (the Hierarchy Manager's column), 3D and 2D entities in one step.
    const int parentZ = parent->GetZOrder(), spriteZ = sprite->GetZOrder();
    // An object state read back does not set a 3D entity's priority back:
    // SetRenderPriority keeps the values.
    check(SetRenderPriority(document, {parent->GetID(), sprite->GetID()}, 7).ok && parent->GetZOrder() == 7 &&
              sprite->GetZOrder() == 7,
          "set render priority");
    check(undo.Undo() && parent->GetZOrder() == parentZ && sprite->GetZOrder() == spriteZ, "undo render priority");
    check(undo.Redo() && parent->GetZOrder() == 7 && sprite->GetZOrder() == 7, "redo render priority");
    check(undo.Undo(), "undo render priority again");
    // Array Setup: columns, rows and cells are object state edits.
    CKDataArray *array = CKDataArray::Cast(context->CreateObject(CKCID_DATAARRAY, (CKSTRING) "Test Array"));
    level->AddObject(array);
    check(EditObjectState(document, array->GetID(), "Add Column", [](CKObject *o) {
              CKDataArray::Cast(o)->InsertColumn(-1, CKARRAYTYPE_INT, (char *) "Count");
          }).ok && array->GetColumnCount() == 1,
          "add column");
    check(EditObjectState(document, array->GetID(), "Add Row", [](CKObject *o) { CKDataArray::Cast(o)->AddRow(); }).ok &&
              array->GetRowCount() == 1,
          "add row");
    check(EditObjectState(document, array->GetID(), "Edit Array", [](CKObject *o) {
              CKDataArray::Cast(o)->SetElementStringValue(0, 0, (char *) "42");
          }).ok,
          "edit cell");
    int value = 0;
    array->GetElementValue(0, 0, &value);
    check(value == 42, "cell value");
    check(undo.Undo(), "undo edit cell");
    array->GetElementValue(0, 0, &value);
    check(value != 42 && array->GetRowCount() == 1, "cell undone");
    check(undo.Undo() && array->GetRowCount() == 0, "undo add row");
    check(undo.Undo() && array->GetColumnCount() == 0, "undo add column");
    check(undo.Redo() && array->GetColumnCount() == 1 && std::string(array->GetColumnName(0)) == "Count",
          "redo add column");
    std::cout << "Setup / manager commands passed\n";
}

// Create menu (Dev creation tools) and Level / Scene Setup.
void CreateAndSceneCommands(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    CKLevel *level = context->GetCurrentLevel();
    CHECK(level);
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(100);
    auto check = [&](bool condition, const char *what) {
        if (!condition) {
            std::cerr << "Create / scene commands: " << what << std::endl;
            Fail("the check written above", __FILE__, __LINE__);
        }
    };
    CKScene *levelScene = level->GetLevelScene();
    const bool namesNumbered = true;
    // Every creation tool: the object joins the level, undo puts it in the graveyard.
    const LayoutObject kinds[] = {LayoutObject::Camera, LayoutObject::Light, LayoutObject::Frame3D,
                                  LayoutObject::Curve, LayoutObject::Grid, LayoutObject::Frame2D,
                                  LayoutObject::Material, LayoutObject::Texture, LayoutObject::Portal};
    const CK_CLASSID classes[] = {CKCID_TARGETCAMERA, CKCID_TARGETLIGHT, CKCID_3DENTITY, CKCID_CURVE, CKCID_GRID,
                                  CKCID_2DENTITY, CKCID_MATERIAL, CKCID_TEXTURE, CKCID_3DENTITY};
    const char *names[] = {"New Camera", "New Light", "New 3D Frame", "New Curve", "New Grid",
                           "New 2D Frame", "New Material", "New Texture", "New Portal"};
    for (int i = 0; i < 9; ++i) {
        EditResult made = CreateLayoutObject(document, kinds[i]);
        if (!made.ok) std::cerr << names[i] << ": " << made.message << std::endl;
        CKObject *object = context->GetObject(made.created);
        check(made.ok && object && object->GetClassID() == classes[i], "create");
        const std::string name = object->GetName() ? object->GetName() : "";
        check(name.rfind(names[i], 0) == 0, "default name");
        check(levelScene->IsObjectHere(object), "created object in the level");
        check(undo.Undo() && document.IsDiscarded(made.created) && !levelScene->IsObjectHere(object),
              "undo create");
        check(undo.Redo() && !document.IsDiscarded(made.created) && levelScene->IsObjectHere(object),
              "redo create");
    }
    // Defaults read from Dev.exe.
    EditResult light = CreateLayoutObject(document, LayoutObject::Light);
    CKLight *made = CKLight::Cast(context->GetObject(light.created));
    check(made && made->GetType() == VX_LIGHTPOINT && made->GetRange() == 200.0f, "light defaults");
    if (namesNumbered) check(std::string(made->GetName()) == "New Light.0001", "numbered name");
    EditResult texture = CreateLayoutObject(document, LayoutObject::Texture);
    CKTexture *createdTexture = CKTexture::Cast(context->GetObject(texture.created));
    check(createdTexture && createdTexture->GetWidth() == 128 && createdTexture->GetHeight() == 128, "texture size");
    EditResult grid = CreateLayoutObject(document, LayoutObject::Grid);
    CKGrid *createdGrid = CKGrid::Cast(context->GetObject(grid.created));
    // The layer types are the grid manager's (Grids.dll; Ballance's runtime has none).
    const bool gridManager = context->GetManagerByGuid(GRID_MANAGER_GUID) != nullptr;
    check(createdGrid && createdGrid->GetWidth() == 10 && createdGrid->GetLength() == 10 &&
              createdGrid->GetLayerCount() == (gridManager ? 1 : 0),
          "grid defaults");
    // CKGrid's swapped vtable pairs (GridCompat.h): classification and layer by type.
    if (gridManager) {
        CKAttributeManager *attributes = context->GetAttributeManager();
        const CKAttributeType defaultClass = attributes->GetAttributeTypeByName((CKSTRING) "- Default Grid Class -");
        check(defaultClass >= 0 && createdGrid->HasAttribute(defaultClass), "default grid classification");
        check(GridRemoveClassification(createdGrid, defaultClass) == CK_OK && !createdGrid->HasAttribute(defaultClass),
              "remove classification by type");
        check(GridAddClassification(createdGrid, defaultClass) == CK_OK && createdGrid->HasAttribute(defaultClass),
              "add classification by type");
        check(GridGetLayer(createdGrid, 1) == createdGrid->GetLayerByIndex(0), "get layer by type");
        check(GridRemoveLayer(createdGrid, 1) == CK_OK && createdGrid->GetLayerCount() == 0, "remove layer by type");
        check(GridAddLayer(createdGrid, 1) && createdGrid->GetLayerCount() == 1 && GridGetLayer(createdGrid, 1),
              "add layer by type");
    }
    EditResult frame = CreateLayoutObject(document, LayoutObject::Frame3D);
    CK3dEntity *createdFrame = CK3dEntity::Cast(context->GetObject(frame.created));
    check(createdFrame && (createdFrame->GetFlags() & CK_3DENTITY_FRAME), "frame flag");
    EditResult portal = CreateLayoutObject(document, LayoutObject::Portal, frame.created);
    CK3dEntity *createdPortal = CK3dEntity::Cast(context->GetObject(portal.created));
    check(createdPortal && (createdPortal->GetFlags() & CK_3DENTITY_PORTAL) &&
              std::string(createdPortal->GetName()) == std::string(createdFrame->GetName()) + " Portal",
          "portal around the selection");
    // A camera of reference: the new camera copies it (and its target).
    CKScene *scene = context->GetCurrentScene() ? context->GetCurrentScene() : levelScene;
    EditResult first = CreateLayoutObject(document, LayoutObject::Camera);
    CKCamera *reference = CKCamera::Cast(context->GetObject(first.created));
    reference->SetFov(1.0f);
    scene->SetStartingCamera(reference);
    EditResult second = CreateLayoutObject(document, LayoutObject::Camera);
    CKCamera *copy = CKCamera::Cast(context->GetObject(second.created));
    check(copy && std::fabs(copy->GetFov() - 1.0f) < 1e-5f, "camera copies the reference");
    scene->SetStartingCamera(nullptr);
    // Level / Scene Setup: environment settings with undo.
    const SceneEnvironment before = ReadSceneEnvironment(levelScene);
    SceneEnvironment fog = before;
    fog.fogMode = 3;
    fog.fogStart = 5.0f;
    fog.fogEnd = 50.0f;
    fog.background = 0xFF336699;
    fog.startingCamera = second.created;
    fog.backgroundTexture = texture.created;
    fog.useAtStart = !before.useAtStart;
    check(SetSceneEnvironment(document, levelScene->GetID(), fog, "Fog").ok, "scene settings");
    check(levelScene->GetFogMode() == VXFOG_LINEAR && levelScene->GetFogEnd() == 50.0f &&
              levelScene->GetBackgroundColor() == 0xFF336699 && levelScene->GetStartingCamera() == copy &&
              levelScene->GetBackgroundTexture() == createdTexture,
          "scene settings applied");
    check(!SetSceneEnvironment(document, levelScene->GetID(), fog, "Fog").ok, "unchanged settings refused");
    check(undo.Undo() && levelScene->GetFogMode() == before.fogMode && !levelScene->GetStartingCamera() &&
              levelScene->GetBackgroundColor() == before.background,
          "undo scene settings");
    check(undo.Redo() && levelScene->GetFogEnd() == 50.0f, "redo scene settings");
    check(undo.Undo(), "undo scene settings again");
    // 3D Sprite Setup: mode, size, offset, UV mapping and material are object state.
    CKSprite3D *sprite = CKSprite3D::Cast(context->CreateObject(CKCID_SPRITE3D, (CKSTRING) "Test Sprite3D"));
    level->AddObject(sprite);
    CKMaterial *spriteMaterial = CKMaterial::Cast(context->GetObject(CreateLayoutObject(document, LayoutObject::Material).created));
    const VXSPRITE3D_TYPE mode = sprite->GetMode();
    check(EditObjectState(document, sprite->GetID(), "Sprite", [&](CKObject *o) {
              CKSprite3D *s3 = CKSprite3D::Cast(o);
              s3->SetMode(mode == VXSPRITE3D_ORIENTABLE ? VXSPRITE3D_YROTATE : VXSPRITE3D_ORIENTABLE);
              Vx2DVector size(3.0f, 4.0f), offset(0.5f, 0.25f);
              s3->SetSize(size);
              s3->SetOffset(offset);
              VxRect uv(0.0f, 0.0f, 0.5f, 0.5f);
              s3->SetUVMapping(uv);
              s3->SetMaterial(spriteMaterial);
          }).ok,
          "sprite3d edit");
    Vx2DVector size;
    sprite->GetSize(size);
    check(size.x == 3.0f && sprite->GetMode() != mode && sprite->GetMaterial() == spriteMaterial, "sprite3d edited");
    check(undo.Undo() && sprite->GetMode() == mode && sprite->GetMaterial() != spriteMaterial, "undo sprite3d edit");
    sprite->GetSize(size);
    check(size.x != 3.0f, "sprite3d size undone");
    check(undo.Redo(), "redo sprite3d edit");
    sprite->GetSize(size);
    VxRect uv;
    sprite->GetUVMapping(uv);
    check(size.y == 4.0f && uv.right == 0.5f, "sprite3d edit redone");
    // 3D Frame Setup: a local position is relative to the parent.
    CK3dEntity *child = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Local Frame"));
    level->AddObject(child);
    child->SetParent(createdFrame);
    VxVector parentAt(10.0f, 0.0f, 0.0f), local(1.0f, 2.0f, 3.0f), world, back;
    createdFrame->SetPosition(&parentAt);
    check(EditObjectState(document, child->GetID(), "Set Position", [&](CKObject *o) {
              CK3dEntity::Cast(o)->SetPosition(&local, createdFrame);
          }).ok,
          "local position");
    child->GetPosition(&back, createdFrame);
    VxVector expected;
    createdFrame->Transform(&expected, &local);
    child->GetPosition(&world);
    check(std::fabs(back.y - 2.0f) < 1e-3f && std::fabs(world.x - expected.x) < 1e-3f && std::fabs(world.z - expected.z) < 1e-3f,
          "local position set");
    check(undo.Undo(), "undo local position");
    child->GetPosition(&back, createdFrame);
    check(std::fabs(back.y - 2.0f) > 1e-3f, "local position undone");
    // 2D Sprite Setup: an image slot loaded from a file, color key, cropping
    // and Stick flags are object state edits.
    CKSprite *sprite2d = CKSprite::Cast(context->CreateObject(CKCID_SPRITE, (CKSTRING) "Test Sprite"));
    level->AddObject(sprite2d);
    check(sprite2d->Create(4, 4, 32, 0) != FALSE && sprite2d->GetSlotCount() == 1, "sprite create");
    const std::string bmp = (std::filesystem::temp_directory_path() / "SchematicSpriteSlot.bmp").string();
    {
        // A 4 x 4, 32-bit BMP (bottom-up rows).
        std::vector<unsigned char> file(54 + 64, 0);
        auto put32 = [&](size_t at, unsigned value) {
            for (int k = 0; k < 4; ++k) file[at + k] = static_cast<unsigned char>(value >> (8 * k));
        };
        file[0] = 'B';
        file[1] = 'M';
        put32(2, static_cast<unsigned>(file.size()));
        put32(10, 54);
        put32(14, 40);
        put32(18, 4);
        put32(22, 4);
        file[26] = 1;
        file[28] = 32;
        put32(34, 64);
        for (size_t p = 54; p < file.size(); p += 4) {
            file[p] = 0x20;
            file[p + 1] = 0x80;
            file[p + 2] = 0xF0;
            file[p + 3] = 0xFF;
        }
        FILE *out = std::fopen(bmp.c_str(), "wb");
        check(out != nullptr, "write sprite bmp");
        std::fwrite(file.data(), 1, file.size(), out);
        std::fclose(out);
    }
    bool loaded = false;
    check(EditObjectState(document, sprite2d->GetID(), "Add Texture", [&](CKObject *o) {
              CKSprite *s = CKSprite::Cast(o);
              s->SetSlotCount(2);
              loaded = s->LoadImage(const_cast<CKSTRING>(bmp.c_str()), 1) != FALSE;
              if (loaded) s->SetCurrentSlot(1);
          }).ok &&
              loaded && sprite2d->GetSlotCount() == 2 && sprite2d->GetCurrentSlot() == 1,
          "sprite add texture");
    check(EditObjectState(document, sprite2d->GetID(), "Sprite", [](CKObject *o) {
              CKSprite *s = CKSprite::Cast(o);
              s->SetTransparent(TRUE);
              s->SetTransparentColor(0xFF00FF00);
              s->UseSourceRect(TRUE);
              VxRect crop(0.0f, 0.0f, 2.0f, 2.0f);
              s->SetSourceRect(crop);
              s->ModifyFlags(CK_2DENTITY_STICKLEFT | CK_2DENTITY_STICKBOTTOM, 0);
          }).ok,
          "sprite edit");
    check(sprite2d->IsTransparent() && (sprite2d->GetFlags() & CK_2DENTITY_STICKBOTTOM) && sprite2d->IsUsingSourceRect(),
          "sprite edited");
    check(undo.Undo() && !sprite2d->IsTransparent() && !(sprite2d->GetFlags() & CK_2DENTITY_STICKBOTTOM),
          "undo sprite edit");
    check(undo.Undo() && sprite2d->GetSlotCount() == 1 && sprite2d->GetCurrentSlot() == 0, "undo sprite add texture");
    check(undo.Redo() && sprite2d->GetSlotCount() == 2 && sprite2d->GetCurrentSlot() == 1, "redo sprite add texture");
    // The same for a texture's Add Slot (Texture Setup).
    const int textureSlots = createdTexture->GetSlotCount();
    check(EditObjectState(document, createdTexture->GetID(), "Add Slot", [textureSlots](CKObject *o) {
              CKTexture::Cast(o)->SetSlotCount(textureSlots + 1);
              CKTexture::Cast(o)->SetCurrentSlot(textureSlots);
          }).ok,
          "texture add slot");
    check(undo.Undo() && createdTexture->GetSlotCount() == textureSlots, "undo texture add slot");
    check(undo.Redo() && createdTexture->GetSlotCount() == textureSlots + 1, "redo texture add slot");
    std::filesystem::remove(bmp);
    // Place Setup links (SetPlaceLinks): attach, change the portal, remove.
    CKPlace *placeA = CKPlace::Cast(context->GetObject(CreatePlace(document, "Place A", {}).created));
    CKPlace *placeB = CKPlace::Cast(context->GetObject(CreatePlace(document, "Place B", {}).created));
    CK3dEntity *linkPortal = CK3dEntity::Cast(context->GetObject(CreateLayoutObject(document, LayoutObject::Portal).created));
    check(placeA && placeB && linkPortal && placeA->GetPortalCount() == 0, "places for links");
    check(SetPlaceLinks(document, placeA->GetID(), {{placeB->GetID(), 0}}, "Attach New Place").ok &&
              placeA->GetPortalCount() == 1,
          "attach new place");
    check(SetPlaceLinks(document, placeA->GetID(), {{placeB->GetID(), linkPortal->GetID()}}, "Through Portal").ok,
          "through portal");
    CK3dEntity *through = nullptr;
    check(placeA->GetPortalCount() == 1 && placeA->GetPortal(0, &through) == placeB && through == linkPortal, "portal set");
    check(!SetPlaceLinks(document, placeA->GetID(), PlaceLinksOf(placeA), "Same").ok, "unchanged links refused");
    check(undo.Undo() && placeA->GetPortal(0, &through) == placeB && through == nullptr, "undo through portal");
    check(undo.Undo() && placeA->GetPortalCount() == 0, "undo attach new place");
    check(undo.Redo() && undo.Redo() && placeA->GetPortal(0, &through) == placeB && through == linkPortal,
          "redo place links");
    check(SetPlaceLinks(document, placeA->GetID(), {}, "Remove Place").ok && placeA->GetPortalCount() == 0, "remove place");
    check(undo.Undo() && placeA->GetPortalCount() == 1, "undo remove place");
    // Automatic Portal (Dev.exe sub_471A60): a portal named after the places,
    // placeB seen through it; one undo step.
    {
        // (placeA's link made placeB see placeA too: CKPlace::AddPortal links both.)
        const int links = placeB->GetPortalCount();
        const EditResult automatic = CreateAutomaticPortal(document, placeB->GetID(), placeA->GetID());
        CK3dEntity *made = CK3dEntity::Cast(context->GetObject(automatic.created));
        CK3dEntity *seen = nullptr;
        check(automatic.ok && made && std::string(made->GetName()) == "Portal Place B-Place A" &&
                  (made->GetFlags() & CK_3DENTITY_PORTAL) && placeB->GetPortalCount() == links + 1 &&
                  placeB->GetPortal(links, &seen) == placeA && seen == made,
              "automatic portal");
        check(undo.Undo() && placeB->GetPortalCount() == links && document.IsDiscarded(automatic.created),
              "undo automatic portal");
        check(undo.Redo() && placeB->GetPortalCount() == links + 1 && !document.IsDiscarded(automatic.created),
              "redo automatic portal");
        check(!CreateAutomaticPortal(document, placeA->GetID(), placeA->GetID()).ok, "a place with itself refused");
        check(undo.Undo() && placeB->GetPortalCount() == links, "automatic portal undone again");
    }
    // Level Manager scene sections: flags change in the row's scene, not the current one.
    CKScene *other = CKScene::Cast(context->GetObject(CreateScene(document, "Flag Scene", {placeA->GetID()}).created));
    check(other && other->IsObjectHere(placeA), "flag scene");
    const CKDWORD levelFlags = levelScene->GetObjectFlags(placeA), otherFlags = other->GetObjectFlags(placeA);
    check(ModifySceneFlags(document, {placeA->GetID()}, CK_SCENEOBJECT_START_DEACTIVATE,
                           CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_LEAVE, "Deactivate", other->GetID())
              .ok,
          "flags in a scene");
    check((other->GetObjectFlags(placeA) & CK_SCENEOBJECT_START_DEACTIVATE) && levelScene->GetObjectFlags(placeA) == levelFlags,
          "only that scene changed");
    check(undo.Undo() && other->GetObjectFlags(placeA) == otherFlags, "undo flags in a scene");
    // Sprite Text Setup: the font is read back from the object's state.
    CKSpriteText *spriteText = CKSpriteText::Cast(context->CreateObject(CKCID_SPRITETEXT, (CKSTRING) "Test Sprite Text"));
    level->AddObject(spriteText);
    check(spriteText && spriteText->Create(64, 16, 32, 0) != FALSE, "sprite text create");
    spriteText->SetFont((CKSTRING) "Arial", 20, 700, TRUE, FALSE);
    spriteText->SetText((CKSTRING) "Before");
    SpriteTextFont font;
    check(ReadSpriteTextFont(spriteText, &font) && font.face == "Arial" && font.size == 20 && font.weight == 700 && font.italic &&
              !font.underline,
          "sprite text font read");
    const int fontSize = font.size;
    check(EditObjectState(document, spriteText->GetID(), "Sprite Text", [](CKObject *o) {
              CKSpriteText *t = CKSpriteText::Cast(o);
              t->SetText((CKSTRING) "After");
              t->SetFont((CKSTRING) "Courier New", 12, 400, FALSE, TRUE);
              t->SetAlign(static_cast<CKSPRITETEXT_ALIGNMENT>(CKSPRITETEXT_LEFT | CKSPRITETEXT_TOP));
          }).ok &&
              ReadSpriteTextFont(spriteText, &font) && font.face == "Courier New" && font.underline &&
              std::string(spriteText->GetText()) == "After",
          "sprite text edit");
    check(undo.Undo() && ReadSpriteTextFont(spriteText, &font) && font.face == "Arial" && font.size == fontSize &&
              font.italic && std::string(spriteText->GetText()) == "Before",
          "undo sprite text edit");
    // Alignment lives in the runtime's low flag word, which GetAlign does
    // not return and CKSaveObjectState does not keep. All ten choices must
    // change the property, survive redraw and undo/redo, and preserve text.
    const auto &alignment = properties::SpriteTextAlignment;
    alignment.set(spriteText, CKSPRITETEXT_LEFT | CKSPRITETEXT_BOTTOM);
    const int initialAlign = alignment.get(spriteText);
    std::vector<int> alignments = {CKSPRITETEXT_CENTER};
    for (int h : {CKSPRITETEXT_LEFT, CKSPRITETEXT_HCENTER, CKSPRITETEXT_RIGHT})
        for (int v : {CKSPRITETEXT_TOP, CKSPRITETEXT_VCENTER, CKSPRITETEXT_BOTTOM})
            alignments.push_back(h | v);
    for (int wanted : alignments) {
        const int before = alignment.get(spriteText);
        check(before != wanted && EditObjectState(document, spriteText->GetID(), alignment.label,
                  [&](CKObject *o) { alignment.set(o, wanted); }).ok && alignment.get(spriteText) == wanted,
              "sprite text alignment edit");
        spriteText->SetText((CKSTRING) "Redraw");
        check(alignment.get(spriteText) == wanted, "alignment after redraw");
        check(undo.Undo() && alignment.get(spriteText) == before && std::string(spriteText->GetText()) == "Before",
              "undo sprite text alignment");
        check(undo.Redo() && alignment.get(spriteText) == wanted && std::string(spriteText->GetText()) == "Before",
              "redo sprite text alignment");
        check(!EditObjectState(document, spriteText->GetID(), alignment.label,
                  [&](CKObject *o) { alignment.set(o, wanted); }).ok && alignment.get(spriteText) == wanted,
              "unchanged sprite text alignment");
    }
    for (size_t i = 0; i < alignments.size(); ++i) check(undo.Undo(), "undo all sprite text alignments");
    check(alignment.get(spriteText) == initialAlign, "restore initial sprite text alignment");
    // Animation Setup: length, frame rate, flags and modes of a keyed
    // animation; its object animations are in its state.
    CKKeyedAnimation *keyed = CKKeyedAnimation::Cast(context->CreateObject(CKCID_KEYEDANIMATION, (CKSTRING) "Test Keyed"));
    CKObjectAnimation *part = CKObjectAnimation::Cast(context->CreateObject(CKCID_OBJECTANIMATION, (CKSTRING) "Test Part"));
    check(keyed && part && keyed->AddAnimation(part) == CK_OK && keyed->GetAnimationCount() == 1, "keyed animation");
    keyed->SetLength(30.0f);
    keyed->LinkToFrameRate(FALSE, 30.0f);
    const bool interruptible = keyed->CanBeInterrupt() != 0; // CKANIMATION_CANBEBREAK is set on new animations
    check(EditObjectState(document, keyed->GetID(), "Animation", [](CKObject *o) {
              CKAnimation *a = CKAnimation::Cast(o);
              a->SetLength(60.0f);
              a->LinkToFrameRate(TRUE, 25.0f);
              a->SetCanBeInterrupt(!a->CanBeInterrupt());
              a->SetTransitionMode(static_cast<CK_ANIMATION_TRANSITION_MODE>(CK_TRANSITION_WARPBEST | CK_TRANSITION_USEVELOCITY));
              a->SetSecondaryAnimationMode(CKSECONDARYANIMATION_LOOP);
          }).ok &&
              keyed->GetLength() == 60.0f && keyed->IsLinkedToFrameRate() && keyed->GetLinkedFrameRate() == 25.0f &&
              keyed->GetTransitionMode() == (CK_TRANSITION_WARPBEST | CK_TRANSITION_USEVELOCITY) &&
              keyed->GetSecondaryAnimationMode() == CKSECONDARYANIMATION_LOOP,
          "animation edit");
    check(undo.Undo() && keyed->GetLength() == 30.0f && !keyed->IsLinkedToFrameRate() &&
              (keyed->CanBeInterrupt() != 0) == interruptible &&
              keyed->GetTransitionMode() == 0 && keyed->GetAnimationCount() == 1 && keyed->GetAnimation(0) == part,
          "undo animation edit");
    check(EditObjectState(document, part->GetID(), "Ignore", [](CKObject *o) {
              CKObjectAnimation::Cast(o)->SetFlags(CKObjectAnimation::Cast(o)->GetFlags() | CK_OBJECTANIMATION_IGNOREPOS);
          }).ok &&
              (part->GetFlags() & CK_OBJECTANIMATION_IGNOREPOS),
          "object animation flags");
    check(undo.Undo() && !(part->GetFlags() & CK_OBJECTANIMATION_IGNOREPOS), "undo object animation flags");
    // Mesh Setup of a patch mesh: Iterations.
    CKPatchMesh *patch = CKPatchMesh::Cast(context->CreateObject(CKCID_PATCHMESH, (CKSTRING) "Test Patch Mesh"));
    check(patch != nullptr, "patch mesh");
    const int iterations = patch->GetIterationCount();
    check(EditObjectState(document, patch->GetID(), "Iterations", [iterations](CKObject *o) {
              CKPatchMesh::Cast(o)->SetIterationCount(iterations + 2);
          }).ok &&
              patch->GetIterationCount() == iterations + 2,
          "patch mesh iterations");
    check(undo.Undo() && patch->GetIterationCount() == iterations, "undo patch mesh iterations");
    // Midi Sound Setup: the file name and save options.
    CKMidiSound *midi = CKMidiSound::Cast(context->CreateObject(CKCID_MIDISOUND, (CKSTRING) "Test Midi"));
    check(midi != nullptr, "midi sound");
    check(EditObjectState(document, midi->GetID(), "Change File", [](CKObject *o) {
              CKMidiSound::Cast(o)->SetSoundFileName((CKSTRING) "Schematic Test.mid");
              CKMidiSound::Cast(o)->SetSaveOptions(CKSOUND_INCLUDEORIGINALFILE);
          }).ok &&
              midi->GetSoundFileName() && std::string(midi->GetSoundFileName()) == "Schematic Test.mid",
          "midi file name");
    check(undo.Undo() && (!midi->GetSoundFileName() || !*midi->GetSoundFileName()) &&
              midi->GetSaveOptions() != CKSOUND_INCLUDEORIGINALFILE,
          "undo midi file name");
    // The Setups of the classes that are not behavioral objects: their
    // edits are object states too.
    CKBodyPart *upper = CKBodyPart::Cast(context->CreateObject(CKCID_BODYPART, (CKSTRING) "Test Upper Arm"));
    CKBodyPart *lower = CKBodyPart::Cast(context->CreateObject(CKCID_BODYPART, (CKSTRING) "Test Forearm"));
    CKKinematicChain *chain =
        CKKinematicChain::Cast(context->CreateObject(CKCID_KINEMATICCHAIN, (CKSTRING) "Test Chain"));
    check(upper && lower && chain && lower->SetParent(upper), "kinematic chain");
    check(EditObjectState(document, chain->GetID(), "Effectors", [upper, lower](CKObject *o) {
              CKKinematicChain::Cast(o)->SetStartEffector(upper);
              CKKinematicChain::Cast(o)->SetEndEffector(lower);
          }).ok &&
              chain->GetStartEffector() == upper && chain->GetEndEffector() == lower && chain->GetChainBodyCount() == 2,
          "kinematic chain effectors");
    check(undo.Undo() && !chain->GetStartEffector() && !chain->GetEndEffector(), "undo kinematic chain effectors");
    check(undo.Redo() && chain->GetStartEffector() == upper && chain->GetEndEffector() == lower,
          "redo kinematic chain effectors");
    CKSynchroObject *synchro = CKSynchroObject::Cast(context->CreateObject(CKCID_SYNCHRO, (CKSTRING) "Test Synchro"));
    check(synchro != nullptr, "synchro");
    const int waiters = synchro->GetRendezVousNumberOfWaiters();
    check(EditObjectState(document, synchro->GetID(), "Number Of Users", [waiters](CKObject *o) {
              CKSynchroObject::Cast(o)->SetRendezVousNumberOfWaiters(waiters + 3);
          }).ok &&
              synchro->GetRendezVousNumberOfWaiters() == waiters + 3,
          "synchro users");
    check(undo.Undo() && synchro->GetRendezVousNumberOfWaiters() == waiters, "undo synchro users");
    CKStateObject *state = CKStateObject::Cast(context->CreateObject(CKCID_STATE, (CKSTRING) "Test State"));
    check(state && !state->IsStateActive(), "state");
    check(EditObjectState(document, state->GetID(), "Active", [](CKObject *o) { CKStateObject::Cast(o)->EnterState(); }).ok &&
              state->IsStateActive(),
          "state active");
    check(undo.Undo() && !state->IsStateActive(), "undo state active");
    if (CKLayer *layer = createdGrid ? createdGrid->GetLayerByIndex(0) : nullptr) {
        const bool visible = layer->IsVisible() != 0;
        check(EditObjectState(document, layer->GetID(), "Layer Visibility",
                              [visible](CKObject *o) { CKLayer::Cast(o)->SetVisible(!visible); }).ok &&
                  (layer->IsVisible() != 0) == !visible,
              "layer visibility");
        check(undo.Undo() && (layer->IsVisible() != 0) == visible, "undo layer visibility");
    } else if (gridManager) {
        check(false, "grid layer");
    }
    std::cout << "Create / scene commands passed\n";
}

namespace {

#if defined(_M_IX86)
// A fault in the middle of x87 code, two values loaded (CK2_3D 2.1 is x87
// code: RCKMesh::PickLines, under CKRenderContext::Pick, faults so).
__declspec(noinline) void FaultWithX87Values() {
    __asm {
        fld1
        fld1
        xor eax, eax
        mov dword ptr [eax], eax
    }
}

// The registers in use: the tag word's entries not 11 (empty).
int X87StackDepth() {
    unsigned char environment[28];
    __asm {
        fnstenv environment
        fldenv environment
    }
    const unsigned tags = environment[8] | (environment[9] << 8);
    int depth = 0;
    for (int i = 0; i < 8; ++i) depth += ((tags >> (2 * i)) & 3) != 3;
    return depth;
}
#endif

} // namespace

// GuardedCall catching a fault in the runtime's x87 code: the values it had
// loaded stay on the FPU's register stack (the __except block does not pop
// them). A few such faults fill it, and from then on every x87 load
// overflows to NaN: every float CK returns (in ST0), and the sizes and
// indices it computes from floats, which corrupted the heap after
// CKRenderContext::Pick had faulted (2.1, OPEN-ISSUES). GuardedCall empties
// the stack and keeps the control word.
void GuardedCalls(CkRuntimeHost &host) {
#if defined(_M_IX86)
    CKContext *context = host.Context();
    CKCamera *camera = CKCamera::Cast(context->CreateObject(CKCID_CAMERA, (CKSTRING) "Guarded Call Camera"));
    CHECK(camera);
    camera->SetFov(0.5f);
    unsigned short before = 0, after = 0;
    __asm fnstcw before
    // The first logged: the exception, its module and offset, the address written.
    for (int i = 0; i < 6; ++i)
        CHECK(!GuardedCall([] { FaultWithX87Values(); }, i == 0 ? &host : nullptr, "a fault in x87 code"));
    __asm fnstcw after
    CHECK(X87StackDepth() == 0);
    CHECK(after == before);
    // CK2_3D's RCKCamera::GetFov loads the field on the x87 stack.
    const float fov = camera->GetFov();
    CHECK(fov == 0.5f);
    context->DestroyObject(camera);
    std::cout << "Guarded calls passed\n";
#else
    CHECK(!GuardedCall([] { throw std::runtime_error("Guarded exception"); }, &host, "C++ exception"));
#endif
}

// CK2's ClearAll activates the managers kept inactive, one refused at
// registration then taking the place of the active manager with its GUID
// (TT_ParticleSystems_RT.dll's Particle Manager has ParticleSystems.dll's,
// whose Building Blocks then corrupt the heap): CkRuntimeHost::ClearAll keeps
// the active managers.
void ManagersAfterClearAll(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    std::vector<CKBaseManager *> active;
    for (int i = 0; i < context->GetManagerCount(); ++i)
        if (CKBaseManager *manager = context->GetManager(i)) active.push_back(manager);
    for (int round = 0; round < 3; ++round) {
        host.ClearAll();
        for (CKBaseManager *manager : active)
            if (context->GetManagerByGuid(manager->GetGuid()) != manager) {
                std::cerr << "Managers after ClearAll: " << (manager->GetName() ? manager->GetName() : "?")
                          << " replaced" << std::endl;
                Fail("the check written above", __FILE__, __LINE__);
            }
    }
    std::cout << "Managers after ClearAll passed (" << active.size() << " managers)\n";
}

void DebuggerBudget(CkRuntimeHost &host) {
    class SlowFrames : public CKBaseManager {
    public:
        int calls = 0;
        explicit SlowFrames(CKContext *context)
            : CKBaseManager(context, CKGUID(0x61b23184, 0x72c41652), "Debugger budget regression") {}
        CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PreProcess; }
        CKERROR PreProcess() override {
            ++calls;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return CK_OK;
        }
    };
    Document document(host);
    CHECK(document.New(nullptr));
    auto *frames = new SlowFrames(document.Context());
    CHECK(document.Context()->RegisterNewManager(frames) == CK_OK);
    Debugger debugger(document);
    debugger.framesPerUpdate = 100;
    debugger.Play();
    debugger.Update(0.001);
    debugger.Pause();
    CHECK(frames->calls == 1 && debugger.Frames() == 1);
    debugger.Detach();
}

// Script Debugger: a behavior breakpoint stops a traced run on that behavior.
void ScriptDebugger(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes) {
    CKContext *context = host.Context();
    CKLevel *level = context->GetCurrentLevel();
    auto *entity = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Debug Entity"));
    level->AddObject(entity);
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(100);
    EditResult script = CreateScript(document, entity->GetID(), "Debug Script");
    CHECK(script.ok);
    const CK_ID id = script.created;
    CHECK(EditScriptData(document, id, "Open Script", [](InterfaceData &data) { SetScriptOpen(data, true); }));
    CK_ID block = 0;
    for (const CKGUID &guid : prototypes) {
        EditResult inserted = InsertBuildingBlock(document, id, id, guid, Point(300, 100));
        if (!inserted.ok) continue;
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(inserted.created));
        if (behavior->GetInputCount() > 0 &&
            CreateBehaviorLink(document, id, StartEndpoint(document, id), LinkEndpoint(inserted.created, 0, ENDPOINT_BIN)).ok) {
            block = inserted.created;
            break;
        }
        undo.Undo();
    }
    CHECK(block);
    CHECK(ToggleBreakpoint(document, id, EXTRA_DATA_BEHAVIOR, block));
    CHECK(FindBreakpoint(document.Session(id)->Data(), EXTRA_DATA_BEHAVIOR, block));
    // Saved with the Schematic.
    CKBehavior *runtime = CKBehavior::Cast(context->GetObject(id));
    InterfaceData serializable = document.Session(id)->Data();
    CKStateChunk *chunk = GenerateInterfaceChunk(serializable, runtime);
    InterfaceData reloaded;
    CHECK(chunk && reloaded.LoadFromChunk(runtime, chunk) == CK_OK && FindBreakpoint(reloaded, EXTRA_DATA_BEHAVIOR, block));
    DeleteCKStateChunk(chunk);
    // The Script runs from the scene start.
    CKScene *scene = level->GetLevelScene();
    scene->SetObjectFlags(runtime, CK_SCENEOBJECT_FLAGS(CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_ACTIVE));
    // The Debugger launches the scene when nothing is managed yet.
    (void)scene;
    if (std::getenv("SCHEMATIC_DEBUG_TRACE")) {
        CKBehaviorManager *behaviors = context->GetBehaviorManager();
        std::cout << "  current scene " << (level->GetCurrentScene() ? "set" : "(none)")
                  << " here " << scene->IsObjectHere(entity) << " script active " << runtime->IsActive()
                  << " managed objects " << (behaviors ? behaviors->GetObjectsCount() : -1) << std::endl;
    }
    Debugger debugger(document);
    debugger.trace = true;
    debugger.maxFrames = 5;
    debugger.Play();
    for (int i = 0; i < 100 && debugger.GetState() == Debugger::State::Running; ++i) debugger.Update(0.05);
    bool reached = false;
    for (const auto &entry : debugger.Log()) {
        if (std::getenv("SCHEMATIC_DEBUG_TRACE")) std::cout << "  " << entry.text << "\n";
        reached = reached || entry.text.find("reached") != std::string::npos;
    }
    const bool atBlock = debugger.CurrentBehavior() == block;
    CHECK(reached && atBlock && debugger.GetState() == Debugger::State::Paused);
    debugger.LeaveBehavior();
    debugger.StepFrame();
    // Dev pauses the context after Step One Frame (managers get OnCKPause);
    // a breakpoint within a traced frame does not.
    CHECK(!context->IsPlaying());
    debugger.Play();
    CHECK(context->IsPlaying());
    debugger.Pause();
    CHECK(!context->IsPlaying() || debugger.InFrame());
    debugger.Reset();
    CHECK(debugger.GetState() == Debugger::State::Stopped);
    // Breakpoint removal is undoable.
    CHECK(ToggleBreakpoint(document, id, EXTRA_DATA_BEHAVIOR, block));
    CHECK(!FindBreakpoint(document.Session(id)->Data(), EXTRA_DATA_BEHAVIOR, block));
    CHECK(undo.Undo() && FindBreakpoint(document.Session(id)->Data(), EXTRA_DATA_BEHAVIOR, block));
    std::cout << "Script debugger " << (reached ? "passed (breakpoint reached)" : "ran without reaching the breakpoint")
              << " after " << debugger.Frames() << " frames\n";
    debugger.Detach();
}

// The commands the editor's windows use that the other cases do not: the
// Schematic's move and fold, a behavior's priority, a breakpoint's
// conditions, Replace with File Graph (checked against the runtime with
// their undo and redo), then a Setup's object edit, Send to Place and a
// scene's environment.
void EditorCommands(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes) {
    CKContext *context = host.Context();
    CKLevel *level = context->GetCurrentLevel();
    auto *entity = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Edited Entity"));
    level->AddObject(entity);
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(1000);
    const EditResult made = CreateScript(document, entity->GetID(), "Edited Script");
    CHECK(made.ok);
    const CK_ID id = made.created;
    CHECK(EditScriptData(document, id, "Open Script", [](InterfaceData &data) { SetScriptOpen(data, true); }));
    Recorder recorder(document, id);
    const EditResult block = InsertBuildingBlock(document, id, id, prototypes[0], Point(300, 100));
    recorder.After(block, "insert");
    const EditResult graph = CreateBehaviorGraph(document, id, id, "Graph", Point(300, 300));
    recorder.After(graph, "graph");
    CHECK(block.ok && graph.ok);

    // The canvas drag: the primary snapped to the grid, the others by its delta.
    {
        const SchematicModel model = BuildModel(document, id);
        const BehaviorItem *item = model.FindBehavior(block.created);
        CHECK(item);
        const InterfaceData original = document.Session(id)->Data();
        const Rect before = FindRecord(original, block.created)->rect;
        const bool moved = EditScriptData(document, id, "Move", [&](InterfaceData &data) {
            MoveElements(data, original, true, {item->ref}, item->ref, Point(47, 23));
        });
        recorder.After(EditResult{moved}, "move");
        const Rect after = FindRecord(document.Session(id)->Data(), block.created)->rect;
        CHECK(moved && (after.hPos != before.hPos || after.vPos != before.vPos));
    }
    // A graph folded, then expanded again.
    for (bool folded : {true, false}) {
        const State before = Capture(document, id);
        const bool edited = EditScriptData(document, id, folded ? "Fold" : "Expand",
                                           [&](InterfaceData &data) { SetBehaviorFolded(data, graph.created, folded); });
        recorder.After(EditResult{edited}, folded ? "fold" : "expand");
        CHECK(edited && !Same(before, Capture(document, id)));
    }
    const EditResult priority = SetBehaviorPriority(document, id, block.created, 5);
    recorder.After(priority, "priority");
    CHECK(priority.ok && CKBehavior::Cast(context->GetObject(block.created))->GetPriority() == 5);
    // A breakpoint's conditions; without the breakpoint nothing is recorded.
    CHECK(ToggleBreakpoint(document, id, EXTRA_DATA_BEHAVIOR, block.created));
    recorder.After(EditResult{true}, "breakpoint");
    ExtraSubData condition;
    condition.value1 = 1;
    CHECK(SetBreakpointConditions(document, id, EXTRA_DATA_BEHAVIOR, block.created, {condition}));
    recorder.After(EditResult{true}, "conditions");
    const ExtraData *breakpoint = FindBreakpoint(document.Session(id)->Data(), EXTRA_DATA_BEHAVIOR, block.created);
    CHECK(breakpoint && breakpoint->subData.size() == 1);
    CHECK(!SetBreakpointConditions(document, id, EXTRA_DATA_BEHAVIOR, graph.created, {condition}));
    recorder.After(EditResult{false}, "conditions without a breakpoint");
    // Replace with File Graph: a graph saved as .nms replaces another.
    {
        const EditResult saved = CreateBehaviorGraph(document, id, id, "Saved Graph", Point(600, 300));
        recorder.After(saved, "graph to save");
        const std::string path = (std::filesystem::temp_directory_path() / "VirtoolsDevEditorCommands.nms").string();
        std::string error;
        CHECK(saved.ok && SaveBehaviorsAsNms(document, {saved.created}, path, &error));
        const EditResult replaced = ReplaceWithFileGraph(document, id, graph.created, path);
        recorder.After(replaced, "replace with file graph");
        if (!replaced.ok) std::cerr << "replace with file graph: " << replaced.message << '\n';
        CHECK(replaced.ok);
        std::filesystem::remove(path);
    }
    recorder.UndoAllRedoAll();

    // A Setup's edit of any object: its state before, then one step.
    {
        ObjectsGesture edit;
        CHECK(edit.Begin(document, {entity->GetID()}));
        const VxVector moved(1.0f, 2.0f, 3.0f);
        entity->SetPosition(&moved);
        CHECK(edit.Commit(document, "Set Position").ok);
        VxVector now;
        CHECK(undo.Undo());
        entity->GetPosition(&now);
        CHECK(Magnitude(now) < 0.001f);
        CHECK(undo.Redo());
        entity->GetPosition(&now);
        CHECK(Magnitude(now - moved) < 0.001f);
        CHECK(edit.Begin(document, {entity->GetID()}));
        CHECK(edit.Commit(document, "Nothing").Unchanged());
    }
    // Send to Place: the place becomes the parent; undo gives the old one back.
    {
        const EditResult place = CreatePlace(document, "Edited Place", {});
        CHECK(place.ok);
        auto parentIs = [&](CK_ID parent) {
            return entity->GetParent() ? entity->GetParent()->GetID() == parent : parent == 0;
        };
        CHECK(parentIs(0) && SendToPlace(document, {entity->GetID()}, place.created).ok && parentIs(place.created));
        CHECK(undo.Undo() && parentIs(0));
        CHECK(undo.Redo() && parentIs(place.created));
    }
    // A scene's environment (the Scene Setup): refused without a change.
    {
        CKScene *scene = level->GetLevelScene();
        const SceneEnvironment before = ReadSceneEnvironment(scene);
        CHECK(!CommitSceneEnvironment(document, scene->GetID(), before, "Nothing").ok);
        const CKDWORD ambient = before.ambient ^ 0x00102030;
        scene->SetAmbientLight(ambient);
        CHECK(CommitSceneEnvironment(document, scene->GetID(), before, "Ambient").ok);
        CHECK(undo.Undo() && ReadSceneEnvironment(scene).ambient == before.ambient);
        CHECK(undo.Redo() && ReadSceneEnvironment(scene).ambient == ambient);
    }
    std::cout << "Editor commands passed (" << recorder.Applied() << " Schematic commands)\n";
}

void Catalogs(CKContext *context) {
    const auto types = ListParameterTypes(context);
    CHECK(std::any_of(types.begin(), types.end(), [](const ParameterTypeEntry &type) {
        return type.guid == CKPGUID_INT;
    }));
    const auto operations = ListParameterOperations(context);
    const CKGUID addition = context->GetParameterManager()->OperationNameToGuid((CKSTRING) "Addition");
    CHECK(std::any_of(operations.begin(), operations.end(),
                       [&](const OperationEntry &operation) { return operation.guid == addition; }));
    const auto signatures = ListOperationSignatures(context, addition);
    CHECK(std::any_of(signatures.begin(), signatures.end(), [](const OperationSignature &signature) {
        return signature.result == CKPGUID_INT && signature.input1 == CKPGUID_INT && signature.input2 == CKPGUID_INT;
    }));
    std::cout << "Catalogs: " << types.size() << " parameter types, " << operations.size() << " operations, "
              << signatures.size() << " Addition signatures" << '\n';
}

// Native text conversion at the CK/UI boundary, without selecting a code page.
void TextEncodingChecks() {
    std::vector<std::string> told;
    SetLossyTextHandler([&told](const std::string &text) { told.push_back(text); });
    struct Restore {
#ifndef _WIN32
        std::string locale = std::setlocale(LC_CTYPE, nullptr);
#endif
        ~Restore() {
            SetLossyTextHandler(nullptr);
#ifndef _WIN32
            std::setlocale(LC_CTYPE, locale.c_str());
#endif
        }
    } restore;
    const std::string plain = "Plain name 1.cmo";
    bool lossy = true;
    CHECK(Utf8ToAnsi(plain, &lossy) == plain && !lossy && AnsiToUtf8(plain) == plain && told.empty());
    const std::string filter("A\0*.a\0", 6);
    CHECK(AnsiToWide(filter).size() == filter.size());
    CHECK(WideToAnsi(AnsiToWide(filter)) == filter);
    CHECK(WideToUtf8(Utf8ToWide(filter)) == filter);
    CHECK(AnsiToUtf8("").empty() && Utf8ToAnsi("", &lossy).empty() && !lossy);
    const std::string smile = "Smile \xF0\x9F\x98\x80";
    CHECK(WideToUtf8(Utf8ToWide(smile)) == smile);
    const std::string invalid[] = {"\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x82", "\xFF"};
    for (const auto &text : invalid) {
        bool rejected = false;
        try { Utf8ToWide(text); } catch (const TextEncodingError &) { rejected = true; }
        CHECK(rejected);
        std::string output = "original";
        rejected = false;
        told.clear();
        try { output = Utf8ToAnsi(text, &lossy); } catch (const TextEncodingError &) { rejected = true; }
        CHECK(rejected && lossy && output == "original" && told.size() == 1 && told.front() == "Invalid UTF-8 text");
    }
#ifdef _WIN32
    const int localeCount = 1;
#else
    const int localeCount = 2;
    const char *locales[] = {"C", ""};
#endif
    for (int locale = 0; locale < localeCount; ++locale) {
#ifndef _WIN32
        CHECK(std::setlocale(LC_CTYPE, locales[locale]) != nullptr);
#endif
        const std::string samples[] = {plain, "caf\xC3\xA9", "\xE2\x82\xAC", "\xE4\xB8\xAD\xE6\x96\x87", smile, "\xEF\xBF\xBD"};
        for (const auto &utf8 : samples) {
            const std::wstring wide = Utf8ToWide(utf8);
            std::string expected;
            bool valid = true;
#ifdef _WIN32
            const UINT page = GetACP();
            BOOL substituted = FALSE;
            BOOL *used = page == CP_UTF8 ? nullptr : &substituted;
            DWORD flags = page == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
            int length = WideCharToMultiByte(page, flags, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, used);
            valid = length > 0 && !substituted;
            if (valid) {
                expected.resize(length);
                CHECK(WideCharToMultiByte(page, flags, wide.data(), static_cast<int>(wide.size()), expected.data(), length, nullptr, used) == length && !substituted);
            }
#else
            std::mbstate_t state = {};
            for (wchar_t character : wide) {
                char bytes[MB_LEN_MAX];
                size_t count = std::wcrtomb(bytes, character, &state);
                if (count == static_cast<size_t>(-1)) { valid = false; break; }
                expected.append(bytes, count);
            }
            if (valid && !std::mbsinit(&state)) {
                char bytes[MB_LEN_MAX];
                size_t count = std::wcrtomb(bytes, L'\0', &state);
                CHECK(count != static_cast<size_t>(-1));
                expected.append(bytes, count - 1);
            }
#endif
            told.clear(); lossy = true;
            std::string actual = "original";
            bool rejected = false;
            try { actual = Utf8ToAnsi(utf8, &lossy); } catch (const TextEncodingError &) { rejected = true; }
            CHECK(rejected != valid && lossy == !valid);
            if (valid) {
                CHECK(actual == expected && told.empty());
                CHECK(AnsiToUtf8(actual) == utf8 && AnsiToWide(actual) == wide);
                CHECK(WideToAnsi(wide, &lossy) == expected && !lossy && told.empty());
            } else {
                CHECK(actual == "original" && told.size() == 1 && told.front() == utf8);
                rejected = false;
                try { actual = WideToAnsi(wide, &lossy); } catch (const TextEncodingError &) { rejected = true; }
                CHECK(rejected && lossy && actual == "original" && told.size() == 2 && told.back() == utf8);
            }
        }
        const std::string raw("\xD6\xD0\xCE\xC4");
        const std::string before = raw;
        const std::string display = AnsiToUtf8(raw);
        CHECK(raw == before && WideToUtf8(Utf8ToWide(display)) == display);
    }
    std::cout << "Native text encoding passed\n";
}

// The Setups' actions (SetupCommands): each one undo step, undone and redone;
// the refusals.
void SetupActions(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    CHECK(context->GetManagerByGuid(GRID_MANAGER_GUID));
    CKLevel *level = context->GetCurrentLevel();
    Document document(host);
    document.RefreshScripts();
    document.History().SetLimit(100);
    auto *entity = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Action Entity"));
    CHECK(entity);
    if (level) level->AddObject(entity);
    entity->SetPosition3f(1.0f, 2.0f, 3.0f);
    entity->SetScale3f(2.0f, 3.0f, 4.0f);
    const CK_ID id = entity->GetID();
    // A 3D entity's moved flags differ after an undo (OPEN-ISSUES): its position and scale are compared.
    VxVector at, scale;
    CHECK(ResetWorldMatrix(document, id).ok);
    entity->GetPosition(&at);
    CHECK(at.x == 0.0f && at.y == 0.0f && at.z == 0.0f);
    CHECK(document.History().Undo());
    entity->GetPosition(&at);
    CHECK(at.x == 1.0f && at.y == 2.0f && at.z == 3.0f);
    CHECK(document.History().Redo());
    entity->SetScale3f(2.0f, 3.0f, 4.0f);
    CHECK(SetScaleToUnit(document, id).ok);
    entity->GetScale(&scale);
    CHECK(scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f);
    CHECK(document.History().Undo());
    entity->GetScale(&scale);
    CHECK(scale.x == 2.0f && scale.y == 3.0f && scale.z == 4.0f);
    CHECK(document.History().Redo());
    CHECK(!ResetWorldMatrix(document, document.Context()->GetCurrentLevel() ? context->GetCurrentLevel()->GetID() : 0).ok);

    // A texture's slots: one added (current), the current one removed; the last one stays.
    auto *texture = CKTexture::Cast(context->CreateObject(CKCID_TEXTURE, (CKSTRING) "Action Texture"));
    CHECK(texture && texture->Create(16, 16, 32, 0));
    // Its slot count is not in its state (ObjectStateAction keeps it): the slots are compared.
    auto slots = [&](int count, int current) { return texture->GetSlotCount() == count && texture->GetCurrentSlot() == current; };
    CHECK(AddBitmapSlot(document, texture->GetID()).ok && slots(2, 1));
    CHECK(document.History().Undo() && slots(1, 0));
    CHECK(document.History().Redo() && slots(2, 1));
    CHECK(RemoveCurrentSlot(document, texture->GetID()).ok && slots(1, 0));
    CHECK(document.History().Undo() && slots(2, 1));
    CHECK(document.History().Redo() && slots(1, 0));
    CHECK(!RemoveCurrentSlot(document, texture->GetID()).ok);

    // The slot menu (Dev.exe sub_485C10): each edit undone and redone with the
    // images (pixels, size, slots) as they were.
    {
        const CK_ID tid = texture->GetID();
        texture->SetPixel(0, 0, 0xFF102030, 0);
        texture->ReleaseSurfacePtr(0);
        auto pixel = [&](int slot = 0) { return texture->GetPixel(0, 0, slot); };
        CHECK(EditSlotImage(document, tid, SlotImageOp::InvertColors).ok && pixel() == 0xFFEFDFCF);
        CHECK(document.History().Undo() && pixel() == 0xFF102030);
        CHECK(document.History().Redo() && pixel() == 0xFFEFDFCF);
        CHECK(EditSlotImage(document, tid, SlotImageOp::AlphaTransparent).ok && pixel() == 0x00EFDFCF);
        CHECK(EditSlotImage(document, tid, SlotImageOp::AlphaTransparent).Unchanged());
        CHECK(EditSlotImage(document, tid, SlotImageOp::InvertAlpha).ok && pixel() == 0xFFEFDFCF);
        CHECK(document.History().Undo() && pixel() == 0x00EFDFCF);
        CHECK(document.History().Undo() && pixel() == 0xFFEFDFCF);
        CHECK(EditSlotImage(document, tid, SlotImageOp::AlphaTranslucent).ok && (pixel() >> 24) == 128);
        CHECK(document.History().Undo() && pixel() == 0xFFEFDFCF);
        // Saved, then read back in a new slot, in the current slot, and again.
        const std::string bmp = (std::filesystem::temp_directory_path() / "VirtoolsDevSlot.bmp").string();
        CHECK(SaveSlotImage(texture, bmp, false));
        CHECK(AddSlotImages(document, tid, {bmp, bmp}).ok && slots(3, 2) && (pixel(2) & 0xFFFFFF) == 0xEFDFCF);
        CHECK(document.History().Undo() && slots(1, 0));
        CHECK(document.History().Redo() && slots(3, 2));
        CHECK(!AddSlotImages(document, tid, {bmp + ".missing"}).ok && slots(3, 2));
        CHECK(EditSlotImage(document, tid, SlotImageOp::InvertColors).ok && (pixel(2) & 0xFFFFFF) == 0x102030);
        CHECK(ReloadSlotImage(document, tid).ok && (pixel(2) & 0xFFFFFF) == 0xEFDFCF);
        CHECK(document.History().Undo() && (pixel(2) & 0xFFFFFF) == 0x102030);
        CHECK(ReplaceSlotImage(document, tid, bmp).ok && (pixel(2) & 0xFFFFFF) == 0xEFDFCF);
        CHECK(document.History().Undo() && (pixel(2) & 0xFFFFFF) == 0x102030);
        // The alpha from the image's channels (their mean, 0xEF 0xDF 0xCF: 0xDF).
        CHECK(ReplaceSlotAlpha(document, tid, bmp).ok && (pixel(2) >> 24) == 0xDF);
        CHECK(document.History().Undo() && (pixel(2) >> 24) == 0xFF);
        // Every slot resized.
        CHECK(ResizeSlotImages(document, tid, 8, 4).ok && texture->GetWidth() == 8 && texture->GetHeight() == 4);
        CHECK(document.History().Undo() && texture->GetWidth() == 16 && texture->GetHeight() == 16 && slots(3, 2) &&
              (pixel(2) & 0xFFFFFF) == 0x102030);
        CHECK(document.History().Redo() && texture->GetWidth() == 8);
        CHECK(document.History().Undo());
        std::error_code ignored;
        std::filesystem::remove(bmp, ignored);
    }

    // The Material Setup's Mode (Dev.exe sub_4634E0 712, sub_46A6E0).
    {
        auto *material = CKMaterial::Cast(context->CreateObject(CKCID_MATERIAL, (CKSTRING) "Mode Material"));
        CHECK(material);
        namespace p = properties;
        p::MaterialMode.set(material, 2);
        CHECK(p::MaterialMode.get(material) == 2 && material->AlphaTestEnabled() && material->AlphaBlendEnabled() &&
              material->ZWriteEnabled() && material->GetAlphaFunc() == VXCMP_GREATEREQUAL && material->GetAlphaRef() == 1);
        p::MaterialMode.set(material, 1);
        CHECK(p::MaterialMode.get(material) == 1 && !material->AlphaTestEnabled() && !material->ZWriteEnabled() &&
              material->GetSourceBlend() == VXBLEND_SRCALPHA && material->GetDestBlend() == VXBLEND_INVSRCALPHA);
        VxColor diffuse = material->GetDiffuse();
        diffuse.a = 0.5f;
        material->SetDiffuse(diffuse);
        p::MaterialMode.set(material, 0);
        CHECK(p::MaterialMode.get(material) == 0 && !material->AlphaBlendEnabled() && material->GetDiffuse().a == 1.0f &&
              material->GetSourceBlend() == VXBLEND_ONE && material->GetDestBlend() == VXBLEND_ZERO);
        material->SetDestBlend(VXBLEND_ONE);
        CHECK(p::MaterialMode.get(material) == 3);
        context->DestroyObject(material);
    }

    // The Array Setup's menus (Dev.exe sub_43F120): each command one undo step.
    {
        auto *array = CKDataArray::Cast(context->CreateObject(CKCID_DATAARRAY, (CKSTRING) "Action Array"));
        CHECK(array);
        if (level) level->AddObject(array);
        const CK_ID aid = array->GetID();
        array->InsertColumn(-1, CKARRAYTYPE_INT, (CKSTRING) "Number");
        array->InsertColumn(-1, CKARRAYTYPE_STRING, (CKSTRING) "Name");
        array->InsertColumn(-1, CKARRAYTYPE_PARAMETER, (CKSTRING) "Value", CKPGUID_FLOAT);
        const char *names[] = {"b", "c", "a"};
        for (int r = 0; r < 3; ++r) {
            array->AddRow();
            array->SetElementValue(r, 0, &r, sizeof(int));
            array->SetElementStringValue(r, 1, const_cast<char *>(names[r]));
        }
        auto number = [&](int r) {
            int value = -1;
            array->GetElementValue(r, 0, &value);
            return value;
        };
        auto name = [&](int r) {
            char text[32] = "";
            array->GetElementStringValue(r, 1, text, sizeof(text));
            return std::string(text);
        };
        CHECK(SortArray(document, aid, 1, true).ok && name(0) == "a" && name(2) == "c" && number(0) == 2);
        CHECK(document.History().Undo() && name(0) == "b" && number(0) == 0);
        // A parameter column is not sorted (integer, float and string only).
        CHECK(!SortArray(document, aid, 2, true).ok && !SortableColumn(array, 2) && SortableColumn(array, 0));
        CHECK(SetArrayKeyColumn(document, aid, 1).ok && array->GetKeyColumn() == 1);
        CHECK(SetArrayKeyColumn(document, aid, 1).Unchanged());
        // A key column takes a value once.
        CHECK(!SetArrayCells(document, aid, {0}, 1, "c").ok && name(0) == "b");
        CHECK(SetArrayCells(document, aid, {0}, 1, "z").ok && name(0) == "z");
        CHECK(document.History().Undo() && name(0) == "b");
        CHECK(document.History().Undo() && array->GetKeyColumn() == -1);
        CHECK(SetArrayCells(document, aid, {0, 2}, 1, "same").ok && name(0) == "same" && name(2) == "same" && name(1) == "c");
        CHECK(document.History().Undo() && name(0) == "b" && name(2) == "a");
        CHECK(MoveArrayColumn(document, aid, 0, 1).ok && std::string(array->GetColumnName(1)) == "Number");
        CHECK(MoveArrayColumn(document, aid, 1, 1).ok && std::string(array->GetColumnName(2)) == "Number");
        CHECK(!MoveArrayColumn(document, aid, 2, 1).ok);
        CHECK(document.History().Undo() && document.History().Undo() && std::string(array->GetColumnName(0)) == "Number");
        CHECK(EditArrayColumn(document, aid, 1, "Label", CKARRAYTYPE_STRING, CKGUID(0, 0)).ok &&
              std::string(array->GetColumnName(1)) == "Label" && name(1) == "c");
        CHECK(document.History().Undo() && std::string(array->GetColumnName(1)) == "Name");
        CHECK(MoveArrayRow(document, aid, 0, 1).ok && name(0) == "c" && name(1) == "b");
        CHECK(!MoveArrayRow(document, aid, 2, 1).ok);
        CHECK(document.History().Undo() && name(0) == "b");
        CHECK(DuplicateArrayRows(document, aid, {0, 2}).ok && array->GetRowCount() == 5 && name(3) == "b" && name(4) == "a" &&
              number(4) == 2);
        CHECK(document.History().Undo() && array->GetRowCount() == 3);
        CHECK(RemoveArrayRows(document, aid, {2, 0}).ok && array->GetRowCount() == 1 && name(0) == "c");
        CHECK(document.History().Undo() && array->GetRowCount() == 3 && name(0) == "b" && name(2) == "a");
        // Paste: a float on the parameter column's rows; another type is left.
        CKParameterOut *source = context->CreateCKParameterOut((CKSTRING) "Copied", CKPGUID_FLOAT);
        CKParameterOut *other = context->CreateCKParameterOut((CKSTRING) "Other", CKPGUID_STRING);
        const float copied = 4.5f;
        source->SetValue(&copied);
        auto value = [&](int r) {
            float v = -1.0f;
            if (CKParameter *p = *reinterpret_cast<CKParameter **>(array->GetElement(r, 2))) p->GetValue(&v);
            return v;
        };
        const float before = value(1);
        CHECK(PasteArrayParameters(document, aid, {1}, 2, {source->GetID()}).ok && value(1) == 4.5f);
        CHECK(document.History().Undo() && value(1) == before);
        CHECK(!PasteArrayParameters(document, aid, {1}, 2, {other->GetID()}).ok);
        CHECK(!PasteArrayParameters(document, aid, {1}, 1, {source->GetID()}).ok);
        // Paste As Shortcut: the cell is the copied parameter (the array's
        // own one kept for undo); Remove Shortcut: a copy of it, the array's.
        auto cell = [&](int r) { return *reinterpret_cast<CKParameter **>(array->GetElement(r, 2)); };
        CKParameter *own = cell(1);
        CHECK(own && own->GetOwner() == array);
        CHECK(PasteArrayShortcuts(document, aid, {1}, 2, {source->GetID()}).ok && cell(1) == source);
        CHECK(!PasteArrayShortcuts(document, aid, {1}, 2, {other->GetID()}).ok);
        CHECK(RemoveArrayShortcuts(document, aid, {1}, 2).ok && cell(1) != source && cell(1)->GetOwner() == array &&
              value(1) == 4.5f);
        CHECK(!RemoveArrayShortcuts(document, aid, {1}, 2).ok);
        CHECK(document.History().Undo() && cell(1) == source);
        CHECK(document.History().Undo() && cell(1) == own && own->GetOwner() == array && value(1) == before);
        context->DestroyObject(source);
        context->DestroyObject(other);
    }

    // A save format made for a reader (the 2.1 readers have no default
    // properties): under CKCopyBitmapProperties' 256 bytes, taken by a texture.
    {
        const std::vector<char> bytes = MakeSaveFormat(context, "bmp");
        CHECK(!bytes.empty() && bytes.size() < 256);
        auto *texture = CKTexture::Cast(context->CreateObject(CKCID_TEXTURE, (CKSTRING) "Format Texture"));
        CHECK(texture);
        texture->SetSaveFormat(reinterpret_cast<CKBitmapProperties *>(const_cast<char *>(bytes.data())));
        CHECK(texture->GetSaveFormat() && _stricmp(texture->GetSaveFormat()->m_Ext.m_Data, "bmp") == 0);
        context->DestroyObject(texture);
        CHECK(MakeSaveFormat(context, "nosuchformat").empty());
    }

    // A grid's layer: added, removed; the same type twice: no change.
    auto *grid = CKGrid::Cast(context->CreateObject(CKCID_GRID, (CKSTRING) "Action Grid"));
    CHECK(grid);
    if (level) level->AddObject(grid);
    // The composition's objects compared (the layer goes with an undo), the
    // grid by its layers (its state's bytes differ after an undo).
    CK_ID layer = 0;
    ExpectUndoRoundTrip(document, "add layer", [&] {
        const EditResult added = AddGridLayer(document, grid->GetID(), 1);
        layer = added.created;
        return added;
    });
    CHECK(grid->GetLayerCount() == 1 && GridGetLayer(grid, 1) && GridGetLayer(grid, 1)->GetID() == layer);
    CHECK(document.History().Undo() && grid->GetLayerCount() == 0 && document.IsDiscarded(layer));
    CHECK(document.History().Redo() && grid->GetLayerCount() == 1 && !document.IsDiscarded(layer));
    CHECK(AddGridLayer(document, grid->GetID(), 1).Unchanged());
    CHECK(RemoveGridLayer(document, grid->GetID(), 1).ok && grid->GetLayerCount() == 0);
    CHECK(document.History().Undo() && grid->GetLayerCount() == 1);
    CHECK(document.History().Redo() && grid->GetLayerCount() == 0);
    // The Grid Cross: a column added on the left keeps the squares' values
    // (shifted), undone the grid is as it was.
    {
        if (grid->GetWidth() < 2 || grid->GetLength() < 2) grid->SetDimensions(4, 3, 4.0f, 3.0f);
        CHECK(AddGridLayer(document, grid->GetID(), 1).ok);
        const int width = grid->GetWidth(), length = grid->GetLength();
        std::cout << "setup actions: grid " << width << " x " << length << '\n';
        int seven = 7, read = 0;
        GridGetLayer(grid, 1)->SetValue2(0, 0, &seven);
        CHECK(ResizeGrid(document, grid->GetID(), 0, true).ok);
        CHECK(grid->GetWidth() == width + 1 && grid->GetLength() == length && GridGetLayer(grid, 1));
        if (GridGetLayer(grid, 1)) GridGetLayer(grid, 1)->GetValue2(1, 0, &read);
        CHECK(read == 7);
        CHECK(document.History().Undo() && grid->GetWidth() == width && GridGetLayer(grid, 1));
        read = 0;
        if (GridGetLayer(grid, 1)) GridGetLayer(grid, 1)->GetValue2(0, 0, &read);
        CHECK(read == 7);
        CHECK(ResizeGrid(document, grid->GetID(), 3, false).ok == (length > 1));
        // Layer types: registered, renamed (a name in use refused).
        const EditResult type = RegisterGridLayerType(document, "Action Layer Type");
        CHECK(type.ok && type.created > 0);
        CHECK(RenameGridLayerType(document, static_cast<int>(type.created), "Action Layer Renamed").ok);
        CKGridManager *manager = static_cast<CKGridManager *>(context->GetManagerByGuid(GRID_MANAGER_GUID));
        CHECK(manager && std::string(manager->GetTypeName(static_cast<int>(type.created))) == "Action Layer Renamed");
        CHECK(!RenameGridLayerType(document, static_cast<int>(type.created), manager->GetTypeName(1)).ok);
        CHECK(SetGridLayerTypeLinker(document, static_cast<int>(type.created), true).ok &&
              manager->GetAssociatedParam(static_cast<int>(type.created)) == CKPGUID_LINKERGRAPH_ENUM);
    }

    // A mesh consolidated (its two equal vertices made one).
    auto *mesh = CKMesh::Cast(context->CreateObject(CKCID_MESH, (CKSTRING) "Action Mesh"));
    CHECK(mesh);
    mesh->SetVertexCount(4);
    VxVector corners[4] = {VxVector(0, 0, 0), VxVector(1, 0, 0), VxVector(0, 1, 0), VxVector(1, 0, 0)};
    for (int i = 0; i < 4; ++i) mesh->SetVertexPosition(i, &corners[i]);
    mesh->SetFaceCount(2);
    mesh->SetFaceVertexIndex(0, 0, 1, 2);
    mesh->SetFaceVertexIndex(1, 2, 3, 0);
    const EditResult consolidated = ConsolidateMesh(document, mesh->GetID());
    std::cout << "setup actions: consolidate " << (consolidated.ok ? "applied" : consolidated.message) << ", "
              << mesh->GetVertexCount() << " vertices\n";
    CHECK(consolidated.ok || consolidated.Unchanged());
    if (consolidated.ok) CHECK(document.History().Undo() && mesh->GetVertexCount() == 4);
}

void NewSceneEnvironment(CkRuntimeHost &host) {
    Document document(host);
    std::string error;
    auto check = [&](const SceneEnvironment &expected) {
        CHECK(document.New(&error, expected));
        CKScene *scene = host.Context()->GetCurrentLevel()->GetLevelScene();
        CHECK(scene);
        const SceneEnvironment actual = ReadSceneEnvironment(scene);
        CHECK(actual.ambient == expected.ambient && actual.background == expected.background &&
              actual.fogColor == expected.fogColor && actual.fogMode == expected.fogMode &&
              actual.fogStart == expected.fogStart && actual.fogEnd == expected.fogEnd &&
              actual.fogDensity == expected.fogDensity && !document.Dirty());
    };
    check(DefaultNewSceneEnvironment());
    SceneEnvironment custom = DefaultNewSceneEnvironment();
    custom.ambient = 0x123456;
    custom.background = 0x654321;
    custom.fogColor = 0xFEDCBA;
    custom.fogMode = VXFOG_LINEAR;
    custom.fogStart = 12.5f;
    custom.fogEnd = 250.0f;
    custom.fogDensity = 0.25f;
    check(custom);
    check(DefaultNewSceneEnvironment());
}

void InputViewport(CkRuntimeHost &host) {
    auto *input = static_cast<CKInputManager *>(host.Context()->GetManagerByGuid(INPUT_MANAGER_GUID));
    CHECK(input);
    std::shared_ptr<SDL_Window> window(SDL_CreateWindow("Input regression", 320, 240, SDL_WINDOW_HIDDEN),
        [input](SDL_Window *value) {
            input->SetInputViewport(nullptr, VxRect(), 0, 0);
            if (value) SDL_DestroyWindow(value);
        });
    CHECK(window);
    const SDL_WindowID id = SDL_GetWindowID(window.get());
    input->Pause(FALSE);
    CHECK(input->SetInputViewport(window.get(), VxRect(20, 30, 220, 130), 400, 300));
    CHECK(!input->SetInputViewport(window.get(), VxRect(20, 30, 20, 130), 400, 300));
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION; motion.motion.windowID = id;
    motion.motion.x = 70; motion.motion.y = 55;
    motion.motion.xrel = 2; motion.motion.yrel = -3;
    CHECK(SDL_PushEvent(&motion));
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN; key.key.windowID = id;
    key.key.scancode = SDL_SCANCODE_A; key.key.down = true;
    key.key.timestamp = SDL_GetTicksNS();
    CHECK(SDL_PushEvent(&key));
    // The application drains its queue first, as the editor does each frame.
    SDL_Event event{};
    int keyEvents = 0;
    while (SDL_PollEvent(&event)) if (event.type == SDL_EVENT_KEY_DOWN) ++keyEvents;
    CHECK(keyEvents == 1 && input->PreProcess() == CK_OK);
    Vx2DVector position; VxVector delta;
    input->GetMousePosition(position, FALSE); input->GetMouseRelativePosition(delta);
    CHECK(position.x == 100 && position.y == 75 && delta.x == 4 && delta.y == -9);
    CHECK(input->IsKeyDown(0x1e) && input->GetNumberOfKeyInBuffer() == 1);
    CHECK(input->PostProcess() == CK_OK);
    // Input processing also leaves the application's pending SDL events intact.
    key.key.scancode = SDL_SCANCODE_B;
    CHECK(SDL_PushEvent(&key) && input->PreProcess() == CK_OK && input->IsKeyDown(0x30));
    keyEvents = 0;
    while (SDL_PollEvent(&event)) if (event.type == SDL_EVENT_KEY_DOWN) ++keyEvents;
    CHECK(keyEvents == 1);
    input->Pause(TRUE);
    CHECK(!input->IsKeyDown(0x1e) && !input->IsKeyDown(0x30));
    CHECK(SDL_PushEvent(&key));
    input->Pause(FALSE);
    CHECK(input->PreProcess() == CK_OK && !input->IsKeyDown(0x30));
    key.key.windowID = id + 1000;
    CHECK(SDL_PushEvent(&key) && input->PreProcess() == CK_OK && !input->IsKeyDown(0x30));
    key.key.windowID = id;
    CHECK(SDL_PushEvent(&key) && input->PreProcess() == CK_OK && input->IsKeyDown(0x30));
    SDL_Event focusLost{}; focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST; focusLost.window.windowID = id;
    CHECK(SDL_PushEvent(&focusLost) && input->PreProcess() == CK_OK && !input->IsKeyDown(0x30));
    while (SDL_PollEvent(&event)) {}
    // Multiple queue reallocations retain key order, and SDL keeps its copy.
    for (int i = 0; i < 200; ++i) {
        key.type = i % 2 ? SDL_EVENT_KEY_UP : SDL_EVENT_KEY_DOWN;
        key.key.scancode = (i / 2) % 2 ? SDL_SCANCODE_D : SDL_SCANCODE_C;
        key.key.down = i % 2 == 0;
        CHECK(SDL_PushEvent(&key));
    }
    CHECK(input->PreProcess() == CK_OK && input->GetNumberOfKeyInBuffer() == 200);
    for (int i = 0; i < 200; ++i) {
        CKDWORD code = 0;
        CHECK(input->GetKeyFromBuffer(i, code) == (i % 2 ? KEY_RELEASED : KEY_PRESSED));
        CHECK(code == ((i / 2) % 2 ? 0x20u : 0x2eu));
    }
    keyEvents = 0;
    while (SDL_PollEvent(&event))
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) ++keyEvents;
    CHECK(keyEvents == 200 && input->PostProcess() == CK_OK);
    input->Pause(TRUE);
    input->Pause(FALSE);
    CHECK(input->PreProcess() == CK_OK && input->GetNumberOfKeyInBuffer() == 0);
    CHECK(!input->IsKeyDown(0x2e) && !input->IsKeyDown(0x20));
}

} // namespace vdevtest

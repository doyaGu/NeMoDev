#include "features/setup/SetupViewData.h"
#include "core/document/Document.h"
#include "core/level/SetupCommands.h"
#include "core/runtime/GridCompat.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

void TestSetupPages(vdev::Document &doc, vdev::EditorApplication &app) {
    using namespace vdev;
    const EditorClient client(app.Host(), 0);
    const auto sync = [&] { app.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    const auto create = [&](CK_CLASSID cid, const char *name = "Setup page") {
        auto *object = doc.Context()->CreateObject(cid, name);
        Check(object != nullptr, "create class Setup fixture");
        doc.Include(object->GetID());
        return object;
    };
    struct Page {
        CK_CLASSID cid;
        const char *section;
    };
    const Page pages[] = {{CKCID_3DOBJECT, "entity3d"},
                          {CKCID_CAMERA, "camera"},
                          {CKCID_LIGHT, "light"},
                          {CKCID_2DENTITY, "entity2d"},
                          {CKCID_SPRITE, "bitmap"},
                          {CKCID_TEXTURE, "bitmap"},
                          {CKCID_MATERIAL, "material"},
                          {CKCID_MESH, "mesh"},
                          {CKCID_PATCHMESH, "mesh"},
                          {CKCID_GRID, "grid"},
                          {CKCID_CURVE, "curve"},
                          {CKCID_CHARACTER, "character"},
                          {CKCID_WAVESOUND, "sound"},
                          {CKCID_SCENE, "scene"},
                          {CKCID_3DENTITY, "frame"},
                          {CKCID_PLACE, "place"},
                          {CKCID_SPRITE3D, "sprite3d"},
                          {CKCID_ANIMATION, "animation"},
                          {CKCID_OBJECTANIMATION, "object-animation"},
                          {CKCID_MIDISOUND, "midi"},
                          {CKCID_SPRITETEXT, "sprite-text"},
                          {CKCID_KINEMATICCHAIN, "kinematic"},
                          {CKCID_LAYER, "layer"},
                          {CKCID_PARAMETERIN, "parameter-input"},
                          {CKCID_PARAMETEROPERATION, "operation"},
                          {CKCID_BEHAVIOR, "behavior"},
                          {CKCID_BEHAVIORIO, "behavior-io"},
                          {CKCID_BEHAVIORLINK, "behavior-link"},
                          {CKCID_SYNCHRO, "synchro"},
                          {CKCID_STATE, "state"},
                          {CKCID_CRITICALSECTION, "critical-section"},
                          {CKCID_INTERFACEOBJECTMANAGER, "interface-objects"},
                          {CKCID_GROUP, "other"}};
    for (const auto &page : pages) {
        auto *object = create(page.cid);
        sync();
        SetupDetails data;
        const auto undo = doc.History().UndoCount();
        const auto revision = doc.ChangeRevision();
        const bool dirty = doc.Dirty();
        Check(ReadSetupDetails(client, doc.Reference(object->GetID()), page.section, data), page.section);
        Check(data.target == doc.Reference(object->GetID()) && data.documentRevision == revision &&
                  doc.ChangeRevision() == revision && doc.History().UndoCount() == undo && doc.Dirty() == dirty,
              "class Setup query modified state");
    }
    auto *parameter = doc.Context()->CreateCKParameterOut("Setup parameter", CKPGUID_INT);
    Check(parameter != nullptr, "parameter Setup fixture");
    doc.Include(parameter->GetID());
    sync();
    SetupDetails parameterPage;
    Check(ReadSetupDetails(client, doc.Reference(parameter->GetID()), "parameter", parameterPage), "parameter page");
    auto *entity = CK3dEntity::Cast(create(CKCID_3DOBJECT, "Original entity"));
    auto *mesh = CKMesh::Cast(create(CKCID_MESH, "Original mesh"));
    entity->AddMesh(mesh);
    auto *material = CKMaterial::Cast(create(CKCID_MATERIAL, "Original material"));
    mesh->AddChannel(material);
    auto *secondMaterial = CKMaterial::Cast(create(CKCID_MATERIAL, "Second material"));
    mesh->AddChannel(secondMaterial);
    sync();
    SetupDetails entityPage, meshPage;
    Check(ReadSetupDetails(client, doc.Reference(entity->GetID()), "entity3d", entityPage) &&
              ReadSetupDetails(client, doc.Reference(mesh->GetID()), "mesh", meshPage),
          "linked Setup data");
    Check(entityPage.Values().Rows("meshes").size() == 1 && meshPage.Values().Rows("channels").size() == 2,
          "material channel rows were lost");
    const SetupFields meshRow(entityPage.Values().Rows("meshes").front().fields);
    Check(meshRow.Object("target") == doc.Reference(mesh->GetID()) && meshRow.Text("name") == "Original mesh",
          "mesh row identity");
    mesh->SetName("Changed mesh");
    Check(meshRow.Text("name") == "Original mesh", "class Setup kept borrowed native text");
    SetupDetails properties;
    Check(ReadSetupProperties(client, doc.Reference(material->GetID()), {"material.diffuse", "material.mode"},
                              properties) &&
              properties.Values().Reals("material.diffuse").size() == 4,
          "batch property values");
    Check(
        !ReadSetupProperties(client, doc.Reference(material->GetID()), {"material.mode", "material.mode"}, properties),
        "duplicate property request");
    Check(!ReadSetupProperties(client, doc.Reference(material->GetID()), {"not-a-property"}, properties),
          "unknown property request");
    Check(ReadSetupProperties(client, doc.Reference(mesh->GetID()), {"mesh.channel-active"}, properties, {1}),
          "indexed property read");
    Check(!ReadSetupProperties(client, doc.Reference(mesh->GetID()), {"mesh.channel-active"}, properties, {200}),
          "invalid channel read");
    const auto saved = entityPage;
    Check(!ReadSetupDetails(client, entityPage.target, "not-a-page", entityPage) && entityPage.target == saved.target &&
              entityPage.fields.size() == saved.fields.size(),
          "failed page read overwrote copy");
    Check(!ReadSetupDetails(client, entityPage.target, "bitmap", entityPage), "wrong class page read");
    Check(!ReadSetupDetails(client, entityPage.target, "entity3d", entityPage, {UINT64_MAX}), "overflowed page index");
    std::vector<SetupObjectRow> choices;
    auto *hidden = create(CKCID_MATERIAL, "Private material");
    hidden->ModifyObjectFlags(CK_OBJECT_PRIVATE, 0);
    sync();
    Check(ReadSetupChoices(client, CKCID_MATERIAL, choices) &&
              std::none_of(choices.begin(), choices.end(),
                           [&](const auto &row) { return row.object == doc.Reference(hidden->GetID()); }),
          "private choices leaked");
    Check(!ReadSetupChoices(client, UINT32_MAX, choices), "unknown choice class");
    EditorStatus worker = EDITOR_OK;
    std::thread thread([&] {
        worker =
            client.Query("setup.details", {ObjectArgument("target", saved.target), TextArgument("section", "entity3d")})
                .status;
    });
    thread.join();
    Check(worker == EDITOR_REFUSED, "class query entered CK from worker");

    // Both bitmap kinds share the same copied pixels and ordinary commands.
    for (auto cid : {CKCID_TEXTURE, CKCID_SPRITE}) {
        auto *object = create(cid);
        auto *texture = CKTexture::Cast(object);
        auto *sprite = CKSprite::Cast(object);
        if (texture)
            texture->Create(2, 2, 32);
        else
            sprite->Create(2, 2, 32);
        if (texture)
            texture->SetPixel(0, 0, 0xff102030);
        else
            sprite->SetPixel(0, 0, 0xff102030);
        sync();
        const auto ref = doc.Reference(object->GetID());
        SetupDetails pixels;
        Check(ReadSetupDetails(client, ref, "bitmap-pixels", pixels, {0, 0}) && pixels.Values().Integer("total") == 4 &&
                  pixels.Values().Integers("pixels").front() == 0xff102030,
              "copied bitmap pixels");
        Check(!ReadSetupDetails(client, ref, "bitmap-pixel", pixels, {0, 2, 0}), "pixel bounds");
        doc.History().Clear();
        doc.MarkSaved();
        doc.History().MarkSavePoint();
        sync();
        const auto revision = doc.ChangeRevision();
        Check(client.Command("setup.slot-invert", {ObjectArgument("target", ref), IntegerArgument("slot", 0),
                                                   IntegerArgument("document-revision", revision)})
                      .status == EDITOR_OK,
              "registered image command");
        const auto pixel = [&] { return texture ? texture->GetPixel(0, 0) : sprite->GetPixel(0, 0); };
        Check(pixel() == 0xffefdfcf && doc.History().UndoCount() == 1 && doc.History().Undo() && pixel() == 0xff102030,
              "image command one Undo");
        sync();
        const bool redo = doc.History().CanRedo(), dirty = doc.Dirty();
        Check(client.Command("setup.slot-replace",
                             {ObjectArgument("target", ref), TextArgument("path", "/missing/setup.png"),
                              IntegerArgument("slot", 0), IntegerArgument("document-revision", doc.ChangeRevision())})
                          .status != EDITOR_OK &&
                  pixel() == 0xff102030 && doc.History().CanRedo() == redo && doc.Dirty() == dirty,
              "failed image command lost state");
        Check(client.Command("setup.slot-invert", {ObjectArgument("target", ref), IntegerArgument("slot", 0),
                                                   IntegerArgument("document-revision", revision)})
                      .status == EDITOR_STALE,
              "late image command used obsolete contents");
        const auto format = MakeSaveFormat(doc.Context(), "bmp");
        Check(!format.empty(), "native bitmap format fixture");
        auto *nativeFormat = reinterpret_cast<CKBitmapProperties *>(const_cast<char *>(format.data()));
        if (texture)
            texture->SetSaveFormat(nativeFormat);
        else
            sprite->SetSaveFormat(nativeFormat);
        BitmapFormat beforeFormat, afterFormat;
        Check(ReadBitmapFormat(object, beforeFormat), "capture native image format");
        const auto restore = texture ? texture->ToRestore() : sprite->ToRestore();
        Check(ReadSetupDetails(client, ref, "bitmap-format", pixels) &&
                  ReadSetupDetails(client, ref, "bitmap-encoding", pixels) && !pixels.Values().Text("info").empty(),
              "image saver projection");
        Check(ReadBitmapFormat(object, afterFormat) && beforeFormat.bytes == afterFormat.bytes &&
                  (texture ? texture->ToRestore() : sprite->ToRestore()) == restore,
              "readonly image queries changed format bytes or GPU reload state");
    }
    auto *grid = CKGrid::Cast(create(CKCID_GRID));
    grid->SetDimensions(3, 2, 3, 2);
    const auto type = RegisterGridLayerType(doc, "Projected type");
    Check(type.ok, "grid type fixture");
    sync();
    const auto gridRef = doc.Reference(grid->GetID());
    doc.History().Clear();
    Check(client.Command("setup.grid-add-layer",
                         {ObjectArgument("target", gridRef), IntegerArgument("type", type.created),
                          IntegerArgument("document-revision", doc.ChangeRevision())})
                      .status == EDITOR_OK &&
              grid->GetLayerCount() == 1 && doc.History().UndoCount() == 1,
          "registered grid action");
    sync();
    SetupDetails gridPage;
    Check(ReadSetupDetails(client, gridRef, "grid", gridPage) && gridPage.Values().Rows("layers").size() == 1 &&
              SetupFields(gridPage.Values().Rows("layers").front().fields).Integers("squares").size() == 6,
          "grid cell projection");
    Check(doc.History().Undo() && grid->GetLayerCount() == 0, "grid action Undo");
    sync();
    Check(client.Command("setup.grid-resize",
                         {ObjectArgument("target", gridRef), IntegerArgument("side", 4), IntegerArgument("add", 1),
                          IntegerArgument("document-revision", doc.ChangeRevision())})
                  .status == EDITOR_INVALID,
          "invalid grid side");
    auto stale = doc.Reference(material->GetID());
    Check(doc.Context()->DestroyObject(material, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK,
          "retire page target");
    create(CKCID_MATERIAL, "New lifetime");
    sync();
    Check(!ReadSetupDetails(client, stale, "material", meshPage), "old class target followed reused ID");
    std::string error;
    Check(doc.New(&error), error.c_str());
    sync();
    Check(!ReadSetupDetails(client, saved.target, "entity3d", entityPage), "class query crossed document switch");
}

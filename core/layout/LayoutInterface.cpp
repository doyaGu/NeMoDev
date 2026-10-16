#include "core/layout/LayoutInterface.h"

#include "core/document/Document.h"
#include "core/runtime/SdkCompat.h"

namespace vdev {

namespace {

constexpr CKDWORD VERSION_IDENTIFIER = 0xC0DE, PLACEMENT_IDENTIFIER = 0xC0DF;
constexpr CKDWORD DATA_IDENTIFIER = 0xF0F0AAAA, LAST_IDENTIFIER = 0xF0F0AAAC;

CKInterfaceObjectManager *LayoutManager(CKContext *context) {
    const XObjectPointerArray &list = context->GetObjectListByType(CKCID_INTERFACEOBJECTMANAGER, TRUE);
    for (int i = 0; i < list.Size(); ++i)
        if (CKInterfaceObjectManager *manager = InterfaceObjectManagerOf(list[i]))
            if (manager->GetGuid() == LAYOUT_VIEW_GUID) return manager;
    return nullptr;
}

// The view's own chunk: the one with the data identifier (the placement
// chunk has the version identifier too).
CKStateChunk *DataChunk(CKInterfaceObjectManager *manager) {
    for (int i = 0; manager && i < manager->GetChunkCount(); ++i)
        if (CKStateChunk *chunk = manager->GetChunk(i)) {
            chunk->StartRead();
            if (chunk->SeekIdentifier(DATA_IDENTIFIER)) return chunk;
        }
    return nullptr;
}

std::vector<CK_ID> ReadArray(CKStateChunk *chunk) {
    std::vector<CK_ID> ids;
    CKObjectArray *array = CreateCKObjectArray();
    chunk->ReadObjectArray(array);
    for (array->Reset(); !array->EndOfList(); array->Next()) ids.push_back(array->GetDataId());
    DeleteCKObjectArray(array);
    return ids;
}

void WriteArray(CKStateChunk *chunk, CKContext *context, const std::vector<CK_ID> &ids) {
    CKObjectArray *array = CreateCKObjectArray();
    for (CK_ID id : ids)
        if (context->GetObject(id)) array->InsertRear(id);
    chunk->WriteObjectArray(array, context);
    DeleteCKObjectArray(array);
}

} // namespace

bool LayoutSetupGuid(CKObject *object, CKGUID &guid) {
    if (!object) return false;
    const CK_CLASSID cid = object->GetClassID();
    // (0x023E7DEE, 0x46053B01 + n), Sound and Grid apart.
    int setup = -1;
    if (cid == CKCID_LEVEL) setup = 0x0E;
    else if (CKIsChildClassOf(cid, CKCID_SCENE)) setup = 0x0D;
    else if (CKIsChildClassOf(cid, CKCID_GRID)) {
        guid = CKGUID(0x75F150EF, 0x085E607B);
        return true;
    } else if (CKIsChildClassOf(cid, CKCID_WAVESOUND)) {
        guid = CKGUID(0x5D9D6874, 0x4E6C1401);
        return true;
    } else if (CKIsChildClassOf(cid, CKCID_CAMERA)) setup = 0x01;
    else if (CKIsChildClassOf(cid, CKCID_LIGHT)) setup = 0x02;
    else if (CKIsChildClassOf(cid, CKCID_CURVE)) setup = 0x03;
    else if (CKIsChildClassOf(cid, CKCID_SPRITE3D)) setup = 0x09;
    else if (CKIsChildClassOf(cid, CKCID_3DENTITY)) {
        // 3D Frame Setup for frames, portals and places, else 3D Object Setup.
        const CKDWORD flags = CK3dEntity::Cast(object)->GetFlags();
        const bool frame = CKIsChildClassOf(cid, CKCID_PLACE) ||
                           ((cid == CKCID_3DENTITY || cid == CKCID_3DOBJECT) && (flags & (CK_3DENTITY_FRAME | CK_3DENTITY_PORTAL)));
        setup = frame ? 0x04 : 0x05;
    } else if (CKIsChildClassOf(cid, CKCID_2DENTITY)) setup = 0x0A;
    else if (CKIsChildClassOf(cid, CKCID_TEXTURE)) setup = 0x06;
    else if (CKIsChildClassOf(cid, CKCID_MESH)) setup = 0x07;
    else if (CKIsChildClassOf(cid, CKCID_MATERIAL)) setup = 0x08;
    else if (CKIsChildClassOf(cid, CKCID_DATAARRAY)) setup = 0x0B;
    if (setup < 0) return false;
    guid = CKGUID(0x023E7DEE, 0x46053B00 + setup);
    return true;
}

LayoutInterfaceData ReadLayoutInterfaceData(CKContext *context) {
    LayoutInterfaceData data;
    CKStateChunk *chunk = context ? DataChunk(LayoutManager(context)) : nullptr;
    if (!chunk) return data;
    // sub_4B46C0, in its order.
    chunk->StartRead();
    int version = 0;
    if (chunk->SeekIdentifier(VERSION_IDENTIFIER)) version = chunk->ReadInt();
    if (!chunk->SeekIdentifier(DATA_IDENTIFIER)) return data;
    data.present = true;
    data.group = chunk->ReadObjectID();
    if (chunk->ReadInt()) data.selection = ReadArray(chunk);
    data.locked = chunk->ReadInt() != 0;
    data.lists[0] = ReadArray(chunk);
    data.lists[1] = ReadArray(chunk);
    if (version > 1) {
        data.flagged = ReadArray(chunk);
        void *buffer = nullptr;
        const int size = chunk->ReadBuffer(&buffer);
        if (buffer) {
            const CKDWORD *flags = static_cast<const CKDWORD *>(buffer);
            data.flags.assign(flags, flags + size / static_cast<int>(sizeof(CKDWORD)));
            CKDeletePointer(buffer);
        }
    } else {
        // Version 1: the objects flagged 0x40.
        data.flagged = ReadArray(chunk);
        data.flags.assign(data.flagged.size(), 0x40);
    }
    data.flags.resize(data.flagged.size(), 0);
    for (int i = 2; i < 6; ++i) data.lists[i] = ReadArray(chunk);
    const int cameras = chunk->ReadInt();
    for (int i = 0; i < cameras; ++i) data.cameras.push_back(chunk->ReadObjectID());
    const int setups = chunk->ReadInt();
    for (int i = 0; i < setups; ++i) {
        LayoutInterfaceData::Setup setup;
        setup.guid = chunk->ReadGuid();
        setup.object = chunk->ReadObjectID();
        setup.other = chunk->ReadObjectID();
        if (version >= 1)
            for (int &value : setup.values) value = chunk->ReadInt();
        data.setups.push_back(setup);
    }
    if (chunk->SeekIdentifier(LAST_IDENTIFIER)) data.last = ReadArray(chunk);
    return data;
}

void WriteLayoutInterfaceData(Document &document, const LayoutInterfaceData &data) {
    CKContext *context = document.Context();
    if (!context) return;
    CKInterfaceObjectManager *manager = LayoutManager(context);
    if (!manager) {
        // DoSaveObjects (sub_5CE470): the view's object with its placement
        // chunk (version 2, then 0, 0, 1 as in Dev's files; no window rectangle).
        manager = InterfaceObjectManagerOf(context->CreateObject(CKCID_INTERFACEOBJECTMANAGER));
        if (!manager) return;
        manager->SetGuid(LAYOUT_VIEW_GUID);
        CKStateChunk *placement = CreateCKStateChunk(1, nullptr);
        placement->StartWrite();
        placement->WriteIdentifier(VERSION_IDENTIFIER);
        placement->WriteInt(2);
        placement->WriteInt(0);
        placement->WriteInt(0);
        placement->WriteInt(1);
        placement->CloseChunk();
        manager->AddStateChunk(placement);
        document.Include(manager->GetID());
    }
    // sub_4B4E50, in its order (RemoveStateChunk deletes the former one).
    if (CKStateChunk *former = DataChunk(manager)) manager->RemoveStateChunk(former);
    CKStateChunk *chunk = CreateCKStateChunk(1, nullptr);
    chunk->StartWrite();
    chunk->WriteIdentifier(VERSION_IDENTIFIER);
    chunk->WriteInt(2);
    chunk->WriteIdentifier(DATA_IDENTIFIER);
    const CK_ID group = context->GetObject(data.group) ? data.group : 0;
    chunk->WriteObjectID(group);
    // The selection only without a group.
    std::vector<CK_ID> selection;
    if (!group)
        for (CK_ID id : data.selection)
            if (context->GetObject(id)) selection.push_back(id);
    chunk->WriteInt(static_cast<int>(selection.size()));
    if (!selection.empty()) WriteArray(chunk, context, selection);
    chunk->WriteInt(data.locked ? 1 : 0);
    WriteArray(chunk, context, data.lists[0]);
    WriteArray(chunk, context, data.lists[1]);
    std::vector<CK_ID> flagged;
    std::vector<CKDWORD> flags;
    for (size_t i = 0; i < data.flagged.size(); ++i)
        if (context->GetObject(data.flagged[i])) {
            flagged.push_back(data.flagged[i]);
            flags.push_back(i < data.flags.size() ? data.flags[i] : 0);
        }
    WriteArray(chunk, context, flagged);
    chunk->WriteBuffer(static_cast<int>(flags.size() * sizeof(CKDWORD)), flags.empty() ? nullptr : flags.data());
    for (int i = 2; i < 6; ++i) WriteArray(chunk, context, data.lists[i]);
    // Dev writes one camera.
    chunk->WriteInt(1);
    chunk->WriteObjectID(!data.cameras.empty() && context->GetObject(data.cameras.front()) ? data.cameras.front() : 0);
    chunk->WriteInt(static_cast<int>(data.setups.size()));
    for (const LayoutInterfaceData::Setup &setup : data.setups) {
        chunk->WriteGuid(setup.guid);
        chunk->WriteObjectID(context->GetObject(setup.object) ? setup.object : 0);
        chunk->WriteObjectID(context->GetObject(setup.other) ? setup.other : 0);
        for (int value : setup.values) chunk->WriteInt(value);
    }
    chunk->WriteIdentifier(LAST_IDENTIFIER);
    WriteArray(chunk, context, data.last);
    chunk->CloseChunk();
    manager->AddStateChunk(chunk);
}

LayoutLists ReadLayoutLists(CKContext *context, const LayoutInterfaceData &data) {
    LayoutLists lists;
    if (!context) return lists;
    auto alive = [&](CK_ID id) { return context->GetObject(id) != nullptr; };
    for (int list : {LAYOUT_CONE_LIGHTS, LAYOUT_CONE_CAMERAS})
        for (CK_ID id : data.lists[list])
            if (alive(id)) lists.cones.insert(id);
    for (int list : {LAYOUT_PLAYER_CAMERAS, LAYOUT_PLAYER_LIGHTS, LAYOUT_PLAYER_CURVES})
        for (CK_ID id : data.lists[list])
            if (alive(id)) lists.playerShown.insert(id);
    for (CK_ID id : data.last)
        if (alive(id)) lists.playerShown.insert(id);
    for (size_t i = 0; i < data.flagged.size() && i < data.flags.size(); ++i) {
        const CK_ID id = data.flagged[i];
        if (!alive(id)) continue;
        const int representation = static_cast<int>(data.flags[i] & FRAME_REPRESENTATION);
        if (representation != 1) lists.representations[id] = representation;
        if (data.flags[i] & FRAME_PLAYER_SHOWN) lists.playerShown.insert(id);
    }
    // A composition saved with the 3D Layout's data: the curves +180 does not
    // list are drawn without their control points (Dev.exe sub_4584A0 183).
    if (data.present) {
        const std::set<CK_ID> drawn(data.lists[LAYOUT_DRAWN_CURVES].begin(), data.lists[LAYOUT_DRAWN_CURVES].end());
        const XObjectPointerArray &curves = context->GetObjectListByType(CKCID_CURVE, TRUE);
        for (int i = 0; i < curves.Size(); ++i)
            if (curves[i] && !curves[i]->IsPrivate() && !drawn.count(curves[i]->GetID()))
                lists.hiddenCurvePoints.insert(curves[i]->GetID());
    }
    return lists;
}

void WriteLayoutLists(CKContext *context, const LayoutLists &lists, const std::function<bool(CK_ID)> &live,
                      LayoutInterfaceData &data) {
    for (int list : {LAYOUT_CONE_LIGHTS, LAYOUT_CONE_CAMERAS, LAYOUT_PLAYER_CAMERAS, LAYOUT_PLAYER_LIGHTS, LAYOUT_PLAYER_CURVES})
        data.lists[list].clear();
    data.last.clear();
    std::map<CK_ID, CKDWORD> previous;
    for (size_t i = 0; i < data.flagged.size() && i < data.flags.size(); ++i) previous[data.flagged[i]] = data.flags[i];
    data.flagged.clear();
    data.flags.clear();
    if (!context) return;
    auto saved = [&](CK_ID id) { return context->GetObject(id) && (!live || live(id)); };
    for (CK_ID id : lists.cones) {
        if (!saved(id)) continue;
        CKObject *object = context->GetObject(id);
        if (CKLight::Cast(object)) data.lists[LAYOUT_CONE_LIGHTS].push_back(id);
        else if (CKCamera::Cast(object)) data.lists[LAYOUT_CONE_CAMERAS].push_back(id);
    }
    for (CK_ID id : lists.playerShown) {
        if (!saved(id)) continue;
        CKObject *object = context->GetObject(id);
        if (CKCamera::Cast(object)) data.lists[LAYOUT_PLAYER_CAMERAS].push_back(id);
        else if (CKLight::Cast(object)) data.lists[LAYOUT_PLAYER_LIGHTS].push_back(id);
        else if (CKCurve::Cast(object)) data.lists[LAYOUT_PLAYER_CURVES].push_back(id);
        else if (CK2dEntity::Cast(object)) data.last.push_back(id);
    }
    // +180, the curves Dev draws: every curve saved, those read first in
    // their order. The editor draws every curve; Dev draws only the listed
    // ones, so a curve made here is listed too (Dev lists it when it enters
    // a scene).
    std::vector<CK_ID> &drawn = data.lists[LAYOUT_DRAWN_CURVES];
    std::set<CK_ID> listed;
    std::vector<CK_ID> kept;
    for (CK_ID id : drawn)
        if (saved(id) && CKCurve::Cast(context->GetObject(id)) && !lists.hiddenCurvePoints.count(id) && listed.insert(id).second)
            kept.push_back(id);
    const XObjectPointerArray &curves = context->GetObjectListByType(CKCID_CURVE, TRUE);
    for (int i = 0; i < curves.Size(); ++i)
        if (curves[i] && !curves[i]->IsPrivate() && saved(curves[i]->GetID()) && !lists.hiddenCurvePoints.count(curves[i]->GetID()) &&
            listed.insert(curves[i]->GetID()).second)
            kept.push_back(curves[i]->GetID());
    drawn = kept;
    // Every frame (Dev.exe sub_49DC50).
    const XObjectPointerArray &entities = context->GetObjectListByType(CKCID_3DENTITY, FALSE);
    for (int i = 0; i < entities.Size(); ++i) {
        CK3dEntity *entity = CK3dEntity::Cast(entities[i]);
        if (!entity || !(entity->GetFlags() & CK_3DENTITY_FRAME) || !saved(entity->GetID())) continue;
        const CK_ID id = entity->GetID();
        auto found = previous.find(id);
        CKDWORD flags = found != previous.end() ? found->second & ~(FRAME_REPRESENTATION | FRAME_PLAYER_SHOWN) : 0;
        auto representation = lists.representations.find(id);
        flags |= representation != lists.representations.end()
                     ? static_cast<CKDWORD>(representation->second) & FRAME_REPRESENTATION
                     : 1;
        if (lists.playerShown.count(id)) flags |= FRAME_PLAYER_SHOWN;
        data.flagged.push_back(id);
        data.flags.push_back(flags);
    }
}

} // namespace vdev

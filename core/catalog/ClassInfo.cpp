#include "core/catalog/ClassInfo.h"
namespace vdev {
ClassIconKind ClassIconKindOf(int32_t cid) {
    if (cid == CKCID_LEVEL)
        return ClassIconKind::Level;
    if (cid == CKCID_PLACE)
        return ClassIconKind::Place;
    if (cid == CKCID_SCENE)
        return ClassIconKind::Scene;
    if (cid == CKCID_BEHAVIOR)
        return ClassIconKind::Script;
    if (cid && CKIsChildClassOf(cid, CKCID_2DENTITY))
        return ClassIconKind::Entity2D;
    if (cid && CKIsChildClassOf(cid, CKCID_3DENTITY))
        return ClassIconKind::Entity3D;
    if (cid == CKCID_MATERIAL)
        return ClassIconKind::Material;
    return ClassIconKind::Object;
}
int ClassIconImage(int32_t cid) {
    // Dev.exe sub_5D1F80 gives the class's index in the list LoadIcons fills
    // (resvis.dll icons 0x175, 0x176, 0xC3 to 0xDA, 0x178, 0x177, 0x17C, ...).
    static const int LIST[] = {0x175, 0x176, 0xC3,  0xC4,  0xC5,  0xC6,  0xC7,  0xC8,  0xC9,  0xCA,  0xCB,  0xCC,
                               0xCD,  0xCE,  0xCF,  0xD0,  0xD1,  0xD2,  0xD3,  0xD4,  0xD5,  0xD6,  0xD7,  0xD8,
                               0xD9,  0xDA,  0x178, 0x177, 0x17C, 0x17E, 0x17F, 0x180, 0x181, 0x182, 0x183, 0x186,
                               0x185, 0x184, 0x188, 0x189, 0x20E, 0x214, 0x215, 0x52C, 0x3B5, 0x3B8, 0x3B6, 0x3B9,
                               0x216, 0x3B7, 0x3C3, 0x3C1, 0x3C0, 0x3C2, 0x3C4, 0x3C6, 0x3C5};
    int index = 20;
    switch (cid) {
    case CKCID_BEHAVIOR: index = 40; break;
    case CKCID_SCENE: index = 3; break;
    case CKCID_OBJECTANIMATION:
    case CKCID_ANIMATION:
    case CKCID_KEYEDANIMATION: index = 30; break;
    case CKCID_LEVEL: index = 2; break;
    case CKCID_PLACE: index = 4; break;
    case CKCID_GROUP: index = 31; break;
    case CKCID_SOUND:
    case CKCID_WAVESOUND:
    case CKCID_MIDISOUND: index = 29; break;
    case CKCID_2DENTITY:
    case CKCID_SPRITE:
    case CKCID_SPRITETEXT: index = 13; break;
    case CKCID_MATERIAL: index = 11; break;
    case CKCID_TEXTURE: index = 12; break;
    case CKCID_MESH:
    case CKCID_PATCHMESH: index = 6; break;
    case CKCID_3DENTITY: index = 10; break;
    case CKCID_CAMERA:
    case CKCID_TARGETCAMERA: index = 8; break;
    case CKCID_SPRITE3D: index = 14; break;
    case CKCID_LIGHT:
    case CKCID_TARGETLIGHT: index = 5; break;
    case CKCID_CHARACTER: index = 15; break;
    case CKCID_3DOBJECT: index = 21; break;
    case CKCID_BODYPART: index = 48; break;
    case CKCID_CURVE: index = 22; break;
    case CKCID_GRID: index = 55; break;
    case CKCID_DATAARRAY: index = 56; break;
    default: break;
    }
    return LIST[index];
}


}

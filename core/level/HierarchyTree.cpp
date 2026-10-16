#include "core/level/HierarchyTree.h"

#include <algorithm>

namespace vdev {

namespace {

bool Listed(CKObject *object, const std::function<bool(CK_ID)> &skip) {
    if (!object || object->IsPrivate() || object->IsDynamic() || CKIsChildClassOf(object, CKCID_CURVEPOINT)) return false;
    return !skip || !skip(object->GetID());
}

HierarchyItem ItemOf(CKObject *object) {
    HierarchyItem item;
    item.object = object->GetID();
    item.cid = object->GetClassID();
    item.name = object->GetName() ? object->GetName() : "";
    item.renderObject = CKIsChildClassOf(object, CKCID_RENDEROBJECT) != FALSE;
    item.resource = CKIsChildClassOf(object, CKCID_MESH) || CKIsChildClassOf(object, CKCID_MATERIAL) ||
                    CKIsChildClassOf(object, CKCID_TEXTURE);
    return item;
}

// Dev.exe sub_5778A0: the items with the tree's 0x400 flag first (Dev sets
// it on two Level Manager folders only, none here), then by name,
// case-insensitive, the unnamed last.
void Sort(std::vector<HierarchyItem> &items) {
    std::stable_sort(items.begin(), items.end(), [](const HierarchyItem &a, const HierarchyItem &b) {
        if (a.name.empty() || b.name.empty()) return !a.name.empty() && b.name.empty();
        return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

struct Builder {
    const HierarchyOptions &options;
    const std::function<bool(CK_ID)> &skip;

    void AddMaterial(HierarchyItem &parent, CKMaterial *material) const {
        if (!options.materials || !Listed(material, skip)) return;
        HierarchyItem item = ItemOf(material);
        if (options.textures)
            if (CKTexture *texture = material->GetTexture(0))
                if (Listed(texture, skip)) item.children.push_back(ItemOf(texture));
        parent.children.push_back(std::move(item));
    }

    HierarchyItem Add(CKObject *object) const {
        HierarchyItem item = ItemOf(object);
        if (CK3dEntity *entity = CK3dEntity::Cast(object)) {
            for (int i = 0; i < entity->GetChildrenCount(); ++i)
                if (CK3dEntity *child = entity->GetChild(i))
                    if (Listed(child, skip)) item.children.push_back(Add(child));
            if (options.meshes)
                for (int i = 0; i < entity->GetMeshCount(); ++i)
                    if (CKMesh *mesh = entity->GetMesh(i)) {
                        if (!Listed(mesh, skip)) continue;
                        HierarchyItem meshItem = ItemOf(mesh);
                        for (int m = 0; m < mesh->GetMaterialCount(); ++m) AddMaterial(meshItem, mesh->GetMaterial(m));
                        Sort(meshItem.children);
                        item.children.push_back(std::move(meshItem));
                    }
            if (CKSprite3D *sprite = CKSprite3D::Cast(object)) AddMaterial(item, sprite->GetMaterial());
        } else if (CK2dEntity *entity = CK2dEntity::Cast(object)) {
            for (int i = 0; i < entity->GetChildrenCount(); ++i)
                if (CK2dEntity *child = entity->GetChild(i))
                    if (Listed(child, skip)) item.children.push_back(Add(child));
            AddMaterial(item, entity->GetMaterial());
        }
        Sort(item.children);
        return item;
    }
};

HierarchyItem Root(int root, const char *name, CK_CLASSID cid) {
    HierarchyItem item;
    item.root = root;
    item.name = name;
    item.cid = cid;
    return item;
}

} // namespace

std::vector<HierarchyItem> BuildHierarchy(CKContext *context, const HierarchyOptions &options,
                                          const std::function<bool(CK_ID)> &skip) {
    std::vector<HierarchyItem> roots;
    roots.push_back(Root(HierarchyItem::BACKGROUND_2D, "2D Background Root", CKCID_2DENTITY));
    roots.push_back(Root(HierarchyItem::FOREGROUND_2D, "2D Foreground Root", CKCID_2DENTITY));
    roots.push_back(Root(HierarchyItem::ROOT_3D, "3D Root", CKCID_3DENTITY));
    if (!context) return roots;
    const Builder builder{options, skip};
    CKScene *scene = context->GetCurrentScene();
    if (!scene && context->GetCurrentLevel()) scene = context->GetCurrentLevel()->GetLevelScene();
    const XObjectPointerArray &entities3d = context->GetObjectListByType(CKCID_3DENTITY, TRUE);
    for (int i = 0; i < entities3d.Size(); ++i) {
        CK3dEntity *entity = CK3dEntity::Cast(entities3d[i]);
        if (!entity || entity->GetParent() || !Listed(entity, skip)) continue;
        if (scene && !entity->IsInScene(scene)) continue;
        roots[HierarchyItem::ROOT_3D].children.push_back(builder.Add(entity));
    }
    const XObjectPointerArray &entities2d = context->GetObjectListByType(CKCID_2DENTITY, TRUE);
    for (int i = 0; i < entities2d.Size(); ++i) {
        CK2dEntity *entity = CK2dEntity::Cast(entities2d[i]);
        if (!entity || entity->GetParent() || !Listed(entity, skip)) continue;
        roots[entity->IsBackground() ? HierarchyItem::BACKGROUND_2D : HierarchyItem::FOREGROUND_2D].children.push_back(
            builder.Add(entity));
    }
    for (HierarchyItem &root : roots) Sort(root.children);
    return roots;
}

std::vector<CK_ID> HierarchyOf(CKContext *context, CK_ID id) {
    std::vector<CK_ID> objects;
    CKObject *object = context ? context->GetObject(id) : nullptr;
    if (!object) return objects;
    std::vector<CKObject *> pending(1, object);
    while (!pending.empty()) {
        CKObject *current = pending.back();
        pending.pop_back();
        objects.push_back(current->GetID());
        std::vector<CKObject *> children;
        if (CK3dEntity *entity = CK3dEntity::Cast(current))
            for (int i = 0; i < entity->GetChildrenCount(); ++i) children.push_back(entity->GetChild(i));
        else if (CK2dEntity *entity2d = CK2dEntity::Cast(current))
            for (int i = 0; i < entity2d->GetChildrenCount(); ++i) children.push_back(entity2d->GetChild(i));
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            if (*it) pending.push_back(*it);
    }
    return objects;
}

} // namespace vdev

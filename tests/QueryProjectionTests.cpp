#include "VirtoolsDevTestSupport.h"

#include "core/level/AttributeQueries.h"
#include "core/level/ObjectQueries.h"

namespace vdevtest {
namespace {
const HierarchyItem *Find(const std::vector<HierarchyItem> &items, CK_ID id) {
    for (const auto &item : items) {
        if (item.object == id) return &item;
        if (auto *found = Find(item.children, id)) return found;
    }
    return nullptr;
}
bool Listed(const AttributeCatalog &catalog, CKAttributeType type) {
    for (const auto &category : catalog.categories)
        if (std::find(category.types.begin(), category.types.end(), type) != category.types.end()) return true;
    return false;
}
void Bury(Document &document, CK_ID id) {
    Edit edit(document, "Remove query fixture");
    edit.Bury(id);
    CHECK(edit.Commit().ok);
}
} // namespace

void ObjectQueryProjections(CkRuntimeHost &host) {
    Document document(host);
    CHECK(!DescribeObject(document, 0).id && FilterObjects(document, {0}).empty());
    CHECK(document.New(nullptr));
    auto created = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(created.ok);
    const auto id = created.created;
    CHECK(RenameLevelObject(document, id, "Read query fixture").ok);
    CHECK(SetRenderPriority(document, {id}, 31).ok);
    document.MarkSaved();
    document.History().MarkSavePoint();
    const auto count = document.History().UndoCount();
    const auto outside = document.ChangesOutsideUndo();
    const auto before = DescribeObject(document, id);
    CHECK(before.id == id && before.name == "Read query fixture" && before.renderObject && before.renderPriority == 31);
    CHECK(FilterObjects(document, {0, id, 0xFFFFFFFF}, CKCID_RENDEROBJECT) == std::vector<CK_ID>{id});
    CHECK(FilterObjects(document, {id}, CKCID_TEXTURE).empty());
    CHECK(!document.Dirty() && count == document.History().UndoCount() && outside == document.ChangesOutsideUndo());
    CHECK(RenameLevelObject(document, id, "Changed name").ok && SetRenderPriority(document, {id}, -8).ok);
    CHECK(DescribeObject(document, id).renderPriority == -8 && DescribeObject(document, id).name == "Changed name");
    CHECK(before.name == "Read query fixture" && before.renderPriority == 31);
    CHECK(document.History().Undo() && document.History().Undo());
    CHECK(!document.Dirty() && DescribeObject(document, id).name == before.name);
    Bury(document, id);
    CHECK(document.Context()->GetObject(id) && document.IsDiscarded(id));
    CHECK(!DescribeObject(document, id).id && FilterObjects(document, {id}).empty());
    CHECK(FilterObjects(document, {id}, CKCID_RENDEROBJECT, ObjectQueryScope::Existing) == std::vector<CK_ID>{id});
    CHECK(document.History().Undo() && DescribeObject(document, id).id == id);
    CHECK(document.History().Redo() && FilterObjects(document, {id}).empty());
    document.History().Clear();
    CHECK(!document.Context()->GetObject(id));
    CHECK(FilterObjects(document, {id}, CKCID_RENDEROBJECT, ObjectQueryScope::Existing).empty());
    document.Close();
    CHECK(!DescribeObject(document, id).id && before.name == "Read query fixture" && before.renderPriority == 31);
}

void HierarchyQueryCommands(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    const auto a = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto b = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto c = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(a.ok && b.ok && c.ok);
    CHECK(SetHierarchyParent(document, b.created, a.created).ok);
    CHECK(SetRenderPriority(document, {b.created}, 47).ok);
    const auto roots = BuildHierarchy(document, {});
    const auto *row = Find(roots, b.created);
    CHECK(row && row->renderObject && row->renderPriority == 47);
    CHECK(HierarchyOf(document, a.created) == std::vector<CK_ID>({a.created, b.created}));
    const auto error = HierarchyDragError(document, {a.created, b.created});
    CHECK(!error.empty());
    const auto history = document.History().UndoCount();
    const auto refused = SetHierarchyParents(document, {a.created, b.created}, c.created);
    CHECK(refused.size() == 2 && !refused[0].ok && refused[0].message == error);
    CHECK(history == document.History().UndoCount());
    CHECK(HierarchyDragError(document, {0xFFFFFFFF}).size() > 0);
    CHECK(HierarchyDragError(document, {b.created}).empty());
    CHECK(SetHierarchyParents(document, {b.created}, c.created).front().ok);
    CHECK(HierarchyOf(document, a.created) == std::vector<CK_ID>{a.created});
    CHECK(HierarchyOf(document, c.created) == std::vector<CK_ID>({c.created, b.created}));
    CHECK(document.History().Undo());
    CHECK(HierarchyOf(document, a.created) == std::vector<CK_ID>({a.created, b.created}));
    Bury(document, b.created);
    CHECK(!Find(BuildHierarchy(document, {}), b.created));
    CHECK(HierarchyOf(document, a.created) == std::vector<CK_ID>{a.created});
    CHECK(!HierarchyDragError(document, {b.created}).empty());
    CHECK(document.History().Undo() && Find(BuildHierarchy(document, {}), b.created));
    // The projection has no native resource ownership or lazy getters.
    document.Close();
    CHECK(row->object == b.created && row->renderPriority == 47);
    CHECK(!Find(BuildHierarchy(document, {}), b.created));
    CHECK(HierarchyOf(document, a.created).empty());
}

void AttributeQueryProjections(CkRuntimeHost &host) {
    Document document(host);
    CHECK(!ReadAttributeCatalog(document).available && ReadAttributeObjects(document, {0}).empty());
    CHECK(document.New(nullptr));
    CHECK(CreateAttributeCategory(document, "Query Attributes").ok);
    const auto made = CreateAttributeType(document, "Query Value", CKPGUID_INT, "Query Attributes");
    const auto marker = CreateAttributeType(document, "Query Marker", CKGUID(0, 0), "Query Attributes");
    const auto lightOnly = CreateAttributeType(document, "Query Light", CKPGUID_FLOAT, "Query Attributes", CKCID_LIGHT);
    CHECK(made.ok && marker.ok && lightOnly.ok);
    const auto type = static_cast<CKAttributeType>(made.created);
    auto *manager = document.Context()->GetAttributeManager();
    const auto hidden = manager->RegisterNewAttributeType((CKSTRING) "Query Hidden", CKPGUID_INT, CKCID_BEOBJECT, CK_ATTRIBUT_HIDDEN);
    CHECK(hidden >= 0);
    const auto a = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto b = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto light = CreateLayoutObject(document, LayoutObject::Light);
    CHECK(a.ok && b.ok && light.ok);
    CHECK(AddAttribute(document, {a.created, b.created}, type).ok);
    CHECK(AddAttribute(document, {a.created}, static_cast<CKAttributeType>(marker.created)).ok);
    CHECK(SetAttributeText(document, a.created, type, "42").ok);
    const auto retained = ReadAttributeCatalog(document);
    CHECK(retained.available && retained.Find(type) && retained.Find(type)->name == "Query Value");
    CHECK(Listed(retained, type) && !Listed(retained, hidden));
    CHECK(!Listed(ReadAttributeCatalog(document, {a.created}), static_cast<CKAttributeType>(lightOnly.created)));
    CHECK(Listed(ReadAttributeCatalog(document, {a.created, light.created}), static_cast<CKAttributeType>(lightOnly.created)));
    CHECK(!Listed(ReadAttributeCatalog(document, {0xFFFFFFFF}), type));
    const auto rows = ReadAttributeObjects(document, {type, static_cast<CKAttributeType>(marker.created)}, {a.created});
    CHECK(rows.size() == 2 && rows[0].object == a.created && rows[0].parameter && rows[0].value == "42");
    CHECK(rows[1].object == a.created && !rows[1].parameter);
    const auto ordered = ReadAttributeObjects(document, {type}, {b.created, a.created});
    CHECK(ordered.size() == 2 && ordered[0].object == b.created && ordered[1].object == a.created);
    document.MarkSaved();
    document.History().MarkSavePoint();
    const auto count = document.History().UndoCount();
    const auto outside = document.ChangesOutsideUndo();
    CHECK(ReadAttributeCatalog(document).Find(type));
    CHECK(ReadAttributeObjects(document, {type}).size() == 2);
    CHECK(!document.Dirty() && count == document.History().UndoCount() && outside == document.ChangesOutsideUndo());
    CHECK(SetAttributeText(document, a.created, type, "73").ok);
    CHECK(rows[0].value == "42" && ReadAttributeObjects(document, {type}, {a.created}).front().value == "73");
    CHECK(document.History().Undo() && !document.Dirty());
    CHECK(RemoveAttributeFromObjects(document, {a.created}, type).ok);
    CHECK(ReadAttributeObjects(document, {type}, {a.created}).empty());
    CHECK(document.History().Undo());
    CHECK(ReadAttributeObjects(document, {type}, {a.created}).front().value == "42");
    // Retained parameters stay usable as copy sources, while their owner
    // must disappear from queries for visible objects and attribute rows.
    const auto copied = ReadAttributeObjects(document, {type}, {a.created}).front();
    auto pin = document.RetainObjects({copied.parameter});
    Bury(document, a.created);
    CHECK(ReadAttributeObjects(document, {type}, {a.created}).empty());
    CHECK(FilterObjects(document, {copied.parameter}).empty());
    CHECK(FilterObjects(document, {copied.parameter}, CKCID_PARAMETER, ObjectQueryScope::Existing).size() == 1);
    CHECK(PasteAttributeValue(document, copied.parameter, {b.created}, type).ok);
    CHECK(ReadAttributeObjects(document, {type}, {b.created}).front().value == "42");
    CHECK(document.History().Undo());
    CHECK(document.History().Undo());
    document.Context()->GetObject(b.created)->ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    CHECK(ReadAttributeObjects(document, {type}).size() == 1);
    document.Context()->GetObject(b.created)->ModifyObjectFlags(0, CK_OBJECT_DYNAMIC);
    CHECK(RenameAttributeType(document, type, "Renamed query attribute").ok);
    CHECK(retained.Find(type)->name == "Query Value");
    CHECK(ReadAttributeCatalog(document).Find(type)->name == "Renamed query attribute");
    CHECK(DeleteAttributeType(document, type).ok);
    CHECK(!ReadAttributeCatalog(document).Find(type) && ReadAttributeObjects(document, {type}).empty());
    document.Close();
    CHECK(!ReadAttributeCatalog(document).available && ListParameterTypes(document).empty());
    CHECK(retained.Find(type)->name == "Query Value" && rows[0].value == "42");
}
} // namespace vdevtest

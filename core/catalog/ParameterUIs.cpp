#include "core/catalog/ParameterUIs.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace vdev {

namespace {

struct Entry {
    CKGUID guid;
    ParameterUI ui;
    int dialog, width, height, pick;
    CK_CLASSID pickClass;
    bool expand;
};

// CK2UI.dll's choices by GUID (sub_24E52360) and the arguments its creators
// give CreateParameterDialog (template, pick modes, class, expand), with the
// templates' sizes.
const Entry EXACT[] = {
    {CKPGUID_FLOAT, ParameterUI::Number, 2001, 76, 14, 1, 0, false},
    {CKPGUID_PERCENTAGE, ParameterUI::Number, 2001, 76, 14, 1, 0, false},
    {CKPGUID_ANGLE, ParameterUI::Angle, 154, 216, 18, 1, 0, false},
    {CKPGUID_INT, ParameterUI::Number, 2001, 76, 14, 1, 0, false},
    {CKPGUID_TIME, ParameterUI::Time, 2002, 171, 14, 1, 0, false},
    {CKPGUID_KEY, ParameterUI::Key, 157, 146, 14, 1, 0, false},
    {CKPGUID_BOOL, ParameterUI::Bool, 138, 21, 14, 0, 0, false},
    {CKPGUID_STRING, ParameterUI::String, 135, 231, 14, 1, 0, true},
    {CKPGUID_VECTOR, ParameterUI::Vector, 2004, 216, 14, 3, CKCID_3DENTITY, false},
    {CKPGUID_QUATERNION, ParameterUI::Quaternion, 2008, 286, 14, 3, CKCID_3DENTITY, false},
    {CKPGUID_EULERANGLES, ParameterUI::EulerAngles, 154, 216, 18, 3, CKCID_3DENTITY, false},
    {CKPGUID_2DVECTOR, ParameterUI::Vector2D, 134, 146, 14, 3, CKCID_2DENTITY, false},
    {CKPGUID_MATRIX, ParameterUI::Matrix, 159, 336, 57, 2, CKCID_3DENTITY, true},
    {CKPGUID_COLOR, ParameterUI::Color, 2005, 197, 14, 1, CKCID_MATERIAL, false},
    {CKPGUID_BOX, ParameterUI::Box, 143, 269, 28, 3, CKCID_3DENTITY, false},
    {CKPGUID_RECT, ParameterUI::Rect, 2003, 213, 28, 3, CKCID_2DENTITY, true},
    {CKPGUID_SCRIPT, ParameterUI::Script, 158, 216, 28, 2, CKCID_BEHAVIOR, false},
    {CKPGUID_MESSAGE, ParameterUI::Message, 140, 216, 14, 0, 0, false},
    {CKPGUID_ATTRIBUTE, ParameterUI::Attribute, 155, 221, 28, 1, 0, false},
    {CKPGUID_2DCURVE, ParameterUI::Curve2D, 102, 213, 169, 1, 0, true},
};

// The object types CK2UI gives its Object UI by GUID.
const CKGUID OBJECTS[] = {CKPGUID_OBJECT,     CKPGUID_BEOBJECT,    CKPGUID_BEHAVIOR,     CKPGUID_MATERIAL,
                          CKPGUID_RENDEROBJECT, CKPGUID_MESH,      CKPGUID_DATAARRAY,    CKPGUID_TEXTURE,
                          CKPGUID_SPRITE,     CKPGUID_3DENTITY,    CKPGUID_CURVEPOINT,   CKPGUID_LIGHT,
                          CKPGUID_TARGETLIGHT, CKPGUID_CAMERA,     CKPGUID_TARGETCAMERA, CKPGUID_SPRITE3D,
                          CKPGUID_OBJECT3D,   CKPGUID_BODYPART,    CKPGUID_CHARACTER,    CKPGUID_CURVE,
                          CKPGUID_LEVEL,      CKPGUID_PLACE,       CKPGUID_GROUP,        CKPGUID_2DENTITY,
                          CKPGUID_SPRITETEXT, CKPGUID_WAVESOUND,   CKPGUID_SOUND,        CKPGUID_MIDISOUND};

// Then these GUIDs, in CK2UI's order (sub_24E52FD0 compares them; a type
// derived from one and not listed takes its UI after).
struct Derived {
    CKGUID parent;
    ParameterUI ui;
    int dialog, width, height, pick;
    bool expand;
};
const Derived DERIVED[] = {
    {CKPGUID_OBJECTANIMATION, ParameterUI::Animation, 2006, 216, 14, 1, false},
    {CKPGUID_ANIMATION, ParameterUI::Animation, 2006, 216, 14, 1, false},
    {CKPGUID_KINEMATICCHAIN, ParameterUI::Object, 2011, 219, 43, 2, true},
    {CKPGUID_SCENE, ParameterUI::Object, 2011, 219, 43, 2, true},
    {CKPGUID_SYNCHRO, ParameterUI::Synchro, 148, 216, 28, 1, false},
    {CKPGUID_CRITICALSECTION, ParameterUI::Synchro, 148, 216, 28, 1, false},
    {CKPGUID_STATE, ParameterUI::Synchro, 148, 216, 28, 1, false},
    {CKPGUID_PATCHMESH, ParameterUI::Object, 2011, 219, 43, 2, true},
    {CKPGUID_ENUMS, ParameterUI::Enum, 152, 216, 14, 1, false},
    {CKPGUID_STRUCTS, ParameterUI::Struct, 151, 223, 16, 0, false},
    {CKPGUID_FLAGS, ParameterUI::Flags, 2007, 169, 164, 1, true},
    {CKPGUID_COPYDEPENDENCIES, ParameterUI::Dependencies, 156, 296, 196, 0, false},
    {CKPGUID_DELETEDEPENDENCIES, ParameterUI::Dependencies, 156, 296, 196, 0, false},
    {CKPGUID_SAVEDEPENDENCIES, ParameterUI::Dependencies, 156, 296, 196, 0, false},
    {CKPGUID_REPLACEDEPENDENCIES, ParameterUI::Dependencies, 156, 296, 196, 0, false},
    {CKPGUID_OBJECTARRAY, ParameterUI::ObjectArray, 2010, 216, 105, 0, false},
    {CKGUID(0x2b2d2f43, 0x02ab7bc5), ParameterUI::LodOptions, 2012, 231, 55, 0, false}, // LODManager.h CKPGUID_LODOPTIONS
};

// guid or one of the types it derives from is parent (the descriptions'
// DerivedFrom chain; CKParameterManager::IsDerivedFrom reads derivation
// masks that types registered later can leave stale).
bool DerivesFrom(CKParameterManager *manager, CKGUID guid, const CKGUID &parent) {
    for (int depth = 0; depth < 32 && (guid.d1 || guid.d2); ++depth) {
        if (guid == parent) return true;
        CKParameterTypeDesc *description = manager->GetParameterTypeDescription(manager->ParameterGuidToType(guid));
        if (!description || description->DerivedFrom == guid) return false;
        guid = description->DerivedFrom;
    }
    return false;
}

std::string Lower(std::string text) {
    for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

void SortChoices(ObjectChoices &choices) {
    std::stable_sort(choices.begin(), choices.end(), [](const std::pair<CK_ID, std::string> &a,
                                                        const std::pair<CK_ID, std::string> &b) {
        return _stricmp(a.second.c_str(), b.second.c_str()) < 0;
    });
}

} // namespace

ParameterUIInfo ParameterUIOf(CKParameterManager *manager, CKParameterType type) {
    ParameterUIInfo info;
    CKParameterTypeDesc *description = manager && type >= 0 ? manager->GetParameterTypeDescription(type) : nullptr;
    if (!description) return info;
    // The flags, enumerations and structures first.
    if (description->dwFlags & CKPARAMETERTYPE_FLAGS) return {ParameterUI::Flags, 2007, 169, 164, 1, 0, true};
    if (description->dwFlags & CKPARAMETERTYPE_ENUMS) return {ParameterUI::Enum, 152, 216, 14, 1, 0, false};
    if (description->dwFlags & CKPARAMETERTYPE_STRUCT) return {ParameterUI::Struct, 151, 223, 16, 0, 0, false};
    const CKGUID guid = description->Guid;
    for (const Entry &entry : EXACT)
        if (entry.guid == guid)
            return {entry.ui, entry.dialog, entry.width, entry.height, entry.pick, entry.pickClass, entry.expand};
    for (const CKGUID &object : OBJECTS)
        if (object == guid) {
            // Its class (sub_24E6C7E0); only the generic classes show the class filter.
            return {ParameterUI::Object, 2011, 219, 43, 2, manager->TypeToClassID(type), true};
        }
    for (int exact = 1; exact >= 0; --exact)
        for (const Derived &entry : DERIVED)
            if (exact ? entry.parent == guid : DerivesFrom(manager, guid, entry.parent)) {
                ParameterUIInfo derived = {entry.ui, entry.dialog, entry.width, entry.height, entry.pick, 0, entry.expand};
                if (entry.ui == ParameterUI::Object) derived.pickClass = manager->TypeToClassID(type);
                return derived;
            }
    return info;
}

ObjectChoices ObjectsOfClass(const Document &document, CK_CLASSID cid, bool derived, const std::string &filter) {
    ObjectChoices choices;
    CKContext *context = document.Context();
    if (!context) return choices;
    CKLevel *level = context->GetCurrentLevel();
    CKScene *levelScene = level ? level->GetLevelScene() : nullptr;
    const std::string wanted = Lower(filter);
    for (CKObject *object : document.LiveObjects(cid, derived)) {
        if (object->IsPrivate() || object == levelScene || !object->GetName()) continue;
        if (!wanted.empty() && Lower(object->GetName()).find(wanted) == std::string::npos) continue;
        choices.emplace_back(object->GetID(), object->GetName());
    }
    choices.emplace_back(0, "NULL");
    SortChoices(choices);
    return choices;
}

ObjectChoices ScriptOwners(const Document &document, CK_CLASSID cid) {
    ObjectChoices choices;
    CKContext *context = document.Context();
    if (!context || !CKIsChildClassOf(cid, CKCID_BEOBJECT)) return choices;
    CKLevel *level = context->GetCurrentLevel();
    CKScene *levelScene = level ? level->GetLevelScene() : nullptr;
    for (CKObject *each : document.LiveObjects(cid)) {
        CKBeObject *object = CKBeObject::Cast(each);
        if (!object || object->IsPrivate() || !object->GetScriptCount() || object == levelScene) continue;
        choices.emplace_back(object->GetID(), object->GetName() ? object->GetName() : "");
    }
    choices.emplace_back(0, "NULL");
    SortChoices(choices);
    return choices;
}

ObjectChoices ScriptsOf(const Document &document, CK_ID ownerId) {
    ObjectChoices choices;
    CKContext *context = document.Context();
    if (!context) return choices;
    auto listed = [&](CKBehavior *script) {
        return script && script->GetType() == CKBEHAVIORTYPE_SCRIPT && !document.IsDiscarded(script->GetID()) &&
               !(script->GetObjectFlags() & (CK_OBJECT_INTERFACEOBJ | CK_OBJECT_PRIVATE | CK_OBJECT_NOTTOBESAVED));
    };
    if (CKBeObject *owner = CKBeObject::Cast(context->GetObject(ownerId))) {
        for (int i = 0; i < owner->GetScriptCount(); ++i)
            if (CKBehavior *script = owner->GetScript(i))
                if (listed(script)) choices.emplace_back(script->GetID(), script->GetName() ? script->GetName() : "");
    } else {
        const int count = context->GetObjectsCountByClassID(CKCID_BEHAVIOR);
        CK_ID *ids = context->GetObjectsListByClassID(CKCID_BEHAVIOR);
        for (int i = 0; i < count; ++i) {
            CKBehavior *script = CKBehavior::Cast(context->GetObject(ids[i]));
            if (listed(script)) choices.emplace_back(script->GetID(), script->GetName() ? script->GetName() : "");
        }
        choices.emplace_back(0, "NULL");
    }
    SortChoices(choices);
    return choices;
}

std::vector<std::pair<CK_CLASSID, std::string>> DerivedClasses(CK_CLASSID cid) {
    std::vector<std::pair<CK_CLASSID, std::string>> classes;
    for (CK_CLASSID i = 0; i < 56; ++i)
        if (CKIsChildClassOf(i, cid))
            if (const char *name = CKClassIDToString(i)) classes.emplace_back(i, name);
    std::stable_sort(classes.begin(), classes.end(), [](const std::pair<CK_CLASSID, std::string> &a,
                                                        const std::pair<CK_CLASSID, std::string> &b) {
        return _stricmp(a.second.c_str(), b.second.c_str()) < 0;
    });
    return classes;
}

CK_DEPENDENCIES_OPMODE DependenciesModeOf(CKParameterManager *manager, CKParameterType type) {
    CKParameterTypeDesc *description = manager && type >= 0 ? manager->GetParameterTypeDescription(type) : nullptr;
    if (!description) return static_cast<CK_DEPENDENCIES_OPMODE>(0);
    const CKGUID guid = description->Guid;
    if (DerivesFrom(manager, guid, CKPGUID_COPYDEPENDENCIES)) return CK_DEPENDENCIES_COPY;
    if (DerivesFrom(manager, guid, CKPGUID_DELETEDEPENDENCIES)) return CK_DEPENDENCIES_DELETE;
    if (DerivesFrom(manager, guid, CKPGUID_SAVEDEPENDENCIES)) return CK_DEPENDENCIES_SAVE;
    if (DerivesFrom(manager, guid, CKPGUID_REPLACEDEPENDENCIES)) return CK_DEPENDENCIES_REPLACE;
    return static_cast<CK_DEPENDENCIES_OPMODE>(0);
}

namespace {

int OptionCount(CK_CLASSID cid, CK_DEPENDENCIES_OPMODE mode) {
    CKClassDesc *description = CKGetClassDesc(cid);
    return description && description->DependsCountFct ? description->DependsCountFct(mode) : 0;
}

// The subtrees a class gives its tree item's parent: itself with its
// descendants when it is listed, else its children's subtrees (an item goes
// under the nearest listed ancestor), brothers sorted by name.
std::vector<std::vector<DependencyClass>> Subtrees(CK_CLASSID cid, int depth, CK_DEPENDENCIES_OPMODE mode) {
    const bool listed = OptionCount(cid, mode) != 0;
    std::vector<std::vector<DependencyClass>> below;
    for (CK_CLASSID child = 0; child < static_cast<CK_CLASSID>(CKGetClassCount()); ++child) {
        CKClassDesc *description = CKGetClassDesc(child);
        if (child == cid || !description || description->Parent != cid || !CKIsChildClassOf(child, cid)) continue;
        for (std::vector<DependencyClass> &subtree : Subtrees(child, listed ? depth + 1 : depth, mode))
            below.push_back(std::move(subtree));
    }
    std::stable_sort(below.begin(), below.end(),
                     [](const std::vector<DependencyClass> &a, const std::vector<DependencyClass> &b) {
                         return _stricmp(a.front().name.c_str(), b.front().name.c_str()) < 0;
                     });
    if (!listed) return below;
    const char *name = CKClassIDToString(cid);
    std::vector<DependencyClass> tree{{cid, name ? name : "", depth}};
    for (const std::vector<DependencyClass> &subtree : below) tree.insert(tree.end(), subtree.begin(), subtree.end());
    return {tree};
}

} // namespace

std::vector<DependencyClass> DependencyClasses(CK_DEPENDENCIES_OPMODE mode) {
    std::vector<DependencyClass> classes;
    for (const std::vector<DependencyClass> &subtree : Subtrees(CKCID_OBJECT, 0, mode))
        classes.insert(classes.end(), subtree.begin(), subtree.end());
    return classes;
}

bool SetHeldValue(CKParameter *parameter, void *value) {
    CKContext *context = parameter ? parameter->GetCKContext() : nullptr;
    if (!context || !value || parameter->GetDataSize() != static_cast<int>(sizeof(void *))) return false;
    // A carrier of the type lends its pointer's place to value for the copy.
    CKParameterOut *carrier = context->CreateCKParameterOut((CKSTRING) "", parameter->GetGUID(), TRUE);
    void **slot = carrier ? static_cast<void **>(carrier->GetReadDataPtr(FALSE)) : nullptr;
    if (!slot) {
        if (carrier) context->DestroyObject(carrier);
        return false;
    }
    void *own = *slot;
    *slot = value;
    parameter->CopyValue(carrier, FALSE);
    *slot = own;
    context->DestroyObject(carrier);
    return true;
}

std::vector<std::string> DependencyOptions(CK_CLASSID cid, CK_DEPENDENCIES_OPMODE mode) {
    std::vector<std::string> options;
    CKClassDesc *description = CKGetClassDesc(cid);
    const int count = std::min(OptionCount(cid, mode), 32);
    for (int i = 0; i < count; ++i) {
        const char *name = description->DependsFct ? description->DependsFct(i, mode) : nullptr;
        options.push_back(name ? name : "");
    }
    return options;
}

} // namespace vdev

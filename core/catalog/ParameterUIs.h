#pragma once

#include <string>
#include <utility>
#include <vector>

#include "CKAll.h"
#include "core/catalog/ParameterUIInfo.h"

#include "core/document/Document.h"

namespace vdev {

ParameterUIInfo ParameterUIOf(CKParameterManager *manager, CKParameterType type);

// The lists of CK2UI's combo boxes (CB_ADDSTRING into sorted combos: by name,
// "NULL" (0) sorted with them).
using ObjectChoices = std::vector<std::pair<CK_ID, std::string>>;
// The lists take the composition's live objects (Document::LiveObjects): a
// deleted object is not offered.
// An Object UI (sub_24E6CE00): the objects of a class (and of the classes
// derived from it), not private, named, but the level scene; filter: the
// lower case text their lower case name must hold.
ObjectChoices ObjectsOfClass(const Document &document, CK_CLASSID cid, bool derived, const std::string &filter);
// A Script UI (sub_24E71CA0): the behavioral objects of a class with
// Scripts, but the level scene.
ObjectChoices ScriptOwners(const Document &document, CK_CLASSID cid);
// Its Scripts (sub_24E71A90): the owner's Scripts, or all Scripts and
// "NULL" without one; interface and private Scripts are left out.
ObjectChoices ScriptsOf(const Document &document, CK_ID owner);
// The classes derived from one (CK2UI lists class ids below 56), by name.
std::vector<std::pair<CK_CLASSID, std::string>> DerivedClasses(CK_CLASSID cid);

// The Dependencies UI (dialog 156): the operation of a dependencies type
// (CK2UI sub_24E6A330 ... sub_24E6A790: Copy, Delete, Save, Replace), 0 for
// another type.
CK_DEPENDENCIES_OPMODE DependenciesModeOf(CKParameterManager *manager, CKParameterType type);
// Its tree (sub_24E6ACB0): from CKCID_OBJECT, the classes with options in
// the mode, each under its nearest listed ancestor, brothers by name
// (TVI_SORT); depth-first.
struct DependencyClass {
    CK_CLASSID cid = 0;
    std::string name;
    int depth = 0;
};
std::vector<DependencyClass> DependencyClasses(CK_DEPENDENCIES_OPMODE mode);
// A class's options in the mode (its check boxes, sub_24E6B350: bit i).
std::vector<std::string> DependencyOptions(CK_CLASSID cid, CK_DEPENDENCIES_OPMODE mode);

// A value held by pointer (dependencies, an object array) set from one in
// the editor's memory: the type's copy function copies it into the
// parameter's own, in CK's heap (the containers' inline code must not grow
// or free what CK allocated). False when the parameter holds no pointer.
bool SetHeldValue(CKParameter *parameter, void *value);

} // namespace vdev

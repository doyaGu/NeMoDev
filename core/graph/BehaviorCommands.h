#pragma once

#include <map>
#include <string>
#include <vector>

#include "CKAll.h"
#include "core/schematic/ElementRef.h"
#include "core/document/EditResult.h"

namespace vdev {

class Document;

// A Building Block setting: one of its local parameters (Dev's "s" mark).
struct BehaviorSetting {
    int slot = 0;
    CK_ID id = 0;
    std::string name;  // ANSI
    std::string type;  // parameter type name
    std::string value; // CKParameter::GetStringValue
};

std::vector<BehaviorSetting> BehaviorSettings(CKBehavior *behavior);

// Dev "Edit Settings" (S): sets settings by text (slot -> value) and sends
// CKM_BEHAVIORSETTINGSEDITED; the Building Block may then enable, disable,
// add or remove pins. Undo restores the previous texts and sends it again.
// Links of pins the Building Block removes are not restored by undo.
EditResult EditBehaviorSettings(Document &document, CK_ID script, CK_ID behavior,
                                const std::map<int, std::string> &values);

// True when the Building Block declares this pin kind variable
// (CKBEHAVIOR_VARIABLEINPUTS, ...OUTPUTS, ...PARAMETERINPUTS, ...PARAMETEROUTPUTS).
bool AcceptsVariablePin(CKBehavior *behavior, PinKind kind);

// Dev "Construct" on a variable Building Block: adds the pin, then sends
// CKM_BEHAVIOREDITED (e.g. Identity adds the matching output). Undo removes
// the last pin of that kind and sends the message again.
EditResult AddBuildingBlockPin(Document &document, CK_ID script, CK_ID behavior, PinKind kind,
                               const std::string &name = std::string(), const CKGUID &type = CKGUID());

} // namespace vdev

#pragma once

#include <set>
#include <string>
#include <vector>

#include "CKAll.h"
#include "core/interface/InterfaceData.h"

namespace vdev {

// Helpers relating stored Schematic records to the CK runtime graph, which
// is the source of topology. None of them changes CK or InterfaceData.

// The CK object a link endpoint designates, or null:
//   BIN/BOUT        CKBehaviorIO of the behavior (GetInput/GetOutput)
//   START_BIN       the Script input CKBehaviorIO (endpoint id is the IO)
//   PIN             CKParameterIn of a behavior or operation (op slots 0/1)
//   TARGET_PIN      Target parameter of a behavior that uses its Target
//   POUT            CKParameterOut of a behavior or operation
//   PLOCAL          CKParameterLocal slot of the graph named by the id
//   POUT_SHORTCUT   source parameter of the storing graph's shortcut slot
CKObject *ResolveEndpointObject(CKContext *context, const BehaviorData &graph, const LinkEndpoint &endpoint);

// True when a stored link describes a relation that exists in CK:
//   behavior link   the CKBehaviorLink is owned by the storing graph and
//                   joins the two endpoint IOs;
//   parameter link  end pIn takes its direct or shared source from start,
//                   start pOut has end as destination, or both endpoints
//                   are the same parameter (exported through a graph).
bool LinkMatchesRuntime(CKContext *context, const BehaviorData &graph, const Link &link);

// The undisplayed conversion operation ("Convert", "Get Name", ...) a stored
// parameter link passes through, or null: the end pIn's direct source is the
// output of an operation the graph does not display, and that operation takes
// its input from the link's start. Dev draws such a link in the operation
// link color with the operation name (2D Text.cmo screenshot).
CKParameterOperation *HiddenConversion(CKContext *context, const BehaviorData &graph, const Link &link);

// The hidden conversion operations of every stored link of a Script.
std::set<CK_ID> HiddenConversions(CKContext *context, const InterfaceData &data);

// Display/runtime mismatches of one Script, as stable text keys. Dev-saved
// chunks may already contain some, so editors compare the sets before and
// after a command rather than requiring an empty result.
std::set<std::string> RuntimeConsistencyIssues(CKContext *context, const InterfaceData &data, CKBehavior *script);

// Objects owned through a behavior, operation or link: IOs, parameters,
// locals, sub-behaviors, sub-links and operation parameters, recursively.
std::set<CK_ID> CollectOwnedObjects(CKContext *context, const std::set<CK_ID> &roots);

// True for parameter types whose 4-byte value is a pointer the type creates,
// copies and deletes (2D Curve, arrays, ...): raw value bytes must never be
// copied; use CKParameter::CopyValue or the string form.
bool ManagesMemory(CKContext *context, CKParameterType type);

// Text form of a value (CKParameter::GetStringValue, ANSI); empty on failure.
std::string ParameterText(CKParameter *parameter);
// Checked read for editing: failure is distinct from a valid empty string.
bool ParameterText(CKParameter *parameter, std::string &text);

} // namespace vdev

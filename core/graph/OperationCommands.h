#pragma once

#include "CKAll.h"
#include "core/catalog/ParameterCatalog.h"
#include "core/document/Edit.h" // LinkEndpoint (InterfaceData)
#include "core/document/EditResult.h"
#include "core/schematic/ElementRef.h"

namespace vdev {

class Document;

// The Schematic's commands that change a parameter operation and its links
// in several steps, each one undo step, all or nothing (Edit): a step
// refused takes back the others and the result says why.

// Dev's Modify Parameter Operation (M): a new operation of the signature at
// the old one's place takes over the old one's parameter links whose types
// still fit, then the old one is deleted. The links to a disabled input are
// taken over too: Dev does not draw them, they are CK's still and work when
// the input is enabled. dropped: the links that did not fit the new types
// (left out; the rest is done). created: the new operation.
EditResult ReplaceParameterOperation(Document &document, CK_ID script, CK_ID operation,
                                     const OperationSignature &signature, int *dropped = nullptr);

// Dev's Edit Parameter Operation Link: the parameter link a-b (link: as the
// Script stores it) through signature's conversion instead.
EditResult ReplaceLinkConversion(Document &document, CK_ID script, const ElementRef &link, const LinkEndpoint &a,
                                 const LinkEndpoint &b, const OperationSignature &signature);

// Dev's Add Parameter Operation in Link: the parameter link a-b replaced by a
// visible operation at its middle, linked on both sides (a side that does
// not link leaves the operation unlinked there). created: the operation.
EditResult InsertOperationInLink(Document &document, CK_ID script, const ElementRef &link, const LinkEndpoint &a,
                                 const LinkEndpoint &b, const OperationSignature &signature);

} // namespace vdev

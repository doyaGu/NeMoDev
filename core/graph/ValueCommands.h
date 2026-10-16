#pragma once

#include <string>
#include <vector>

#include "core/document/EditResult.h"
#include "core/schematic/ElementRef.h"

namespace vdev {

class Document;

// Parameter values are CK data saved with the parameter objects, not in the
// InterfaceChunk. A value edit is one undoable command holding the raw value
// bytes before and after; it marks the document modified.

// Raw value bytes (CKParameter::GetValue without update), empty on failure.
std::vector<unsigned char> ParameterBytes(CKParameter *parameter);

EditResult SetParameterBytes(Document &document, CK_ID script, CK_ID parameter,
                             const std::vector<unsigned char> &bytes);

// Parses text with the type's CKParameterManager string function.
EditResult SetParameterText(Document &document, CK_ID script, CK_ID parameter,
                            const std::string &text);

// The parameter whose value an input shows: its direct source when that is
// a local parameter (Dev's value holder), otherwise null (linked to an
// output, shared, or without a value).
CKParameterLocal *EditableValueOf(CKParameterIn *input);

// Dev's Edit Parameter type (Dev.exe sub_589D50, the "Parameter Type :"
// combo): a local parameter, an output or an input (with its value holder
// when nothing else uses it: CKParameterIn::SetGUID updating its source; a
// local shared keeps its type) takes another type. One undo step (the
// parameters' states).
EditResult SetParameterType(Document &document, CK_ID parameter, const CKGUID &type);

// The Script's parameter links to or from parameter whose types do not fit
// (the source's type not compatible with the destination's): Dev's "Invalid
// Parameter Link" when Edit Parameter changes a type (Dev.exe sub_536670:
// "Parameter Type is incompatible with Parameter Input Type. This link must
// be deleted."), as DeleteElements takes them. type given: the links that
// would not fit once parameter takes it (SetParameterType's way: an input's
// value holder used by it alone takes it too). Deleting them before the
// type changes keeps both undoable (CK links compatible types only).
std::vector<ElementRef> IncompatibleParameterLinks(Document &document, CK_ID script, CK_ID parameter,
                                                   const CKGUID &type = CKGUID());

} // namespace vdev

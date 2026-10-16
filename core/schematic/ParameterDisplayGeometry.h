#pragma once

#include "core/interface/InterfaceDataElements.h"

class CKContext;

// Internal geometry for local parameters, parameter output shortcuts and
// ParameterOperations. No wire fields or CK parameter value evaluation.
namespace ParameterDisplayGeometry {

constexpr int SCRIPT_MINIMUM_COLUMN = 9;

// Relative to the owning window; Script type23 clamps both parameter tables.
Point GetPosition(const Parameter& parameter, bool scriptUi);

// Relative display reservation, not a reproduction of Dev font measurement.
// NAME estimates its text width; VALUE/NAMEVALUE reserve native width limits.
Rect EstimateBounds(const Parameter& parameter, CKContext* context, bool scriptUi);

// Native type20 outer bounds include the two PINs and the POUT (default H=V=20).
// Operation positions are already pixel coordinates. Failure leaves bounds intact.
bool TryGetOperationBounds(const Operation& operation, Rect& bounds);

} // namespace ParameterDisplayGeometry

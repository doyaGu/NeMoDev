#pragma once

#include "core/interface/InterfaceData.h"

enum class InterfaceDataLinkRouting {
    Straight,
    OrthogonalBeautify
};

// Routing-stage result only; this does not replace graph validation or native
// Dev acceptance. All fields are reset on every reconstruction call.
struct InterfaceDataReconstructionDiagnostics {
    bool linkRoutingSucceeded = false;
    int unresolvedEndpointCount = 0;
    int routingFailureCount = 0;
    int blockedEndpointLinkCount = 0;
};

struct InterfaceDataReconstructionOptions {
    InterfaceDataLinkRouting linkRouting = InterfaceDataLinkRouting::Straight;
    // Compatibility name: true retains runtime direct sources even when
    // parameter types are unknown/incompatible. It never reads a saved chunk.
    // false follows Dev's type validation for newly created parameter links.
    bool preserveSavedDirectParameterLinks = false;
    // Optional, caller-owned output. Required when a caller must distinguish
    // requested beautification from point lists retained after a routing failure.
    InterfaceDataReconstructionDiagnostics *diagnostics = nullptr;
};

/**
 * Reconstructs InterfaceData from a Script CKBehavior.
 *
 * The operation builds behavior links and parameter links from CK runtime
 * objects, validates LinkEndpoint.id/index/type, calculates Schematic View
 * positions, and applies the selected Link::points policy.
 *
 * @param data InterfaceData to replace
 * @param script Script CKBehavior to reconstruct
 * @param options reconstruction choices
 */
void ReconstructInterfaceData(
    InterfaceData &data,
    CKBehavior *script,
    const InterfaceDataReconstructionOptions &options = InterfaceDataReconstructionOptions());

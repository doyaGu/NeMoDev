#pragma once

#include "core/interface/InterfaceData.h"

// Internal coordinate resolver shared by routing and its verifier. Success
// means a finite position was resolved, NOT that all Dev UI geometry matches.
// Dev 2.5 evidence covers Start/local/shortcut offsets, default operation
// parameter centers, type-22 Behavior PIN/POUT position-table placement and
// Target centers, and count-dependent/Target-adjusted type22 BehaviorIO on
// the default grid with runtime GetFlags/GetInputCount/GetOutputCount data.
// Script type23 parameter helpers use zero relative offset; ordinary BIN/BOUT
// are rejected there (Script control flow uses Start). Missing expanded bounds,
// edited parameter displays, records without runtime data or expanded sizes,
// and arbitrary parent/grid transforms remain estimates, not native UI acceptance.
// Pass the actual runtime root kind; do not infer Script from header defaults.
bool TryGetLinkEndpointPosition(
    const InterfaceData &data, const LinkEndpoint &endpoint, Point &position,
    bool rootIsScript);

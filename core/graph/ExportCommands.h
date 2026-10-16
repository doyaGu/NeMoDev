#pragma once

#include <utility>
#include <vector>

#include "core/graph/TopologyCommands.h"
#include "core/schematic/SchematicModel.h"

namespace vdev {

// Dev's exported parameters (Dev documentation, link.html "To export
// Inputs, Outputs, pIns, pOuts outside a behavior graph"): Shift+drag of a
// pIn/pOut outside its graph "and beyond" makes every graph crossed list the
// same parameter instance (CKBehavior::ExportInputParameter /
// ExportOutputParameter append it and keep its owner, CK2 0x24002f35,
// 0x240032d5). Each level is shown by an exported link (highlight flag, the
// "Exported Parameter Link" color) stored in the exporting graph, from its
// pin to the inner pin, as Dev's chunks and the reconstruction store them.
//
// An exported input keeps its source; the source's display (a local value
// holder or a shortcut, used by this input only) moves to `into`, linked to
// the outermost pin, at localPosition when given (else above that pin). An
// input fed by an output or an operation inside the graph is refused.

// pin: PIN or POUT of a behavior (a graph's own pin exports it further).
// into: an open graph enclosing the pin's graph; every graph in between
// gets the parameter.
EditResult ExportParameter(Document &document, CK_ID script, const LinkEndpoint &pin,
                           CK_ID into, const Point *localPosition = nullptr);

// Re-import (Shift+drag of an exported graph pin into the graph): the pin
// leaves graph and every exporting level below it down to the graph `into`
// (graph itself or an exporting graph inside it). An input's source display
// moves to the innermost removed level. The outermost level must be graph:
// a pin exported further up is re-imported from the top. An exported
// output whose value is used outside graph is refused.
EditResult ReimportParameter(Document &document, CK_ID script, CK_ID graph, PinKind kind,
                             int slot, CK_ID into, const Point *localPosition = nullptr);

// Dev "To export Parameter Local outside a behavior graph": a local linked
// to one pIn, dragged up out of its graph, moves to `into` and the pIn is
// exported through the graphs crossed; dragged down into the exporting graph
// of the pIn it feeds, it is re-imported.
EditResult MoveLocalAcrossGraphs(Document &document, CK_ID script, CK_ID graph, int slot,
                                 CK_ID into, const Point &position);

// Dev "To export a bIn/bOut: Drag a bOut or bIn on to the border of the
// BG": every graph between the pin's behavior and `into` gets a new
// behavior input (output) named after the pin, linked to it (delay 0).
// Re-import is not available for inputs and outputs (Dev documentation).
EditResult ExportBehaviorIo(Document &document, CK_ID script, const LinkEndpoint &io,
                            CK_ID into);

// Dev Shift+drag of a graph's pIn/pOut along its border: the pin's grid
// column and edge (top or bottom) in the graph's current state (open and
// closed graphs keep separate positions). Display only.
bool MoveGraphParameter(Document &document, CK_ID script, CK_ID graph, PinKind kind,
                        int slot, int column, bool top);

// Dev's Shift+drag of pins (link.html): the pins (a behavior's pIn, pOut,
// bIn, bOut, as the Schematic has them) dropped at a Script-local point go
// into the deepest open graph there (the Script when none). A graph's own
// parameter alone dropped on its top or bottom border moves along it
// (MoveGraphParameter); a graph's parameter dropped into a graph it encloses
// is re-imported; the others are exported to that graph. One undo step;
// returns the refusals.
std::vector<EditResult> ExportPinsAt(Document &document, CK_ID script, const std::vector<PinItem> &pins,
                                     const Point &at);

// Local parameters of one graph (ids, where each was dropped) dragged into
// another graph: MoveLocalAcrossGraphs for each, one undo step. moved: how
// many were; refusal: the last refusal.
void MoveLocalsAcrossGraphs(Document &document, CK_ID script, CK_ID graph,
                            const std::vector<std::pair<CK_ID, Point>> &locals, CK_ID into, size_t &moved,
                            std::string &refusal);

// The graphs from the pin owner's graph up to (excluding) into, innermost
// first; empty when into does not enclose the owner's graph.
std::vector<CK_ID> GraphsBetween(const InterfaceData &data, CK_ID owner, CK_ID into);

} // namespace vdev

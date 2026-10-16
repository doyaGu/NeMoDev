#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/document/Edit.h"
#include "core/document/EditResult.h"
#include "core/schematic/ElementRef.h"
#include "core/document/UndoHistory.h"

namespace vdev {

class Document;

// Topology edits change the CK graph (the source of topology) and the
// Script's display records together, as one undo step.
//
// CK changes are recorded as reversible actions on object ids (an Edit).
// Removed objects are soft-deleted: detached from their graph and kept alive
// in the document graveyard (never saved), so undo re-attaches the same
// CK_IDs. The display data is shown when the edit is committed, and put back
// after the runtime by undo.
//
// After the CK actions, stored links that described a CK relation before the
// edit and no longer do are pruned (a replaced source, a detached link), as
// are records of detached behaviors and operations. The edit is then
// refused unless RuntimeConsistencyIssues reports nothing new.

// The Edit of one Script's topology. Actions run as they are added; an edit
// that is not committed takes back every action done when destroyed.
class TopologyEdit {
public:
    TopologyEdit(Document &document, CK_ID script, const std::string &label);
    TopologyEdit(const TopologyEdit &) = delete;
    TopologyEdit &operator=(const TopologyEdit &) = delete;

    bool Valid() const { return m_Error.empty(); }
    const std::string &Error() const { return m_Error; }
    CKContext *Context() const;
    CKBehavior *Script() const;
    // The display data the edit will show (Commit).
    InterfaceData &Data() { return m_After; }
    const Document &Composition() const { return m_Document; }

    // Runs redo now and records the pair; false (and nothing recorded) when
    // redo fails or throws a structured exception.
    bool Do(const std::function<bool()> &redo, const std::function<bool()> &undo) { return m_Edit.Do(redo, undo); }
    // Records a step whose redo has already been performed.
    void Done(const std::function<bool()> &redo, const std::function<bool()> &undo) { m_Edit.Done(redo, undo); }
    void Bury(CK_ID id) {
        m_Buried.insert(id);
        m_Edit.Bury(id);
    }
    void Created(CK_ID id) { m_Edit.Created(id); }

    // The history is the document's (the argument goes with the batch that
    // removes it from every command).
    EditResult Commit(CK_ID created = 0);
    EditResult Fail(const std::string &message) { return m_Edit.Fail(message); }
    // Fail for an edit with nothing to do (EditStatus::NoChange).
    EditResult Unchanged(const std::string &message) { return m_Edit.Unchanged(message); }

private:
    Document &m_Document;
    Edit m_Edit;
    CK_ID m_Script;
    InterfaceData m_After;
    std::set<CK_ID> m_Buried;             // removed by the edit (their owned objects leave the display)
    std::set<std::string> m_LinksBefore;  // stored links matching CK before the edit
    std::set<std::string> m_IssuesBefore; // pre-existing display/runtime mismatches
    std::set<CK_ID> m_ConversionsBefore;  // hidden conversion operations of stored links
    struct LocalValue {
        CKGUID type;
        std::string text;
    };
    std::map<CK_ID, LocalValue> m_LocalsBefore; // the tree's locals (a callback may write them)
    std::string m_Error;
};

// --- Edits. Positions are Script-local pixels; graph is the CK_ID of the
// Script root or of an expanded child graph that will own the new element.

EditResult InsertBuildingBlock(Document &document, CK_ID script, CK_ID graph,
                               const CKGUID &prototype, const Point &position);

// Empty behavior graph (CKBEHAVIORTYPE_BEHAVIOR) with one input and one
// output, shown expanded (pin count, names and the 200x120 expanded size
// are library choices).
EditResult CreateBehaviorGraph(Document &document, CK_ID script, CK_ID graph,
                               const std::string &name, const Point &position);

// Behavior link between two pins in either order: BOUT/START_BIN (or a
// graph's own BIN, inside it) to BIN (or a graph's own BOUT, inside it).
// Both ends must be displayed by the same graph. As Dev 2.5 (0x509250):
// the delay is the "Default Link Delay" preference (default 0), and with
// "Auto Loop Link Delay" (default on) a zero delay becomes 1 when the
// destination already reaches the source with no delay
// (CKBehavior::GetShortestDelay == 0), which would close a zero-delay loop.
EditResult CreateBehaviorLink(Document &document, CK_ID script, const LinkEndpoint &a,
                              const LinkEndpoint &b, int delay = 0, bool autoLoopDelay = true);

// Parameter link between two pins in either order, displayed by one graph:
//   pOut / local / shortcut  -> pIn or Target     SetDirectSource
//   graph pIn (inside)       -> child pIn         ShareSourceWith
//   pOut                     -> local, graph pOut AddDestination
// Types must satisfy CKParameterManager::IsTypeCompatible(source, dest);
// Dev's hidden conversion operations are not created. A replaced source's
// stored link is pruned.
//
// With conversion, incompatible types are joined as Dev's "Parameter
// Operation Link": an undisplayed operation of the graph takes the source
// on its first input and feeds the destination input; the stored link goes
// from the source to the destination (drawn in the operation link color
// with the operation name).
struct OperationSignature;
EditResult CreateParameterLink(Document &document, CK_ID script, const LinkEndpoint &a,
                               const LinkEndpoint &b, const OperationSignature *conversion = nullptr);

// Operations that convert the source type of a parameter link a-b to its
// destination type: one input accepting the source, no second input, a
// result the destination accepts. Empty when the ends cannot be linked.
std::vector<OperationSignature> ParameterLinkConversions(Document &document, CK_ID script,
                                                         const LinkEndpoint &a, const LinkEndpoint &b);

// Dev "Add Parameter Operation in Link": operations whose first input
// accepts the link's source and whose result its destination accepts (any
// second input), whether or not the two ends are compatible.
std::vector<OperationSignature> ParameterLinkOperations(Document &document, CK_ID script,
                                                        const LinkEndpoint &a, const LinkEndpoint &b);

EditResult AddLocalParameter(Document &document, CK_ID script, CK_ID graph,
                             const std::string &name, const CKGUID &type, const Point &position);

// Dev "Add <This> Parameter" (Alt+T): a local of the owner's class type set
// as a "myself" parameter (CKParameterLocal::SetAsMyselfParameter), so its
// value is the object the Script is attached to. Named "This" (library choice).
EditResult AddThisParameter(Document &document, CK_ID script, CK_ID graph,
                            const Point &position);

EditResult AddParameterOperation(Document &document, CK_ID script, CK_ID graph,
                                 const CKGUID &operation, const CKGUID &result, const CKGUID &input1,
                                 const CKGUID &input2, const Point &position);

// Adds a behavior input/output (PinKind BehaviorIn/BehaviorOut) or an input/
// output parameter (ParamIn/ParamOut, with type) to an editable behavior
// graph; the Script root takes parameters only. The record grows to fit the
// new pin (library choice; it never shrinks).
EditResult AddGraphPin(Document &document, CK_ID script, CK_ID behavior, PinKind kind,
                       const std::string &name, const CKGUID &type = CKGUID());

EditResult SetTargetUsed(Document &document, CK_ID script, CK_ID behavior, bool use);
EditResult RenameObject(Document &document, CK_ID script, CK_ID object,
                        const std::string &name);
// CKBehavior::SetPriority (Dev Y): higher runs first among siblings.
EditResult SetBehaviorPriority(Document &document, CK_ID script, CK_ID behavior,
                               int priority);
EditResult SetLinkDelay(Document &document, CK_ID script, CK_ID link, int delay);

// Pastes copies of behaviors (with their graphs) into an editable graph.
// CKContext::CopyObjects (needs a current level) copies each behavior with
// what it owns; source holds the display records captured when copying.
// Behavior and parameter links among the pasted behaviors are copied too.
// A pasted input whose source stays outside gets a value local holding the
// current value (Dev's value holders). Comments are pasted with them. The
// selection's top-left corner lands on the snapped position.
EditResult PasteBehaviors(Document &document, const InterfaceData &source,
                          const std::vector<CK_ID> &behaviors, const std::vector<Comment> &comments, CK_ID script,
                          CK_ID graph, const Point &position);

// Dev Shift+Del: removes a behavior graph and keeps its contents in the
// parent graph. Behavior links through the graph's own IOs become direct
// links (delays added: library choice); inputs sharing a graph parameter
// take its source; consumers and destinations of a graph output use the
// child output feeding it. Graphs with exported parameters are refused.
EditResult UngroupGraph(Document &document, CK_ID script, CK_ID graph);

// Dev G: puts sibling behaviors into a new expanded behavior graph.
// Behavior links crossing its border go through new graph IOs (one per
// member IO reached from outside; outer links keep their delay). Member
// inputs fed from outside share a new graph input parameter (one per
// source); member outputs used outside feed a new graph output parameter.
// Parent locals used only by the members move into the graph.
EditResult GroupBehaviors(Document &document, CK_ID script,
                          const std::vector<CK_ID> &behaviors);

// Deletes behaviors (with their subtree), behavior and parameter links,
// operations, local parameters, shortcuts, Comments, control points, and
// the own pins of editable graphs (later pin slots are renumbered).
// Connections from outside a deleted element are disconnected first.
EditResult DeleteElements(Document &document, CK_ID script,
                          const std::vector<ElementRef> &elements);

} // namespace vdev

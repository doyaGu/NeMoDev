#include "core/graph/ReplaceCommands.h"

#include <algorithm>
#include <functional>

#include "core/runtime/CkRuntimeHost.h"
#include "core/schematic/LayoutCalculator.h"
#include "core/graph/NmsCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/schematic/SchematicEdits.h"
#include "core/graph/detail/TopologyInternal.h"

namespace vdev {

using namespace detail;

namespace {

bool IsBuildingBlock(CKBehavior *behavior) {
    return behavior && (behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK);
}

bool IsGraph(CKBehavior *behavior) {
    return behavior && behavior->GetType() != CKBEHAVIORTYPE_BASE && !IsBuildingBlock(behavior);
}

// New slot of each old pin: same name, else same slot; compatible types only.
std::vector<int> MatchPins(const std::vector<std::string> &before, const std::vector<std::string> &after,
                           const std::function<bool(int, int)> &compatible) {
    std::vector<int> map(before.size(), -1);
    std::vector<bool> used(after.size(), false);
    for (size_t i = 0; i < before.size(); ++i)
        for (size_t j = 0; j < after.size(); ++j)
            if (!used[j] && before[i] == after[j] && compatible(static_cast<int>(i), static_cast<int>(j))) {
                map[i] = static_cast<int>(j);
                used[j] = true;
                break;
            }
    for (size_t i = 0; i < before.size(); ++i)
        if (map[i] < 0 && i < after.size() && !used[i] && compatible(static_cast<int>(i), static_cast<int>(i))) {
            map[i] = static_cast<int>(i);
            used[i] = true;
        }
    return map;
}

// A local of graph that feeds only input: Dev's value holder (a deleted
// behavior's input naming it still is not a use).
bool IsValueHolder(const Document &document, CKBehavior *graph, CKParameter *source, CKParameterIn *input) {
    CKParameterLocal *local = CKParameterLocal::Cast(source);
    if (!local || local->IsMyselfParameter() || !graph || graph->GetLocalParameterPosition(local) < 0) return false;
    return OtherUseOf(document, local, input).empty();
}

// Moves the links of old (a child of its parent graph) to its sibling
// replacement, then detaches old. See ReplaceCommands.h.
bool Rewire(TopologyEdit &edit, CK_ID oldId, CK_ID newId, std::string &error) {
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    CKBehavior *old = BehaviorOf(context, oldId), *fresh = BehaviorOf(context, newId);
    const CK_ID parentId = ParentOf(data, oldId);
    CKBehavior *parent = BehaviorOf(context, parentId);
    CKParameterManager *manager = context->GetParameterManager();
    if (!old || !fresh || !parent || !manager || ParentOf(data, newId) != parentId) {
        error = "The replacement is not in the replaced behavior's graph";
        return false;
    }
    std::vector<std::string> oldIn, newIn, oldOut, newOut, oldPin, newPin, oldPout, newPout;
    for (int i = 0; i < old->GetInputCount(); ++i) oldIn.push_back(NameOf(old->GetInput(i)));
    for (int i = 0; i < fresh->GetInputCount(); ++i) newIn.push_back(NameOf(fresh->GetInput(i)));
    for (int i = 0; i < old->GetOutputCount(); ++i) oldOut.push_back(NameOf(old->GetOutput(i)));
    for (int i = 0; i < fresh->GetOutputCount(); ++i) newOut.push_back(NameOf(fresh->GetOutput(i)));
    for (int i = 0; i < old->GetInputParameterCount(); ++i) oldPin.push_back(NameOf(old->GetInputParameter(i)));
    for (int i = 0; i < fresh->GetInputParameterCount(); ++i) newPin.push_back(NameOf(fresh->GetInputParameter(i)));
    for (int i = 0; i < old->GetOutputParameterCount(); ++i) oldPout.push_back(NameOf(old->GetOutputParameter(i)));
    for (int i = 0; i < fresh->GetOutputParameterCount(); ++i) newPout.push_back(NameOf(fresh->GetOutputParameter(i)));
    const auto any = [](int, int) { return true; };
    const std::vector<int> bin = MatchPins(oldIn, newIn, any), bout = MatchPins(oldOut, newOut, any);
    const std::vector<int> pin = MatchPins(oldPin, newPin, [&](int i, int j) {
        return manager->IsTypeCompatible(old->GetInputParameter(i)->GetType(), fresh->GetInputParameter(j)->GetType()) != 0;
    });
    const std::vector<int> pout = MatchPins(oldPout, newPout, [&](int i, int j) {
        return manager->IsTypeCompatible(fresh->GetOutputParameter(j)->GetType(), old->GetOutputParameter(i)->GetType()) != 0;
    });

    // 1. Behavior links of the graph: the same link objects, new IOs.
    std::vector<CKBehaviorLink *> links;
    for (int i = 0; i < parent->GetSubBehaviorLinkCount(); ++i) {
        CKBehaviorLink *link = parent->GetSubBehaviorLink(i);
        CKBehaviorIO *in = link ? link->GetInBehaviorIO() : nullptr, *out = link ? link->GetOutBehaviorIO() : nullptr;
        if ((in && in->GetOwner() == old) || (out && out->GetOwner() == old)) links.push_back(link);
    }
    for (CKBehaviorLink *link : links) {
        const CK_ID linkId = link->GetID();
        const IoRef in = IoRefOf(link->GetInBehaviorIO()), out = IoRefOf(link->GetOutBehaviorIO());
        IoRef newInRef = in, newOutRef = out;
        bool keep = true;
        auto remap = [&](IoRef &ref) {
            if (ref.owner != oldId) return;
            const std::vector<int> &map = ref.input ? bin : bout;
            const int to = ref.index >= 0 && ref.index < static_cast<int>(map.size()) ? map[ref.index] : -1;
            if (to < 0) {
                keep = false;
                return;
            }
            ref.owner = newId;
            ref.index = to;
        };
        remap(newInRef);
        remap(newOutRef);
        const int at = LinkPosition(parent, link);
        if (keep) {
            if (!edit.Do(
                    [context, parentId, linkId, at, newInRef, newOutRef] {
                        return DetachLink(context, parentId, linkId) &&
                               AttachLink(context, parentId, linkId, at, newInRef, newOutRef);
                    },
                    [context, parentId, linkId, at, in, out] {
                        return DetachLink(context, parentId, linkId) && AttachLink(context, parentId, linkId, at, in, out);
                    })) {
                error = "A behavior link refused its new pin";
                return false;
            }
        } else {
            if (!edit.Do([context, parentId, linkId] { return DetachLink(context, parentId, linkId); },
                         [context, parentId, linkId, at, in, out] {
                             return AttachLink(context, parentId, linkId, at, in, out);
                         })) {
                error = "Cannot remove a behavior link";
                return false;
            }
            edit.Bury(linkId);
        }
    }

    // 2. Inputs and the Target: the new input takes the old one's source.
    std::set<CK_ID> dropLocals;
    auto moveInput = [&](CKParameterIn *from, CKParameterIn *to) -> bool {
        if (!from) return true;
        const InputSource source = SourceOf(from);
        if (to) {
            if (CKParameter *direct = to->GetDirectSource())
                if (IsValueHolder(edit.Composition(), parent, direct, to)) dropLocals.insert(direct->GetID());
            const InputSource before = SourceOf(to);
            InputSource after = source;
            after.input = before.input;
            if (source.direct || source.shared.id) {
                if (!edit.Do([context, after] { return RestoreSource(context, after); },
                             [context, before] { return RestoreSource(context, before); }))
                    return false;
            } else if ((to->GetDirectSource() || to->GetSharedSource()) && !ClearSource(edit, to)) {
                return false;
            }
        } else if (CKParameter *direct = from->GetDirectSource()) {
            if (IsValueHolder(edit.Composition(), parent, direct, from)) dropLocals.insert(direct->GetID());
        }
        return !(from->GetDirectSource() || from->GetSharedSource()) || ClearSource(edit, from);
    };
    for (int i = 0; i < old->GetInputParameterCount(); ++i)
        if (!moveInput(old->GetInputParameter(i), pin[i] >= 0 ? fresh->GetInputParameter(pin[i]) : nullptr)) {
            error = "An input refused its source";
            return false;
        }
    if (old->IsUsingTarget() &&
        !moveInput(old->GetTargetParameter(), fresh->IsUsingTarget() ? fresh->GetTargetParameter() : nullptr)) {
        error = "The Target refused its source";
        return false;
    }

    // 3. Outputs: destinations and the inputs reading them.
    for (int k = 0; k < old->GetOutputParameterCount(); ++k) {
        CKParameterOut *output = old->GetOutputParameter(k);
        if (pout[k] < 0) {
            if (!DisconnectParameter(edit, output, {})) {
                error = "Cannot disconnect an output";
                return false;
            }
            continue;
        }
        CKParameterOut *replacement = fresh->GetOutputParameter(pout[k]);
        const CK_ID outputId = output->GetID(), replacementId = replacement->GetID();
        const std::vector<CK_ID> outputBefore = DestinationsOf(output), replacementBefore = DestinationsOf(replacement);
        if (!outputBefore.empty() &&
            !edit.Do(
                [context, outputId, replacementId, outputBefore] {
                    CKParameterOut *from = CKParameterOut::Cast(context->GetObject(outputId));
                    CKParameterOut *to = CKParameterOut::Cast(context->GetObject(replacementId));
                    if (!from || !to) return false;
                    for (CK_ID id : outputBefore) {
                        CKParameter *destination = CKParameter::Cast(context->GetObject(id));
                        if (!destination || to->AddDestination(destination, FALSE) != CK_OK) return false;
                    }
                    from->RemoveAllDestinations();
                    return true;
                },
                [context, outputId, replacementId, outputBefore, replacementBefore] {
                    return RestoreDestinations(context, replacementId, replacementBefore) &&
                           RestoreDestinations(context, outputId, outputBefore);
                })) {
            error = "An output refused its destinations";
            return false;
        }
        for (CKParameterIn *input : ObjectsOfClass<CKParameterIn>(context, CKCID_PARAMETERIN)) {
            if (input->GetDirectSource() != output) continue;
            const InputSource before = SourceOf(input);
            const InputRef ref = before.input;
            if (!edit.Do(
                    [context, ref, replacementId] {
                        CKParameterIn *target = Resolve(context, ref);
                        CKParameter *source = CKParameter::Cast(context->GetObject(replacementId));
                        return target && source && target->SetDirectSource(source) == CK_OK;
                    },
                    [context, before] { return RestoreSource(context, before); })) {
                error = "An input refused the new output";
                return false;
            }
        }
        ForEachRecord(data, [&](BehaviorData &record) {
            for (auto &shared : record.sharedParams)
                if (shared.sourceId == outputId) shared.sourceId = replacementId;
        });
    }

    // 4. Display: links on old's pins move to the new pins.
    ForEachRecord(data, [&](BehaviorData &record) {
        for (auto &link : record.links)
            for (LinkEndpoint *endpoint : {&link.start, &link.end}) {
                if (endpoint->id != oldId) continue;
                if (endpoint->type == ENDPOINT_TARGET_PIN) {
                    if (fresh->IsUsingTarget()) endpoint->id = newId;
                    continue;
                }
                const std::vector<int> *map = endpoint->type == ENDPOINT_BIN    ? &bin
                                              : endpoint->type == ENDPOINT_BOUT ? &bout
                                              : endpoint->type == ENDPOINT_PIN  ? &pin
                                              : endpoint->type == ENDPOINT_POUT ? &pout
                                                                                : nullptr;
                if (!map || endpoint->index < 0 || endpoint->index >= static_cast<int>(map->size()) ||
                    (*map)[endpoint->index] < 0)
                    continue; // pruned with old
                endpoint->id = newId;
                endpoint->index = (*map)[endpoint->index];
            }
    });

    // 5. Value holders that lost their input leave with it.
    std::vector<std::pair<int, CK_ID>> slots;
    for (CK_ID id : dropLocals) {
        CKParameterLocal *local = CKParameterLocal::Cast(context->GetObject(id));
        const int slot = parent->GetLocalParameterPosition(local);
        if (slot >= 0) slots.emplace_back(slot, id);
    }
    std::sort(slots.rbegin(), slots.rend());
    for (const auto &entry : slots) {
        const int slot = entry.first;
        const CK_ID id = entry.second;
        if (!edit.Do(
                [context, parentId, slot, id] {
                    CKBehavior *graph = BehaviorOf(context, parentId);
                    return graph && graph->GetLocalParameter(slot) == context->GetObject(id) &&
                           graph->RemoveLocalParameter(slot) == context->GetObject(id);
                },
                [context, parentId, slot, id] {
                    CKBehavior *graph = BehaviorOf(context, parentId);
                    CKParameterLocal *local = CKParameterLocal::Cast(context->GetObject(id));
                    if (!graph || !local) return false;
                    return InsertLocalParameter(graph, slot, local) && graph->GetLocalParameter(slot) == local;
                })) {
            error = "Cannot remove a local value";
            return false;
        }
        edit.Bury(id);
        RemoveLocalSlot(data, *FindRecord(data, parentId), slot);
    }

    // 6. Old leaves the graph.
    const int at = SubBehaviorPosition(parent, old);
    if (!edit.Do([context, parentId, oldId] { return DetachSubBehavior(context, parentId, oldId); },
                 [context, parentId, oldId, at] { return AttachSubBehavior(context, parentId, oldId, at); })) {
        error = "CKBehavior::RemoveSubBehavior failed";
        return false;
    }
    edit.Bury(oldId);
    return true;
}

// A new Building Block takes the replaced one's settings (locals of the
// same name and type) and, when its pins are variable, as many pins (named
// and typed as the old ones), each followed by the Building Block's
// callback; then the disabled state of its inputs and outputs. A Building
// Block replaced by its own prototype keeps every pin.
void Conform(Document &document, CKBehavior *old, CKBehavior *fresh) {
    bool settings = false;
    for (int i = 0; i < fresh->GetLocalParameterCount(); ++i) {
        CKParameterLocal *to = fresh->GetLocalParameter(i);
        for (int j = 0; to && j < old->GetLocalParameterCount(); ++j) {
            CKParameterLocal *from = old->GetLocalParameter(j);
            if (from && std::string(NameOf(from)) == NameOf(to) && from->GetGUID() == to->GetGUID()) {
                to->CopyValue(from, FALSE);
                settings = true;
                break;
            }
        }
    }
    if (settings)
        GuardedCall([&] { fresh->CallCallbackFunction(CKM_BEHAVIORSETTINGSEDITED); }, &document.Host(),
                    "Building Block callback");
    bool edited = false;
    const CKDWORD flags = fresh->GetFlags();
    if (flags & CKBEHAVIOR_VARIABLEINPUTS)
        while (fresh->GetInputCount() < old->GetInputCount() &&
               fresh->CreateInput(const_cast<CKSTRING>(NameOf(old->GetInput(fresh->GetInputCount())))))
            edited = true;
    if (flags & CKBEHAVIOR_VARIABLEOUTPUTS)
        while (fresh->GetOutputCount() < old->GetOutputCount() &&
               fresh->CreateOutput(const_cast<CKSTRING>(NameOf(old->GetOutput(fresh->GetOutputCount())))))
            edited = true;
    if (flags & CKBEHAVIOR_VARIABLEPARAMETERINPUTS)
        while (fresh->GetInputParameterCount() < old->GetInputParameterCount()) {
            CKParameterIn *model = old->GetInputParameter(fresh->GetInputParameterCount());
            if (!fresh->CreateInputParameter(const_cast<CKSTRING>(NameOf(model)), model->GetGUID())) break;
            edited = true;
        }
    if (flags & CKBEHAVIOR_VARIABLEPARAMETEROUTPUTS)
        while (fresh->GetOutputParameterCount() < old->GetOutputParameterCount()) {
            CKParameterOut *model = old->GetOutputParameter(fresh->GetOutputParameterCount());
            if (!fresh->CreateOutputParameter(const_cast<CKSTRING>(NameOf(model)), model->GetGUID())) break;
            edited = true;
        }
    if (edited)
        GuardedCall([&] { fresh->CallCallbackFunction(CKM_BEHAVIOREDITED); }, &document.Host(), "Building Block callback");
    for (int i = 0; i < std::min(old->GetInputParameterCount(), fresh->GetInputParameterCount()); ++i)
        fresh->EnableInputParameter(i, old->IsInputParameterEnabled(i));
    for (int i = 0; i < std::min(old->GetOutputParameterCount(), fresh->GetOutputParameterCount()); ++i)
        fresh->EnableOutputParameter(i, old->IsOutputParameterEnabled(i));
}

CK_ID ScriptOf(Document &document, CK_ID behavior) {
    for (const auto &info : document.Scripts())
        if (TreeIds(BehaviorOf(document.Context(), info.id)).count(behavior)) return info.id;
    return 0;
}

// Rewire + values for the replacement's unfed inputs, as one command.
EditResult ReplaceWith(Document &document, CK_ID script, CK_ID behavior, CK_ID replacement,
                       const std::string &label) {
    TopologyEdit edit(document, script, label);
    if (!edit.Valid()) return edit.Fail(edit.Error());
    InterfaceData &data = edit.Data();
    const BehaviorData *oldRecord = FindRecord(data, behavior);
    BehaviorData *newRecord = FindRecord(data, replacement);
    if (!oldRecord || !newRecord) return edit.Fail("The behaviors have no display record");
    newRecord->folded = oldRecord->folded;
    CKBehavior *fresh = BehaviorOf(edit.Context(), replacement);
    CKBehavior *old = BehaviorOf(edit.Context(), behavior);
    if (!fresh || !old) return edit.Fail("The behaviors are not in the runtime graph");
    // Priority and display state follow the replaced graph (the copy is removed again by undo).
    SetPriorityKeepingTies(fresh, old->GetPriority());
    std::string error;
    if (!Rewire(edit, behavior, replacement, error)) return edit.Fail(error);
    newRecord = FindRecord(data, replacement);
    if (!AddInputValueLocals(edit, newRecord->parentId, fresh, *newRecord))
        return edit.Fail("Cannot create the input parameter values");
    return edit.Commit(replacement);
}

} // namespace

EditResult ReplaceBuildingBlock(Document &document, CK_ID script, CK_ID behaviorId,
                                const CKGUID &prototype) {
    TopologyEdit edit(document, script, "Replace Building Block");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    CKBehavior *old = BehaviorOf(context, behaviorId);
    if (!IsBuildingBlock(old) || behaviorId == script || !TreeIds(edit.Script()).count(behaviorId))
        return edit.Fail("Only a Building Block can be replaced by a Building Block");
    const CK_ID parentId = ParentOf(data, behaviorId);
    CKBehavior *parent = BehaviorOf(context, parentId);
    const BehaviorData *oldRecord = FindRecord(data, behaviorId);
    if (!EditableGraph(parent) || !oldRecord) return edit.Fail("The Building Block is not in an editable graph");
    CKObjectDeclaration *declaration = CKGetObjectDeclarationFromGuid(prototype);
    if (!declaration) return edit.Fail("Unknown Building Block prototype");

    std::string error;
    CKBehavior *fresh = NewBuildingBlock(edit, prototype, error);
    if (!fresh) return edit.Fail(error);
    const CK_ID id = fresh->GetID();
    Conform(document, old, fresh);
    edit.Created(id); // with the pins and objects its callbacks made
    if (old->IsUsingTarget() && fresh->IsTargetable()) fresh->UseTarget(TRUE);
    if (!OwnerAccepts(parent, fresh) && (!fresh->IsTargetable() || fresh->UseTarget(TRUE) != CK_OK))
        return edit.Fail(std::string(declaration->GetName()) + " does not apply to the Script owner's class");
    SetPriorityKeepingTies(fresh, old->GetPriority());
    const char *prototypeName = old->GetPrototypeName();
    if (old->GetName() && (!prototypeName || std::string(old->GetName()) != prototypeName)) fresh->SetName(old->GetName());
    const int at = SubBehaviorPosition(parent, old) + 1;
    if (!edit.Do([context, parentId, id, at] { return AttachSubBehavior(context, parentId, id, at); },
                 [context, parentId, id] { return DetachSubBehavior(context, parentId, id); }))
        return edit.Fail("CKBehavior::AddSubBehavior failed");
    BehaviorData record(id);
    record.parentId = parentId;
    record.depth = oldRecord->depth;
    record.isBehaviorGraph = fresh->GetType() != CKBEHAVIORTYPE_BASE;
    record.folded = true;
    record.UpdateRuntimeState(fresh);
    LayoutCalculator::CalculateBehaviorSize(record, fresh);
    record.rect.hPos = oldRecord->rect.hPos;
    record.rect.vPos = oldRecord->rect.vPos;
    auto &records = data.behaviors;
    auto place = std::find_if(records.begin(), records.end(), [&](const BehaviorData &r) { return r.id == behaviorId; });
    records.insert(place == records.end() ? records.end() : place + 1, record);
    data.behaviorCount = static_cast<int>(records.size());
    if (!Rewire(edit, behaviorId, id, error)) return edit.Fail(error);
    if (!AddInputValueLocals(edit, parentId, fresh, *FindRecord(data, id)))
        return edit.Fail("Cannot create the input parameter values");
    return edit.Commit(id);
}

EditResult ReplaceBehaviorGraph(Document &document, CK_ID script, CK_ID behavior,
                                CK_ID source) {
    CKContext *context = document.Context();
    CKBehavior *old = BehaviorOf(context, behavior);
    if (!IsGraph(old) || behavior == script) {
        return EditResult::Refused("Only a behavior graph can be replaced by a graph");
    }
    if (!IsGraph(BehaviorOf(context, source)) || source == behavior) {
        return EditResult::Refused("Choose another behavior graph");
    }
    const CK_ID sourceScript = ScriptOf(document, source);
    ScriptSession *sourceSession = sourceScript ? document.Session(sourceScript) : nullptr;
    ScriptSession *session = document.Session(script);
    if (!sourceSession || !session || !FindRecord(sourceSession->Data(), source)) {
        return EditResult::Refused("The replacing graph is not displayed by a Script");
    }
    const BehaviorData *record = FindRecord(session->Data(), behavior);
    if (!record) {
        return EditResult::Refused("The graph has no display record");
    }
    const CK_ID parent = record->parentId;
    const Point position(record->rect.hPos, record->rect.vPos);
    const InterfaceData sourceData = sourceSession->Data();
    // The copy and the replacement: one undo step.
    Edit edit(document, "Replace Behavior Graph");
    const EditResult pasted = PasteBehaviors(document, sourceData, {source}, {}, script, parent, position);
    if (!pasted.ok || !pasted.created) return pasted;
    const EditResult replaced = ReplaceWith(document, script, behavior, pasted.created, "Replace Behavior Graph");
    if (!replaced.ok) return replaced;
    const EditResult committed = edit.Commit(replaced.message, replaced.created);
    return committed.ok ? replaced : committed;
}

EditResult ReplaceWithFileGraph(Document &document, CK_ID script, CK_ID behavior,
                                const std::string &path) {
    if (!IsGraph(BehaviorOf(document.Context(), behavior)) || behavior == script) {
        return EditResult::Refused("Only a behavior graph can be replaced by a graph");
    }
    ScriptSession *session = document.Session(script);
    const BehaviorData *record = session ? FindRecord(session->Data(), behavior) : nullptr;
    if (!record) {
        return EditResult::Refused("The graph has no display record");
    }
    // The import and the replacement: one undo step.
    Edit edit(document, "Replace Behavior Graph");
    const EditResult imported = ImportBehaviorGraph(document, script, record->parentId, path,
                                                    Point(record->rect.hPos, record->rect.vPos));
    if (!imported.ok || !imported.created) return imported;
    if (!IsGraph(BehaviorOf(document.Context(), imported.created)))
        return edit.Fail("The file does not hold a behavior graph");
    const EditResult replaced = ReplaceWith(document, script, behavior, imported.created, "Replace Behavior Graph");
    if (!replaced.ok) return replaced;
    const EditResult committed = edit.Commit(replaced.message, replaced.created);
    return committed.ok ? replaced : committed;
}

std::vector<EditResult> ReplaceBuildingBlocks(Document &document, const std::vector<std::pair<CK_ID, CK_ID>> &targets,
                                              const CKGUID &prototype) {
    EditGroup group(document, "Replace Building Block");
    std::vector<EditResult> results;
    for (const auto &target : targets) results.push_back(ReplaceBuildingBlock(document, target.first, target.second, prototype));
    return results;
}

std::vector<EditResult> ReplaceBehaviorGraphs(Document &document, const std::vector<std::pair<CK_ID, CK_ID>> &targets,
                                              CK_ID source) {
    EditGroup group(document, "Replace Behavior Graph");
    std::vector<EditResult> results;
    for (const auto &target : targets)
        results.push_back(target.second == source ? EditResult::NoChange("The graph is the replacing one")
                                                  : ReplaceBehaviorGraph(document, target.first, target.second, source));
    return results;
}

std::vector<std::pair<CK_ID, CK_ID>> SameNamedBehaviors(Document &document, CK_ID behaviorId) {
    std::vector<std::pair<CK_ID, CK_ID>> found;
    CKContext *context = document.Context();
    CKBehavior *behavior = BehaviorOf(context, behaviorId);
    if (!behavior) return found;
    const std::string name = NameOf(behavior);
    const bool buildingBlock = IsBuildingBlock(behavior);
    for (const auto &info : document.Scripts())
        for (CK_ID id : TreeIds(BehaviorOf(context, info.id))) {
            CKBehavior *other = BehaviorOf(context, id);
            if (id == info.id || !other || NameOf(other) != name) continue;
            if (buildingBlock ? IsBuildingBlock(other) : IsGraph(other)) found.emplace_back(info.id, id);
        }
    return found;
}

} // namespace vdev

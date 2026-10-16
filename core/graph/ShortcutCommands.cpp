#include "core/graph/ShortcutCommands.h"

#include <algorithm>

#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/schematic/SchematicEdits.h"
#include "core/graph/detail/TopologyInternal.h"

namespace vdev {

using namespace detail;

namespace {

bool SameEndpoint(const LinkEndpoint &endpoint, CK_ID id, int index, EndpointType type) {
    return endpoint.id == id && endpoint.index == index && endpoint.type == type;
}

// Input steps: point an input at another direct source, undoably.
bool Repoint(TopologyEdit &edit, CKParameterIn *input, CK_ID sourceId) {
    CKContext *context = edit.Context();
    const InputSource before = SourceOf(input);
    const InputRef target = before.input;
    return edit.Do(
        [context, target, sourceId] {
            CKParameterIn *in = Resolve(context, target);
            CKParameter *source = CKParameter::Cast(context->GetObject(sourceId));
            return in && source && in->SetDirectSource(source) == CK_OK;
        },
        [context, before] { return RestoreSource(context, before); });
}

// Every input of the Script tree (behaviors and operations) whose direct source is `parameter`.
std::vector<CKParameterIn *> InputsFedBy(CKBehavior *script, CKParameter *parameter) {
    std::vector<CKParameterIn *> inputs;
    std::vector<CKBehavior *> pending{script};
    while (!pending.empty()) {
        CKBehavior *behavior = pending.back();
        pending.pop_back();
        auto check = [&](CKParameterIn *input) {
            if (input && input->GetDirectSource() == parameter) inputs.push_back(input);
        };
        for (int i = 0; i < behavior->GetInputParameterCount(); ++i) check(behavior->GetInputParameter(i));
        if (behavior->IsUsingTarget()) check(behavior->GetTargetParameter());
        for (int i = 0; i < behavior->GetParameterOperationCount(); ++i)
            if (CKParameterOperation *operation = behavior->GetParameterOperation(i)) {
                check(operation->GetInParameter1());
                check(operation->GetInParameter2());
            }
        for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i)
            if (CKBehavior *child = behavior->GetSubBehavior(i)) pending.push_back(child);
    }
    return inputs;
}

} // namespace

EditResult RetargetShortcutsInAllScripts(Document &document, CK_ID from, CK_ID to) {
    Edit edit(document, "Set Shortcut Source");
    size_t changed = 0;
    // A copy: the edits list the Scripts again.
    const std::vector<ScriptInfo> scripts = document.Scripts();
    for (const ScriptInfo &info : scripts) {
        const EditResult result = RetargetShortcuts(document, info.id, from, to);
        if (result.ok) ++changed;
        else if (!result.Unchanged()) return edit.Fail(result.message);
    }
    if (!changed) return edit.Unchanged("No shortcut of this parameter in the composition");
    return edit.Commit(std::to_string(changed) + " Script(s)");
}

EditResult ShortcutizeLocalsInAllScripts(Document &document, CK_ID source) {
    std::vector<CK_ID> scripts;
    for (const ScriptInfo &info : document.Scripts()) scripts.push_back(info.id);
    return ShortcutizeLocalsInScripts(document, scripts, source);
}

EditResult ShortcutizeLocalsInScripts(Document &document, const std::vector<CK_ID> &scripts, CK_ID source) {
    Edit edit(document, "Convert to Shortcut Source");
    size_t changed = 0;
    for (CK_ID script : scripts)
        if (ShortcutizeLocals(document, script, source).ok) ++changed;
    if (!changed) return edit.Unchanged("No other local with the same name, type and value");
    return edit.Commit(std::to_string(changed) + " Script(s)");
}

EditResult RetargetShortcuts(Document &document, CK_ID scriptId, CK_ID from, CK_ID to) {
    TopologyEdit edit(document, scriptId, "Set Shortcut Source");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKParameter *source = CKParameter::Cast(context->GetObject(to));
    CKParameterManager *manager = context->GetParameterManager();
    if (!source || !manager) return edit.Fail("The new shortcut source must be a local parameter or an output");
    if (from == to) return edit.Fail("The shortcut already shows this parameter");
    bool any = false;
    std::string error;
    ForEachRecord(edit.Data(), [&](BehaviorData &record) {
        for (int i = 0; i < static_cast<int>(record.sharedParams.size()) && error.empty(); ++i) {
            if (record.sharedParams[i].sourceId != from) continue;
            any = true;
            for (const auto &link : record.links) {
                if (!SameEndpoint(link.start, record.id, i, ENDPOINT_POUT_SHORTCUT)) continue;
                CKParameterIn *input = CKParameterIn::Cast(ResolveEndpointObject(context, record, link.end));
                if (!input) continue;
                if (!manager->IsTypeCompatible(source->GetType(), input->GetType())) {
                    error = "The new source's type does not fit an input of the shortcut";
                    break;
                }
                if (!Repoint(edit, input, to)) error = "An input refused the new source";
            }
            record.sharedParams[i].sourceId = to;
            record.sharedParams[i].id = to;
        }
    });
    if (!error.empty()) return edit.Fail(error);
    if (!any) return edit.Unchanged("No shortcut of this parameter in the Script");
    return edit.Commit();
}

EditResult ShortcutizeLocals(Document &document, CK_ID scriptId, CK_ID sourceId) {
    TopologyEdit edit(document, scriptId, "Convert to Shortcut Source");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKParameterLocal *source = CKParameterLocal::Cast(context->GetObject(sourceId));
    if (!source) return edit.Fail("Select a local parameter");
    const std::string name = NameOf(source);
    const CKGUID type = source->GetGUID();
    const std::string value = ParameterText(source);
    int converted = 0;
    std::string error;
    ForEachRecord(edit.Data(), [&](BehaviorData &record) {
        if (!error.empty()) return;
        CKBehavior *graph = BehaviorOf(context, record.id);
        if (!graph || record.localParams.size() != static_cast<size_t>(graph->GetLocalParameterCount())) return;
        // Last slot first: removing a local renumbers the later ones.
        for (int slot = graph->GetLocalParameterCount() - 1; slot >= 0 && error.empty(); --slot) {
            CKParameterLocal *local = graph->GetLocalParameter(slot);
            if (!local || local == source || local->IsMyselfParameter() || NameOf(local) != name ||
                local->GetGUID() != type || ParameterText(local) != value)
                continue;
            // A local written by an output keeps its own value.
            bool written = false;
            for (const auto &link : record.links)
                written = written || SameEndpoint(link.end, record.id, slot, ENDPOINT_PLOCAL);
            if (written) continue;
            // CK: every input it fed takes the source.
            for (CKParameterIn *input : InputsFedBy(edit.Script(), local))
                if (!Repoint(edit, input, sourceId)) {
                    error = "An input refused the shortcut source";
                    return;
                }
            // Display: a shortcut where the local was; its links start there.
            Parameter shortcut = record.localParams[slot];
            shortcut.id = 0; // not stored: reads back 0
            shortcut.sourceId = sourceId;
            record.sharedParams.push_back(shortcut);
            const int shortcutSlot = static_cast<int>(record.sharedParams.size()) - 1;
            for (auto &link : record.links) {
                for (LinkEndpoint *endpoint : {&link.start, &link.end}) {
                    if (SameEndpoint(*endpoint, record.id, slot, ENDPOINT_PLOCAL))
                        *endpoint = LinkEndpoint(record.id, shortcutSlot, ENDPOINT_POUT_SHORTCUT);
                    else if (endpoint->type == ENDPOINT_PLOCAL && endpoint->id == record.id && endpoint->index > slot)
                        --endpoint->index;
                }
            }
            record.localParams.erase(record.localParams.begin() + slot);
            // CK: the local leaves its graph (soft delete).
            const CK_ID graphId = record.id, localId = local->GetID();
            if (!edit.Do(
                    [context, graphId, slot, localId] {
                        CKBehavior *owner = BehaviorOf(context, graphId);
                        return owner && owner->RemoveLocalParameter(slot) == context->GetObject(localId);
                    },
                    [context, graphId, slot, localId] {
                        CKBehavior *owner = BehaviorOf(context, graphId);
                        CKParameterLocal *parameter = CKParameterLocal::Cast(context->GetObject(localId));
                        if (!owner || !parameter) return false;
                        return InsertLocalParameter(owner, slot, parameter);
                    })) {
                error = "Cannot remove the local parameter";
                return;
            }
            edit.Bury(localId);
            ++converted;
        }
    });
    if (!error.empty()) return edit.Fail(error);
    if (!converted) return edit.Fail("No other local with the same name, type and value in the Script");
    EditResult result = edit.Commit();
    if (result.ok) result.message = std::to_string(converted) + " local(s) turned into shortcuts";
    return result;
}

} // namespace vdev

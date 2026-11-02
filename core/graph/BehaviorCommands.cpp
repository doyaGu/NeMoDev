// Building Block edits that go through the Building Block's own callback:
// settings (Dev "Edit Settings", S) and pins of variable Building Blocks
// (Dev "Construct"). The callback may enable, disable, add or remove pins;
// undo restores the previous settings / removes the added pin and calls the
// callback again, so the Building Block rebuilds its previous pins.
#include "core/graph/BehaviorCommands.h"

#include <algorithm>
#include <memory>

#include "core/runtime/CkRuntimeHost.h"
#include "core/schematic/LayoutCalculator.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/schematic/SchematicEdits.h"
#include "core/graph/detail/TopologyInternal.h"

namespace vdev {

using namespace detail;

namespace {

struct PinCounts {
    int inputs = 0, outputs = 0, parameterInputs = 0, parameterOutputs = 0;
    bool target = false;
    bool operator==(const PinCounts &other) const {
        return inputs == other.inputs && outputs == other.outputs && parameterInputs == other.parameterInputs &&
               parameterOutputs == other.parameterOutputs && target == other.target;
    }
};

PinCounts CountPins(CKBehavior *behavior) {
    PinCounts counts;
    if (!behavior) return counts;
    counts.inputs = behavior->GetInputCount();
    counts.outputs = behavior->GetOutputCount();
    counts.parameterInputs = behavior->GetInputParameterCount();
    counts.parameterOutputs = behavior->GetOutputParameterCount();
    counts.target = behavior->IsUsingTarget() != FALSE;
    return counts;
}

// Objects the callback created belong to the document (saved with it).
void Register(Document &document, CK_ID behaviorId) {
    for (CK_ID id : CollectOwnedObjects(document.Context(), {behaviorId})) document.Include(id);
}

bool Callback(Document &document, CK_ID behaviorId, CKDWORD message) {
    CKBehavior *behavior = BehaviorOf(document.Context(), behaviorId);
    if (!behavior) return false;
    // A callback that raises fails the step (the callers put back what it did).
    const bool ok =
        GuardedCall([&] { behavior->CallCallbackFunction(message); }, &document.Host(), "Building Block callback");
    Register(document, behaviorId);
    return ok;
}

// Locals feeding the Building Block's inputs, and its own settings: a
// callback may change an input's type (and so the linked local's, its value
// reset) or write a setting (2.1's Calculator), so commands restore their
// types and values after the callback.
struct LocalState {
    ObjectRef object{};
    CKGUID type;
    std::string text;
};
using LocalStates = std::vector<LocalState>;

LocalStates CaptureLocals(const Document &document, CKBehavior *behavior) {
    LocalStates states;
    if (!behavior) return states;
    for (int i = 0; i < behavior->GetInputParameterCount(); ++i) {
        CKParameterIn *input = behavior->GetInputParameter(i);
        CKParameterLocal *local = input ? CKParameterLocal::Cast(input->GetDirectSource()) : nullptr;
        if (!local || local->GetOwner() == behavior) continue;
        states.push_back({document.Reference(local->GetID()), local->GetGUID(), ParameterText(local)});
    }
    for (int i = 0; i < behavior->GetLocalParameterCount(); ++i)
        if (CKParameterLocal *local = behavior->GetLocalParameter(i))
            states.push_back({document.Reference(local->GetID()), local->GetGUID(), ParameterText(local)});
    return states;
}

void RestoreLocals(Document &document, const LocalStates &states) {
    for (const auto &state : states) {
        CKParameterLocal *local = CKParameterLocal::Cast(
            document.Resolve(state.object, CKCID_PARAMETERLOCAL, Document::ObjectScope::Existing));
        if (!local) continue;
        if (local->GetGUID() != state.type) local->SetGUID(state.type);
        if (ParameterText(local) != state.text) local->SetStringValue(const_cast<CKSTRING>(state.text.c_str()));
    }
}

bool ApplySettings(Document &document, ObjectRef object, const std::map<int, std::string> &texts,
                   const LocalStates *locals) {
    auto *runtime = CKBehavior::Cast(document.Resolve(object, CKCID_BEHAVIOR, Document::ObjectScope::Existing));
    if (!runtime)
        return false;
    for (const auto &text : texts) {
        auto *local = runtime->GetLocalParameter(text.first);
        if (!local || local->SetStringValue(const_cast<CKSTRING>(text.second.c_str())) != CK_OK)
            return false;
    }
    const bool ok = Callback(document, runtime->GetID(), CKM_BEHAVIORSETTINGSEDITED);
    if (locals)
        RestoreLocals(document, *locals);
    return ok;
}
class BehaviorSettingsAction final : public Action {
public:
    BehaviorSettingsAction(ObjectRef target, std::map<int, std::string> before, std::map<int, std::string> after,
                           LocalStates locals)
        : object(target), before(std::move(before)), after(std::move(after)), localsBefore(std::move(locals)) {}
    bool Redo(Document &document) override {
        if (!ApplySettings(document, object, after, captured ? &localsAfter : nullptr))
            return false;
        if (!captured) {
            localsAfter = CaptureLocals(document, CKBehavior::Cast(document.Resolve(object)));
            captured = true;
        }
        return true;
    }
    bool Undo(Document &document) override { return ApplySettings(document, object, before, &localsBefore); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        if (auto id = document.NativeId(object, Document::ObjectScope::Existing))
            objects.insert(id);
        for (const auto *states : {&localsBefore, &localsAfter})
            for (const auto &local : *states)
                if (auto id = document.NativeId(local.object, Document::ObjectScope::Existing))
                    objects.insert(id);
    }

private:
    ObjectRef object{};
    std::map<int, std::string> before, after;
    LocalStates localsBefore, localsAfter;
    bool captured = false;
};
class VariablePinAction final : public Action {
public:
    VariablePinAction(ObjectRef target, PinKind pin, std::string name, CKGUID guid, PinCounts counts,
                      LocalStates locals)
        : object(target), kind(pin), label(std::move(name)), type(guid), pins(counts), before(std::move(locals)) {}
    bool Redo(Document &document) override {
        if (!Apply(document, true))
            return false;
        if (captured)
            RestoreLocals(document, after);
        else {
            after = CaptureLocals(document, CKBehavior::Cast(document.Resolve(object)));
            captured = true;
        }
        return true;
    }
    bool Undo(Document &document) override {
        const bool ok = Apply(document, false);
        RestoreLocals(document, before);
        return ok;
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        for (auto ref : {object})
            if (auto id = document.NativeId(ref, Document::ObjectScope::Existing))
                objects.insert(id);
        for (const auto *states : {&before, &after})
            for (const auto &local : *states)
                if (auto id = document.NativeId(local.object, Document::ObjectScope::Existing))
                    objects.insert(id);
    }

private:
    bool Apply(Document &document, bool add) {
        const auto behaviorId = document.NativeId(object, Document::ObjectScope::Existing);
        if (!behaviorId)
            return false;
        auto *context = document.Context();
        if (add) {
            CKBehavior *runtime = BehaviorOf(context, behaviorId);
            if (!runtime)
                return false;
            CKSTRING text = const_cast<CKSTRING>(label.c_str());
            CKObject *created = nullptr;
            switch (kind) {
            case PinKind::BehaviorIn:
                created = runtime->CreateInput(text);
                break;
            case PinKind::BehaviorOut:
                created = runtime->CreateOutput(text);
                break;
            case PinKind::ParamIn:
                created = runtime->CreateInputParameter(text, type);
                break;
            case PinKind::ParamOut:
                created = runtime->CreateOutputParameter(text, type);
                break;
            default:
                break;
            }
            return created && Callback(document, behaviorId, CKM_BEHAVIOREDITED);
        }
        CKBehavior *runtime = BehaviorOf(context, behaviorId);
        if (!runtime)
            return false;
        CKObject *last = nullptr;
        switch (kind) {
        case PinKind::BehaviorIn:
            if (runtime->GetInputCount() > pins.inputs)
                last = runtime->GetInput(runtime->GetInputCount() - 1);
            if (last)
                runtime->DeleteInput(runtime->GetInputCount() - 1);
            break;
        case PinKind::BehaviorOut:
            if (runtime->GetOutputCount() > pins.outputs)
                last = runtime->GetOutput(runtime->GetOutputCount() - 1);
            if (last)
                runtime->DeleteOutput(runtime->GetOutputCount() - 1);
            break;
        case PinKind::ParamIn:
            if (runtime->GetInputParameterCount() > pins.parameterInputs) {
                last = runtime->RemoveInputParameter(runtime->GetInputParameterCount() - 1);
                if (last)
                    context->DestroyObject(last);
            }
            break;
        case PinKind::ParamOut:
            if (runtime->GetOutputParameterCount() > pins.parameterOutputs) {
                last = runtime->RemoveOutputParameter(runtime->GetOutputParameterCount() - 1);
                if (last)
                    context->DestroyObject(last);
            }
            break;
        default:
            break;
        }
        return last != nullptr && Callback(document, behaviorId, CKM_BEHAVIOREDITED);
    }
    ObjectRef object{};
    PinKind kind;
    std::string label;
    CKGUID type;
    PinCounts pins;
    LocalStates before, after;
    bool captured = false;
};

// The display record follows the Building Block's new pins: its size is
// recomputed when the pin counts changed, and new unsourced inputs get a
// value local (as an inserted Building Block).
void FollowPins(TopologyEdit &edit, CK_ID graphId, CK_ID behaviorId, const PinCounts &before) {
    CKBehavior *behavior = BehaviorOf(edit.Context(), behaviorId);
    BehaviorData *record = FindRecord(edit.Data(), behaviorId);
    if (!behavior || !record) return;
    record->UpdateRuntimeState(behavior);
    if (!(CountPins(behavior) == before)) {
        const Rect placed = record->rect;
        LayoutCalculator::CalculateBehaviorSize(*record, behavior);
        record->rect.hPos = placed.hPos;
        record->rect.vPos = placed.vPos;
    }
    const BehaviorData copy = *record;
    AddInputValueLocals(edit, graphId, behavior, copy, nullptr);
}

} // namespace

std::vector<BehaviorSetting> BehaviorSettings(CKBehavior *behavior) {
    std::vector<BehaviorSetting> settings;
    if (!behavior || !(behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK)) return settings;
    CKParameterManager *manager = behavior->GetCKContext()->GetParameterManager();
    for (int i = 0; i < behavior->GetLocalParameterCount(); ++i) {
        CKParameterLocal *local = behavior->GetLocalParameter(i);
        if (!local) continue;
        BehaviorSetting setting;
        setting.slot = i;
        setting.id = local->GetID();
        setting.name = local->GetName() ? local->GetName() : "";
        const char *type = manager ? manager->ParameterTypeToName(local->GetType()) : nullptr;
        setting.type = type ? type : "";
        setting.value = ParameterText(local);
        settings.push_back(setting);
    }
    return settings;
}

EditResult EditBehaviorSettings(Document &document, CK_ID scriptId, CK_ID behaviorId,
                                const std::map<int, std::string> &values) {
    TopologyEdit edit(document, scriptId, "Edit Settings");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehavior *behavior = BehaviorOf(context, behaviorId);
    if (!behavior || !TreeIds(edit.Script()).count(behaviorId) || !(behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK))
        return edit.Fail("Select a Building Block of this Script");
    std::map<int, std::string> before, after;
    for (const auto &value : values) {
        CKParameterLocal *local = behavior->GetLocalParameter(value.first);
        if (!local) return edit.Fail("Unknown setting");
        const std::string old = ParameterText(local);
        if (old == value.second) continue;
        before[value.first] = old;
        after[value.first] = value.second;
    }
    if (after.empty()) return edit.Unchanged("The settings are unchanged");
    const CK_ID graphId = ParentOf(edit.Data(), behaviorId);
    const PinCounts pins = CountPins(behavior);
    const auto reference = document.Reference(behaviorId);
    const LocalStates localsBefore = CaptureLocals(document, behavior);
    if (!edit.Do(std::make_unique<BehaviorSettingsAction>(reference, before, after, localsBefore))) {
        ApplySettings(document, reference, before, &localsBefore);
        return edit.Fail("A setting does not accept this value");
    }
    FollowPins(edit, graphId, behaviorId, pins);
    return edit.Commit();
}

bool AcceptsVariablePin(CKBehavior *behavior, PinKind kind) {
    if (!behavior || !(behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK)) return false;
    const CKDWORD flags = behavior->GetFlags();
    switch (kind) {
    case PinKind::BehaviorIn: return (flags & CKBEHAVIOR_VARIABLEINPUTS) != 0;
    case PinKind::BehaviorOut: return (flags & CKBEHAVIOR_VARIABLEOUTPUTS) != 0;
    case PinKind::ParamIn: return (flags & CKBEHAVIOR_VARIABLEPARAMETERINPUTS) != 0;
    case PinKind::ParamOut: return (flags & CKBEHAVIOR_VARIABLEPARAMETEROUTPUTS) != 0;
    default: return false;
    }
}

EditResult AddBuildingBlockPin(Document &document, CK_ID scriptId, CK_ID behaviorId,
                               PinKind kind, const std::string &name, const CKGUID &type) {
    TopologyEdit edit(document, scriptId, "Construct");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehavior *behavior = BehaviorOf(context, behaviorId);
    if (!behavior || !TreeIds(edit.Script()).count(behaviorId)) return edit.Fail("Select a Building Block of this Script");
    if (!AcceptsVariablePin(behavior, kind)) return edit.Fail("This Building Block has a fixed set of these pins");
    const CK_ID graphId = ParentOf(edit.Data(), behaviorId);
    const PinCounts pins = CountPins(behavior);
    const std::string label = !name.empty() ? name
                              : kind == PinKind::BehaviorIn ? "In " + std::to_string(pins.inputs)
                              : kind == PinKind::BehaviorOut ? "Out " + std::to_string(pins.outputs)
                              : kind == PinKind::ParamIn ? "pIn " + std::to_string(pins.parameterInputs)
                                                          : "pOut " + std::to_string(pins.parameterOutputs);
    const LocalStates localsBefore = CaptureLocals(document, behavior);
    const auto reference = document.Reference(behaviorId);
    auto action = std::make_unique<VariablePinAction>(reference, kind, label, type, pins, localsBefore);
    // Keep only the value record for failure compensation; the Edit owns
    // the successful action. The failed action is never in history.
    VariablePinAction recovery(reference, kind, label, type, pins, localsBefore);
    if (!edit.Do(std::move(action))) {
        recovery.Undo(document);
        return edit.Fail("The Building Block refused the new pin");
    }
    FollowPins(edit, graphId, behaviorId, pins);
    return edit.Commit();
}

} // namespace vdev

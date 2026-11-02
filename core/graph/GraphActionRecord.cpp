#include "core/graph/detail/GraphActions.h"

#include "core/graph/RuntimeGraph.h"
#include "core/runtime/CkRuntimeHost.h"

namespace vdev {
namespace {

CKObject *Object(Document &document, ObjectRef ref) {
    return document.Resolve(ref, CKCID_OBJECT, Document::ObjectScope::Existing);
}
ObjectRef Reference(const Document &document, CK_ID id) {
    auto ref = document.Reference(id);
    if (id && ref == ObjectRef{})
        return {document.Token(), id, 0};
    return ref;
}
GraphParameter Input(const Document &document, const detail::InputRef &ref) {
    return {ref.owner ? ObjectRef{} : Reference(document, ref.id), Reference(document, ref.owner), ref.slot};
}
GraphIo Io(const Document &document, const detail::IoRef &ref) {
    return {Reference(document, ref.owner), ref.input, ref.index};
}
GraphParameter Parameter(const Document &document, CK_ID id) {
    const auto reference = Reference(document, id);
    auto *output = CKParameterOut::Cast(document.Resolve(reference, CKCID_OBJECT, Document::ObjectScope::Existing));
    if (output) {
        if (auto *owner = CKBehavior::Cast(output->GetOwner())) {
            const auto slot = owner->GetOutputParameterPosition(output);
            if (slot >= 0) return {{}, Reference(document, owner->GetID()), slot, false};
        } else if (auto *owner = CKParameterOperation::Cast(output->GetOwner())) {
            if (owner->GetOutParameter() == output) return {{}, Reference(document, owner->GetID()), 0, false};
        }
    }
    return {reference};
}
CKObject *ResolveParameter(Document &document, const GraphParameter &ref) {
    if (ref.owner == ObjectRef{}) return Object(document, ref.object);
    auto *owner = Object(document, ref.owner);
    if (!owner) return nullptr;
    if (ref.input) return detail::Resolve(document.Context(), detail::InputRef{0, owner->GetID(), ref.slot});
    if (auto *behavior = CKBehavior::Cast(owner))
        return ref.slot >= 0 && ref.slot < behavior->GetOutputParameterCount() ? behavior->GetOutputParameter(ref.slot) : nullptr;
    if (auto *operation = CKParameterOperation::Cast(owner)) return ref.slot == 0 ? operation->GetOutParameter() : nullptr;
    return nullptr;
}
CKParameterIn *ResolveInput(Document &document, const GraphParameter &ref) {
    return CKParameterIn::Cast(ResolveParameter(document, ref));
}
bool Bound(const GraphParameter &ref) { return ref.object != ObjectRef{} || ref.owner != ObjectRef{}; }
detail::IoRef NativeIo(Document &document, const GraphIo &ref) {
    auto *owner = CKBehavior::Cast(Object(document, ref.owner));
    return {owner ? owner->GetID() : 0, ref.input, ref.slot};
}
bool Apply(Document &document, const GraphAttachment &change) {
    auto *owner = CKBehavior::Cast(Object(document, change.owner));
    auto *object = Object(document, change.object);
    if (!owner || !object)
        return false;
    auto *context = document.Context();
    const auto graph = owner->GetID(), id = object->GetID();
    const auto slot = change.slot;
    using L = GraphList;
    switch (change.list) {
    case L::Behaviors:
        return change.insert ? detail::AttachSubBehavior(context, graph, id, slot)
                             : detail::DetachSubBehavior(context, graph, id);
    case L::Links:
        return change.insert ? detail::AttachLink(context, graph, id, slot, NativeIo(document, change.in),
                                                  NativeIo(document, change.out))
                             : detail::DetachLink(context, graph, id);
    case L::Operations:
        return change.insert ? detail::AttachOperation(context, graph, id, slot)
                             : detail::DetachOperation(context, graph, id);
    case L::InputIo:
    case L::OutputIo:
        return change.insert ? detail::InsertIo(context, graph, change.list == L::InputIo, slot, id)
                             : detail::RemoveIo(context, graph, change.list == L::InputIo, slot, id);
    case L::Locals: {
        auto *parameter = CKParameterLocal::Cast(object);
        if (!parameter)
            return false;
        return change.insert
                   ? InsertLocalParameter(owner, slot, parameter)
                   : owner->GetLocalParameter(slot) == parameter && owner->RemoveLocalParameter(slot) == parameter;
    }
    case L::Inputs:
    case L::ExportedInputs: {
        auto *parameter = CKParameterIn::Cast(object);
        if (!parameter)
            return false;
        if (!change.insert)
            return owner->GetInputParameter(slot) == parameter && owner->RemoveInputParameter(slot) == parameter;
        if (change.list == L::Inputs)
            return InsertInputParameter(owner, slot, parameter);
        if (slot < 0 || slot > owner->GetInputParameterCount())
            return false;
        std::vector<CKParameterIn *> after;
        while (owner->GetInputParameterCount() > slot)
            after.insert(after.begin(), owner->RemoveInputParameter(owner->GetInputParameterCount() - 1));
        owner->ExportInputParameter(parameter);
        for (auto *other : after)
            owner->ExportInputParameter(other);
        return owner->GetInputParameter(slot) == parameter;
    }
    case L::Outputs:
    case L::ExportedOutputs: {
        auto *parameter = CKParameterOut::Cast(object);
        if (!parameter)
            return false;
        if (!change.insert)
            return owner->GetOutputParameter(slot) == parameter && owner->RemoveOutputParameter(slot) == parameter;
        if (change.list == L::Outputs)
            return InsertOutputParameter(owner, slot, parameter);
        if (slot < 0 || slot > owner->GetOutputParameterCount())
            return false;
        std::vector<CKParameterOut *> after;
        while (owner->GetOutputParameterCount() > slot)
            after.insert(after.begin(), owner->RemoveOutputParameter(owner->GetOutputParameterCount() - 1));
        owner->ExportOutputParameter(parameter);
        for (auto *other : after)
            owner->ExportOutputParameter(other);
        return owner->GetOutputParameter(slot) == parameter;
    }
    }
    return false;
}
bool Apply(Document &document, const GraphSource &source) {
    auto *input = ResolveInput(document, source.input);
    if (!input)
        return false;
    if (source.shared.owner != ObjectRef{} || source.shared.object != ObjectRef{}) {
        auto *shared = ResolveInput(document, source.shared);
        return shared && input->ShareSourceWith(shared) == CK_OK;
    }
    auto *direct = CKParameter::Cast(ResolveParameter(document, source.direct));
    return (!Bound(source.direct) || direct) && input->SetDirectSource(direct) == CK_OK;
}
bool Apply(Document &document, const GraphDestinations &change) {
    auto *output = CKParameterOut::Cast(ResolveParameter(document, change.output));
    if (!output)
        return false;
    std::vector<CKParameter *> targets;
    for (auto ref : change.destinations) {
        auto *target = CKParameter::Cast(ResolveParameter(document, ref));
        if (!target)
            return false;
        targets.push_back(target);
    }
    if (change.mode == GraphDestinationMode::Replace)
        output->RemoveAllDestinations();
    for (auto *target : targets) {
        if (change.mode == GraphDestinationMode::Remove)
            output->RemoveDestination(target);
        else if (output->AddDestination(target, change.copyValue) != CK_OK)
            return false;
    }
    return true;
}
bool Apply(Document &document, const GraphText &change) {
    auto *object = Object(document, change.object);
    if (!object)
        return false;
    if (change.parameter) {
        auto *parameter = CKParameter::Cast(object);
        return parameter && parameter->SetStringValue(const_cast<CKSTRING>(change.value.c_str())) == CK_OK;
    }
    object->SetName(const_cast<CKSTRING>(change.value.c_str()));
    return true;
}
bool Apply(Document &document, const GraphInteger &change) {
    auto *object = Object(document, change.object);
    switch (change.property) {
    case GraphIntegerProperty::Priority:
        if (auto *behavior = CKBehavior::Cast(object)) {
            SetPriorityKeepingTies(behavior, change.value);
            return true;
        }
        break;
    case GraphIntegerProperty::LinkDelay:
        if (auto *link = CKBehaviorLink::Cast(object)) {
            link->SetInitialActivationDelay(change.value);
            link->SetActivationDelay(change.value);
            return true;
        }
        break;
    case GraphIntegerProperty::UseTarget:
        if (auto *behavior = CKBehavior::Cast(object))
            return behavior->UseTarget(change.value) == CK_OK;
        break;
    }
    return false;
}
bool Apply(Document &document, const GraphLocalMove &change) {
    auto *from = CKBehavior::Cast(Object(document, change.from)), *to = CKBehavior::Cast(Object(document, change.to));
    auto *parameter = CKParameterLocal::Cast(Object(document, change.parameter));
    if (!from || !to || !parameter || from->GetLocalParameter(change.fromSlot) != parameter ||
        from->RemoveLocalParameter(change.fromSlot) != parameter)
        return false;
    if (!InsertLocalParameter(to, change.toSlot, parameter))
        return false;
    parameter->SetOwner(to);
    return to->GetLocalParameter(change.toSlot) == parameter;
}

template <class Keep> void References(const GraphParameter &v, Keep keep) {
    keep(v.object);
    keep(v.owner);
}
template <class Keep> void References(const GraphIo &v, Keep keep) {
    keep(v.owner);
}
template <class Keep> void References(const GraphAttachment &v, Keep keep) {
    keep(v.owner);
    keep(v.object);
    References(v.in, keep);
    References(v.out, keep);
}
template <class Keep> void References(const GraphSource &v, Keep keep) {
    References(v.input, keep);
    References(v.direct, keep);
    References(v.shared, keep);
}
template <class Keep> void References(const GraphDestinations &v, Keep keep) {
    References(v.output, keep);
    for (const auto &ref : v.destinations)
        References(ref, keep);
}
template <class Keep> void References(const GraphText &v, Keep keep) {
    keep(v.object);
}
template <class Keep> void References(const GraphInteger &v, Keep keep) {
    keep(v.object);
}
template <class Keep> void References(const GraphLocalMove &v, Keep keep) {
    keep(v.from);
    keep(v.to);
    keep(v.parameter);
}

class RecordedGraphAction final : public Action {
public:
    explicit RecordedGraphAction(GraphActionRecord record) : m_Record(std::move(record)) {}
    bool Redo(Document &document) override { return Run(document, m_Record.after); }
    bool Undo(Document &document) override { return Run(document, m_Record.before); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        const auto keep = [&](ObjectRef ref) {
            if (const auto id = document.NativeId(ref, Document::ObjectScope::Existing))
                objects.insert(id);
        };
        for (const auto *mutation : {&m_Record.after, &m_Record.before})
            for (const auto &operation : mutation->operations)
                std::visit([&](const auto &v) { References(v, keep); }, operation);
    }

private:
    bool Run(Document &document, const GraphMutation &mutation) {
        if (!m_Record.document.value || document.Token() != m_Record.document || mutation.operations.empty())
            return false;
        for (const auto &operation : mutation.operations) {
            if (operation.valueless_by_exception())
                return false;
            bool valid = true;
            std::visit(
                [&](const auto &v) {
                    References(v, [&](ObjectRef ref) {
                        if (ref != ObjectRef{} && !Object(document, ref))
                            valid = false;
                    });
                },
                operation);
            if (!valid)
                return false;
        }
        // Edit and UndoStep protect native calls. Validate the complete
        // mutation before its first write, rather than discovering an old
        // lifetime after earlier operations have already changed the graph.
        for (const auto &operation : mutation.operations)
            if (!std::visit([&](const auto &v) { return Apply(document, v); }, operation))
                return false;
        return true;
    }
    GraphActionRecord m_Record;
};
} // namespace

std::unique_ptr<Action> RestoreGraphAction(GraphActionRecord record) {
    return std::make_unique<RecordedGraphAction>(std::move(record));
}

bool TopologyEdit::Do(GraphMutation after, GraphMutation before) {
    return m_Edit.Do(RestoreGraphAction({m_Document.Token(), std::move(after), std::move(before)}));
}
void TopologyEdit::Done(GraphMutation after, GraphMutation before) {
    m_Edit.Done(RestoreGraphAction({m_Document.Token(), std::move(after), std::move(before)}));
}
bool TopologyEdit::Do(std::unique_ptr<Action> action) {
    return m_Edit.Do(std::move(action));
}
void TopologyEdit::Done(std::unique_ptr<Action> action) {
    m_Edit.Done(std::move(action));
}

namespace detail {
GraphMutation GraphParameterList(const Document &d, CK_ID owner, CK_ID object, int slot, GraphList list, bool insert) {
    return {{GraphAttachment{list, Reference(d, owner), Reference(d, object), slot, insert, {}, {}}}};
}
GraphMutation GraphAttachSubBehavior(const Document &d, CK_ID o, CK_ID c, int slot) {
    return GraphParameterList(d, o, c, slot, GraphList::Behaviors, true);
}
GraphMutation GraphDetachSubBehavior(const Document &d, CK_ID o, CK_ID c) {
    return GraphParameterList(d, o, c, -1, GraphList::Behaviors, false);
}
GraphMutation GraphAttachOperation(const Document &d, CK_ID o, CK_ID c, int slot) {
    return GraphParameterList(d, o, c, slot, GraphList::Operations, true);
}
GraphMutation GraphDetachOperation(const Document &d, CK_ID o, CK_ID c) {
    return GraphParameterList(d, o, c, -1, GraphList::Operations, false);
}
GraphMutation GraphAttachLink(const Document &d, CK_ID o, CK_ID c, int slot, const IoRef &in, const IoRef &out) {
    return {{GraphAttachment{GraphList::Links, Reference(d, o), Reference(d, c), slot, true, Io(d, in), Io(d, out)}}};
}
GraphMutation GraphDetachLink(const Document &d, CK_ID o, CK_ID c) {
    return GraphParameterList(d, o, c, -1, GraphList::Links, false);
}
GraphMutation GraphInsertIo(const Document &d, CK_ID o, bool input, int slot, CK_ID c) {
    return GraphParameterList(d, o, c, slot, input ? GraphList::InputIo : GraphList::OutputIo, true);
}
GraphMutation GraphRemoveIo(const Document &d, CK_ID o, bool input, int slot, CK_ID c) {
    return GraphParameterList(d, o, c, slot, input ? GraphList::InputIo : GraphList::OutputIo, false);
}
GraphMutation GraphSetSource(const Document &d, const InputRef &in, CK_ID direct, const InputRef &shared) {
    return {{GraphSource{Input(d, in), Parameter(d, direct), Input(d, shared)}}};
}
GraphMutation GraphRestoreSource(const Document &d, const InputSource &source) {
    return GraphSetSource(d, source.input, source.direct, source.shared);
}
GraphMutation GraphRestoreDestinations(const Document &d, CK_ID output, const std::vector<CK_ID> &destinations) {
    GraphDestinations value;
    value.output = Parameter(d, output);
    for (auto id : destinations)
        value.destinations.push_back(Parameter(d, id));
    return {{std::move(value)}};
}
GraphMutation GraphAppendDestinations(const Document &d, CK_ID output, const std::vector<CK_ID> &destinations) {
    auto result = GraphRestoreDestinations(d, output, destinations);
    std::get<GraphDestinations>(result.operations.front()).mode = GraphDestinationMode::Add;
    return result;
}
GraphMutation GraphAddDestination(const Document &d, CK_ID output, CK_ID destination, bool copy) {
    return {{GraphDestinations{Parameter(d, output), GraphDestinationMode::Add, {Parameter(d, destination)}, copy}}};
}
GraphMutation GraphRemoveDestination(const Document &d, CK_ID output, CK_ID destination) {
    return {
        {GraphDestinations{Parameter(d, output), GraphDestinationMode::Remove, {Parameter(d, destination)}, false}}};
}
GraphMutation GraphWriteText(const Document &d, CK_ID id, const std::string &text) {
    return {{GraphText{Reference(d, id), text, true}}};
}
GraphMutation GraphRename(const Document &d, CK_ID id, const std::string &text) {
    return {{GraphText{Reference(d, id), text, false}}};
}
GraphMutation GraphSetInteger(const Document &d, CK_ID id, GraphIntegerProperty property, int value) {
    return {{GraphInteger{Reference(d, id), property, value}}};
}
GraphMutation GraphMoveLocal(const Document &d, CK_ID from, int fromSlot, CK_ID to, int toSlot, CK_ID id) {
    return {{GraphLocalMove{Reference(d, from), Reference(d, to), Reference(d, id), fromSlot, toSlot}}};
}
GraphMutation GraphSequence(std::initializer_list<GraphMutation> changes) {
    GraphMutation result;
    for (const auto &change : changes)
        result.operations.insert(result.operations.end(), change.operations.begin(), change.operations.end());
    return result;
}
} // namespace detail
} // namespace vdev

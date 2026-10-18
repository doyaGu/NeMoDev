#include "VirtoolsDevTestSupport.h"

namespace vdevtest {

static std::vector<CKBehavior *> Tree(CKBehavior *script) {
    std::vector<CKBehavior *> tree;
    if (!script) return tree;
    tree.push_back(script);
    for (size_t i = 0; i < tree.size(); ++i)
        for (int c = 0; c < tree[i]->GetSubBehaviorCount(); ++c)
            if (CKBehavior *child = tree[i]->GetSubBehavior(c)) tree.push_back(child);
    return tree;
}

static const char *NameOf(CKObject *object) {
    return object && object->GetName() ? object->GetName() : "";
}

static CK_ID IdOf(CKObject *object) {
    return object ? object->GetID() : 0;
}

// A Target parameter is recreated by every UseTarget(TRUE), so inputs are
// named by owner and slot (-2 for the Target) rather than by CK_ID.
static std::string InputSlotText(CKParameterIn *input) {
    if (!input) return "0";
    CKObject *owner = input->GetOwner();
    if (CKBehavior *behavior = CKBehavior::Cast(owner)) {
        const int slot = behavior->GetTargetParameter() == input ? -2 : behavior->GetInputParameterPosition(input);
        if (slot != -1) return std::to_string(owner->GetID()) + "#" + std::to_string(slot);
    } else if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) {
        return std::to_string(owner->GetID()) + "#" + (operation->GetInParameter1() == input ? "0" : "1");
    }
    return std::to_string(input->GetID());
}

static std::string IoSlotText(CKBehaviorIO *io) {
    CKBehavior *owner = io ? io->GetOwner() : nullptr;
    if (!owner) return "0";
    const int input = owner->GetInputPosition(io);
    return std::to_string(owner->GetID()) + (input >= 0 ? "i" + std::to_string(input)
                                                        : "o" + std::to_string(owner->GetOutputPosition(io)));
}

std::string TopologyFingerprint(CKBehavior *script) {
    std::ostringstream out;
    auto input = [&](CKParameterIn *parameter) {
        if (!parameter) {
            out << " -";
            return;
        }
        out << " " << InputSlotText(parameter) << "'" << NameOf(parameter) << "' t" << parameter->GetType() << " <"
            << IdOf(parameter->GetDirectSource()) << "/" << InputSlotText(parameter->GetSharedSource()) << ">";
    };
    auto parameter = [&](CKParameter *parameter) {
        if (!parameter) {
            out << " -";
            return;
        }
        out << " " << parameter->GetID() << "'" << NameOf(parameter) << "' t" << parameter->GetType();
        // Local values are edit data (value holders of inputs); outputs are
        // computed at run time and are not part of the fingerprint.
        if (CKParameterLocal::Cast(parameter)) {
            const int size = parameter->GetStringValue(nullptr, FALSE);
            std::string value(static_cast<size_t>(size > 0 && size < 4096 ? size : 0), '\0');
            if (!value.empty()) parameter->GetStringValue(&value[0], FALSE);
            out << "=" << value.c_str();
        }
        if (CKParameterOut *output = CKParameterOut::Cast(parameter)) {
            out << " [";
            for (int i = 0; i < output->GetDestinationCount(); ++i) out << " " << IdOf(output->GetDestination(i));
            out << " ]";
        }
    };
    for (CKBehavior *behavior : Tree(script)) {
        out << "B " << behavior->GetID() << " '" << NameOf(behavior) << "' type " << behavior->GetType()
            << " parent " << IdOf(behavior->GetParent()) << " target " << (behavior->IsUsingTarget() ? 1 : 0)
            << " priority " << behavior->GetPriority() << "\n";
        // DeleteInput/DeleteOutput destroy the IO, so IOs are named by slot.
        out << " io";
        for (int i = 0; i < behavior->GetInputCount(); ++i) out << " i'" << NameOf(behavior->GetInput(i)) << "'";
        for (int i = 0; i < behavior->GetOutputCount(); ++i) out << " o'" << NameOf(behavior->GetOutput(i)) << "'";
        out << "\n pin";
        for (int i = 0; i < behavior->GetInputParameterCount(); ++i) input(behavior->GetInputParameter(i));
        if (behavior->IsUsingTarget()) input(behavior->GetTargetParameter());
        out << "\n pout";
        for (int i = 0; i < behavior->GetOutputParameterCount(); ++i) parameter(behavior->GetOutputParameter(i));
        out << "\n local";
        for (int i = 0; i < behavior->GetLocalParameterCount(); ++i) parameter(behavior->GetLocalParameter(i));
        out << "\n sub";
        for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) out << " " << IdOf(behavior->GetSubBehavior(i));
        out << "\n links";
        for (int i = 0; i < behavior->GetSubBehaviorLinkCount(); ++i) {
            CKBehaviorLink *link = behavior->GetSubBehaviorLink(i);
            if (!link) continue;
            out << " " << link->GetID() << "(" << IoSlotText(link->GetInBehaviorIO()) << ">"
                << IoSlotText(link->GetOutBehaviorIO()) << " d" << link->GetInitialActivationDelay() << ")";
        }
        out << "\n ops";
        for (int i = 0; i < behavior->GetParameterOperationCount(); ++i) {
            CKParameterOperation *operation = behavior->GetParameterOperation(i);
            if (!operation) continue;
            const CKGUID guid = operation->GetOperationGuid();
            out << " {" << operation->GetID() << " " << guid.d1 << "-" << guid.d2;
            input(operation->GetInParameter1());
            input(operation->GetInParameter2());
            parameter(operation->GetOutParameter());
            out << " }";
        }
        out << "\n";
    }
    return out.str();
}

} // namespace vdevtest

#include "core/schematic/ElementAddress.h"
#include "core/document/Document.h"
#include "core/graph/RuntimeGraph.h"
#include "core/schematic/SchematicEdits.h"

namespace vdev {
namespace {
bool Indexed(ElementKind kind) {
    return kind == ElementKind::Pin || kind == ElementKind::Link || kind == ElementKind::ControlPoint ||
           kind == ElementKind::LocalParam || kind == ElementKind::Shortcut || kind == ElementKind::Comment;
}
CKObject *PinObject(CKObject *owner, const ElementRef &ref) {
    if (auto *behavior = CKBehavior::Cast(owner)) {
        switch (ref.pin) {
        case PinKind::BehaviorIn: return ref.index >= 0 && ref.index < behavior->GetInputCount() ? behavior->GetInput(ref.index) : nullptr;
        case PinKind::BehaviorOut: return ref.index >= 0 && ref.index < behavior->GetOutputCount() ? behavior->GetOutput(ref.index) : nullptr;
        case PinKind::ParamIn: return ref.index >= 0 && ref.index < behavior->GetInputParameterCount() ? behavior->GetInputParameter(ref.index) : nullptr;
        case PinKind::ParamOut: return ref.index >= 0 && ref.index < behavior->GetOutputParameterCount() ? behavior->GetOutputParameter(ref.index) : nullptr;
        case PinKind::Target: return behavior->IsUsingTarget() ? behavior->GetTargetParameter() : nullptr;
        default: return nullptr;
        }
    }
    if (auto *op = CKParameterOperation::Cast(owner)) {
        if (ref.pin == PinKind::ParamIn && ref.index == 0) return op->GetInParameter1();
        if (ref.pin == PinKind::ParamIn && ref.index == 1) return op->GetInParameter2();
        if (ref.pin == PinKind::ParamOut && ref.index == 0) return op->GetOutParameter();
    }
    return nullptr;
}
bool Present(Document &doc, CK_ID script, const ElementRef &ref, CKObject **pin) {
    auto *session = doc.Session(script);
    if (!session || !ref.IsValid()) return false;
    auto *object = ref.object ? doc.Context()->GetObject(ref.object) : nullptr;
    if (ref.kind == ElementKind::Script || ref.kind == ElementKind::Start) return ref.owner == script;
    if (ref.kind == ElementKind::Behavior || ref.kind == ElementKind::Operation || ref.kind == ElementKind::Pin) {
        auto *owningScript = OwningScript(object);
        if (!owningScript || owningScript->GetID() != script) return false;
        if (ref.kind == ElementKind::Behavior) return CKBehavior::Cast(object) != nullptr;
        if (ref.kind == ElementKind::Operation) return CKParameterOperation::Cast(object) != nullptr;
        *pin = PinObject(object, ref);
        return *pin != nullptr;
    }
    auto *graph = FindRecord(session->Data(), ref.owner);
    if (!graph || ref.index < 0) return false;
    const auto index = size_t(ref.index);
    switch (ref.kind) {
    case ElementKind::Comment: return index < graph->comments.size();
    case ElementKind::LocalParam: return index < graph->localParams.size();
    case ElementKind::Shortcut: return index < graph->sharedParams.size();
    case ElementKind::Link: return index < graph->links.size();
    case ElementKind::ControlPoint: return index < graph->links.size() && ref.sub >= 0 && size_t(ref.sub) < graph->links[index].points.size();
    default: return false;
    }
}
}
ElementAddress ElementAddress::Capture(Document &doc, CK_ID scriptId, const ElementRef &ref) {
    CKObject *pin = nullptr;
    if (!Present(doc, scriptId, ref, &pin)) return {};
    ElementAddress address;
    address.script = doc.Reference(scriptId);
    address.owner = doc.Reference(ref.owner);
    address.object = doc.Reference(ref.object);
    address.endpoint = pin ? doc.Reference(pin->GetID()) : ObjectRef{};
    address.element = ref;
    address.revision = doc.Session(scriptId)->Revision();
    address.topology = doc.TopologyRevision();
    return address;
}
bool ElementAddress::Resolve(Document &doc, CK_ID &scriptId, ElementRef &ref) const {
    const auto id = doc.NativeId(script);
    if (!id || (owner && !doc.Resolve(owner)) || (object && !doc.Resolve(object))) return false;
    auto *session = doc.Session(id);
    if (!session || (Indexed(element.kind) &&
        (session->Revision() != revision || doc.TopologyRevision() != topology))) return false;
    CKObject *pin = nullptr;
    if (!Present(doc, id, element, &pin) || (endpoint && (!pin || doc.Reference(pin->GetID()) != endpoint))) return false;
    scriptId = id;
    ref = element;
    // A stored invalid link can have no live native object. Its display ID
    // comes from the revision-checked record, not from a recycled CK object.
    if (ref.kind == ElementKind::Link || ref.kind == ElementKind::ControlPoint)
        ref.object = FindRecord(session->Data(), ref.owner)->links[size_t(ref.index)].id;
    return true;
}
} // namespace vdev

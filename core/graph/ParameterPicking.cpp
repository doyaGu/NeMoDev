#include "core/graph/ParameterPicking.h"
#include "core/graph/RuntimeGraph.h"
namespace vdev {
namespace {
template <typename T> T ValueOf(CKParameter *p) {
    T v{};
    p->GetValue(&v, FALSE);
    return v;
}
template <typename T> void SetValueOf(CKParameter *p, const T &v) { p->SetValue(const_cast<T *>(&v), sizeof(v)); }
} // namespace
bool PickParameterDraft(const Document &document, CKParameter *work, CKObject *picked, bool shift,
                        CK_CLASSID filterClass, bool derived, const std::string &filter) {
    if (!work || !picked || !work->IsDynamic())
        return false;
    if (auto *parameter = CKParameter::Cast(picked))
        return parameter != work && parameter->IsCompatibleWith(work) && work->CopyValue(parameter, FALSE) == CK_OK;
    const auto info = ParameterUIOf(document.Context()->GetParameterManager(), work->GetType());
    const auto pickedId = picked->GetID();
    CK3dEntity *entity = CK3dEntity::Cast(picked);
    CK3dEntity *parent = entity && shift ? entity->GetParent() : nullptr;
    CK2dEntity *entity2d = CK2dEntity::Cast(picked);
    switch (info.ui) {
    case ParameterUI::Object: {
        // The object when its combo lists it, else NULL (sub_24E6CC10).
        const CK_CLASSID cid = filterClass ? filterClass : info.pickClass;
        CK_ID chosen = 0;
        for (const auto &choice : ObjectsOfClass(document, cid, derived, filter))
            if (choice.first == pickedId)
                chosen = pickedId;
        if (ValueOf<CK_ID>(work) == chosen)
            return false;
        ParameterValueDescription description;
        if (!DescribeParameterValue(document, work, description))
            return false;
        description.value.object = document.Reference(chosen);
        if (!WriteParameterDraft(document, work, description.value))
            return false;
        return true;
    }
    case ParameterUI::Script: {
        auto *behavior = CKBehavior::Cast(picked);
        if (!behavior)
            return false;
        CK_ID chosen = 0;
        auto *owner = behavior->GetOwner();
        for (const auto &choice : ScriptsOf(document, owner ? owner->GetID() : 0))
            if (choice.first == pickedId)
                chosen = pickedId;
        ParameterValueDescription description;
        if (!DescribeParameterValue(document, work, description))
            return false;
        description.value.object = document.Reference(chosen);
        if (!WriteParameterDraft(document, work, description.value))
            return false;
        return true;
    }
    case ParameterUI::Vector:
        if (!entity)
            return false;
        {
            VxVector position;
            entity->GetPosition(&position, parent);
            if (ValueOf<VxVector>(work) == position)
                return false;
            SetValueOf(work, position);
        }
        return true;
    case ParameterUI::Quaternion:
        if (!entity)
            return false;
        {
            VxQuaternion quaternion;
            entity->GetQuaternion(&quaternion, parent);
            SetValueOf(work, quaternion);
        }
        return true;
    case ParameterUI::EulerAngles:
        if (!entity)
            return false;
        {
            VxVector angles;
            Vx3DMatrixToEulerAngles(entity->GetWorldMatrix(), &angles.x, &angles.y, &angles.z);
            if (ValueOf<VxVector>(work) == angles)
                return false;
            SetValueOf(work, angles);
        }
        return true;
    case ParameterUI::Matrix:
        if (!entity)
            return false;
        SetValueOf(work, parent ? entity->GetLocalMatrix() : entity->GetWorldMatrix());
        return true;
    case ParameterUI::Box:
        if (!entity)
            return false;
        SetValueOf(work, entity->GetBoundingBox(shift ? TRUE : FALSE));
        return true;
    case ParameterUI::Vector2D:
        if (!entity2d)
            return false;
        {
            Vx2DVector value;
            if (shift)
                entity2d->GetSize(value);
            else
                entity2d->GetPosition(value);
            SetValueOf(work, value);
        }
        return true;
    case ParameterUI::Rect:
        if (!entity2d)
            return false;
        {
            VxRect rect;
            entity2d->GetRect(rect);
            SetValueOf(work, rect);
        }
        return true;
    case ParameterUI::Color:
        if (CKMaterial *material = CKMaterial::Cast(picked)) {
            SetValueOf(work, material->GetDiffuse());
            return true;
        }
        return false;
    default:
        return false;
    }
}
} // namespace vdev

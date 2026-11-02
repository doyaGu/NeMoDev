#pragma once
#include "editor/Identity.h"
#include "core/graph/SpecializedParameterValues.h"
#include <optional>
#include <string>

namespace vdev {
enum class ParameterValueKind {
    None, Boolean, Integer, Real, Enumeration, Object,
    Angle, Vector2, Vector3, Quaternion, EulerAngles, Matrix, Color, Box, Rect,
    Text, Structure, ObjectArray, Flags, Curve2D, Dependencies, LodOptions,
    Time, Key, Script, Animation, Message, Attribute, Synchro
};
unsigned ParameterComponentCount(ParameterValueKind);
bool ParameterObjectValue(ParameterValueKind);
bool ParameterRealValue(ParameterValueKind);
struct ParameterValue {
    ParameterValueKind kind = ParameterValueKind::None;
    int32_t integer = 0;
    double real = 0;
    ObjectRef object{};
    std::vector<double> components;
    std::string text;
    std::vector<ObjectRef> objects;
    std::vector<ParameterValue> members;
    ParameterCurve curve;
    std::vector<uint32_t> options;
    // A displayed rendezvous count is owned by the referenced object. Only
    // an explicit count edit joins the parameter command.
    std::optional<int32_t> waiters;
    bool setWaiters = false;
};
bool operator==(const ParameterValue &, const ParameterValue &);
inline bool operator!=(const ParameterValue &a, const ParameterValue &b) { return !(a == b); }
ParameterValue ParameterDraftValue(ParameterValue, const ParameterValue &);
} // namespace vdev

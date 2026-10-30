#include "core/graph/SpecializedParameterValues.h"
#include "core/graph/ParameterValues.h"
#include "core/graph/detail/SpecializedParameterStorage.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vdev {
namespace {
// These owned copies use the SDK's protected value storage for a single batch
// update. No SDK object is downcast, and no file representation is reimplemented.
class CurvePoint final : public CK2dCurvePoint {
  public:
    explicit CurvePoint(const CK2dCurvePoint &point) : CK2dCurvePoint(point) {}
    CurvePoint(const ParameterCurvePoint &point, CK2dCurve *owner) {
        m_Curve = owner; m_Flags = point.flags;
        m_Position = Vx2DVector(float(point.values[0]), float(point.values[1]));
        m_Tension = float(point.values[2]); m_Continuity = float(point.values[3]); m_Bias = float(point.values[4]);
        m_InTang = Vx2DVector(float(point.values[5]), float(point.values[6]));
        m_OutTang = Vx2DVector(float(point.values[7]), float(point.values[8]));
        m_Length = float(point.derived[0]); m_RCurvePos = Vx2DVector(float(point.derived[1]), float(point.derived[2]));
    }
    ParameterCurvePoint Value(bool computed) const {
        ParameterCurvePoint value{m_Flags, {m_Position.x, m_Position.y, m_Tension, m_Continuity, m_Bias}};
        if (computed || (m_Flags & CK2DCURVEPOINT_USETANGENTS)) {
            value.values[5] = m_InTang.x; value.values[6] = m_InTang.y;
            value.values[7] = m_OutTang.x; value.values[8] = m_OutTang.y;
        }
        if (computed) value.derived = {m_Length, m_RCurvePos.x, m_RCurvePos.y};
        return value;
    }
};
class Curve final : public CK2dCurve {
  public:
    Curve() = default;
    explicit Curve(const CK2dCurve &curve) : CK2dCurve(curve) {}
    ParameterCurve Value() const {
        ParameterCurve value; value.fitting = FittingCoef; value.length = m_Length;
        for (int i = 0; i < m_ControlPoints.Size(); ++i) value.points.push_back(CurvePoint(m_ControlPoints[i]).Value(m_ControlPoints.Size() >= 2));
        return value;
    }
    void Assign(const ParameterCurve &value, bool update = true) {
        m_ControlPoints.Resize(int(value.points.size())); FittingCoef = float(value.fitting); m_Length = float(value.length);
        for (size_t i = 0; i < value.points.size(); ++i) m_ControlPoints[int(i)] = CurvePoint(value.points[i], this);
        if (update) Update();
    }
};
template<class T> T *Held(CKParameter *parameter) {
    T *value = nullptr;
    return parameter && parameter->GetDataSize() == sizeof(value) && parameter->GetValue(&value, FALSE) == CK_OK ? value : nullptr;
}
struct NativeLod {
    int32_t flags;
    float proportions[6];
};
static_assert(sizeof(NativeLod) == 7 * sizeof(uint32_t), "Native LOD parameter storage");
bool Finite(double value) { return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(); }
}
bool operator==(const ParameterCurve &a, const ParameterCurve &b) {
    if (a.fitting != b.fitting || a.points.size() != b.points.size()) return false;
    for (size_t i = 0; i < a.points.size(); ++i) {
        const auto &x = a.points[i], &y = b.points[i];
        if (x.flags != y.flags) return false;
        if (x.values != y.values) return false;
    }
    return true;
}
bool ValidParameterCurve(const ParameterCurve &curve) {
    if (!Finite(curve.fitting) || curve.points.size() > 4096) return false;
    double previous = -std::numeric_limits<double>::infinity();
    for (const auto &point : curve.points) {
        if (!std::all_of(point.values.begin(), point.values.end(), Finite) || point.values[0] < previous) return false;
        previous = point.values[0];
    }
    return true;
}
bool NormalizeParameterCurve(ParameterCurve &value) {
    if (!ValidParameterCurve(value)) return false;
    Curve curve; curve.Assign(value);
    auto normalized = curve.Value();
    if (!ValidParameterCurve(normalized) || !Finite(normalized.length) ||
        !std::all_of(normalized.points.begin(), normalized.points.end(), [](const auto &point) {
            return std::all_of(point.derived.begin(), point.derived.end(), Finite);
        })) return false;
    value = std::move(normalized); return true;
}
bool ParameterCurvePreview(const ParameterCurve &value, std::vector<double> &samples, std::vector<double> &tangents) {
    if (!ValidParameterCurve(value)) return false;
    Curve curve; curve.Assign(value);
    const auto normalized = curve.Value();
    if (!ValidParameterCurve(normalized)) return false;
    std::vector<double> ys, handles;
    for (unsigned i = 0; i <= 100; ++i) {
        const auto y = curve.GetY(float(i) / 100);
        if (!Finite(y)) return false;
        ys.push_back(y);
    }
    for (const auto &point : normalized.points)
        for (unsigned i = 5; i < 9; ++i) handles.push_back(point.values[i]);
    samples = std::move(ys); tangents = std::move(handles); return true;
}
bool DescribeSpecializedParameter(CKParameter *parameter, ParameterValueDescription &description) {
    const auto ui = ParameterUIOf(parameter->GetCKContext()->GetParameterManager(), parameter->GetType()).ui;
    auto &value = description.value;
    if (ui == ParameterUI::Curve2D) {
        auto *curve = Held<CK2dCurve>(parameter);
        if (!curve || curve->GetControlPointCount() < 0 || curve->GetControlPointCount() > 4096) return false;
        value.kind = ParameterValueKind::Curve2D; value.curve = Curve(*curve).Value();
        return ValidParameterCurve(value.curve);
    }
    if (ui == ParameterUI::Dependencies) {
        auto *dependencies = Held<CKDependencies>(parameter);
        if (!dependencies || dependencies->Size() < 0 || dependencies->Size() > 65536) return false;
        value.kind = ParameterValueKind::Dependencies; value.integer = int32_t(dependencies->m_Flags);
        for (int i = 0; i < dependencies->Size(); ++i) value.options.push_back((*dependencies)[i]);
        const auto mode = DependenciesModeOf(parameter->GetCKContext()->GetParameterManager(), parameter->GetType());
        for (const auto &entry : DependencyClasses(mode)) {
            if (entry.cid < 0 || entry.depth < 0 || description.dependencyClasses.size() >= 4096) return false;
            ParameterValueDescription::DependencyClass item{uint32_t(entry.cid), uint32_t(entry.depth), AnsiToUtf8(entry.name), {}};
            for (const auto &option : DependencyOptions(entry.cid, mode)) item.options.push_back(AnsiToUtf8(option));
            description.dependencyClasses.push_back(std::move(item));
        }
        return true;
    }
    if (ui == ParameterUI::LodOptions) {
        NativeLod lod{};
        if (parameter->GetDataSize() != sizeof(lod) || parameter->GetValue(&lod, FALSE) != CK_OK) return false;
        value.kind = ParameterValueKind::LodOptions; value.integer = lod.flags;
        for (auto number : lod.proportions) { if (!Finite(number)) return false; value.components.push_back(number); }
        description.displayScale = 100; description.rows = 3; description.columns = 2;
        description.componentLabels = {"Mag", "Min"}; description.rowLabels = {"Screen", "Faces", "Screen Alpha"};
        return true;
    }
    return false;
}
bool WriteSpecializedParameter(CKParameter *parameter, const ParameterValue &value) {
    if (value.kind == ParameterValueKind::Curve2D) {
        auto *target = Held<CK2dCurve>(parameter);
        if (!target || !ValidParameterCurve(value.curve)) return false;
        Curve curve; curve.Assign(value.curve, false);
        if (!ValidParameterCurve(curve.Value())) return false;
        *target = curve; return true;
    }
    if (value.kind == ParameterValueKind::Dependencies) {
        CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_FLAGS(value.integer);
        dependencies.Resize(int(value.options.size()));
        for (size_t i = 0; i < value.options.size(); ++i) dependencies[int(i)] = value.options[i];
        return SetHeldValue(parameter, &dependencies);
    }
    if (value.kind == ParameterValueKind::LodOptions && value.components.size() == 6) {
        NativeLod lod{}; lod.flags = value.integer;
        for (unsigned i = 0; i < 6; ++i) lod.proportions[i] = float(value.components[i]);
        return parameter->SetValue(&lod, sizeof(lod)) == CK_OK;
    }
    return false;
}
}

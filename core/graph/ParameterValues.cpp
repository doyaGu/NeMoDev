#include "core/graph/ParameterValues.h"
#include "core/graph/ValueCommands.h"
#include "core/catalog/ValueText.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Edit.h"
#include "core/runtime/SdkCompat.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace vdev {
static_assert(sizeof(VxVector) == 3 * sizeof(float) && sizeof(Vx2DVector) == 2 * sizeof(float), "Native vector storage");
static_assert(sizeof(VxQuaternion) == 4 * sizeof(float) && sizeof(VxColor) == 4 * sizeof(float), "Native four-component storage");
static_assert(sizeof(VxMatrix) == 16 * sizeof(float) && sizeof(VxBbox) == 6 * sizeof(float) && sizeof(VxRect) == 4 * sizeof(float), "Native geometry storage");
unsigned ParameterComponentCount(ParameterValueKind kind) {
    switch (kind) {
    case ParameterValueKind::Vector2: return 2;
    case ParameterValueKind::Vector3: case ParameterValueKind::EulerAngles: return 3;
    case ParameterValueKind::Quaternion: case ParameterValueKind::Color: case ParameterValueKind::Rect: return 4;
    case ParameterValueKind::Box: return 6;
    case ParameterValueKind::Matrix: return 16;
    default: return 0;
    }
}
bool operator==(const ParameterValue &a, const ParameterValue &b) {
    if (a.kind != b.kind) return false;
    if (ParameterComponentCount(a.kind)) return a.components == b.components;
    switch (a.kind) {
    case ParameterValueKind::Angle:
    case ParameterValueKind::Real: return a.real == b.real;
    case ParameterValueKind::Object: return a.object == b.object;
    default: return a.integer == b.integer;
    }
}
bool DescribeParameterValue(const Document &document, CKParameter *parameter, ParameterValueDescription &out) {
    if (!parameter || !document.Context()) return false;
    auto *manager = document.Context()->GetParameterManager();
    const auto ui = ParameterUIOf(manager, parameter->GetType());
    const auto guid = parameter->GetGUID();
    ParameterValueDescription result;
    result.type = guid;
    auto &kind = result.value.kind;
    if (ui.ui == ParameterUI::Bool) kind = ParameterValueKind::Boolean;
    else if (ui.ui == ParameterUI::Enum) kind = ParameterValueKind::Enumeration;
    else if (ui.ui == ParameterUI::Object) { kind = ParameterValueKind::Object; result.objectClass = ui.pickClass; }
    else if (ui.ui == ParameterUI::Number) {
        // Follow the selected UI's type ancestry, including types registered later.
        auto type = guid;
        for (int depth = 0; depth < 32; ++depth) {
            if (type == CKPGUID_INT) { kind = ParameterValueKind::Integer; break; }
            if (type == CKPGUID_FLOAT || type == CKPGUID_PERCENTAGE) { kind = ParameterValueKind::Real; result.displayScale = type == CKPGUID_PERCENTAGE ? 100 : 1; break; }
            const auto *desc = manager->GetParameterTypeDescription(manager->ParameterGuidToType(type));
            if (!desc || desc->DerivedFrom == type) break;
            type = desc->DerivedFrom;
        }
    }
    const auto shape = [&](ParameterValueKind selected, unsigned rows, unsigned columns,
                           std::initializer_list<const char *> labels, std::initializer_list<const char *> rowLabels = {}) {
        kind = selected; result.rows = rows; result.columns = columns;
        result.componentLabels.assign(labels.begin(), labels.end());
        result.rowLabels.assign(rowLabels.begin(), rowLabels.end());
    };
    switch (ui.ui) {
    case ParameterUI::Angle: kind = ParameterValueKind::Angle; result.componentLabels = {"Angle"}; result.displayScale = 180.0 / 3.14159265358979323846; break;
    case ParameterUI::Vector2D: shape(ParameterValueKind::Vector2, 1, 2, {"X", "Y"}); break;
    case ParameterUI::Vector: shape(ParameterValueKind::Vector3, 1, 3, {"X", "Y", "Z"}); break;
    case ParameterUI::Quaternion: shape(ParameterValueKind::Quaternion, 1, 4, {"X", "Y", "Z", "W"}); break;
    case ParameterUI::EulerAngles:
        shape(ParameterValueKind::EulerAngles, 3, 1, {"Angle"}, {"X", "Y", "Z"});
        result.displayScale = 180.0 / 3.14159265358979323846; break;
    case ParameterUI::Matrix: shape(ParameterValueKind::Matrix, 4, 4, {"X", "Y", "Z", "W"}, {"Right", "Up", "Dir", "Position"}); break;
    case ParameterUI::Color: shape(ParameterValueKind::Color, 1, 4, {"R", "G", "B", "A"}); break;
    case ParameterUI::Box: shape(ParameterValueKind::Box, 2, 3, {"X", "Y", "Z"}, {"Min", "Max"}); break;
    case ParameterUI::Rect: shape(ParameterValueKind::Rect, 2, 2, {"X", "Y"}, {"Left,Top", "Right,Bottom"}); break;
    default: break;
    }
    const auto count = ParameterComponentCount(kind);
    const auto bytes = count ? count * sizeof(float) : sizeof(int32_t);
    if (kind == ParameterValueKind::None || parameter->GetDataSize() != int(bytes)) return false;
    if (count) {
        float components[16]{};
        if (parameter->GetValue(components, FALSE) != CK_OK) return false;
        for (unsigned i = 0; i < count; ++i) {
            if (!std::isfinite(components[i])) return false;
            result.value.components.push_back(components[i]);
        }
    } else if (kind == ParameterValueKind::Real || kind == ParameterValueKind::Angle) {
        float value = 0;
        if (parameter->GetValue(&value, FALSE) != CK_OK || !std::isfinite(value)) return false;
        result.value.real = value;
    } else if (kind == ParameterValueKind::Object) {
        CK_ID id = 0;
        if (parameter->GetValue(&id, FALSE) != CK_OK) return false;
        result.value.object = document.Reference(id);
        if (id && !result.value.object) return false;
    } else if (parameter->GetValue(&result.value.integer, FALSE) != CK_OK) return false;
    if (kind == ParameterValueKind::Enumeration) {
        auto *enumeration = manager->GetEnumDescByType(parameter->GetType());
        if (!enumeration || enumeration->GetNumEnums() < 0 || enumeration->GetNumEnums() > 65536) return false;
        for (int i = 0; i < enumeration->GetNumEnums(); ++i) {
            const auto *label = enumeration->GetEnumDescription(i);
            result.choices.push_back({enumeration->GetEnumValue(i), AnsiToUtf8(label ? label : "")});
        }
    }
    result.display = AnsiToUtf8(ParameterText(parameter));
    out = std::move(result);
    return true;
}
bool ParseParameterValue(const ParameterValueDescription &description, const std::string &text, ParameterValue &out) {
    auto value = description.value;
    if (value.kind == ParameterValueKind::Integer || value.kind == ParameterValueKind::Enumeration || value.kind == ParameterValueKind::Boolean) {
        if (!ParseIntegerValue(text, value.integer)) return false;
    } else if (value.kind == ParameterValueKind::Real || value.kind == ParameterValueKind::Angle) {
        if (!ParseRealValue(text, value.real, description.displayScale)) return false;
    } else return false;
    out = value;
    return true;
}
bool ValidateParameterValue(const Document &document, const ParameterValueDescription &description, const ParameterValue &value) {
    if (value.kind != description.value.kind || value.kind == ParameterValueKind::None) return false;
    if (const auto count = ParameterComponentCount(value.kind)) {
        return value.components.size() == count && std::all_of(value.components.begin(), value.components.end(), [](double component) {
            return std::isfinite(component) && std::abs(component) <= std::numeric_limits<float>::max();
        });
    }
    if (value == description.value) return true;
    switch (value.kind) {
    case ParameterValueKind::Boolean: return value.integer == 0 || value.integer == 1;
    case ParameterValueKind::Integer: return true;
    case ParameterValueKind::Angle:
    case ParameterValueKind::Real: return std::isfinite(value.real) && std::abs(value.real) <= std::numeric_limits<float>::max();
    case ParameterValueKind::Enumeration:
        return value == description.value || std::any_of(description.choices.begin(), description.choices.end(), [&](const auto &item) { return item.first == value.integer; });
    case ParameterValueKind::Object:
        return !value.object || document.Resolve(value.object, description.objectClass);
    default: return false;
    }
}
namespace {
bool WriteValue(const Document &document, CKParameter *parameter, const ParameterValue &value, bool replay) {
    ParameterValueDescription description;
    if (!DescribeParameterValue(document, parameter, description) || value.kind != description.value.kind) return false;
    if (!replay && !ValidateParameterValue(document, description, value)) return false;
    unsigned char bytes[16 * sizeof(float)]{};
    const auto count = ParameterComponentCount(value.kind);
    if (count && value.components.size() != count) return false;
    if (count) {
        for (unsigned i = 0; i < count; ++i) {
            const auto component = float(value.components[i]);
            std::memcpy(bytes + i * sizeof(float), &component, sizeof(float));
        }
    } else if (value.kind == ParameterValueKind::Object) {
        auto *object = value.object ? document.Resolve(value.object, description.objectClass, Document::ObjectScope::Existing) : nullptr;
        if (value.object && !object) return false;
        CK_ID id = object ? object->GetID() : 0;
        std::memcpy(bytes, &id, 4);
    } else if (value.kind == ParameterValueKind::Real || value.kind == ParameterValueKind::Angle) {
        const auto real = float(value.real); std::memcpy(bytes, &real, 4);
    } else std::memcpy(bytes, &value.integer, 4);
    if (parameter->SetValue(bytes, count ? int(count * sizeof(float)) : 4) != CK_OK) return false;
    ParameterValueDescription actual;
    auto expected = value;
    if (expected.kind == ParameterValueKind::Real || expected.kind == ParameterValueKind::Angle) expected.real = float(expected.real);
    for (auto &component : expected.components) component = float(component);
    return DescribeParameterValue(document, parameter, actual) && actual.value == expected;
}
class ParameterValueAction final : public Action {
  public:
    ParameterValueAction(ObjectRef target, CKGUID type, ParameterValue before, ParameterValue after)
        : m_Target(target), m_Type(type), m_Before(before), m_After(after) {}
    bool Undo(Document &document) override { return Apply(document, m_Before, m_After); }
    bool Redo(Document &document) override { return Apply(document, m_After, m_Before); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        for (auto ref : {m_Target, m_Before.object, m_After.object})
            if (auto id = document.NativeId(ref, Document::ObjectScope::Existing)) objects.insert(id);
    }
  private:
    bool Apply(Document &document, const ParameterValue &value, const ParameterValue &restore) {
        auto *parameter = CKParameter::Cast(document.Resolve(m_Target, CKCID_PARAMETER, Document::ObjectScope::Existing));
        if (!parameter || parameter->GetGUID() != m_Type) return false;
        bool applied = false;
        if (GuardedCall([&] { applied = WriteValue(document, parameter, value, true); }, &document.Host(), "Set parameter value") && applied) return true;
        bool restored = false;
        if (!GuardedCall([&] { restored = WriteValue(document, parameter, restore, true); }, &document.Host(), "Restore parameter value") || !restored)
            document.InvalidateHistory();
        return false;
    }
    ObjectRef m_Target;
    CKGUID m_Type;
    ParameterValue m_Before, m_After;
};
} // namespace
bool WriteParameterDraft(const Document &document, CKParameter *parameter, const ParameterValue &value) {
    if (!parameter || !parameter->IsDynamic()) return false;
    return WriteValue(document, parameter, value, false);
}
EditResult SetParameterValue(Document &document, ObjectRef target, const CKGUID &type, const ParameterValue &value, CK_ID script) {
    auto *parameter = CKParameter::Cast(document.Resolve(target, CKCID_PARAMETER));
    ParameterValueDescription description;
    if (!parameter || parameter->GetGUID() != type || !DescribeParameterValue(document, parameter, description))
        return EditResult::Refused("The parameter or its type is no longer available");
    if (!ValidateParameterValue(document, description, value)) return EditResult::Refused("Invalid parameter value");
    auto normalized = value;
    if (normalized.kind == ParameterValueKind::Real || normalized.kind == ParameterValueKind::Angle) normalized.real = float(normalized.real);
    for (auto &component : normalized.components) component = float(component);
    if (description.value == normalized) return EditResult::NoChange("The value is unchanged");
    Edit edit(document, "Set Value");
    if (!edit.Do(std::make_unique<ParameterValueAction>(target, type, description.value, normalized))) {
        edit.Fail("The parameter value could not be applied");
        return EditResult::Error("The parameter value could not be applied");
    }
    if (!script) if (auto *owner = OwningScript(parameter)) script = owner->GetID();
    edit.Touches(script);
    return edit.Commit();
}
} // namespace vdev

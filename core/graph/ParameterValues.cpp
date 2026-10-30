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
bool operator==(const ParameterScalar &a, const ParameterScalar &b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
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
    if (kind == ParameterValueKind::None || parameter->GetDataSize() != 4) return false;
    unsigned char bytes[4]{};
    if (parameter->GetValue(bytes, FALSE) != CK_OK) return false;
    if (kind == ParameterValueKind::Real) {
        float value = 0; std::memcpy(&value, bytes, 4);
        if (!std::isfinite(value)) return false;
        result.value.real = value;
    } else if (kind == ParameterValueKind::Object) {
        CK_ID id = 0; std::memcpy(&id, bytes, 4);
        result.value.object = document.Reference(id);
        if (id && !result.value.object) return false;
    } else std::memcpy(&result.value.integer, bytes, 4);
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
bool ParseParameterScalar(const ParameterValueDescription &description, const std::string &text, ParameterScalar &out) {
    auto value = description.value;
    if (value.kind == ParameterValueKind::Integer || value.kind == ParameterValueKind::Enumeration || value.kind == ParameterValueKind::Boolean) {
        if (!ParseIntegerValue(text, value.integer)) return false;
    } else if (value.kind == ParameterValueKind::Real) {
        if (!ParseRealValue(text, value.real, description.displayScale)) return false;
    } else return false;
    out = value;
    return true;
}
bool ValidateParameterScalar(const Document &document, const ParameterValueDescription &description, const ParameterScalar &value) {
    if (value.kind != description.value.kind || value.kind == ParameterValueKind::None) return false;
    if (value == description.value) return true;
    switch (value.kind) {
    case ParameterValueKind::Boolean: return value.integer == 0 || value.integer == 1;
    case ParameterValueKind::Integer: return true;
    case ParameterValueKind::Real: return std::isfinite(value.real) && std::abs(value.real) <= std::numeric_limits<float>::max();
    case ParameterValueKind::Enumeration:
        return value == description.value || std::any_of(description.choices.begin(), description.choices.end(), [&](const auto &item) { return item.first == value.integer; });
    case ParameterValueKind::Object:
        return !value.object || document.Resolve(value.object, description.objectClass);
    default: return false;
    }
}
namespace {
bool WriteScalar(const Document &document, CKParameter *parameter, const ParameterScalar &value, bool replay) {
    ParameterValueDescription description;
    if (!DescribeParameterValue(document, parameter, description) || value.kind != description.value.kind) return false;
    if (!replay && !ValidateParameterScalar(document, description, value)) return false;
    unsigned char bytes[4]{};
    if (value.kind == ParameterValueKind::Object) {
        auto *object = value.object ? document.Resolve(value.object, description.objectClass, Document::ObjectScope::Existing) : nullptr;
        if (value.object && !object) return false;
        CK_ID id = object ? object->GetID() : 0;
        std::memcpy(bytes, &id, 4);
    } else if (value.kind == ParameterValueKind::Real) {
        const auto real = float(value.real); std::memcpy(bytes, &real, 4);
    } else std::memcpy(bytes, &value.integer, 4);
    if (parameter->SetValue(bytes, 4) != CK_OK) return false;
    ParameterValueDescription actual;
    auto expected = value;
    if (expected.kind == ParameterValueKind::Real) expected.real = float(expected.real);
    return DescribeParameterValue(document, parameter, actual) && actual.value == expected;
}
class ScalarAction final : public Action {
  public:
    ScalarAction(ObjectRef target, CKGUID type, ParameterScalar before, ParameterScalar after)
        : m_Target(target), m_Type(type), m_Before(before), m_After(after) {}
    bool Undo(Document &document) override { return Apply(document, m_Before, m_After); }
    bool Redo(Document &document) override { return Apply(document, m_After, m_Before); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        for (auto ref : {m_Target, m_Before.object, m_After.object})
            if (auto id = document.NativeId(ref, Document::ObjectScope::Existing)) objects.insert(id);
    }
  private:
    bool Apply(Document &document, const ParameterScalar &value, const ParameterScalar &restore) {
        auto *parameter = CKParameter::Cast(document.Resolve(m_Target, CKCID_PARAMETER, Document::ObjectScope::Existing));
        if (!parameter || parameter->GetGUID() != m_Type) return false;
        bool applied = false;
        if (GuardedCall([&] { applied = WriteScalar(document, parameter, value, true); }, &document.Host(), "Set parameter value") && applied) return true;
        bool restored = false;
        if (!GuardedCall([&] { restored = WriteScalar(document, parameter, restore, true); }, &document.Host(), "Restore parameter value") || !restored)
            document.InvalidateHistory();
        return false;
    }
    ObjectRef m_Target;
    CKGUID m_Type;
    ParameterScalar m_Before, m_After;
};
} // namespace
bool WriteParameterDraft(const Document &document, CKParameter *parameter, const ParameterScalar &value) {
    if (!parameter || !parameter->IsDynamic()) return false;
    return WriteScalar(document, parameter, value, false);
}
EditResult SetParameterScalar(Document &document, ObjectRef target, const CKGUID &type, const ParameterScalar &value, CK_ID script) {
    auto *parameter = CKParameter::Cast(document.Resolve(target, CKCID_PARAMETER));
    ParameterValueDescription description;
    if (!parameter || parameter->GetGUID() != type || !DescribeParameterValue(document, parameter, description))
        return EditResult::Refused("The parameter or its type is no longer available");
    if (!ValidateParameterScalar(document, description, value)) return EditResult::Refused("Invalid parameter value");
    auto normalized = value;
    if (normalized.kind == ParameterValueKind::Real) normalized.real = float(normalized.real);
    if (description.value == normalized) return EditResult::NoChange("The value is unchanged");
    Edit edit(document, "Set Value");
    if (!edit.Do(std::make_unique<ScalarAction>(target, type, description.value, normalized))) {
        edit.Fail("The parameter value could not be applied");
        return EditResult::Error("The parameter value could not be applied");
    }
    if (!script) if (auto *owner = OwningScript(parameter)) script = owner->GetID();
    edit.Touches(script);
    return edit.Commit();
}
} // namespace vdev

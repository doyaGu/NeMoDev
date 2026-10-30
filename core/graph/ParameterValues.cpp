#include "core/graph/ParameterValues.h"
#include "core/graph/ValueCommands.h"
#include "core/catalog/ValueText.h"
#include "core/graph/RuntimeGraph.h"
#include "core/level/ObjectEditCommands.h"
#include <stdexcept>
#include <unordered_map>
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
    if (a.kind == ParameterValueKind::Text) return a.text == b.text;
    if (a.kind == ParameterValueKind::ObjectArray) return a.objects == b.objects;
    if (a.kind == ParameterValueKind::Structure) return a.members == b.members;
    switch (a.kind) {
    case ParameterValueKind::Angle:
    case ParameterValueKind::Real: return a.real == b.real;
    case ParameterValueKind::Object: return a.object == b.object;
    default: return a.integer == b.integer;
    }
}
ParameterValue RememberParameterReferences(ParameterValue value, const ParameterValue &remembered) {
    if (value.kind != remembered.kind) return value;
    if (value.kind == ParameterValueKind::Object && value.object.object == remembered.object.object) value.object = remembered.object;
    if (value.kind == ParameterValueKind::ObjectArray) {
        std::unordered_map<CK_ID, ObjectRef> previous;
        previous.reserve(remembered.objects.size());
        for (auto ref : remembered.objects) previous.emplace(ref.object, ref);
        for (auto &object : value.objects) {
            const auto found = previous.find(object.object);
            if (found != previous.end()) object = found->second;
        }
    }
    if (value.kind == ParameterValueKind::Structure && value.members.size() == remembered.members.size())
        for (size_t i = 0; i < value.members.size(); ++i) value.members[i] = RememberParameterReferences(std::move(value.members[i]), remembered.members[i]);
    return value;
}
bool ParameterMembers(const Document &document, CKParameter *parameter, std::vector<ParameterMember> &out) {
    auto *context = document.Context();
    auto *structure = context && parameter ? context->GetParameterManager()->GetStructDescByType(parameter->GetType()) : nullptr;
    if (!structure || structure->NbData < 0 || structure->NbData > 4096 ||
        parameter->GetDataSize() != int(structure->NbData * sizeof(CK_ID)) ||
        (structure->NbData && (!structure->Guids || !structure->Desc))) return false;
    const auto *ids = static_cast<const CK_ID *>(parameter->GetReadDataPtr(FALSE));
    if (structure->NbData && !ids) return false;
    std::vector<ParameterMember> members;
    for (int i = 0; i < structure->NbData; ++i) {
        auto ref = document.Reference(ids[i]);
        auto *member = CKParameter::Cast(document.Resolve(ref, CKCID_PARAMETER, Document::ObjectScope::Existing));
        if (!member || member->GetGUID() != structure->Guids[i]) return false;
        members.push_back({ref, structure->Guids[i], AnsiToUtf8(structure->Desc[i] ? structure->Desc[i] : "")});
    }
    out = std::move(members); return true;
}
namespace {
struct ValueReadBudget { std::set<ObjectRef> path; unsigned nodes = 65536, objects = 65536; };
bool DescribeValue(const Document &document, CKParameter *parameter, ParameterValueDescription &out,
                   unsigned depth, ValueReadBudget &budget) {
    if (!parameter || !document.Context() || depth >= 32 || !budget.nodes) return false;
    --budget.nodes;
    auto &path = budget.path;
    const auto reference = document.Reference(parameter->GetID());
    if (!reference || !path.insert(reference).second) return false;
    struct Leave { std::set<ObjectRef> &path; ObjectRef ref; ~Leave() { path.erase(ref); } } leave{path, reference};
    auto *manager = document.Context()->GetParameterManager();
    const auto ui = ParameterUIOf(manager, parameter->GetType());
    const auto guid = parameter->GetGUID();
    ParameterValueDescription result;
    result.type = guid; result.parameter = reference;
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
} // namespace
bool DescribeParameterValue(const Document &document, CKParameter *parameter, ParameterValueDescription &out) {
    ValueReadBudget budget;
    return DescribeValue(document, parameter, out, 0, budget);
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
    if (value.kind == ParameterValueKind::Structure) {
        if (value.members.size() != description.members.size()) return false;
        for (size_t i = 0; i < value.members.size(); ++i)
            if (!ValidateParameterValue(document, description.members[i], value.members[i])) return false;
        return true;
    }
    if (value.kind == ParameterValueKind::ObjectArray)
        return value.objects.size() <= 65536 && std::all_of(value.objects.begin(), value.objects.end(), [&](auto ref) { return ref && document.Resolve(ref); });
    if (value.kind == ParameterValueKind::Text) return value.text.size() <= 1024 * 1024 && value.text.find('\0') == std::string::npos;
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
ParameterValue Normalized(ParameterValue value) {
    if (value.kind == ParameterValueKind::Real || value.kind == ParameterValueKind::Angle) value.real = float(value.real);
    for (auto &component : value.components) component = float(component);
    for (auto &member : value.members) member = Normalized(std::move(member));
    return value;
}
std::vector<std::pair<ObjectRef, CKGUID>> ParameterIdentities(const ParameterValueDescription &description) {
    std::vector<std::pair<ObjectRef, CKGUID>> identities;
    const auto visit = [&](const auto &self, const ParameterValueDescription &value) -> void {
        identities.emplace_back(value.parameter, value.type);
        for (const auto &member : value.members) self(self, member);
    };
    visit(visit, description); return identities;
}
bool WriteValue(const Document &document, CKParameter *parameter, const ParameterValue &value, bool replay) {
    ParameterValueDescription description;
    if (!DescribeParameterValue(document, parameter, description) || value.kind != description.value.kind) return false;
    if (!replay && !ValidateParameterValue(document, description, value)) return false;
    if (description.value == value) return true;
    if (value.kind == ParameterValueKind::Structure) {
        if (description.members.size() != value.members.size()) return false;
        for (size_t i = 0; i < value.members.size(); ++i) {
            auto *child = CKParameter::Cast(document.Resolve(description.members[i].parameter, CKCID_PARAMETER, Document::ObjectScope::Existing));
            if (!child || !WriteValue(document, child, value.members[i], replay)) return false;
        }
        return true;
    }
    if (value.kind == ParameterValueKind::Text) {
        const auto native = Utf8ToAnsi(value.text);
        if (parameter->SetStringValue(native.c_str()) != CK_OK) return false;
        ParameterValueDescription actual;
        return DescribeParameterValue(document, parameter, actual) && actual.value == value;
    }
    if (value.kind == ParameterValueKind::ObjectArray) {
        XObjectArray array;
        for (auto ref : value.objects) {
            const auto id = document.NativeId(ref, replay ? Document::ObjectScope::Existing : Document::ObjectScope::Live);
            if (!id) return false;
            array.PushBack(id);
        }
        if (!SetHeldValue(parameter, &array)) return false;
        ParameterValueDescription actual;
        return DescribeParameterValue(document, parameter, actual) && actual.value == value;
    }
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
    ParameterValueAction(ObjectRef target, CKGUID type, ParameterValueDescription before, ParameterValue after)
        : m_Target(target), m_Type(type), m_Identities(ParameterIdentities(before)), m_Before(std::move(before.value)), m_After(std::move(after)) {}
    bool Undo(Document &document) override { return Apply(document, m_Before, m_After); }
    bool Redo(Document &document) override { return Apply(document, m_After, m_Before); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        const auto retain = [&](ObjectRef ref) { if (auto id = document.NativeId(ref, Document::ObjectScope::Existing)) objects.insert(id); };
        retain(m_Target);
        const auto values = [&](const auto &self, const ParameterValue &value) -> void {
            retain(value.object); for (auto ref : value.objects) retain(ref);
            for (const auto &member : value.members) self(self, member);
        };
        values(values, m_Before); values(values, m_After);
        for (const auto &identity : m_Identities) retain(identity.first);
    }
  private:
    bool Apply(Document &document, const ParameterValue &value, const ParameterValue &restore) {
        auto *parameter = CKParameter::Cast(document.Resolve(m_Target, CKCID_PARAMETER, Document::ObjectScope::Existing));
        ParameterValueDescription current;
        if (!parameter || parameter->GetGUID() != m_Type || !DescribeParameterValue(document, parameter, current) || m_Identities != ParameterIdentities(current)) return false;
        bool applied = false;
        if (GuardedCall([&] { applied = WriteValue(document, parameter, value, true); }, &document.Host(), "Set parameter value") && applied) return true;
        bool restored = false;
        if (!GuardedCall([&] { restored = WriteValue(document, parameter, restore, true); }, &document.Host(), "Restore parameter value") || !restored)
            document.InvalidateHistory();
        return false;
    }
    ObjectRef m_Target;
    CKGUID m_Type;
    std::vector<std::pair<ObjectRef, CKGUID>> m_Identities;
    ParameterValue m_Before, m_After;
};
} // namespace
bool WriteParameterDraft(const Document &document, CKParameter *parameter, const ParameterValue &value) {
    if (!parameter || !parameter->IsDynamic()) return false;
    ParameterValueDescription before;
    if (!DescribeParameterValue(document, parameter, before) || !ValidateParameterValue(document, before, value)) return false;
    bool applied = false;
    if (GuardedCall([&] { applied = WriteValue(document, parameter, value, false); }, &document.Host(), "Edit working parameter") && applied) return true;
    GuardedCall([&] { WriteValue(document, parameter, before.value, true); }, &document.Host(), "Restore working parameter");
    return false;
}
EditResult SetParameterValue(Document &document, ObjectRef target, const CKGUID &type, const ParameterValue &value, CK_ID script) {
    auto *parameter = CKParameter::Cast(document.Resolve(target, CKCID_PARAMETER));
    ParameterValueDescription description;
    if (!parameter || parameter->GetGUID() != type || !DescribeParameterValue(document, parameter, description))
        return EditResult::Refused("The parameter or its type is no longer available");
    if (!ValidateParameterValue(document, description, value)) return EditResult::Refused("Invalid parameter value");
    auto normalized = Normalized(value);
    if (description.value == normalized) return EditResult::NoChange("The value is unchanged");
    Edit edit(document, "Set Value");
    if (!edit.Do(std::make_unique<ParameterValueAction>(target, type, std::move(description), std::move(normalized)))) {
        edit.Fail("The parameter value could not be applied");
        return EditResult::Error("The parameter value could not be applied");
    }
    if (!script) if (auto *owner = OwningScript(parameter)) script = owner->GetID();
    edit.Touches(script);
    return edit.Commit();
}
EditResult CopyParameterValue(Document &document, ObjectRef targetRef, ObjectRef sourceRef, CK_ID script) {
    auto *target = CKParameter::Cast(document.Resolve(targetRef, CKCID_PARAMETER));
    auto *source = CKParameter::Cast(document.Resolve(sourceRef, CKCID_PARAMETER, Document::ObjectScope::Existing));
    if (!target || !source || target->GetGUID() != source->GetGUID()) return EditResult::Refused("The parameter or its type is no longer available");
    if (targetRef == sourceRef) return EditResult::NoChange("The value is unchanged");
    ParameterValueDescription description;
    if (DescribeParameterValue(document, source, description)) return SetParameterValue(document, targetRef, description.type, description.value, script);
    if (!ManagesMemory(document.Context(), target->GetType())) return SetParameterBytes(document, script, target->GetID(), ParameterBytes(source));
    // A plugin may own a value that has no portable description. Its existing
    // copier and saved state remain the value representation. Include every
    // member in the saved objects so identity checks also cover nested values.
    std::vector<CK_ID> objects;
    unsigned remaining = 65536;
    const auto collect = [&](const auto &self, CKParameter *to, CKParameter *from, unsigned depth) -> bool {
        if (!to || !from || to->GetGUID() != from->GetGUID() || depth >= 32 || !remaining) return false;
        --remaining; objects.push_back(to->GetID());
        const auto *type = document.Context()->GetParameterManager()->GetParameterTypeDescription(to->GetType());
        if (!type || !(type->dwFlags & CKPARAMETERTYPE_STRUCT)) return true;
        std::vector<ParameterMember> a, b;
        if (!ParameterMembers(document, to, a) || !ParameterMembers(document, from, b) || a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!self(self, CKParameter::Cast(document.Resolve(a[i].parameter, CKCID_PARAMETER)),
                     CKParameter::Cast(document.Resolve(b[i].parameter, CKCID_PARAMETER, Document::ObjectScope::Existing)), depth + 1)) return false;
        return true;
    };
    if (!collect(collect, target, source, 0)) return EditResult::Refused("Invalid parameter members");
    return EditObjectsState(document, objects, "Set Value", [&](CKObject *object) {
        if (object->GetID() != targetRef.object) return;
        auto *from = CKParameter::Cast(document.Resolve(sourceRef, CKCID_PARAMETER, Document::ObjectScope::Existing));
        if (!from || CKParameter::Cast(object)->CopyValue(from, FALSE) != CK_OK) throw std::runtime_error("The parameter value could not be copied");
    });
}
} // namespace vdev

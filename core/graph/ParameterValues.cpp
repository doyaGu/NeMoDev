#include "core/graph/ParameterValues.h"
#include "core/graph/ParameterSelections.h"
#include "core/graph/detail/SpecializedParameterStorage.h"
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
bool ParameterObjectValue(ParameterValueKind kind) {
    return kind == ParameterValueKind::Object || kind == ParameterValueKind::Script ||
        kind == ParameterValueKind::Animation || kind == ParameterValueKind::Synchro;
}
bool ParameterRealValue(ParameterValueKind kind) {
    return kind == ParameterValueKind::Real || kind == ParameterValueKind::Angle || kind == ParameterValueKind::Time;
}
bool operator==(const ParameterValue &a, const ParameterValue &b) {
    if (a.kind != b.kind) return false;
    if (ParameterComponentCount(a.kind)) return a.components == b.components;
    if (a.kind == ParameterValueKind::Text) return a.text == b.text;
    if (a.kind == ParameterValueKind::ObjectArray) return a.objects == b.objects;
    if (a.kind == ParameterValueKind::Structure) return a.members == b.members;
    if (a.kind == ParameterValueKind::Curve2D) return a.curve == b.curve;
    if (a.kind == ParameterValueKind::Dependencies) return a.integer == b.integer && a.options == b.options;
    if (a.kind == ParameterValueKind::LodOptions) return a.integer == b.integer && a.components == b.components;
    if (a.kind == ParameterValueKind::Message) return a.integer == b.integer && (a.integer != -1 || a.text == b.text);
    if (a.kind == ParameterValueKind::Synchro)
        return a.object == b.object && a.text == b.text && (!(a.setWaiters || b.setWaiters) || a.waiters == b.waiters);
    if (ParameterObjectValue(a.kind)) return a.object == b.object;
    if (ParameterRealValue(a.kind)) return a.real == b.real;
    return a.integer == b.integer;
}
ParameterValue ParameterDraftValue(ParameterValue value, const ParameterValue &remembered) {
    if (value.kind != remembered.kind) return value;
    if (ParameterObjectValue(value.kind) && value.object.object == remembered.object.object) value.object = remembered.object;
    if (value.kind == ParameterValueKind::Message && value.integer == -1 && remembered.integer == -1) value.text = remembered.text;
    if (value.kind == ParameterValueKind::Synchro && value.object == remembered.object) {
        value.text = remembered.text;
        if (remembered.setWaiters) { value.waiters = remembered.waiters; value.setWaiters = true; }
    }
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
        for (size_t i = 0; i < value.members.size(); ++i) value.members[i] = ParameterDraftValue(std::move(value.members[i]), remembered.members[i]);
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
    if (ui.ui == ParameterUI::Curve2D || ui.ui == ParameterUI::Dependencies || ui.ui == ParameterUI::LodOptions) {
        if (!DescribeSpecializedParameter(parameter, result)) return false;
        const auto entries = result.value.curve.points.size() + result.value.options.size();
        if (entries > budget.nodes) return false;
        budget.nodes -= unsigned(entries);
        result.display = AnsiToUtf8(ParameterText(parameter)); out = std::move(result); return true;
    }
    if (ui.ui == ParameterUI::Time || ui.ui == ParameterUI::Key || ui.ui == ParameterUI::Script ||
        ui.ui == ParameterUI::Animation || ui.ui == ParameterUI::Message || ui.ui == ParameterUI::Attribute || ui.ui == ParameterUI::Synchro) {
        if (!DescribeParameterSelection(document, parameter, result)) return false;
        out = std::move(result); return true;
    }
    auto &kind = result.value.kind;
    if (ui.ui == ParameterUI::Bool) kind = ParameterValueKind::Boolean;
    else if (ui.ui == ParameterUI::Enum) kind = ParameterValueKind::Enumeration;
    else if (ui.ui == ParameterUI::Flags) kind = ParameterValueKind::Flags;
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
    if (ui.ui == ParameterUI::String) {
        kind = ParameterValueKind::Text;
        result.value.text = AnsiToUtf8(ParameterText(parameter));
    } else if (ui.ui == ParameterUI::ObjectArray) {
        kind = ParameterValueKind::ObjectArray; result.objectClass = CKCID_OBJECT;
        XObjectArray *array = nullptr;
        if (parameter->GetDataSize() != sizeof(array) || parameter->GetValue(&array, FALSE) != CK_OK || !array || array->Size() < 0 || array->Size() > 65536) return false;
        if (unsigned(array->Size()) > budget.objects) return false;
        budget.objects -= unsigned(array->Size());
        for (int i = 0; i < array->Size(); ++i) {
            const auto object = document.Reference((*array)[i]);
            if (!object) return false;
            result.value.objects.push_back(object);
            auto *entry = document.Resolve(object);
            result.rowLabels.push_back(AnsiToUtf8(entry && entry->GetName() ? entry->GetName() : ""));
        }
    } else if (ui.ui == ParameterUI::Struct) {
        kind = ParameterValueKind::Structure;
        std::vector<ParameterMember> members;
        if (!ParameterMembers(document, parameter, members)) return false;
        for (const auto &member : members) {
            ParameterValueDescription child;
            auto *native = CKParameter::Cast(document.Resolve(member.parameter, CKCID_PARAMETER, Document::ObjectScope::Existing));
            if (!DescribeValue(document, native, child, depth + 1, budget)) return false;
            child.name = member.name;
            result.value.members.push_back(child.value); result.members.push_back(std::move(child));
        }
    }
    if (kind == ParameterValueKind::Text || kind == ParameterValueKind::Structure || kind == ParameterValueKind::ObjectArray) {
        result.display = AnsiToUtf8(ParameterText(parameter)); out = std::move(result); return true;
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
    if (kind == ParameterValueKind::Flags) {
        auto *flags = manager->GetFlagsDescByType(parameter->GetType());
        if (!flags || flags->NbData < 0 || flags->NbData > 65536 ||
            (flags->NbData && (!flags->Vals || !flags->Desc))) return false;
        result.columns = 2;
        for (int i = 0; i < flags->NbData; ++i) {
            const auto mask = uint32_t(flags->GetFlagValue(i));
            const auto *label = flags->GetFlagDescription(i);
            result.choices.push_back({flags->GetFlagValue(i), AnsiToUtf8(label ? label : "")});
            // The native dialog places single-bit flags by bit position.
            if (mask && !(mask & (mask - 1)))
                for (unsigned bit = 0; bit < 32; ++bit)
                    if (mask == (uint32_t(1) << bit)) result.rows = std::max(result.rows, bit / 2 + 1);
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
    if (value.kind == ParameterValueKind::Integer || value.kind == ParameterValueKind::Enumeration || value.kind == ParameterValueKind::Boolean || value.kind == ParameterValueKind::Flags) {
        if (!ParseIntegerValue(text, value.integer)) return false;
    } else if (ParameterRealValue(value.kind)) {
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
    if (value.kind == ParameterValueKind::Curve2D)
        return ValidParameterCurve(value.curve) && (value.curve.points.size() >= 2 || description.value.curve.points.size() < 2);
    if (value.kind == ParameterValueKind::Dependencies)
        return value.options.size() <= 65536 && (value == description.value || (value.integer >= 0 && value.integer <= 2));
    if (value.kind == ParameterValueKind::LodOptions)
        return value.components.size() == 6 && std::all_of(value.components.begin(), value.components.end(), [](double number) {
            return std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max();
        });
    if (value.kind == ParameterValueKind::Time || value.kind == ParameterValueKind::Key || value.kind == ParameterValueKind::Script ||
        value.kind == ParameterValueKind::Animation || value.kind == ParameterValueKind::Message || value.kind == ParameterValueKind::Attribute || value.kind == ParameterValueKind::Synchro)
        return ValidateParameterSelection(document, description, value);
    if (value == description.value) return true;
    switch (value.kind) {
    case ParameterValueKind::Boolean: return value.integer == 0 || value.integer == 1;
    case ParameterValueKind::Integer: return true;
    // Unlisted bits belong to the value too; editing a listed flag must not
    // discard bits introduced by another plugin or a newer type definition.
    case ParameterValueKind::Flags: return true;
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
bool Normalize(ParameterValue &value, const ParameterValue *before) {
    if (ParameterRealValue(value.kind)) value.real = float(value.real);
    for (auto &component : value.components) component = float(component);
    if (value.kind == ParameterValueKind::Curve2D) {
        if (before && value.curve == before->curve) value.curve = before->curve;
        else if (!NormalizeParameterCurve(value.curve)) return false;
    }
    for (size_t i = 0; i < value.members.size(); ++i)
        if (!Normalize(value.members[i], before && i < before->members.size() ? &before->members[i] : nullptr)) return false;
    return true;
}
std::vector<std::pair<ObjectRef, CKGUID>> ParameterIdentities(const ParameterValueDescription &description) {
    std::vector<std::pair<ObjectRef, CKGUID>> identities;
    const auto visit = [&](const auto &self, const ParameterValueDescription &value) -> void {
        identities.emplace_back(value.parameter, value.type);
        for (const auto &member : value.members) self(self, member);
    };
    visit(visit, description); return identities;
}
bool WriteValue(const Document &document, CKParameter *parameter, const ParameterValue &value, bool replay, bool draft = false) {
    ParameterValueDescription description;
    if (!DescribeParameterValue(document, parameter, description) || value.kind != description.value.kind) return false;
    if (!replay && !ValidateParameterValue(document, description, value)) return false;
    if (description.value == value) return true;
    if (value.kind == ParameterValueKind::Curve2D || value.kind == ParameterValueKind::Dependencies || value.kind == ParameterValueKind::LodOptions) {
        if (!WriteSpecializedParameter(parameter, value)) return false;
        ParameterValueDescription actual;
        return DescribeParameterValue(document, parameter, actual) && actual.value == value;
    }
    if (value.kind == ParameterValueKind::Structure) {
        if (description.members.size() != value.members.size()) return false;
        for (size_t i = 0; i < value.members.size(); ++i) {
            auto *child = CKParameter::Cast(document.Resolve(description.members[i].parameter, CKCID_PARAMETER, Document::ObjectScope::Existing));
            if (!child || !WriteValue(document, child, value.members[i], replay, draft)) return false;
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
    } else if (ParameterObjectValue(value.kind)) {
        auto *object = value.object ? document.Resolve(value.object, description.objectClass, Document::ObjectScope::Existing) : nullptr;
        if (value.object && !object) return false;
        CK_ID id = object ? object->GetID() : 0;
        std::memcpy(bytes, &id, 4);
    } else if (ParameterRealValue(value.kind)) {
        const auto real = float(value.real); std::memcpy(bytes, &real, 4);
    } else std::memcpy(bytes, &value.integer, 4);
    if (parameter->SetValue(bytes, count ? int(count * sizeof(float)) : 4) != CK_OK) return false;
    ParameterValueDescription actual;
    auto expected = value;
    if (ParameterRealValue(expected.kind)) expected.real = float(expected.real);
    for (auto &component : expected.components) component = float(component);
    if (!DescribeParameterValue(document, parameter, actual)) return false;
    if (draft && value.kind == ParameterValueKind::Message) return actual.value.integer == value.integer;
    if (draft && value.kind == ParameterValueKind::Synchro) return actual.value.object == value.object;
    return actual.value == expected;
}
// Effect parameters are owned by a material and SetEffect can recreate them.
// Their undo target is the material's effect value, including a member path.
struct MaterialParameter {
    ObjectRef material{};
    int effect = 0;
    std::vector<size_t> members;
};
MaterialParameter MaterialParameterOf(Document &doc, CKParameter *parameter) {
    if (!parameter)
        return {};
    MaterialParameter result;
    auto *root = parameter;
    for (unsigned depth = 0; root && depth < 32; ++depth) {
        auto *parent = CKParameter::Cast(root->GetOwner());
        if (!parent)
            break;
        std::vector<ParameterMember> members;
        if (!ParameterMembers(doc, parent, members))
            return {};
        const auto found = std::find_if(members.begin(), members.end(), [&](const auto &member) {
            return member.parameter == doc.Reference(root->GetID());
        });
        if (found == members.end())
            return {};
        result.members.insert(result.members.begin(), size_t(found - members.begin()));
        root = parent;
    }
    for (auto *object : doc.LiveObjects(CKCID_MATERIAL)) {
        auto *material = CKMaterial::Cast(object);
        if (material->GetEffectParameter() == root) {
            if (result.material)
                return {};
            result.material = doc.Reference(material->GetID());
            result.effect = int(material->GetEffect());
        }
    }
    return result;
}
CKParameter *ResolveMaterialParameter(Document &doc, const MaterialParameter &value) {
    auto *material = CKMaterial::Cast(doc.Resolve(value.material, CKCID_MATERIAL, Document::ObjectScope::Existing));
    if (!material || int(material->GetEffect()) != value.effect)
        return nullptr;
    auto *parameter = material->GetEffectParameter();
    for (auto index : value.members) {
        std::vector<ParameterMember> members;
        if (!ParameterMembers(doc, parameter, members) || index >= members.size())
            return nullptr;
        parameter =
            CKParameter::Cast(doc.Resolve(members[index].parameter, CKCID_PARAMETER, Document::ObjectScope::Existing));
    }
    return parameter;
}
class ParameterValueAction final : public Action {
  public:
      ParameterValueAction(Document &doc, ObjectRef target, CKGUID type, ParameterValueDescription before,
                           ParameterValue after)
          : m_Target(target),
            m_Material(MaterialParameterOf(doc, CKParameter::Cast(doc.Resolve(target, CKCID_PARAMETER)))), m_Type(type),
            m_Identities(ParameterIdentities(before)), m_Before(std::move(before.value)), m_After(std::move(after)) {}
      bool Undo(Document &document) override { return Apply(document, m_Before, m_After); }
      bool Redo(Document &document) override { return Apply(document, m_After, m_Before); }
      void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
          const auto retain = [&](ObjectRef ref) {
              if (auto id = document.NativeId(ref, Document::ObjectScope::Existing))
                  objects.insert(id);
          };
          retain(m_Target);
          retain(m_Material.material);
          const auto values = [&](const auto &self, const ParameterValue &value) -> void {
              retain(value.object);
              for (auto ref : value.objects)
                  retain(ref);
              for (const auto &member : value.members)
                  self(self, member);
          };
          values(values, m_Before);
          values(values, m_After);
          for (const auto &identity : m_Identities)
              retain(identity.first);
      }
  private:
    bool Apply(Document &document, const ParameterValue &value, const ParameterValue &restore) {
        auto *parameter =
            m_Material.material
                ? ResolveMaterialParameter(document, m_Material)
                : CKParameter::Cast(document.Resolve(m_Target, CKCID_PARAMETER, Document::ObjectScope::Existing));
        ParameterValueDescription current;
        if (!parameter || parameter->GetGUID() != m_Type || !DescribeParameterValue(document, parameter, current))
            return false;
        const auto identities = ParameterIdentities(current);
        if (m_Material.material) {
            if (identities.size() != m_Identities.size())
                return false;
            for (size_t i = 0; i < identities.size(); ++i)
                if (identities[i].second != m_Identities[i].second)
                    return false;
        } else if (m_Identities != identities)
            return false;
        bool applied = false;
        if (GuardedCall([&] { applied = WriteValue(document, parameter, value, true); }, &document.Host(), "Set parameter value") && applied) return true;
        bool restored = false;
        if (!GuardedCall([&] { restored = WriteValue(document, parameter, restore, true); }, &document.Host(), "Restore parameter value") || !restored)
            document.InvalidateHistory();
        return false;
    }
    ObjectRef m_Target;
    MaterialParameter m_Material;
    CKGUID m_Type;
    std::vector<std::pair<ObjectRef, CKGUID>> m_Identities;
    ParameterValue m_Before, m_After;
};
} // namespace
bool WriteParameterDraft(const Document &document, CKParameter *parameter, const ParameterValue &value) {
    if (!parameter || !parameter->IsDynamic()) return false;
    ParameterValueDescription before;
    if (!DescribeParameterValue(document, parameter, before) || !ValidateParameterValue(document, before, value)) return false;
    auto normalized = value;
    if (!Normalize(normalized, &before.value)) return false;
    bool applied = false;
    if (GuardedCall([&] { applied = WriteValue(document, parameter, normalized, false, true); }, &document.Host(), "Edit working parameter") && applied) return true;
    GuardedCall([&] { WriteValue(document, parameter, before.value, true, true); }, &document.Host(), "Restore working parameter");
    return false;
}
static EditResult EditParameterValue(Document &document, ObjectRef target, const CKGUID &type,
                                     const ParameterValue &value, CK_ID script, bool prepare) {
    auto *parameter = CKParameter::Cast(document.Resolve(target, CKCID_PARAMETER));
    ParameterValueDescription description;
    if (!parameter || parameter->GetGUID() != type || !DescribeParameterValue(document, parameter, description))
        return EditResult::Refused("The parameter or its type is no longer available");
    // Native copies have already been described with their original bounds,
    // types and references. Keep legacy values that an editor cannot create.
    if (prepare && !ValidateParameterValue(document, description, value)) return EditResult::Refused("Invalid parameter value");
    auto normalized = value;
    if (prepare && !Normalize(normalized, &description.value)) return EditResult::Refused("Invalid parameter value");
    if (description.value == normalized) return EditResult::NoChange("The value is unchanged");
    Edit edit(document, "Set Value");
    bool ready = false;
    if (!GuardedCall([&] { ready = PrepareParameterSelection(document, description, normalized); }, &document.Host(), "Prepare parameter value") || !ready) {
        edit.Fail("The parameter selection could not be prepared");
        return EditResult::Error("The parameter selection could not be prepared");
    }
    // Preparation may change a referenced object's rendezvous count. The value
    // action records the parameter after those separate object edits.
    if (!DescribeParameterValue(document, parameter, description)) return EditResult::Error("The parameter is no longer available");
    if (description.value != normalized &&
        !edit.Do(std::make_unique<ParameterValueAction>(document, target, type, std::move(description),
                                                        std::move(normalized)))) {
        edit.Fail("The parameter value could not be applied");
        return EditResult::Error("The parameter value could not be applied");
    }
    if (!script) if (auto *owner = OwningScript(parameter)) script = owner->GetID();
    edit.Touches(script);
    return edit.Commit();
}
EditResult SetParameterValue(Document &document, ObjectRef target, const CKGUID &type, const ParameterValue &value, CK_ID script) {
    return EditParameterValue(document, target, type, value, script, true);
}
EditResult CopyParameterValue(Document &document, ObjectRef targetRef, ObjectRef sourceRef, CK_ID script) {
    auto *target = CKParameter::Cast(document.Resolve(targetRef, CKCID_PARAMETER));
    auto *source = CKParameter::Cast(document.Resolve(sourceRef, CKCID_PARAMETER, Document::ObjectScope::Existing));
    if (!target || !source || target->GetGUID() != source->GetGUID()) return EditResult::Refused("The parameter or its type is no longer available");
    if (targetRef == sourceRef) return EditResult::NoChange("The value is unchanged");
    ParameterValueDescription description;
    // A native copy already includes its computed data. Recomputing it here
    // would change tangent rounding and the semantics of the SDK copier.
    if (DescribeParameterValue(document, source, description))
        return EditParameterValue(document, targetRef, description.type, description.value, script, false);
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

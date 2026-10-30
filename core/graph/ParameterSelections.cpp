#include "core/graph/ParameterSelections.h"
#include "core/graph/ValueCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vdev {
namespace {
bool Name(const std::string &name) {
    return name.size() <= 1024 * 1024 && name.find('\0') == std::string::npos && AnsiToUtf8(Utf8ToAnsi(name)) == name;
}
} // namespace
bool DescribeParameterSelection(const Document &document, CKParameter *parameter, ParameterValueDescription &description) {
    auto *context = document.Context();
    auto *manager = context->GetParameterManager();
    const auto ui = ParameterUIOf(manager, parameter->GetType()).ui;
    auto &value = description.value;
    switch (ui) {
    case ParameterUI::Time:
        value.kind = ParameterValueKind::Time;
        break;
    case ParameterUI::Key:
        value.kind = ParameterValueKind::Key;
        break;
    case ParameterUI::Script:
        value.kind = ParameterValueKind::Script;
        description.objectClass = CKCID_BEHAVIOR;
        break;
    case ParameterUI::Animation:
        value.kind = ParameterValueKind::Animation;
        description.objectClass = manager->TypeToClassID(parameter->GetType());
        break;
    case ParameterUI::Message:
        value.kind = ParameterValueKind::Message;
        break;
    case ParameterUI::Attribute:
        value.kind = ParameterValueKind::Attribute;
        break;
    case ParameterUI::Synchro:
        value.kind = ParameterValueKind::Synchro;
        description.objectClass = manager->TypeToClassID(parameter->GetType());
        break;
    default:
        return false;
    }
    if (parameter->GetDataSize() != 4) return false;
    if (value.kind == ParameterValueKind::Time) {
        float milliseconds = 0;
        if (parameter->GetValue(&milliseconds, FALSE) != CK_OK || !std::isfinite(milliseconds)) return false;
        value.real = milliseconds;
    } else if (ParameterObjectValue(value.kind)) {
        CK_ID id = 0;
        if (parameter->GetValue(&id, FALSE) != CK_OK) return false;
        value.object = document.Reference(id);
        if (id && !value.object) return false;
        if (!description.objectClass) description.objectClass = CKCID_OBJECT;
        if (value.kind == ParameterValueKind::Synchro)
            if (auto *object = CKSynchroObject::Cast(context->GetObject(id))) value.waiters = object->GetRendezVousNumberOfWaiters();
    } else {
        if (parameter->GetValue(&value.integer, FALSE) != CK_OK) return false;
        if (value.kind == ParameterValueKind::Message) {
            auto *messages = context->GetMessageManager();
            const auto *name = messages ? messages->GetMessageTypeName(value.integer) : nullptr;
            value.text = AnsiToUtf8(name ? name : "");
        }
    }
    description.display = AnsiToUtf8(ParameterText(parameter));
    return true;
}
bool ValidateParameterSelection(const Document &document, const ParameterValueDescription &description, const ParameterValue &value) {
    if (value.kind == ParameterValueKind::Time)
        return std::isfinite(value.real) && std::abs(value.real) <= std::numeric_limits<float>::max();
    if (value.kind == ParameterValueKind::Key) return value == description.value || (value.integer >= 0 && value.integer < 256);
    if (value.kind == ParameterValueKind::Attribute) {
        auto *attributes = document.Context()->GetAttributeManager();
        return value == description.value || value.integer == -1 || (attributes && attributes->IsAttributeIndexValid(value.integer));
    }
    if (value.kind == ParameterValueKind::Message) {
        if (!Name(value.text)) return false;
        if (value == description.value || value.integer == -1) return true;
        const auto *name = document.Context()->GetMessageManager()->GetMessageTypeName(value.integer);
        return name && AnsiToUtf8(name) == value.text;
    }
    if (!ParameterObjectValue(value.kind)) return false;
    auto *object = value.object ? document.Resolve(value.object, description.objectClass) : nullptr;
    if (value.object && !object) return false;
    if (value.kind == ParameterValueKind::Script && value != description.value && object) {
        auto *script = CKBehavior::Cast(object);
        return script && script->GetType() == CKBEHAVIORTYPE_SCRIPT &&
               !(script->GetObjectFlags() & (CK_OBJECT_INTERFACEOBJ | CK_OBJECT_PRIVATE | CK_OBJECT_NOTTOBESAVED));
    }
    if (value.kind != ParameterValueKind::Synchro) return true;
    if (!Name(value.text) || (value.object && !value.text.empty())) return false;
    if (!value.text.empty() && description.objectClass != CKCID_SYNCHRO && description.objectClass != CKCID_CRITICALSECTION &&
        description.objectClass != CKCID_STATE)
        return false;
    // Only synchros have a rendezvous count. Preserve signed native values.
    return (!value.waiters || description.objectClass == CKCID_SYNCHRO) &&
           (!value.setWaiters || (value.waiters && (value.object || !value.text.empty())));
}
bool PrepareParameterSelection(Document &document, const ParameterValueDescription &description, ParameterValue &value) {
    if (value.kind == ParameterValueKind::Structure) {
        for (size_t i = 0; i < value.members.size(); ++i)
            if (!PrepareParameterSelection(document, description.members[i], value.members[i])) return false;
    }
    if (value.kind == ParameterValueKind::Message && value.integer == -1 && !value.text.empty()) {
        const auto name = Utf8ToAnsi(value.text);
        value.integer = document.Context()->GetMessageManager()->AddMessageType(name.c_str());
        if (value.integer < 0) return false;
        // Message types are manager registrations, just as in the native editor.
        // Keep their indices stable across undo; only acceptance registers them.
    }
    if (value.kind != ParameterValueKind::Synchro) return true;
    if (!value.text.empty()) {
        const auto name = Utf8ToAnsi(value.text);
        auto *object = document.Context()->GetObjectByNameAndClass(name.c_str(), description.objectClass);
        if (object && !document.IsLive(object->GetID())) return false;
        if (!object) {
            const auto result = CreateNamedObject(document, description.objectClass, name);
            if (!result.ok) return false;
            object = document.Context()->GetObject(result.created);
        }
        if (!object) return false;
        value.object = document.Reference(object->GetID());
        value.text.clear();
    }
    auto *synchro = CKSynchroObject::Cast(document.Resolve(value.object));
    if (!synchro) {
        value.waiters.reset();
        value.setWaiters = false;
        return true;
    }
    if (value.setWaiters && value.waiters && *value.waiters != synchro->GetRendezVousNumberOfWaiters()) {
        const auto users = *value.waiters;
        const auto result = EditObjectState(document, synchro->GetID(), "Set Number Of Users", [users](CKObject *object) {
            CKSynchroObject::Cast(object)->SetRendezVousNumberOfWaiters(users);
        });
        if (!result.ok && !result.Unchanged()) return false;
    }
    value.waiters = synchro->GetRendezVousNumberOfWaiters();
    value.setWaiters = false;
    return true;
}
} // namespace vdev

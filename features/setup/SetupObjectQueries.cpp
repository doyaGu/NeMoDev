#include "features/setup/SetupQueries.h"
#include "core/graph/RuntimeGraph.h"
#include "features/schematic/SchematicPosition.h"
#include "core/schematic/SchematicNavigation.h"
#include "core/graph/ValueCommands.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/GridCompat.h"
#include "core/runtime/SdkCompat.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <set>

namespace vdev {
namespace {
std::string Keys(CKAnimController *controller) {
    if (!controller)
        return "-";
    const char *type = "?";
    switch (controller->GetType()) {
    case CKANIMATION_LINPOS_CONTROL:
    case CKANIMATION_LINROT_CONTROL:
    case CKANIMATION_LINSCL_CONTROL:
    case CKANIMATION_LINSCLAXIS_CONTROL:
        type = "Linear";
        break;
    case CKANIMATION_TCBPOS_CONTROL:
    case CKANIMATION_TCBROT_CONTROL:
    case CKANIMATION_TCBSCL_CONTROL:
    case CKANIMATION_TCBSCLAXIS_CONTROL:
        type = "TCB";
        break;
    case CKANIMATION_BEZIERPOS_CONTROL:
    case CKANIMATION_BEZIERSCL_CONTROL:
        type = "Bezier";
        break;
    case CKANIMATION_MORPH_CONTROL:
        type = "Morph";
        break;
    default:
        break;
    }
    return std::string(type) + " (" + std::to_string(controller->GetKeyCount()) + ")";
}
void Controllers(CKObjectAnimation *animation, SetupDescription &out) {
    out.Text("position-keys", Keys(animation->GetPositionController()));
    out.Text("rotation-keys", Keys(animation->GetRotationController()));
    out.Text("scale-keys", Keys(animation->GetScaleController()));
    out.Text("axis-keys", Keys(animation->GetScaleAxisController()));
    out.Text("morph-keys", Keys(animation->GetMorphController()));
}
void Schematic(CKBehavior *behavior, SetupDescription &out, CKParameterOperation *operation = nullptr) {
    auto *script = SetupScriptOf(behavior);
    out.Link("script", script);
    out.Link("focus", behavior);
    const bool enabled = script && out.document.FindScript(script->GetID());
    out.Integer("schematic-enabled", enabled);
    if (enabled) {
        const auto selected = operation ? ElementRef{ElementKind::Operation, behavior->GetID(), operation->GetID()}
                              : behavior != script && behavior->GetParent()
                                  ? ElementRef{ElementKind::Behavior, behavior->GetParent()->GetID(), behavior->GetID()}
                                  : Selection::HeaderOf(script->GetID());
        out.fields.push_back(
            SchematicPosition(ElementAddress::Capture(out.document, script->GetID(), selected), "position",
                              behavior != script ? out.document.Reference(behavior->GetID()) : ObjectRef{}));
    }
}
CKObject *HeldObject(CKParameter *parameter) {
    return parameter && parameter->GetParameterClassID() ? parameter->GetValueObject(FALSE) : nullptr;
}
std::string OwnedName(CKBehaviorIO *io) {
    return SetupObjectName(io) + (io && io->GetOwner() ? " (" + SetupObjectName(io->GetOwner()) + ")" : "");
}
void ParameterValue(CKParameter *parameter, SetupDescription &out) {
    out.Link("value", parameter);
    out.Text("value-text", parameter ? AnsiToUtf8(ParameterText(parameter)) : "-- None --");
    out.Link("value-object", HeldObject(parameter));
}
} // namespace
bool DescribeSetupObject(CKObject *object, const std::string &section, const std::vector<uint64_t> &,
                         SetupDescription &out) {
    auto *context = out.document.Context();
    if (section == "animation") {
        auto *animation = CKAnimation::Cast(object);
        if (!animation)
            return false;
        out.Real("frame", animation->GetFrame());
        out.Real("step", animation->GetStep());
        out.Integer("merged", animation->IsMerged());
        out.Integer("secondary", animation->GetSecondaryAnimationMode());
        out.Link("character", animation->GetCharacter());
        out.Link("root", animation->GetRootEntity());
        auto *keyed = CKKeyedAnimation::Cast(animation);
        out.Integer("keyed", keyed != nullptr);
        std::vector<EditorField> rows;
        for (int i = 0; keyed && i < keyed->GetAnimationCount(); ++i) {
            auto *part = keyed->GetAnimation(i);
            if (!part)
                continue;
            SetupDescription row{out.document};
            row.Link("entity", part->Get3dEntity());
            row.Real("length", part->GetLength());
            Controllers(part, row);
            auto value = out.Row(part);
            value.fields.insert(value.fields.end(), row.fields.begin(), row.fields.end());
            rows.push_back(std::move(value));
        }
        out.Rows("animations", std::move(rows));
        return true;
    }
    if (section == "object-animation") {
        auto *animation = CKObjectAnimation::Cast(object);
        if (!animation)
            return false;
        out.Link("entity", animation->Get3dEntity());
        out.Real("step", animation->GetCurrentStep());
        out.Link("shared", animation->Shared() != animation ? animation->Shared() : nullptr);
        out.Integer("merged", animation->IsMerged());
        Controllers(animation, out);
        out.Integer("morph", animation->HasMorphInfo());
        out.Integer("morph-count", animation->HasMorphInfo() ? animation->GetMorphVertexCount() : 0);
        out.Integer("morph-normals", animation->HasMorphNormalInfo());
        std::vector<EditorField> users;
        for (auto *candidate : out.document.LiveObjects(CKCID_KEYEDANIMATION, true)) {
            if (candidate->IsPrivate() || candidate->IsDynamic())
                continue;
            auto *owner = CKKeyedAnimation::Cast(candidate);
            for (int i = 0; i < owner->GetAnimationCount(); ++i)
                if (owner->GetAnimation(i) == animation) {
                    users.push_back(out.Row(owner));
                    break;
                }
        }
        out.Rows("users", std::move(users));
        return true;
    }
    if (section == "midi") {
        auto *sound = CKMidiSound::Cast(object);
        if (!sound)
            return false;
        const auto *file = sound->GetSoundFileName();
        out.Text("file", file ? AnsiToUtf8(file) : "");
        const bool playable = context->GetManagerByGuid(MIDI_MANAGER_GUID) && file && *file;
        out.Integer("playable", playable);
        out.Integer("playing", sound->IsPlaying());
        out.Integer("paused", sound->IsPaused());
        out.Integer("position", playable ? sound->GetCurrentPos() : 0);
        return true;
    }
    if (section == "sprite-text") {
        auto *text = CKSpriteText::Cast(object);
        if (!text)
            return false;
        SpriteTextFont font;
        out.Integer("has-font", ReadSpriteTextFont(text, &font));
        return true;
    }
    if (section == "parameter-users") {
        std::vector<EditorField> rows;
        std::set<CKParameter *> sources;
        const auto add = [&](CKObject *parameter) {
            SetupDescription row{out.document};
            auto *behavior = SetupBehaviorOf(parameter);
            row.Link("behavior", behavior);
            row.Link("script", SetupScriptOf(behavior));
            auto value = out.Row(parameter);
            value.fields.insert(value.fields.end(), row.fields.begin(), row.fields.end());
            rows.push_back(std::move(value));
        };
        for (auto *candidate : out.document.LiveObjects(CKCID_PARAMETERIN, true)) {
            auto *input = CKParameterIn::Cast(candidate);
            auto *source = input->GetRealSource();
            if (HeldObject(source) == object) {
                add(input);
                sources.insert(source);
            }
        }
        for (auto *candidate : out.document.LiveObjects(CKCID_PARAMETER, true)) {
            auto *parameter = CKParameter::Cast(candidate);
            if (HeldObject(parameter) == object && !sources.count(parameter))
                add(parameter);
        }
        out.Rows("users", std::move(rows));
        return true;
    }
    if (section == "kinematic") {
        auto *chain = CKKinematicChain::Cast(object);
        if (!chain)
            return false;
        out.Link("start", chain->GetStartEffector());
        out.Link("end", chain->GetEndEffector());
        const bool complete = chain->GetStartEffector() && chain->GetEndEffector();
        out.Integer("complete", complete);
        out.Real("length", complete ? chain->GetChainLength() : 0);
        const int count = complete ? chain->GetChainBodyCount() : 0;
        out.Integer("body-count", count);
        std::vector<EditorField> rows;
        for (int i = 0; i < count; ++i)
            rows.push_back(out.Row(chain->GetEffector(i)));
        out.Rows("bodies", std::move(rows));
        return true;
    }
    if (section == "layer") {
        auto *layer = CKLayer::Cast(object);
        if (!layer)
            return false;
        auto *grid = CKGrid::Cast(context->GetObject(layer->GetOwner()));
        auto *manager = static_cast<CKGridManager *>(context->GetManagerByGuid(GRID_MANAGER_GUID));
        VxColor color(1, 1, 0, 1);
        if (manager)
            manager->GetAssociatedColor(layer->GetType(), &color);
        const int width = grid ? grid->GetWidth() : 0, length = grid ? grid->GetLength() : 0;
        out.Link("grid", grid);
        out.Integer("width", width);
        out.Integer("length", length);
        out.Integer("type", layer->GetType());
        out.Integer("format", layer->GetFormat());
        const auto *name = manager ? manager->GetTypeName(layer->GetType()) : nullptr;
        out.Text("type-name", name ? AnsiToUtf8(name) : "?");
        out.Numbers("color", {color.r, color.g, color.b, color.a});
        EditorField values{"squares", EDITOR_INTEGER_LIST};
        for (int y = 0; y < length; ++y)
            for (int x = 0; x < width; ++x) {
                int value = 0;
                layer->GetValue2(x, y, &value);
                values.integers.push_back(uint64_t(int64_t(value)));
            }
        out.fields.push_back(std::move(values));
        return true;
    }
    if (section == "parameter") {
        auto *parameter = CKParameter::Cast(object);
        if (!parameter)
            return false;
        auto *local = CKParameterLocal::Cast(parameter);
        auto *output = CKParameterOut::Cast(parameter);
        auto *behavior = SetupBehaviorOf(parameter);
        bool setting = false;
        for (int i = 0; local && behavior && i < behavior->GetLocalParameterCount(); ++i)
            if (behavior->GetLocalParameter(i) == local)
                setting = behavior->IsLocalParameterSetting(i) != 0;
        const bool myself = local && local->IsMyselfParameter();
        out.Text("heading", local    ? myself    ? "This Parameter"
                                       : setting ? "Setting"
                                                 : "Local Parameter"
                            : output ? "Output Parameter"
                                     : "Parameter");
        out.Text("type-name", SetupTypeName(context, parameter->GetType()));
        out.Link("owner", parameter->GetOwner());
        out.Link("behavior", behavior);
        out.Integer("editable", !myself);
        out.Integer("output", output != nullptr);
        ParameterValue(parameter, out);
        Schematic(behavior, out);
        std::vector<EditorField> rows;
        for (int i = 0; output && i < output->GetDestinationCount(); ++i) {
            auto *destination = output->GetDestination(i);
            if (!destination)
                continue;
            SetupDescription row{out.document};
            row.Link("owner", destination->GetOwner());
            auto value = out.Row(destination);
            value.fields.insert(value.fields.end(), row.fields.begin(), row.fields.end());
            rows.push_back(std::move(value));
        }
        out.Rows("destinations", std::move(rows));
        return true;
    }
    if (section == "parameter-input") {
        auto *input = CKParameterIn::Cast(object);
        if (!input)
            return false;
        out.Integer("enabled", input->IsEnabled());
        out.Text("type-name", SetupTypeName(context, input->GetType()));
        out.Link("owner", input->GetOwner());
        out.Link("behavior", SetupBehaviorOf(input));
        out.Link("direct", input->GetDirectSource());
        out.Link("shared", input->GetSharedSource());
        ParameterValue(input->GetRealSource(), out);
        out.Integer("editable", EditableValueOf(input) != nullptr);
        Schematic(SetupBehaviorOf(input), out);
        return true;
    }
    if (section == "operation") {
        auto *operation = CKParameterOperation::Cast(object);
        if (!operation)
            return false;
        const auto *name = context->GetParameterManager()->OperationGuidToName(operation->GetOperationGuid());
        out.Text("operation", name && *name ? AnsiToUtf8(name) : "?");
        out.Text("guid", SetupGuidText(operation->GetOperationGuid()));
        out.Link("owner", operation->GetOwner());
        Schematic(operation->GetOwner(), out, operation);
        std::vector<EditorField> rows;
        const auto add = [&](const char *label, CKObject *pin, CKParameterType type, CKParameter *value) {
            auto row = out.Row(pin);
            row.fields.push_back({"label", EDITOR_TEXT_VALUE, {}, label});
            row.fields.push_back({"type-name", EDITOR_TEXT_VALUE, {}, pin ? SetupTypeName(context, type) : ""});
            row.fields.push_back({"value-text", EDITOR_TEXT_VALUE, {}, value ? AnsiToUtf8(ParameterText(value)) : ""});
            rows.push_back(std::move(row));
        };
        auto *a = operation->GetInParameter1(), *b = operation->GetInParameter2();
        auto *result = operation->GetOutParameter();
        add("Input 1", a, a ? a->GetType() : 0, a ? a->GetRealSource() : nullptr);
        add("Input 2", b, b ? b->GetType() : 0, b ? b->GetRealSource() : nullptr);
        add("Output", result, result ? result->GetType() : 0, result);
        out.Rows("operands", std::move(rows));
        return true;
    }
    if (section == "behavior") {
        auto *behavior = CKBehavior::Cast(object);
        if (!behavior)
            return false;
        auto *script = SetupScriptOf(behavior);
        const bool graph = behavior->GetType() != CKBEHAVIORTYPE_BASE;
        out.Integer("graph", graph);
        out.Text("heading", behavior->GetType() == CKBEHAVIORTYPE_SCRIPT ? "Script"
                            : graph                                      ? "Behavior Graph"
                                                                         : "Building Block");
        out.Integer("prototype", !graph || behavior->GetPrototypeGuid() != CKGUID());
        out.Text("prototype-name", behavior->GetPrototypeName() ? AnsiToUtf8(behavior->GetPrototypeName()) : "-");
        out.Text("guid", SetupGuidText(behavior->GetPrototypeGuid()));
        out.Integer("version", behavior->GetVersion());
        out.Link("owner", behavior->GetOwner());
        out.Link("parent", behavior->GetParent());
        Schematic(behavior, out);
        out.Integer("active", behavior->IsActive());
        out.Integer("using-target", behavior->IsUsingTarget());
        out.Link("target-parameter", behavior->IsUsingTarget() ? behavior->GetTargetParameter() : nullptr);
        const auto *compatible = CKClassIDToString(behavior->GetCompatibleClassID());
        out.Text("compatible", compatible ? compatible : "?");
        out.Integer("flags", behavior->GetFlags());
        out.Integer("inputs", behavior->GetInputCount());
        out.Integer("outputs", behavior->GetOutputCount());
        out.Integer("input-parameters", behavior->GetInputParameterCount());
        out.Integer("output-parameters", behavior->GetOutputParameterCount());
        out.Integer("local-parameters", behavior->GetLocalParameterCount());
        out.Integer("links", behavior->GetSubBehaviorLinkCount());
        out.Integer("operations", behavior->GetParameterOperationCount());
        std::vector<EditorField> rows;
        for (int i = 0; graph && i < behavior->GetSubBehaviorCount(); ++i) {
            auto *child = behavior->GetSubBehavior(i);
            if (!child)
                continue;
            auto row = out.Row(child);
            row.fields.push_back({"base", EDITOR_INTEGER_VALUE, {}, {}, child->GetType() == CKBEHAVIORTYPE_BASE});
            row.fields.push_back({"priority", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(child->GetPriority()))});
            rows.push_back(std::move(row));
        }
        out.Rows("behaviors", std::move(rows));
        return true;
    }
    if (section == "behavior-io") {
        auto *io = CKBehaviorIO::Cast(object);
        if (!io)
            return false;
        auto *owner = io->GetOwner();
        out.Link("owner", owner);
        out.Integer("input", (io->GetType() & CK_BEHAVIORIO_IN) != 0);
        out.Integer("active", io->IsActive());
        Schematic(owner, out);
        std::vector<EditorField> rows;
        std::set<CKBehaviorLink *> seen;
        for (auto *graph : {owner ? owner->GetParent() : nullptr, owner}) {
            if (!graph || graph->GetType() == CKBEHAVIORTYPE_BASE)
                continue;
            for (int i = 0; i < graph->GetSubBehaviorLinkCount(); ++i) {
                auto *link = graph->GetSubBehaviorLink(i);
                if (!link || (link->GetInBehaviorIO() != io && link->GetOutBehaviorIO() != io) ||
                    !seen.insert(link).second)
                    continue;
                auto row = out.Row(link);
                row.fields.push_back({"from", EDITOR_TEXT_VALUE, {}, OwnedName(link->GetInBehaviorIO())});
                row.fields.push_back({"to", EDITOR_TEXT_VALUE, {}, OwnedName(link->GetOutBehaviorIO())});
                row.fields.push_back(
                    {"delay", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(link->GetInitialActivationDelay()))});
                rows.push_back(std::move(row));
            }
        }
        out.Rows("links", std::move(rows));
        return true;
    }
    if (section == "behavior-link") {
        auto *link = CKBehaviorLink::Cast(object);
        if (!link)
            return false;
        auto *from = link->GetInBehaviorIO(), *to = link->GetOutBehaviorIO();
        auto *start = from ? from->GetOwner() : nullptr;
        out.Link("from", from);
        out.Link("to", to);
        out.Link("start", start);
        out.Link("end", to ? to->GetOwner() : nullptr);
        Schematic(start ? start : to ? to->GetOwner() : nullptr, out);
        out.Integer("current-delay", link->GetActivationDelay());
        return true;
    }
    if (section == "synchro") {
        auto *synchro = CKSynchroObject::Cast(object);
        if (!synchro)
            return false;
        std::vector<EditorField> rows;
        for (int i = 0; i < synchro->GetRendezVousNumberOfArrivedObjects(); ++i)
            rows.push_back(out.Row(synchro->GetRendezVousArrivedObject(i)));
        out.Rows("arrived", std::move(rows));
        return true;
    }
    if (section == "state")
        return CKStateObject::Cast(object) != nullptr;
    if (section == "critical-section")
        return CKCriticalSectionObject::Cast(object) != nullptr;
    if (section == "interface-objects") {
        auto *manager = InterfaceObjectManagerOf(object);
        if (!manager)
            return false;
        out.Text("guid", SetupGuidText(manager->GetGuid()));
        out.Integer("count", manager->GetChunkCount());
        EditorField sizes{"sizes", EDITOR_INTEGER_LIST};
        for (int i = 0; i < manager->GetChunkCount(); ++i)
            sizes.integers.push_back(manager->GetChunk(i) ? uint64_t(manager->GetChunk(i)->GetDataSize()) : UINT64_MAX);
        out.fields.push_back(std::move(sizes));
        return true;
    }
    if (section == "other") {
        const auto *name = CKClassIDToString(object->GetClassID());
        out.Text("class-name", name ? name : "?");
        out.Text("name", SetupObjectName(object));
        return true;
    }
    return false;
}
} // namespace vdev

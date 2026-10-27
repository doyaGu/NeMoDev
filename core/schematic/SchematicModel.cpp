#include "core/schematic/SchematicModel.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <set>

#include "core/schematic/BehaviorDisplayGeometry.h"
#include "core/interface/InterfaceChunkFormat.h"
#include "core/schematic/LinkEndpointGeometry.h"
#include "core/schematic/ParameterDisplayGeometry.h"
#include "core/graph/RuntimeGraph.h"
#include "core/level/SceneCommands.h"
#include "core/document/ScriptSession.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

// Inline InterfaceChunk header bits.
constexpr CKDWORD UI_FLAG_SCRIPT_HIDDEN = 0x20000;
// Fallback Start size; the chunk does not store it.
constexpr float DEFAULT_START_WIDTH = 30.0f;
constexpr float DEFAULT_START_HEIGHT = 20.0f;

std::string NameOf(CKObject *object) {
    return object && object->GetName() ? AnsiToUtf8(object->GetName()) : std::string();
}

std::string TypeName(CKContext *context, CKParameterType type) {
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
    const char *name = manager && type >= 0 ? manager->ParameterTypeToName(type) : nullptr;
    return name ? AnsiToUtf8(name) : std::string();
}

std::string ValueOf(CKParameter *parameter) {
    if (!parameter) return std::string();
    // GetStringValue(nullptr) returns the required size including the NUL.
    const int size = parameter->GetStringValue(nullptr, FALSE);
    if (size <= 0 || size > 4096) return std::string();
    std::string value(static_cast<size_t>(size), '\0');
    parameter->GetStringValue(&value[0], FALSE);
    value.resize(std::strlen(value.c_str()));
    return AnsiToUtf8(value);
}

void Grow(Rect &bounds, bool &empty, const Rect &rect) {
    if (!std::isfinite(rect.hPos) || !std::isfinite(rect.vPos) || !std::isfinite(rect.hSize) ||
        !std::isfinite(rect.vSize))
        return;
    if (empty) {
        bounds = rect;
        empty = false;
        return;
    }
    const float right = std::max(bounds.Right(), rect.Right());
    const float bottom = std::max(bounds.Bottom(), rect.Bottom());
    bounds.hPos = std::min(bounds.hPos, rect.hPos);
    bounds.vPos = std::min(bounds.vPos, rect.vPos);
    bounds.hSize = right - bounds.hPos;
    bounds.vSize = bottom - bounds.vPos;
}

} // namespace

const BehaviorItem *SchematicModel::FindBehavior(CK_ID id) const {
    auto found = m_BehaviorIndex.find(id);
    return found != m_BehaviorIndex.end() ? &behaviors[found->second] : nullptr;
}

const PinItem *SchematicModel::FindPin(const LinkEndpoint &endpoint) const {
    for (const auto &pin : pins)
        if (pin.endpoint.id == endpoint.id && pin.endpoint.index == endpoint.index &&
            pin.endpoint.type == endpoint.type)
            return &pin;
    return nullptr;
}

CK_ID SchematicModel::GraphAt(const Point &point) const {
    CK_ID best = 0;
    int bestDepth = -1;
    for (const auto &behavior : behaviors) {
        if (!IsOpenGraph(behavior.id) || behavior.depth <= bestDepth) continue;
        if (behavior.box.Contains(point)) {
            best = behavior.id;
            bestDepth = behavior.depth;
        }
    }
    if (best) return best;
    return script.open && script.bounds.Contains(point) ? script.id : 0;
}

void SchematicModel::Build(const ScriptSession &session, CKContext *context, const TextMeasure &measure) {
    *this = SchematicModel();
    const InterfaceData &data = session.Data();
    const BehaviorData &rootRecord = data.rootBehavior;
    CKBehavior *root = context ? CKBehavior::Cast(context->GetObject(session.ScriptId())) : nullptr;
    const bool rootIsScript = root && (root->GetType() & CKBEHAVIORTYPE_SCRIPT);

    script.id = session.ScriptId();
    script.ref = {ElementKind::Script, script.id, script.id};
    script.name = NameOf(root);
    script.ownerName = root ? NameOf(root->GetOwner()) : std::string();
    script.ownerClass = root && root->GetOwner() ? root->GetOwner()->GetClassID() : 0;
    if (CKScene *scene = root ? InitialConditionsScene(context) : nullptr)
        script.initialConditions = scene->IsObjectHere(root) && scene->GetObjectInitialValue(root);
    script.isScript = rootIsScript;
    script.open = session.IsOpen();
    script.hidden = (rootRecord.flags & UI_FLAG_SCRIPT_HIDDEN) != 0;
    script.color = data.header.color;
    script.priority = root ? root->GetPriority() : 0;
    script.snapshot = data.header.snapshot;
    script.snapshotOwner = data.header.SnapshotOwner();
    m_OpenGraphs[script.id] = script.open;

    // Records by id with their depth below the Script.
    std::map<CK_ID, const BehaviorData *> records;
    records[rootRecord.id] = &rootRecord;
    for (const auto &record : data.behaviors) records[record.id] = &record;
    std::vector<std::pair<int, const BehaviorData *>> ordered;
    for (const auto &record : data.behaviors) {
        int depth = 1;
        CK_ID parent = record.parentId;
        while (parent && parent != rootRecord.id && depth < 64) {
            auto found = records.find(parent);
            if (found == records.end()) break;
            parent = found->second->parentId;
            ++depth;
        }
        ordered.emplace_back(depth, &record);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });

    bool emptyBounds = true;
    auto resolve = [&](const LinkEndpoint &endpoint, Point &position) {
        return TryGetLinkEndpointPosition(data, endpoint, position, rootIsScript);
    };
    auto addPin = [&](CK_ID owner, PinKind kind, int index, EndpointType type, const std::string &name,
                      const std::string &typeName, bool onOperation, bool onScript) {
        PinItem pin;
        pin.owner = owner;
        pin.kind = kind;
        pin.index = index;
        pin.endpoint = LinkEndpoint(owner, index, type);
        pin.name = name;
        pin.typeName = typeName;
        pin.onOperation = onOperation;
        pin.onScript = onScript;
        pin.ref = {ElementKind::Pin, owner, owner, index, -1, kind};
        if (!resolve(pin.endpoint, pin.center)) return;
        pins.push_back(pin);
    };
    auto addBehaviorPins = [&](CKBehavior *behavior, CK_ID id, bool isRoot) {
        if (!behavior) return;
        if (!isRoot) {
            for (int i = 0; i < behavior->GetInputCount(); ++i)
                addPin(id, PinKind::BehaviorIn, i, ENDPOINT_BIN, NameOf(behavior->GetInput(i)), "", false, false);
            for (int i = 0; i < behavior->GetOutputCount(); ++i)
                addPin(id, PinKind::BehaviorOut, i, ENDPOINT_BOUT, NameOf(behavior->GetOutput(i)), "", false, false);
        }
        for (int i = 0; i < behavior->GetInputParameterCount(); ++i) {
            CKParameterIn *input = behavior->GetInputParameter(i);
            // Dev does not show disabled inputs (Building Block settings).
            if (!isRoot && input && !input->IsEnabled()) continue;
            addPin(id, PinKind::ParamIn, i, ENDPOINT_PIN, NameOf(input),
                   input ? TypeName(context, input->GetType()) : "", false, isRoot);
        }
        for (int i = 0; i < behavior->GetOutputParameterCount(); ++i) {
            CKParameterOut *output = behavior->GetOutputParameter(i);
            addPin(id, PinKind::ParamOut, i, ENDPOINT_POUT, NameOf(output),
                   output ? TypeName(context, output->GetType()) : "", false, isRoot);
        }
        if (behavior->IsUsingTarget() && behavior->GetTargetParameter()) {
            CKParameterIn *target = behavior->GetTargetParameter();
            addPin(id, PinKind::Target, -2, ENDPOINT_TARGET_PIN, NameOf(target),
                   TypeName(context, target->GetType()), false, isRoot);
        }
    };

    // Behaviors: visible when every enclosing graph is open.
    for (const auto &entry : ordered) {
        const BehaviorData &record = *entry.second;
        auto open = m_OpenGraphs.find(record.parentId ? record.parentId : rootRecord.id);
        if (open == m_OpenGraphs.end() || !open->second) continue;
        CKBehavior *behavior = context ? CKBehavior::Cast(context->GetObject(record.id)) : nullptr;
        BehaviorItem item;
        item.ref = {ElementKind::Behavior, record.parentId, record.id};
        item.id = record.id;
        item.parent = record.parentId;
        item.depth = entry.first;
        item.name = NameOf(behavior);
        item.prototype = behavior && behavior->GetPrototypeName() ? AnsiToUtf8(behavior->GetPrototypeName()) : "";
        item.missingRuntime = behavior == nullptr;
        item.graph = behavior && behavior->GetType() != CKBEHAVIORTYPE_BASE;
        item.buildingBlock = behavior && (behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK);
        item.expandable = item.graph && !item.buildingBlock;
        item.folded = record.folded;
        item.usingTarget = behavior && behavior->IsUsingTarget();
        item.priority = behavior ? behavior->GetPriority() : 0;
        item.settings = item.buildingBlock && behavior->GetLocalParameterCount() > 0;
        item.variable = behavior && (behavior->GetFlags() & (CKBEHAVIOR_VARIABLEINPUTS | CKBEHAVIOR_VARIABLEOUTPUTS |
                                                             CKBEHAVIOR_VARIABLEPARAMETERINPUTS |
                                                             CKBEHAVIOR_VARIABLEPARAMETEROUTPUTS));
        const CKDWORD behaviorFlags = behavior ? behavior->GetFlags() : 0;
        item.customDialog = item.buildingBlock && (behaviorFlags & (CKBEHAVIOR_CUSTOMEDITDIALOG | CKBEHAVIOR_CUSTOMSETTINGSEDITDIALOG));
        item.sender = item.buildingBlock && (behaviorFlags & CKBEHAVIOR_MESSAGESENDER);
        item.receiver = item.buildingBlock && (behaviorFlags & CKBEHAVIOR_MESSAGERECEIVER);
        if (item.buildingBlock) {
            const CKGUID guid = behavior->GetPrototypeGuid();
            CKBehaviorPrototype *prototype = CKGetPrototypeFromGuid(guid);
            CKObjectDeclaration *declaration = CKGetObjectDeclarationFromGuid(guid);
            item.prototypeMissing = !prototype;
            item.obsolete = prototype && (prototype->GetFlags() & CK_BEHAVIORPROTOTYPE_OBSOLETE);
            // To replace (Dev.exe sub_536C70): another version than the
            // declaration's, or pins other than the prototype's where they
            // are not variable or made by the Building Block itself.
            if (declaration && behavior->GetVersion() != declaration->GetVersion()) {
                item.outdated = true;
            } else if (prototype) {
                const CKDWORD flags = behavior->GetFlags();
                item.outdated = (!(flags & 0x1000080) && prototype->GetInputCount() != behavior->GetInputCount()) ||
                                (!(flags & 0x2000100) && prototype->GetOutputCount() != behavior->GetOutputCount()) ||
                                (!(flags & 0x4000200) && prototype->GetInParameterCount() != behavior->GetInputParameterCount()) ||
                                (!(flags & 0x8000400) && prototype->GetOutParameterCount() != behavior->GetOutputParameterCount()) ||
                                (!(flags & 0x40000000) && prototype->GetLocalParameterCount() != behavior->GetLocalParameterCount());
            }
        }
        Rect content;
        if (BehaviorDisplayGeometry::TryGetContentBounds(record, content)) {
            item.box = Rect(record.rect.hPos + content.hPos, record.rect.vPos + content.vPos, content.hSize,
                            content.vSize);
        } else {
            item.box = record.rect;
        }
        m_OpenGraphs[record.id] = item.expandable && !item.folded;
        m_BehaviorIndex[record.id] = behaviors.size();
        behaviors.push_back(item);
        Grow(contentBounds, emptyBounds, item.box);
        addBehaviorPins(behavior, record.id, false);
    }
    if (script.open) addBehaviorPins(root, rootRecord.id, true);

    // Locals some shortcut of this Script displays.
    std::set<CK_ID> shortcutSources;
    for (const auto &entry : records)
        for (const auto &shortcut : entry.second->sharedParams) shortcutSources.insert(shortcut.sourceId);

    // Elements stored by each open graph.
    for (const auto &entry : records) {
        const BehaviorData &graph = *entry.second;
        if (!IsOpenGraph(graph.id)) continue;
        const bool scriptUi = rootIsScript && graph.id == rootRecord.id;
        const Point origin(graph.rect.hPos, graph.rect.vPos);
        std::set<int> hiddenLocals; // local slots holding the value of a disabled input
        std::set<int> directLocals; // local slots linked straight to a parameter input

        for (int i = 0; i < static_cast<int>(graph.links.size()); ++i) {
            const Link &link = graph.links[i];
            LinkItem item;
            item.ref = {ElementKind::Link, graph.id, link.id, i};
            item.owner = graph.id;
            item.slot = i;
            item.behaviorLink = link.IsBehaviorLink();
            item.highlight = link.highlight;
            for (const auto &operation : graph.operations)
                item.operationLink = item.operationLink || operation.id == link.start.id || operation.id == link.end.id;
            item.start = link.start;
            item.end = link.end;
            Point from, to;
            const bool hasFrom = resolve(link.start, from);
            const bool hasTo = resolve(link.end, to);
            item.resolved = hasFrom && hasTo;
            if (!item.resolved) ++unresolvedEndpoints;
            if (hasFrom) item.path.push_back(from);
            for (const auto &point : link.points) item.path.push_back(point);
            if (hasTo) item.path.push_back(to);
            if (item.behaviorLink && context) {
                if (auto *runtime = CKBehaviorLink::Cast(context->GetObject(link.id)))
                    item.delay = runtime->GetInitialActivationDelay();
            }
            if (!item.behaviorLink && link.start.type == ENDPOINT_PLOCAL && link.start.id == graph.id &&
                (link.end.type == ENDPOINT_PIN || link.end.type == ENDPOINT_TARGET_PIN) && link.points.empty())
                directLocals.insert(link.start.index);
            if (!item.behaviorLink && context) {
                CKParameterIn *input = CKParameterIn::Cast(ResolveEndpointObject(context, graph, link.end));
                if (input && !input->IsEnabled()) {
                    item.hidden = true;
                    if (link.start.type == ENDPOINT_PLOCAL && link.start.id == graph.id)
                        hiddenLocals.insert(link.start.index);
                }
                if (CKParameterOperation *conversion = HiddenConversion(context, graph, link)) {
                    const char *name = context->GetParameterManager()
                                           ? context->GetParameterManager()->OperationGuidToName(
                                                 conversion->GetOperationGuid())
                                           : nullptr;
                    item.conversion = name ? AnsiToUtf8(name) : NameOf(conversion);
                    item.operationLink = true;
                }
            }
            links.push_back(item);
        }

        for (const auto &operation : graph.operations) {
            OperationItem item;
            item.ref = {ElementKind::Operation, graph.id, operation.id};
            item.owner = graph.id;
            item.id = operation.id;
            ParameterDisplayGeometry::TryGetOperationBounds(operation, item.bounds);
            CKParameterOperation *runtime =
                context ? CKParameterOperation::Cast(context->GetObject(operation.id)) : nullptr;
            if (runtime && context->GetParameterManager()) {
                const char *name = context->GetParameterManager()->OperationGuidToName(runtime->GetOperationGuid());
                item.name = name ? AnsiToUtf8(name) : NameOf(runtime);
                if (CKParameterIn *in1 = runtime->GetInParameter1())
                    addPin(operation.id, PinKind::ParamIn, 0, ENDPOINT_PIN, NameOf(in1),
                           TypeName(context, in1->GetType()), true, false);
                if (CKParameterIn *in2 = runtime->GetInParameter2())
                    addPin(operation.id, PinKind::ParamIn, 1, ENDPOINT_PIN, NameOf(in2),
                           TypeName(context, in2->GetType()), true, false);
                if (CKParameterOut *out = runtime->GetOutParameter())
                    addPin(operation.id, PinKind::ParamOut, 0, ENDPOINT_POUT, NameOf(out),
                           TypeName(context, out->GetType()), true, false);
            }
            operations.push_back(item);
            Grow(contentBounds, emptyBounds, item.bounds);
        }

        // Local display slots follow CKBehavior::GetLocalParameter order; loaded
        // chunks do not store the local's CK_ID (Parameter::id is 0).
        CKBehavior *graphBehavior = context ? CKBehavior::Cast(context->GetObject(graph.id)) : nullptr;
        auto addParams = [&](const std::vector<Parameter> &table, bool shortcut) {
            for (int i = 0; i < static_cast<int>(table.size()); ++i) {
                const Parameter &parameter = table[i];
                ParamItem item;
                item.ref = {shortcut ? ElementKind::Shortcut : ElementKind::LocalParam, graph.id, parameter.id, i};
                item.owner = graph.id;
                item.slot = i;
                item.shortcut = shortcut;
                item.hidden = !shortcut && hiddenLocals.count(i) != 0;
                item.style = parameter.style;
                item.directInput = !shortcut && directLocals.count(i) != 0 &&
                                   ParamDisplayMode(parameter.style) == PARAM_STYLE_COLLAPSED;
                CKParameter *runtime = nullptr;
                if (shortcut) {
                    runtime = context ? CKParameter::Cast(context->GetObject(parameter.sourceId)) : nullptr;
                } else if (graphBehavior && i < graphBehavior->GetLocalParameterCount()) {
                    runtime = graphBehavior->GetLocalParameter(i);
                }
                item.parameter = runtime ? runtime->GetID() : (shortcut ? parameter.sourceId : parameter.id);
                item.ref.object = item.parameter;
                item.shortcutSource = !shortcut && item.parameter && shortcutSources.count(item.parameter) != 0;
                Rect bounds = ParameterDisplayGeometry::EstimateBounds(parameter, context, scriptUi);
                item.bounds = Rect(origin.h + bounds.hPos, origin.v + bounds.vPos, bounds.hSize, bounds.vSize);
                const Point position = ParameterDisplayGeometry::GetPosition(parameter, scriptUi);
                item.center = Point(origin.h + position.h + 10.0f, origin.v + position.v + 10.0f);
                item.name = NameOf(runtime);
                item.value = ValueOf(runtime);
                item.typeName = runtime ? TypeName(context, runtime->GetType()) : "";
                item.display = item.bounds;
                // VALUE/NAMEVALUE reserve Dev's maximum widths (1020/1520);
                // draw and pick only the measured text.
                const int mode = ParamDisplayMode(parameter.style);
                if (measure && (mode == PARAM_STYLE_VALUE || mode == PARAM_STYLE_NAMEVALUE)) {
                    const std::string text = mode == PARAM_STYLE_VALUE ? item.value : item.name + ": " + item.value;
                    item.display.hSize = std::min(item.bounds.hSize, std::max(20.0f, measure(text) + 30.0f));
                }
                if (std::getenv("SCHEMATIC_PARAM_TRACE"))
                    std::fprintf(stderr, "param graph=%u %s slot=%d style=0x%x name='%s' value='%s' at %.0f,%.0f\n",
                                 static_cast<unsigned>(graph.id), shortcut ? "shortcut" : "local", i,
                                 static_cast<unsigned>(parameter.style), item.name.c_str(), item.value.substr(0, 40).c_str(),
                                 item.center.h, item.center.v);
                params.push_back(item);
                Grow(contentBounds, emptyBounds, item.bounds);
            }
        };
        const size_t firstParam = params.size();
        addParams(graph.localParams, false);
        for (auto &item : links) {
            if (item.owner != graph.id || item.behaviorLink || item.start.type != ENDPOINT_PLOCAL || !item.path.size())
                continue;
            const size_t index = firstParam + static_cast<size_t>(item.start.index);
            item.fromDirectLocal = item.start.index >= 0 && index < params.size() && params[index].directInput &&
                                   item.path.size() == 2;
        }
        addParams(graph.sharedParams, true);

        for (int i = 0; i < static_cast<int>(graph.comments.size()); ++i) {
            const Comment &comment = graph.comments[i];
            CommentItem item;
            item.ref = {ElementKind::Comment, graph.id, comment.id, i};
            item.owner = graph.id;
            item.slot = i;
            item.text = AnsiToUtf8(comment.text);
            item.collapsed = comment.IsCollapsed();
            item.bounds = comment.GetRect();
            comments.push_back(item);
            Grow(contentBounds, emptyBounds, item.bounds);
        }
    }

    if (rootIsScript && script.open) {
        start.present = true;
        start.ref = {ElementKind::Start, script.id, script.id};
        const float width = data.header.hStartSize > 0.0f ? data.header.hStartSize : DEFAULT_START_WIDTH;
        const float height = data.header.vStartSize > 0.0f ? data.header.vStartSize : DEFAULT_START_HEIGHT;
        start.bounds = Rect(data.header.hStartPos, rootRecord.rect.vPos + data.header.vStartPos, width, height);
        CKBehaviorIO *startIo = root->GetInputCount() > 0 ? root->GetInput(0) : nullptr;
        if (!resolve(LinkEndpoint(startIo ? startIo->GetID() : 0, 0, ENDPOINT_START_BIN), start.center))
            start.center = Point(start.bounds.Right(), start.bounds.CenterY());
        Grow(contentBounds, emptyBounds, start.bounds);
    }

    // Breakpoints (ExtraData types 1 parameter, 2 behavior, 3 link).
    for (const auto &entry : data.extraData) {
        if (entry.type == EXTRA_DATA_BEHAVIOR)
            for (auto &behavior : behaviors) behavior.breakpoint = behavior.breakpoint || behavior.id == entry.id1;
        if (entry.type == EXTRA_DATA_CONNECTION)
            for (auto &link : links) link.breakpoint = link.breakpoint || (link.behaviorLink && link.ref.object == entry.id1);
        if (entry.type == EXTRA_DATA_PARAMETER)
            for (auto &param : params) param.breakpoint = param.breakpoint || param.parameter == entry.id1;
    }

    // The band has the stored Script height (Dev resizes it by dragging its
    // boundary); contents below are clipped. Without a stored height, the
    // contents decide.
    float height = CLOSED_SCRIPT_HEIGHT;
    script.contentHeight = std::max(MIN_OPEN_SCRIPT_HEIGHT,
                                    emptyBounds ? MIN_OPEN_SCRIPT_HEIGHT
                                                : contentBounds.Bottom() + 20.0f - rootRecord.rect.vPos);
    if (script.open) {
        height = data.header.vSize > 0.0f ? data.header.vSize : script.contentHeight;
        height = std::max(height, MIN_OPEN_SCRIPT_HEIGHT);
    }
    float right = rootRecord.rect.hPos + SCRIPT_HEADER_WIDTH + 400.0f;
    if (script.open && !emptyBounds) right = std::max(right, contentBounds.Right() + 40.0f);
    script.bounds = Rect(rootRecord.rect.hPos, rootRecord.rect.vPos, right - rootRecord.rect.hPos, height);
    script.header = Rect(script.bounds.hPos, script.bounds.vPos, SCRIPT_HEADER_WIDTH, script.bounds.vSize);
}

} // namespace vdev

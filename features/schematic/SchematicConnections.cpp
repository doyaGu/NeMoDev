#include "features/schematic/SchematicConnections.h"
#include "features/schematic/SchematicPosition.h"
#include "core/document/Document.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ExportCommands.h"
#include "core/graph/LinkCommands.h"
#include "core/graph/OperationCommands.h"
#include "core/schematic/LinkRouting.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicGrid.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <set>

namespace vdev {
namespace {
const EditorValue *Arg(const EditorRequest &r, const char *key) {
    for (uint32_t i = 0; i < r.count; ++i)
        if (std::string(r.values[i].key.data, r.values[i].key.size) == key)
            return r.values + i;
    return nullptr;
}
uint64_t Number(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? v->integer : 0;
}
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_OK;
}
EditorStatus Result(const EditResult &r, EditorSink sink) {
    Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(r.message)}});
    return r.ok                                ? EDITOR_OK
           : r.Unchanged()                     ? EDITOR_NO_CHANGE
           : r.Status() == EditStatus::Refused ? EDITOR_REFUSED
                                               : EDITOR_ERROR;
}
bool Same(const LinkEndpoint &a, const LinkEndpoint &b) {
    return a.id == b.id && a.type == b.type && a.index == b.index;
}
CKParameterIn *Input(CKContext *context, const LinkEndpoint &endpoint) {
    if (endpoint.type != ENDPOINT_PIN && endpoint.type != ENDPOINT_TARGET_PIN)
        return nullptr;
    auto *owner = context->GetObject(endpoint.id);
    if (auto *behavior = CKBehavior::Cast(owner))
        return endpoint.type == ENDPOINT_TARGET_PIN ? behavior->GetTargetParameter()
               : endpoint.index >= 0 && endpoint.index < behavior->GetInputParameterCount()
                   ? behavior->GetInputParameter(endpoint.index)
                   : nullptr;
    if (auto *operation = CKParameterOperation::Cast(owner))
        return endpoint.index == 0   ? operation->GetInParameter1()
               : endpoint.index == 1 ? operation->GetInParameter2()
                                     : nullptr;
    return nullptr;
}
struct Endpoint {
    ElementRef ref;
    LinkEndpoint endpoint;
    Point position;
    bool behavior = false;
    LinkSource source;
};
bool EndpointOf(Document &doc, const SchematicModel &model, const ElementRef &ref, Endpoint &out) {
    out.ref = ref;
    if (ref.kind == ElementKind::Start) {
        if (!model.start.present)
            return false;
        auto *root = CKBehavior::Cast(doc.Context()->GetObject(model.script.id));
        auto *start = root && root->GetInputCount() ? root->GetInput(0) : nullptr;
        if (!start)
            return false;
        out.endpoint = LinkEndpoint(start->GetID(), 0, ENDPOINT_START_BIN);
        out.position = model.start.center;
        out.behavior = true;
    } else if (ref.kind == ElementKind::Pin) {
        const PinItem *pin = nullptr;
        for (const auto &p : model.pins)
            if (p.ref == ref) {
                pin = &p;
                break;
            }
        if (!pin)
            return false;
        out.endpoint = pin->endpoint;
        out.position = pin->center;
        out.behavior = pin->kind == PinKind::BehaviorIn || pin->kind == PinKind::BehaviorOut;
        out.source.kind = pin->kind;
        out.source.name = pin->name;
        auto *object = doc.Context()->GetObject(ref.object);
        CKParameter *parameter = nullptr;
        if (auto *behavior = CKBehavior::Cast(object))
            if (ref.pin == PinKind::ParamOut && ref.index >= 0 && ref.index < behavior->GetOutputParameterCount())
                parameter = behavior->GetOutputParameter(ref.index);
        if (auto *operation = CKParameterOperation::Cast(object))
            parameter = operation->GetOutParameter();
        if (parameter)
            out.source.type = parameter->GetGUID();
    } else if (ref.kind == ElementKind::LocalParam || ref.kind == ElementKind::Shortcut) {
        const ParamItem *param = nullptr;
        for (const auto &p : model.params)
            if (p.ref == ref) {
                param = &p;
                break;
            }
        if (!param)
            return false;
        out.endpoint =
            LinkEndpoint(param->owner, param->slot, param->shortcut ? ENDPOINT_POUT_SHORTCUT : ENDPOINT_PLOCAL);
        out.position = param->center;
        out.source.kind = PinKind::ParamOut;
        out.source.name = param->name;
        if (auto *parameter = CKParameter::Cast(doc.Context()->GetObject(param->parameter)))
            out.source.type = parameter->GetGUID();
    } else
        return false;
    out.source.endpoint = out.endpoint;
    return true;
}
EditorStatus Resolve(Document &doc, const EditorValue *value, CK_ID script, ElementRef &out) {
    ElementAddress address;
    CK_ID in = 0;
    if (!value || !ReadSchematicPosition(*value, address))
        return EDITOR_INVALID;
    if (!address.Resolve(doc, in, out) || in != script)
        return EDITOR_STALE;
    return EDITOR_OK;
}
std::vector<uint64_t> Signature(const OperationSignature &s) {
    std::vector<uint64_t> result;
    for (const auto &guid : {s.operation, s.result, s.input1, s.input2}) {
        result.push_back(guid.d1);
        result.push_back(guid.d2);
    }
    return result;
}
void LinkPoints(Document &doc, CK_ID script, const LinkEndpoint &a, const LinkEndpoint &b,
                const std::vector<Point> &points, bool route) {
    auto *session = doc.Session(script);
    InterfaceData copy = session->Data();
    copy.observers.clear();
    auto *root = CKBehavior::Cast(doc.Context()->GetObject(script));
    bool found = false;
    auto visit = [&](BehaviorData &record) {
        for (auto &link : record.links) {
            if (found || !link.points.empty())
                continue;
            const bool forward = Same(link.start, a) && Same(link.end, b);
            const bool reverse = Same(link.start, b) && Same(link.end, a);
            if (!forward && !reverse)
                continue;
            if (!points.empty()) {
                link.points = points;
                if (reverse)
                    std::reverse(link.points.begin(), link.points.end());
                found = true;
            } else if (route && RouteSingleLink(copy, record, link, root) && !link.points.empty())
                found = true;
        }
    };
    visit(copy.rootBehavior);
    for (auto &record : copy.behaviors)
        visit(record);
    if (found)
        EditScriptData(doc, script, "Link", [&](InterfaceData &data) { data = copy; });
}
ElementRef UniqueSourceLocal(Document &doc, const SchematicModel &model, CKParameterIn *input) {
    auto *local = input ? CKParameterLocal::Cast(input->GetDirectSource()) : nullptr;
    if (!local)
        return {};
    int uses = 0;
    auto count = [&](CKBehavior *b) {
        for (int i = 0; b && i < b->GetInputParameterCount(); ++i)
            if (b->GetInputParameter(i) && b->GetInputParameter(i)->GetDirectSource() == local)
                ++uses;
        if (b && b->GetTargetParameter() && b->GetTargetParameter()->GetDirectSource() == local)
            ++uses;
    };
    if (auto *graph = CKBehavior::Cast(local->GetOwner())) {
        for (int i = 0; i < graph->GetSubBehaviorCount(); ++i)
            count(graph->GetSubBehavior(i));
        for (int i = 0; i < graph->GetParameterOperationCount(); ++i)
            if (auto *op = graph->GetParameterOperation(i)) {
                if (op->GetInParameter1() && op->GetInParameter1()->GetDirectSource() == local)
                    ++uses;
                if (op->GetInParameter2() && op->GetInParameter2()->GetDirectSource() == local)
                    ++uses;
            }
    }
    if (uses == 1)
        for (const auto &p : model.params)
            if (p.ref.kind == ElementKind::LocalParam && p.ref.object == local->GetID())
                return p.ref;
    return {};
}
struct Connection {
    CK_ID script = 0;
    SchematicModel model;
    std::vector<Endpoint> sources;
    Endpoint from, to;
    ElementRef destination, link;
    bool toEndpoint = false, relink = false;
    unsigned mode = 0;
    std::vector<Point> points;
};
EditorStatus Prepare(Document *doc, const EditorRequest &r, Connection &c) {
    const auto *target = Arg(r, "target");
    if (!doc || !doc->IsOpen() || doc->Token() != r.scope.document || !target)
        return EDITOR_STALE;
    c.script = doc->NativeId(target->object);
    auto *session = c.script ? doc->Session(c.script) : nullptr;
    if (!session || !doc->FindScript(c.script))
        return EDITOR_STALE;
    if (session->ReadOnly())
        return EDITOR_REFUSED;
    if (session->Revision() != Number(r, "revision") || doc->TopologyRevision() != Number(r, "topology"))
        return EDITOR_STALE;
    for (const char *key : {"alt", "reverse", "route", "auto-loop", "replace"})
        if (Number(r, key) > 1)
            return EDITOR_INVALID;
    if (Number(r, "mode") > 2 || Number(r, "delay") > INT_MAX)
        return EDITOR_INVALID;
    c.mode = unsigned(Number(r, "mode"));
    c.model.Build(*session, doc->Context(), {}, true);
    if (const auto *link = Arg(r, "link"); link && link->fields.count) {
        auto status = Resolve(*doc, link, c.script, c.link);
        if (status != EDITOR_OK)
            return status;
        if (c.link.kind != ElementKind::Link)
            return EDITOR_INVALID;
        const auto *record = FindRecord(session->Data(), c.link.owner);
        const auto &stored = record->links[size_t(c.link.index)];
        c.from.endpoint = Number(r, "reverse") ? stored.end : stored.start;
        c.to.endpoint = Number(r, "reverse") ? stored.start : stored.end;
        c.from.behavior = c.to.behavior = stored.IsBehaviorLink();
        c.relink = c.mode == 0;
        // Keep the actual pin/local lifetimes as well as the indexed link.
        // Re-reading a raw endpoint ID at dispatch could bind a replacement.
        ElementRef sourceRef;
        status = Resolve(*doc, Arg(r, "from"), c.script, sourceRef);
        if (status != EDITOR_OK)
            return status;
        Endpoint source;
        if (!EndpointOf(*doc, c.model, sourceRef, source) || !Same(source.endpoint, c.from.endpoint))
            return EDITOR_STALE;
        c.from = source;
        if (c.mode) {
            ElementRef targetRef;
            status = Resolve(*doc, Arg(r, "to"), c.script, targetRef);
            if (status != EDITOR_OK)
                return status;
            Endpoint target;
            if (!EndpointOf(*doc, c.model, targetRef, target) || !Same(target.endpoint, c.to.endpoint))
                return EDITOR_STALE;
            c.to = target;
        }
    } else if (c.mode)
        return EDITOR_INVALID;
    if (!c.mode) {
        auto status = Resolve(*doc, Arg(r, "to"), c.script, c.destination);
        if (status != EDITOR_OK)
            return status;
        c.toEndpoint = EndpointOf(*doc, c.model, c.destination, c.to);
        if (!c.toEndpoint && c.destination.kind != ElementKind::Behavior)
            return EDITOR_INVALID;
        if (!c.relink) {
            ElementRef ref;
            status = Resolve(*doc, Arg(r, "from"), c.script, ref);
            if (status != EDITOR_OK)
                return status;
            if (!EndpointOf(*doc, c.model, ref, c.from))
                return EDITOR_INVALID;
            std::set<ElementRef> unique;
            if (const auto *selected = Arg(r, "selected"))
                for (uint32_t i = 0; i < selected->fields.count; ++i) {
                    ElementRef item;
                    status = Resolve(*doc, selected->fields.data + i, c.script, item);
                    if (status != EDITOR_OK)
                        return status;
                    if (!unique.insert(item).second)
                        return EDITOR_INVALID;
                    Endpoint source;
                    if (item.kind == ref.kind && (item.kind != ElementKind::Pin || item.pin == ref.pin) &&
                        EndpointOf(*doc, c.model, item, source) && source.behavior == c.from.behavior)
                        c.sources.push_back(source);
                }
            if (c.sources.empty())
                c.sources.push_back(c.from);
        } else
            c.sources.push_back(c.from);
        if (c.toEndpoint && c.to.behavior != c.from.behavior)
            return EDITOR_REFUSED;
    }
    if (const auto *points = Arg(r, "points")) {
        if (points->reals.count % 2)
            return EDITOR_INVALID;
        for (uint32_t i = 0; i < points->reals.count; i += 2) {
            const double x = points->reals.data[i], y = points->reals.data[i + 1];
            if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > std::numeric_limits<float>::max() ||
                std::abs(y) > std::numeric_limits<float>::max())
                return EDITOR_INVALID;
            c.points.emplace_back(float(x), float(y));
        }
    }
    return EDITOR_OK;
}
std::vector<OperationSignature> Choices(Document &doc, const Connection &c) {
    if (c.from.behavior || (!c.mode && (!c.toEndpoint || c.sources.size() > 1)))
        return {};
    return c.mode == 2 ? ParameterLinkOperations(doc, c.script, c.from.endpoint, c.to.endpoint)
                       : ParameterLinkConversions(doc, c.script, c.from.endpoint, c.to.endpoint);
}
} // namespace
struct SchematicConnections::Data {
    std::function<Document *()> current;
    explicit Data(std::function<Document *()> current) : current(std::move(current)) {}
    static EditorStatus Read(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->current();
        Connection c;
        const auto status = Prepare(doc, *r, c);
        if (status != EDITOR_OK)
            return status;
        auto *input = c.toEndpoint ? Input(doc->Context(), c.to.endpoint) : nullptr;
        if (!input && !c.from.behavior)
            input = Input(doc->Context(), c.from.endpoint);
        std::vector<EditorField> result{
            {"replace",
             EDITOR_INTEGER_VALUE,
             {},
             {},
             uint64_t(!c.relink && !c.mode && c.sources.size() == 1 && input &&
                      (input->GetDirectSource() || input->GetSharedSource()))},
            {"input-name", EDITOR_TEXT_VALUE, {}, input ? AnsiToUtf8(input->GetName() ? input->GetName() : "") : ""},
            {"multiple", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(c.sources.size() > 1 || !c.toEndpoint)},
            {"choices", EDITOR_RECORD}};
        for (const auto &signature : Choices(*doc, c)) {
            EditorField row{std::to_string(result.back().fields.size()), EDITOR_RECORD},
                value{"signature", EDITOR_INTEGER_LIST};
            value.integers = Signature(signature);
            row.fields = {{"label", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(signature.label)}, std::move(value)};
            result.back().fields.push_back(std::move(row));
        }
        return Write(sink, result);
    }
    static EditorStatus Run(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->current();
        Connection c;
        const auto status = Prepare(doc, *r, c);
        if (status != EDITOR_OK)
            return status;
        const auto choices = Choices(*doc, c);
        const OperationSignature *chosen = nullptr;
        if (const auto *signature = Arg(*r, "signature"); signature && signature->integers.count) {
            if (signature->integers.count != 8)
                return EDITOR_INVALID;
            const std::vector<uint64_t> requested(signature->integers.data, signature->integers.data + 8);
            for (const auto &s : choices)
                if (Signature(s) == requested) {
                    chosen = &s;
                    break;
                }
            if (!chosen)
                return EDITOR_STALE;
        }
        if (c.mode) {
            if (!chosen)
                return EDITOR_INVALID;
            return Result(c.mode == 1
                              ? ReplaceLinkConversion(*doc, c.script, c.link, c.from.endpoint, c.to.endpoint, *chosen)
                              : InsertOperationInLink(*doc, c.script, c.link, c.from.endpoint, c.to.endpoint, *chosen),
                          sink);
        }
        if (!c.toEndpoint) {
            if (c.relink)
                return EDITOR_REFUSED;
            std::stable_sort(c.sources.begin(), c.sources.end(), [&](const Endpoint &a, const Endpoint &b) {
                return c.from.behavior
                           ? (a.position.v != b.position.v ? a.position.v < b.position.v : a.position.h < b.position.h)
                           : (a.position.h != b.position.h ? a.position.h < b.position.h : a.position.v < b.position.v);
            });
            std::vector<LinkSource> sources;
            for (const auto &s : c.sources)
                sources.push_back(s.source);
            bool refused = false;
            std::string message;
            const int made = LinkToNewPins(*doc, c.script, sources, c.destination.object, c.from.behavior,
                                           int(Number(*r, "delay")), Number(*r, "auto-loop") != 0, &refused, &message);
            if (refused)
                message = "The Building Block cannot create that kind of pin";
            if (made) {
                Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(message)}});
                return EDITOR_OK;
            }
            return Result(message.empty() ? EditResult::NoChange() : EditResult::Refused(message), sink);
        }
        if (c.sources.size() > 1 && !c.relink) {
            std::stable_sort(c.sources.begin(), c.sources.end(), [&](const Endpoint &a, const Endpoint &b) {
                return c.from.behavior
                           ? (a.position.v != b.position.v ? a.position.v < b.position.v : a.position.h < b.position.h)
                           : (a.position.h != b.position.h ? a.position.h < b.position.h : a.position.v < b.position.v);
            });
            size_t pressed = 0;
            for (size_t i = 0; i < c.sources.size(); ++i)
                if (Same(c.sources[i].endpoint, c.from.endpoint))
                    pressed = i;
            std::vector<std::pair<LinkEndpoint, LinkEndpoint>> pairs;
            if (Number(*r, "alt"))
                for (const auto &s : c.sources)
                    pairs.emplace_back(s.endpoint, c.to.endpoint);
            else {
                if (c.destination.kind != ElementKind::Pin)
                    return EDITOR_REFUSED;
                std::vector<const PinItem *> targets;
                for (const auto &p : c.model.pins)
                    if (p.owner == c.destination.object && p.kind == c.destination.pin)
                        targets.push_back(&p);
                std::sort(targets.begin(), targets.end(),
                          [](const auto *a, const auto *b) { return a->index < b->index; });
                int dropped = -1;
                for (size_t i = 0; i < targets.size(); ++i)
                    if (targets[i]->ref == c.destination)
                        dropped = int(i);
                if (dropped < 0)
                    return EDITOR_STALE;
                for (size_t i = 0; i < c.sources.size(); ++i) {
                    const int k = dropped + int(i) - int(pressed);
                    if (k >= 0 && size_t(k) < targets.size())
                        pairs.emplace_back(c.sources[i].endpoint, targets[size_t(k)]->endpoint);
                }
            }
            const auto made = LinkPairs(*doc, c.script, pairs, c.from.behavior, int(Number(*r, "delay")),
                                        Number(*r, "auto-loop") != 0);
            return Result(made ? EditResult::Applied() : EditResult::Refused("None of these links is allowed"), sink);
        }
        auto *input = Input(doc->Context(), c.to.endpoint);
        if (!input)
            input = Input(doc->Context(), c.from.endpoint);
        if (!c.relink && input && (input->GetDirectSource() || input->GetSharedSource()) && !Number(*r, "replace"))
            return EDITOR_REFUSED;
        const auto local = Number(*r, "replace") ? UniqueSourceLocal(*doc, c.model, input) : ElementRef{};
        Edit edit(*doc, c.relink ? "Move Link" : "Link");
        if (c.relink) {
            const auto removed = DeleteElements(*doc, c.script, {c.link});
            if (!removed.ok)
                return Result(removed, sink);
        }
        auto made = c.from.behavior ? CreateBehaviorLink(*doc, c.script, c.from.endpoint, c.to.endpoint,
                                                         int(Number(*r, "delay")), Number(*r, "auto-loop") != 0)
                                    : CreateParameterLink(*doc, c.script, c.from.endpoint, c.to.endpoint, chosen);
        if (!made.ok && !c.relink && !c.from.behavior && !chosen && choices.size() == 1)
            made = CreateParameterLink(*doc, c.script, c.from.endpoint, c.to.endpoint, &choices.front());
        if (!made.ok)
            return Result(made, sink);
        if (local.IsValid()) {
            const auto removed = DeleteElements(*doc, c.script, {local});
            if (!removed.ok)
                return Result(removed, sink);
        }
        if (!c.relink)
            LinkPoints(*doc, c.script, c.from.endpoint, c.to.endpoint, c.points, Number(*r, "route") != 0);
        return Result(edit.Commit(), sink);
    }
    static EditorStatus Route(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->current();
        const auto *target = Arg(*r, "target");
        if (!doc || !target || doc->Token() != r->scope.document)
            return EDITOR_STALE;
        const auto script = doc->NativeId(target->object);
        auto *session = script ? doc->Session(script) : nullptr;
        if (!session || session->Revision() != Number(*r, "revision") ||
            doc->TopologyRevision() != Number(*r, "topology"))
            return EDITOR_STALE;
        if (session->ReadOnly())
            return EDITOR_REFUSED;
        const auto *elements = Arg(*r, "elements");
        std::vector<ElementRef> links;
        std::set<ElementRef> unique;
        for (uint32_t i = 0; elements && i < elements->fields.count; ++i) {
            ElementRef ref;
            const auto status = Resolve(*doc, elements->fields.data + i, script, ref);
            if (status != EDITOR_OK)
                return status;
            if (ref.kind != ElementKind::Link || !unique.insert(ref).second)
                return EDITOR_INVALID;
            links.push_back(ref);
        }
        if (links.empty())
            return EDITOR_NO_CHANGE;
        InterfaceData after = session->Data();
        after.observers.clear();
        bool changed = false;
        size_t failed = 0;
        auto *root = CKBehavior::Cast(doc->Context()->GetObject(script));
        for (const auto &ref : links) {
            auto *record = FindRecord(after, ref.owner);
            auto &link = record->links[size_t(ref.index)];
            if (!link.points.empty()) {
                link.points.clear();
                changed = true;
            } else if (!RouteSingleLink(after, *record, link, root))
                ++failed;
            else
                changed |= !link.points.empty();
        }
        const auto message = failed ? "P: no path found for " + std::to_string(failed) + " link(s)" : std::string();
        if (!changed)
            return Result(EditResult::NoChange(message), sink);
        Edit edit(*doc, "Update Link Path", Edit::Marks::Scripts);
        if (!edit.ShowScript(script, after))
            return Result(edit.Fail("Cannot edit the Script display"), sink);
        return Result(edit.Commit(message), sink);
    }
    static EditorStatus InsertPoint(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->current();
        const auto *target = Arg(*r, "target");
        if (!doc || !target || doc->Token() != r->scope.document)
            return EDITOR_STALE;
        const auto script = doc->NativeId(target->object);
        auto *session = script ? doc->Session(script) : nullptr;
        if (!session || session->Revision() != Number(*r, "revision") ||
            doc->TopologyRevision() != Number(*r, "topology"))
            return EDITOR_STALE;
        if (session->ReadOnly())
            return EDITOR_REFUSED;
        ElementRef ref;
        const auto status = Resolve(*doc, Arg(*r, "link"), script, ref);
        if (status != EDITOR_OK)
            return status;
        if (ref.kind != ElementKind::Link)
            return EDITOR_INVALID;
        const auto index = Number(*r, "index");
        const auto *at = Arg(*r, "at");
        if (!at || at->reals.count != 2 || index > INT_MAX ||
            index > FindRecord(session->Data(), ref.owner)->links[size_t(ref.index)].points.size())
            return EDITOR_INVALID;
        for (uint32_t i = 0; i < 2; ++i)
            if (!std::isfinite(at->reals.data[i]) || std::abs(at->reals.data[i]) > 100000000)
                return EDITOR_INVALID;
        const Point point(SchematicGrid::SnapToGrid(float(at->reals.data[0]), 20),
                          SchematicGrid::SnapToGrid(float(at->reals.data[1]), 20));
        const bool changed = EditScriptData(*doc, script, "Insert Control Point", [&](InterfaceData &data) {
            FindRecord(data, ref.owner)->links[size_t(ref.index)].InsertControlPoint(int(index), point);
        });
        return Result(changed ? EditResult::Applied() : EditResult::NoChange(), sink);
    }
    static EditorStatus Export(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->current();
        const auto *target = Arg(*r, "target");
        if (!doc || !target || doc->Token() != r->scope.document)
            return EDITOR_STALE;
        const auto script = doc->NativeId(target->object);
        auto *session = script ? doc->Session(script) : nullptr;
        if (!session || session->Revision() != Number(*r, "revision") ||
            doc->TopologyRevision() != Number(*r, "topology"))
            return EDITOR_STALE;
        if (session->ReadOnly())
            return EDITOR_REFUSED;
        const auto *elements = Arg(*r, "elements"), *at = Arg(*r, "at");
        if (!at || at->reals.count != 2)
            return EDITOR_INVALID;
        for (uint32_t i = 0; i < 2; ++i)
            if (std::abs(at->reals.data[i]) > std::numeric_limits<float>::max())
                return EDITOR_INVALID;
        SchematicModel model;
        model.Build(*session, doc->Context(), {}, true);
        std::vector<PinItem> pins;
        std::set<ElementRef> unique;
        for (uint32_t i = 0; elements && i < elements->fields.count; ++i) {
            ElementRef ref;
            const auto status = Resolve(*doc, elements->fields.data + i, script, ref);
            if (status != EDITOR_OK)
                return status;
            if (ref.kind != ElementKind::Pin || !unique.insert(ref).second)
                return EDITOR_INVALID;
            bool found = false;
            for (const auto &pin : model.pins)
                if (pin.ref == ref && !pin.onOperation && pin.kind != PinKind::Target) {
                    pins.push_back(pin);
                    found = true;
                    break;
                }
            if (!found)
                return EDITOR_REFUSED;
        }
        if (pins.empty())
            return EDITOR_NO_CHANGE;
        const auto refused =
            ExportPinsAt(*doc, script, pins, Point(float(at->reals.data[0]), float(at->reals.data[1])));
        std::string message;
        for (const auto &r : refused) {
            if (!message.empty())
                message += "\n";
            message += "Export: " + r.message;
        }
        Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(message)}});
        // ExportPinsAt intentionally keeps permitted pins from a mixed selection.
        return EDITOR_OK;
    }
};
SchematicConnections::SchematicConnections(std::function<Document *()> current)
    : m_Data(std::make_unique<Data>(std::move(current))) {}
SchematicConnections::~SchematicConnections() = default;
bool SchematicConnections::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter args[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                    {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("from"), EDITOR_RECORD, 1},
                                    {EDITOR_TEXT("to"), EDITOR_RECORD, 1},
                                    {EDITOR_TEXT("selected"), EDITOR_RECORD, 1},
                                    {EDITOR_TEXT("link"), EDITOR_RECORD, 1},
                                    {EDITOR_TEXT("reverse"), EDITOR_INTEGER_VALUE, 1},
                                    {EDITOR_TEXT("alt"), EDITOR_INTEGER_VALUE, 1},
                                    {EDITOR_TEXT("delay"), EDITOR_INTEGER_VALUE, 1},
                                    {EDITOR_TEXT("auto-loop"), EDITOR_INTEGER_VALUE, 1},
                                    {EDITOR_TEXT("route"), EDITOR_INTEGER_VALUE, 1},
                                    {EDITOR_TEXT("points"), EDITOR_REAL_LIST, 1},
                                    {EDITOR_TEXT("replace"), EDITOR_INTEGER_VALUE, 1},
                                    {EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE, 1},
                                    {EDITOR_TEXT("signature"), EDITOR_INTEGER_LIST, 1}};
    const EditorParameter fields[] = {{EDITOR_TEXT("replace"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("input-name"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("multiple"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("choices"), EDITOR_RECORD}};
    const EditorQueryDesc query{sizeof(query),
                                EDITOR_TEXT("schematic.connection"),
                                EDITOR_TEXT("builtin.schematic-connections"),
                                args,
                                16,
                                Data::Read,
                                m_Data.get(),
                                EDITOR_QUERY_EACH_READ,
                                EDITOR_DOCUMENT,
                                fields,
                                4};
    const EditorParameter exportArgs[] = {
        args[0], args[1], args[2], {EDITOR_TEXT("elements"), EDITOR_RECORD}, {EDITOR_TEXT("at"), EDITOR_REAL_LIST}};
    const EditorParameter routeArgs[] = {args[0], args[1], args[2], {EDITOR_TEXT("elements"), EDITOR_RECORD}};
    const EditorParameter pointArgs[] = {args[0],
                                         args[1],
                                         args[2],
                                         {EDITOR_TEXT("link"), EDITOR_RECORD},
                                         {EDITOR_TEXT("at"), EDITOR_REAL_LIST},
                                         {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.route-links"), EDITOR_TEXT("builtin.schematic-connections"),
         EDITOR_TEXT("Update Link Path"), routeArgs, 4, Data::Route, m_Data.get(), EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.insert-control-point"),
         EDITOR_TEXT("builtin.schematic-connections"), EDITOR_TEXT("Insert Control Point"), pointArgs, 6,
         Data::InsertPoint, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.connect"), EDITOR_TEXT("builtin.schematic-connections"),
         EDITOR_TEXT("Link"), args, 16, Data::Run, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.export-pins"), EDITOR_TEXT("builtin.schematic-connections"),
         EDITOR_TEXT("Export"), exportArgs, 5, Data::Export, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("schematic.connections");
    module.queries = &query;
    module.queryCount = 1;
    module.commands = commands;
    module.commandCount = 4;
    return editor.Register(module, error);
}
} // namespace vdev

#include "features/schematic/SchematicGeometry.h"
#include "features/schematic/SchematicPosition.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ExportCommands.h"
#include "core/runtime/TextEncoding.h"
#include "core/schematic/SchematicModelBuilder.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/LinkRouting.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace vdev {
namespace {
const EditorValue *Arg(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key)
            return request.values + i;
    return nullptr;
}
uint64_t Number(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? v->integer : 0;
}
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto v = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, v.data(), uint32_t(v.size())) : EDITOR_INVALID;
}
EditorStatus Result(const EditResult &r, EditorSink sink, bool selectionUnchanged = true) {
    if (sink.write)
        Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(r.message)},
                     {"selection-unchanged", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(selectionUnchanged)}});
    return r.ok                                ? EDITOR_OK
           : r.Unchanged()                     ? EDITOR_NO_CHANGE
           : r.Status() == EditStatus::Refused ? EDITOR_REFUSED
                                               : EDITOR_ERROR;
}
bool Floats(const EditorValue *value, uint32_t count, std::vector<float> &out) {
    if (!value || value->type != EDITOR_REAL_LIST || value->reals.count != count)
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        const auto n = value->reals.data[i];
        if (!std::isfinite(n) || std::abs(n) > 100000000)
            return false;
        out.push_back(float(n));
    }
    return true;
}
bool Movable(ElementKind kind) {
    return kind == ElementKind::Behavior || kind == ElementKind::Operation || kind == ElementKind::LocalParam ||
           kind == ElementKind::Shortcut || kind == ElementKind::Comment || kind == ElementKind::ControlPoint ||
           kind == ElementKind::Start || kind == ElementKind::Link;
}
struct Geometry {
    DocumentToken document{};
    ObjectRef script{};
    uint64_t revision = 0, topology = 0, mode = 0;
    std::vector<ElementRef> elements;
    ElementRef primary;
    std::vector<ElementAddress> addresses;
    InterfaceData before;
    Rect size;
    std::vector<std::pair<Rect, Rect>> limits;
    bool rootIsScript = false, autoResize = false;
};
EditorStatus Prepare(Document *doc, const EditorRequest &request, Geometry &g, bool versions, bool capture = true) {
    const auto *target = Arg(request, "target");
    if (!doc || !doc->IsOpen() || doc->Token() != request.scope.document || !target)
        return EDITOR_STALE;
    auto *root = CKBehavior::Cast(doc->Resolve(target->object));
    auto *session = root ? doc->Session(root->GetID()) : nullptr;
    if (!session || !doc->FindScript(root->GetID()))
        return EDITOR_STALE;
    if (session->ReadOnly())
        return EDITOR_REFUSED;
    if (versions &&
        (Number(request, "revision") != session->Revision() || Number(request, "topology") != doc->TopologyRevision()))
        return EDITOR_STALE;
    g.document = doc->Token();
    g.script = target->object;
    g.revision = session->Revision();
    g.topology = doc->TopologyRevision();
    g.mode = Number(request, "mode");
    if (g.mode > 3 || Number(request, "coherence") > 1 || Number(request, "auto-resize") > 1)
        return EDITOR_INVALID;
    g.autoResize = Number(request, "auto-resize") != 0;
    g.rootIsScript = (root->GetType() & CKBEHAVIORTYPE_SCRIPT) != 0;
    const auto *elements = Arg(request, "elements");
    std::set<ElementRef> unique;
    if (elements)
        for (uint32_t i = 0; i < elements->fields.count; ++i) {
            ElementAddress address;
            ElementRef ref;
            uint32_t script = 0;
            if (!ReadSchematicPosition(elements->fields.data[i], address))
                return EDITOR_INVALID;
            if (!address.Resolve(*doc, script, ref) || script != root->GetID())
                return EDITOR_STALE;
            if (!Movable(ref.kind) || !unique.insert(ref).second)
                return EDITOR_INVALID;
            g.elements.push_back(ref);
            if (capture)
                g.addresses.push_back(address);
        }
    if (g.mode != 1 && g.elements.empty())
        return EDITOR_INVALID;
    if (g.mode == 0) {
        const auto *primary = Arg(request, "primary");
        ElementAddress address;
        uint32_t script = 0;
        if (!primary || !ReadSchematicPosition(*primary, address))
            return EDITOR_INVALID;
        if (!address.Resolve(*doc, script, g.primary) || script != root->GetID())
            return EDITOR_STALE;
        if (!unique.count(g.primary))
            return EDITOR_INVALID;
    }
    if ((g.mode == 2 && (g.elements.size() != 1 || g.elements[0].kind != ElementKind::Behavior)) ||
        (g.mode == 3 && (g.elements.size() != 1 || g.elements[0].kind != ElementKind::Comment)))
        return EDITOR_INVALID;
    if (!capture)
        return EDITOR_OK;
    g.before = session->Data();
    g.before.observers.clear();
    SchematicModel model;
    model = BuildSchematicModel(*session, doc->Context(), {}, true);
    if (g.mode == 1)
        g.size = Rect(0, 0, 0, g.before.header.vSize);
    if (g.mode == 2) {
        const auto *item = model.FindBehavior(g.elements[0].object);
        if (!item || !item->graph || item->folded)
            return EDITOR_INVALID;
        g.size = item->box;
    }
    if (g.mode == 3)
        for (const auto &item : model.comments)
            if (item.ref == g.elements[0])
                g.size = item.bounds;
    if (g.mode != 0 || !Number(request, "coherence"))
        return EDITOR_OK;
    auto moved = [&](CK_ID id) {
        return std::any_of(g.elements.begin(), g.elements.end(),
                           [&](const ElementRef &r) { return r.kind == ElementKind::Behavior && r.object == id; });
    };
    auto container = [&](CK_ID owner, Rect &box) {
        std::set<CK_ID> visited;
        for (auto id = owner; id && id != root->GetID() && visited.insert(id).second;) {
            const auto *graph = model.FindBehavior(id);
            if (!graph || moved(id))
                return false;
            if (graph->graph && !graph->folded) {
                box = graph->box;
                return true;
            }
            id = graph->parent;
        }
        return false;
    };
    for (const auto &ref : g.elements) {
        Rect item, box;
        CK_ID owner = 0;
        bool found = false;
        if (ref.kind == ElementKind::Behavior)
            if (const auto *b = model.FindBehavior(ref.object)) {
                item = b->box;
                owner = b->parent;
                found = true;
            }
        for (const auto &p : model.params)
            if (!found && p.ref == ref) {
                item = p.bounds;
                owner = p.owner;
                found = true;
            }
        for (const auto &p : model.operations)
            if (!found && p.ref == ref) {
                item = p.bounds;
                owner = p.owner;
                found = true;
            }
        for (const auto &p : model.comments)
            if (!found && p.ref == ref) {
                item = p.bounds;
                owner = p.owner;
                found = true;
            }
        if (found && container(owner, box))
            g.limits.push_back({item, box});
    }
    return EDITOR_OK;
}
EditorStatus Apply(const Geometry &g, const EditorValue &value, InterfaceData &after, Point &drop, bool &hasDrop,
                   bool &route, bool &changed, bool validateOnly = false) {
    if (value.type != EDITOR_RECORD)
        return EDITOR_INVALID;
    const EditorRequest r{{}, value.fields.data, value.fields.count};
    for (uint32_t i = 0; i < r.count; ++i) {
        const auto &v = r.values[i];
        const std::string key(v.key.data, v.key.size);
        if ((key == "delta" || key == "size" || key == "drop") ? v.type != EDITOR_REAL_LIST
            : key == "route"                                   ? v.type != EDITOR_INTEGER_VALUE
                                                               : true)
            return EDITOR_INVALID;
    }
    changed = false;
    route = Number(r, "route") != 0;
    if (Number(r, "route") > 1)
        return EDITOR_INVALID;
    hasDrop = false;
    if (const auto *point = Arg(r, "drop"); point && point->reals.count) {
        std::vector<float> xy;
        if (!Floats(point, 2, xy))
            return EDITOR_INVALID;
        drop = Point(xy[0], xy[1]);
        hasDrop = true;
    }
    std::vector<float> v;
    if (!Floats(Arg(r, g.mode == 0 ? "delta" : "size"), 2, v))
        return EDITOR_INVALID;
    if (validateOnly)
        return EDITOR_OK;
    after = g.before;
    if (g.mode == 0) {
        Point delta(v[0], v[1]);
        for (const auto &limit : g.limits) {
            const auto &item = limit.first, &box = limit.second;
            const float minH = box.hPos + 20 - item.hPos, maxH = box.Right() - 20 - item.Right(),
                        minV = box.vPos + 20 - item.vPos, maxV = box.Bottom() - 20 - item.Bottom();
            delta.h = minH > maxH ? 0 : std::clamp(delta.h, std::min(0.f, minH), std::max(0.f, maxH));
            delta.v = minV > maxV ? 0 : std::clamp(delta.v, std::min(0.f, minV), std::max(0.f, maxV));
        }
        changed = MoveElements(after, g.before, g.rootIsScript, g.elements, g.primary, delta);
    } else {
        const float minimum = g.mode == 2 ? 60.f : g.mode == 1 ? MIN_OPEN_SCRIPT_HEIGHT : 20.f;
        const float width = std::max(minimum, std::round(v[0] / 20.f) * 20.f),
                    height = std::max(minimum, std::round(v[1] / 20.f) * 20.f);
        if (g.mode == 1) {
            changed = after.header.vSize != height;
            after.header.vSize = height;
        } else if (g.mode == 2) {
            auto *b = FindRecord(after, g.elements[0].object);
            changed = width != g.size.hSize || height != g.size.vSize;
            b->hExpandSize += width - g.size.hSize;
            b->vExpandSize += height - g.size.vSize;
        } else {
            const auto &ref = g.elements[0];
            auto &comment = FindRecord(after, ref.owner)->comments[size_t(ref.index)];
            auto box = comment.GetRect();
            changed = width != box.hSize || height != box.vSize;
            box.hSize = width;
            box.vSize = height;
            comment.SetRect(box);
        }
    }
    return EDITOR_OK;
}
} // namespace
bool CurrentGeometry(Document *doc, const Geometry &g) {
    if (!doc || doc->Token() != g.document || !doc->Resolve(g.script) || doc->TopologyRevision() != g.topology)
        return false;
    const auto *session = doc->Session(g.script.object);
    if (!session || session->Revision() != g.revision)
        return false;
    for (const auto &address : g.addresses) {
        uint32_t script = 0;
        ElementRef ref;
        if (!address.Resolve(*doc, script, ref) || script != g.script.object)
            return false;
    }
    return true;
}
struct SchematicGeometry::Data {
    std::function<Document *()> current;
    explicit Data(std::function<Document *()> current) : current(std::move(current)) {}
    static EditorStatus Query(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        Geometry g;
        const auto status = Prepare(self.current(), *r, g, false);
        if (status != EDITOR_OK)
            return status;
        std::vector<EditorField> fields;
        for (const char *key : {"target", "mode", "elements", "primary", "coherence", "auto-resize"}) {
            if (const auto *v = Arg(*r, key)) {
                std::vector<EditorField> one;
                if (CopyEditorFields(one, v, 1) != EDITOR_OK)
                    return EDITOR_INVALID;
                fields.push_back(std::move(one.front()));
            } else
                fields.push_back({key, std::string(key) == "elements" || std::string(key) == "primary"
                                           ? EDITOR_RECORD
                                           : EDITOR_INTEGER_VALUE});
        }
        fields.push_back({"revision", EDITOR_INTEGER_VALUE, {}, {}, g.revision});
        fields.push_back({"topology", EDITOR_INTEGER_VALUE, {}, {}, g.topology});
        EditorField value{"value", EDITOR_RECORD}, delta{"delta", EDITOR_REAL_LIST}, size{"size", EDITOR_REAL_LIST};
        delta.reals = {0, 0};
        size.reals = {g.size.hSize, g.size.vSize};
        value.fields = {std::move(delta), std::move(size), {"route", EDITOR_INTEGER_VALUE}};
        fields.push_back(std::move(value));
        return Write(sink, fields);
    }
    static EditorStatus Check(void *data, const EditorHost *, const EditorRequest *r) {
        Geometry g;
        const auto status = Prepare(static_cast<Data *>(data)->current(), *r, g, true, false);
        if (status != EDITOR_OK)
            return status;
        const auto *value = Arg(*r, "value");
        if (!value)
            return EDITOR_INVALID;
        InterfaceData after;
        Point drop;
        bool hasDrop, route, changed;
        return Apply(g, *value, after, drop, hasDrop, route, changed, true);
    }
    static EditorStatus Preview(void *data, const EditorHost *, const EditorRequest *r, EditorPreviewAction action,
                                void **state) {
        auto *doc = static_cast<Data *>(data)->current();
        if (action == EDITOR_PREVIEW_CANCEL || action == EDITOR_PREVIEW_RELEASE) {
            std::unique_ptr<Geometry> g(static_cast<Geometry *>(*state));
            *state = nullptr;
            if (!g)
                return EDITOR_OK;
            if (!doc || doc->Token() != g->document || !doc->Resolve(g->script))
                return EDITOR_STALE;
            if (auto *s = doc->Session(g->script.object))
                s->ClearDisplayPreview();
            return EDITOR_OK;
        }
        if (action != EDITOR_PREVIEW_UPDATE)
            return EDITOR_INVALID;
        if (!*state) {
            auto g = std::make_unique<Geometry>();
            const auto status = Prepare(doc, *r, *g, true);
            if (status != EDITOR_OK)
                return status;
            *state = g.release();
        }
        auto &g = *static_cast<Geometry *>(*state);
        if (!CurrentGeometry(doc, g))
            return EDITOR_STALE;
        auto *session = doc->Session(g.script.object);
        const auto *value = Arg(*r, "value");
        if (!value)
            return EDITOR_INVALID;
        InterfaceData after;
        Point drop;
        bool hasDrop, route, changed;
        const auto status = Apply(g, *value, after, drop, hasDrop, route, changed);
        if (status != EDITOR_OK)
            return status;
        return session->PreviewDisplay(std::move(after)) ? EDITOR_OK : EDITOR_ERROR;
    }
    EditorStatus Commit(const EditorRequest &r, const Geometry &g, EditorSink sink) {
        auto *doc = current();
        if (!CurrentGeometry(doc, g))
            return EDITOR_STALE;
        auto *session = doc->Session(g.script.object);
        const auto *value = Arg(r, "value");
        if (!value)
            return EDITOR_INVALID;
        InterfaceData after;
        Point drop;
        bool hasDrop, route, changed;
        const auto status = Apply(g, *value, after, drop, hasDrop, route, changed);
        if (status != EDITOR_OK)
            return status;
        session->ClearDisplayPreview();
        if (!changed)
            return EDITOR_NO_CHANGE;
        std::string message;
        SchematicModel model;
        model = BuildSchematicModel(after, g.script.object, doc->Context(), {}, true);
        if (g.mode == 0 && hasDrop && !g.elements.empty()) {
            const auto graph = g.elements.front().owner,
                       into = model.GraphAt(drop) ? model.GraphAt(drop) : g.script.object;
            const bool locals = std::all_of(g.elements.begin(), g.elements.end(), [&](const auto &e) {
                return e.kind == ElementKind::LocalParam && e.owner == graph;
            });
            if (locals && into && into != graph) {
                std::vector<std::pair<CK_ID, Point>> positions;
                const auto *record = FindRecord(after, graph);
                for (const auto &e : g.elements) {
                    const auto &cell = record->localParams[size_t(e.index)];
                    positions.push_back({e.object, Point(record->rect.hPos + cell.hPos * 20 + 1,
                                                         record->rect.vPos + cell.vPos * 20 + 1)});
                }
                size_t steps = 0;
                std::string refusal;
                MoveLocalsAcrossGraphs(*doc, g.script.object, graph, positions, into, steps, refusal);
                if (steps)
                    return Result(EditResult::Applied(), sink, false);
                // A refused export keeps the ordinary move, as before.
                std::set<CK_ID> visited;
                for (auto at = graph; at && visited.insert(at).second;) {
                    if (at == into) {
                        message = "Export local parameter: " + refusal;
                        break;
                    }
                    const auto *parent = FindRecord(after, at);
                    at = parent ? parent->parentId : 0;
                }
            }
        }
        if (route && g.mode == 0) {
            std::set<CK_ID> moved;
            for (const auto &e : g.elements)
                if (e.kind == ElementKind::Behavior || e.kind == ElementKind::Operation)
                    moved.insert(e.object);
            auto *root = CKBehavior::Cast(doc->Resolve(g.script));
            auto visit = [&](BehaviorData &record) {
                for (auto &link : record.links)
                    if (moved.count(link.start.id) || moved.count(link.end.id)) {
                        const auto points = link.points;
                        link.points.clear();
                        if (!RouteSingleLink(after, record, link, root))
                            link.points = points;
                    }
            };
            visit(after.rootBehavior);
            for (auto &record : after.behaviors)
                visit(record);
        }
        // Content fitting is part of this edit, never a write from rendering.
        if (g.mode != 1) {
            SchematicModel final;
            final = BuildSchematicModel(after, g.script.object, doc->Context(), {}, true);
            const auto content = final.script.contentHeight, stored = after.header.vSize;
            if (stored > 0 && (content > stored || (g.autoResize && content < stored)))
                after.header.vSize = content;
        }
        static const char *labels[] = {"Move", "Resize Script", "Resize Graph", "Resize Comment"};
        Edit edit(*doc, labels[g.mode], Edit::Marks::Scripts);
        if (!edit.ShowScript(g.script.object, after))
            return Result(edit.Fail("Cannot edit the Script display"), sink);
        return Result(edit.Commit(message), sink);
    }
    static EditorStatus Run(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        Geometry g;
        const auto status = Prepare(self.current(), *r, g, true);
        return status == EDITOR_OK ? self.Commit(*r, g, sink) : status;
    }
    static EditorStatus CommitPreview(void *data, const EditorHost *, const EditorRequest *r, void *state,
                                      EditorSink sink) {
        return state ? static_cast<Data *>(data)->Commit(*r, *static_cast<Geometry *>(state), sink) : EDITOR_INVALID;
    }
};
SchematicGeometry::SchematicGeometry(std::function<Document *()> current)
    : m_Data(std::make_unique<Data>(std::move(current))) {}
SchematicGeometry::~SchematicGeometry() = default;
bool SchematicGeometry::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter input[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                     {EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE},
                                     {EDITOR_TEXT("elements"), EDITOR_RECORD, 1},
                                     {EDITOR_TEXT("primary"), EDITOR_RECORD, 1},
                                     {EDITOR_TEXT("coherence"), EDITOR_INTEGER_VALUE, 1},
                                     {EDITOR_TEXT("auto-resize"), EDITOR_INTEGER_VALUE, 1}};
    std::vector<EditorParameter> fields(input, input + 6);
    fields.push_back({EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE});
    fields.push_back({EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE});
    fields.push_back({EDITOR_TEXT("value"), EDITOR_RECORD});
    const EditorQueryDesc query{sizeof(query),
                                EDITOR_TEXT("schematic.geometry"),
                                EDITOR_TEXT("builtin.schematic-geometry"),
                                input,
                                6,
                                Data::Query,
                                m_Data.get(),
                                EDITOR_QUERY_EACH_READ,
                                EDITOR_DOCUMENT,
                                fields.data(),
                                uint32_t(fields.size())};
    const EditorCommandDesc command{sizeof(command),
                                    EDITOR_TEXT("schematic.edit-geometry"),
                                    EDITOR_TEXT("builtin.schematic-geometry"),
                                    EDITOR_TEXT("Edit Schematic Geometry"),
                                    fields.data(),
                                    uint32_t(fields.size()),
                                    Data::Run,
                                    m_Data.get(),
                                    EDITOR_DOCUMENT,
                                    EDITOR_DOCUMENT_EDIT,
                                    Data::Check,
                                    Data::Preview,
                                    Data::CommitPreview};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("schematic.geometry");
    module.commands = &command;
    module.commandCount = 1;
    module.queries = &query;
    module.queryCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev

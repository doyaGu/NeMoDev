#include "core/schematic/SchematicModelBuilder.h"
#include "features/schematic/SchematicModelCommands.h"
#include "features/schematic/SchematicModelData.h"
#include "features/schematic/SchematicPosition.h"
#include "core/document/Document.h"
#include "core/document/ScriptSession.h"
#include "core/graph/RuntimeGraph.h"
#include "core/level/LevelTree.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/schematic/SchematicSearch.h"
#include "core/schematic/SchematicNavigation.h"
#include "core/schematic/SchematicEdits.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
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
ObjectRef Object(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? v->object : ObjectRef{};
}
std::vector<ObjectRef> Objects(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v && v->objects.count ? std::vector<ObjectRef>(v->objects.data, v->objects.data + v->objects.count)
                                 : std::vector<ObjectRef>{};
}
EditorField Int(const char *key, uint64_t value) { return {key, EDITOR_INTEGER_VALUE, {}, {}, value}; }
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto b = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, b.data(), uint32_t(b.size())) : EDITOR_INVALID;
}
} // namespace
struct SchematicModelCommands::Data {
    std::function<Document *()> current;
    DocumentToken document{};
    struct Cached {
        uint64_t revision = ~0ull, topology = ~0ull, view = ~0ull;
        SchematicModel model;
        std::vector<ElementAddress> positions;
    };
    std::map<std::pair<ObjectRef, bool>, Cached> models;
    struct SearchCache {
        bool valid = false;
        uint64_t revision = 0, topology = 0, scripts = 0;
        std::vector<EditorField> arguments;
        std::vector<std::tuple<uint32_t, uint64_t, uint64_t>> sessions;
        std::vector<SearchResult> rows;
    } search;
    static auto SearchVersions(const Document &d) {
        std::vector<std::tuple<uint32_t, uint64_t, uint64_t>> versions;
        for (const auto &entry : d.Sessions())
            versions.emplace_back(entry.first, entry.second->Revision(), entry.second->ViewRevision());
        return versions;
    }
    explicit Data(std::function<Document *()> fn) : current(std::move(fn)) {}
    Document *Doc(const EditorRequest &r) {
        auto *d = current();
        if (!d || !d->IsOpen() || d->Token() != r.scope.document)
            return nullptr;
        if (document != d->Token()) {
            models.clear();
            search = {};
            document = d->Token();
        }
        return d;
    }
    Cached *Model(Document &d, ObjectRef target, bool open, bool refresh = false) {
        auto *session = d.Resolve(target) && d.FindScript(target.object) ? d.Session(target.object) : nullptr;
        if (!session)
            return nullptr;
        for (auto it = models.begin(); it != models.end();)
            if (!d.Resolve(it->first.first))
                it = models.erase(it);
            else
                ++it;
        auto &entry = models[{target, open}];
        if (refresh || entry.revision != session->Revision() || entry.topology != d.TopologyRevision() ||
            entry.view != session->ViewRevision()) {
            SchematicModel next;
            next = BuildSchematicModel(*session, d.Context(), {}, open);
            entry.model = std::move(next);
            entry.positions.clear();
            const auto capture = [&](const auto &items) {
                for (const auto &item : items)
                    entry.positions.push_back(ElementAddress::Capture(d, target.object, item.ref));
            };
            capture(entry.model.behaviors);
            capture(entry.model.pins);
            capture(entry.model.links);
            capture(entry.model.operations);
            capture(entry.model.params);
            capture(entry.model.comments);
            entry.revision = session->Revision();
            entry.topology = d.TopologyRevision();
            entry.view = session->ViewRevision();
        }
        return &entry;
    }
    static EditorStatus Stack(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *d = self.Doc(*r);
        if (!d)
            return EDITOR_STALE;
        const auto *input = Arg(*r, "state");
        EditorRequest state{r->scope, input ? input->fields.data : nullptr, input ? input->fields.count : 0};
        r = &state;
        std::set<std::string> keys;
        for (uint32_t i = 0; i < r->count; ++i) {
            const auto &v = r->values[i];
            const std::string key(v.key.data, v.key.size);
            if (!keys.insert(key).second)
                return EDITOR_INVALID;
            const auto type = key == "target"                                           ? EDITOR_OBJECT
                              : key == "options"                                        ? EDITOR_INTEGER_VALUE
                              : (key == "visible" || key == "hidden" || key == "order") ? EDITOR_OBJECT_LIST
                                                                                        : EditorValueType(0);
            if (v.type != type)
                return EDITOR_INVALID;
        }
        const auto options = Number(*r, "options");
        if (options > 7)
            return EDITOR_INVALID;
        auto target = Object(*r, "target");
        if (!d->Resolve(target) || !d->FindScript(target.object))
            target = {};
        if (!target)
            for (auto old : Objects(*r, "visible"))
                if (d->Resolve(old) && d->FindScript(old.object)) {
                    target = old;
                    break;
                }
        if (!target && !d->Scripts().empty())
            target = d->Reference(d->Scripts().front().id);
        const auto *anchor = target ? d->FindScript(target.object) : nullptr;
        const auto hidden = Objects(*r, "hidden"), order = Objects(*r, "order");
        auto *level = d->Context()->GetCurrentLevel();
        auto *scene = (options & 2) && level ? level->GetCurrentScene() : nullptr;
        EditorField all{"all", EDITOR_OBJECT_LIST}, visible{"scripts", EDITOR_OBJECT_LIST}, rows{"rows", EDITOR_RECORD};
        bool excluded = false;
        for (const auto &info : d->Scripts()) {
            const auto ref = d->Reference(info.id);
            all.objects.push_back(ref);
            auto *object = d->Resolve(ref);
            auto *owner = d->Context()->GetObject(info.ownerId);
            const bool sameOwner =
                anchor && (anchor->ownerId ? info.ownerId == anchor->ownerId : info.id == anchor->id);
            bool shown =
                anchor && ((options & 1) || sameOwner) && std::find(hidden.begin(), hidden.end(), ref) == hidden.end();
            if (!(options & 4) && ref != target && ((object && object->IsDynamic()) || (owner && owner->IsDynamic())))
                shown = false;
            if (shown && scene && owner && owner != level && !scene->IsObjectHere(owner)) {
                excluded |= ref == target;
                shown = false;
            }
            // Hidden Scripts keep their existing metadata without loading another session.
            auto *session = shown ? d->Session(info.id) : d->ExistingSession(info.id);
            if (session) {
                EditorField row{std::to_string(rows.fields.size()), EDITOR_RECORD};
                row.fields = {{"target", EDITOR_OBJECT, ref},
                              Int("revision", session->Revision()),
                              Int("view-revision", session->ViewRevision()),
                              Int("readonly", session->ReadOnly()),
                              Int("preview", session->HasDisplayPreview()),
                              {"height", EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, session->Data().header.vSize}};
                rows.fields.push_back(std::move(row));
            }
            if (shown && session)
                visible.objects.push_back(ref);
        }
        std::stable_sort(visible.objects.begin(), visible.objects.end(), [&](ObjectRef a, ObjectRef b) {
            auto rank = [&](ObjectRef v) {
                auto *s = d->ExistingSession(v.object);
                return s && s->Origin() == SessionOrigin::LoadedChunk ? uint64_t(s->Data().header.scriptIndex) : ~0ull;
            };
            return rank(a) < rank(b);
        });
        if (!order.empty())
            std::stable_sort(visible.objects.begin(), visible.objects.end(), [&](ObjectRef a, ObjectRef b) {
                return std::find(order.begin(), order.end(), a) < std::find(order.begin(), order.end(), b);
            });
        return Write(sink, {{"target", EDITOR_OBJECT, target},
                            all,
                            visible,
                            rows,
                            Int("topology", d->TopologyRevision()),
                            Int("document-revision", d->ChangeRevision()),
                            Int("scripts-revision", d->ScriptsRevision()),
                            Int("scene-excluded", excluded)});
    }
    static EditorStatus ModelRead(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *d = self.Doc(*r);
        if (!d)
            return EDITOR_STALE;
        if (Number(*r, "open") > 1)
            return EDITOR_INVALID;
        auto *m = self.Model(*d, Object(*r, "target"), Number(*r, "open"), !Number(*r, "first"));
        if (!m)
            return EDITOR_STALE;
        if ((Number(*r, "revision") && Number(*r, "revision") != m->revision) ||
            (Number(*r, "view-revision") && Number(*r, "view-revision") != m->view) ||
            (Number(*r, "topology") && Number(*r, "topology") != m->topology))
            return EDITOR_STALE;
        const uint64_t first = Number(*r, "first"), total = SchematicModelSize(m->model);
        if (first > total)
            return EDITOR_INVALID;
        auto projection = SchematicModelFields(m->model, size_t(first));
        for (auto &field : projection.fields)
            if (field.key == "items")
                for (size_t i = 0; i < field.fields.size(); ++i) {
                    const auto &position = m->positions[size_t(first) + i];
                    if (position)
                        field.fields[i].fields.push_back(SchematicPosition(position));
                }
        return Write(sink, {Int("revision", m->revision), Int("view-revision", m->view), Int("topology", m->topology),
                            Int("total", total), Int("next", std::min(first + 128, total)), projection});
    }
    static EditorStatus Snapshot(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *d = self.Doc(*r);
        if (!d)
            return EDITOR_STALE;
        if (Number(*r, "open") > 1)
            return EDITOR_INVALID;
        auto *m = self.Model(*d, Object(*r, "target"), Number(*r, "open") != 0);
        if (!m || m->revision != Number(*r, "revision") || m->topology != Number(*r, "topology") ||
            m->view != Number(*r, "view-revision"))
            return EDITOR_STALE;
        const auto image = m->model.script.snapshot;
        const uint64_t total = image ? image->pixels.size() / 4 : 0;
        const auto first = Number(*r, "first");
        if (first > total)
            return EDITOR_INVALID;
        EditorField pixels{"pixels", EDITOR_INTEGER_LIST};
        auto next = std::min(first + 65536, total);
        for (auto i = first; i < next; ++i) {
            const auto *p = image->pixels.data() + i * 4;
            pixels.integers.push_back(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
                                      uint32_t(p[3]) << 24);
        }
        return Write(sink, {pixels, Int("total", total), Int("next", next)});
    }
    static EditorStatus Navigation(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        if (!d)
            return EDITOR_STALE;
        auto target = Object(*r, "target");
        if (!d->Resolve(target) || !d->FindScript(target.object))
            return EDITOR_STALE;
        ElementAddress address;
        const auto *position = Arg(*r, "position");
        if (position && position->fields.count) {
            if (!ReadSchematicPosition(*position, address) || address.script != target)
                return EDITOR_INVALID;
        } else
            address = ElementAddress::Capture(*d, target.object, Selection::HeaderOf(target.object));
        CK_ID id = 0;
        ElementRef ref;
        if (!address.Resolve(*d, id, ref))
            return EDITOR_STALE;
        const auto focus = Object(*r, "focus-target");
        auto *object = focus ? d->Resolve(focus) : nullptr;
        if (focus && !object)
            return EDITOR_STALE;
        if (object && object->GetID() != id && OwningScript(object) != d->Resolve(target))
            return EDITOR_STALE;
        EditorField parents{"parents", EDITOR_OBJECT_LIST};
        std::set<CK_ID> visited;
        CK_ID parent = focus.object;
        auto *session = d->Session(id);
        while (parent && visited.insert(parent).second) {
            parents.objects.push_back(d->Reference(parent));
            const auto *record = FindRecord(session->DisplayData(), parent);
            parent = record ? record->parentId : 0;
        }
        return Write(sink, {SchematicPosition(address), parents});
    }
    static EditorStatus Validate(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        if (!d)
            return EDITOR_STALE;
        EditorField valid{"elements", EDITOR_RECORD};
        const auto *elements = Arg(*r, "elements");
        for (uint32_t i = 0; elements && i < elements->fields.count; ++i) {
            ElementAddress a;
            if (!ReadSchematicPosition(elements->fields.data[i], a))
                return EDITOR_INVALID;
            uint32_t script = 0;
            ElementRef ref;
            if (a.Resolve(*d, script, ref))
                valid.fields.push_back(SchematicPosition(
                    a, std::string(elements->fields.data[i].key.data, elements->fields.data[i].key.size).c_str()));
        }
        return Write(sink, {valid});
    }
    static EditorStatus Sort(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        if (!d)
            return EDITOR_STALE;
        auto mode = Number(*r, "mode");
        if (mode > 4)
            return EDITOR_INVALID;
        std::vector<CK_ID> ids;
        for (auto ref : Objects(*r, "objects")) {
            if (!d->Resolve(ref) || !d->FindScript(ref.object))
                return EDITOR_STALE;
            ids.push_back(ref.object);
        }
        EditorField result{"objects", EDITOR_OBJECT_LIST};
        for (auto id : SortScripts(*d, std::move(ids), ScriptSort(mode)))
            result.objects.push_back(d->Reference(id));
        return Write(sink, {result});
    }
    static EditorStatus Find(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *d = self.Doc(*r);
        if (!d)
            return EDITOR_STALE;
        const auto *text = Arg(*r, "text");
        const auto options = Number(*r, "options");
        if (options > 7 || !text || !text->text.size)
            return EDITOR_INVALID;
        SearchQuery query;
        query.text = std::string(text->text.data, text->text.size);
        query.kinds = SearchAll;
        auto results = SearchDocument(*d, query);
        auto order = Objects(*r, "order"), hidden = Objects(*r, "hidden"), closed = Objects(*r, "closed");
        for (const auto &info : d->Scripts()) {
            auto ref = d->Reference(info.id);
            if (std::find(order.begin(), order.end(), ref) == order.end())
                order.push_back(ref);
        }
        const auto rank = [&](uint32_t id) {
            return std::find(order.begin(), order.end(), d->Reference(id)) - order.begin();
        };
        std::stable_sort(results.begin(), results.end(),
                         [&](const SearchResult &a, const SearchResult &b) { return rank(a.script) < rank(b.script); });
        results.erase(
            std::remove_if(results.begin(), results.end(),
                           [&](const SearchResult &v) {
                               auto ref = d->Reference(v.script);
                               if (!(options & 2) && std::find(hidden.begin(), hidden.end(), ref) != hidden.end())
                                   return true;
                               if (options & 1)
                                   return false;
                               if (std::find(closed.begin(), closed.end(), ref) != closed.end())
                                   return true;
                               auto *model = self.Model(*d, ref, true);
                               return !model || (v.focus && v.focus != v.script && !model->model.IsOpenGraph(v.focus) &&
                                                 !model->model.FindBehavior(v.focus));
                           }),
            results.end());
        int64_t at = -1;
        ElementAddress previous;
        const auto *position = Arg(*r, "previous");
        uint32_t script = 0;
        ElementRef ref;
        if (position && position->fields.count && ReadSchematicPosition(*position, previous) &&
            previous.Resolve(*d, script, ref))
            for (size_t i = 0; i < results.size(); ++i)
                if (results[i].address.script == previous.script && results[i].ref == ref)
                    at = int64_t(i);
        const auto n = int64_t(results.size());
        const bool backwards = options & 4;
        const auto next = at < 0 ? (backwards ? n - 1 : 0) : at + (backwards ? -1 : 1);
        if (next < 0 || next >= n)
            return Write(
                sink,
                {Int("found", 0), Int("count", n), {"address", EDITOR_RECORD}, {"focus-target", EDITOR_OBJECT, {}}});
        const auto &found = results[size_t(next)];
        return Write(sink, {Int("found", 1),
                            Int("count", n),
                            SchematicPosition(found.address),
                            {"focus-target", EDITOR_OBJECT, d->Reference(found.focus)}});
    }
    static EditorStatus Search(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *d = self.Doc(*r);
        if (!d)
            return EDITOR_STALE;
        if (Number(*r, "all") > 1 || Number(*r, "filter") > 2 || Number(*r, "derived") > 1 ||
            Number(*r, "id") > UINT32_MAX || (Number(*r, "kinds") & ~uint64_t(SearchAll)))
            return EDITOR_INVALID;
        SearchQuery query;
        query.listAll = true;
        const auto *text = Arg(*r, "text");
        query.text = text ? std::string(text->text.data ? text->text.data : "", text->text.size) : "";
        query.id = CK_ID(Number(*r, "id"));
        query.kinds = unsigned(Number(*r, "kinds"));
        query.onlyScripts = !Number(*r, "all");
        query.filter = SearchFilter(Number(*r, "filter"));
        query.derived = Number(*r, "derived");
        const auto type = Number(*r, "type");
        query.type =
            d->Context()->GetParameterManager()->ParameterGuidToType(CKGUID(CKDWORD(type >> 32), CKDWORD(type)));
        if (query.filter != SearchFilter::None && query.type < 0)
            return EDITOR_INVALID;
        for (auto ref : Objects(*r, "scripts")) {
            if (!d->Resolve(ref) || !d->FindScript(ref.object))
                return EDITOR_STALE;
            query.scripts.push_back(ref.object);
        }
        const auto value = Object(*r, "value");
        if (value) {
            auto *v = CKParameter::Cast(d->Resolve(value));
            if (!v)
                return EDITOR_STALE;
            query.value = v->GetID();
        }
        if (query.filter == SearchFilter::Value && !query.value)
            return EDITOR_INVALID;
        const auto first = Number(*r, "first");
        std::vector<EditorField> arguments;
        if (CopyEditorFields(arguments, r->values, r->count) != EDITOR_OK)
            return EDITOR_INVALID;
        arguments.erase(
            std::remove_if(arguments.begin(), arguments.end(), [](const EditorField &v) { return v.key == "first"; }),
            arguments.end());
        if (query.value)
            arguments.push_back(
                {"filter-text", EDITOR_TEXT_VALUE, {}, ParameterText(CKParameter::Cast(d->Resolve(value)))});
        auto &cache = self.search;
        if (!first || !cache.valid || cache.arguments != arguments || cache.revision != d->ChangeRevision() ||
            cache.topology != d->TopologyRevision() || cache.scripts != d->ScriptsRevision() ||
            cache.sessions != SearchVersions(*d)) {
            auto rows = SearchDocument(*d, query);
            cache = {true,
                     d->ChangeRevision(),
                     d->TopologyRevision(),
                     d->ScriptsRevision(),
                     std::move(arguments),
                     SearchVersions(*d),
                     std::move(rows)};
        }
        const auto &results = cache.rows;
        if (first > results.size())
            return EDITOR_INVALID;
        EditorField rows{"rows", EDITOR_RECORD};
        const auto end = std::min<uint64_t>(first + 128, results.size());
        for (auto i = first; i < end; ++i) {
            const auto &v = results[size_t(i)];
            EditorField row{std::to_string(i), EDITOR_RECORD};
            row.fields = {SchematicPosition(v.address),
                          {"focus-target", EDITOR_OBJECT, v.focusReference},
                          {"object", EDITOR_OBJECT, v.objectReference},
                          {"label", EDITOR_TEXT_VALUE, {}, v.label},
                          {"kind", EDITOR_TEXT_VALUE, {}, v.kind},
                          {"detail", EDITOR_TEXT_VALUE, {}, v.detail},
                          {"script-name", EDITOR_TEXT_VALUE, {}, v.scriptName},
                          {"object-name", EDITOR_TEXT_VALUE, {}, v.objectName},
                          Int("class", uint64_t(v.objectClass))};
            rows.fields.push_back(std::move(row));
        }
        return Write(sink, {rows, Int("total", results.size()), Int("next", end),
                            Int("document-revision", d->ChangeRevision()), Int("topology", d->TopologyRevision()),
                            Int("scripts-revision", d->ScriptsRevision())});
    }
    static EditorStatus DeleteSearch(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        if (!d)
            return EDITOR_STALE;
        std::map<uint32_t, std::vector<ElementRef>> elements;
        std::set<uint32_t> scripts;
        const auto *items = Arg(*r, "elements");
        for (uint32_t i = 0; items && i < items->fields.count; ++i) {
            ElementAddress a;
            if (!ReadSchematicPosition(items->fields.data[i], a))
                return EDITOR_INVALID;
            uint32_t script = 0;
            ElementRef ref;
            if (!a.Resolve(*d, script, ref))
                return EDITOR_STALE;
            if (ref.kind == ElementKind::Script)
                scripts.insert(script);
            else
                elements[script].push_back(ref);
        }
        for (const auto &entry : elements)
            if (!scripts.count(entry.first)) {
                const auto result = DeleteElements(*d, entry.first, entry.second);
                if (!result.ok && !result.Unchanged()) {
                    Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(result.message)}});
                    return EDITOR_REFUSED;
                }
            }
        for (auto script : scripts) {
            const auto result = DeleteScript(*d, script);
            if (!result.ok && !result.Unchanged())
                return EDITOR_REFUSED;
        }
        return elements.empty() && scripts.empty() ? EDITOR_NO_CHANGE : EDITOR_OK;
    }
    static EditorStatus ReplaceSearch(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        if (!d)
            return EDITOR_STALE;
        const auto replacement = Object(*r, "value");
        if (!d->Resolve(replacement, CKCID_PARAMETER))
            return EDITOR_STALE;
        const auto *items = Arg(*r, "elements");
        std::vector<std::pair<CK_ID, CK_ID>> parameters;
        for (uint32_t i = 0; items && i < items->fields.count; ++i) {
            ElementAddress a;
            if (!ReadSchematicPosition(items->fields.data[i], a))
                return EDITOR_INVALID;
            uint32_t script = 0;
            ElementRef ref;
            if (!a.Resolve(*d, script, ref))
                return EDITOR_STALE;
            if (a.object)
                parameters.emplace_back(script, a.object.object);
        }
        const auto result = ReplaceParameterValues(*d, parameters, replacement.object);
        Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(result.message)}});
        return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_REFUSED;
    }
    static EditorStatus Fit(void *data, const EditorHost *, const EditorRequest *r, EditorSink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        auto *s = d && d->Resolve(Object(*r, "target")) ? d->Session(Object(*r, "target").object) : nullptr;
        if (!s || s->Revision() != Number(*r, "revision") || d->TopologyRevision() != Number(*r, "topology"))
            return EDITOR_STALE;
        const auto *height = Arg(*r, "height");
        if (!height || !std::isfinite(height->real) || height->real <= 0 || height->real > 1e8)
            return EDITOR_INVALID;
        return s->FitContentHeight(float(height->real)) ? EDITOR_OK : EDITOR_NO_CHANGE;
    }
    static EditorStatus Refresh(void *data, const EditorHost *, const EditorRequest *r, EditorSink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        if (!d)
            return EDITOR_STALE;
        d->RefreshScripts();
        return EDITOR_OK;
    }
};
SchematicModelCommands::SchematicModelCommands(std::function<Document *()> fn)
    : m_Data(std::make_unique<Data>(std::move(fn))) {}
SchematicModelCommands::~SchematicModelCommands() = default;
const std::vector<ProviderChoice> &SchematicModelCommands::DefaultProviders() {
    static const std::vector<ProviderChoice> choices = [] {
        std::vector<ProviderChoice> v;
        for (const auto *id :
             {"schematic.stack", "schematic.model", "schematic.snapshot", "schematic.navigation-target",
              "schematic.validate-positions", "schematic.sort-scripts", "schematic.find", "schematic.search"})
            v.push_back({"query", id, "builtin.schematic-model"});
        for (const auto *id : {"schematic.fit-content", "schematic.refresh", "schematic.delete-search-results",
                               "schematic.replace-search-values"})
            v.push_back({"command", id, "builtin.schematic-model"});
        return v;
    }();
    return choices;
}
bool SchematicModelCommands::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorParameter stackArgs[] = {{EDITOR_TEXT("state"), EDITOR_RECORD, 1}};
    const EditorParameter stackFields[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                           {EDITOR_TEXT("all"), EDITOR_OBJECT_LIST},
                                           {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST},
                                           {EDITOR_TEXT("rows"), EDITOR_RECORD},
                                           {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                           {EDITOR_TEXT("document-revision"), EDITOR_INTEGER_VALUE},
                                           {EDITOR_TEXT("scripts-revision"), EDITOR_INTEGER_VALUE},
                                           {EDITOR_TEXT("scene-excluded"), EDITOR_INTEGER_VALUE}};
    const EditorParameter modelArgs[] = {target,
                                         {EDITOR_TEXT("open"), EDITOR_INTEGER_VALUE},
                                         {EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1},
                                         {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1},
                                         {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE, 1},
                                         {EDITOR_TEXT("view-revision"), EDITOR_INTEGER_VALUE, 1},
                                         {EDITOR_TEXT("refresh"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter modelFields[] = {
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},      {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("view-revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE},          {EDITOR_TEXT("model"), EDITOR_RECORD}};
    const EditorParameter snapshotFields[] = {{EDITOR_TEXT("pixels"), EDITOR_INTEGER_LIST},
                                              {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
                                              {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}};
    const EditorParameter navigationArgs[] = {
        target, {EDITOR_TEXT("position"), EDITOR_RECORD, 1}, {EDITOR_TEXT("focus-target"), EDITOR_OBJECT, 1, 1}};
    const EditorParameter navigationFields[] = {{EDITOR_TEXT("address"), EDITOR_RECORD},
                                                {EDITOR_TEXT("parents"), EDITOR_OBJECT_LIST}};
    const EditorParameter elements{EDITOR_TEXT("elements"), EDITOR_RECORD};
    const EditorParameter sortArgs[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
                                        {EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE}};
    const EditorParameter sortFields{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
    const EditorParameter findArgs[] = {
        {EDITOR_TEXT("text"), EDITOR_TEXT_VALUE},       {EDITOR_TEXT("order"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("hidden"), EDITOR_OBJECT_LIST},    {EDITOR_TEXT("closed"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("options"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("previous"), EDITOR_RECORD, 1}};
    const EditorParameter findFields[] = {{EDITOR_TEXT("found"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("address"), EDITOR_RECORD},
                                          {EDITOR_TEXT("focus-target"), EDITOR_OBJECT}};
    const EditorParameter searchArgs[] = {
        {EDITOR_TEXT("text"), EDITOR_TEXT_VALUE},     {EDITOR_TEXT("id"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("kinds"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("all"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("filter"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE},  {EDITOR_TEXT("derived"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("value"), EDITOR_OBJECT, 1, 1},  {EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter searchFields[] = {{EDITOR_TEXT("rows"), EDITOR_RECORD},
                                            {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("document-revision"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("scripts-revision"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.search"), EDITOR_TEXT("builtin.schematic-model"), searchArgs,
         10, Data::Search, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, searchFields, 6},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.stack"), EDITOR_TEXT("builtin.schematic-model"), stackArgs, 1,
         Data::Stack, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, stackFields, 8, nullptr,
         EDITOR_QUERY_RETAINED_REFERENCES},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.model"), EDITOR_TEXT("builtin.schematic-model"), modelArgs, 7,
         Data::ModelRead, m_Data.get(), EDITOR_QUERY_VERSIONED, EDITOR_DOCUMENT, modelFields, 6,
         [](void *, const EditorHost *) -> uint64_t { return 1; }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.snapshot"), EDITOR_TEXT("builtin.schematic-model"), modelArgs,
         7, Data::Snapshot, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, snapshotFields, 3},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.navigation-target"), EDITOR_TEXT("builtin.schematic-model"),
         navigationArgs, 3, Data::Navigation, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, navigationFields,
         2},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.validate-positions"), EDITOR_TEXT("builtin.schematic-model"),
         &elements, 1, Data::Validate, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &elements, 1, nullptr,
         EDITOR_QUERY_RETAINED_REFERENCES},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.sort-scripts"), EDITOR_TEXT("builtin.schematic-model"),
         sortArgs, 2, Data::Sort, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &sortFields, 1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.find"), EDITOR_TEXT("builtin.schematic-model"), findArgs, 6,
         Data::Find, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, findFields, 4, nullptr,
         EDITOR_QUERY_RETAINED_REFERENCES}};
    const EditorParameter fitArgs[] = {target,
                                       {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                       {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                       {EDITOR_TEXT("height"), EDITOR_REAL_VALUE}};
    const EditorParameter replaceArgs[] = {elements, {EDITOR_TEXT("value"), EDITOR_OBJECT}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.delete-search-results"),
         EDITOR_TEXT("builtin.schematic-model"), EDITOR_TEXT("Delete Search Results"), &elements, 1, Data::DeleteSearch,
         m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.replace-search-values"),
         EDITOR_TEXT("builtin.schematic-model"), EDITOR_TEXT("Replace Search Values"), replaceArgs, 2,
         Data::ReplaceSearch, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.fit-content"), EDITOR_TEXT("builtin.schematic-model"),
         EDITOR_TEXT("Fit Script Content"), fitArgs, 4, Data::Fit, m_Data.get(), EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.refresh"), EDITOR_TEXT("builtin.schematic-model"),
         EDITOR_TEXT("Refresh Schematic"), nullptr, 0, Data::Refresh, m_Data.get(), EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_CHANGE}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("schematic.model");
    module.queries = queries;
    module.queryCount = uint32_t(std::size(queries));
    module.commands = commands;
    module.commandCount = uint32_t(std::size(commands));
    return editor.Register(module, error);
}
} // namespace vdev

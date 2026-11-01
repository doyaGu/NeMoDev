#include "features/schematic/SchematicCopying.h"
#include "features/schematic/SchematicPosition.h"
#include "core/document/Clipboard.h"
#include "core/document/ScriptSession.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/ValueCommands.h"
#include "core/schematic/SchematicModelBuilder.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicGrid.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
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
EditorStatus Result(Document &doc, const EditResult &r, EditorSink sink) {
    Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(r.message)},
                 {"created", EDITOR_OBJECT, doc.Reference(r.created)}});
    return r.ok                                ? EDITOR_OK
           : r.Unchanged()                     ? EDITOR_NO_CHANGE
           : r.Status() == EditStatus::Refused ? EDITOR_REFUSED
                                               : EDITOR_ERROR;
}
bool PointOf(const EditorValue *v, Point &point) {
    if (!v || v->type != EDITOR_REAL_LIST || v->reals.count != 2)
        return false;
    for (uint32_t i = 0; i < 2; ++i)
        if (!std::isfinite(v->reals.data[i]) || std::abs(v->reals.data[i]) > 100000000)
            return false;
    point = Point(float(v->reals.data[0]), float(v->reals.data[1]));
    return true;
}
struct Copied {
    Clipboard clipboard;
    std::vector<std::pair<CK_ID, Point>> locals, operations;
    Rect bounds;
};
EditorStatus Capture(Document &doc, CK_ID script, const EditorValue *elements, Copied &out) {
    auto *session = doc.Session(script);
    if (!session)
        return EDITOR_STALE;
    std::vector<ElementRef> items;
    std::set<ElementRef> unique;
    for (uint32_t i = 0; elements && i < elements->fields.count; ++i) {
        ElementAddress address;
        ElementRef ref;
        CK_ID in = 0;
        if (!ReadSchematicPosition(elements->fields.data[i], address))
            return EDITOR_INVALID;
        if (!address.Resolve(doc, in, ref) || in != script)
            return EDITOR_STALE;
        if (!unique.insert(ref).second)
            return EDITOR_INVALID;
        items.push_back(ref);
    }
    out.clipboard.data = session->Data();
    out.clipboard.data.observers.clear();
    SchematicModel model;
    model = BuildSchematicModel(*session, doc.Context(), {}, true);
    bool any = false;
    const auto grow = [&](const Rect &box) {
        if (!any) {
            out.bounds = box;
            any = true;
            return;
        }
        const float left = std::min(out.bounds.hPos, box.hPos), top = std::min(out.bounds.vPos, box.vPos);
        out.bounds = Rect(left, top, std::max(out.bounds.Right(), box.Right()) - left,
                          std::max(out.bounds.Bottom(), box.Bottom()) - top);
    };
    for (const auto &item : items) {
        if (item.kind == ElementKind::Behavior) {
            out.clipboard.behaviors.push_back(item.object);
            if (const auto *behavior = model.FindBehavior(item.object)) {
                grow(behavior->box);
            }
        } else if (item.kind == ElementKind::Operation && item.object) {
            out.clipboard.operations.push_back(item.object);
            for (const auto &operation : model.operations)
                if (operation.ref == item) {
                    out.operations.push_back({item.object, Point(operation.bounds.hPos, operation.bounds.vPos)});
                    grow(operation.bounds);
                    break;
                }
        } else if ((item.kind == ElementKind::LocalParam || item.kind == ElementKind::Shortcut) && item.object) {
            out.clipboard.parameters.push_back(item.object);
            for (const auto &param : model.params)
                if (param.ref == item) {
                    out.locals.push_back({param.parameter, Point(param.bounds.hPos, param.bounds.vPos)});
                    grow(param.bounds);
                    break;
                }
        } else if (item.kind == ElementKind::Pin && item.pin == PinKind::ParamOut) {
            auto *owner = doc.Context()->GetObject(item.object);
            CKParameter *output = nullptr;
            if (auto *behavior = CKBehavior::Cast(owner))
                output = behavior->GetOutputParameter(item.index);
            else if (auto *operation = CKParameterOperation::Cast(owner))
                output = operation->GetOutParameter();
            if (output)
                out.clipboard.parameters.push_back(output->GetID());
        } else if (item.kind == ElementKind::Comment) {
            const auto *record = FindRecord(session->Data(), item.owner);
            out.clipboard.comments.push_back(record->comments[size_t(item.index)]);
            grow(record->comments[size_t(item.index)].GetRect());
        }
    }
    return out.clipboard.Empty() ? EDITOR_NO_CHANGE : EDITOR_OK;
}
CK_ID GraphAt(const SchematicModel &model, const Point &at) {
    if (!model.script.open)
        return 0;
    const auto graph = model.GraphAt(at);
    return graph ? graph : model.script.id;
}
EditResult CopyParameters(Document &doc, CK_ID script, SchematicModel &model,
                          const std::vector<std::pair<CK_ID, Point>> &locals,
                          const std::vector<std::pair<CK_ID, Point>> &operations) {
    bool changed = false;
    std::string message;
    const auto remember = [&](const EditResult &r) {
        changed |= r.ok;
        if (!r.ok && !r.Unchanged())
            message = r.message;
    };
    for (const auto &entry : locals) {
        auto *source = CKParameter::Cast(doc.Context()->GetObject(entry.first));
        const auto graph = GraphAt(model, entry.second);
        if (!source || !graph)
            continue;
        const auto made = AddLocalParameter(doc, script, graph, source->GetName() ? source->GetName() : "",
                                            source->GetGUID(), entry.second);
        remember(made);
        if (made.ok) {
            const auto bytes = ParameterBytes(source);
            if (!bytes.empty())
                remember(SetParameterBytes(doc, script, made.created, bytes));
        }
    }
    for (const auto &entry : operations) {
        auto *source = CKParameterOperation::Cast(doc.Context()->GetObject(entry.first));
        const auto graph = GraphAt(model, entry.second);
        if (!source || !source->GetOutParameter() || !graph)
            continue;
        const CKGUID none(0, 0);
        remember(AddParameterOperation(
            doc, script, graph, source->GetOperationGuid(), source->GetOutParameter()->GetGUID(),
            source->GetInParameter1() ? source->GetInParameter1()->GetGUID() : none,
            source->GetInParameter2() ? source->GetInParameter2()->GetGUID() : none, entry.second));
    }
    if (changed) {
        auto result = EditResult::Applied();
        result.message = message;
        return result;
    }
    return message.empty() ? EditResult::NoChange() : EditResult::Refused(message);
}
} // namespace
struct SchematicCopying::Data {
    std::function<Document *()> current;
    Clipboard &clipboard;
    enum Action { Copy, Cut, Paste, Shortcuts, Duplicate, Drag };
    struct Entry {
        Data *owner;
        Action action;
    } entries[6];
    Data(std::function<Document *()> current, Clipboard &clipboard)
        : current(std::move(current)), clipboard(clipboard),
          entries{{this, Copy}, {this, Cut}, {this, Paste}, {this, Shortcuts}, {this, Duplicate}, {this, Drag}} {}
    Document *DocumentOf(const EditorRequest &r) const {
        auto *doc = current();
        return doc && doc->IsOpen() && doc->Token() == r.scope.document ? doc : nullptr;
    }
    ScriptSession *Session(Document &doc, const EditorRequest &r) const {
        const auto *target = Arg(r, "target");
        const auto id = target ? doc.NativeId(target->object) : 0;
        auto *session = id ? doc.Session(id) : nullptr;
        return session && session->Revision() == Number(r, "revision") &&
                       doc.TopologyRevision() == Number(r, "topology")
                   ? session
                   : nullptr;
    }
    static EditorStatus Read(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.DocumentOf(*r);
        if (!doc)
            return EDITOR_STALE;
        const bool valid = self.clipboard.BelongsTo(*doc) && !self.clipboard.Empty();
        return Write(sink,
                     {{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.clipboard.Revision()},
                      {"available", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(valid)},
                      {"behaviors", EDITOR_INTEGER_VALUE, {}, {}, valid ? self.clipboard.behaviors.size() : 0},
                      {"comments", EDITOR_INTEGER_VALUE, {}, {}, valid ? self.clipboard.comments.size() : 0},
                      {"parameters", EDITOR_INTEGER_VALUE, {}, {}, valid ? self.clipboard.parameters.size() : 0},
                      {"operations", EDITOR_INTEGER_VALUE, {}, {}, valid ? self.clipboard.operations.size() : 0}});
    }
    static EditorStatus Selection(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.DocumentOf(*r);
        auto *session = doc ? self.Session(*doc, *r) : nullptr;
        if (!session)
            return EDITOR_STALE;
        Copied copied;
        const auto status = Capture(*doc, session->ScriptId(), Arg(*r, "elements"), copied);
        if (status != EDITOR_OK)
            return status;
        std::vector<EditorField> fields;
        if (CopyEditorFields(fields, r->values, r->count) != EDITOR_OK)
            return EDITOR_INVALID;
        EditorField bounds{"bounds", EDITOR_REAL_LIST};
        bounds.reals = {copied.bounds.hPos, copied.bounds.vPos, copied.bounds.hSize, copied.bounds.vSize};
        fields.push_back(std::move(bounds));
        return Write(sink, fields);
    }
    static EditorStatus Run(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &entry = *static_cast<Entry *>(data);
        auto &self = *entry.owner;
        auto *doc = self.DocumentOf(*r);
        auto *session = doc ? self.Session(*doc, *r) : nullptr;
        if (!session)
            return EDITOR_STALE;
        const auto script = session->ScriptId();
        if (session->ReadOnly() && entry.action != Copy)
            return EDITOR_REFUSED;
        const auto *elements = Arg(*r, "elements");
        Copied copied;
        if (entry.action == Copy || entry.action == Cut || entry.action == Duplicate || entry.action == Drag) {
            const auto status = Capture(*doc, script, elements, copied);
            if (status != EDITOR_OK)
                return status;
        }
        Point at;
        if (entry.action != Copy && entry.action != Cut && !PointOf(Arg(*r, "at"), at))
            return EDITOR_INVALID;
        if (entry.action == Copy || entry.action == Cut || entry.action == Duplicate) {
            self.clipboard.Set(*doc, std::move(copied.clipboard));
            if (entry.action == Copy)
                return Write(sink, {{"message",
                                     EDITOR_TEXT_VALUE,
                                     {},
                                     "Copied " + std::to_string(self.clipboard.behaviors.size()) + " behaviors, " +
                                         std::to_string(self.clipboard.comments.size()) + " Comments"}});
            if (entry.action == Cut) {
                std::vector<ElementRef> items;
                for (uint32_t i = 0; i < elements->fields.count; ++i) {
                    ElementAddress a;
                    ReadSchematicPosition(elements->fields.data[i], a);
                    items.push_back(a.element);
                }
                return Result(*doc, DeleteElements(*doc, script, items), sink);
            }
        } else if (entry.action != Drag &&
                   (Number(*r, "clipboard-revision") != self.clipboard.Revision() || !self.clipboard.BelongsTo(*doc)))
            return EDITOR_STALE;
        CK_ID destination = script;
        if (entry.action == Drag) {
            const auto *target = Arg(*r, "destination");
            destination = target ? doc->NativeId(target->object) : 0;
            auto *dest = destination ? doc->Session(destination) : nullptr;
            if (!dest || dest->ReadOnly() || dest->Revision() != Number(*r, "destination-revision"))
                return EDITOR_STALE;
        }
        auto *dest = doc->Session(destination);
        SchematicModel model;
        model = BuildSchematicModel(*dest, doc->Context(), {}, true);
        if (!model.script.open)
            return EDITOR_REFUSED;
        const auto &source = entry.action == Drag ? copied.clipboard : self.clipboard;
        if (entry.action == Shortcuts) {
            if (source.parameters.empty())
                return EDITOR_NO_CHANGE;
            const auto graph = GraphAt(model, at);
            if (!graph)
                return EDITOR_REFUSED;
            const bool root = model.script.isScript;
            const bool changed = EditScriptData(*doc, destination, "Paste Shortcut", [&](InterfaceData &data) {
                auto *record = FindRecord(data, graph);
                int row = 0;
                for (auto id : source.parameters) {
                    Parameter shortcut(id, int(std::floor((at.h - record->rect.hPos) / 20)),
                                       int(std::floor((at.v - record->rect.vPos) / 20)) + row++, PARAM_STYLE_NAME);
                    shortcut.sourceId = id;
                    if (record == &data.rootBehavior && root) {
                        shortcut.hPos = std::max(shortcut.hPos, 9);
                        shortcut.vPos = std::max(shortcut.vPos, 0);
                    }
                    record->sharedParams.push_back(shortcut);
                }
            });
            return Result(*doc, changed ? EditResult::Applied() : EditResult::NoChange(), sink);
        }
        Edit edit(*doc, entry.action == Drag ? "Copy" : entry.action == Duplicate ? "Duplicate" : "Paste");
        EditResult parameters = EditResult::NoChange();
        if (entry.action == Drag) {
            const float dh = at.h - copied.bounds.hPos, dv = at.v - copied.bounds.vPos;
            for (auto &item : copied.locals) {
                item.second.h += dh;
                item.second.v += dv;
            }
            for (auto &item : copied.operations) {
                item.second.h += dh;
                item.second.v += dv;
            }
            parameters = CopyParameters(*doc, destination, model, copied.locals, copied.operations);
        } else if (source.behaviors.empty() && source.comments.empty()) {
            std::vector<std::pair<CK_ID, Point>> locals, operations;
            auto next = at;
            for (auto id : source.parameters) {
                locals.push_back({id, next});
                next.v += 40;
            }
            for (auto id : source.operations) {
                operations.push_back({id, next});
                next.v += 80;
            }
            parameters = CopyParameters(*doc, destination, model, locals, operations);
        }
        if (source.behaviors.empty() && source.comments.empty()) {
            if (parameters.ok)
                return Result(*doc, edit.Commit(), sink);
            return Result(*doc, parameters, sink);
        }
        const auto graph = GraphAt(model, entry.action == Drag ? Point(at.h + 1, at.v + 1) : at);
        if (!graph)
            return parameters.ok ? Result(*doc, edit.Commit(), sink) : EDITOR_REFUSED;
        const auto pasted =
            PasteBehaviors(*doc, source.data, source.behaviors, source.comments, destination, graph, at);
        if (!pasted.ok && !parameters.ok)
            return Result(*doc, pasted, sink);
        const auto committed = edit.Commit();
        if (!committed.ok)
            return Result(*doc, committed, sink);
        return Result(*doc, pasted.ok ? pasted : parameters, sink);
    }
};
SchematicCopying::SchematicCopying(std::function<Document *()> current, Clipboard &clipboard)
    : m_Data(std::make_unique<Data>(std::move(current), clipboard)) {}
SchematicCopying::~SchematicCopying() = default;
bool SchematicCopying::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter selection[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                         {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                         {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                         {EDITOR_TEXT("elements"), EDITOR_RECORD}};
    const EditorParameter contents[] = {
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},   {EDITOR_TEXT("available"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("behaviors"), EDITOR_INTEGER_VALUE},  {EDITOR_TEXT("comments"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("parameters"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("operations"), EDITOR_INTEGER_VALUE}};
    std::vector<EditorParameter> capture(selection, selection + 4);
    capture.push_back({EDITOR_TEXT("bounds"), EDITOR_REAL_LIST});
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.clipboard"), EDITOR_TEXT("builtin.schematic-copying"), nullptr,
         0, Data::Read, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, contents, 6},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.copy-selection"), EDITOR_TEXT("builtin.schematic-copying"),
         selection, 4, Data::Selection, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, capture.data(), 5}};
    const EditorParameter paste[] = {selection[0],
                                     selection[1],
                                     selection[2],
                                     {EDITOR_TEXT("at"), EDITOR_REAL_LIST},
                                     {EDITOR_TEXT("clipboard-revision"), EDITOR_INTEGER_VALUE}};
    const EditorParameter duplicate[] = {selection[0], selection[1], selection[2], selection[3], paste[3]};
    const EditorParameter drag[] = {selection[0],
                                    selection[1],
                                    selection[2],
                                    selection[3],
                                    paste[3],
                                    {EDITOR_TEXT("destination"), EDITOR_OBJECT},
                                    {EDITOR_TEXT("destination-revision"), EDITOR_INTEGER_VALUE}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.copy"), EDITOR_TEXT("builtin.schematic-copying"),
         EDITOR_TEXT("Copy"), selection, 4, Data::Run, &m_Data->entries[0], EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.cut"), EDITOR_TEXT("builtin.schematic-copying"),
         EDITOR_TEXT("Cut"), selection, 4, Data::Run, &m_Data->entries[1], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.paste"), EDITOR_TEXT("builtin.schematic-copying"),
         EDITOR_TEXT("Paste"), paste, 5, Data::Run, &m_Data->entries[2], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.paste-shortcuts"), EDITOR_TEXT("builtin.schematic-copying"),
         EDITOR_TEXT("Paste Shortcuts"), paste, 5, Data::Run, &m_Data->entries[3], EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.duplicate"), EDITOR_TEXT("builtin.schematic-copying"),
         EDITOR_TEXT("Duplicate"), duplicate, 5, Data::Run, &m_Data->entries[4], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.copy-elements"), EDITOR_TEXT("builtin.schematic-copying"),
         EDITOR_TEXT("Copy"), drag, 7, Data::Run, &m_Data->entries[5], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("schematic.copying");
    module.queries = queries;
    module.queryCount = 2;
    module.commands = commands;
    module.commandCount = 6;
    return editor.Register(module, error);
}
} // namespace vdev

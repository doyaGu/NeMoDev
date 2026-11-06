#include "features/diagnostics/DiagnosticCommands.h"
#include "core/document/ToolCommands.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <set>
#include <stdexcept>

namespace vdev {
namespace {
enum class Kind { Unused, Versions, Memory };
const EditorValue *Field(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return request.values + i;
    return nullptr;
}
uint64_t Number(const EditorRequest &request, const char *key) { const auto *v = Field(request, key); return v ? v->integer : 0; }
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
const char *Label(Kind kind) {
    return kind == Kind::Unused ? "Unused Object Explorer" : kind == Kind::Versions ? "Building Block Version Test" : "All CK Object In Memory";
}
const EditorField *Member(const EditorField &row, const char *key) {
    for (const auto &field : row.fields) if (field.key == key) return &field;
    return nullptr;
}
} // namespace
struct DiagnosticCommands::Data : std::enable_shared_from_this<Data> {
    struct Snapshot {
        EditorScope scope{};
        uint64_t revision = 0, change = 0;
        std::vector<EditorField> rows;
        std::set<ObjectRef> objects;
        std::string report;
        uint64_t obsolete = 0, missing = 0, bytes = 0;
    };
    struct Job {
        Kind kind;
        EditorOperation operation;
        RuntimeOperation::DocumentScope document;
        Snapshot result;
        Document *source;
        CKContext *context;
        std::atomic<bool> cancelled{false};
    };
    struct Handler { Data *owner; Kind kind; };
    using Key = std::pair<EditorView, Kind>;
    EditorApplication &editor;
    RuntimeOperation &work;
    CkRuntimeHost &runtime;
    std::function<Document *()> current;
    std::map<Key, Snapshot> snapshots;
    std::map<EditorOperation, std::shared_ptr<Job>> jobs;
    std::deque<Handler> handlers;
    std::vector<EditorField> info;
    uint64_t nextRevision = 1;
    bool stopped = false;
    Data(EditorApplication &editor, RuntimeOperation &work, CkRuntimeHost &runtime,
         std::function<Document *()> current)
        : editor(editor), work(work), runtime(runtime), current(std::move(current)) {}
    bool Ready() const {
        return !stopped && editor.NativeAvailable() && work.CanQueue("diagnostics") && runtime.Context() && !editor.HasEdits();
    }
    bool Live(EditorScope scope) const {
        const auto now = editor.Scope(scope.view);
        return now.view == scope.view && now.document == scope.document;
    }
    const Snapshot *Find(Kind kind, EditorScope scope) const {
        const auto found = snapshots.find({scope.view, kind});
        return found != snapshots.end() && found->second.scope.document == scope.document ? &found->second : nullptr;
    }
    static void Collect(Job &job) {
        auto &result = job.result;
        if (job.kind == Kind::Unused) {
            std::string error;
            const auto rows = UnusedObjects(*job.source, &error);
            if (!error.empty()) throw std::runtime_error(error);
            for (const auto &item : rows) {
                EditorField row{std::to_string(result.rows.size()), EDITOR_RECORD};
                const auto *name = CKClassIDToString(item.cid);
                row.fields = {{"object", EDITOR_OBJECT, job.source->Reference(item.id)},
                    {"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(item.name)},
                    {"class", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(item.cid)},
                    {"class-name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(name ? name : "?")}};
                result.rows.push_back(std::move(row));
                result.objects.insert(job.source->Reference(item.id));
            }
        } else if (job.kind == Kind::Versions) {
            const auto tested = TestBuildingBlockVersions(*job.source);
            result.report = AnsiToUtf8(tested.report); result.obsolete = tested.obsolete; result.missing = tested.withoutPrototype;
            for (const auto &item : tested.changed) {
                auto *block = CKBehavior::Cast(job.source->Context()->GetObject(item.second));
                if (!block) continue;
                const auto guid = block->GetPrototypeGuid();
                EditorField row{std::to_string(result.rows.size()), EDITOR_RECORD};
                row.fields = {{"script", EDITOR_OBJECT, job.source->Reference(item.first)},
                    {"object", EDITOR_OBJECT, job.source->Reference(item.second)},
                    {"prototype", EDITOR_INTEGER_VALUE, {}, {}, (uint64_t(guid.d1) << 32) | guid.d2}};
                result.rows.push_back(std::move(row));
            }
        } else {
            auto *scene = job.context->GetCurrentScene();
            const auto &objects = job.context->GetObjectListByType(CKCID_OBJECT, TRUE);
            for (int i = 0; i < objects.Size(); ++i) {
                auto *object = objects[i]; if (!object) continue;
                const auto size = uint64_t(std::max(0, job.context->GetObjectSize(object)));
                const bool render = CKIsChildClassOf(object, CKCID_RENDEROBJECT) != FALSE;
                const bool sceneObject = scene && CKIsChildClassOf(object, CKCID_SCENEOBJECT);
                const auto *name = CKClassIDToString(object->GetClassID());
                EditorField row{std::to_string(result.rows.size()), EDITOR_RECORD};
                row.fields = {{"object", EDITOR_OBJECT, job.source ? job.source->Reference(object->GetID()) : ObjectRef{}},
                    {"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(object->GetName() ? object->GetName() : "")},
                    {"class-name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(name ? name : "")},
                    {"id", EDITOR_INTEGER_VALUE, {}, {}, object->GetID()}, {"size", EDITOR_INTEGER_VALUE, {}, {}, size},
                    {"render", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(render)},
                    {"visible", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(render && object->IsVisible())},
                    {"dynamic", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(object->IsDynamic() != FALSE)},
                    {"in-scene", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(sceneObject && static_cast<CKSceneObject *>(object)->IsInScene(scene))}};
                result.bytes += size; result.rows.push_back(std::move(row));
            }
        }
    }
    EditorStatus Start(Kind kind, const EditorRequest &request) {
        if (!Ready()) return EDITOR_REFUSED;
        auto *doc = current();
        if (kind != Kind::Memory && (!doc || !doc->IsOpen() || doc->Token() != request.scope.document)) return EDITOR_STALE;
        auto job = std::make_shared<Job>();
        job->kind = kind; job->operation = request.operation; job->document = work.CurrentDocument();
        job->result.scope = request.scope; job->source = doc; job->context = runtime.Context();
        job->result.change = doc ? doc->ChangeRevision() : 0;
        jobs.emplace(request.operation, job);
        const std::weak_ptr<Data> owner = shared_from_this();
        bool queued = false;
        try {
            queued = work.Queue(Label(kind), [job] { if (!job->cancelled) Collect(*job); },
                [owner, job](const std::string &error) {
                    auto self = owner.lock(); if (!self || self->stopped) return;
                    self->jobs.erase(job->operation);
                    EditorResponse result;
                    if (!self->Live(job->result.scope) || job->cancelled) result.status = EDITOR_CANCELLED;
                    else if (!job->document.IsCurrent()) result.status = EDITOR_STALE;
                    else if (!error.empty()) { result.status = EDITOR_ERROR; result.message = error; }
                    else {
                        job->result.revision = self->nextRevision++;
                        result.status = EDITOR_OK;
                        result.fields = {{"revision", EDITOR_INTEGER_VALUE, {}, {}, job->result.revision}};
                        if (job->kind == Kind::Versions) self->runtime.Log(LogLevel::Info, job->result.report);
                        self->snapshots[{job->result.scope.view, job->kind}] = std::move(job->result);
                    }
                    if (result.status == EDITOR_ERROR) self->runtime.Log(LogLevel::Warning, result.message);
                    self->editor.CompleteOperation(job->operation, std::move(result));
                }, RuntimeOperation::Delivery::Always);
        } catch (...) { jobs.erase(request.operation); throw; }
        if (!queued) { jobs.erase(request.operation); return EDITOR_REFUSED; }
        return EDITOR_QUEUED;
    }
    EditorStatus CheckChange(Kind kind, const EditorRequest &request) const {
        if (!Ready()) return EDITOR_REFUSED;
        const auto *snapshot = Find(kind, request.scope);
        auto *doc = current();
        if (!doc || !doc->IsOpen() || !snapshot || snapshot->revision != Number(request, "revision") ||
            doc->Token() != request.scope.document || doc->ChangeRevision() != snapshot->change) return EDITOR_STALE;
        if (kind == Kind::Unused) {
            const auto *objects = Field(request, "objects");
            for (uint32_t i = 0; objects && i < objects->objects.count; ++i) {
                const auto ref = objects->objects.data[i];
                if (!doc->Resolve(ref) || !snapshot->objects.count(ref)) return EDITOR_STALE;
            }
        }
        return EDITOR_OK;
    }
    static EditorStatus Scan(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        auto &h = *static_cast<Handler *>(data); return h.owner->Start(h.kind, *request);
    }
    static EditorStatus ScanState(void *data, const EditorHost *, const EditorRequest *) {
        return static_cast<Handler *>(data)->owner->Ready() ? EDITOR_OK : EDITOR_REFUSED;
    }
    static EditorStatus ChangeState(void *data, const EditorHost *, const EditorRequest *request) {
        auto &h = *static_cast<Handler *>(data); return h.owner->CheckChange(h.kind, *request);
    }
    static EditorStatus Change(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
        auto &h = *static_cast<Handler *>(data); auto &self = *h.owner;
        const auto status = self.CheckChange(h.kind, *request); if (status != EDITOR_OK) return status;
        const auto *snapshot = self.Find(h.kind, request->scope);
        if (h.kind == Kind::Unused) {
            std::vector<CK_ID> ids;
            const auto *objects = Field(*request, "objects");
            for (uint32_t i = 0; i < objects->objects.count; ++i) ids.push_back(objects->objects.data[i].object);
            if (ids.empty()) return EDITOR_NO_CHANGE;
            const auto result = DestroyLevelObjects(*self.current(), ids);
            if (!result.message.empty()) Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(result.message)}});
            self.runtime.Log(result.ok ? LogLevel::Info : LogLevel::Warning,
                "Unused Object Explorer: deleted " + AnsiToUtf8(result.message));
            return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_ERROR;
        }
        std::vector<ObjectRef> scripts, objects; std::vector<uint64_t> prototypes;
        for (const auto &row : snapshot->rows) {
            scripts.push_back(Member(row, "script")->object); objects.push_back(Member(row, "object")->object);
            prototypes.push_back(Member(row, "prototype")->integer);
        }
        if (objects.empty()) return EDITOR_NO_CHANGE;
        const EditorValue values[] = {{EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST, {}, {}, 0, {scripts.data(), uint32_t(scripts.size())}},
            {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}},
            {EDITOR_TEXT("prototypes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {prototypes.data(), uint32_t(prototypes.size())}}};
        const EditorRequest next{request->scope, values, 3};
        auto result = self.editor.Execute("schematic.replace-blocks", next);
        uint64_t count = 0;
        for (const auto &field : result.fields) if (field.key == "replaced") count = field.integer;
        if (!result.message.empty()) result.fields.push_back({"message", EDITOR_TEXT_VALUE, {}, result.message});
        Write(sink, result.fields);
        if (result.status == EDITOR_OK) self.runtime.Log(LogLevel::Info, std::to_string(count) + " Building Block(s) replaced");
        else if (!result.message.empty()) self.runtime.Log(LogLevel::Warning, result.message);
        return result.status;
    }
    static EditorStatus Read(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &h = *static_cast<Handler *>(data); const auto *result = h.owner->Find(h.kind, request->scope);
        if (!result) return Number(*request, "revision") ? EDITOR_STALE : EDITOR_NO_CHANGE;
        const auto first = Number(*request, "first"), revision = Number(*request, "revision");
        if (revision && revision != result->revision) return EDITOR_STALE;
        if (first > result->rows.size()) return EDITOR_INVALID;
        const auto end = std::min(result->rows.size(), size_t(first) + 128);
        EditorField rows{"rows", EDITOR_RECORD}; rows.fields.assign(result->rows.begin() + size_t(first), result->rows.begin() + end);
        return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, result->revision},
            {"total", EDITOR_INTEGER_VALUE, {}, {}, result->rows.size()}, {"next", EDITOR_INTEGER_VALUE, {}, {}, end},
            rows, {"report", EDITOR_TEXT_VALUE, {}, result->report}, {"obsolete", EDITOR_INTEGER_VALUE, {}, {}, result->obsolete},
            {"missing", EDITOR_INTEGER_VALUE, {}, {}, result->missing}, {"bytes", EDITOR_INTEGER_VALUE, {}, {}, result->bytes}});
    }
};
DiagnosticCommands::DiagnosticCommands(EditorApplication &editor, RuntimeOperation &work, CkRuntimeHost &runtime,
    std::function<Document *()> current)
    : m_Data(std::make_shared<Data>(editor, work, runtime, std::move(current))) {}
DiagnosticCommands::~DiagnosticCommands() { Stop(); }
void DiagnosticCommands::Poll() {
    auto &self = *m_Data;
    for (auto it = self.snapshots.begin(); it != self.snapshots.end();)
        if (!self.Live(it->second.scope)) { it = self.snapshots.erase(it); ++self.nextRevision; } else ++it;
    for (auto &entry : self.jobs) if (!self.Live(entry.second->result.scope)) entry.second->cancelled = true;
}
void DiagnosticCommands::InvalidateVersions() {
    ++m_Data->nextRevision;
    for (auto it = m_Data->snapshots.begin(); it != m_Data->snapshots.end();)
        if (it->first.second == Kind::Versions) it = m_Data->snapshots.erase(it); else ++it;
}
void DiagnosticCommands::Stop() {
    if (m_Data->stopped) return;
    m_Data->stopped = true;
    for (const auto &entry : m_Data->jobs) {
        entry.second->cancelled = true; m_Data->editor.CompleteOperation(entry.first, {EDITOR_CANCELLED});
    }
    m_Data->jobs.clear(); m_Data->snapshots.clear();
}
const std::vector<ProviderChoice> &DiagnosticCommands::DefaultProviders() {
    static const std::vector<ProviderChoice> result = {
        {"query", "diagnostics.unused", "builtin.diagnostics"}, {"query", "diagnostics.versions", "builtin.diagnostics"},
        {"query", "diagnostics.memory", "builtin.diagnostics"}, {"query", "diagnostics.info", "builtin.diagnostics"},
        {"command", "diagnostics.scan-unused", "builtin.diagnostics"}, {"command", "diagnostics.scan-versions", "builtin.diagnostics"},
        {"command", "diagnostics.scan-memory", "builtin.diagnostics"}, {"command", "diagnostics.delete-unused", "builtin.diagnostics"},
        {"command", "diagnostics.replace-versions", "builtin.diagnostics"}};
    return result;
}
bool DiagnosticCommands::Register(std::string &error) {
    auto &self = *m_Data;
    const auto &counts = self.runtime.Counts();
    self.info = {{"version", EDITOR_INTEGER_VALUE, {}, {}, CKGetVersion()},
        {"runtime", EDITOR_TEXT_VALUE, {}, self.runtime.RuntimeDirectory()},
        {"dlls", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(counts.behaviorDlls)},
        {"prototypes", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(counts.behaviorPrototypes)},
        {"managers", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(counts.managerDlls)}};
    const EditorParameter page[] = {{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter fields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("rows"), EDITOR_RECORD}, {EDITOR_TEXT("report"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("obsolete"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("missing"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("bytes"), EDITOR_INTEGER_VALUE}};
    const EditorParameter revision{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE};
    const EditorParameter remove[] = {revision, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}};
    const EditorParameter infoFields[] = {{EDITOR_TEXT("version"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("runtime"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("dlls"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("prototypes"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("managers"), EDITOR_INTEGER_VALUE}};
    std::vector<EditorQueryDesc> queries;
    std::vector<EditorCommandDesc> commands;
    const char *ids[] = {"unused", "versions", "memory"};
    // Descriptors are copied during Register; keep generated names alive until then.
    std::deque<std::string> names;
    for (int i = 0; i < 3; ++i) {
        self.handlers.push_back({&self, Kind(i)}); auto *h = &self.handlers.back();
        names.push_back(std::string("diagnostics.") + ids[i]);
        queries.push_back({sizeof(EditorQueryDesc), EditorText(names.back()), EDITOR_TEXT("builtin.diagnostics"), page, 2,
            Data::Read, h, EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, fields, 8,
            [](void *data, const EditorHost *) { return static_cast<Data::Handler *>(data)->owner->nextRevision; }});
        names.push_back(std::string("diagnostics.scan-") + ids[i]);
        commands.push_back({sizeof(EditorCommandDesc), EditorText(names.back()), EDITOR_TEXT("builtin.diagnostics"),
            EditorLiteral(Label(Kind(i))), nullptr, 0, Data::Scan, h, i == 2 ? EDITOR_APPLICATION : EDITOR_DOCUMENT,
            EDITOR_RUNTIME_CONTROL, Data::ScanState});
        if (i < 2) commands.push_back({sizeof(EditorCommandDesc), i ? EditorString EDITOR_TEXT("diagnostics.replace-versions") : EditorString EDITOR_TEXT("diagnostics.delete-unused"),
            EDITOR_TEXT("builtin.diagnostics"), EditorLiteral(i ? "Replace Changed Building Blocks" : "Delete Unused Objects"),
            i ? &revision : remove, i ? 1u : 2u, Data::Change, h, EDITOR_DOCUMENT,
            i ? EDITOR_DOCUMENT_EDIT : EDITOR_DOCUMENT_CHANGE, Data::ChangeState});
    }
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("diagnostics.info"), EDITOR_TEXT("builtin.diagnostics"), nullptr, 0,
        [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) { return Write(sink, static_cast<Data *>(data)->info); },
        &self, EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, infoFields, 5,
        [](void *, const EditorHost *) -> uint64_t { return 1; }});
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("diagnostics.checks");
    module.data = this;
    module.update = [](void *data, const EditorHost *, EditorModulePhase phase) {
        if (phase == EDITOR_PREPARE_FRAME) static_cast<DiagnosticCommands *>(data)->Poll();
    };
    module.stop = [](void *data, const EditorHost *) { static_cast<DiagnosticCommands *>(data)->Stop(); };
    const EditorString dependency = EDITOR_TEXT("schematic.editing"); module.dependencies = &dependency; module.dependencyCount = 1;
    module.commands = commands.data(); module.commandCount = uint32_t(commands.size()); module.queries = queries.data(); module.queryCount = uint32_t(queries.size());
    return self.editor.Register(module, error);
}
} // namespace vdev

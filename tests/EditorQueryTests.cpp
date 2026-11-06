#include "application/EditorApplication.h"
#include "application/ExecutionCommands.h"
#include "application/Preferences.h"
#include "core/document/Document.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
thread_local size_t *trackedAllocations = nullptr;
class Allocations {
public:
    Allocations() { trackedAllocations = &count; }
    ~Allocations() { trackedAllocations = nullptr; }
    size_t count = 0;
};
void ReentrantResultDelivery() {
    struct Data { std::string text = "Original long query value retained throughout nested delivery"; uint64_t version = 1; } data;
    const auto originalText = data.text;
    EditorApplication app({});
    const EditorParameter fields[] = {{EDITOR_TEXT("value"), EDITOR_RECORD}};
    const EditorQueryDesc query{sizeof(query), EDITOR_TEXT("test.delivery"), EDITOR_TEXT("test.delivery"), nullptr, 0,
        [](void *p, const EditorHost *, const EditorRequest *, EditorSink sink) {
            auto &data = *static_cast<Data *>(p);
            const EditorString names[] = {EDITOR_TEXT("First long text list entry"), EDITOR_TEXT("Second long text list entry")};
            const double numbers[] = {1, 2, 3};
            EditorValue members[3]{};
            members[0].key = EDITOR_TEXT("label"); members[0].type = EDITOR_TEXT_VALUE;
            members[0].text = EditorText(data.text);
            members[1].key = EDITOR_TEXT("names"); members[1].type = EDITOR_TEXT_LIST; members[1].texts = {names, 2};
            members[2].key = EDITOR_TEXT("numbers"); members[2].type = EDITOR_REAL_LIST; members[2].reals = {numbers, 3};
            EditorValue value{}; value.key = EDITOR_TEXT("value"); value.type = EDITOR_RECORD; value.fields = {members, 3};
            return sink.write(sink.data, &value, 1);
        }, &data, EDITOR_QUERY_VERSIONED, EDITOR_WORKSPACE, fields, 1,
        [](void *p, const EditorHost *) { return static_cast<Data *>(p)->version; }};
    const EditorCommandDesc command{sizeof(command), EDITOR_TEXT("test.delivery-change"), EDITOR_TEXT("test.delivery"),
        EDITOR_TEXT("Change delivery source"), nullptr, 0,
        [](void *p, const EditorHost *, const EditorRequest *, EditorSink sink) {
            auto &data = *static_cast<Data *>(p); data.text = "Changed long query value after cache invalidation"; ++data.version;
            const EditorValue values[] = {{EDITOR_TEXT("result"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("Command result remains available")},
                                         {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("Command completion message remains available")}};
            return sink.write(sink.data, values, 2);
        }, &data, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.delivery");
    module.queries = &query; module.queryCount = 1; module.commands = &command; module.commandCount = 1;
    std::string error; Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    struct SinkData { EditorApplication &app; bool query = false, command = false; } delivered{app};
    const auto *host = app.Host();
    const EditorRequest request{app.Scope()};
    const EditorSink sink{&delivered, [](void *p, const EditorValue *values, uint32_t count) -> EditorStatus {
        auto &s = *static_cast<SinkData *>(p);
        Check(count == 1 && values[0].fields.count == 3, "query delivery lost record fields");
        const auto *members = values[0].fields.data;
        const auto original = std::string(members[0].text.data, members[0].text.size);
        Check(s.app.Execute("test.delivery-change", {s.app.Scope()}).status == EDITOR_OK, "nested command failed");
        Check(s.app.Query("test.delivery", {s.app.Scope()}).status == EDITOR_OK, "nested query failed");
        Check(std::string(members[0].text.data, members[0].text.size) == original && original.find("Original") == 0,
              "nested cache invalidation changed a delivered text span");
        Check(members[1].texts.count == 2 && std::string(members[1].texts.data[1].data, members[1].texts.data[1].size) ==
                  "Second long text list entry" && members[2].reals.count == 3 && members[2].reals.data[2] == 3,
              "nested delivery invalidated text/number arrays");
        s.query = true; return EDITOR_OK;
    }};
    Check(host->query(host->data, EditorString EDITOR_TEXT("test.delivery"), &request, sink) == EDITOR_OK && delivered.query,
          "reentrant query delivery failed");
    data.text = originalText; ++data.version;
    Check(app.Query("test.delivery", request).status == EDITOR_OK, "query cache warmup failed");
    delivered.query = false;
    Check(host->query(host->data, EditorString EDITOR_TEXT("test.delivery"), &request, sink) == EDITOR_OK && delivered.query,
          "cached query delivery lost its value during nested invalidation");
    const EditorSink reject{nullptr, [](void *, const EditorValue *, uint32_t) { return EDITOR_INVALID; }};
    const EditorSink fail{nullptr, [](void *, const EditorValue *, uint32_t) -> EditorStatus {
        throw std::runtime_error("Injected sink failure");
    }};
    Check(host->query(host->data, query.id, &request, reject) == EDITOR_ERROR &&
              host->query(host->data, query.id, &request, fail) == EDITOR_ERROR &&
              app.Query("test.delivery", request).status == EDITOR_OK,
          "consumer failure escaped or changed the cached query");
    const EditorSink completion{&delivered, [](void *p, const EditorValue *values, uint32_t count) -> EditorStatus {
        auto &s = *static_cast<SinkData *>(p);
        Check(s.app.Query("test.delivery", {s.app.Scope()}).status == EDITOR_OK, "query in completion sink failed");
        bool result = false, message = false, operation = false;
        for (uint32_t i = 0; i < count; ++i) {
            const std::string key(values[i].key.data, values[i].key.size);
            if (key == "result") result = std::string(values[i].text.data, values[i].text.size) == "Command result remains available";
            if (key == "message") message = std::string(values[i].text.data, values[i].text.size) == "Command completion message remains available";
            if (key == "operation") operation = values[i].integer != 0;
        }
        s.command = result && message && operation; return EDITOR_OK;
    }};
    Check(host->execute(host->data, EditorString EDITOR_TEXT("test.delivery-change"), &request, completion) == EDITOR_OK &&
              delivered.command, "command delivery lost message or completion ID");
}

void RecordKeyValidation() {
    for (size_t count : {size_t(1), size_t(64), size_t(65), size_t(4096)}) {
        std::vector<std::string> names;
        names.reserve(count);
        for (size_t i = 0; i < count; ++i) names.push_back("field-" + std::to_string(count - i));
        std::vector<EditorValue> values;
        for (size_t i = 0; i < count; ++i)
            values.push_back({EditorText(names[i]), EDITOR_INTEGER_VALUE, {}, {}, i});
        EditorValue record{EDITOR_TEXT("value"), EDITOR_RECORD};
        record.fields = {values.data(), uint32_t(values.size())};
        std::vector<EditorField> copied;
        Check(CopyEditorFields(copied, &record, 1) == EDITOR_OK, "valid record keys rejected");
        Check(copied.front().fields.size() == count, "record lost fields");
        for (size_t i = 0; i < count; ++i)
            Check(copied.front().fields[i].key == names[i] && copied.front().fields[i].integer == i,
                  "key validation changed field order or value");
        const auto before = copied;
        values.back().key = count == 1 ? EditorString{} : values.front().key;
        Check(CopyEditorFields(copied, &record, 1) == EDITOR_INVALID && copied == before,
              "invalid record keys changed collected fields");
        values.back().key = EditorText(names.back());
        const EditorValue siblings[] = {record, record};
        copied.clear();
        Check(CopyEditorFields(copied, siblings, 2) == EDITOR_INVALID && copied.empty(),
              "duplicate parent keys accepted");
    }
}

void CachedQueryAllocations() {
    // Verify the counter before relying on it to guard the cached query path.
    {
        Allocations allocations;
        auto *memory = ::operator new(256);
        ::operator delete(memory);
        Check(allocations.count == 1, "C++ allocation counter did not observe an allocation");
    }
    EditorApplication app({});
    unsigned reads = 0;
    const EditorParameter field{EDITOR_TEXT("description"), EDITOR_TEXT_VALUE};
    const EditorQueryDesc query{sizeof(query), EDITOR_TEXT("test.cached-query-allocations"), EDITOR_TEXT("test.queries"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            ++*static_cast<unsigned *>(data);
            const EditorValue value{EDITOR_TEXT("description"), EDITOR_TEXT_VALUE, {},
                EDITOR_TEXT("An unchanged query must not copy this long text on each delivery")};
            return sink.write(sink.data, &value, 1);
        }, &reads, EDITOR_QUERY_VERSIONED, EDITOR_WORKSPACE, &field, 1,
        [](void *, const EditorHost *) -> uint64_t { return 1; }};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.queries");
    module.queries = &query; module.queryCount = 1;
    std::string error;
    Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto *host = app.Host();
    const EditorRequest request{app.Scope()};
    unsigned deliveries = 0;
    const EditorSink sink{&deliveries, [](void *data, const EditorValue *values, uint32_t count) {
        if (count != 1 || values[0].type != EDITOR_TEXT_VALUE || values[0].text.size != 64) return EDITOR_INVALID;
        ++*static_cast<unsigned *>(data);
        return EDITOR_OK;
    }};
    Check(host->query(host->data, query.id, &request, sink) == EDITOR_OK, "query warmup failed");
    size_t count = 0;
    {
        Allocations allocations;
        for (int i = 0; i < 1000; ++i)
            Check(host->query(host->data, query.id, &request, sink) == EDITOR_OK, "cached query delivery failed");
        count = allocations.count;
    }
    Check(reads == 1 && deliveries == 1001, "cached query was rebuilt or did not reach its consumer");
    std::cout << "Allocations in 1000 cached query deliveries: " << count << '\n';
    Check(count == 0, "unchanged cached query allocated during delivery");
}
void TimeQueries() {
    CkRuntimeHost runtime;
    std::string error;
    const auto *path = std::getenv("VIRTOOLS_PATH");
    Check(runtime.Start(path ? path : ".", {}, &error), error.c_str());
    Document document(runtime);
    Check(document.New(&error), error.c_str());
    bool native = true;
    EditorApplication app([&] { return &document; }, [&] { return native; });
    Preferences preferences;
    ExecutionCommands execution(app, [&] { return &document; }, preferences);
    Check(execution.Register(error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    execution.Bind(document);
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    const EditorRequest request{app.Scope()};
    uint64_t version = 0, next = 0;
    Check(app.QueryRevision("runtime.time", version) == EDITOR_OK && version, "time settings have no reusable version");
    const auto &original = app.ReadQuery("runtime.time", request);
    Check(original.status == EDITOR_OK && &app.ReadQuery("runtime.time", request) == &original,
          "unchanged time settings were rebuilt");
    execution.Synchronize();
    Check(app.QueryRevision("runtime.time", next) == EDITOR_OK && next == version,
          "unchanged time settings published a different version");
    const bool hide = !preferences.Bool("Time Settings\\Hide Fps");
    preferences.SetBool("Time Settings\\Hide Fps", hide);
    Check(app.QueryRevision("runtime.time", next) == EDITOR_OK && next != version &&
              app.Query("runtime.time", request).fields.back().fields.front().integer == uint64_t(hide),
          "time display preference did not refresh the settings");
    version = next;
    auto settings = app.Query("runtime.time", request).fields;
    settings[2].real = 123.5;
    const auto values = BorrowEditorFields(settings);
    Check(app.Execute("runtime.set-time", {app.Scope(), values.data(), uint32_t(values.size())}).status == EDITOR_OK &&
              app.QueryRevision("runtime.time", next) == EDITOR_OK && next != version &&
              app.Query("runtime.time", request).fields[2].real == 123.5,
          "time command did not refresh the settings");
    native = false;
    Check(app.QueryRevision("runtime.time", version) == EDITOR_OK && version == next &&
              app.Query("runtime.time", request).fields[2].real == 123.5,
          "published time settings were lost while native access was unavailable");
}
void IdentifierSpans() {
    EditorApplication app({});
    unsigned reads = 0;
    const EditorQueryDesc query{sizeof(query), EDITOR_TEXT("test.identifier"), EDITOR_TEXT("test.queries"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink) {
            ++*static_cast<unsigned *>(data);
            return EDITOR_OK;
        }, &reads, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.queries");
    module.queries = &query; module.queryCount = 1;
    std::string error;
    Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto *host = app.Host();
    const EditorRequest request{app.Scope()};
    // Only the span belongs to the name; it need not end at a zero byte.
    const char trailing[] = "test.identifier!not-part-of-the-span";
    Check(host->query(host->data, {trailing, query.id.size}, &request, {}) == EDITOR_OK && reads == 1,
          "identifier lookup read beyond its span");
    const EditorString invalid[] = {{nullptr, 1}, {nullptr, 0}, {"x", 257},
        EDITOR_TEXT("no_namespace"), EDITOR_TEXT("test.identifier!"), EDITOR_TEXT("test.\0identifier"),
        EDITOR_TEXT("test.\xc0\xaf"), EDITOR_TEXT("test.\xc3\xa9"), EDITOR_TEXT("test.unknown")};
    for (const auto id : invalid)
        Check(host->query(host->data, id, &request, {}) == EDITOR_INVALID && reads == 1,
              "invalid identifier reached a query provider");
    Check(host->query(host->data, query.id, nullptr, {}) == EDITOR_INVALID && reads == 1,
          "missing query request was accepted");
    const EditorValue unexpected{EDITOR_TEXT("unexpected"), EDITOR_INTEGER_VALUE};
    const EditorRequest arguments{app.Scope(), &unexpected, 1};
    Check(host->query(host->data, query.id, &arguments, {}) == EDITOR_INVALID && reads == 1,
          "fast lookup bypassed argument validation");
    const auto view = app.OpenScope();
    const EditorRequest closed{app.Scope(view)};
    app.CloseScope(view);
    Check(host->query(host->data, query.id, &closed, {}) == EDITOR_STALE && reads == 1,
          "fast lookup bypassed view lifetime validation");
}
} // namespace

// Only this small test executable replaces allocation functions; production
// binaries and the SDK use their normal allocators. Counting is thread-local.
void *operator new(std::size_t size) {
    if (auto *memory = std::malloc(size ? size : 1)) {
        if (trackedAllocations) ++*trackedAllocations;
        return memory;
    }
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

int main(int argc, char **argv) {
    try {
        Check(argc == 2, "one query test name is required");
        const std::string name = argv[1];
        if (name == "DeliveryLifetime") ReentrantResultDelivery();
        else if (name == "RecordKeys") RecordKeyValidation();
        else if (name == "CachedAllocations") CachedQueryAllocations();
        else if (name == "TimeSettings") TimeQueries();
        else if (name == "IdentifierSpans") IdentifierSpans();
        else throw std::runtime_error("Unknown query test: " + name);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "application/EditorApplication.h"
#include "application/DocumentDataCallbacks.h"
#include "application/EditorProfile.h"
#include "core/document/Document.h"
#include "core/document/DocumentSaver.h"
#include "core/layout/LayoutCommands.h"
#include "core/runtime/CkRuntimeHost.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace vdev;
namespace {
void Check(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
struct Files {
    std::filesystem::path path = std::filesystem::current_path() /
        ("document-data-module-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files() { std::filesystem::create_directory(path); }
    ~Files() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
void RegistryAndCommands(CkRuntimeHost &host, const std::string &library) {
    Files files;
    const auto legacy = (files.path / "v1.cmo").u8string();
    {
        auto registry = std::make_shared<DocumentDataRegistry>(*DefaultDocumentDataRegistry());
        DocumentDataType oldType; oldType.id = "fixture.notes";
        std::string error; Check(registry->Register(std::move(oldType), error), error);
        Document old(host, registry); Check(old.New(&error), error);
        Check(SetDocumentData(old, {"fixture.notes", 1, {}, {0, 7, 255}, {}}).ok, "legacy data fixture failed");
        SaveReport report; Check(SaveDocumentAs(old, legacy, {}, report), "legacy data save failed");
    }
    std::unique_ptr<Document> doc;
    EditorApplication app([&] { return doc.get(); });
    EditorDocumentDataDesc type{sizeof(type), EDITOR_TEXT("fixture.notes"), EDITOR_TEXT("fixture.builtin"), 2};
    EditorModuleDesc builtin{};
    builtin.size = sizeof(builtin); builtin.version = EDITOR_VERSION; builtin.id = EDITOR_TEXT("fixture.builtin");
    builtin.documentData = &type; builtin.documentDataCount = 1;
    std::string error;
    auto invalid = type; invalid.version = 0;
    auto invalidModule = builtin; invalidModule.documentData = &invalid;
    Check(!app.Register(invalidModule, error), "accepted zero data version");
    Check(app.Register(builtin, error) && app.Load(library, error), error);
    Check(!app.Activate({}, error), "ambiguous data providers were silently selected");
    EditorConfiguration configuration;
    configuration.providers.push_back({"document-data", "fixture.notes", "fixture.external"});
    Check(app.Activate({}, error, configuration), error);
    Check(app.ProviderModule(configuration.providers.front()) == "fixture.document-data", "missing data provider ownership");
    EditorProfile profile, restored;
    Check(profile.Capture(Preferences{}, configuration, {}, [&](const auto &choice) { return app.ProviderModule(choice); }, error), error);
    std::string json;
    Check(profile.Encode(json, error) && restored.Decode(json, (files.path / "profile.json").u8string(), error), error);
    Check(restored.Contributions().providers.size() == 1 &&
        restored.Contributions().providers.front().kind == "document-data", "data provider startup selection was lost");
    doc = std::make_unique<Document>(host, app.DataTypes());
    Check(doc->New(&error), error);
    const auto frame = CreateLayoutObject(*doc, LayoutObject::Frame3D);
    Check(frame.ok, frame.message);
    const auto object = doc->Reference(frame.created);
    app.Attach(doc->Token(), doc->TopologyRevision(), true, true);
    app.SetSelection(object);
    const EditorValue arguments[] = {{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, 7},
        {EDITOR_TEXT("object"), EDITOR_OBJECT, object}};
    EditorRequest request{app.Scope(), arguments, 2};
    const auto *api = app.Host();
    const EditorDocumentData value{sizeof(value), 2};
    Check(api->writeDocumentData(api->data, request.scope, EditorString EDITOR_TEXT("fixture.notes"), {}, &value) == EDITOR_REFUSED,
          "data mutation escaped the command edit");
    Check(app.Execute("fixture.notes.set", request).status == EDITOR_OK, "external data command failed");
    auto data = *FindDocumentData(*doc, "fixture.notes");
    Check(data.bytes == std::vector<unsigned char>({0, 7, 255}) && data.objects[0] == object && !data.objects[1],
          "external spans/references were not copied");
    const auto history = doc->History().UndoCount();
    Check(doc->History().Undo() && !FindDocumentData(*doc, "fixture.notes"), "module data undo failed");
    Check(doc->History().Redo(), "module data redo failed");
    Check(app.Execute("fixture.notes.set", request).status == EDITOR_NO_CHANGE && doc->History().UndoCount() == history,
          "unchanged data created another undo step");
    auto changed = arguments[0]; changed.integer = 9;
    request.values = &changed; request.count = 1;
    Check(app.Execute("fixture.notes.fail", request).status == EDITOR_ERROR, "failure injection succeeded");
    Check(*FindDocumentData(*doc, "fixture.notes") == data && doc->History().UndoCount() == history && app.Selection() == object,
          "failed external command changed data/history/selection");
    unsigned reads = 0;
    EditorDocumentDataSink sink{&reads, [](void *p, const EditorDocumentData *value) -> EditorStatus {
        ++*static_cast<unsigned *>(p);
        return value && value->byteCount == 3 && value->bytes[1] == 7 ? EDITOR_OK : EDITOR_ERROR;
    }};
    Check(api->readDocumentData(api->data, app.Scope(), EditorString EDITOR_TEXT("fixture.notes"), {}, sink) == EDITOR_OK && reads == 1,
          "external data query failed");
    std::thread wrongThread([&] {
        Check(api->readDocumentData(api->data, app.Scope(), EditorString EDITOR_TEXT("fixture.notes"), {}, sink) == EDITOR_STALE,
              "worker entered editor data query");
    }); wrongThread.join();
    configuration.providers[0].provider = "fixture.builtin";
    Check(!app.Configure(configuration, error), "data provider changed while its document was open");
    SaveReport report;
    const auto file = (files.path / "module.cmo").u8string();
    Check(SaveDocumentAs(*doc, file, {}, report), report.errors.empty() ? "module save failed" : report.errors.front());
    auto prepared = doc->PrepareOpen(file, &error); Check(bool(prepared), error); prepared->Commit();
    const auto &loaded = *FindDocumentData(*doc, "fixture.notes");
    Check(loaded.bytes == data.bytes && doc->Resolve(loaded.objects[0]), "external module data roundtrip failed");
    prepared = doc->PrepareOpen(legacy, &error); Check(bool(prepared), error); prepared->Commit();
    Check(FindDocumentData(*doc, "fixture.notes")->version == 2 && doc->Dirty(), "external upgrade was not applied");
    doc->Close(); app.Attach({}, 0, true, false);
    Check(app.Configure(configuration, error), error);
    Check(!doc->DataTypes()->Find("fixture.notes")->merge, "closed document kept its old data provider");
    configuration.providers[0].provider = "fixture.external";
    Check(app.Configure(configuration, error) && bool(doc->DataTypes()->Find("fixture.notes")->merge), error);
    Check(doc->Open(file, &error), error);
    // Registries/documents outlive stop and library unload without retaining
    // module-owned pointers in data or undo records.
    app.Attach(doc->Token(), doc->TopologyRevision(), true, true);
    request.scope = app.Scope(); request.values = &changed; request.count = 1;
    Check(app.Execute("fixture.notes.set", request).status == EDITOR_OK, "data edit before stop failed");
    app.Stop();
    Check(doc->History().Undo() && FindDocumentData(*doc, "fixture.notes")->bytes == data.bytes,
          "undo entered unloaded module code or lost data");
    Check(doc->History().Redo() && FindDocumentData(*doc, "fixture.notes")->bytes[1] == 9,
          "redo after module unload failed");
    Check(SaveDocumentAs(*doc, (files.path / "stopped.cmo").u8string(), {}, report), "opaque save after module unload failed");
}
void InvalidCallbacks() {
    auto callbacks = std::make_shared<DocumentDataCallbacks>();
    const int modes =
#if defined(_WIN32) && defined(_MSC_VER)
        5;
#else
        4;
#endif
    for (int mode = 0; mode < modes; ++mode) {
        EditorDocumentDataDesc desc{sizeof(desc), EDITOR_TEXT("fixture.notes"), EDITOR_TEXT("fixture.external"), 2};
        desc.data = &mode;
        desc.upgrade = [](void *p, const EditorDocumentData *old, EditorDocumentDataSink sink) -> EditorStatus {
            const auto mode = *static_cast<int *>(p);
#if defined(_WIN32) && defined(_MSC_VER)
            if (mode == 4) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#endif
            if (mode == 0) throw std::runtime_error("Injected external upgrade failure");
            if (mode == 1) return EDITOR_OK;
            auto value = *old; value.version = 2;
            if (mode == 2) { sink.write(sink.data, &value); sink.write(sink.data, &value); return EDITOR_OK; }
            value.byteCount = UINT32_MAX;
            sink.write(sink.data, &value); return EDITOR_OK;
        };
        auto type = MakeDocumentDataType(desc, callbacks);
        DocumentDataRecord old{"fixture.notes", 1, {}, {17}, {}}, out = old;
        std::string error;
        Check(!type.upgrade(old, out, error) && out == old && !error.empty(), "invalid external data output was accepted");
    }
    // A failed callback, including a Windows native fault, cannot leave the
    // file worker's module lock held and hang shutdown on the editor thread.
    std::thread closing([&] { callbacks->Close(); }); closing.join();
}
void CallbackLifetime() {
    auto callbacks = std::make_shared<DocumentDataCallbacks>();
    struct State { std::mutex mutex; std::condition_variable cv; bool entered = false, release = false; } state;
    EditorDocumentDataDesc desc{sizeof(desc), EDITOR_TEXT("fixture.notes"), EDITOR_TEXT("fixture.external"), 2};
    desc.data = &state;
    desc.upgrade = [](void *p, const EditorDocumentData *old, EditorDocumentDataSink sink) -> EditorStatus {
        auto &s = *static_cast<State *>(p);
        std::unique_lock<std::mutex> lock(s.mutex); s.entered = true; s.cv.notify_all();
        s.cv.wait(lock, [&] { return s.release; });
        auto value = *old; value.version = 2;
        return sink.write(sink.data, &value);
    };
    auto type = MakeDocumentDataType(desc, callbacks);
    DocumentDataRecord old{"fixture.notes", 1, {}, {0, 255}, {}}, result;
    std::string error;
    std::atomic<bool> converted{false}, stopped{false};
    std::thread worker([&] { converted = type.upgrade(old, result, error); });
    { std::unique_lock<std::mutex> lock(state.mutex); state.cv.wait(lock, [&] { return state.entered; }); }
    std::thread stop([&] { callbacks->Close(); stopped = true; });
    // Release under the same synchronization used by the callback, without
    // relying on sleeps or scheduling speed for the last callback's lifetime.
    { std::lock_guard<std::mutex> lock(state.mutex); Check(!stopped, "stop skipped active callback"); state.release = true; }
    state.cv.notify_all(); worker.join(); stop.join();
    Check(converted && stopped && result.version == 2, "active conversion failed during stop");
    Check(!type.upgrade(old, result, error), "stopped registry entered old module code");
}
}
int main(int argc, char **argv) {
    try {
        Check(argc == 2, "module library required");
        CkRuntimeHost host; std::string error;
        const auto *runtime = std::getenv("VIRTOOLS_PATH");
        Check(host.Start(runtime ? runtime : ".", {}, &error), error);
        RegistryAndCommands(host, argv[1]);
        CallbackLifetime();
        InvalidCallbacks();
        std::cout << "Module document data tests passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

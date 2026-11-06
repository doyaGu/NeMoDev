#include "application/Preferences.h"
#include "application/ExecutionCommands.h"
#include "core/document/Document.h"
#include "core/graph/TopologyCommands.h"
#include "core/layout/LayoutCommands.h"
#include "editor/ImGuiDrawing.h"
#include "workspace/EditorClient.h"
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
const EditorField &Field(const EditorResponse &response, const char *key) {
    const auto *field = EditorClient::Find(response, key);
    Check(field, "missing runtime projection field"); return *field;
}
} // namespace
void TestRuntimeProjections(vdev::CkRuntimeHost &host) {
    using namespace vdev;
    Document document(host);
    std::string error;
    Check(document.New(&error), error.c_str());
    bool native = true;
    Preferences preferences;
    preferences.SetBool("3D Layout\\Show Dynamic Objects", false);
    unsigned documentReads = 0;
    const auto current = [&] { ++documentReads; return &document; };
    EditorApplication editor(current, [&] { return native; });
    ExecutionCommands execution(editor, current, preferences);
    unsigned navigations = 0;
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorCommandDesc jump{sizeof(EditorCommandDesc), EDITOR_TEXT("test.profile-jump"), EDITOR_TEXT("test.profile"),
        EDITOR_TEXT("Show profile row"), &target, 1,
        [](void *data, const EditorHost *, const EditorRequest *, EditorSink) {
            ++*static_cast<unsigned *>(data); return EDITOR_OK;
        }, &navigations, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.profile");
    module.commands = &jump; module.commandCount = 1;
    Check(execution.Register(error) && editor.Register(module, error) && editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    execution.Bind(document);
    const auto attach = [&] { editor.Attach(document.Token(), document.TopologyRevision(), true, true); };
    const auto query = [&](const char *id, std::vector<EditorValue> args = {}) {
        auto result = editor.Query(id, {editor.Scope(), args.data(), uint32_t(args.size())});
        Check(result.status == EDITOR_OK, (std::string("runtime query failed: ") + id).c_str()); return result;
    };
    const auto owner = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(owner.ok, "runtime projection owner creation failed");
    ObjectRef lastScript{};
    for (int i = 0; i < 133; ++i) {
        const auto script = CreateScript(document, owner.created, "Profile Script " + std::to_string(i));
        Check(script.ok, "runtime projection Script creation failed");
        lastScript = document.Reference(script.created);
    }
    attach(); execution.Synchronize();
    uint64_t stateVersion = 0, sameStateVersion = 0;
    Check(editor.QueryRevision("runtime.state", stateVersion) == EDITOR_OK, "runtime state has no published version");
    execution.Synchronize(); query("runtime.state");
    Check(editor.QueryRevision("runtime.state", sameStateVersion) == EDITOR_OK && stateVersion == sameStateVersion,
          "unchanged runtime state invalidated its projection");
    const auto enabled = IntegerArgument("enabled", 1);
    Check(editor.Execute("runtime.set-redraw-at-breakpoint", {editor.Scope(), &enabled, 1}).status == EDITOR_OK && preferences.redrawOnlyAtBreakpoint &&
          Field(query("runtime.state"), "redraw-at-breakpoint").integer, "runtime option did not update its saved setting");
    Check(editor.QueryRevision("runtime.state", sameStateVersion) == EDITOR_OK && stateVersion != sameStateVersion,
          "changed runtime state retained a stale projection");
    Check(editor.Execute("runtime.reset", {editor.Scope(), nullptr, 0}).status == EDITOR_OK, "runtime projection log setup failed");
    document.History().MarkSavePoint(); document.MarkSaved();
    const auto selected = document.Reference(owner.created);
    editor.SetSelection(selected);
    const auto history = document.History().UndoCount();
    const auto token = document.Token();
    const auto first = query("runtime.context");
    Check(Field(first, "scripts").objects.size() == 128 && Field(first, "next").integer == 128 &&
          Field(first, "script-owners").objects.front() == document.Reference(owner.created), "runtime choices are not bounded or lost their owners");
    const auto tail = query("runtime.context", {IntegerArgument("first", 128)});
    Check(Field(tail, "revision").integer == Field(first, "revision").integer &&
          Field(tail, "scripts").objects.back() == lastScript && Field(tail, "script-names").texts.back() == "Profile Script 132",
          "runtime choices lost a page or an object identity");
    const auto observe = [&](EditorView view) {
        const auto enabled = IntegerArgument("enabled", 1);
        Check(editor.Execute("runtime.observe-profile", {editor.Scope(view), &enabled, 1}).status == EDITOR_OK, "profile observer refused");
    };
    const auto one = editor.OpenScope(), two = editor.OpenScope();
    document.Context()->EnableProfiling(FALSE);
    observe(one); observe(two); execution.Synchronize();
    Check(document.Context()->IsProfilingEnable(), "visible profiling did not enable native counters");
    const auto profile = query("runtime.profile", {IntegerArgument("details", 1)});
    Check(Field(profile, "available").integer && Field(profile, "row-count").integer > 128 &&
          !Field(profile, "show-dynamic").integer, "profile summary or details missing");
    auto version = Field(profile, "revision").integer;
    auto rows = query("runtime.profile-rows", {IntegerArgument("first", 0), IntegerArgument("revision", version)});
    Check(Field(rows, "rows").fields.size() == 128, "profile detail page exceeds its limit");
    rows = query("runtime.profile-rows", {IntegerArgument("first", 128), IntegerArgument("revision", version)});
    ObjectRef retained{};
    for (const auto &row : Field(rows, "rows").fields)
        for (const auto &value : row.fields) if (value.key == "script" && value.object == lastScript) retained = value.object;
    Check(retained == lastScript, "paged profile details lost the Script identity");
    preferences.SetBool("3D Layout\\Show Dynamic Objects", true);
    const auto visible = query("runtime.profile");
    Check(Field(visible, "show-dynamic").integer, "dynamic visibility did not follow its published setting");
    version = Field(visible, "revision").integer;
    editor.CloseScope(one); execution.Synchronize();
    Check(document.Context()->IsProfilingEnable(), "closing one view disabled another view's profiling");
    // Reads during file work use only the completed samples, even when the
    // host blocks native access independently of the execution callback.
    editor.NativeAvailable(false);
    const auto reads = documentReads;
    Check(Field(query("runtime.profile"), "revision").integer == version &&
          Field(query("runtime.context"), "scripts").objects == Field(first, "scripts").objects,
          "host-busy projections changed their native sample");
    Check(documentReads == reads, "host-busy projection entered the document");
    editor.NativeAvailable(true); native = false;
    editor.CloseScope(two); execution.Synchronize();
    const auto busyReads = documentReads;
    Check(Field(query("runtime.profile"), "revision").integer == version &&
          query("runtime.profile-rows", {IntegerArgument("first", 128), IntegerArgument("revision", version)}).status == EDITOR_OK,
          "busy profiling did not preserve its completed data");
    Check(documentReads == busyReads, "worker-busy projection entered the document");
    native = true; execution.Synchronize();
    Check(!document.Context()->IsProfilingEnable(), "closing the final view during file work lost profiling cleanup");
    document.Context()->EnableProfiling(TRUE);
    const auto already = editor.OpenScope(); observe(already); execution.Synchronize();
    editor.CloseScope(already); execution.Synchronize();
    Check(document.Context()->IsProfilingEnable(), "closing a view overwrote previously enabled profiling");
    document.Context()->EnableProfiling(FALSE);
    const auto replacing = editor.OpenScope(); observe(replacing); execution.Synchronize();
    native = false;
    const auto publishedLog = Field(query("runtime.state"), "log-entries").integer;
    Check(publishedLog, "runtime projection log was unexpectedly empty");
    std::thread worker([&] { execution.ReleaseDocument(); }); worker.join();
    Check(!document.Context()->IsProfilingEnable(), "file worker did not restore profiling before replacement");
    Check(Field(query("runtime.profile"), "revision").integer == version &&
          Field(query("runtime.state"), "log-entries").integer == publishedLog, "release changed published data on the worker");
    native = true; execution.Synchronize();
    Check(!Field(query("runtime.state"), "log-entries").integer, "release completion kept the old published log in the same document");
    Check(document.Token() == token && document.History().UndoCount() == history && !document.Dirty() &&
          editor.Selection() == selected && !document.EditOpen(),
          "runtime queries or profiling modified document identity or undo history");
    // A retained diagnostic row cannot navigate to a later object using the
    // same native slot. The registered command performs the same check for UI
    // and external callers.
    document.Context()->DestroyObject(retained.object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID);
    CKObject *replacement = nullptr;
    for (int i = 0; i < 256 && !replacement; ++i) {
        auto *created = document.Context()->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Profile replacement");
        Check(created, "profile replacement creation failed");
        if (created->GetID() == retained.object) replacement = created;
    }
    Check(replacement && !document.Resolve(retained), "profile target slot was not reused");
    const auto stale = ObjectArgument("target", retained);
    Check(editor.Execute("test.profile-jump", {editor.Scope(), &stale, 1}).status == EDITOR_STALE && !navigations,
          "retained profile target navigated to a replacement");
    const auto invalid = IntegerArgument("details", 2);
    Check(editor.Query("runtime.profile", {editor.Scope(), &invalid, 1}).status == EDITOR_INVALID, "invalid profile options accepted");
    execution.DocumentChanging();
    Check(document.New(&error), error.c_str()); attach(); execution.Synchronize();
    Check(document.Context()->IsProfilingEnable(), "live profile view did not follow a successful document switch");
    const auto after = query("runtime.profile", {IntegerArgument("details", 1)});
    Check(Field(after, "revision").integer != version && Field(query("runtime.context"), "scripts").objects.empty(),
          "old runtime choices survived document replacement");
    const EditorValue oldRows[] = {IntegerArgument("first", 0), IntegerArgument("revision", version)};
    Check(editor.Query("runtime.profile-rows", {editor.Scope(), oldRows, 2}).status == EDITOR_STALE,
          "old profile page was read from a newer sample");
    editor.CloseScope(replacing); execution.Synchronize();
    Check(!document.Context()->IsProfilingEnable(), "new document kept a closed profiler's counters");
    execution.DocumentChanging(); document.Close(); editor.Attach({}, 0, true, false);
    Check(!execution.Update(0.01) && !Field(query("runtime.state"), "available").integer &&
          !Field(query("runtime.profile"), "available").integer,
          "idle execution did not finish closing its document projections");
    execution.Stop();
    Check(!Field(query("runtime.profile"), "available").integer, "stopped execution published an old profile");
}

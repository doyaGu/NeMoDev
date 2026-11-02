#include "application/EditorModules.h"
#include "core/document/Document.h"
#include "core/level/LevelCommands.h"
#include "editor/ImGuiDrawing.h"
#include "workspace/EditorClient.h"
#include "workspace/EditorWorkspace.h"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Window {
    std::string topic = "initial";
    uint64_t session = 1729;
};
struct Backend {
    std::map<EditorView, Window *> windows;
    bool fail = false, single = false;
    bool Register(EditorApplication &app, std::string &error) {
        EditorViewDesc view{};
        view.size = sizeof(view);
        view.imguiVersion = EDITOR_IMGUI_VERSION;
        view.id = EDITOR_TEXT("test.saved-view");
        view.provider = EDITOR_TEXT("test.saved-provider");
        view.title = EDITOR_TEXT("Saved View");
        view.area = EDITOR_WORKSPACE;
        view.singleInstance = single;
        view.data = this;
        view.create = [](void *p, const EditorHost *, EditorView id) -> void * {
            auto &b = *static_cast<Backend *>(p);
            if (b.fail)
                return nullptr;
            auto *w = new Window;
            b.windows.emplace(id, w);
            return w;
        };
        view.destroy = [](void *p) { delete static_cast<Window *>(p); };
        view.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *frame) {
            EditorImGuiWindow window(*frame);
        };
        view.stateVersion = 1;
        view.saveState = [](void *p, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const auto &w = *static_cast<Window *>(p);
            if (w.topic == "bad-save")
                throw std::runtime_error("injected view save failure");
            const EditorValue v[] = {{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, {}, EditorText(w.topic)},
                                     {EDITOR_TEXT("session"), EDITOR_INTEGER_VALUE, {}, {}, w.session}};
            return sink.write(sink.data, v, 2);
        };
        view.restoreState = [](void *p, const EditorHost *, const EditorRequest *r) {
            if (r->count != 2)
                return EDITOR_INVALID;
            Window next;
            for (uint32_t i = 0; i < r->count; ++i) {
                const auto &v = r->values[i];
                const std::string key(v.key.data, v.key.size);
                if (key == "topic" && v.type == EDITOR_TEXT_VALUE)
                    next.topic.assign(v.text.data, v.text.size);
                else if (key == "session" && v.type == EDITOR_INTEGER_VALUE)
                    next.session = v.integer;
                else
                    return EDITOR_INVALID;
            }
            if (next.topic == "reject")
                return EDITOR_ERROR;
            if (next.topic == "throw")
                throw std::runtime_error("injected view restore failure");
            *static_cast<Window *>(p) = std::move(next);
            return EDITOR_OK;
        };
        static const EditorParameter settings{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE};
        view.settings = &settings;
        view.settingCount = 1;
        EditorModuleDesc module{};
        module.size = sizeof(module);
        module.version = EDITOR_VERSION;
        module.id = EDITOR_TEXT("test.saved-module");
        module.views = &view;
        module.viewCount = 1;
        return app.Register(module, error);
    }
};
} // namespace
void TestWorkspacePersistence(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    EditorApplication editor([&] { return &doc; });
    EditorWorkspace workspace(editor);
    Backend backend;
    Check(workspace.Register(error) && backend.Register(editor, error) && editor.Activate(EDITOR_IMGUI_VERSION, error),
          error.c_str());
    editor.Attach(doc.Token(), doc.TopologyRevision(), true, true);
    {
        const EditorParameter native{EDITOR_TEXT("native"), EDITOR_OBJECT};
        EditorViewDesc invalid{};
        invalid.size = sizeof(invalid);
        invalid.imguiVersion = EDITOR_IMGUI_VERSION;
        invalid.id = EDITOR_TEXT("test.invalid-settings");
        invalid.provider = EDITOR_TEXT("test.invalid-settings");
        invalid.title = EDITOR_TEXT("Invalid");
        invalid.create = [](void *, const EditorHost *, EditorView) -> void * { return nullptr; };
        invalid.destroy = [](void *) {};
        invalid.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *) {};
        invalid.settings = &native;
        invalid.settingCount = 1;
        EditorModuleDesc module{};
        module.size = sizeof(module);
        module.version = EDITOR_VERSION;
        module.id = EDITOR_TEXT("test.invalid-settings");
        module.views = &invalid;
        module.viewCount = 1;
        EditorApplication invalidApp({});
        Check(!invalidApp.Register(module, error), "unsafe persistent schema was registered");
    }
    WorkspaceConfiguration layout;
    layout.views = {{"test.saved-view", "test.left", "", false, false, false, WorkspaceWindowId::Role},
                    {"test.saved-view", "test.right", "", false, false, false, WorkspaceWindowId::Role}};
    Check(workspace.Configure(layout, error), error.c_str());
    const auto left = workspace.OpenRole("test.left"), right = workspace.OpenRole("test.right");
    Check(left && right && left != right, "independent saved instances");
    backend.windows.at(left)->topic = u8"左侧 navigation";
    backend.windows.at(left)->session = 999;
    backend.windows.at(right)->topic = "right navigation";
    workspace.Focus(left);
    WorkspaceState state;
    Check(workspace.Save(state, error), error.c_str());
    Check(state.views.size() == 2 && state.views[0].settings.size() == 1 && state.views[0].settings[0].key == "topic",
          "session state leaked into persistent fields");
    const auto name = workspace.WindowName(left);
    auto retained = state;
    backend.windows.at(left)->topic = "bad-save";
    Check(!workspace.Save(retained, error) && retained.views.front().settings == state.views.front().settings,
          "throwing save changed the previous workspace state");
    backend.windows.at(left)->topic = u8"左侧 navigation";
    const auto object = CreateLayoutObject(doc, LayoutObject::Frame3D);
    Check(object.ok, "history target");
    const auto selected = doc.Reference(object.created);
    editor.SetSelection(selected);
    Check(RenameLevelObject(doc, selected, "Saved workspace history").ok && doc.History().Undo(),
          "seed saved workspace history");
    editor.Attach(doc.Token(), doc.TopologyRevision(), true, true);
    const auto undo = doc.History().UndoCount();
    const auto redo = doc.History().CanRedo();
    const auto revision = doc.ChangeRevision();
    auto bad = state;
    bad.views.back().settings[0].text = "reject";
    Check(!workspace.Restore(bad, error) && workspace.Role("test.left") == left &&
              workspace.Role("test.right") == right && backend.windows.at(left)->topic == u8"左侧 navigation" &&
              editor.Scope(left).view,
          "failed restore replaced original instances or scopes");
    bad.views.back().settings[0].text = "throw";
    Check(!workspace.Restore(bad, error) && workspace.Role("test.left") == left &&
              workspace.Role("test.right") == right && editor.Scope(left).view,
          "throwing restore replaced original instances or scopes");
    bad = state;
    bad.layout = "[Docking][Data]\nDockNode ID=0x1 Parent=0x2\nDockNode ID=0x2 Parent=0x1\n";
    Check(!workspace.Restore(bad, error) && workspace.Role("test.left") == left,
          "cyclic dock tree changed active workspace");
    Check(workspace.Restore(state, error), error.c_str());
    const auto restored = workspace.Role("test.left");
    Check(restored && restored != left && !editor.Scope(left).view && workspace.WindowName(restored) == name &&
              backend.windows.at(restored)->topic == u8"左侧 navigation" &&
              backend.windows.at(restored)->session == 1729,
          "restore lost window identity or restored process-local state");
    Check(doc.ChangeRevision() == revision && doc.History().UndoCount() == undo && doc.History().CanRedo() == redo &&
              editor.Selection() == selected,
          "workspace restore changed document, selection or history");
    {
        EditorApplication missing([&] { return &doc; });
        EditorWorkspace unavailable(missing);
        Check(unavailable.Register(error) && missing.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
        missing.Attach(doc.Token(), doc.TopologyRevision(), true, true);
        Check(unavailable.Restore(state, error), error.c_str());
        const auto placeholder = unavailable.Role("test.left");
        Check(placeholder && unavailable.Unavailable(placeholder) && unavailable.WindowName(placeholder) == name,
              "missing provider lost placeholder identity");
        WorkspaceState retained;
        Check(unavailable.Save(retained, error) && retained.views[0].settings == state.views[0].settings,
              "missing provider lost opaque settings");
        const auto projection =
            EditorClient(missing.Host(), 0).Query("workspace.view", {IntegerArgument("view", placeholder)});
        Check(projection.status == EDITOR_OK && EditorClient::Integer(projection, "available") == 0,
              "unavailable status was not projected");
        Check(EditorClient(missing.Host(), 0)
                      .Command("workspace.close-view", {IntegerArgument("view", placeholder)})
                      .status == EDITOR_OK,
              "close unavailable view command");
        Check(unavailable.Save(retained, error), error.c_str());
        Check(std::any_of(retained.views.begin(), retained.views.end(),
                          [](const auto &v) { return v.role == "test.left" && !v.open && !v.settings.empty(); }),
              "closing missing provider discarded settings");
        const auto reopened =
            EditorClient(missing.Host(), 0)
                .Command("workspace.reopen-instance", {IntegerArgument("instance", state.views[0].instance)});
        Check(reopened.status == EDITOR_OK && unavailable.Unavailable(unavailable.Role("test.left")),
              "saved unavailable instance could not reopen");
    }
    {
        EditorApplication app([&] { return &doc; });
        EditorWorkspace target(app);
        Backend single;
        single.single = true;
        Check(target.Register(error) && single.Register(app, error) && app.Activate(EDITOR_IMGUI_VERSION, error),
              error.c_str());
        app.Attach(doc.Token(), doc.TopologyRevision(), true, true);
        const auto id = target.Open("test.saved-view");
        Check(id != 0, "open singleton");
        single.windows.at(id)->topic = "closed singleton";
        single.windows.at(id)->session = 999;
        const auto name = target.WindowName(id);
        Check(target.Close(id), "close singleton");
        WorkspaceState closed;
        Check(target.Save(closed, error) && closed.views.size() == 1 && !closed.views.front().open &&
                  closed.views.front().settings.size() == 1,
              "closed singleton settings were not retained");
        const auto reopened = target.Open("test.saved-view");
        Check(reopened && target.WindowName(reopened) == name && single.windows.at(reopened)->session == 999 &&
                  single.windows.at(reopened)->topic == "closed singleton",
              "persistent settings replaced complete same-session singleton state");
    }
    {
        EditorApplication failed([&] { return &doc; });
        EditorWorkspace target(failed);
        Backend failing;
        failing.fail = true;
        Check(target.Register(error) && failing.Register(failed, error) && failed.Activate(EDITOR_IMGUI_VERSION, error),
              error.c_str());
        failed.Attach(doc.Token(), doc.TopologyRevision(), true, true);
        Check(!target.Restore(state, error) && target.SavingBlocked() && target.ViewCount() == 0,
              "failed startup restore allowed overwriting saved state");
        Check(EditorClient(failed.Host(), 0).Command("workspace.discard-saved-state").status == EDITOR_OK &&
                  !target.SavingBlocked(),
              "workspace recovery action failed");
    }
}

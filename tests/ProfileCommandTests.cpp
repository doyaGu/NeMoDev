#include "application/ProfileCommands.h"
#include "workspace/EditorClient.h"
#include "editor/ImGuiDrawing.h"
#include "core/document/Document.h"
#include "core/level/LevelCommands.h"
#include <algorithm>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
std::string Bytes(const EditorProfile &profile) {
    std::string text, error; Check(profile.Encode(text, error), error.c_str()); return text;
}
bool Feature(EditorApplication &editor, std::string &error) {
    const EditorQueryDesc query{sizeof(EditorQueryDesc), EDITOR_TEXT("test.profile-value"), EDITOR_TEXT("test.profile"),
        nullptr, 0, [](void *, const EditorHost *, const EditorRequest *, EditorSink) { return EDITOR_OK; },
        nullptr, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.profile-feature"); module.queries = &query; module.queryCount = 1;
    return editor.Register(module, error);
}
}
void TestProfileCommands(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document document(runtime); std::string error;
    Check(document.New(&error), error.c_str());
    EditorApplication editor([&] { return &document; });
    EditorProfile profile;
    bool refuse = false, throws = false; unsigned writes = 0;
    ProfileCommands commands(editor, profile, [&](EditorProfile &next, std::string &message) {
        ++writes;
        Check(Bytes(next) != Bytes(profile), "writer received the active profile instead of the prepared change");
        if (throws) throw std::runtime_error("Injected profile exception");
        if (refuse) { message = "Injected profile publication refusal"; return false; }
        return true;
    });
    Check(editor.Assemble({{"profile", [&](auto &, auto &e) { return commands.Register(e); }}, {"feature", Feature}},
          EDITOR_IMGUI_VERSION, error), error.c_str());
    editor.Attach(document.Token(), document.TopologyRevision(), true, true);
    const auto object = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(object.ok, "create profile history target");
    const auto selected = document.Reference(object.created); editor.SetSelection(selected);
    document.History().Clear(); document.MarkSaved(); document.History().MarkSavePoint();
    Check(RenameLevelObject(document, selected, "Profile history").ok && document.History().Undo(), "seed profile Redo");
    editor.Attach(document.Token(), document.TopologyRevision(), true, true);
    const auto token = document.Token(); const auto revision = document.ChangeRevision(); const auto dirty = document.Dirty();
    const auto undo = document.History().UndoCount(); const auto redo = document.History().CanRedo();
    const EditorClient client(editor.Host(), 0);
    auto before = client.Query("profile.modules");
    Check(before.status == EDITOR_OK, "profile modules query failed");
    const auto set = [&](const char *id, bool enabled) {
        EditorField row{"module", EDITOR_RECORD};
        row.fields = {{"id", EDITOR_TEXT_VALUE, {}, id}, {"enabled", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(enabled)}};
        const std::vector<EditorField> fields{std::move(row)};
        const auto values = BorrowEditorFields(fields);
        return editor.Execute("profile.set-module", {client.Scope(), values.data(), uint32_t(values.size())});
    };
    Check(set("test.profile-feature", false).status == EDITOR_OK && writes == 1 && !profile.Modules().front().enabled,
          "module change did not persist for next startup");
    const auto current = client.Query("profile.modules");
    const auto *rows = EditorClient::Find(current, "modules");
    Check(rows && rows->fields.size() == 3 && before.fields != current.fields, "module query was not refreshed or copied");
    const auto found = std::find_if(rows->fields.begin(), rows->fields.end(), [](const auto &row) { return row.key == "test.profile-feature"; });
    Check(found != rows->fields.end(), "profile query lost disabled module");
    Check(std::any_of(found->fields.begin(), found->fields.end(), [](const auto &field) { return field.key == "active" && field.integer == 1; }) &&
          std::any_of(found->fields.begin(), found->fields.end(), [](const auto &field) { return field.key == "enabled" && field.integer == 0; }),
          "module query confused current and next-start state");
    Check(client.Query("test.profile-value").status == EDITOR_OK, "pending disable unloaded active code");
    const auto retained = Bytes(profile);
    refuse = true;
    Check(set("test.profile-feature", true).status == EDITOR_ERROR && Bytes(profile) == retained, "failed write changed profile");
    throws = true;
    Check(set("test.profile-feature", true).status == EDITOR_ERROR && Bytes(profile) == retained, "throwing writer changed profile");
    Check(set("application.operations", false).status == EDITOR_INVALID && writes == 3, "kernel disable reached the writer");
    Check(document.Token() == token && document.ChangeRevision() == revision && document.Dirty() == dirty &&
          document.History().UndoCount() == undo && document.History().CanRedo() == redo && editor.Selection() == selected,
          "module configuration changed document, selection or history");
    EditorProfile restored; Check(restored.Decode(retained, "profile.json", error), error.c_str());
    EditorApplication restarted({});
    Check(restarted.Assemble({{"feature", Feature}}, EDITOR_IMGUI_VERSION, error, {}, restored.ResolvedModules()), error.c_str());
    Check(EditorClient(restarted.Host(), 0).Query("test.profile-value").status != EDITOR_OK,
          "next startup did not restore module disable");
}

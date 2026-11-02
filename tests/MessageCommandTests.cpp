#include "application/MessageCommands.h"
#include "core/document/Document.h"
#include "core/layout/LayoutCommands.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include "workspace/EditorClient.h"

#include <stdexcept>

namespace {
void Check(bool ok, const char *text) { if (!ok) throw std::runtime_error(text); }
}
void TestMessageCommands(vdev::CkRuntimeHost &host) {
    using namespace vdev;
    Document document(host);
    std::string error;
    Check(document.New(&error), error.c_str());
    const auto made = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(made.ok, "message fixture creation failed");
    const auto target = document.Reference(made.created);
    Check(RenameLevelObject(document, target, "Message fixture").ok && document.History().Undo(),
          "message fixture must have undo and redo history");
    MessageCommands messages;
    bool native = true;
    unsigned documentReads = 0;
    EditorApplication editor([&] { ++documentReads; return &document; }, [&] { return native; });
    Check(messages.Register(editor, error) && editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    editor.Attach(document.Token(), document.TopologyRevision(), true, true);
    editor.SetSelection(target);
    const auto token = document.Token();
    const auto revision = document.TopologyRevision();
    const auto undo = document.History().UndoCount();
    const auto dirty = document.Dirty();
    const EditorClient client(editor.Host(), 0);
    const auto unchanged = [&] {
        Check(document.Token() == token && document.TopologyRevision() == revision && document.Dirty() == dirty &&
                  editor.Selection() == target && document.History().UndoCount() == undo && document.History().CanRedo(),
              "message operation changed document, selection or history");
    };
    const auto reply = [&](uint64_t id, const char *choice) {
        return client.Command("message.respond", {IntegerArgument("message", id), TextArgument("choice", choice)});
    };
    Check(client.Query("message.current").status == EDITOR_NO_CHANGE, "empty message queue returned a message");
    const auto first = client.Command("message.show", {TextArgument("text", u8"保存失败\n未修改原文件"),
        TextArgument("title", u8"提示"), IntegerArgument("icon", 6)});
    const auto id = EditorClient::Integer(first, "message");
    Check(first.status == EDITOR_OK && id, "message command did not publish an identity");
    const auto saved = client.Query("message.current");
    Check(EditorClient::Text(saved, "text") == u8"保存失败\n未修改原文件" && EditorClient::Text(saved, "title") == u8"提示" &&
              EditorClient::Integer(saved, "icon") == 6 && !EditorClient::Integer(saved, "cancel"),
          "message projection lost text, title or icon");
    Check(client.Command("message.show", {TextArgument("text", "invalid"), IntegerArgument("icon", 7)}).status == EDITOR_INVALID &&
              client.Command("message.show", {TextArgument("text", "invalid"), IntegerArgument("cancel", 2)}).status == EDITOR_INVALID,
          "invalid message options were enqueued");
    const auto second = client.Command("message.show", {TextArgument("text", "second"), IntegerArgument("cancel", 1)});
    const auto secondId = EditorClient::Integer(second, "message");
    Check(second.status == EDITOR_OK && secondId > id && client.Query("message.current").fields == saved.fields,
          "a later message replaced the first message");
    Check(reply(secondId, "accept").status == EDITOR_STALE && reply(id, "cancel").status == EDITOR_INVALID &&
              reply(id, "unknown").status == EDITOR_INVALID && client.Query("message.current").fields == saved.fields,
          "an invalid or out-of-order answer consumed a message");
    unchanged();
    native = false;
    editor.NativeAvailable(false);
    const auto reads = documentReads;
    Check(client.Query("message.current").fields == saved.fields && reply(id, "accept").status == EDITOR_OK &&
              EditorClient::Integer(client.Query("message.current"), "message") == secondId && documentReads == reads,
          "busy message queries or answers entered the native document");
    Check(reply(id, "accept").status == EDITOR_STALE && reply(secondId, "cancel").status == EDITOR_OK &&
              client.Query("message.current").status == EDITOR_NO_CHANGE &&
              EditorClient::Text(saved, "text") == u8"保存失败\n未修改原文件",
          "message answers replayed or modified an earlier query result");
    unchanged();
    std::string text = "queued copy";
    editor.EnterDraw();
    const auto queued = client.Command("message.show", {TextArgument("text", text)});
    editor.LeaveDraw();
    text.assign(500, 'x');
    Check(queued.status == EDITOR_QUEUED && client.Query("message.current").status == EDITOR_NO_CHANGE,
          "message bypassed the frame command queue");
    editor.EndFrame();
    const auto pending = client.Query("message.current");
    const auto pendingId = EditorClient::Integer(pending, "message");
    Check(client.Completion(queued.operation).status == EDITOR_OK && EditorClient::Text(pending, "text") == "queued copy",
          "queued message borrowed its input text");
    const auto view = editor.OpenScope();
    const EditorClient dialog(editor.Host(), view);
    editor.EnterDraw();
    const auto closing = dialog.Command("message.respond", {IntegerArgument("message", pendingId), TextArgument("choice", "accept")});
    editor.LeaveDraw();
    editor.CloseScope(view);
    editor.EndFrame();
    Check(closing.status == EDITOR_QUEUED && client.Completion(closing.operation).status != EDITOR_OK &&
              client.Query("message.current").fields == pending.fields,
          "closing a dialog answered its application message");
    editor.Attach({}, 0, true, false);
    Check(client.Query("message.current").fields == pending.fields && reply(pendingId, "accept").status == EDITOR_OK,
          "document replacement discarded or invalidated an application message");
    editor.Attach(token, revision, true, true);
    editor.SetSelection(target);
    unchanged();
    Check(client.Command("message.show", {TextArgument("text", "invalid"), TextArgument("format", "unknown")}).status == EDITOR_INVALID &&
          client.Command("message.show", {TextArgument("text", "invalid"), TextArgument("format", "report"),
                                          IntegerArgument("cancel", 1)}).status == EDITOR_INVALID,
          "invalid report display options changed the queue");
    const auto report = client.Command("message.show", {TextArgument("text", "Saved file\nVerified parameters"),
        TextArgument("title", "Save report"), TextArgument("format", "report")});
    const auto reportValue = client.Query("message.current");
    Check(report.status == EDITOR_OK && EditorClient::Text(reportValue, "format") == "report" &&
              EditorClient::Text(reportValue, "title") == "Save report" &&
              reply(EditorClient::Integer(reportValue, "message"), "accept").status == EDITOR_OK,
          "save report did not use the normal message queue and reply");
    unchanged();
    editor.Stop();
}

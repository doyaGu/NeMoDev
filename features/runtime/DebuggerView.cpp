#include "editor/ImGuiDrawing.h"
#include "features/runtime/RuntimeViewData.h"
#include "imgui.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/setup/SetupLayout.h"
#include <algorithm>
#include <cstdlib>

namespace vdev {
namespace runtime {
namespace {
constexpr int REACHED_LIT = 1109, REACHED_DIM = 1110, LOG_SHOW = 1114, LOG_HIDE = 1117;
constexpr double BLINK_SECONDS = 2;
bool ReadContext(const EditorClient &client, EditorResponse &output) {
    EditorResponse next;
    uint64_t first = 0, total = 0, revision = 0;
    do {
        auto page = client.Query("runtime.context", {IntegerArgument("first", first)});
        if (page.status != EDITOR_OK) return false;
        if (!first) { total = Integer(page, "total"); revision = Integer(page, "revision"); }
        if (total > 1000000 || Integer(page, "total") != total || Integer(page, "revision") != revision) return false;
        const auto end = Integer(page, "next");
        if (end > total || end < first || (end == first && first < total)) return false;
        const auto *objects = EditorClient::Find(page, "objects"), *names = EditorClient::Find(page, "object-names"),
                   *scripts = EditorClient::Find(page, "scripts"), *scriptNames = EditorClient::Find(page, "script-names"),
                   *owners = EditorClient::Find(page, "script-owners");
        if (!objects || !names || !scripts || !scriptNames || !owners ||
            objects->objects.size() != names->texts.size() || scripts->objects.size() != scriptNames->texts.size() ||
            scripts->objects.size() != owners->objects.size()) return false;
        if (!first) next = std::move(page);
        else for (auto &target : next.fields)
            if (const auto *source = EditorClient::Find(page, target.key.c_str())) {
                target.objects.insert(target.objects.end(), source->objects.begin(), source->objects.end());
                target.texts.insert(target.texts.end(), source->texts.begin(), source->texts.end());
            }
        first = end;
    } while (first < total);
    output = std::move(next);
    return true;
}
struct DebuggerViewState {
    bool logShown = true;
    std::string maxFramesText, fpsText;
    int lastReached = 0, gotoIndex = -1;
    double blinkStart = -100;
    DocumentToken document{};
    ObjectRef debugObject{}, debugScript{}, currentObject{}, currentScript{};
    uint64_t revision = 1;
    EditorResponse context;
    EditorResponse state;
    uint64_t stateRevision = 0;
    uint64_t contextRevision = 0;
    void Draw(const EditorClient &, EditorViewFrame &);
};
void DebuggerViewState::Draw(const EditorClient &client, EditorViewFrame &frame) {
    const float s = setup::PageUnit();
    vi::DialogLayout layout;
    EditorPrepareViewWindow(frame);
    if (!vi::BeginScrollableWindow(frame.window.data, "Script Debugger", {680 * s, 460 * s}, s, layout)) {
        vi::EndWindow();
        return;
    }
    const auto published = client.Revision("runtime.state");
    if (!published || published != stateRevision) { state = client.Query("runtime.state"); stateRevision = published; }
    const bool available = state.status == EDITOR_OK && Integer(state, "available");
    const bool usable = available && Ready(client);
    const auto execute = [&](const char *id, const EditorValue *values = nullptr, uint32_t count = 0) {
        return client.Command(id, count ? std::vector<EditorValue>(values, values + count) : std::vector<EditorValue>{});
    };
    const auto boolean = [&](const char *id, bool enabled) {
        client.Command(id, {IntegerArgument("enabled", enabled)});
    };
    if (document != client.Scope().document) {
        document = client.Scope().document;
        debugObject = debugScript = currentObject = currentScript = {};
        maxFramesText.clear(); fpsText.clear();
        lastReached = 0; gotoIndex = -1;
        context = {}; contextRevision = 0;
        ++revision;
    }
    const auto choicesVersion = client.Revision("runtime.context");
    if ((!choicesVersion || choicesVersion != contextRevision) && ReadContext(client, context))
        contextRevision = Integer(context, "revision");
    if (vi::BeginDialogContent("##debugger-content", layout)) {
        {
            setup::SetupColumns columns("##debugger-sections", 3);
            if (columns) {
                columns.Next();
                ImGui::TextUnformatted("Trace Options");
                {
                    setup::SetupForm form("##trace-options");
                    if (form) {
                        form.Row("Debug Inside Frames");
                        bool inside = available && Integer(state, "inside-frames");
                        if (vi::CheckButton("Debug Inside Frames", inside, nullptr, usable))
                            boolean("runtime.set-inside-frames", inside);
                        form.Row("Redraw Schematic Only When Breakpoint Reached");
                        bool redraw = Integer(state, "redraw-at-breakpoint") != 0;
                        if (vi::CheckButton("Redraw Schematic Only When Breakpoint Reached", redraw, nullptr,
                                            usable))
                            boolean("runtime.set-redraw-at-breakpoint", redraw);
                    }
                }
                // Debug Context: the combos show where the debugger is; a box locks it
                // to the object or the Script chosen (the object's combo then fixed;
                // locking a Script locks its owner too, sub_5872B0).
                ImGui::TextUnformatted("Debug Context");
                const bool objectLocked = bool(Object(state, "locked-object"));
                const bool scriptLocked = bool(Object(state, "locked-script"));
                if (available) {
                    const auto nextObject = Object(state, "object"), nextScript = Object(state, "script");
                    if (objectLocked)
                        debugObject = Object(state, "locked-object");
                    else if (nextObject != currentObject)
                        debugObject = nextObject;
                    if (scriptLocked)
                        debugScript = Object(state, "locked-script");
                    else if (nextScript != currentScript)
                        debugScript = nextScript;
                    currentObject = nextObject;
                    currentScript = nextScript;
                }
                std::vector<ObjectRef> owners, scripts;
                std::vector<std::string> ownerNames, scriptNames;
                const auto *objects = EditorClient::Find(context, "objects"),
                           *objectNames = EditorClient::Find(context, "object-names"),
                           *allScripts = EditorClient::Find(context, "scripts"),
                           *allNames = EditorClient::Find(context, "script-names"),
                           *allOwners = EditorClient::Find(context, "script-owners");
                if (objects && objectNames && objects->objects.size() == objectNames->texts.size()) {
                    owners = objects->objects;
                    ownerNames = objectNames->texts;
                }
                if (debugObject && std::find(owners.begin(), owners.end(), debugObject) == owners.end())
                    debugObject = {};
                if (allScripts && allNames && allOwners &&
                    allScripts->objects.size() == allNames->texts.size() &&
                    allScripts->objects.size() == allOwners->objects.size())
                    for (size_t i = 0; i < allScripts->objects.size(); ++i)
                        if (!debugObject || allOwners->objects[i] == debugObject) {
                            scripts.push_back(allScripts->objects[i]);
                            scriptNames.push_back(allNames->texts[i]);
                        }
                if (debugScript && std::find(scripts.begin(), scripts.end(), debugScript) == scripts.end())
                    debugScript = {};
                const auto name = [](ObjectRef object, const std::vector<ObjectRef> &objects,
                                     const std::vector<std::string> &names) {
                    const auto found = std::find(objects.begin(), objects.end(), object);
                    return found == objects.end() ? std::string("none")
                                                  : names[size_t(found - objects.begin())];
                };
                const auto names = [](const std::vector<std::string> &names) {
                    auto result = names;
                    result.insert(result.begin(), "none");
                    return result;
                };
                const auto objectName = name(debugObject, owners, ownerNames);
                const auto scriptName = name(debugScript, scripts, scriptNames);
                {
                    setup::SetupForm form("##debug-context");
                    if (form) {
                        const auto lockSize = ImGui::GetFrameHeight();
                        form.Row("Current Object");
                        auto at = ImGui::GetCursorScreenPos();
                        auto width = std::max(1.f, ImGui::GetContentRegionAvail().x - lockSize -
                                                       ImGui::GetStyle().ItemSpacing.x);
                        ImGui::BeginDisabled(!usable);
                        if (!objectLocked && !scriptLocked) {
                            const int chosen = vi::Combo(
                                "##object", at, {at.x + width, at.y + lockSize}, objectName,
                                [&] { return names(ownerNames); }, 0);
                            if (chosen >= 0)
                                debugObject = chosen == 0 ? ObjectRef{} : owners[size_t(chosen - 1)];
                        } else {
                            ImGui::PushTextWrapPos(at.x + width - ImGui::GetWindowPos().x);
                            ImGui::TextUnformatted(objectName.c_str());
                            ImGui::PopTextWrapPos();
                        }
                        ImGui::SameLine();
                        bool lockObject = objectLocked;
                        if (vi::CheckButton(
                                "##lockobject", {at.x + width + ImGui::GetStyle().ItemSpacing.x, at.y},
                                {at.x + width + ImGui::GetStyle().ItemSpacing.x + lockSize, at.y + lockSize},
                                lockObject, nullptr, usable && !scriptLocked)) {
                            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT,
                                                     lockObject ? debugObject : ObjectRef{}};
                            execute("runtime.lock-object", &target, 1);
                        }
                        ImGui::EndDisabled();
                        form.Row("Current Script");
                        at = ImGui::GetCursorScreenPos();
                        width = std::max(1.f, ImGui::GetContentRegionAvail().x - lockSize -
                                                  ImGui::GetStyle().ItemSpacing.x);
                        ImGui::BeginDisabled(!usable);
                        if (!scriptLocked) {
                            const int chosen = vi::Combo(
                                "##script", at, {at.x + width, at.y + lockSize}, scriptName,
                                [&] { return names(scriptNames); }, 0);
                            if (chosen >= 0)
                                debugScript = chosen == 0 ? ObjectRef{} : scripts[size_t(chosen - 1)];
                        } else {
                            ImGui::PushTextWrapPos(at.x + width - ImGui::GetWindowPos().x);
                            ImGui::TextUnformatted(scriptName.c_str());
                            ImGui::PopTextWrapPos();
                        }
                        ImGui::SameLine();
                        bool lockScript = scriptLocked;
                        if (vi::CheckButton(
                                "##lockscript", {at.x + width + ImGui::GetStyle().ItemSpacing.x, at.y},
                                {at.x + width + ImGui::GetStyle().ItemSpacing.x + lockSize, at.y + lockSize},
                                lockScript, nullptr, usable)) {
                            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT,
                                                     lockScript ? debugScript : ObjectRef{}};
                            execute("runtime.lock-script", &target, 1);
                        }
                        ImGui::EndDisabled();
                        form.Row("Current Behavior");
                        const auto behavior = EditorClient::Text(context, "behavior-name");
                        ImGui::TextWrapped("%s", behavior.empty() ? "none" : behavior.c_str());
                    }
                }

                // Trace Tools: Debug Step and Leave Behavior with Debug Inside Frames.
                columns.Next();
                ImGui::TextUnformatted("Trace Tools");
                const bool stepping = usable && Integer(state, "trace") && Integer(state, "inside-frames");
                if (vi::Button("Debug Step", "Debug Step", stepping))
                    execute("runtime.debug-step");
                if (vi::Button("Leave Behavior", "Leave Behavior", stepping))
                    execute("runtime.leave-behavior");
                if (usable) {
                    setup::SetupForm form("##trace-timing");
                    if (form) {
                        // The text typed is taken when the edit is left.
                        if (maxFramesText.empty())
                            maxFramesText = std::to_string(Integer(state, "max-frames"));
                        if (fpsText.empty())
                            fpsText = std::to_string(static_cast<int>(Real(state, "simulated-fps") + 0.5f));
                        bool done = false;
                        form.Row("Max CK Frames");
                        vi::Edit("##maxframes", maxFramesText, vi::CENTER, &done);
                        if (done) {
                            const EditorValue value{EDITOR_TEXT("value"),
                                                    EDITOR_INTEGER_VALUE,
                                                    {},
                                                    {},
                                                    uint64_t(std::max(0, std::atoi(maxFramesText.c_str())))};
                            execute("runtime.set-max-frames", &value, 1);
                        }
                        if (!ImGui::IsItemActive())
                            maxFramesText = std::to_string(Integer(state, "max-frames"));
                        done = false;
                        form.Row("Simulated FPS");
                        vi::Edit("##fps", fpsText, vi::CENTER, &done);
                        if (done) {
                            const EditorValue value{EDITOR_TEXT("value"),
                                                    EDITOR_REAL_VALUE,
                                                    {},
                                                    {},
                                                    0,
                                                    {},
                                                    {},
                                                    {},
                                                    std::max(1.0, std::atof(fpsText.c_str()))};
                            execute("runtime.set-simulated-fps", &value, 1);
                        }
                        if (!ImGui::IsItemActive())
                            fpsText = std::to_string(static_cast<int>(Real(state, "simulated-fps") + 0.5f));
                    }
                }

                // Breakpoints: Add Breakpoint picks an element in the Schematic;
                // Breakpoint Reached blinks, then stays lit while Goto has reached ones
                // to go to, one after the other (sub_587B10).
                columns.Next();
                ImGui::TextUnformatted("Breakpoints");
                if (vi::Button("Add Breakpoint", "Add Breakpoint", usable))
                    execute("script.pick-breakpoint");
                if (vi::Button("Configure Breakpoints", "Configure Breakpoints", usable))
                    execute("script.configure-breakpoints");
                if (vi::Button("Breakpoint List", "Breakpoint List", usable))
                    execute("script.open-breakpoints");
                const size_t reachedCount = available ? size_t(Integer(state, "reached-breakpoints")) : 0;
                if (usable && int(Integer(state, "reached")) != lastReached) {
                    lastReached = int(Integer(state, "reached"));
                    blinkStart = ImGui::GetTime();
                    gotoIndex = -1;
                }
                bool lit = reachedCount > 0;
                if (lit && ImGui::GetTime() - blinkStart < BLINK_SECONDS)
                    lit = static_cast<int>((ImGui::GetTime() - blinkStart) / 0.25) % 2 == 0;
                ImVec2 size;
                if (ImTextureID bitmap = DevBitmap(lit ? REACHED_LIT : REACHED_DIM, &size)) {
                    ImGui::Image(bitmap, {size.x * s, size.y * s});
                }
                if (vi::Button("Goto", "Goto", reachedCount > 0)) {
                    gotoIndex = (gotoIndex + 1) % int(reachedCount);
                    const auto reached =
                        client.Query("runtime.reached-breakpoint",
                                     {IntegerArgument("index", uint64_t(gotoIndex)),
                                      IntegerArgument("reached", Integer(state, "reached"))});
                    if (reached.status == EDITOR_OK)
                        Show(client, Object(reached, "script"), Object(reached, "behavior"),
                             Object(reached, "object"));
                }
            }
        }
        ImVec2 size;
        // The Debug Log: its button shows or hides it (sub_5866B0 1200).
        if (ImTextureID arrow = DevBitmap(logShown ? LOG_HIDE : LOG_SHOW, &size)) {
            const auto at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddImage(arrow, at, ImVec2(at.x + size.x * s, at.y + size.y * s));
            ImGui::SetCursorScreenPos(at);
            if (ImGui::InvisibleButton("Debug Log",
                                       ImVec2(size.x * s + 6 * s + vi::TextWidth("Debug Log", false),
                                              std::max(size.y * s, ImGui::GetFrameHeight()))))
                logShown = !logShown;
            vi::Text(ImVec2(at.x + size.x * s + 3.0f * s, at.y + (size.y * s - ImGui::GetFontSize()) * 0.5f),
                     ImVec2(at.x + size.x * s + 6 * s + vi::TextWidth("Debug Log", false), at.y + size.y * s),
                     "Debug Log");
        }
        if (logShown) {
            if (vi::Button("Clear", "Clear", usable))
                execute("runtime.clear-log");
            static const std::vector<vi::ListColumn> COLUMNS{{"Debugging Message List", 0.0f}};
            const auto count = available ? Integer(state, "log-entries") : 0;
            const auto logRevision = Integer(state, "log-revision");
            const auto readLog = [&](int index) {
                return client.Query("runtime.log", {IntegerArgument("index", uint64_t(index)),
                                                    IntegerArgument("revision", logRevision)});
            };
            bool twice = false;
            const auto at = ImGui::GetCursorScreenPos();
            const int row = vi::ListView(
                "##debuglog", at, {at.x + ImGui::GetContentRegionAvail().x, at.y + 140 * s}, s, COLUMNS,
                int(count), [&](int r, int) { return EditorClient::Text(readLog(r), "text"); }, nullptr,
                &twice, nullptr, true);
            if (row >= 0 && twice) {
                const auto entry = readLog(row);
                if (entry.status == EDITOR_OK)
                    Show(client, Object(entry, "script"), Object(entry, "behavior"));
            }
        }
    }
    vi::EndDialogContent();
    if (vi::DialogButton("Close", ImVec2(layout.right - 60.0f * s, layout.buttonsY), s))
        frame.close = 1;
    vi::EndWindow();
}

} // namespace
EditorViewDesc DebuggerView() {
    EditorViewDesc view{};
    view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("runtime.debugger"); view.provider = EDITOR_TEXT("builtin.runtime-views");
    view.title = EDITOR_TEXT("Script Debugger"); view.area = EDITOR_APPLICATION;
    view.create = [](void *, const EditorHost *, EditorView) -> void * { return new DebuggerViewState; };
    view.destroy = [](void *data) { delete static_cast<DebuggerViewState *>(data); };
    view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
        auto &view = *static_cast<DebuggerViewState *>(data);
        view.Draw(EditorClient(host, id), *frame); ++view.revision;
    };
    view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &view = *static_cast<DebuggerViewState *>(data);
        const EditorValue values[] = {ObjectArgument("object", view.debugObject), ObjectArgument("script", view.debugScript),
            IntegerArgument("log-visible", view.logShown)};
        return sink.write ? sink.write(sink.data, values, 3) : EDITOR_INVALID;
    };
    view.inspectRevision = [](void *data, const EditorHost *) { return static_cast<DebuggerViewState *>(data)->revision; };
    static const EditorParameter fields[] = {{EDITOR_TEXT("object"), EDITOR_OBJECT}, {EDITOR_TEXT("script"), EDITOR_OBJECT},
        {EDITOR_TEXT("log-visible"), EDITOR_INTEGER_VALUE}};
    view.fields = fields; view.fieldCount = 3;
    return view;
}
} // namespace runtime
} // namespace vdev

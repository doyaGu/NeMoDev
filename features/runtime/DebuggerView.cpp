#include "features/runtime/RuntimeViewData.h"
#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "imgui.h"
#include <algorithm>
#include <cstdlib>

namespace vdev {
namespace runtime {
namespace {
constexpr int REACHED_LIT = 1109, REACHED_DIM = 1110, LOG_SHOW = 1114, LOG_HIDE = 1117;
constexpr float WIDTH = 408, HEIGHT = 222, HEIGHT_NO_LOG = 125, PAD = 7.5f;
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
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 units(WIDTH, logShown ? HEIGHT : HEIGHT_NO_LOG);
    const ImVec2 windowSize = vi::TemplateClient(units, s, PAD);
    ImVec2 o;
    EditorPrepareViewWindow(frame);
    if (!vi::BeginWindow(frame.window.data, "Script Debugger", windowSize, s, o)) {
        vi::EndWindow();
        return;
    }
    const vi::TemplateLayout l = vi::LayoutTemplate(o, units, s, PAD);
    vi::TemplatePanel(l);
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
    auto text = [&](float x, float y, float w, float h, const char *label, bool bold = false) {
        vi::Text(l.Min(x, y), l.Max(x, y, w, h), label, bold);
    };

    // Trace Options.
    text(6, 4, 82, 8, "Trace Options", true);
    vi::Frame(l.Min(5, 14), l.Max(5, 14, 182, 31));
    // Debug Inside Frames: the full debug mode (it needs Trace Mode, turned on with it).
    bool inside = available && Integer(state, "inside-frames");
    if (vi::CheckButton("Debug Inside Frames", l.Min(8, 18), l.Max(8, 18, 83, 10), inside, "Debug Inside Frames", usable)) {
        boolean("runtime.set-inside-frames", inside);
    }
    bool redraw = Integer(state, "redraw-at-breakpoint") != 0;
    if (vi::CheckButton("Redraw Schematic Only When Breakpoint Reached", l.Min(8, 32), l.Max(8, 32, 177, 8),
                        redraw, "Redraw Schematic Only When Breakpoint Reached", usable))
        boolean("runtime.set-redraw-at-breakpoint", redraw);

    // Debug Context: the combos show where the debugger is; a box locks it
    // to the object or the Script chosen (the object's combo then fixed;
    // locking a Script locks its owner too, sub_5872B0).
    text(5, 49, 82, 8, "Debug Context", true);
    vi::Frame(l.Min(5, 59), l.Max(5, 59, 182, 49));
    text(9, 65, 57, 8, "Current Object");
    text(9, 80, 57, 8, "Current Script");
    text(9, 95, 57, 8, "Current Behavior");
    const bool objectLocked = bool(Object(state, "locked-object"));
    const bool scriptLocked = bool(Object(state, "locked-script"));
    if (available) {
        const auto nextObject = Object(state, "object"), nextScript = Object(state, "script");
        if (objectLocked) debugObject = Object(state, "locked-object");
        else if (nextObject != currentObject) debugObject = nextObject;
        if (scriptLocked) debugScript = Object(state, "locked-script");
        else if (nextScript != currentScript) debugScript = nextScript;
        currentObject = nextObject; currentScript = nextScript;
    }
    std::vector<ObjectRef> owners, scripts;
    std::vector<std::string> ownerNames, scriptNames;
    const auto *objects = EditorClient::Find(context, "objects"), *objectNames = EditorClient::Find(context, "object-names"),
               *allScripts = EditorClient::Find(context, "scripts"), *allNames = EditorClient::Find(context, "script-names"),
               *allOwners = EditorClient::Find(context, "script-owners");
    if (objects && objectNames && objects->objects.size() == objectNames->texts.size()) {
        owners = objects->objects; ownerNames = objectNames->texts;
    }
    if (debugObject && std::find(owners.begin(), owners.end(), debugObject) == owners.end()) debugObject = {};
    if (allScripts && allNames && allOwners && allScripts->objects.size() == allNames->texts.size() &&
        allScripts->objects.size() == allOwners->objects.size())
        for (size_t i = 0; i < allScripts->objects.size(); ++i)
            if (!debugObject || allOwners->objects[i] == debugObject) {
                scripts.push_back(allScripts->objects[i]); scriptNames.push_back(allNames->texts[i]);
            }
    if (debugScript && std::find(scripts.begin(), scripts.end(), debugScript) == scripts.end()) debugScript = {};
    const auto name = [](ObjectRef object, const std::vector<ObjectRef> &objects, const std::vector<std::string> &names) {
        const auto found = std::find(objects.begin(), objects.end(), object);
        return found == objects.end() ? std::string("none") : names[size_t(found - objects.begin())];
    };
    const auto names = [](const std::vector<std::string> &names) {
        auto result = names; result.insert(result.begin(), "none"); return result;
    };
    const auto objectName = name(debugObject, owners, ownerNames);
    const auto scriptName = name(debugScript, scripts, scriptNames);
    ImGui::BeginDisabled(!usable);
    if (!objectLocked && !scriptLocked) {
        const int chosen = vi::Combo("##object", l.Min(73, 65), l.Max(73, 65, 94, 12), objectName,
                                     [&] { return names(ownerNames); }, 0);
        if (chosen >= 0) debugObject = chosen == 0 ? ObjectRef{} : owners[size_t(chosen - 1)];
    } else {
        vi::Text(ImVec2(l.Min(73, 65).x + 3.0f * s, l.Min(73, 65).y + 2.0f * s), l.Max(73, 65, 94, 12),
                 objectName.c_str());
        ImGui::GetWindowDrawList()->AddLine(ImVec2(l.Min(73, 65).x, l.Max(73, 65, 94, 12).y - 1.5f),
                                            ImVec2(l.Max(73, 65, 94, 12).x, l.Max(73, 65, 94, 12).y - 1.5f), vi::Color(4));
    }
    bool lockObject = objectLocked;
    if (vi::CheckButton("##lockobject", l.Min(175, 65), l.Max(175, 65, 8, 8), lockObject, nullptr, usable && !scriptLocked)) {
        const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, lockObject ? debugObject : ObjectRef{}};
        execute("runtime.lock-object", &target, 1);
    }
    if (!scriptLocked) {
        const int chosen = vi::Combo("##script", l.Min(73, 80), l.Max(73, 80, 94, 12), scriptName,
                                     [&] { return names(scriptNames); }, 0);
        if (chosen >= 0) debugScript = chosen == 0 ? ObjectRef{} : scripts[size_t(chosen - 1)];
    } else {
        vi::Text(ImVec2(l.Min(73, 80).x + 3.0f * s, l.Min(73, 80).y + 2.0f * s), l.Max(73, 80, 94, 12),
                 scriptName.c_str());
    }
    bool lockScript = scriptLocked;
    if (vi::CheckButton("##lockscript", l.Min(175, 80), l.Max(175, 80, 8, 8), lockScript, nullptr, usable)) {
        const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, lockScript ? debugScript : ObjectRef{}};
        execute("runtime.lock-script", &target, 1);
    }
    ImGui::EndDisabled();
    const auto behavior = EditorClient::Text(context, "behavior-name");
    text(73, 95, 94, 12, behavior.empty() ? "none" : behavior.c_str());

    // Trace Tools: Debug Step and Leave Behavior with Debug Inside Frames.
    text(192, 4, 82, 8, "Trace Tools", true);
    vi::Frame(l.Min(192, 14), l.Max(192, 14, 99, 94));
    const bool stepping = usable && Integer(state, "trace") && Integer(state, "inside-frames");
    if (vi::Button("Debug Step", l.Min(196, 18), l.Max(196, 18, 57, 10), "Debug Step", stepping)) execute("runtime.debug-step");
    if (vi::Button("Leave Behavior", l.Min(196, 32), l.Max(196, 32, 57, 10), "Leave Behavior", stepping))
        execute("runtime.leave-behavior");
    text(196, 50, 50, 8, "Max CK Frames");
    text(196, 65, 50, 8, "Simulated FPS");
    if (usable) {
        // The text typed is taken when the edit is left.
        if (maxFramesText.empty()) maxFramesText = std::to_string(Integer(state, "max-frames"));
        if (fpsText.empty()) fpsText = std::to_string(static_cast<int>(Real(state, "simulated-fps") + 0.5f));
        bool done = false;
        vi::Edit("##maxframes", l.Min(253, 50), l.Max(253, 50, 33, 12), maxFramesText, vi::CENTER, &done);
        if (done) {
            const EditorValue value{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(std::max(0, std::atoi(maxFramesText.c_str())))};
            execute("runtime.set-max-frames", &value, 1);
        }
        if (!ImGui::IsItemActive()) maxFramesText = std::to_string(Integer(state, "max-frames"));
        done = false;
        vi::Edit("##fps", l.Min(253, 65), l.Max(253, 65, 33, 12), fpsText, vi::CENTER, &done);
        if (done) {
            const EditorValue value{EDITOR_TEXT("value"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, std::max(1.0, std::atof(fpsText.c_str()))};
            execute("runtime.set-simulated-fps", &value, 1);
        }
        if (!ImGui::IsItemActive()) fpsText = std::to_string(static_cast<int>(Real(state, "simulated-fps") + 0.5f));
    }

    // Breakpoints: Add Breakpoint picks an element in the Schematic;
    // Breakpoint Reached blinks, then stays lit while Goto has reached ones
    // to go to, one after the other (sub_587B10).
    text(296, 4, 82, 8, "Breakpoints", true);
    vi::Frame(l.Min(296, 14), l.Max(296, 14, 106, 94));
    if (vi::Button("Add Breakpoint", l.Min(303, 18), l.Max(303, 18, 94, 10), "Add Breakpoint", usable))
        execute("script.pick-breakpoint");
    if (vi::Button("Configure Breakpoints", l.Min(303, 30), l.Max(303, 30, 94, 10), "Configure Breakpoints", usable))
        execute("script.configure-breakpoints");
    if (vi::Button("Breakpoint List", l.Min(303, 65), l.Max(303, 65, 94, 10), "Breakpoint List", usable))
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
        const ImVec2 at = l.Min(303, 79);
        ImGui::GetWindowDrawList()->AddImage(bitmap, at, ImVec2(at.x + size.x * s, at.y + size.y * s));
    }
    if (vi::Button("Goto", l.Min(324, 95), l.Max(324, 95, 50, 10), "Goto", reachedCount > 0)) {
        gotoIndex = (gotoIndex + 1) % int(reachedCount);
        const auto reached = client.Query("runtime.reached-breakpoint", {IntegerArgument("index", uint64_t(gotoIndex)),
            IntegerArgument("reached", Integer(state, "reached"))});
        if (reached.status == EDITOR_OK) Show(client, Object(reached, "script"), Object(reached, "behavior"), Object(reached, "object"));
    }

    // The Debug Log: its button shows or hides it (sub_5866B0 1200).
    if (ImTextureID arrow = DevBitmap(logShown ? LOG_HIDE : LOG_SHOW, &size)) {
        const ImVec2 at(l.Min(4, 114).x, l.Min(4, 114).y - 3.0f * s);
        ImGui::GetWindowDrawList()->AddImage(arrow, at, ImVec2(at.x + size.x * s, at.y + size.y * s));
        ImGui::SetCursorScreenPos(at);
        if (ImGui::InvisibleButton("Debug Log", ImVec2(66.0f * 1.5f * s, size.y * s))) logShown = !logShown;
        vi::Text(ImVec2(at.x + size.x * s + 3.0f * s, at.y + (size.y * s - ImGui::GetFontSize()) * 0.5f),
                 ImVec2(at.x + 66.0f * 1.5f * s, at.y + size.y * s), "Debug Log");
    }
    if (logShown) {
        if (vi::Button("Clear", l.Min(82, 113), l.Max(82, 113, 50, 10), "Clear", usable)) execute("runtime.clear-log");
        static const std::vector<vi::ListColumn> COLUMNS{{"Debugging Message List", 0.0f}};
        const auto count = available ? Integer(state, "log-entries") : 0;
        const auto logRevision = Integer(state, "log-revision");
        const auto readLog = [&](int index) {
            return client.Query("runtime.log", {IntegerArgument("index", uint64_t(index)), IntegerArgument("revision", logRevision)});
        };
        bool twice = false;
        const int row = vi::ListView("##debuglog", l.Min(5, 125), l.Max(5, 125, 398, 93), s, COLUMNS,
            int(count), [&](int r, int) { return EditorClient::Text(readLog(r), "text"); }, nullptr, &twice, nullptr, true);
        if (row >= 0 && twice) {
            const auto entry = readLog(row);
            if (entry.status == EDITOR_OK) Show(client, Object(entry, "script"), Object(entry, "behavior"));
        }
    }

    if (vi::DialogButton("Close", ImVec2(l.right - 60.0f * s, l.buttonsY), s)) frame.close = 1;
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

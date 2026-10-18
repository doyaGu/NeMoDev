#include "ui/app/CommandLine.h"


#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <filesystem>
#include <sstream>

#ifdef VIRTOOLSDEV_UI_TESTS
#include "imgui.h"
#endif

#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace fs = std::filesystem;
namespace {
std::wstring AbsolutePath(const std::wstring &path) {
    return Utf8ToWide(fs::absolute(fs::u8path(WideToUtf8(path))).u8string());
}
}

void CommandLine::Parse(const std::vector<std::wstring> &args) {
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == L"--runtime") {
            if (i + 1 < args.size()) ++i;
            else badOption = args[i];
#ifdef VIRTOOLSDEV_UI_TESTS
        } else if (args[i] == L"--capture" && i + 1 < args.size()) {
            capturePath = AbsolutePath(args[++i]);
        } else if (args[i] == L"--drag" && i + 4 < args.size()) {
            drag = true;
            dragFrom[0] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
            dragFrom[1] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
            dragTo[0] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
            dragTo[1] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
        } else if (args[i] == L"--right-click" && i + 2 < args.size()) {
            rightClick = true;
            rightAt[0] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
            rightAt[1] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
        } else if (args[i] == L"--then-click" && i + 2 < args.size()) {
            thenClick = true;
            thenAt[0] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
            thenAt[1] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
        } else if (args[i] == L"--keys" && i + 1 < args.size()) {
            std::wstringstream list(args[++i]);
            for (std::wstring key; list >> key;) keys.push_back(key);
        } else if (args[i] == L"--double-click" && i + 2 < args.size()) {
            doubleClick = true;
            clickAt[0] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
            clickAt[1] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
        } else if (args[i] == L"--wheel" && i + 3 < args.size()) {
            wheel = true;
            wheelAt[0] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
            wheelAt[1] = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
            wheelNotches = std::wcstol(args[++i].c_str(), nullptr, 10);
            if (i + 1 < args.size() && (args[i + 1] == L"ctrl" || args[i + 1] == L"shift")) wheelModifier = args[++i];
        } else if (args[i] == L"--frames" && i + 1 < args.size()) {
            captureFrames = std::max(2, static_cast<int>(std::wcstol(args[++i].c_str(), nullptr, 10)));
        } else if (args[i] == L"--open-scripts") {
            startup.openScripts = true;
        } else if (args[i] == L"--expand-graphs") {
            startup.expandGraphs = true;
        } else if (args[i] == L"--all-scripts") {
            startup.showAllScripts = true;
        } else if (args[i] == L"--owner-scripts") {
            startup.showAllScripts = false;
        } else if (args[i] == L"--fit") {
            startup.fit = true;
        } else if (args[i] == L"--insert-block" && i + 1 < args.size()) {
            startup.insertBlock = WideToAnsi(args[++i]);
        } else if (args[i] == L"--messages") {
            startup.messages = true;
        } else if (args[i] == L"--expand-level") {
            startup.expandLevel = true;
        } else if (args[i] == L"--new") {
            startup.newComposition = true;
        } else if ((args[i] == L"--import" || args[i] == L"--import-character" || args[i] == L"--import-place" ||
                    args[i] == L"--import-scene") &&
                   i + 1 < args.size()) {
            // Document::ImportMode: File, Character, Place, Scene.
            const int mode = args[i] == L"--import-character" ? 1 : args[i] == L"--import-place" ? 2
                             : args[i] == L"--import-scene"   ? 3
                                                              : 0;
            startup.imports.emplace_back(WideToUtf8(AbsolutePath(args[++i])), mode);
        } else if (args[i] == L"--resource" && i + 1 < args.size()) {
            startup.resources.push_back(WideToUtf8(AbsolutePath(args[++i])));
        } else if (args[i] == L"--resource-folder" && i + 1 < args.size()) {
            startup.resourceFolder = WideToUtf8(args[++i]);
        } else if (args[i] == L"--tool" && i + 1 < args.size()) {
            startup.tools.push_back(WideToUtf8(args[++i]));
        } else if (args[i] == L"--layout-tool" && i + 1 < args.size()) {
            startup.layoutTool = WideToAnsi(args[++i]);
        } else if (args[i] == L"--layout-view" && i + 1 < args.size()) {
            startup.layoutView = WideToAnsi(args[++i]);
        } else if (args[i] == L"--layout-select" && i + 1 < args.size()) {
            startup.layoutSelect.push_back(WideToAnsi(args[++i]));
        } else if (args[i] == L"--layout-group" && i + 1 < args.size()) {
            startup.layoutGroup = WideToAnsi(args[++i]);
        } else if (args[i] == L"--layout-debug") {
            startup.layoutDebug = true;
        } else if (args[i] == L"--layout-command" && i + 1 < args.size()) {
            startup.layoutCommands.push_back(std::wcstol(args[++i].c_str(), nullptr, 10));
        } else if (args[i] == L"--drop-block" && i + 3 < args.size()) {
            startup.dropBlock = WideToAnsi(args[++i]);
            startup.dropX = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
            startup.dropY = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
        } else if ((args[i] == L"--merge" || args[i] == L"--merge-scene") && i + 1 < args.size()) {
            const bool asScene = args[i] == L"--merge-scene";
            startup.merges.emplace_back(WideToUtf8(AbsolutePath(args[++i])), asScene);
        } else if (args[i] == L"--create" && i + 1 < args.size()) {
            startup.creates.push_back(WideToAnsi(args[++i]));
        } else if (args[i] == L"--setup" && i + 1 < args.size()) {
            startup.setups.push_back(WideToAnsi(args[++i]));
        } else if (args[i] == L"--play" && i + 1 < args.size()) {
            startup.playFrames = std::wcstol(args[++i].c_str(), nullptr, 10);
        } else if (args[i] == L"--menu-at" && i + 2 < args.size()) {
            startup.menuAt = true;
            startup.menuX = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
            startup.menuY = static_cast<float>(std::wcstod(args[++i].c_str(), nullptr));
        } else if (args[i] == L"--close-script" && i + 1 < args.size()) {
            startup.closeScripts.push_back(WideToUtf8(args[++i]));
        } else if (args[i] == L"--zoom-script" && i + 2 < args.size()) {
            const std::string name = WideToUtf8(args[++i]);
            startup.zoomScripts.emplace_back(name, static_cast<float>(std::wcstod(args[++i].c_str(), nullptr)));
        } else if (args[i] == L"--edit-parameters" && i + 1 < args.size()) {
            startup.editParameters = WideToAnsi(args[++i]);
        } else if (args[i] == L"--edit-value" && i + 1 < args.size()) {
            startup.editValue = WideToAnsi(args[++i]);
        } else if (args[i] == L"--focus-window" && i + 1 < args.size()) {
            startup.focusWindow = WideToUtf8(args[++i]);
        } else if (args[i] == L"--show-script" && i + 1 < args.size()) {
            startup.showScript = WideToUtf8(args[++i]);
        } else if (args[i] == L"--isolated") {
            startup.isolated = true;
        } else if (args[i] == L"--ui-tests" && i + 1 < args.size()) {
            uiTests = WideToUtf8(args[++i]);
            startup.isolated = true;
        } else if (args[i] == L"--ui-results" && i + 1 < args.size()) {
            uiResults = AbsolutePath(args[++i]);
        } else if (args[i] == L"--ui-show") {
            uiShow = true;
#endif
        } else if (args[i].rfind(L"--", 0) == 0) {
            // A mistyped option, or one without its values: not a file to open.
            if (badOption.empty()) badOption = args[i];
        } else {
            fileToOpen = AbsolutePath(args[i]);
        }
    }
}

#ifdef VIRTOOLSDEV_UI_TESTS
void CommandLine::FeedInput(int frameCount, EditorApp &app) const {
    const bool capture = Capture();
        if (capture && drag) {
            // Move, press, drag in steps, release, then report the last undo step.
            ImGuiIO &input = ImGui::GetIO();
            const int step = frameCount - DRAG_START;
            if (step >= 0 && step <= DRAG_STEPS + 4) {
                const float t = std::min(1.0f, std::max(0.0f, (step - 2) / static_cast<float>(DRAG_STEPS)));
                input.AddMousePosEvent(dragFrom[0] + (dragTo[0] - dragFrom[0]) * t, dragFrom[1] + (dragTo[1] - dragFrom[1]) * t);
                if (step == 1) input.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                if (step == DRAG_STEPS + 3) input.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            }
            if (step == DRAG_STEPS + 6) app.LogUndoTop();
        }
        if (capture && doubleClick) {
            // Press and release twice within ImGui's double-click time.
            ImGuiIO &input = ImGui::GetIO();
            const int step = frameCount - DRAG_START;
            if (step >= 0 && step <= 5) {
                input.AddMousePosEvent(clickAt[0], clickAt[1]);
                if (step == 1 || step == 3) input.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                if (step == 2 || step == 4) input.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            }
        }
        if (capture && rightClick) {
            ImGuiIO &input = ImGui::GetIO();
            const int step = frameCount - DRAG_START;
            if (step >= 0 && step <= 3) {
                input.AddMousePosEvent(rightAt[0], rightAt[1]);
                if (step == 1) input.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
                if (step == 2) input.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
            }
        }
        if (capture && thenClick) {
            ImGuiIO &input = ImGui::GetIO();
            const int step = frameCount - DRAG_START - 20;
            if (step >= 0 && step <= 3) {
                input.AddMousePosEvent(thenAt[0], thenAt[1]);
                if (step == 1) input.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                if (step == 2) input.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            }
        }
        if (capture && !keys.empty()) {
            ImGuiIO &input = ImGui::GetIO();
            const int step = frameCount - DRAG_START - 30;
            if (step >= 0 && step % 3 != 2 && step / 3 < static_cast<int>(keys.size())) {
                std::wstring key = keys[static_cast<size_t>(step / 3)];
                const bool down = step % 3 == 0;
                bool shift = false, ctrl = false, alt = false;
                for (;;) {
                    if (key.rfind(L"shift+", 0) == 0) shift = true, key = key.substr(6);
                    else if (key.rfind(L"ctrl+", 0) == 0) ctrl = true, key = key.substr(5);
                    else if (key.rfind(L"alt+", 0) == 0) alt = true, key = key.substr(4);
                    else break;
                }
                static const std::pair<const wchar_t *, ImGuiKey> NAMES[] = {
                    {L"down", ImGuiKey_DownArrow}, {L"up", ImGuiKey_UpArrow},     {L"left", ImGuiKey_LeftArrow},
                    {L"right", ImGuiKey_RightArrow}, {L"home", ImGuiKey_Home},    {L"end", ImGuiKey_End},
                    {L"pageup", ImGuiKey_PageUp},   {L"pagedown", ImGuiKey_PageDown}, {L"space", ImGuiKey_Space},
                    {L"enter", ImGuiKey_Enter},     {L"escape", ImGuiKey_Escape}, {L"delete", ImGuiKey_Delete}};
                ImGuiKey named = ImGuiKey_None;
                for (const auto &name : NAMES)
                    if (key == name.first) named = name.second;
                if (named == ImGuiKey_None && key.size() >= 2 && key[0] == L'f') {
                    const int number = std::wcstol(key.c_str() + 1, nullptr, 10);
                    if (number >= 1 && number <= 12) named = static_cast<ImGuiKey>(ImGuiKey_F1 + number - 1);
                }
                if (shift) input.AddKeyEvent(ImGuiMod_Shift, down);
                if (ctrl) input.AddKeyEvent(ImGuiMod_Ctrl, down);
                if (alt) input.AddKeyEvent(ImGuiMod_Alt, down);
                if (named != ImGuiKey_None) {
                    input.AddKeyEvent(named, down);
                    if (named == ImGuiKey_Space && down && !ctrl && !alt) input.AddInputCharacter(' ');
                } else if (key.size() == 1) {
                    const wchar_t c = key[0];
                    if (c >= L'a' && c <= L'z') input.AddKeyEvent(static_cast<ImGuiKey>(ImGuiKey_A + (c - L'a')), down);
                    if (down && !ctrl && !alt) input.AddInputCharacter(shift ? towupper(c) : c);
                }
            }
        }
        if (capture && wheel) {
            ImGuiIO &input = ImGui::GetIO();
            const int step = frameCount - DRAG_START;
            const int count = wheelNotches < 0 ? -wheelNotches : wheelNotches;
            if (step >= 0 && step <= count + 1) {
                input.AddMousePosEvent(wheelAt[0], wheelAt[1]);
                const ImGuiKey key = wheelModifier == L"ctrl" ? ImGuiMod_Ctrl : wheelModifier == L"shift" ? ImGuiMod_Shift : ImGuiKey_None;
                if (key != ImGuiKey_None) input.AddKeyEvent(key, step <= count);
                if (step >= 1 && step <= count) input.AddMouseWheelEvent(0.0f, wheelNotches < 0 ? -1.0f : 1.0f);
            }
        }
}

#endif
} // namespace vdev

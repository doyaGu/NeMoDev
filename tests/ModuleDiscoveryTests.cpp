#include "application/EditorModules.h"
#include "workspace/EditorClient.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
namespace {
void Check(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
void Environment(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}
struct Variable {
    const char *name;
    std::optional<std::string> before;
    Variable(const char *name, const std::string &value) : name(name) {
        if (const auto *old = std::getenv(name)) before = old;
        Environment(name, value.c_str());
    }
    ~Variable() { Environment(name, before ? before->c_str() : nullptr); }
};
std::vector<std::string> Lines(const fs::path &path) {
    std::ifstream input(path);
    std::vector<std::string> out;
    for (std::string line; std::getline(input, line);) out.push_back(line);
    return out;
}
void Record(const fs::path &path, const char *text) { std::ofstream(path, std::ios::app) << text << '\n'; }
constexpr EditorDrawingVersion Drawing{1, 2, 3, 4, 5};
struct Embedded {
    fs::path log;
    int stops = 0;
    vdev::EditorModuleSource Source() {
        return {"linked fixture", [this](vdev::EditorApplication &editor, std::string &error) {
            EditorModuleDesc desc{}; desc.size = sizeof(desc); desc.version = EDITOR_VERSION;
            desc.id = EDITOR_TEXT("fixture.embedded"); desc.data = this;
            desc.stop = [](void *data) {
                auto &self = *static_cast<Embedded *>(data); ++self.stops;
                Record(self.log, "stop fixture.embedded");
            };
            return editor.Register(desc, error);
        }};
    }
};
void CheckStopped(const fs::path &log, int expected = 1) {
    const auto lines = Lines(log);
    for (const auto *name : {"fixture.addon", "fixture.base", "fixture.embedded"})
        Check(std::count(lines.begin(), lines.end(), std::string("stop ") + name) == expected,
              std::string("wrong stop count for ") + name);
    const auto addon = std::find(lines.begin(), lines.end(), "stop fixture.addon");
    const auto base = std::find(lines.begin(), lines.end(), "stop fixture.base");
    const auto embedded = std::find(lines.begin(), lines.end(), "stop fixture.embedded");
    Check(addon < base && base < embedded, "modules did not stop in reverse dependency order");
    const auto firstUnload = std::find_if(lines.begin(), lines.end(), [](const auto &line) { return line.rfind("unload ", 0) == 0; });
    Check(embedded < firstUnload, "module code unloaded before all stop callbacks finished");
}
}

int main(int argc, char **argv) {
    const auto root = fs::temp_directory_path() / ("nemodev-module-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Check(argc == 4, "expected base, addon and missing-entry module files");
        const auto directory = root / fs::u8path(u8"中文 modules");
        fs::create_directories(directory);
        const auto suffix = fs::u8path(argv[1]).extension().u8string();
        const auto base = directory / ("z base" + suffix), addon = directory / ("a addon" + suffix);
        fs::copy_file(fs::u8path(argv[1]), base); fs::copy_file(fs::u8path(argv[2]), addon);
        std::ofstream(directory / "readme.txt") << "not executable";
        fs::create_directories(directory / ("nested" + suffix));
        fs::copy_file(base, directory / ("nested" + suffix) / ("ignored" + suffix));
        std::string error;
        std::vector<vdev::EditorModuleSource> discovered;
        Check(vdev::DiscoverEditorModules({{(root / "optional").u8string()}, {directory.u8string(), true}},
              {base.u8string()}, discovered, error) && discovered.size() == 2, error);
        Check(discovered.front().name == fs::canonical(addon).u8string(), "discovery order depends on filesystem enumeration");
        std::error_code linkError;
        const auto alias = root / ("alias" + suffix);
        fs::create_hard_link(base, alias, linkError);
        if (!linkError) {
            Check(vdev::DiscoverEditorModules({{directory.u8string()}}, {alias.u8string(), base.u8string()}, discovered, error) &&
                  discovered.size() == 2, "hard-link alias loaded twice");
        }
        const auto symbolic = root / ("symbolic" + suffix);
        fs::create_symlink(base, symbolic, linkError);
        if (!linkError) {
            Check(vdev::DiscoverEditorModules({{directory.u8string()}}, {symbolic.u8string()}, discovered, error) &&
                  discovered.size() == 2, "symbolic-link alias loaded twice");
        }
        const auto before = discovered.front().name;
        Check(!vdev::DiscoverEditorModules({{base.u8string(), true}}, {}, discovered, error) &&
              discovered.size() == 2, "a file was accepted as a module directory");
        Check(!vdev::DiscoverEditorModules({{(root / "missing").u8string(), true}}, {}, discovered, error) &&
              discovered.size() == 2 && discovered.front().name == before, "missing required directory changed the result");
        Check(!vdev::DiscoverEditorModules({}, {(root / "missing-file").u8string()}, discovered, error) &&
              discovered.size() == 2, "missing explicit file changed the result");
        Check(vdev::EditorModuleDirectoryAt((root / "Bin").u8string()) == (root / "Bin/modules").u8string(), "flat module layout");
        for (const auto &path : {root / "Moved App.app", root / "Moved App.app/Contents/MacOS", root / "Moved App.app/Contents/Resources"})
            Check(vdev::EditorModuleDirectoryAt(path.u8string()) == (root / "Moved App.app/Contents/PlugIns/EditorModules").u8string(),
                  "relocated app module layout");

        const auto log = root / "lifecycle.txt";
        Variable logging("NEMODEV_MODULE_LOG", log.u8string());
        Variable failure("NEMODEV_MODULE_FAILURE", "");
        const auto sources = [&](Embedded &linked) {
            auto result = discovered; result.insert(result.begin(), linked.Source()); return result;
        };
        {
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            auto all = sources(linked);
            bool wrongThread = true;
            std::thread other([&] { std::string message; wrongThread = editor.Assemble(all, Drawing, message); });
            other.join(); Check(!wrongThread && Lines(log).empty(), "wrong-thread assembly ran module code");
            Check(editor.Assemble(all, Drawing, error), error);
            const auto value = vdev::EditorClient(editor.Host(), 0).Query("fixture.addon.value");
            Check(value.status == EDITOR_OK && vdev::EditorClient::Integer(value, "value") == 42, "discovered query was not activated");
            Check(!editor.Assemble(all, Drawing, error) && linked.stops == 0, "late assembly changed active modules");
            editor.Stop(); editor.Stop(); Check(linked.stops == 1, "stop was not idempotent");
        }
        CheckStopped(log);
        for (const auto *kind : {"broken", "no-entry"}) {
            const auto file = root / (std::string(kind) + suffix);
            if (std::string(kind) == "broken") std::ofstream(file) << "not a library";
            else fs::copy_file(fs::u8path(argv[3]), file);
            std::vector<vdev::EditorModuleSource> invalid;
            Check(vdev::DiscoverEditorModules({}, {file.u8string()}, invalid, error), error);
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            invalid.insert(invalid.begin(), linked.Source());
            Check(!editor.Assemble(invalid, Drawing, error) && error.find(kind) != std::string::npos && linked.stops == 1,
                  std::string("bad library did not roll back: ") + kind);
            Check(editor.Assemble({linked.Source()}, Drawing, error), error); editor.Stop();
            Check(linked.stops == 2, "bad library prevented a linked-module retry");
        }
        for (const auto *mode : {"entry-error", "entry-throw", "bad-version", "duplicate-id", "missing", "cycle"}) {
            std::ofstream(log, std::ios::trunc).close();
            Environment("NEMODEV_MODULE_FAILURE", mode);
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            Check(!editor.Assemble(sources(linked), Drawing, error) && !error.empty() && linked.stops == 1,
                  std::string("assembly failure did not clean linked modules: ") + mode);
            Check(vdev::EditorClient(editor.Host(), 0).Query("fixture.addon.value").status != EDITOR_OK,
                  "failed assembly left a callable query");
            const auto failed = Lines(log);
            Check(std::count(failed.begin(), failed.end(), "stop fixture.addon") == 1, "failed entry was not stopped exactly once");
            Environment("NEMODEV_MODULE_FAILURE", "");
            Check(editor.Assemble(sources(linked), Drawing, error), "assembly cannot retry after cleanup: " + error);
            editor.Stop(); Check(linked.stops == 2, "retry did not transfer module lifetime");
        }
        std::ofstream(log, std::ios::trunc).close();
        Environment("NEMODEV_MODULE_FAILURE", "stop-throw");
        {
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            Check(editor.Assemble(sources(linked), Drawing, error), error); editor.Stop();
        }
        CheckStopped(log);
        // A source may register a descriptor and fail afterward. The batch
        // owns that accepted descriptor and removes it before a clean retry.
        {
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            auto source = linked.Source();
            vdev::EditorModuleSource partial{"partial linked module", [&](auto &app, auto &message) {
                Check(source.collect(app, message), message); throw std::runtime_error("after registration"); return false;
            }};
            Check(!editor.Assemble({partial}, Drawing, error) && linked.stops == 1, "partial linked source survived failure");
            Check(editor.Assemble({source}, Drawing, error), error); editor.Stop();
            Check(linked.stops == 2, "linked module retry changed ownership");
        }
        {
            vdev::EditorApplication editor([] { return nullptr; });
            int keptStops = 0;
            EditorModuleDesc kept{}; kept.size = sizeof(kept); kept.version = EDITOR_VERSION;
            kept.id = EDITOR_TEXT("fixture.kept"); kept.data = &keptStops;
            kept.stop = [](void *data) { ++*static_cast<int *>(data); };
            Check(editor.Register(kept, error), error);
            struct Probe { vdev::EditorApplication *editor; bool refused = false; } probe{&editor};
            vdev::EditorModuleSource cleanup{"cleanup reentry", [&](auto &app, auto &message) {
                EditorModuleDesc desc{}; desc.size = sizeof(desc); desc.version = EDITOR_VERSION;
                desc.id = EDITOR_TEXT("fixture.cleanup"); desc.data = &probe;
                desc.stop = [](void *data) {
                    auto &probe = *static_cast<Probe *>(data);
                    std::string error;
                    EditorModuleDesc nested{}; nested.size = sizeof(nested); nested.version = EDITOR_VERSION;
                    nested.id = EDITOR_TEXT("fixture.reentrant");
                    probe.refused = !probe.editor->Register(nested, error) &&
                        !probe.editor->Assemble({}, Drawing, error) && !probe.editor->Activate(Drawing, error);
                    probe.editor->Stop();
                };
                return app.Register(desc, message);
            }};
            vdev::EditorModuleSource fail{"failed collector", [](auto &, auto &) -> bool { throw 1; }};
            Check(!editor.Assemble({cleanup, fail}, Drawing, error) && probe.refused && keptStops == 0,
                  "rollback allowed reentry or stopped an earlier registration");
            Check(!editor.Register(kept, error), "rollback removed an earlier registration");
            Check(editor.Assemble({}, Drawing, error), error); editor.Stop();
            Check(keptStops == 1, "earlier registration was stopped more than once");
        }
        Environment("NEMODEV_MODULE_FAILURE", "");
        {
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            Check(editor.Assemble(sources(linked), Drawing, error, {}, {{"fixture.addon", false, addon.u8string()}}), error);
            Check(vdev::EditorClient(editor.Host(), 0).Query("fixture.addon.value").status != EDITOR_OK,
                  "disabled external module contributed queries");
            const auto info = editor.Modules();
            Check(std::any_of(info.begin(), info.end(), [](const auto &row) { return row.id == "fixture.addon" && !row.enabled; }),
                  "disabled module disappeared from the configuration query");
            editor.Stop();
        }
        {
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            Check(!editor.Assemble(sources(linked), Drawing, error, {}, {{"fixture.embedded", false}}) &&
                  error.find("dependency") != std::string::npos, "disabled dependency silently activated dependents");
        }
        {
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            Check(editor.Assemble(sources(linked), Drawing, error, {},
                  {{"fixture.embedded", false}, {"fixture.base", false}, {"fixture.addon", false}}), error);
            Check(linked.stops == 1, "disabled linked module was not stopped before activation");
            editor.Stop(); Check(linked.stops == 1, "disabled linked module stopped twice");
        }
        {
            const auto replacement = root / ("replacement" + suffix); fs::copy_file(base, replacement);
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            Check(editor.Assemble(sources(linked), Drawing, error, {}, {{"fixture.base", true, replacement.u8string()}}), error);
            const auto info = editor.Modules();
            Check(std::any_of(info.begin(), info.end(), [&](const auto &row) {
                return row.id == "fixture.base" && row.enabled && row.file == fs::canonical(replacement).u8string();
            }), "replacement did not occupy the configured module identity");
        }
        if (fs::exists(alias)) {
            const auto selectedLog = root / "selected-alias.log";
            Variable selected("NEMODEV_MODULE_LOG", selectedLog.u8string());
            Embedded linked{selectedLog}; vdev::EditorApplication editor([] { return nullptr; });
            Check(editor.Assemble(sources(linked), Drawing, error, {}, {{"fixture.base", true, alias.u8string()}}), error);
            const auto events = Lines(selectedLog);
            Check(std::count(events.begin(), events.end(), "entry fixture.base") == 1,
                  "saved hard-link source loaded the same file twice");
        }
        {
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            Check(!editor.Assemble(sources(linked), Drawing, error, {}, {{"fixture.embedded", true, base.u8string()}}),
                  "a replacement with a different identifier activated");
            Check(editor.Assemble(sources(linked), Drawing, error), error);
        }
        {
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            vdev::EditorConfiguration bad; bad.providers.push_back({"query", "fixture.addon.value", "unknown.provider"});
            Check(!editor.Assemble(sources(linked), Drawing, error, {}, {}, bad), "unknown configured provider silently fell back");
            Check(editor.Assemble(sources(linked), Drawing, error), error);
        }
        {
            Embedded linked{log}; vdev::EditorApplication editor([] { return nullptr; });
            vdev::EditorConfiguration bad; bad.providers.push_back({"query", "fixture.addon.value", "unknown.provider"});
            Check(!editor.Assemble(sources(linked), Drawing, error, bad, {{"fixture.missing", false}}),
                  "unrelated module setting suppressed an invalid default provider");
            Check(editor.Assemble(sources(linked), Drawing, error), error);
        }
        fs::remove_all(root);
        std::cout << "Module discovery, linked/library assembly, rollback, retry and teardown passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        std::error_code ignored; fs::remove_all(root, ignored);
        return 1;
    }
}

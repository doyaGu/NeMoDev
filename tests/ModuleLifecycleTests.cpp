#include "application/EditorApplication.h"
#include "application/RuntimeOperation.h"
#include "application/EditorModules.h"
#include "editor/ImGuiDrawing.h"
#include "core/runtime/CkRuntimeHost.h"
#include "features/resources/DataResourceCommands.h"

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
thread_local size_t *allocations = nullptr;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
struct Module {
    std::string name;
    std::vector<std::string> &events;
    EditorApplication &app;
    unsigned starts = 0, prepare = 0, before = 0, after = 0, stops = 0, destroyed = 0, effects = 0;
    EditorStatus startStatus = EDITOR_OK, startupCommand = EDITOR_OK;
    bool throwStart = false, throwUpdate = false, throwStop = false, reenter = false, record = true;
    bool recursiveStart = true, recursiveUpdate = true, configured = true;
    void Event(const char *event) { if (record) events.push_back(name + ":" + event); }
};
EditorModuleDesc Describe(Module &module) {
    EditorModuleDesc desc{};
    desc.size = sizeof(desc); desc.version = EDITOR_VERSION; desc.id = EditorText(module.name); desc.data = &module;
    desc.start = [](void *data, const EditorHost *host) {
        auto &module = *static_cast<Module *>(data);
        ++module.starts; module.Event("start");
        const EditorRequest request{host->scope(host->data, 0)};
        module.startupCommand = host->execute(host->data, EditorString EDITOR_TEXT("test.lifecycle-effect"), &request, {});
        if (module.throwStart) throw std::runtime_error("Injected startup exception");
        return module.startStatus;
    };
    desc.update = [](void *data, const EditorHost *, EditorModulePhase phase) {
        auto &module = *static_cast<Module *>(data);
        if (phase == EDITOR_PREPARE_FRAME) { ++module.prepare; module.Event("prepare"); }
        else if (phase == EDITOR_BEFORE_VIEWS) { ++module.before; module.Event("before"); }
        else { ++module.after; module.Event("after"); }
        if (module.reenter) {
            std::string error;
            module.recursiveStart = module.app.StartModules(error);
            module.recursiveUpdate = module.app.UpdateModules(phase);
            module.configured = module.app.Configure(module.app.Configuration(), error);
            module.app.Stop();
        }
        if (module.throwUpdate) throw std::runtime_error("Injected update exception");
    };
    desc.stop = [](void *data, const EditorHost *) {
        auto &module = *static_cast<Module *>(data);
        ++module.stops; module.Event("stop");
        if (module.throwStop) throw std::runtime_error("Injected stop exception");
    };
    desc.destroy = [](void *data) {
        auto &module = *static_cast<Module *>(data);
        ++module.destroyed; module.Event("destroy");
    };
    return desc;
}
struct Fixture {
    std::vector<std::string> events;
    EditorApplication app{{}};
    Module first{"test.first", events, app}, second{"test.second", events, app}, third{"test.third", events, app};
    std::vector<EditorResponse> reports;
    std::string error;
    Fixture() { app.report = [this](const auto &result) { reports.push_back(result); }; }
    ~Fixture() { app.Stop(); }
    bool Register() {
        auto secondDesc = Describe(second);
        const EditorString dependency = EditorText(first.name);
        secondDesc.dependencies = &dependency; secondDesc.dependencyCount = 1;
        auto firstDesc = Describe(first);
        const EditorCommandDesc command{sizeof(command), EDITOR_TEXT("test.lifecycle-effect"), EDITOR_TEXT("test.first"),
            EDITOR_TEXT("Lifecycle effect"), nullptr, 0,
            [](void *data, const EditorHost *, const EditorRequest *, EditorSink) {
                ++static_cast<Module *>(data)->effects;
                return EDITOR_OK;
            }, &first, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE};
        firstDesc.commands = &command; firstDesc.commandCount = 1;
        return app.Register(secondDesc, error) && app.Register(firstDesc, error);
    }
    void Activate() { Check(Register() && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str()); }
};
void Order() {
    Fixture f; f.Activate();
    Check(!f.app.UpdateModules(EDITOR_BEFORE_VIEWS) && f.events.empty(), "modules advanced before startup");
    Check(f.app.StartModules(f.error) && f.app.StartModules(f.error), f.error.c_str());
    Check(f.first.startupCommand == EDITOR_REFUSED && !f.first.effects, "startup executed a modifying command");
    Check(f.app.UpdateModules(EDITOR_PREPARE_FRAME) && f.app.UpdateModules(EDITOR_BEFORE_VIEWS) &&
              f.app.UpdateModules(EDITOR_AFTER_VIEWS), "module advance failed");
    f.app.Stop(); f.app.Stop();
    Check(f.events == std::vector<std::string>{"test.first:start", "test.second:start", "test.first:prepare", "test.second:prepare", "test.first:before",
              "test.second:before", "test.first:after", "test.second:after", "test.second:stop", "test.first:stop",
              "test.second:destroy", "test.first:destroy"},
          "module startup, frame phases or shutdown did not follow dependency order exactly once");
    Check(!f.app.UpdateModules(EDITOR_BEFORE_VIEWS), "stopped modules still advanced");
}
void Disabled() {
    Fixture f;
    const EditorModuleSource source{"test.lifecycle", [&](EditorApplication &, std::string &) { return f.Register(); }};
    const EditorModuleSetting settings[] = {{"test.first", false}, {"test.second", false}};
    Check(f.app.Assemble({source}, EDITOR_IMGUI_VERSION, f.error, {}, {std::begin(settings), std::end(settings)}), f.error.c_str());
    Check(f.app.StartModules(f.error) && f.app.UpdateModules(EDITOR_BEFORE_VIEWS), f.error.c_str());
    f.app.Stop();
    Check(!f.first.starts && !f.second.starts && !f.first.before && !f.second.before &&
              !f.first.stops && !f.second.stops && f.first.destroyed == 1 && f.second.destroyed == 1,
          "disabled module ran or leaked its entry data");
}
void StartFailure(bool throwing) {
    Fixture f; f.Activate();
    f.second.throwStart = throwing; f.second.startStatus = EDITOR_ERROR;
    Check(!f.app.StartModules(f.error) && f.error.find("test.second") != std::string::npos,
          "startup failure did not identify the failing module");
    Check(f.events == std::vector<std::string>{"test.first:start", "test.second:start", "test.second:stop", "test.first:stop",
                                             "test.second:destroy", "test.first:destroy"},
          "failed startup did not release the module batch in reverse dependency order");
    f.app.Stop();
    Check(f.first.stops == 1 && f.second.stops == 1 && !f.app.UpdateModules(EDITOR_AFTER_VIEWS),
          "failed startup ran cleanup twice or remained callable");
}
void UpdateException() {
    Fixture f; f.Activate();
    Check(f.app.StartModules(f.error), f.error.c_str());
    f.first.throwUpdate = true;
    Check(!f.app.UpdateModules(EDITOR_BEFORE_VIEWS) && f.second.before == 1 && f.reports.size() == 1 &&
              f.reports[0].message.find("test.first") != std::string::npos, "update exception escaped or skipped independent modules");
    f.app.report = [](const auto &) { throw std::runtime_error("Injected report exception"); };
    Check(!f.app.UpdateModules(EDITOR_AFTER_VIEWS) && f.second.after == 1, "report exception interrupted module advance");
    f.first.throwUpdate = false;
    Check(f.app.UpdateModules(EDITOR_BEFORE_VIEWS) && f.app.Configure(f.app.Configuration(), f.error),
          "update exception left callback or configuration state locked");
}
void ReentrantCalls() {
    Fixture f; f.Activate();
    Check(f.app.StartModules(f.error), f.error.c_str());
    f.first.reenter = true;
    Check(f.app.UpdateModules(EDITOR_BEFORE_VIEWS) && !f.first.recursiveStart && !f.first.recursiveUpdate &&
              !f.first.configured && !f.first.stops && f.second.before == 1,
          "a module changed configuration, recursively advanced, or unloaded executing code");
}
void ThreadChecks() {
    Fixture f; f.Activate();
    bool started = true, updated = true;
    std::thread wrong([&] {
        std::string error;
        started = f.app.StartModules(error);
        updated = f.app.UpdateModules(EDITOR_BEFORE_VIEWS);
        f.app.Stop();
    }); wrong.join();
    Check(!started && !updated && f.events.empty(), "module lifecycle crossed editor threads");
    Check(f.app.StartModules(f.error) && f.app.UpdateModules(EDITOR_AFTER_VIEWS), "thread rejection damaged the editor lifecycle");
}
void StopException() {
    Fixture f; f.Activate(); Check(f.app.StartModules(f.error), f.error.c_str());
    f.second.throwStop = true;
    f.app.Stop(); f.app.Stop();
    Check(f.second.stops == 1 && f.first.stops == 1, "throwing module cleanup skipped dependencies or ran twice");
}
void StableAllocations() {
    Fixture f; f.Activate(); Check(f.app.StartModules(f.error), f.error.c_str());
    f.first.record = f.second.record = false;
    Check(f.app.UpdateModules(EDITOR_PREPARE_FRAME) && f.app.UpdateModules(EDITOR_BEFORE_VIEWS) &&
              f.app.UpdateModules(EDITOR_AFTER_VIEWS), "module warmup failed");
    size_t count = 0;
    allocations = &count;
    bool passed = true;
    for (unsigned i = 0; i < 1000; ++i)
        passed = f.app.UpdateModules(EDITOR_PREPARE_FRAME) && f.app.UpdateModules(EDITOR_BEFORE_VIEWS) &&
                 f.app.UpdateModules(EDITOR_AFTER_VIEWS) && passed;
    allocations = nullptr;
    Check(passed && !count && f.first.prepare == 1001 && f.first.before == 1001 && f.second.after == 1001,
          "stable module dispatch allocated memory or lost an update");
}
void StopBeforeRelease() {
    Fixture f; f.Activate(); Check(f.app.StartModules(f.error), f.error.c_str());
    f.app.StopModules(); f.app.StopModules();
    Check(f.first.stops == 1 && f.second.stops == 1 && !f.first.destroyed && !f.second.destroyed &&
              !f.app.UpdateModules(EDITOR_PREPARE_FRAME) && !f.app.StartModules(f.error),
          "stopping released module data, advanced work or allowed another startup");
    const EditorRequest request{f.app.Scope()};
    Check(f.app.CommandState("test.lifecycle-effect", request) == EDITOR_REFUSED &&
              f.app.Execute("test.lifecycle-effect", request).status == EDITOR_REFUSED &&
              !f.app.Configure(f.app.Configuration(), f.error) && !f.first.effects,
          "stopped modules accepted new work or configuration changes");
    f.app.Stop();
    Check(f.first.destroyed == 1 && f.second.destroyed == 1 && f.first.stops == 1,
          "final release repeated stop or retained entry-owned data");
}
void UnstartedRelease() {
    Fixture f; Check(f.Register(), f.error.c_str());
    auto third = Describe(f.third);
    const EditorString dependency = EditorText(f.second.name);
    third.dependencies = &dependency; third.dependencyCount = 1;
    Check(f.app.Register(third, f.error) && f.app.Activate(EDITOR_IMGUI_VERSION, f.error), f.error.c_str());
    f.second.startStatus = EDITOR_ERROR;
    Check(!f.app.StartModules(f.error) && !f.third.starts && !f.third.stops && f.third.destroyed == 1 &&
              f.first.stops == 1 && f.second.stops == 1,
          "startup failure stopped an unstarted module or leaked its entry data");
}
void UpdateOnly() {
    Fixture f;
    auto desc = Describe(f.first); desc.start = nullptr;
    Check(f.app.Register(desc, f.error) && f.app.Activate(EDITOR_IMGUI_VERSION, f.error) &&
              f.app.StartModules(f.error), f.error.c_str());
    Check(f.app.UpdateModules(EDITOR_PREPARE_FRAME) && f.first.prepare == 1 && !f.first.starts,
          "module without startup callback could not advance");
    f.app.Stop();
    Check(f.first.stops == 1 && f.first.destroyed == 1, "update-only module did not stop and release once");
}
void OptionalOrder(bool present) {
    Fixture f;
    auto first = Describe(f.first), second = Describe(f.second);
    const EditorString dependency = EditorText(f.second.name);
    first.optionalDependencies = &dependency; first.optionalDependencyCount = 1;
    Check(f.app.Register(first, f.error) && (!present || f.app.Register(second, f.error)) &&
              f.app.Activate(EDITOR_IMGUI_VERSION, f.error) && f.app.StartModules(f.error), f.error.c_str());
    Check(f.events.front() == (present ? "test.second:start" : "test.first:start"),
          "optional module dependency changed availability or ignored ordering");
    f.app.StopModules();
    Check(f.first.stops == 1 && (present ? f.second.stops == 1 && f.events.back() == "test.second:stop" : !f.second.stops),
          "optional dependencies did not reverse the stop order");
}
void OptionalCycle() {
    Fixture f;
    auto first = Describe(f.first), second = Describe(f.second);
    const EditorString a = EditorText(f.first.name), b = EditorText(f.second.name);
    first.optionalDependencies = &b; first.optionalDependencyCount = 1;
    second.optionalDependencies = &a; second.optionalDependencyCount = 1;
    Check(f.app.Register(first, f.error) && f.app.Register(second, f.error) &&
              !f.app.Activate(EDITOR_IMGUI_VERSION, f.error) && f.error.find("cycle") != std::string::npos,
          "cycle through present optional modules was accepted");
    f.app.Stop();
    Check(!f.first.starts && !f.second.starts && !f.first.stops && !f.second.stops &&
              f.first.destroyed == 1 && f.second.destroyed == 1, "failed ordering leaked entry data or stopped unstarted modules");
}
void BorrowedControllerLifetime() {
    CkRuntimeHost runtime;
    std::string error;
    const auto *path = std::getenv("VIRTOOLS_PATH");
    Check(runtime.Start(path ? path : ".", {}, &error), error.c_str());
    EditorApplication app({});
    {
        RuntimeOperation operations;
        DataResourceCommands resources(app, operations, runtime);
        Check(resources.Register(error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
        // Entry descriptions borrow this controller. It was never started, so
        // its own destructor releases it before the application table is closed.
    }
    app.Stop();
}
} // namespace
void *operator new(std::size_t size) {
    if (allocations) ++*allocations;
    if (auto *memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
int main(int argc, char **argv) {
    try {
        Check(argc == 2, "one module lifecycle test name is required");
        const std::string name = argv[1];
        if (name == "Order") Order();
        else if (name == "Disabled") Disabled();
        else if (name == "StartRefusal") StartFailure(false);
        else if (name == "StartException") StartFailure(true);
        else if (name == "UpdateException") UpdateException();
        else if (name == "ReentrantCalls") ReentrantCalls();
        else if (name == "ThreadChecks") ThreadChecks();
        else if (name == "StopException") StopException();
        else if (name == "StableAllocations") StableAllocations();
        else if (name == "StopBeforeRelease") StopBeforeRelease();
        else if (name == "UnstartedRelease") UnstartedRelease();
        else if (name == "UpdateOnly") UpdateOnly();
        else if (name == "OptionalPresent") OptionalOrder(true);
        else if (name == "OptionalAbsent") OptionalOrder(false);
        else if (name == "OptionalCycle") OptionalCycle();
        else if (name == "BorrowedControllerLifetime") BorrowedControllerLifetime();
        else throw std::runtime_error("Unknown module lifecycle test: " + name);
        return 0;
    } catch (const std::exception &error) {
        allocations = nullptr;
        std::cerr << error.what() << '\n';
        return 1;
    }
}

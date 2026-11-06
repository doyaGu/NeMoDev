#include "application/RuntimeOperation.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstdlib>
#include <new>

namespace {
using namespace vdev;
thread_local size_t *allocations = nullptr;
void Check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
void DispatchRecheck() {
    bool editing = false, ran = false;
    unsigned completions = 0;
    RuntimeOperation runtime([&](const std::string &) { return editing ? "Edit is open" : std::string(); });
    Check(runtime.Queue(
              "Deferred", [&] { ran = true; },
              [&](const std::string &error) {
                  Check(error == "Edit is open", "dispatch did not report its updated refusal");
                  ++completions;
              }),
          "queue task");
    runtime.Poll();
    editing = true;
    runtime.Start();
    runtime.Finish();
    Check(!ran && completions == 1 && !runtime.Busy(), "a promoted task ignored an edit opened during the frame");
}
void BusyCheck() {
    unsigned checks = 0;
    RuntimeOperation runtime([&](const std::string &) {
        ++checks;
        return std::string();
    });
    Check(runtime.Queue("Queued", [] {}, {}), "queue task");
    const auto previous = checks;
    Check(!runtime.CanQueue("Other") && checks == previous, "busy readiness accessed the document admission callback");
    runtime.Finish();
}
void WrongThread() {
    unsigned checks = 0, completions = 0;
    bool queued = true, allowed = true, ran = false;
    RuntimeOperation runtime([&](const std::string &) {
        ++checks;
        return std::string();
    });
    auto scope = runtime.CurrentDocument();
    std::thread worker([&] {
        allowed = runtime.CanQueue("Worker");
        queued = runtime.Queue("Worker", [&] { ran = true; }, [&](const std::string &) { ++completions; });
        runtime.DocumentChanged();
        runtime.Poll();
        runtime.Start();
    });
    worker.join();
    runtime.Finish();
    Check(!queued && !allowed && !ran && checks == 0 && completions == 0 && scope.IsCurrent(),
          "a worker entered UI-thread admission or changed the runtime queue");
}
void AdmissionException() {
    unsigned completed = 0;
    bool ran = false;
    RuntimeOperation runtime([](const std::string &) -> std::string { throw std::runtime_error("Injected refusal"); });
    Check(!runtime.CanQueue("Throwing"), "throwing readiness was accepted");
    Check(!runtime.Queue(
              "Throwing", [&] { ran = true; },
              [&](const std::string &error) {
                  Check(!error.empty(), "admission exception was lost");
                  ++completed;
              }) &&
              completed == 1 && !ran && !runtime.Busy(),
          "admission exception lost completion or retained work");
}
void WorkflowIsolation() {
    RuntimeOperation runtime;
    auto request = runtime.Begin("File input");
    Check(bool(request) && request.Available() && !runtime.Available() && !runtime.Busy(),
          "input did not own an idle runtime");
    Check(!runtime.Begin("Other") && !runtime.CanQueue("Other"), "another workflow entered native input");
    bool foreign = false, owned = false;
    unsigned refused = 0;
    Check(!runtime.Queue(
              "Foreign", [&] { foreign = true; },
              [&](const std::string &error) {
                  Check(!error.empty(), "foreign workflow lost its refusal");
                  ++refused;
              }),
          "unowned task entered a reserved runtime");
    {
        auto submitting = request.Enter();
        Check(runtime.CanQueue("Owned") && runtime.Queue("Owned", [&] { owned = true; }, {}),
              "owner could not submit work");
    }
    runtime.Finish();
    Check(owned && !foreign && refused == 1 && request.Available() && !runtime.Available(),
          "workflow ownership changed after a task");
    request.Reset();
    Check(runtime.Available(), "completed input retained native ownership");
}
void RequestCancellation() {
    RuntimeOperation runtime;
    bool ran = false;
    unsigned completed = 0;
    auto request = runtime.Begin("File input");
    {
        auto submitting = request.Enter();
        Check(runtime.Queue(
                  "Owned", [&] { ran = true; },
                  [&](const std::string &error) {
                      Check(!error.empty(), "ended request lost task refusal");
                      ++completed;
                  }),
              "queue owned work");
    }
    runtime.Poll();
    request.Reset();
    runtime.Finish();
    Check(!ran && completed == 1 && runtime.Available(), "canceled input executed native work");
}
void ScopedSubmission() {
    RuntimeOperation runtime;
    auto original = runtime.Begin("Input");
    auto request = std::move(original);
    Check(!original && bool(request), "request move duplicated ownership");
    bool ran = false;
    {
        auto outer = request.Enter();
        try {
            auto inner = request.Enter();
            throw std::runtime_error("Injected callback failure");
        } catch (const std::runtime_error &) {
        }
        Check(runtime.Queue("Owner", [&] { ran = true; }, {}), "nested failure lost the submitting owner");
    }
    Check(!runtime.CanQueue("Foreign"), "submission scope leaked its owner");
    runtime.Finish();
    Check(ran && request.Available(), "moved request lost its task");
}
void ContinuationOwnership() {
    RuntimeOperation runtime;
    auto request = runtime.Begin("Input");
    unsigned work = 0, callbacks = 0;
    {
        auto submitting = request.Enter();
        Check(runtime.Queue(
                  "First", [&] { ++work; },
                  [&](const std::string &error) {
                      Check(error.empty(), "first task failed");
                      ++callbacks;
                      Check(runtime.Queue(
                                "Continue", [&] { ++work; },
                                [&](const std::string &error) {
                                    Check(error.empty(), "continuation lost owner");
                                    ++callbacks;
                                    request.Reset();
                                }),
                            "completion could not continue its workflow");
                  }),
              "queue first task");
    }
    runtime.Finish();
    Check(work == 2 && callbacks == 2 && runtime.Available(), "continuation ownership did not unwind");
}
void CompletionStartsWorkflow() {
    RuntimeOperation runtime;
    auto first = runtime.Begin("First");
    RuntimeOperation::Request second;
    unsigned ran = 0;
    {
        auto submitting = first.Enter();
        Check(runtime.Queue(
                  "First", [&] { ++ran; },
                  [&](const std::string &error) {
                      Check(error.empty(), "first task failed");
                      first.Reset();
                      second = runtime.Begin("Second");
                      Check(bool(second), "released workflow blocked its successor");
                      auto next = second.Enter();
                      Check(runtime.Queue(
                                "Second", [&] { ++ran; },
                                [&](const std::string &error) {
                                    Check(error.empty(), "successor failed");
                                    second.Reset();
                                }),
                            "queue successor");
                  }),
              "queue first");
    }
    runtime.Finish();
    Check(ran == 2 && runtime.Available(), "successor inherited an expired owner");
}
void DocumentContinuation() {
    RuntimeOperation runtime;
    auto request = runtime.Begin("Open composition");
    unsigned ran = 0;
    const auto original = runtime.CurrentDocument();
    {
        auto submitting = request.Enter();
        Check(runtime.Queue(
                  "Prepare", [&] { ++ran; },
                  [&](const std::string &error) {
                      Check(error.empty(), "preparation failed");
                      runtime.DocumentChanged();
                      Check(runtime.Queue(
                                "Finish new document", [&] { ++ran; },
                                [&](const std::string &error) {
                                    Check(error.empty(), "new document lost workflow ownership");
                                    request.Reset();
                                }),
                            "new-document continuation refused");
                  }),
              "queue document preparation");
    }
    runtime.Finish();
    Check(ran == 2 && !original.IsCurrent() && runtime.Available(), "document replacement mixed request lifetimes");
}
void AdmissionReentry() {
    RuntimeOperation *queue = nullptr;
    bool nested = true, nestedAllowed = true, ran = false;
    unsigned nestedCallbacks = 0;
    RuntimeOperation runtime([&](const std::string &) {
        nestedAllowed = queue->CanQueue("Nested");
        nested = queue->Queue("Nested", [] {}, [&](const std::string &) { ++nestedCallbacks; });
        queue->Start();
        queue->Poll();
        queue->DocumentChanged();
        return std::string();
    });
    queue = &runtime;
    const auto scope = runtime.CurrentDocument();
    Check(runtime.Queue("Outer", [&] { ran = true; }, {}), "outer task refused");
    runtime.Finish();
    Check(ran && !nested && !nestedAllowed && !nestedCallbacks && scope.IsCurrent(),
          "admission callback changed queue ownership");
}
void DispatchException() {
    bool fail = false, ran = false;
    unsigned completed = 0;
    RuntimeOperation runtime([&](const std::string &) -> std::string {
        if (fail) throw std::runtime_error("Injected dispatch refusal");
        return {};
    });
    Check(runtime.Queue(
              "Promoted", [&] { ran = true; },
              [&](const std::string &error) {
                  Check(error == "Injected dispatch refusal", "dispatch exception lost its reason");
                  ++completed;
              }),
          "queue task");
    runtime.Poll();
    fail = true;
    runtime.Start();
    runtime.Finish();
    Check(!ran && completed == 1 && !runtime.Busy(), "throwing dispatch retained or ran work");
    fail = false;
    Check(runtime.Queue("Recovered", [&] { ran = true; }, {}), "dispatch exception poisoned readiness");
    runtime.Finish();
    Check(ran, "recovered work did not run");
}
void WorkerExclusion() {
    unsigned availability = 0, admission = 0;
    RuntimeOperation runtime(
        [&](const std::string &) {
            ++admission;
            return std::string();
        },
        {},
        [&] {
            ++availability;
            return true;
        });
    std::promise<void> entered, release;
    auto ready = entered.get_future();
    auto gate = release.get_future();
    Check(runtime.Queue("Worker",
                        [&] {
                            entered.set_value();
                            gate.wait();
                        },
                        {}),
          "queue worker");
    runtime.Start();
    const bool started = ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    const auto a = availability, b = admission;
    const bool available = runtime.Available(), canQueue = runtime.CanQueue("UI");
    const bool queued = runtime.Queue("Following", [] {}, {});
    release.set_value();
    Check(started && !available && !canQueue && queued && a == availability && b == admission,
          "busy native readiness entered UI/native callbacks");
    runtime.Finish();
}
void NativeAvailability() {
    bool available = false;
    RuntimeOperation runtime({}, {}, [&] { return available; });
    Check(!runtime.Available() && !runtime.Begin("Unavailable") && !runtime.CanQueue("Unavailable"),
          "unavailable runtime admitted work");
    available = true;
    auto request = runtime.Begin("Input");
    Check(bool(request), "available runtime refused input");
    available = false;
    Check(!request.Available() && !runtime.Available(), "owner bypassed native availability");
    available = true;
    Check(request.Available() && !runtime.Available(), "native availability lost request ownership");
    request.Reset();
    Check(runtime.Available(), "released request kept native access blocked");
}
void DeferredInputOwnership() {
    RuntimeOperation runtime;
    auto request = runtime.Begin("Duplicate input");
    std::function<void()> answer;
    bool ran = false;
    {
        auto submitting = request.Enter();
        answer = runtime.Continue([&] {
            Check(runtime.Queue(
                      "Load after input", [&] { ran = true; },
                      [&](const std::string &error) {
                          Check(error.empty(), "deferred input lost its source");
                          request.Reset();
                      }),
                  "deferred input was refused");
        });
    }
    Check(!runtime.CanQueue("Foreign"), "waiting input lost ownership");
    answer();
    runtime.Finish();
    Check(ran && runtime.Available(), "deferred input did not finish its workflow");
}
void ExpiredContinuation() {
    RuntimeOperation runtime;
    auto request = runtime.Begin("Old input");
    std::function<void()> answer;
    bool ran = false, accepted = true;
    unsigned refused = 0;
    {
        auto submitting = request.Enter();
        answer = runtime.Continue([&] {
            accepted = runtime.Queue(
                "Old callback", [&] { ran = true; },
                [&](const std::string &error) {
                    Check(!error.empty(), "expired callback lost refusal");
                    ++refused;
                });
        });
    }
    request.Reset();
    auto next = runtime.Begin("New input");
    answer();
    runtime.Finish();
    Check(!accepted && !ran && refused == 1 && next.Available() && !runtime.Available(),
          "expired callback entered a replacement workflow");
}
void ContinuationThread() {
    RuntimeOperation runtime;
    bool ran = false, invoked = false, created = false;
    auto answer = runtime.Continue([&] { ran = true; });
    std::thread worker([&] {
        try {
            answer();
        } catch (const std::logic_error &) {
            invoked = true;
        }
        try {
            runtime.Continue([] {});
        } catch (const std::logic_error &) {
            created = true;
        }
    });
    worker.join();
    Check(invoked && created && !ran && runtime.Available(), "a continuation entered from a worker");
}
void StableAvailability() {
    size_t count = 0;
    allocations = &count;
    auto *probe = ::operator new(256);
    ::operator delete(probe);
    allocations = nullptr;
    Check(count == 1, "allocation counter did not observe its probe");
    RuntimeOperation runtime({}, {}, [] { return true; });
    runtime.Available();
    count = 0;
    bool ready = true;
    allocations = &count;
    for (unsigned i = 0; i < 1000; ++i)
        ready &= runtime.Available();
    allocations = nullptr;
    auto request = runtime.Begin("Input");
    allocations = &count;
    for (unsigned i = 0; i < 1000; ++i)
        ready &= !runtime.Available() && request.Available();
    allocations = nullptr;
    Check(ready && count == 0, "stable native readiness allocated or changed ownership");
    std::cout << "Allocations in 2000 native availability checks: " << count << '\n';
}
} // namespace
void *operator new(std::size_t size) {
    if (auto *memory = std::malloc(size ? size : 1)) {
        if (allocations) ++*allocations;
        return memory;
    }
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete(void *memory) noexcept {
    std::free(memory);
}
void operator delete[](void *memory) noexcept {
    std::free(memory);
}
void operator delete(void *memory, std::size_t) noexcept {
    std::free(memory);
}
void operator delete[](void *memory, std::size_t) noexcept {
    std::free(memory);
}
int main(int argc, char **argv) {
    try {
        Check(argc == 2, "one runtime admission case is required");
        const std::string name = argv[1];
        if (name == "DispatchRecheck")
            DispatchRecheck();
        else if (name == "BusyCheck")
            BusyCheck();
        else if (name == "WrongThread")
            WrongThread();
        else if (name == "AdmissionException")
            AdmissionException();
        else if (name == "WorkflowIsolation")
            WorkflowIsolation();
        else if (name == "RequestCancellation")
            RequestCancellation();
        else if (name == "ScopedSubmission")
            ScopedSubmission();
        else if (name == "ContinuationOwnership")
            ContinuationOwnership();
        else if (name == "CompletionStartsWorkflow")
            CompletionStartsWorkflow();
        else if (name == "DocumentContinuation")
            DocumentContinuation();
        else if (name == "AdmissionReentry")
            AdmissionReentry();
        else if (name == "DispatchException")
            DispatchException();
        else if (name == "WorkerExclusion")
            WorkerExclusion();
        else if (name == "NativeAvailability")
            NativeAvailability();
        else if (name == "DeferredInputOwnership")
            DeferredInputOwnership();
        else if (name == "ExpiredContinuation")
            ExpiredContinuation();
        else if (name == "ContinuationThread")
            ContinuationThread();
        else if (name == "StableAvailability")
            StableAvailability();
        else
            throw std::runtime_error("Unknown runtime admission case: " + name);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

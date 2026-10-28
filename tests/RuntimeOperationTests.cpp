#include "ui/app/RuntimeOperation.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

int main() {
    try {
        vdev::RuntimeOperation operation;
        const auto mainThread = std::this_thread::get_id();
        std::promise<void> entered, release;
        auto started = entered.get_future();
        auto gate = release.get_future();
        std::thread::id worker;
        std::vector<int> order;
        int completions = 0;
        CHECK(operation.Queue("Blocked operation", [&] {
            worker = std::this_thread::get_id();
            entered.set_value();
            gate.wait();
            order.push_back(1);
        }, [&](const std::string &error) {
            CHECK(error.empty() && std::this_thread::get_id() == mainThread);
            ++completions;
            CHECK(operation.Queue("Continuation one", [&] { order.push_back(2); }, {}));
            CHECK(operation.Queue("Continuation two", [&] { order.push_back(3); }, {}));
        }));
        CHECK(operation.Busy() && operation.Label() == "Blocked operation");
        CHECK(started.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
        CHECK(operation.Queue("Following request", [&] { order.push_back(4); }, {}));
        operation.Start();
        const bool didStart = started.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        operation.Poll(); // Must return without waiting for the blocked worker.
        release.set_value(); // Release before assertions so failures can also join.
        CHECK(didStart && worker != mainThread && completions == 0);
        operation.Finish();
        CHECK(!operation.Busy() && operation.Label().empty() && completions == 1);
        CHECK(order == std::vector<int>({1, 2, 3, 4}));
        operation.Poll();
        CHECK(completions == 1);

        CHECK(operation.Queue("Failure", [] { throw std::runtime_error("Expected failure"); }, [&](const std::string &error) {
            CHECK(error == "Expected failure");
            ++completions;
            CHECK(operation.Queue("Follow-up", [] {}, [&](const std::string &nextError) {
                CHECK(nextError.empty() && std::this_thread::get_id() == mainThread);
                ++completions;
            }));
        }));
        operation.Finish();
        CHECK(!operation.Busy() && completions == 3);

        bool staleWork = false, staleCompletion = false, transitioned = false;
        auto oldScope = operation.CurrentDocument();
        CHECK(operation.Queue("Switch document", [] {}, [&](const std::string &error) {
            CHECK(error.empty());
            operation.DocumentChanged();
            CHECK(!oldScope.IsCurrent());
            CHECK(operation.Queue("Commit replacement", [&] { transitioned = true; }, {}));
        }));
        auto retained = std::make_shared<int>(0);
        std::weak_ptr<int> discarded = retained;
        CHECK(operation.Queue("Old document", [&, retained] { staleWork = true; }, [&](const std::string &) { staleCompletion = true; }));
        retained.reset();
        operation.Finish();
        CHECK(transitioned && !staleWork && !staleCompletion && discarded.expired());
        CHECK(operation.CurrentDocument().IsCurrent());

        // A completed worker can also become stale before its UI callback.
        std::promise<void> done;
        CHECK(operation.Queue("Late result", [&] { done.set_value(); }, [&](const std::string &) { staleCompletion = true; }));
        operation.Start();
        done.get_future().wait();
        operation.DocumentChanged();
        operation.Finish();
        CHECK(!staleCompletion);

        CHECK(operation.Queue("Promoted old document", [&] { staleWork = true; }, [&](const std::string &) { staleCompletion = true; }));
        operation.Poll();
        operation.DocumentChanged();
        operation.Finish();
        CHECK(!staleWork && !staleCompletion && !operation.Busy());

        // Readiness is checked again after earlier work and before dispatch.
        bool editOpen = false, rejectedWork = false;
        int rejected = 0;
        vdev::RuntimeOperation gated([&](const std::string &) { return editOpen ? "Edit is open" : std::string(); });
        CHECK(gated.Queue("Leave edit open", [] {}, [&](const std::string &) { editOpen = true; }));
        CHECK(gated.Queue("Deferred edit", [&] { rejectedWork = true; }, [&](const std::string &error) {
            CHECK(error == "Edit is open"); ++rejected;
        }));
        gated.Finish();
        CHECK(!rejectedWork && rejected == 1 && !gated.Busy());
        CHECK(!gated.Queue("Rejected immediately", [] {}, [&](const std::string &error) { CHECK(!error.empty()); ++rejected; }));
        CHECK(rejected == 2);
        editOpen = false;
        CHECK(gated.Queue("Recovered", [&] { rejectedWork = true; }, {}));
        gated.Poll(); // Promotes only; all workers start after EndFrame.
        CHECK(!rejectedWork);
        gated.Finish();
        CHECK(rejectedWork);
        CHECK(!gated.Queue("No work", {}, {}));

        std::atomic<bool> finished{false};
        {
            vdev::RuntimeOperation closing;
            CHECK(closing.Queue("Shutdown", [&] { finished = true; }, {}));
            closing.Start();
        }
        CHECK(finished);
        std::cout << "Runtime FIFO, continuations, document scopes, admission, failure and shutdown passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

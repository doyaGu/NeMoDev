#include "ui/app/RuntimeOperation.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

int main() {
    try {
        vdev::RuntimeOperation operation;
        const auto mainThread = std::this_thread::get_id();
        std::promise<void> entered, release;
        auto started = entered.get_future();
        auto gate = release.get_future();
        std::thread::id worker;
        int completions = 0;
        CHECK(operation.Queue("Blocked operation", [&] {
            worker = std::this_thread::get_id();
            entered.set_value();
            gate.wait();
        }, [&](const std::string &error) {
            CHECK(error.empty() && std::this_thread::get_id() == mainThread);
            ++completions;
        }));
        CHECK(operation.Busy() && operation.Label() == "Blocked operation");
        CHECK(started.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
        CHECK(!operation.Queue("Overlapping operation", [] {}, {}));
        operation.Start();
        const bool didStart = started.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        operation.Poll(); // Must return without waiting for the blocked worker.
        release.set_value(); // Release before assertions so failures can also join.
        CHECK(didStart && worker != mainThread && completions == 0);
        operation.Finish();
        CHECK(!operation.Busy() && completions == 1);
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

        std::atomic<bool> finished{false};
        {
            vdev::RuntimeOperation closing;
            CHECK(closing.Queue("Shutdown", [&] { finished = true; }, {}));
            closing.Start();
        }
        CHECK(finished);
        std::cout << "Runtime operation scheduling, completion, failure and shutdown passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

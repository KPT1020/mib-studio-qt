// trigger_service_start_stop_race_test
//
// Regression guard for a concurrent-stop() bug introduced while fixing the
// GUI-initiated camera-stop use-after-free (commit "Close trigger-thread
// use-after-free during GUI-initiated camera stop"): CaptureService::stop()
// (GUI thread) invokes cameraReadyCallback_(nullptr) BEFORE joining the
// capture thread, and CaptureService::run()'s releaseCamera() (capture
// thread) independently reaches the very same callback on its own exit path.
// Both funnel into TriggerService::stop() for the same TriggerService
// instance, so two threads can call stop() concurrently.
//
// TriggerService::stop()'s only guard used to be a lock-free
// `if (!running_.load()) return;` check: two racing callers can both pass it
// before either clears running_, and both then reach thread_.join() on the
// same std::thread object -- concurrent join() on one std::thread is a data
// race / UB (can throw std::system_error, hang, or corrupt the thread
// object). This test hammers TriggerService::stop() from two threads,
// synchronized to fire as close to simultaneously as possible, over many
// iterations, and fails if either call throws or the process does not
// terminate cleanly.

#include "backend/services/TriggerService.h"

#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <thread>

using backend::services::TriggerService;

namespace {

// Lines up exactly two threads so both proceed past arriveAndWait() as close
// to simultaneously as possible, maximizing the odds of hitting the
// stop()/stop() race window on each iteration.
class TwoThreadBarrier {
public:
    void arriveAndWait() {
        std::unique_lock<std::mutex> lk(mutex_);
        ++count_;
        if (count_ == 2) {
            cv_.notify_all();
        } else {
            cv_.wait(lk, [this] { return count_ == 2; });
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_ = 0;
};

} // namespace

int main() {
    constexpr int kIterations = 500;

    for (int i = 0; i < kIterations; ++i) {
        TriggerService svc;
        svc.start();

        TwoThreadBarrier barrier;
        std::exception_ptr excA;
        std::exception_ptr excB;

        std::thread a([&] {
            barrier.arriveAndWait();
            try {
                svc.stop();
            } catch (...) {
                excA = std::current_exception();
            }
        });
        std::thread b([&] {
            barrier.arriveAndWait();
            try {
                svc.stop();
            } catch (...) {
                excB = std::current_exception();
            }
        });

        a.join();
        b.join();

        if (excA) {
            try {
                std::rethrow_exception(excA);
            } catch (const std::exception& ex) {
                std::cerr << "TriggerService: concurrent stop() (thread A) threw: "
                          << ex.what() << " (iteration " << i
                          << ") -- likely a racing double-join on thread_\n";
            }
            return 1;
        }
        if (excB) {
            try {
                std::rethrow_exception(excB);
            } catch (const std::exception& ex) {
                std::cerr << "TriggerService: concurrent stop() (thread B) threw: "
                          << ex.what() << " (iteration " << i
                          << ") -- likely a racing double-join on thread_\n";
            }
            return 1;
        }
    }

    std::cout << "TriggerService concurrent stop() test OK (" << kIterations
              << " iterations, no crash/exception from racing stop() calls)\n";
    return 0;
}

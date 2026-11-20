#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace rl78_programmer::app {

// A completion remains owned and busy until the UI consumes it. No HWND or
// posted heap pointer participates in resource ownership.
template<class Result, class Progress>
class ApplicationWorker {
public:
    ~ApplicationWorker() {
        if (thread_.joinable()) thread_.join();
    }

    bool IsBusy() const noexcept { return busy_; }

    bool Start(std::function<std::unique_ptr<Result>()> work) {
        if (busy_) return false;
        busy_ = true;
        done_.store(false);
        try {
            thread_ = std::thread([this, work = std::move(work)]() {
                auto result = work();
                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    result_ = std::move(result);
                }
                done_.store(true);
            });
        } catch (...) {
            busy_ = false;
            throw;
        }
        return true;
    }

    void Publish(Progress value) {
        std::lock_guard<std::mutex> guard(mutex_);
        progress_ = std::make_unique<Progress>(std::move(value));
    }

    std::unique_ptr<Progress> TakeProgress() {
        std::lock_guard<std::mutex> guard(mutex_);
        return std::move(progress_);
    }

    std::unique_ptr<Result> TakeResult() {
        if (!busy_ || !done_.load()) return {};
        thread_.join();
        std::lock_guard<std::mutex> guard(mutex_);
        busy_ = false;
        progress_.reset();
        return std::move(result_);
    }

private:
    bool busy_ = false; // UI-thread owned; done_ publishes worker completion.
    std::atomic<bool> done_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::unique_ptr<Result> result_;
    std::unique_ptr<Progress> progress_;
};

} // namespace rl78_programmer::app

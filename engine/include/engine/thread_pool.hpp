/**
 * @file thread_pool.hpp
 * @brief Small hardware-aware thread pool for background work
 *
 * The pool sizes itself from std::thread::hardware_concurrency() so it adapts
 * to any core count (1 core -> no worker threads, work runs inline). It is for
 * CPU-only work: none of these tasks may touch OpenGL, which is bound to the
 * main thread.
 */

#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    /// @param requestedThreads 0 = pick from hardware_concurrency()
    explicit ThreadPool(size_t requestedThreads = 0) {
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        // Leave the main thread a core when possible; never spawn less than 1
        // worker when more than one core exists.
        size_t workers = requestedThreads;
        if (workers == 0) {
            workers = hw > 2 ? hw - 1 : 1;
        }
        workers = std::min<size_t>(workers, hw);

        for (size_t i = 0; i < workers; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~ThreadPool() { shutdown(); }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// @return number of worker threads actually created
    size_t threadCount() const { return workers_.size(); }

    /// Queue a task and get a future for its result.
    template <typename Fn>
    auto submit(Fn&& fn) -> std::future<decltype(fn())> {
        using Result = decltype(fn());
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(fn));
        std::future<Result> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return future; // pool shutting down: future stays broken
            tasks_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

    /// Stop workers and join. Safe to call more than once.
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return;
            stopping_ = true;
        }
        cv_.notify_all();
        for (std::thread& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
    }

private:
    void workerLoop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty()) return;
                job = std::move(tasks_.front());
                tasks_.pop();
            }
            job();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

#endif // THREAD_POOL_HPP

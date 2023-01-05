#include "task_pool.h"
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace be {

struct task_pool::Impl
{
    std::atomic<bool> paused{ false };
    std::atomic<bool> running{ false };
    std::condition_variable task_available_cv = {};
    std::condition_variable task_done_cv = {};
    std::queue<task_t> tasks_ = {};
    std::atomic<std::size_t> tasks_total{ 0 };
    mutable std::mutex tasks_mutex = {};
    unsigned thread_count = 0;
    std::vector<std::thread> threads;
    std::atomic<bool> waiting{ false };

    explicit Impl(unsigned thread_count_) : thread_count(determine_thread_count(thread_count_)), threads(thread_count)
    {
        create_threads();
    }
    ~Impl() { destroy_threads(); }

    Impl(Impl const &) = delete;
    Impl(Impl &&) = delete;
    Impl &operator=(Impl const &) = delete;
    Impl &operator=(Impl &&) = delete;

    void create_threads()
    {
        running = true;
        for (unsigned i = 0; i < thread_count; ++i) { threads[i] = std::thread(&Impl::worker, this); }
    }

    void destroy_threads()
    {
        running = false;
        task_available_cv.notify_all();
        for (unsigned i = 0; i < thread_count; ++i) {
            if (threads[i].joinable()) { threads[i].join(); }
        }
        threads.clear();
    }

    static unsigned determine_thread_count(const unsigned thread_count_)
    {
        if (thread_count_ > 0) {
            return thread_count_;
        } else {
            if (std::thread::hardware_concurrency() > 0) {
                return std::thread::hardware_concurrency();
            } else {
                return 1;
            }
        }
    }

    void worker()
    {
        while (running) {
            std::unique_lock<std::mutex> tasks_lock(tasks_mutex);
            task_available_cv.wait(tasks_lock, [this] { return !tasks_.empty() || !running; });
            if (running && !paused) {
                task_model model{ nullptr, nullptr };
                task_view view{ nullptr };
                std::tie(model, view) = std::move(tasks_.front());
                tasks_.pop();
                tasks_lock.unlock();
                view.execute(model);
                tasks_lock.lock();
                --tasks_total;
                model.task_deleter(model.task);
                if (waiting) { task_done_cv.notify_one(); }
            }
        }
    }

    void add_task(task_t task)
    {
        {
            std::unique_lock<std::mutex> tasks_lock(tasks_mutex);
            tasks_.push(std::move(task));
        }
        ++tasks_total;
        task_available_cv.notify_one();
    }

    std::size_t get_tasks_queued() const
    {
        std::unique_lock<std::mutex> tasks_lock(tasks_mutex);
        return tasks_.size();
    }

    std::size_t get_tasks_running() const
    {
        std::unique_lock<std::mutex> tasks_lock(tasks_mutex);
        return tasks_total - tasks_.size();
    }

    std::size_t get_tasks_total() const { return tasks_total; }

    unsigned get_thread_count() const { return thread_count; }

    bool is_paused() const { return paused; }

    void pause() { paused = true; }

    void reset(const unsigned thread_count_)
    {
        const bool was_paused = paused;
        paused = true;
        wait_for_tasks();
        destroy_threads();
        thread_count = determine_thread_count(thread_count_);
        threads = std::vector<std::thread>(thread_count);
        paused = was_paused;
        create_threads();
    }

    void unpause() { paused = false; }

    void wait_for_tasks()
    {
        waiting = true;
        std::unique_lock<std::mutex> tasks_lock(tasks_mutex);
        task_done_cv.wait(tasks_lock, [this] { return (tasks_total == (paused ? tasks_.size() : 0)); });
        waiting = false;
    }
};

task_pool::task_pool(const unsigned thread_count) : impl_{ new Impl(thread_count) } {}

task_pool::~task_pool() { impl_->wait_for_tasks(); }

task_pool::task_pool(task_pool &&other) noexcept : impl_(other.impl_) { other.impl_ = nullptr; }

task_pool &task_pool::operator=(task_pool &&other) noexcept
{
    wait_for_tasks();
    std::swap(other.impl_, impl_);
    return *this;
}

std::size_t task_pool::get_tasks_queued() const { return impl_->get_tasks_queued(); }

std::size_t task_pool::get_tasks_running() const { return impl_->get_tasks_running(); }

std::size_t task_pool::get_tasks_total() const { return impl_->get_tasks_total(); }

unsigned task_pool::get_thread_count() const { return impl_->get_thread_count(); }

bool task_pool::is_paused() const { return impl_->is_paused(); }

void task_pool::pause() { impl_->pause(); }

void task_pool::reset(const unsigned thread_count_) { impl_->reset(thread_count_); }

void task_pool::unpause() { impl_->unpause(); }

void task_pool::wait_for_tasks() { impl_->wait_for_tasks(); }

void task_pool::push_task(task_t &&task) { impl_->add_task(std::move(task)); }

}// namespace be

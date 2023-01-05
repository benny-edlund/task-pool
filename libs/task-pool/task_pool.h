#pragma once

#include "task_pool_fallbacks.h"
#include <functional>
#include <future>
#include <type_traits>
#include <utility>

namespace be {

class task_pool
{
    struct task_model
    {
        void *task = nullptr;
        void (*task_deleter)(void *);
    };

    struct task_view
    {
        void (*execute)(task_model const &);
    };

    using task_t = std::pair<task_model, task_view>;

    template<typename F,
        typename FuncType = std::remove_reference_t<std::remove_cv_t<F>>,
        std::enable_if_t<std::is_function<std::remove_pointer_t<std::remove_reference_t<FuncType>>>::value, bool> =
            true>
    static auto make_task(F &&)
    {
        struct Task
        {
            void operator()() const { FuncType(); }
        };

        return std::make_pair(task_model{ new Task, [](void *x) { delete static_cast<Task *>(x); } },
            task_view{ [](task_model const &model) { (*static_cast<Task *>(model.task))(); } });
    }

    template<typename F,
        typename FuncType = std::remove_reference_t<std::remove_cv_t<F>>,
        std::enable_if_t<!std::is_function<std::remove_pointer_t<std::remove_reference_t<FuncType>>>::value, bool> =
            true>
    static auto make_task(F &&task)
    {
        struct Task : FuncType
        {
            explicit Task(FuncType &&f) : FuncType(std::forward<F>(f)) {}
            using FuncType::operator();
        };
        return std::make_pair(
            task_model{ new Task(std::forward<F>(task)), [](void *x) { delete static_cast<Task *>(x); } },
            task_view{ [](task_model const &model) { (*static_cast<Task *>(model.task))(); } });
    }

  public:
    task_pool(const unsigned = 0);
    ~task_pool();

    task_pool(task_pool const &) = delete;
    task_pool &operator=(task_pool const &) = delete;
    task_pool(task_pool &&) noexcept;
    task_pool &operator=(task_pool &&) noexcept;

    void reset(const unsigned = 0);

    BE_NODISGARD std::size_t get_tasks_queued() const;

    BE_NODISGARD std::size_t get_tasks_running() const;

    BE_NODISGARD std::size_t get_tasks_total() const;

    BE_NODISGARD unsigned get_thread_count() const;

    BE_NODISGARD bool is_paused() const;

    void pause();

    template<typename F,
        typename... A,
        typename R = be_invoke_result_t<std::decay_t<F>, std::decay_t<A>...>,
        std::enable_if_t<be_is_void_v<R>, bool> = true>
    BE_NODISGARD std::future<R> submit(F &&task, A &&...args)
    {
        auto promise = std::promise<R>();
        auto task_future = promise.get_future();
        push_task(make_task([task_function = std::bind(std::forward<F>(task), std::forward<A>(args)...),
                                task_promise = std::move(promise)]() mutable {
            try {
                task_function();
                task_promise.set_value();
            } catch (...) {
                task_promise.set_exception(std::current_exception());
            }
        }));
        return task_future;
    }

    template<typename F,
        typename... A,
        typename R = be_invoke_result_t<std::decay_t<F>, std::decay_t<A>...>,
        std::enable_if_t<!be_is_void_v<R>, bool> = true>
    BE_NODISGARD std::future<R> submit(F &&task, A &&...args)
    {
        std::shared_ptr<std::promise<R>> task_promise = std::make_shared<std::promise<R>>();
        push_task([task_function = std::bind(std::forward<F>(task), std::forward<A>(args)...), task_promise]() mutable {
            try {
                task_promise->set_value(task_function());
            } catch (...) {
                task_promise->set_exception(std::current_exception());
            }
        });
        return task_promise->get_future();
    }

    void unpause();

    void wait_for_tasks();

  private:
    void push_task(task_t &&);

    struct Impl;
    Impl *impl_;
};

}// namespace be
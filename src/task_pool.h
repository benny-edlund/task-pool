#pragma once

#include "task_pool_fallbacks.h"
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

namespace be {

template<typename T> struct be_is_function_pointer : public std::is_function<std::remove_pointer_t<std::decay_t<T>>>
{
};
template<typename T>
constexpr bool is_function_pointer_v = std::is_pointer<T>::value &&std::is_function<std::remove_pointer_t<T>>::value;

class task_pool
{
    struct task_proxy
    {
        void (*execute_task)(void *);
        std::unique_ptr<void, void (*)(void *)> storage;

        task_proxy() = delete;
        template<typename Task>
        explicit task_proxy(Task *task)
            : execute_task([](void *x) { (*static_cast<Task *>(x))(); }),
              storage(task, [](void *x) { delete static_cast<Task *>(x); })
        {}
        ~task_proxy() = default;
        task_proxy(task_proxy const &) = delete;
        task_proxy &operator=(task_proxy const &) = delete;
        task_proxy(task_proxy &&) noexcept;
        task_proxy &operator=(task_proxy &&) noexcept;
    };

    template<typename F,
        typename FuncType = std::remove_reference_t<std::remove_cv_t<F>>,
        std::enable_if_t<is_function_pointer_v<FuncType>, bool> = true>
    static auto make_task(F &&)
    {
        struct Task
        {
            void operator()() const { FuncType(); }
        };
        return task_proxy(new Task);
    }

    template<typename F,
        typename FuncType = std::remove_reference_t<std::remove_cv_t<F>>,
        std::enable_if_t<!is_function_pointer_v<FuncType>, bool> = true>
    static auto make_task(F &&task)
    {
        struct Task : FuncType
        {
            explicit Task(FuncType &&f) : FuncType(std::forward<F>(f)) {}
            using FuncType::operator();
        };
        return task_proxy(new Task(std::forward<F>(task)));
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
    void unpause();
    void wait_for_tasks();

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
        auto promise = std::promise<R>();
        auto future = promise.get_future();
        push_task(make_task([task_function = std::bind(std::forward<F>(task), std::forward<A>(args)...),
                                task_promise = std::move(promise)]() mutable {
            try {
                task_promise->set_value(task_function());
            } catch (...) {
                task_promise->set_exception(std::current_exception());
            }
        }));
        return future;
    }

  private:
    void push_task(task_proxy &&);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}// namespace be
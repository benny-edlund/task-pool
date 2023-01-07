#include <algorithm>
#include <atomic>
#include <catch2/catch.hpp>
#include <chrono>
#include <random>
#include <task_pool.h>
#include <thread>
#include <vector>
#include <iostream>

using namespace std::chrono_literals;

TEST_CASE("construction/thread-count", "[task_pool]")
{
    std::vector<unsigned> numbers(std::thread::hardware_concurrency());
    std::iota(numbers.begin(), numbers.end(), 1);

    for (auto const &expected : numbers) {
        be::task_pool pool(expected);
        auto actual = pool.get_thread_count();
        REQUIRE(actual == expected);
    }
}
//
// TEST_CASE("move construct", "[task_pool]")
//{
//    std::atomic_bool finish{ false };
//    be::task_pool from(1);
//    auto thread_count = from.get_thread_count();
//    auto future = from.submit([&]() -> void {
//        while (!finish) { std::this_thread::sleep_for(1ms); }
//    });
//    while (from.get_tasks_running() == 0) { std::this_thread::sleep_for(1ms); }
//    REQUIRE(from.get_tasks_running() == 1);
//    be::task_pool to(std::move(from));
//    REQUIRE(to.get_tasks_running() == 1);
//    REQUIRE(to.get_thread_count() == thread_count);
//    finish = true;
//    future.wait();
//}
//
//
// TEST_CASE("move assign", "[task_pool]")
//{
//    std::atomic_bool finish{ false };
//    be::task_pool to;
//    REQUIRE(to.get_tasks_running() == 0);
//    std::future<void> future = [&] {
//        be::task_pool from(1);
//        auto f = from.submit([&]() -> void {
//            while (!finish) { std::this_thread::sleep_for(1ms); }
//        });
//        while (from.get_tasks_running() == 0) { std::this_thread::sleep_for(1ms); }
//        REQUIRE(from.get_tasks_running() == 1);
//        to = std::move(from);
//        return f;
//    }();
//    REQUIRE(to.get_tasks_running() == 1);
//    REQUIRE(to.get_thread_count() == 1);
//
//    finish = true;
//    future.wait();
//}


TEST_CASE("reset", "[task_pool]")
{
    std::vector<unsigned> numbers(std::thread::hardware_concurrency());
    std::iota(numbers.begin(), numbers.end(), 1);

    be::task_pool pool;
    for (auto const &expected : numbers) {
        pool.reset(expected);
        auto actual = pool.get_thread_count();
        REQUIRE(actual == expected);
    }
}

TEST_CASE("get_tasks_queued", "[task_pool]")
{
    std::atomic_bool finish{ false };
    be::task_pool pool(1);
    REQUIRE(pool.get_tasks_queued() == 0);
    pool.pause();
    auto future = pool.submit([&]() -> void {
        while (!finish) { std::this_thread::sleep_for(1ms); }
    });
    REQUIRE(pool.get_tasks_queued() == 1);
    pool.unpause();
    finish = true;
    future.wait();
    REQUIRE(pool.get_tasks_queued() == 0);
}

TEST_CASE("get_tasks_running", "[task_pool]")
{
    std::atomic_bool started{ false };
    std::atomic_bool finish{ false };
    be::task_pool pool(1);
    REQUIRE(pool.get_tasks_running() == 0);
    auto future = pool.submit([&]() -> void {
        started = true;
        while (!finish) { std::this_thread::sleep_for(1ms); }
    });
    while (!started) { std::this_thread::sleep_for(1ms); }
    REQUIRE(pool.get_tasks_running() == 1);
    finish = true;
    future.wait();
    REQUIRE(pool.get_tasks_running() == 0);
}

TEST_CASE("get_tasks_total", "[task_pool]")
{
    std::atomic_bool finish{ false };
    be::task_pool pool(1);
    pool.pause();
    REQUIRE(pool.get_tasks_total() == 0);
    auto _ = pool.submit([&]() -> void {
        while (!finish) { std::this_thread::sleep_for(1ms); }
    });
    REQUIRE(pool.get_tasks_total() == 1);
    auto __ = pool.submit([&]() -> void {
        while (!finish) { std::this_thread::sleep_for(1ms); }
    });
    REQUIRE(pool.get_tasks_total() == 2);
    pool.unpause();
    finish = true;
}

TEST_CASE("pause/is_paused/unpause", "[task_pool]")
{
    std::atomic_bool finish{ false };
    be::task_pool pool(1);
    pool.pause();
    REQUIRE(pool.get_tasks_total() == 0);
    auto _ = pool.submit([&]() -> void {
        while (!finish) { std::this_thread::sleep_for(1ms); }
    });
    std::this_thread::sleep_for(1ms);
    REQUIRE(pool.get_tasks_total() == 1);
    REQUIRE(pool.get_tasks_running() == 0);
    pool.unpause();
    std::this_thread::sleep_for(1ms);
    REQUIRE(pool.get_tasks_running() == 1);
    finish = true;
}

TEST_CASE("wait_for_tasks", "[task_pool]")
{
    be::task_pool pool(1);
    pool.pause();
    REQUIRE(pool.get_tasks_total() == 0);
    auto a = pool.submit([&]() -> void { std::this_thread::sleep_for(1ms); });
    auto b = pool.submit([&]() -> void { std::this_thread::sleep_for(1ms); });
    auto c = pool.submit([&]() -> void { std::this_thread::sleep_for(1ms); });
    REQUIRE(pool.get_tasks_total() == 3);
    pool.unpause();
    pool.wait_for_tasks();
    REQUIRE(pool.get_tasks_total() == 0);
}

void test_func_(std::atomic_bool *x) { (*x) = true; }


TEST_CASE("free function", "[task_pool][submit]")
{
    std::atomic_bool called{ false };
    {
        be::task_pool pool(1);
        pool.submit(&test_func_, &called).wait();
        REQUIRE(called);
    }
}

struct test_
{
    std::atomic_bool called{ false };
    void test_func_() { called = true; }
};

TEST_CASE("member function", "[task_pool][submit]")
{
    {
        test_ x;
        be::task_pool pool(1);
        pool.submit(&test_::test_func_, &x).wait();
        REQUIRE(x.called);
    }
}

TEST_CASE("lambda pure by &", "[task_pool][submit]")
{
    {
        std::atomic_bool called{ false };
        auto fun = [](auto *x) { (*x) = true; };
        be::task_pool pool(1);
        pool.submit(fun, &called).wait();
        REQUIRE(called);
    }
}

TEST_CASE("lambda pure by &&", "[task_pool][submit]")
{
    {
        std::atomic_bool called{ false };
        be::task_pool pool(1);
        pool.submit([](auto *x) { (*x) = true; }, &called).wait();
        REQUIRE(called);
    }
}

TEST_CASE("lambda with capture by &", "[task_pool][submit]")
{
    {
        std::atomic_bool called{ false };
        auto x = [&]() { called = true; };
        be::task_pool pool(1);
        pool.submit(x).wait();
        REQUIRE(called);
    }
}

TEST_CASE("lambda with capture by &&", "[task_pool][submit]")
{
    std::atomic_bool called{ false };
    be::task_pool pool(1);
    pool.submit([&]() { called = true; }).wait();
    REQUIRE(called);
}

TEST_CASE("std::function capture by &&", "[task_pool][submit]")
{
    std::atomic_bool called{ false };
    be::task_pool pool(1);
    pool.submit(std::function<void()>([&]() { called = true; })).wait();
    REQUIRE(called);
}

TEST_CASE("std::function capture by &", "[task_pool][submit]")
{
    std::atomic_bool called{ false };
    be::task_pool pool(1);
    auto x = std::function<void()>([&]() { called = true; });
    pool.submit(x).wait();
    REQUIRE(called);
}

TEST_CASE("std::function pure by &&", "[task_pool][submit]")
{
    std::atomic_bool called{ false };
    be::task_pool pool(1);
    pool.submit(std::function<void(std::atomic_bool *)>([](auto *x) { (*x) = true; }), &called).wait();
    REQUIRE(called);
}

TEST_CASE("std::function pure by &", "[task_pool][submit]")
{
    std::atomic_bool called{ false };
    be::task_pool pool(1);
    auto fun = std::function<void(std::atomic_bool *)>([](auto *x) { (*x) = true; });
    pool.submit(fun, &called).wait();
    REQUIRE(called);
}

TEST_CASE("stateful lambda by &&", "[task_pool][submit]")
{
    be::task_pool pool(1);
    std::atomic_bool called{ false };
    pool.submit([value = 2](auto *x) mutable { (*x) = --value == 1; }, &called).wait();
    REQUIRE(called);
}

TEST_CASE("stateful lambda from inner scope", "[task_pool][submit]")
{
    be::task_pool pool(1);
    pool.pause();
    std::atomic_bool called{ false };
    std::future<void> f;
    {
        f = pool.submit([value = 2](auto *x) mutable { (*x) = --value == 1; }, &called);
    }
    pool.unpause();
    f.wait();
    REQUIRE(called);
}

TEST_CASE("submit with result", "[task_pool][submit]")
{

    be::task_pool pool(1);
    pool.pause();
    std::atomic_bool called{ false };
    std::future<int> f;
    {
        f = pool.submit(
            [value = 2](auto *x) mutable {
                (*x) = --value == 1;
                return value;
            },
            &called);
    }
    pool.unpause();
    auto result = f.get();
    REQUIRE(called);
    REQUIRE(result == 1);
}

std::atomic_uint64_t allocations{ 0 };
std::atomic_uint64_t deallocations{ 0 };
std::atomic_uint64_t constructions{ 0 };

template<class T> struct counting_allocator
{
    std::allocator<T> real_allocator{};
    using allocator_traits = std::allocator_traits<std::allocator<T>>;
    using value_type = T;
    counting_allocator() noexcept = default;
    template<class U> explicit counting_allocator(const counting_allocator<U> & /*unused*/) noexcept {}
    T *allocate(std::size_t n)
    {
        ++allocations;
        std::cerr << "Allocating " << typeid(T).name() << '\n';
        return allocator_traits::allocate(real_allocator, n);
    }
    template<typename U, typename... Args> void construct(U *p, Args &&...args)
    {
        ++constructions;
        std::cerr << "Constructing " << typeid(T).name() << '\n';
        allocator_traits::construct(real_allocator, p, std::forward<Args>(args)...);
    }
    void deallocate(T *p, std::size_t n)
    {
        ++deallocations;
        std::cerr << "Deallocating " << typeid(T).name() << '\n';
        allocator_traits::deallocate(real_allocator, p, n);
    }
};

template<class T, class U>
constexpr bool operator==(const counting_allocator<T> & /*T*/, const counting_allocator<U> & /*U*/) noexcept
{
    return true;
}

template<class T, class U>
constexpr bool operator!=(const counting_allocator<T> & /*T*/, const counting_allocator<U> & /*U*/) noexcept
{
    return false;
}

TEST_CASE("submit with result allocator", "[task_pool][submit]")
{

    allocations = 0;
    deallocations = 0;
    constructions = 0;

    std::atomic_bool called{ false };
    counting_allocator<int> alloc;
    {
        be::task_pool pool(1);
        pool.pause();
        std::future<int> f;
        {
            f = pool.submit(
                std::allocator_arg_t{},
                alloc,
                [value = 2](auto *x) mutable {
                    (*x) = --value == 1;
                    return value;
                },
                &called);
        }
        pool.unpause();
        auto result = f.get();
        REQUIRE(called);
        REQUIRE(result == 1);
    }
    CHECK(allocations == 3);
    CHECK(deallocations == 3);
    CHECK(constructions == 2);
}

TEST_CASE("submit without result allocator", "[task_pool][submit]")
{
    allocations = 0;
    deallocations = 0;
    constructions = 0;

    std::atomic_bool called{ false };
    counting_allocator<void> alloc;
    {
        be::task_pool pool(1);
        pool.pause();
        std::future<void> f;
        {
            f = pool.submit(
                std::allocator_arg_t{},
                alloc,
                [value = 2](auto *x) mutable {
                    (*x) = --value == 1;
                },
                &called);
        }
        pool.unpause();
        f.wait();
        REQUIRE(called);
    }
    CHECK(allocations == 3);
    CHECK(deallocations == 3);
    CHECK(constructions == 2);
}

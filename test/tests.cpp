#include <algorithm>
#include <atomic>
#include <catch2/catch.hpp>
#include <chrono>
#include <cstddef>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <task_pool/pipes.h>
#include <task_pool/pool.h>
#include <task_pool/traits.h>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

struct test_exception : public std::exception
{
};

TEST_CASE( "construction/thread-count", "[task_pool]" )
{
    std::vector< unsigned > numbers;
    numbers.resize( std::thread::hardware_concurrency() );
    std::iota( numbers.begin(), numbers.end(), 1 );

    for ( auto const& expected : numbers )
    {
        be::task_pool pool( expected );
        auto          actual = pool.get_thread_count();
        REQUIRE( actual == expected );
    }
}

TEST_CASE( "construction/latency/thread-count", "[task_pool]" )
{
    std::vector< unsigned > numbers;
    numbers.resize( std::thread::hardware_concurrency() );
    std::iota( numbers.begin(), numbers.end(), 1 );

    for ( auto const& expected : numbers )
    {
        be::task_pool pool( std::chrono::microseconds( expected ), expected );
        auto          actual_latency     = pool.get_check_latency();
        auto          actual_threadcount = pool.get_thread_count();
        REQUIRE( actual_latency == std::chrono::microseconds( expected ) );
        REQUIRE( actual_threadcount == expected );
    }
}

TEST_CASE( "construction/default value", "[task_pool]" )
{
    auto          expected = std::thread::hardware_concurrency();
    be::task_pool pool;
    auto          actual = pool.get_thread_count();
    REQUIRE( actual == expected );
}

TEST_CASE( "move construct", "[task_pool]" )
{
    std::atomic_bool finish{ false };
    be::task_pool    from( 1 );
    auto             thread_count = from.get_thread_count();
    auto             future       = from.submit( std::launch::async, [&]() {
        while ( !finish )
        {
            std::this_thread::sleep_for( 1ms );
        }
    } );
    while ( from.get_tasks_running() == 0 )
    {
        std::this_thread::sleep_for( 1ms );
    }
    REQUIRE( from.get_tasks_running() == 1 );
    be::task_pool to( std::move( from ) );
    REQUIRE( to.get_tasks_running() == 1 );
    REQUIRE( to.get_thread_count() == thread_count );
    finish = true;
    future.wait();
}

TEST_CASE( "move assign", "[task_pool]" )
{
    std::atomic_bool finish{ false };
    be::task_pool    to;
    REQUIRE( to.get_tasks_running() == 0 );
    std::future< void > future = [&] {
        be::task_pool from( 1 );
        auto          f = from.submit( std::launch::async, [&]() -> void {
            while ( !finish )
            {
                std::this_thread::sleep_for( 1ms );
            }
        } );
        while ( from.get_tasks_running() == 0 )
        {
            std::this_thread::sleep_for( 1ms );
        }
        REQUIRE( from.get_tasks_running() == 1 );
        to = std::move( from );
        return f;
    }();
    REQUIRE( to.get_tasks_running() == 1 );
    REQUIRE( to.get_thread_count() == 1 );

    finish = true;
    future.wait();
}

TEST_CASE( "reset", "[task_pool]" )
{
    std::vector< unsigned > numbers( std::thread::hardware_concurrency() );
    std::iota( numbers.begin(), numbers.end(), 1 );

    be::task_pool pool;
    for ( auto const& expected : numbers )
    {
        pool.reset( expected );
        auto actual = pool.get_thread_count();
        REQUIRE( actual == expected );
    }
}

TEST_CASE( "get_tasks_queued", "[task_pool]" )
{
    std::atomic_bool finish{ false };
    be::task_pool    pool( 1 );
    REQUIRE( pool.get_tasks_queued() == 0 );
    pool.pause();
    auto future = pool.submit( std::launch::async, [&]() -> void {
        while ( !finish )
        {
            std::this_thread::sleep_for( 1ms );
        }
    } );
    REQUIRE( pool.get_tasks_queued() == 1 );
    pool.unpause();
    finish = true;
    pool.wait();
    REQUIRE( pool.get_tasks_queued() == 0 );
}

TEST_CASE( "get_tasks_running", "[task_pool]" )
{
    std::atomic_bool started{ false };
    std::atomic_bool finish{ false };
    be::task_pool    pool( 1 );
    REQUIRE( pool.get_tasks_running() == 0 );
    auto future = pool.submit( std::launch::async, [&]() -> void {
        started = true;
        while ( !finish )
        {
            std::this_thread::sleep_for( 1ms );
        }
    } );
    while ( !started )
    {
        std::this_thread::sleep_for( 1ms );
    }
    REQUIRE( pool.get_tasks_running() == 1 );
    finish = true;
    pool.wait();
    REQUIRE( pool.get_tasks_running() == 0 );
}

TEST_CASE( "get_tasks_waiting", "[task_pool]" )
{
    std::atomic_bool started{ false };
    std::atomic_bool finish{ false };
    be::task_pool    pool( 1 );
    REQUIRE( pool.get_tasks_waiting() == 0 );
    auto future = pool.submit( std::launch::async, [&]() {
        started = true;
        while ( !finish )
        {
            std::this_thread::sleep_for( 1ms );
        }
        return true;
    } );
    auto done   = pool.submit(
        std::launch::async, []( bool /*yup*/ ) {}, std::move( future ) );
    while ( !started )
    {
        std::this_thread::sleep_for( 1ms );
    }
    REQUIRE( pool.get_tasks_waiting() == 1 );
    finish = true;
    pool.wait();
    REQUIRE( pool.get_tasks_waiting() == 0 );
}

TEST_CASE( "get_tasks_total", "[task_pool]" )
{
    std::atomic_bool finish{ false };
    be::task_pool    pool( 1 );
    pool.pause();
    REQUIRE( pool.get_tasks_total() == 0 );
    pool.submit( std::launch::async, [&]() -> void {
        while ( !finish )
        {
            std::this_thread::sleep_for( 1ms );
        }
    } );
    REQUIRE( pool.get_tasks_total() == 1 );
    pool.submit( std::launch::async, [&]() -> void {
        while ( !finish )
        {
            std::this_thread::sleep_for( 1ms );
        }
    } );
    REQUIRE( pool.get_tasks_total() == 2 );
    pool.unpause();
    finish = true;
    pool.wait();
}

TEST_CASE( "pause/is_paused/unpause", "[task_pool]" )
{
    be::task_pool pool( 1 );
    REQUIRE_FALSE( pool.is_paused() );
    pool.pause();
    REQUIRE( pool.is_paused() );
    pool.unpause();
    REQUIRE_FALSE( pool.is_paused() );
}

TEST_CASE( "wait()", "[task_pool]" )
{
    be::task_pool pool( 1 );
    pool.pause();
    REQUIRE( pool.get_tasks_total() == 0 );
    pool.submit( std::launch::async, [&]() -> void { std::this_thread::sleep_for( 1ms ); } );
    pool.submit( std::launch::async, [&]() -> void { std::this_thread::sleep_for( 1ms ); } );
    pool.submit( std::launch::async, [&]() -> void { std::this_thread::sleep_for( 1ms ); } );
    REQUIRE( pool.get_tasks_total() == 3 );
    pool.unpause();
    pool.wait();
    REQUIRE( pool.get_tasks_total() == 0 );
}

TEST_CASE( "wait() when paused", "[task_pool]" )
{
    be::task_pool pool( 1 );
    pool.pause();
    REQUIRE( pool.get_tasks_total() == 0 );
    pool.submit( std::launch::async, [&]() -> void { std::this_thread::sleep_for( 1ms ); } );
    pool.submit( std::launch::async, [&]() -> void { std::this_thread::sleep_for( 1ms ); } );
    pool.submit( std::launch::async, [&]() -> void { std::this_thread::sleep_for( 1ms ); } );
    REQUIRE( pool.get_tasks_total() == 3 );
    pool.wait(); // this must not block
}

void test_func_( std::atomic_bool* x )
{
    ( *x ) = true;
}

TEST_CASE( "free function", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    {
        be::task_pool pool( 1 );
        pool.submit( std::launch::async, &test_func_, &called );
        pool.wait();
        REQUIRE( called );
    }
}

struct test_
{
    std::atomic_bool called{ false };
    void             test_func_() { called = true; }
};

TEST_CASE( "member function", "[task_pool][submit]" )
{
    {
        test_         x;
        be::task_pool pool( 1 );
        pool.submit( std::launch::async, &test_::test_func_, &x ).wait();
        REQUIRE( x.called );
    }
}

TEST_CASE( "lambda pure by &", "[task_pool][submit]" )
{
    {
        std::atomic_bool called{ false };
        auto             fun = []( std::atomic_bool* x ) { ( *x ) = true; };
        be::task_pool    pool( 1 );
        pool.submit( std::launch::async, fun, &called ).wait();
        REQUIRE( called );
    }
}

TEST_CASE( "lambda pure by &&", "[task_pool][submit]" )
{
    {
        std::atomic_bool called{ false };
        be::task_pool    pool( 1 );
        pool.submit(
                std::launch::async, []( std::atomic_bool* x ) { ( *x ) = true; }, &called )
            .wait();
        REQUIRE( called );
    }
}

TEST_CASE( "lambda with capture by &", "[task_pool][submit]" )
{
    {
        std::atomic_bool called{ false };
        auto             x = [&]() { called = true; };
        be::task_pool    pool( 1 );
        pool.submit( std::launch::async, x ).wait();
        REQUIRE( called );
    }
}

TEST_CASE( "lambda with capture by &&", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    be::task_pool    pool( 1 );
    pool.submit( std::launch::async, [&]() { called = true; } ).wait();
    REQUIRE( called );
}

TEST_CASE( "std::function capture by &&", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    be::task_pool    pool( 1 );
    pool.submit( std::launch::async, std::function< void() >( [&]() { called = true; } ) ).wait();
    REQUIRE( called );
}

TEST_CASE( "std::function capture by &", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    be::task_pool    pool( 1 );
    auto             x = std::function< void() >( [&]() { called = true; } );
    pool.submit( std::launch::async, x ).wait();
    REQUIRE( called );
}

TEST_CASE( "std::function pure by &&", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    be::task_pool    pool( 1 );
    pool.submit( std::launch::async,
                 std::function< void( std::atomic_bool* ) >( []( auto* x ) { ( *x ) = true; } ),
                 &called )
        .wait();
    REQUIRE( called );
}

TEST_CASE( "std::function pure by &", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    be::task_pool    pool( 1 );
    auto             fun =
        std::function< void( std::atomic_bool* ) >( []( std::atomic_bool* x ) { ( *x ) = true; } );
    pool.submit( std::launch::async, fun, &called ).wait();
    REQUIRE( called );
}

TEST_CASE( "stateful lambda by &&", "[task_pool][submit]" )
{
    be::task_pool    pool( 1 );
    std::atomic_bool called{ false };
    pool.submit(
            std::launch::async,
            [value = 2]( std::atomic_bool* x ) mutable { ( *x ) = --value == 1; },
            &called )
        .wait();
    REQUIRE( called );
}

TEST_CASE( "stateful lambda from inner scope", "[task_pool][submit]" )
{
    be::task_pool pool( 1 );
    pool.pause();
    std::atomic_bool    called{ false };
    std::future< void > f;
    {
        f = pool.submit(
            std::launch::async,
            [value = 2]( std::atomic_bool* x ) mutable { ( *x ) = --value == 1; },
            &called );
    }
    pool.unpause();
    f.wait();
    REQUIRE( called );
}

TEST_CASE( "submit with result", "[task_pool][submit]" )
{
    be::task_pool pool( 1 );
    pool.pause();
    std::atomic_bool   called{ false };
    std::future< int > f;
    {
        f = pool.submit(
            std::launch::async,
            [value = 2]( std::atomic_bool* x ) mutable {
                ( *x ) = --value == 1;
                return value;
            },
            &called );
    }
    pool.unpause();
    auto result = f.get();
    REQUIRE( called );
    REQUIRE( result == 1 );
}

int func_with_future_( int x, std::atomic_bool* called )
{
    *called = true;
    return x;
}
TEST_CASE( "void( int ) with future ", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    {
        be::task_pool    pool( 1 );
        static const int s_value( 42 );
        auto             future = pool.submit( std::launch::async, []() { return s_value; } );
        auto             done =
            pool.submit( std::launch::async, &func_with_future_, std::move( future ), &called );
        done.wait();
        REQUIRE( called );
    }
}
TEST_CASE( "void( int ) with future throws ", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    {
        be::task_pool pool( 1 );
        auto future = pool.submit( std::launch::async, []() -> int { throw test_exception{}; } );
        auto done =
            pool.submit( std::launch::async, &func_with_future_, std::move( future ), &called );
        REQUIRE_THROWS_AS( done.get(), test_exception );
    }
}
struct counts
{
    std::atomic_uint64_t allocations{ 0 };
    std::atomic_uint64_t deallocations{ 0 };
    std::atomic_uint64_t constructions{ 0 };
};
#if defined( _WIN32 )
static counts s_counts;
#endif
template< class T >
struct counting_allocator
{
    counts* counter = nullptr;

    using allocator_traits = std::allocator_traits< std::allocator< T > >;
    using value_type       = T;
#ifdef _WIN32 // so windows only support allocators with default constructor?
    counting_allocator()
        : counter( &s_counts )
    {
    }
#endif // _WIN32

    explicit counting_allocator( counts& amounts ) noexcept
        : counter( &amounts )
    {
    } // cppcheck false positive on constParamter
    template< class U >
    explicit counting_allocator( const counting_allocator< U >& other ) noexcept
        : counter( other.counter )
    {
    }

    T* allocate( std::size_t n )
    {
        ++( *counter ).allocations;
        // std::cerr << "Allocating " << typeid(T).name() << '\n';
        std::allocator< T > alloc;
        return allocator_traits::allocate( alloc, n );
    }
    template< typename U, typename... Args >
    void construct( U* p, Args&&... args )
    {
        ++( *counter ).constructions;
        // std::cerr << "Constructing " << typeid(T).name() << '\n';
        std::allocator< T > alloc{};
        allocator_traits::construct( alloc, p, std::forward< Args >( args )... );
    }
    void deallocate( T* p, std::size_t n ) const
    {
        ++( *counter ).deallocations;
        // std::cerr << "Deallocating " << typeid(T).name() << '\n';
        std::allocator< T > alloc{};
        allocator_traits::deallocate( alloc, p, n );
    }
};

template< class T, class U >
constexpr bool operator==( const counting_allocator< T >& /*T*/,
                           const counting_allocator< U >& /*U*/ ) noexcept
{
    return true;
}

template< class T, class U >
constexpr bool operator!=( const counting_allocator< T >& /*T*/,
                           const counting_allocator< U >& /*U*/ ) noexcept
{
    return true;
}

TEST_CASE( "submit with result allocator", "[task_pool][submit][allocator]" )
{
    std::atomic_bool          called{ false };
    counts                    amounts;
    counting_allocator< int > alloc( amounts );

    {
        be::task_pool_t< counting_allocator< int > > pool( 1, alloc );
        pool.pause();
        std::future< int > f;
        {
            auto func = [value = 2]( std::atomic_bool* x ) mutable {
                ( *x ) = --value == 1;
                return value;
            };
            STATIC_REQUIRE_FALSE( be::wants_allocator< decltype( func ) >::value );
            f = pool.submit( std::launch::async, func, &called );
        }
        pool.unpause();
        auto result = f.get();
        REQUIRE( called );
        REQUIRE( result == 1 );
    }
    CHECK( amounts.allocations > 0 );
    CHECK( amounts.deallocations > 0 );
    CHECK( amounts.constructions > 0 );
}

void fun_with_token( be::stop_token /*unused*/ );

TEST_CASE( "wants_stop_token", "[task_pool][submit][stop_token]" )
{
    auto func     = []( be::stop_token /*unused*/ ) {};
    auto func_mut = []( be::stop_token /*unused*/ ) mutable {};
    STATIC_REQUIRE( be::wants_stop_token_v< void( be::stop_token ) > == true );
    STATIC_REQUIRE( be::wants_stop_token_v< decltype( &fun_with_token ) > == true );
    STATIC_REQUIRE( be::wants_stop_token_v< decltype( func ) > == true );
    STATIC_REQUIRE( be::wants_stop_token_v< decltype( func_mut ) > == true );
}

TEST_CASE( "submit with stop token", "[task_pool][submit][stop_token]" )
{
    be::task_pool       pool( 1 );
    std::atomic_bool    called{ false };
    std::future< void > f;
    {
        f = pool.submit( std::launch::async, [&called]( be::stop_token stop ) mutable {
            called = true;
            while ( !stop )
            {
                std::this_thread::sleep_for( 1ms );
            }
        } );
    }
    while ( !called )
    {
        std::this_thread::sleep_for( 1ms );
    }
    pool.abort();
    REQUIRE( f.wait_for( 1s ) == std::future_status::ready );
}

//
// Checking submit overloads - success branch
//
TEST_CASE( "void()& function", "[task_pool][submit]" )
{
    std::atomic_bool called;
    auto             fun = [&]() { called = true; };
    be::task_pool    pool( 1 );
    auto             future = pool.submit( std::launch::async, fun );
    future.wait();
    REQUIRE( called == true );
}
TEST_CASE( "void()&& function", "[task_pool][submit]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit( std::launch::async, [&]() { called = true; } );
    future.wait();
    REQUIRE( called == true );
}

TEST_CASE( "void()& function throws", "[task_pool][submit][throws]" )
{
    std::atomic_bool called;
    auto             fun = [&]() {
        called = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::launch::async, fun );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)& function", "[task_pool][submit][stop_token]" )
{
    std::atomic_bool called;
    auto fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable { *check = true; };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::launch::async, fun, &called );
    future.wait();
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)&& function", "[task_pool][submit][stop_token]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit(
        std::launch::async,
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable { *check = true; },
        &called );
    future.wait();
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)&& function throws", "[task_pool][submit][stop_token][throws]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit(
        std::launch::async,
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
            *check = true;
            throw test_exception{};
        },
        &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool()& function", "[task_pool][submit]" )
{
    std::atomic_bool called;
    auto             fun = [&]() {
        called = true;
        return true;
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::launch::async, fun );
    auto          actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool()&& function", "[task_pool][submit]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit( std::launch::async, [&]() {
        called = true;
        return true;
    } );
    auto             actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool()& function throws", "[task_pool][submit][throws]" )
{
    std::atomic_bool called;
    auto             fun = [&]() {
        called = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::launch::async, fun );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool(... be::stop_token)& function", "[task_pool][submit][stop_token]" )
{
    std::atomic_bool called;
    auto             fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
        *check = true;
        return true;
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::launch::async, fun, &called );
    auto          actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool(... be::stop_token)&& function", "[task_pool][submit][stop_token]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit(
        std::launch::async,
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
            *check = true;
            return true;
        },
        &called );
    auto actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool(... be::stop_token)& function throws", "[task_pool][submit][stop_token][throws]" )
{
    std::atomic_bool called;
    auto             fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) -> bool {
        *check = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::launch::async, fun, &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "void(allocator)& function ", "[task_pool][submit][allocator]" )
{
    std::atomic_bool called;
    auto fun = [&]( std::allocator_arg_t /*tag*/, std::allocator< int > const& /*alloc*/ ) {
        called = true;
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::launch::async, fun );
    future.wait();
    REQUIRE( called == true );
}

// Checking submit overloads with allocator - success branch
//
TEST_CASE( "void(allocator)& function throws ", "[task_pool][submit][allocator][throws]" )
{
    std::atomic_bool called;
    auto fun = [&]( std::allocator_arg_t /*tag*/, std::allocator< int > const& /*alloc*/ ) {
        called = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::launch::async, fun );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "void(allocator, ... be::stop_token)& function with allocator",
           "[task_pool][submit][stop_token][allocator]" )
{
    std::atomic_bool called;
    auto             fun = []( std::allocator_arg_t /*tag*/,
                   std::allocator< int > const& /*alloc*/,
                   std::atomic_bool* check,
                   be::stop_token /*abort*/ ) mutable { *check = true; };
    be::task_pool    pool( 1 );
    auto             future = pool.submit( std::launch::async, fun, &called );
    future.wait();
    REQUIRE( called == true );
}

TEST_CASE( "void(allocator, ... be::stop_token)& function with allocator throws",
           "[task_pool][submit][stop_token][allocator][throws]" )
{
    std::atomic_bool called;
    auto             fun = []( std::allocator_arg_t /*tag*/,
                   std::allocator< float > const& /*alloc*/,
                   std::atomic_bool* check,
                   be::stop_token /*abort*/ ) mutable {
        *check = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::launch::async, fun, &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool( allocator, ...  )&& function ", "[task_pool][submit][allocator]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit(
        std::launch::async,
        [&]( std::allocator_arg_t /*tag*/, std::allocator< int > const& /*alloc*/ ) -> bool {
            called = true;
            return true;
        } );
    REQUIRE_NOTHROW( future.get() );
    REQUIRE( called == true );
}

TEST_CASE( "bool( allocator, ... )&& function throws", "[task_pool][submit][allocator][throws]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit(
        std::launch::async,
        [&]( std::allocator_arg_t /*tag*/, std::allocator< int > const& /*alloc*/ ) -> bool {
            called = true;
            throw test_exception{};
        } );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool(allocator, ... be::stop_token)&& function",
           "[task_pool][submit][stop_token][allocator]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );

    auto function = []( std::allocator_arg_t /*tag*/,
                        std::allocator< int > const& /* alloc*/,
                        std::atomic_bool& check,
                        be::stop_token /*abort*/ ) -> bool {
        check = true;
        return true;
    };

    auto future = pool.submit( std::launch::async, function, std::ref( called ) );
    auto actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool(allocator, ... be::stop_token)&& function throws",
           "[task_pool][submit][stop_token][allocator][throws]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit(
        std::launch::async,

        []( std::allocator_arg_t /*tag*/,
            std::allocator< bool > const& /* alloc*/,
            std::atomic_bool& check,
            be::stop_token /*abort*/ ) -> bool {
            check = true;
            throw test_exception{};
        },
        std::ref( called ) );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

//
// is_future trait
//
struct FutureLike
{
    int  get();
    void wait();
    template< typename Duration >
    std::future_status wait_for( Duration );
    template< typename Time >
    std::future_status wait_until( Time );
};

struct NotFuture
{
};

struct NotQuiteFuture_1
{
    int  get();
    void wait();
    template< typename Duration >
    void wait_for( Duration );
    template< typename Time >
    std::future_status wait_until( Time );
};

struct NotQuiteFuture_2
{
    void wait();
    template< typename Duration >
    std::future_status wait_for( Duration );
    template< typename Time >
    std::future_status wait_until( Time );
};

struct NotQuiteFuture_3
{
    int  get();
    void wait();
    template< typename Time >
    std::future_status wait_until( Time );
};

struct NotQuiteFuture_4
{
    int  get();
    void wait();
    template< typename Duration >
    std::future_status wait_for( Duration );
};

struct NotQuiteFuture_5
{
    int  get();
    void wait();
    template< typename Duration >
    void wait_for( Duration );
    template< typename Time >
    std::future_status wait_until( Time );
};

struct NotQuiteFuture_6
{
    int  get();
    void wait();
    template< typename Duration >
    std::future_status wait_for( Duration );
    template< typename Time >
    void wait_until( Time );
};

TEST_CASE( "is_future<T>", "[task_pool][traits]" )
{
    STATIC_REQUIRE_FALSE( be::is_future< NotFuture >::value );
    STATIC_REQUIRE_FALSE( be::is_future< NotQuiteFuture_1 >::value );
    STATIC_REQUIRE_FALSE( be::is_future< NotQuiteFuture_2 >::value );
    STATIC_REQUIRE_FALSE( be::is_future< NotQuiteFuture_3 >::value );
    STATIC_REQUIRE_FALSE( be::is_future< NotQuiteFuture_4 >::value );
    STATIC_REQUIRE_FALSE( be::is_future< NotQuiteFuture_5 >::value );
    STATIC_REQUIRE_FALSE( be::is_future< NotQuiteFuture_6 >::value );
    STATIC_REQUIRE( be::is_future< std::future< void > >::value );
    STATIC_REQUIRE( be::is_future< FutureLike >::value );

    std::future< void > v;
}

TEST_CASE( "future_value_t<T>", "[task_pool][traits]" )
{
    STATIC_REQUIRE( std::is_same< void, be::future_value_t< std::future< void > > >::value );
    STATIC_REQUIRE( std::is_same< int, be::future_value_t< FutureLike > >::value );
}

TEST_CASE( "contains_future<Ts...>", "[task_pool][traits]" )
{
    STATIC_REQUIRE_FALSE( be::contains_future< float, int, double >::value );
    STATIC_REQUIRE( be::contains_future< float, int, std::future< double > >::value );
    STATIC_REQUIRE( be::contains_future< std::future< double >, float, int >::value );
}

TEST_CASE( "future_argument<T>", "[task_pool][traits]" )
{
    STATIC_REQUIRE( std::is_same< void, be::future_argument< void >::type >::value );
    STATIC_REQUIRE( std::is_same< void, be::future_argument< std::future< void > >::type >::value );
}

TEST_CASE( "submit( std::launch::async,  void(int), future )->void", "[task_pool][submit]" )
{
    const int           expected = 42;
    std::atomic_int     actual{ 0 };
    be::task_pool       pool( 1 );
    auto                fun_a  = []( int x ) { return x; };
    auto                fun_b  = [&]( int x ) { actual = x; };
    std::future< int >  future = pool.submit( std::launch::async, fun_a, expected );
    std::future< void > result = pool.submit( std::launch::async, fun_b, std::move( future ) );
    result.wait();
    REQUIRE( expected == actual );
}

TEST_CASE( "submit( std::launch::async,  int(int), future )->int", "[task_pool][submit]" )
{
    const int          expected = 42;
    be::task_pool      pool( 1 );
    auto               fun    = []( int x ) { return x; };
    std::future< int > future = pool.submit( std::launch::async, fun, expected );
    std::future< int > result = pool.submit( std::launch::async, fun, std::move( future ) );
    result.wait();
    REQUIRE( result.get() == expected );
}

TEST_CASE( "submit( std::launch::async,  f, future, ... ) -> void", "[task_pool][submit]" )
{
    const int           X = 42;
    const int           Y = 42;
    std::atomic_int     actual{ 0 };
    be::task_pool       pool( 1 );
    auto                fun_a    = []( int x ) { return x; };
    std::future< int >  future_a = pool.submit( std::launch::async, fun_a, X );
    auto                fun_b    = [&]( int x, int y ) { actual = x * y; };
    std::future< void > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( actual == X * Y );
}

TEST_CASE( "submit( std::launch::async,  f, future, ... ) -> int", "[task_pool][submit]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 2 );
    auto               fun_a    = []( int x ) { return x; };
    std::future< int > future_a = pool.submit( std::launch::async, fun_a, X );
    auto               fun_b    = []( int x, int y ) { return x * y; };
    std::future< int > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( result.get() == X * Y );
}

TEST_CASE( "submit( std::launch::async,  f(stop_token), future, ... ) -> int",
           "[task_pool][submit][stop_token]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x, be::stop_token /*abort*/ ) { return x; };
    std::future< int > future_a = pool.submit( std::launch::async, fun_a, X );
    auto               fun_b    = []( int x, int y, be::stop_token /*abort*/ ) { return x * y; };
    std::future< int > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( result.get() == X * Y );
}

TEST_CASE( "submit( std::launch::async,  f(stop_token), future, ... ) -> void",
           "[task_pool][submit][stop_token]" )
{
    const int           X = 42;
    const int           Y = 42;
    std::atomic_int     actual{ 0 };
    be::task_pool       pool( 1 );
    auto                fun_a    = []( int x, be::stop_token /*abort*/ ) { return x; };
    std::future< int >  future_a = pool.submit( std::launch::async, fun_a, X );
    auto                fun_b  = [&]( int x, int y, be::stop_token /*abort*/ ) { actual = x * y; };
    std::future< void > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( actual == X * Y );
}

TEST_CASE( "submit( std::launch::async,  f(allocator), future, ... ) -> int",
           "[task_pool][submit][allocator]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 2 );
    auto               fun_a    = []( int x ) { return x; };
    std::future< int > future_a = pool.submit( std::launch::async, fun_a, X );
    auto               fun_b =
        []( std::allocator_arg_t /*tag*/, std::allocator< int > const& /*alloc*/, int x, int y ) {
            return x * y;
        };
    std::future< int > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( result.get() == X * Y );
}

TEST_CASE( "submit( std::launch::async,  f(allocator), future, ... ) -> int throws",
           "[task_pool][submit][allocator][throws]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x ) { return x; };
    std::future< int > future_a = pool.submit( std::launch::async, fun_a, X );
    auto               fun_b    = []( std::allocator_arg_t /*tag*/,
                     std::allocator< int > const& /*alloc*/,
                     int /*x*/,
                     int /*y*/ ) -> int { throw test_exception{}; };
    std::future< int > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( std::launch::async,  f(allocator,...), future, ... ) -> void",
           "[task_pool][submit][allocator]" )
{
    const int          X = 42;
    const int          Y = 42;
    std::atomic_int    actual{ 0 };
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x ) { return x; };
    std::future< int > future_a = pool.submit( std::launch::async, fun_a, X );
    auto               fun_b =
        [&]( std::allocator_arg_t /*tag*/, std::allocator< int > const& /*alloc*/, int x, int y ) {
            actual = x * y;
        };
    std::future< void > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( actual == X * Y );
}

TEST_CASE( "submit( std::launch::async,  f(allocator,...), future, ... ) -> void throws",
           "[task_pool][submit][allocator][throws]" )
{
    const int           X = 42;
    const int           Y = 42;
    be::task_pool       pool( 1 );
    auto                fun_a    = []( int x ) { return x; };
    std::future< int >  future_a = pool.submit( std::launch::async, fun_a, X );
    auto                fun_b    = [&]( std::allocator_arg_t /*tag*/,
                      std::allocator< int > const& /*alloc*/,
                      int /*x*/,
                      int /*y*/ ) -> void { throw test_exception{}; };
    std::future< void > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( std::launch::async,  f(allocator,... , stop_token), future, ... ) -> void",
           "[task_pool][submit][allocator]" )
{
    const int           X = 42;
    const int           Y = 42;
    std::atomic_int     actual{ 0 };
    be::task_pool       pool( 1 );
    auto                fun_a    = []( int x ) { return x; };
    std::future< int >  future_a = pool.submit( std::launch::async, fun_a, X );
    auto                fun_b    = [&]( std::allocator_arg_t /*tag*/,
                      std::allocator< int > const& /*alloc*/,
                      int x,
                      int y,
                      be::stop_token /*token*/ ) { actual = x * y; };
    std::future< void > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( actual == X * Y );
}

TEST_CASE(
    "submit( std::launch::async,  f(allocator,... , stop_token), future, ... ) -> void throws",
    "[task_pool][submit][allocator][throws]" )
{
    const int          X = 42;
    const int          Y = 42;
    std::atomic_int    actual{ 0 };
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x ) { return x; };
    std::future< int > future_a = pool.submit( std::launch::async, fun_a, X );
    auto               fun_b    = [&]( std::allocator_arg_t /*tag*/,
                      std::allocator< int > const& /*alloc*/,
                      int x,
                      int y,
                      be::stop_token /*token*/ ) {
        actual = x * y;
        throw test_exception{};
    };
    std::future< void > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
    REQUIRE( actual == X * Y );
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE( "submit( std::launch::async,  f, future )->void throws", "[task_pool][submit][throws]" )
{
    const int           expected = 42;
    be::task_pool       pool( 1 );
    auto                fun_a  = []( int x ) -> int { return x; };
    auto                fun_b  = [&]( int ) { throw test_exception{}; }; // NOLINT
    std::future< int >  future = pool.submit( std::launch::async, fun_a, expected );
    std::future< void > result = pool.submit( std::launch::async, fun_b, std::move( future ) );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

struct test_processor
{
    int run( int value ) const noexcept // NOLINT
    {
        return value;
    }
};

// TEST_CASE( "submit( std::launch::async,  f(member), instance, future )->int ",
//            "[task_pool][submit]" )
// {
//     const int          expected = 42;
//     be::task_pool      pool( 1 );
//     auto               fun_a  = []( int x ) -> int { return x; };
//     std::future< int > future = pool.submit( std::launch::async, fun_a, expected );
//     test_processor     instance;
//     std::future< int > result =
//         pool.submit( std::launch::async, &test_processor::run, &instance, std::move( future ) );
//     REQUIRE( result.get() == expected );
// }
// TEST_CASE( "submit( std::launch::async,  f(member), instance, future )->int throws ",
//            "[task_pool][submit][throws]" )
// {
//     const int          expected = 42;
//     be::task_pool      pool( 1 );
//     auto               fun_a  = []( int /*x*/ ) -> int { throw test_exception{}; };
//     std::future< int > future = pool.submit( std::launch::async, fun_a, expected );
//     test_processor     instance;
//     std::future< int > result =
//         pool.submit( std::launch::async, &test_processor::run, &instance, std::move( future ) );
//     REQUIRE_THROWS_AS( result.get(), test_exception );
// }

void func_run_( int value, std::atomic_bool& called )
{
    called = value != 0;
}
TEST_CASE( "submit( std::launch::async,  f(free func), instance, future )->int ",
           "[task_pool][submit]" )
{
    std::atomic_bool    called{ false };
    const int           expected = 42;
    be::task_pool       pool( 1 );
    auto                fun_a  = []( int x ) -> int { return x; };
    std::future< int >  future = pool.submit( std::launch::async, fun_a, expected );
    std::future< void > result =
        pool.submit( std::launch::async, &func_run_, std::move( future ), std::ref( called ) );
    REQUIRE_NOTHROW( result.get() );
    REQUIRE( called );
}

TEST_CASE( "submit( std::launch::async,  f, future )->int throws", "[task_pool][submit][throws]" )
{
    const int          expected = 42;
    be::task_pool      pool( 1 );
    auto               fun    = []( int ) -> int { throw test_exception{}; }; // NOLINT
    std::future< int > result = pool.submit( std::launch::async, fun, expected );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( std::launch::async,  f, future, ... ) -> void throws",
           "[task_pool][submit][throws]" )
{
    const int           X = 42;
    const int           Y = 42;
    be::task_pool       pool( 1 );
    auto                fun_a    = []( int x ) { return x; };
    std::future< int >  future_a = pool.submit( std::launch::async, fun_a, X );
    auto                fun_b    = [&]( int, int ) { throw test_exception{}; }; // NOLINT
    std::future< void > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( std::launch::async,  f, future, ... ) -> int throws",
           "[task_pool][submit][throws]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x ) { return x; };
    std::future< int > future_a = pool.submit( std::launch::async, fun_a, X );
    auto               fun_b    = []( int, int ) -> int { throw test_exception{}; }; // NOLINT
    std::future< int > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( std::launch::async,  f(stop_token), future, ... ) -> int throws",
           "[task_pool][submit][stop_token][throws]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x, be::stop_token /*abort*/ ) { return x; };
    std::future< int > future_a = pool.submit( std::launch::async, fun_a, X );
    auto               fun_b    = []( int, int, be::stop_token /*abort*/ ) -> int { // NOLINT
        throw test_exception{};
    };
    std::future< int > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( std::launch::async,  f(stop_token), future, ... ) -> void throws",
           "[task_pool][submit][stop_token][throws]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x, be::stop_token /*abort*/ ) { return x; };
    std::future< int > future_a = pool.submit( std::launch::async, fun_a, X );
    auto fun_b = [&]( int, int, be::stop_token /*abort*/ ) { throw test_exception{}; }; // NOLINT
    std::future< void > result = pool.submit( std::launch::async, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

//
// Input value types
//
TEST_CASE( "reference_wrapper to & argument", "[task_pool][submit]" )
{
    be::task_pool pool( 1 );
    pool.pause();
    std::atomic_int actual{ 0 };
    auto            task     = [&]( int const& x ) { actual = x; };
    const int       value    = 42;
    int             expected = value;
    auto            future   = pool.submit( std::launch::async, task, std::ref( expected ) );
    expected *= 2;
    pool.unpause();
    future.wait();
    REQUIRE( actual == expected );
}

//
// task function can take a allocator in their signature and we should then pass them
// the task allocator
//

TEST_CASE( "contains_allocator_args", "[task_pool][traits][allocator]" )
{
    STATIC_REQUIRE_FALSE( be::contains_allocator_arg< int, float, double >::value );
    STATIC_REQUIRE_FALSE( be::contains_allocator_arg< int >::value );
    STATIC_REQUIRE( be::contains_allocator_arg< int, std::allocator_arg_t, double >::value );
    STATIC_REQUIRE( be::contains_allocator_arg< std::allocator_arg_t >::value );
}

TEST_CASE( "wants_allocator", "[task_pool][traits][allocator]" )
{
    STATIC_REQUIRE_FALSE( be::wants_allocator< int ( * )( float, int, double ) >::value );
    STATIC_REQUIRE_FALSE( be::wants_allocator< void ( * )() >::value );
    STATIC_REQUIRE(
        be::wants_allocator< int ( * )( float, std::allocator_arg_t, int, double ) >::value );
    STATIC_REQUIRE( be::wants_allocator< void ( * )( std::allocator_arg_t ) >::value );
    STATIC_REQUIRE( be::wants_allocator_v< void ( * )( std::allocator_arg_t ) > );
    STATIC_REQUIRE( be::is_allocator_constructible< std::promise< void > >::value );
    STATIC_REQUIRE( be::is_allocator_constructible< std::promise< void > >::value );
    struct mock
    {
        mock();
    };
    STATIC_REQUIRE_FALSE( be::is_allocator_constructible< mock >::value );

    auto func_one = []() {};
    STATIC_REQUIRE_FALSE( be::wants_allocator_v< decltype( func_one ) > );
    auto func_two = []( int /*tag*/ ) mutable {};
    STATIC_REQUIRE_FALSE( be::wants_allocator_v< decltype( func_two ) > );
    auto func_three = []( std::allocator_arg_t /*tag*/, int /*tag*/ ) mutable {};
    STATIC_REQUIRE( be::wants_allocator_v< decltype( func_three ) > );
}

// Wants allocator

TEST_CASE( "( allocator, ... ) -> void", "[task_pool][submit][allocator]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    using data_type = std::vector< int, counting_allocator< int > >;
    std::atomic_size_t actual{ 0 };
    auto               make_data = [&]( std::allocator_arg_t /*tag*/,
                          counting_allocator< int > const& alloc,
                          std::size_t                      count ) mutable {
        data_type data( count, 1, alloc );
        actual = data.size();
    };

    std::size_t const                            value_counts = 1000;
    be::task_pool_t< counting_allocator< int > > pool( allocator );
    auto result = pool.submit( std::launch::async, make_data, std::size_t{ value_counts } );
    pool.wait();
    REQUIRE_NOTHROW( result.get() );
    CHECK( allocations.allocations > 0 );
    CHECK( allocations.constructions > value_counts );
    CHECK( allocations.deallocations > 0 );
}

TEST_CASE( "( allocator, ... ) -> void throws", "[task_pool][submit][allocator][throws]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );
    std::atomic_size_t        actual{ 0 };
    using data_type = std::vector< int, counting_allocator< int > >;
    auto make_data  = [&]( std::allocator_arg_t /*tag*/,
                          counting_allocator< int > const& alloc,
                          std::size_t                      count ) mutable {
        data_type data( count, 1, alloc );
        actual = data.size();
        throw test_exception{};
    };

    std::size_t const                            value_counts = 1000;
    be::task_pool_t< counting_allocator< int > > pool( allocator );
    auto result = pool.submit( std::launch::async, make_data, std::size_t{ value_counts } );
    pool.wait();
    REQUIRE_THROWS_AS( result.get(), test_exception );
    CHECK( allocations.allocations > 0 );
    CHECK( allocations.constructions > value_counts );
    CHECK( allocations.deallocations > 0 );
}

TEST_CASE( "( allocator, ... ) -> size_t", "[task_pool][submit][allocator]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    using data_type = std::vector< int, counting_allocator< int > >;
    // auto make_data  = [&]( std::allocator_arg_t /*tag*/,
    //                       counting_allocator< int > const& alloc,
    //                       std::size_t count ) -> void { data_type data( count, 1, alloc ); };
    std::atomic_size_t actual{ 0 };
    auto               make_data = [&]( std::allocator_arg_t /*tag*/,
                          counting_allocator< int > const& alloc,
                          std::size_t                      count ) {
        data_type data( count, 1, alloc );
        actual = data.size();
    };

    std::size_t const                            value_counts = 1000;
    be::task_pool_t< counting_allocator< int > > pool( allocator );

    auto result = pool.submit( std::launch::async, make_data, std::size_t{ value_counts } );
    pool.wait();
    // REQUIRE( result.get().size() == value_counts ); // doh
    CHECK( allocations.allocations > 0 );
    CHECK( allocations.constructions > value_counts );
    CHECK( allocations.deallocations > 0 );
}

TEST_CASE( "( allocator, ..., stop_token) -> size_t", "[task_pool][submit][allocator][stop_token]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    using data_type = std::vector< int, counting_allocator< int > >;
    auto make_data  = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t                      count,
                         be::stop_token /*token*/ ) { return data_type( count, 1, alloc ); };

    auto process_data = []( data_type&& x ) { return x.size(); };

    std::size_t const                            value_counts = 1000;
    be::task_pool_t< counting_allocator< int > > pool( allocator );
    auto data   = pool.submit( std::launch::async, make_data, std::size_t{ value_counts } );
    auto result = pool.submit( std::launch::async, process_data, std::move( data ) );
    pool.wait();
    REQUIRE( value_counts == result.get() ); // doh
    REQUIRE( allocations.allocations > 0 );
    REQUIRE( allocations.constructions > value_counts );
    REQUIRE( allocations.deallocations > 0 );
}

TEST_CASE( "( allocator, ..., stop_token) -> size_t throws",
           "[task_pool][submit][allocator][stop_token][throws]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    using data_type = std::vector< int, counting_allocator< int > >;
    auto make_data  = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& /*alloc*/,
                         std::size_t /*count*/,
                         be::stop_token /*token*/ ) -> data_type { throw test_exception{}; };

    auto process_data = []( data_type&& x ) { return x.size(); };

    std::size_t const                            value_counts = 1000;
    be::task_pool_t< counting_allocator< int > > pool( allocator );
    auto data   = pool.submit( std::launch::async, make_data, std::size_t{ value_counts } );
    auto result = pool.submit( std::launch::async, process_data, std::move( data ) );
    pool.wait();
    REQUIRE_THROWS_AS( result.get(), test_exception ); // doh
}

TEST_CASE( "( allocator, ... ) -> void no.2", "[task_pool][submit][allocator]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    using data_type = std::vector< int, counting_allocator< int > >;
    auto make_data  = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t count ) { return data_type( count, 1, alloc ); };

    auto process_data = []( data_type&& x ) { return x.clear(); };

    std::size_t const                            value_counts = 1000;
    be::task_pool_t< counting_allocator< int > > pool( allocator );
    auto data   = pool.submit( std::launch::async, make_data, std::size_t{ value_counts } );
    auto result = pool.submit( std::launch::async, process_data, std::move( data ) );
    pool.wait();
    result.wait();
    REQUIRE( allocations.allocations > 0 );
    REQUIRE( allocations.constructions > value_counts );
    REQUIRE( allocations.deallocations > 0 );
}

TEST_CASE( "( allocator, ..., stop_token ) -> void", "[task_pool][submit][allocator][stop_token]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    using data_type = std::vector< int, counting_allocator< int > >;
    auto make_data  = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t                      count,
                         be::stop_token /*token*/ ) { return data_type( count, 1, alloc ); };

    auto process_data = []( data_type&& x ) { return x.clear(); };

    std::size_t const                            value_counts = 1000;
    be::task_pool_t< counting_allocator< int > > pool( allocator );
    auto data   = pool.submit( std::launch::async, make_data, std::size_t{ value_counts } );
    auto result = pool.submit( std::launch::async, process_data, std::move( data ) );
    pool.wait();
    result.wait();
    REQUIRE( allocations.allocations > 0 );
    REQUIRE( allocations.constructions > value_counts );
    REQUIRE( allocations.deallocations > 0 );
}

//
// Wants allocator + future
//
TEST_CASE( "( allocator, future ) -> size_t", "[task_pool][submit][allocator]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    using data_type                       = std::vector< int, counting_allocator< int > >;
    static std::size_t const value_counts = 1000;
    auto                     make_data    = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t count ) { return data_type( count, 1, alloc ); };

    auto process_data = []( data_type&& x ) { return x.size(); };

    {
        be::task_pool_t< counting_allocator< int > > pool( allocator );
        auto                                         get_count = []() { return value_counts; };
        auto value  = pool.submit( std::launch::async, get_count );
        auto data   = pool.submit( std::launch::async, make_data, std::move( value ) );
        auto result = pool.submit( std::launch::async, process_data, std::move( data ) );
        REQUIRE( value_counts == result.get() ); // doh
        REQUIRE( allocations.allocations > 0 );
        REQUIRE( allocations.constructions > value_counts );
        REQUIRE( allocations.deallocations > 0 );
    }
}

TEST_CASE( "( allocator, future ) -> size_t throws #1", "[task_pool][submit][allocator][throws]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    using data_type                = std::vector< int, counting_allocator< int > >;
    std::size_t const value_counts = 1000;
    auto              make_data    = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& /*alloc*/,
                         std::size_t /*count*/ ) -> data_type { throw test_exception{}; };

    auto process_data = []( data_type&& x ) { return x.size(); };

    {
        be::task_pool_t< counting_allocator< int > > pool( allocator );

        auto get_count = [=]() { return value_counts; };
        auto value     = pool.submit( std::launch::async, get_count );
        auto data      = pool.submit( std::launch::async, make_data, std::move( value ) );
        auto result    = pool.submit( std::launch::async, process_data, std::move( data ) );
        REQUIRE_THROWS_AS( result.get(), test_exception );
    }
}

TEST_CASE( "( allocator, future ) -> size_t throws #2", "[task_pool][submit][allocator][throws]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    std::size_t const value_counts = 1000;
    auto              make_data = [=]( std::size_t count ) { return std::vector< int >( count ); };

    auto process_data = []( std::allocator_arg_t /*tag*/,
                            counting_allocator< int > const& /*alloc*/,
                            std::vector< int > /*data*/ ) -> size_t { // NOLINT
        throw test_exception{};
    }; // NOLINT

    {
        be::task_pool_t< counting_allocator< int > > pool( 4, allocator );

        auto get_count = [=]() { return value_counts; };
        auto value     = pool.submit( std::launch::async, get_count );
        auto data      = pool.submit( std::launch::async, make_data, std::move( value ) );
        auto result    = pool.submit( std::launch::async, process_data, std::move( data ) );
        REQUIRE_THROWS_AS( result.get(), test_exception );
    }
}

TEST_CASE( "( allocator, future, stop_token ) -> void",
           "[task_pool][submit][allocator][stop_token]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    std::size_t const value_counts = 1000;
    auto              make_data = []( std::size_t count ) { return std::vector< int >( count ); };

    auto process_data = []( std::allocator_arg_t /*tag*/,
                            counting_allocator< int > const& /*alloc*/,
                            std::vector< int > /*data*/, // NOLINT
                            be::stop_token /*token*/ ) -> void {
        throw test_exception{};
    }; // NOLINT

    {
        be::task_pool_t< counting_allocator< int > > pool( allocator );

        auto get_count = [=]() { return value_counts; };
        auto value     = pool.submit( std::launch::async, get_count );
        auto data      = pool.submit( std::launch::async, make_data, std::move( value ) );
        auto result    = pool.submit( std::launch::async, process_data, std::move( data ) );
        REQUIRE_THROWS_AS( result.get(), test_exception );
    }
}

TEST_CASE( "( allocator, future, stop_token ) -> void throws",
           "[task_pool][submit][allocator][stop_token][throws]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    std::size_t const value_counts = 1000;
    auto              make_data = []( std::size_t count ) { return std::vector< int >( count ); };

    auto process_data = []( std::allocator_arg_t /*tag*/,
                            counting_allocator< int > const& /*alloc*/,
                            std::vector< int > /*data*/, // NOLINT
                            be::stop_token /*token*/ ) -> void {
        throw test_exception{};
    }; // NOLINT

    {
        be::task_pool_t< counting_allocator< int > > pool( allocator );

        auto get_count = [=]() { return value_counts; };
        auto value     = pool.submit( std::launch::async, get_count );
        auto data      = pool.submit( std::launch::async, make_data, std::move( value ) );
        auto result    = pool.submit( std::launch::async, process_data, std::move( data ) );
        REQUIRE_THROWS_AS( result.get(), test_exception );
    }
}

TEST_CASE( "( allocator, future, stop_token ) -> size_t throws #1",
           "[task_pool][submit][allocator][stop_token][throws]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    std::size_t const value_counts = 1000;
    auto              make_data = []( std::size_t count ) { return std::vector< int >( count ); };

    auto process_data = []( std::allocator_arg_t /*tag*/,
                            counting_allocator< int > const& /*alloc*/,
                            std::vector< int > /*data*/,                            // NOLINT
                            be::stop_token /*token*/ ) { throw test_exception{}; }; // NOLINT

    {
        be::task_pool_t< counting_allocator< int > > pool( 4, allocator );

        auto get_count = [=]() { return value_counts; };
        auto value     = pool.submit( std::launch::async, get_count );
        auto data      = pool.submit( std::launch::async, make_data, std::move( value ) );
        auto result    = pool.submit( std::launch::async, process_data, std::move( data ) );
        REQUIRE_THROWS_AS( result.get(), test_exception );
    }
}

TEST_CASE( "( allocator, future, stop_token ) -> size_t",
           "[task_pool][submit][allocator][stop_token]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );
    using data_type = std::vector< int, counting_allocator< int > >;

    std::size_t const value_counts = 1000;
    auto              get_count    = [=]() { return value_counts; };
    auto              make_data    = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t                      count,
                         be::stop_token /*token*/ ) { return data_type( count, 1, alloc ); };

    auto process_data = []( data_type&& x ) { return x.size(); };

    be::task_pool_t< counting_allocator< int > > pool( allocator );

    auto value  = pool.submit( std::launch::async, get_count );
    auto data   = pool.submit( std::launch::async, make_data, std::move( value ) );
    auto result = pool.submit( std::launch::async, process_data, std::move( data ) );
    pool.wait();
    REQUIRE( value_counts == result.get() ); // doh
    REQUIRE( allocations.allocations > 0 );
    REQUIRE( allocations.constructions > value_counts );
    REQUIRE( allocations.deallocations > 0 );
}

TEST_CASE( "( allocator, future, stop_token ) -> size_t no.2",
           "[task_pool][submit][allocator][stop_token]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );
    using data_type = std::vector< int, counting_allocator< int > >;

    std::size_t const value_counts = 1000;
    auto              get_count    = [=]() { return value_counts; };
    auto              make_data    = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t                      count,
                         be::stop_token /*token*/ ) { return data_type( count, 1, alloc ); };

    auto process_data = []( std::allocator_arg_t /*tag*/,
                            counting_allocator< int > const& /*alloc*/,
                            data_type&& /*x*/,
                            be::stop_token /*token*/ ) -> std::size_t { throw test_exception{}; };

    be::task_pool_t< counting_allocator< int > > pool( allocator );

    auto value  = pool.submit( std::launch::async, get_count );
    auto data   = pool.submit( std::launch::async, make_data, std::move( value ) );
    auto result = pool.submit( std::launch::async, process_data, std::move( data ) );
    pool.wait();
    REQUIRE_THROWS_AS( result.get(), test_exception ); // doh
    REQUIRE( allocations.allocations > 0 );
    REQUIRE( allocations.constructions > value_counts );
    REQUIRE( allocations.deallocations > 0 );
}

TEST_CASE( "( allocator, future ) -> void", "[task_pool][submit][allocator]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    using data_type = std::vector< int, counting_allocator< int > >;

    std::size_t const value_counts = 1000;
    auto              get_count    = [=]() { return value_counts; };

    auto make_data = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t count ) { return data_type( count, 1, alloc ); };

    auto process_data = []( data_type&& x ) { return x.clear(); };

    be::task_pool_t< counting_allocator< int > > pool( allocator );

    auto value  = pool.submit( std::launch::async, get_count );
    auto data   = pool.submit( std::launch::async, make_data, std::move( value ) );
    auto result = pool.submit( std::launch::async, process_data, std::move( data ) );
    pool.wait();
    result.wait();
    REQUIRE( allocations.allocations > 0 );
    REQUIRE( allocations.constructions > value_counts );
    REQUIRE( allocations.deallocations > 0 );
}

TEST_CASE( "( allocator, future, stop_token ) -> void no.2",
           "[task_pool][submit][allocator][stop_token]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    using data_type                = std::vector< int, counting_allocator< int > >;
    std::size_t const value_counts = 1000;
    auto              get_count    = [=]() { return value_counts; };

    auto make_data = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t count ) { return data_type( count, 1, alloc ); };

    auto process_data = []( std::allocator_arg_t /*tag*/,
                            counting_allocator< int > const& /*alloc*/,
                            data_type&& x,
                            be::stop_token /*token*/ ) { return x.clear(); };

    be::task_pool_t< counting_allocator< int > > pool( allocator );

    auto value  = pool.submit( std::launch::async, get_count );
    auto data   = pool.submit( std::launch::async, make_data, std::move( value ) );
    auto result = pool.submit( std::launch::async, process_data, std::move( data ) );
    pool.wait();
    result.wait();
    REQUIRE( allocations.allocations > 0 );
    REQUIRE( allocations.constructions > value_counts );
    REQUIRE( allocations.deallocations > 0 );
}

//
// Custom promise types
//
struct Future
{
    enum class Status
    {
        ready,
        timeout,
        deferred
    };
    void   get();
    void   wait();
    Status wait_for( std::chrono::steady_clock::duration );
    Status wait_until( std::chrono::steady_clock::time_point );
};

template< typename T >
struct Promise
{
    Promise();
    Future get_future();
    void   set_value( T );
    void   set_exception( std::exception_ptr );
};

template<>
struct Promise< void >
{
    Promise();
    Future get_future();
    void   set_value();
    void   set_exception( std::exception_ptr );
};

TEST_CASE( "is_future", "[traits]" )
{
    STATIC_REQUIRE( be::is_future< std::future< void > >::value );
    STATIC_REQUIRE( be::is_future< Future >::value );
}

TEST_CASE( "is_promise", "[traits]" )
{
    STATIC_REQUIRE( be::is_promise< std::promise >::value );
    STATIC_REQUIRE( be::is_promise< Promise >::value );
    STATIC_REQUIRE( be::is_promise_v< Promise > );
    STATIC_REQUIRE_FALSE( be::is_promise_v< std::future > );
}

template< typename T >
struct my_future : public std::future< T >
{
    explicit my_future( std::future< T > x )
        : std::future< T >( std::move( x ) )
    {
    }
};

template< typename T >
struct my_promise : public std::promise< T >
{
    my_promise()
        : std::promise< T >()
    {
    }
    my_future< T > get_future()
    {
        return my_future< T >( std::move( std::promise< T >::get_future() ) );
    }
};

TEST_CASE( "submit<my_promise>( ... )", "[submit][promises]" )
{
    static int const counts    = 1'000'000;
    auto             make_data = []( std::size_t x ) {
        std::vector< int > values( x );
        std::iota( values.begin(), values.end(), 1 );
        return values;
    };
    auto check_values = []( std::vector< int > vec ) { // NOLINT
        REQUIRE( vec.size() == counts );
    };
    be::task_pool pool;
    auto          value  = pool.submit( std::launch::async, make_data, counts );
    auto          result = pool.submit( std::launch::async, check_values, std::move( value ) );
    pool.wait();
}

//
// Tasks with lazy arguments should be abortable without taking stop tokens if they have not started
//

TEST_CASE( "abort when not started", "[stop_token]" )
{
    be::task_pool    pool;
    std::atomic_bool started{ false };
    auto             make_data = [&]( std::size_t x, be::stop_token abort ) {
        started = true;
        std::vector< int > values( x );
        std::iota( values.begin(), values.end(), 1 );
        while ( !abort )
        {
            std::this_thread::sleep_for( 1us );
        }
        return values;
    };
    std::atomic_bool called{ false };
    auto             check_values = [&]( std::vector< int > vec ) { // NOLINT
        called = true;
        vec.clear();
    };
    static const std::size_t s_count = 1'000;
    auto                     data    = pool.submit( std::launch::async, make_data, s_count );
    auto result = pool.submit( std::launch::async, check_values, std::move( data ) );
    while ( !started )
    {
        std::this_thread::sleep_for( 1us );
    }
    // right so now make_data would have started so we should now be able to call abort on the
    // submit function make_data should finish and check_values should not be called
    pool.abort();
    REQUIRE_FALSE( called );
}

//
// Execicing task_proxy::operator()=
//

TEST_CASE( "task_proxy move assignment", "[task_proxy]" )
{
    be::task_pool     pool;
    static const auto s_us_100 = 100us;
    static const auto s_us_10  = 10us;
    static const auto s_us_1   = 1us;
    static const auto s_ms_10  = 10ms;
    static const auto s_ms_1   = 1ms;
    auto              us_1     = []() {
        std::this_thread::sleep_for( s_us_1 );
        return s_us_1; //
    };
    auto us_10 = []() {
        std::this_thread::sleep_for( s_us_10 );
        return s_us_10;
    };
    auto us_100 = []() {
        std::this_thread::sleep_for( s_us_100 );
        return s_us_100;
    };
    auto ms_1 = []() {
        std::this_thread::sleep_for( s_ms_1 );
        return s_ms_1;
    };
    auto ms_10 = []() {
        std::this_thread::sleep_for( s_ms_10 );
        return s_ms_10;
    };

    // ok so what we want to do is submit some tasks that depend on some inputs in such a way
    // that the items at the front of the checklist will finish after the items at the end of
    // the checklist. When the pool is running and going through the checklist this should then
    // trigger iter_swap in the call to std::partion in check_tasks between the front and the back
    // and this should test that our task_proxy::operator= function works as expected and transfers
    // the correct function pointers and storage

    namespace cc  = std::chrono;
    auto res_10ms = pool.submit(
        std::launch::async,
        [v = s_ms_10]( cc::milliseconds x ) { return x + v; },
        pool.submit( std::launch::async, ms_10 ) );
    auto res_1ms = pool.submit(
        std::launch::async,
        [v = s_ms_1]( cc::milliseconds x ) { return x + v; },
        pool.submit( std::launch::async, ms_1 ) );
    auto res_1us = pool.submit(
        std::launch::async,
        [v = s_us_1]( cc::microseconds x ) {
            auto value = v;
            return x + value;
        },
        pool.submit( std::launch::async, us_1 ) );
    auto res_10us = pool.submit(
        std::launch::async,
        [v = s_us_10]( cc::microseconds x ) { return x + v; },
        pool.submit( std::launch::async, us_10 ) );
    auto res_100us = pool.submit(
        std::launch::async,
        [v = s_us_100]( cc::microseconds x ) { return x + v; },
        pool.submit( std::launch::async, us_100 ) );

    pool.wait();
    REQUIRE( res_10ms.get() == s_ms_10 * 2 );
    REQUIRE( res_1ms.get() == s_ms_1 * 2 );
    REQUIRE( res_100us.get() == s_us_100 * 2 );
    REQUIRE( res_10us.get() == s_us_10 * 2 );
    REQUIRE( res_1us.get() == s_us_1 * 2 );
}

TEST_CASE( "pipe temporaries block", "[pipe]" )
{
    be::task_pool pool;

    static_assert( be::is_pool< be::task_pool >::value, "nop" );

    std::atomic_bool called{ false };
    auto             first = [] {
        std::this_thread::sleep_for( 1us );
        return 1;
    };
    auto second = [&]( int ) { called = true; }; // NOLINT

    // if a pipe object is left uncaptured it will call wait() on its future at destruction
    pool | first | second;
    REQUIRE( called );
}

TEST_CASE( "pipe temporaries throws", "[pipe][throws]" )
{
    be::task_pool    pool;
    std::atomic_bool called{ false };
    auto             first = [&]() -> int {
        auto when = std::chrono::steady_clock::now() + 1ms;
        std::this_thread::sleep_until( when );
        throw test_exception{};
    };
    auto second = [&]( int ) { called = true; }; // NOLINT

    try
    {
        pool | first | second;
    }
    catch ( ... )
    {
    }
    REQUIRE_FALSE( called );
}

TEST_CASE( "pipe futures do not block", "[pipe]" )
{
    be::task_pool pool;

    std::atomic_bool start{ false };
    std::atomic_bool called{ false };
    auto             first = [&] {
        while ( !start )
        {
            std::this_thread::sleep_for( 1us );
        }
        return 1;
    };
    auto second = [&]( int ) { called = true; }; // NOLINT

    auto pipe = pool | first | second;
    REQUIRE_FALSE( called );
    start = true;
    pipe.wait();
    pipe.get();
    REQUIRE( called );
}

TEST_CASE( "broken pipeline", "[pipe]" )
{
    be::task_pool pool;

    std::atomic_bool start{ false };
    std::atomic_bool called{ false };
    auto             first = [&]() -> int {
        while ( !start )
        {
            std::this_thread::sleep_for( 1us );
        }
        throw test_exception{};
    };
    auto second = [&]( int ) { called = true; }; // NOLINT

    auto pipe   = pool | first | second;
    start       = true;
    auto status = pipe.wait_for( 1s );
    REQUIRE( status == std::future_status::ready );
    REQUIRE_THROWS( pipe.get() );
    REQUIRE_FALSE( called );
}

TEST_CASE( "pipe with stop_token", "[pipe][stop_token]" )
{
    be::task_pool pool;

    std::atomic_bool called{ false };
    auto             first = [] {
        std::this_thread::sleep_for( 1us );
        return 1;
    };
    auto second = [&]( int, be::stop_token ) { called = true; }; // NOLINT
    {
        pool | first | second;
    }
    REQUIRE( called );
}

TEST_CASE( "pipe with allocator", "[pipe][allocator]" )
{
    be::task_pool pool;

    std::atomic_bool called{ false };
    auto             first = [] {
        std::this_thread::sleep_for( 1us );
        return 1;
    };
    auto second = [&]( std::allocator_arg_t /*x*/,
                       std::allocator< int > const& /*alloc*/,
                       int /*value*/ ) { called = true; }; // NOLINT
    {
        pool | first | second;
    }
    REQUIRE( called );
}

TEST_CASE( "pipe with allocator and stop_token", "[pipe][allocator][stop_token]" )
{
    be::task_pool pool;

    std::atomic_bool called{ false };
    auto             first = [] {
        std::this_thread::sleep_for( 1us );
        return 1;
    };

    auto second = [&]( std::allocator_arg_t /*x*/,
                       std::allocator< int > const& /*alloc*/,
                       int /*value*/,
                       be::stop_token /*token*/ ) { called = true; }; // NOLINT
    {
        pool | first | second;
    }
    REQUIRE( called );
}

TEST_CASE( "pipe detatchment using conversion operator", "[pipe]" )
{
    be::task_pool pool;

    std::atomic_bool start{ false };
    std::atomic_bool called{ false };
    auto             first = [] {
        std::this_thread::sleep_for( 1us );
        return 1;
    };

    auto second = [&]( int /*value*/ ) {
        while ( !start )
        {
            std::this_thread::sleep_for( 1us );
        }
        called = true;
    }; // NOLINT
    std::future< void > future;
    {
        auto pipeline = pool | first | second;
        future        = static_cast< typename decltype( pipeline )::future_type >( pipeline );
    }
    start = true;
    future.wait();
    REQUIRE( called );
}

TEST_CASE( "detach pipelines", "[pipe]" )
{
    be::task_pool pool;

    std::atomic_bool called{ false };
    auto             first = [] {
        std::this_thread::sleep_for( 1us );
        return 1;
    };

    auto second = [&]( std::allocator_arg_t /*x*/,
                       std::allocator< int > const& /*alloc*/,
                       int /*value*/,
                       be::stop_token /*token*/ ) { called = true; }; // NOLINT
    {
        pool | first | second | be::detach;
    }
    pool.wait();
    REQUIRE( called );
}

//
// Task pools are futures!
//
TEST_CASE( "pipe::get", "[future]" )
{
    be::task_pool pool;
    STATIC_REQUIRE( be::is_future< be::task_pool >::value );

    std::atomic_bool called{ false };
    auto             first = [] {
        std::this_thread::sleep_for( 1us );
        return 1;
    };
    auto second = [&]( int /*value*/ ) { called = true; }; // NOLINT
    auto pipe   = pool | first | second;
    REQUIRE( pool.get() );
    REQUIRE( called );
}

TEST_CASE( "pipe::wait_for, success", "[future]" )
{
    be::task_pool pool;
    STATIC_REQUIRE( be::is_future< be::task_pool >::value );

    std::atomic_bool called{ false };
    auto             first = [] {
        std::this_thread::sleep_for( 1us );
        return 1;
    };
    auto second = [&]( int /*value*/ ) { called = true; }; // NOLINT
    auto pipe   = pool | first | second;
    REQUIRE( pool.wait_for( 1s ) == std::future_status::ready );
    REQUIRE( called );
}

TEST_CASE( "pipe::wait_for, fail", "[future]" )
{
    be::task_pool pool;
    STATIC_REQUIRE( be::is_future< be::task_pool >::value );

    std::atomic_bool called{ false };
    std::atomic_bool finish{ false };
    auto             first = [&] {
        while ( !finish )
        {
            std::this_thread::sleep_for( 1us );
        }
        return 1;
    };
    auto second = [&]( int /*value*/ ) { called = true; }; // NOLINT
    auto pipe   = pool | first | second;
    REQUIRE( pool.wait_for( 1us ) == std::future_status::timeout );
    finish = true;
    pool.wait();
    REQUIRE( called );
}

TEST_CASE( "pipe::wait_until, success", "[future]" )
{
    be::task_pool pool;
    STATIC_REQUIRE( be::is_future< be::task_pool >::value );

    std::atomic_bool called{ false };
    auto             first = [] {
        std::this_thread::sleep_for( 1us );
        return 1;
    };
    auto second = [&]( int /*value*/ ) { called = true; }; // NOLINT
    auto pipe   = pool | first | second;
    REQUIRE( pool.wait_until( std::chrono::steady_clock::now() + 1s ) ==
             std::future_status::ready );
    REQUIRE( called );
}

TEST_CASE( "pipe::wait_until, fail", "[future]" )
{
    be::task_pool pool;
    STATIC_REQUIRE( be::is_future< be::task_pool >::value );

    std::atomic_bool called{ false };
    std::atomic_bool finish{ false };
    auto             first = [&] {
        while ( !finish )
        {
            std::this_thread::sleep_for( 1us );
        }
        return 1;
    };
    auto second = [&]( int /*value*/ ) { called = true; }; // NOLINT
    auto pipe   = pool | first | second;
    REQUIRE( pool.wait_until( std::chrono::steady_clock::now() + 1us ) ==
             std::future_status::timeout );
    finish = true;
    pool.wait();
    REQUIRE( called );
}

TEST_CASE( "pipe::wait_until, when paused", "[future]" )
{
    be::task_pool pool;
    STATIC_REQUIRE( be::is_future< be::task_pool >::value );

    std::atomic_bool started{ false };

    auto first = [&] {
        started = true;
        return 1;
    };
    auto second = [&]( int /*value*/ ) {}; // NOLINT
    auto pipe   = pool | first | second;
    pool.pause();
    REQUIRE( pool.wait_until( std::chrono::steady_clock::now() + 1us ) ==
             std::future_status::ready );
    pool.unpause();
    pipe.wait();
}

TEST_CASE( "Execute in main", "[std::launch::deferred]" )
{
    be::task_pool    pool;
    std::atomic_bool called{ false };
    auto             future = pool.submit(
        std::launch::deferred,
        []( std::atomic_bool& status ) { status = true; },
        std::ref( called ) );
    REQUIRE_FALSE( called.load() );
    pool.invoke_deferred();
    REQUIRE( called.load() );
}

TEST_CASE( "Execute in main with dependencies", "[std::launch::deferred]" )
{
    be::task_pool pool;

    std::atomic_bool waiting{ true };
    std::atomic_bool dependency_called{ false };
    auto             dependency = pool.submit(
        std::launch::async,
        []( std::atomic_bool& waiting_, std::atomic_bool& dependency_called_ ) {
            while ( waiting_.load() )
            {
                std::this_thread::sleep_for( 1ms );
            }
            dependency_called_ = true;
            return true;
        },
        std::ref( waiting ),
        std::ref( dependency_called ) );

    std::atomic_bool called{ false };
    auto             future = pool.submit(
        std::launch::deferred,
        []( std::atomic_bool& status, bool /*input*/ ) { status = true; },
        std::ref( called ),
        std::move( dependency ) );
    pool.invoke_deferred();
    REQUIRE_FALSE( called.load() );
    waiting                     = false;
    static auto const s_timeout = 10ms;
    pool.wait_for( s_timeout );
    pool.invoke_deferred();
    REQUIRE( called.load() );
}
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
#include <task_pool/task_pool.h>
#include <task_pool/traits.h>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

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
    auto             future       = from.submit( [&]() -> void {
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
        auto          f = from.submit( [&]() -> void {
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
    auto future = pool.submit( [&]() -> void {
        while ( !finish )
        {
            std::this_thread::sleep_for( 1ms );
        }
    } );
    REQUIRE( pool.get_tasks_queued() == 1 );
    pool.unpause();
    finish = true;
    pool.wait_for_tasks();
    REQUIRE( pool.get_tasks_queued() == 0 );
}

TEST_CASE( "get_tasks_running", "[task_pool]" )
{
    std::atomic_bool started{ false };
    std::atomic_bool finish{ false };
    be::task_pool    pool( 1 );
    REQUIRE( pool.get_tasks_running() == 0 );
    auto future = pool.submit( [&]() -> void {
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
    pool.wait_for_tasks();
    REQUIRE( pool.get_tasks_running() == 0 );
}

TEST_CASE( "get_tasks_total", "[task_pool]" )
{
    std::atomic_bool finish{ false };
    be::task_pool    pool( 1 );
    pool.pause();
    REQUIRE( pool.get_tasks_total() == 0 );
    pool.submit( [&]() -> void {
        while ( !finish )
        {
            std::this_thread::sleep_for( 1ms );
        }
    } );
    REQUIRE( pool.get_tasks_total() == 1 );
    pool.submit( [&]() -> void {
        while ( !finish )
        {
            std::this_thread::sleep_for( 1ms );
        }
    } );
    REQUIRE( pool.get_tasks_total() == 2 );
    pool.unpause();
    finish = true;
    pool.wait_for_tasks();
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

TEST_CASE( "wait_for_tasks", "[task_pool]" )
{
    be::task_pool pool( 1 );
    pool.pause();
    REQUIRE( pool.get_tasks_total() == 0 );
    pool.submit( [&]() -> void { std::this_thread::sleep_for( 1ms ); } );
    pool.submit( [&]() -> void { std::this_thread::sleep_for( 1ms ); } );
    pool.submit( [&]() -> void { std::this_thread::sleep_for( 1ms ); } );
    REQUIRE( pool.get_tasks_total() == 3 );
    pool.unpause();
    pool.wait_for_tasks();
    REQUIRE( pool.get_tasks_total() == 0 );
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
        pool.submit( &test_func_, &called );
        pool.wait_for_tasks();
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
        pool.submit( &test_::test_func_, &x ).wait();
        REQUIRE( x.called );
    }
}

TEST_CASE( "lambda pure by &", "[task_pool][submit]" )
{
    {
        std::atomic_bool called{ false };
        auto             fun = []( std::atomic_bool* x ) { ( *x ) = true; };
        be::task_pool    pool( 1 );
        pool.submit( fun, &called ).wait();
        REQUIRE( called );
    }
}

TEST_CASE( "lambda pure by &&", "[task_pool][submit]" )
{
    {
        std::atomic_bool called{ false };
        be::task_pool    pool( 1 );
        pool.submit( []( std::atomic_bool* x ) { ( *x ) = true; }, &called ).wait();
        REQUIRE( called );
    }
}

TEST_CASE( "lambda with capture by &", "[task_pool][submit]" )
{
    {
        std::atomic_bool called{ false };
        auto             x = [&]() { called = true; };
        be::task_pool    pool( 1 );
        pool.submit( x ).wait();
        REQUIRE( called );
    }
}

TEST_CASE( "lambda with capture by &&", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    be::task_pool    pool( 1 );
    pool.submit( [&]() { called = true; } ).wait();
    REQUIRE( called );
}

TEST_CASE( "std::function capture by &&", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    be::task_pool    pool( 1 );
    pool.submit( std::function< void() >( [&]() { called = true; } ) ).wait();
    REQUIRE( called );
}

TEST_CASE( "std::function capture by &", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    be::task_pool    pool( 1 );
    auto             x = std::function< void() >( [&]() { called = true; } );
    pool.submit( x ).wait();
    REQUIRE( called );
}

TEST_CASE( "std::function pure by &&", "[task_pool][submit]" )
{
    std::atomic_bool called{ false };
    be::task_pool    pool( 1 );
    pool.submit( std::function< void( std::atomic_bool* ) >( []( auto* x ) { ( *x ) = true; } ),
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
    pool.submit( fun, &called ).wait();
    REQUIRE( called );
}

TEST_CASE( "stateful lambda by &&", "[task_pool][submit]" )
{
    be::task_pool    pool( 1 );
    std::atomic_bool called{ false };
    pool.submit( [value = 2]( std::atomic_bool* x ) mutable { ( *x ) = --value == 1; }, &called )
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
        f = pool.submit( [value = 2]( std::atomic_bool* x ) mutable { ( *x ) = --value == 1; },
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
    void deallocate( T* p, std::size_t n )
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
        be::task_pool pool( 1 );
        pool.pause();
        std::future< int > f;
        {
            auto func = [value = 2]( std::atomic_bool* x ) mutable {
                ( *x ) = --value == 1;
                return value;
            };
            STATIC_REQUIRE_FALSE( be::wants_allocator< decltype( func ) >::value );
            f = pool.submit( std::allocator_arg_t{}, alloc, func, &called );
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

TEST_CASE( "submit without result allocator", "[task_pool][submit][allocator]" )
{
    std::atomic_bool           called{ false };
    counts                     amounts;
    counting_allocator< void > alloc{ amounts };
    {
        be::task_pool pool( 1 );
        pool.pause();
        std::future< void > f;
        {
            f = pool.submit(
                std::allocator_arg_t{},
                alloc,
                [value = 2]( std::atomic_bool* x ) mutable { ( *x ) = --value == 1; },
                &called );
        }
        pool.unpause();
        f.wait();
        REQUIRE( called );
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
        f = pool.submit( [&called]( be::stop_token stop ) mutable {
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

struct work
{
    template< typename WorkType >
    auto operator()( WorkType data ) const
    {
        return data.value;
    }
};

TEST_CASE( "Special callable types", "[task_pool][submit][stop_token]" )
{
    {
        be::task_pool pool{ 1 };
        const int     expected = 42;
        struct work_data
        {
            int value;
        };

        auto result = pool.submit( work{}, work_data{ expected } );
        REQUIRE( result.get() == expected );
    }

    {
        class special_work
        {
            void do_something() { called = true; }

        public:
            void run( be::stop_token abort )
            {
                while ( !abort )
                {
                    do_something();
                }
            }
            bool called = false;
        };

        special_work task;
        {
            be::task_pool pool{ 1 };
            pool.submit( &special_work::run, &task );
            pool.abort();
        }
    }
}

//
// Checking submit overloads - success branch
//
TEST_CASE( "void()& function", "[task_pool][submit]" )
{
    std::atomic_bool called;
    auto             fun = [&]() { called = true; };
    be::task_pool    pool( 1 );
    auto             future = pool.submit( fun );
    future.wait();
    REQUIRE( called == true );
}
TEST_CASE( "void()&& function", "[task_pool][submit]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit( [&]() { called = true; } );
    future.wait();
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)& function", "[task_pool][submit][stop_token]" )
{
    std::atomic_bool called;
    auto fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable { *check = true; };
    be::task_pool pool( 1 );
    auto          future = pool.submit( fun, &called );
    future.wait();
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)&& function", "[task_pool][submit]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit(
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable { *check = true; },
        &called );
    future.wait();
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
    auto          future = pool.submit( fun );
    auto          actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool()&& function", "[task_pool][submit]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit( [&]() {
        called = true;
        return true;
    } );
    auto             actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool(... be::stop_token)& function", "[task_pool][submit][stop_token]" )
{
    std::atomic_bool called;
    auto             fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
        *check = true;
        return true;
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( fun, &called );
    auto          actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool(... be::stop_token)&& function", "[task_pool][submit][stop_token]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit(
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
            *check = true;
            return true;
        },
        &called );
    auto actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

//
// Checking submit overloads - fail branch
//

struct test_exception : public std::exception
{
};

TEST_CASE( "void()& function throws", "[task_pool][submit][throws]" )
{
    std::atomic_bool called;
    auto             fun = [&]() {
        called = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( fun );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}
TEST_CASE( "void()&& function throws", "[task_pool][submit][throws]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit( [&]() {
        called = true;
        throw test_exception{};
    } );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)& function throws", "[task_pool][submit][stop_token][throws]" )
{
    std::atomic_bool called;
    auto             fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
        *check = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( fun, &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)&& function throws", "[task_pool][submit][stop_token][throws]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit(
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
            *check = true;
            throw test_exception{};
        },
        &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool()& function throws", "[task_pool][submit][throws]" )
{
    std::atomic_bool called;
    auto             fun = [&]() {
        called = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( fun );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool()&& function throws", "[task_pool][submit][throws]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit( [&]() {
        called = true;
        throw test_exception{};
    } );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool(... be::stop_token)& function throws", "[task_pool][submit][stop_token][throws]" )
{
    std::atomic_bool called;
    auto             fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) -> bool {
        *check = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( fun, &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool(... be::stop_token)&& function throws", "[task_pool][submit][stop_token][throws]" )
{
    std::atomic_bool called;
    be::task_pool    pool( 1 );
    auto             future = pool.submit(
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
            *check = true;
            throw test_exception{};
        },
        &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

//
// Checking submit overloads with allocator - success branch
//
TEST_CASE( "void()& function with allocator", "[task_pool][submit][allocator]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    auto fun = [&]( std::allocator_arg_t /*tag*/, std::allocator< int > const& /*alloc*/ ) {
        called = true;
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::allocator_arg_t{}, alloc, fun );
    future.wait();
    REQUIRE( called == true );
}
TEST_CASE( "void()&& function with allocator", "[task_pool][submit][allocator]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto future = pool.submit( std::allocator_arg_t{}, alloc, [&]() { called = true; } );
    future.wait();
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)& function with allocator",
           "[task_pool][submit][stop_token][allocator]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    auto                  fun = []( std::allocator_arg_t /*tag*/,
                   std::allocator< int > const& /*alloc*/,
                   std::atomic_bool* check,
                   be::stop_token /*abort*/ ) mutable { *check = true; };
    be::task_pool         pool( 1 );
    auto                  future = pool.submit( std::allocator_arg_t{}, alloc, fun, &called );
    future.wait();
    REQUIRE( called == true );
}
TEST_CASE( "void(... be::stop_token)& function with allocator throws .no2",
           "[task_pool][submit][stop_token][allocator][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    auto                  fun = []( std::allocator_arg_t /*tag*/,
                   std::allocator< int > const& /*alloc*/,
                   std::atomic_bool* check,
                   be::stop_token /*abort*/ ) mutable {
        *check = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::allocator_arg_t{}, alloc, fun, &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "void(alloc ... be::stop_token)&& function with allocator",
           "[task_pool][submit][stop_token][allocator]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto                  future = pool.submit(
        std::allocator_arg_t{},
        alloc,
        []( std::allocator_arg_t /*tag*/,
            std::allocator< int > const& /*alloc*/,
            std::atomic_bool* check,
            be::stop_token /*abort*/ ) mutable { *check = true; },
        &called );
    future.wait();
    REQUIRE( called == true );
}

TEST_CASE( "bool()& function with allocator", "[task_pool][submit][allocator]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    auto                  fun = [&]() {
        called = true;
        return true;
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::allocator_arg_t{}, alloc, fun );
    auto          actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool()&& function with allocator", "[task_pool][submit][allocator]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto                  future = pool.submit( std::allocator_arg_t{}, alloc, [&]() {
        called = true;
        return true;
    } );
    auto                  actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool()&& function with allocator throws .no2",
           "[task_pool][submit][allocator][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto                  future = pool.submit( std::allocator_arg_t{}, alloc, [&]() {
        called = true;
        throw test_exception{};
        return true;
    } );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool( alloc )&& function with allocator throws .no2",
           "[task_pool][submit][allocator][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto                  future =
        pool.submit( std::allocator_arg_t{},
                     alloc,
                     [&]( std::allocator_arg_t /*tag*/, std::allocator< int > const& /*alloc*/ ) {
                         called = true;
                         throw test_exception{};
                         return true;
                     } );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool(... be::stop_token)& function with allocator",
           "[task_pool][submit][stop_token][allocator]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    auto                  fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
        *check = true;
        return true;
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::allocator_arg_t{}, alloc, fun, &called );
    auto          actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

TEST_CASE( "bool(... be::stop_token)& function with allocator throws .no2",
           "[task_pool][submit][stop_token][allocator][throw]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    auto                  fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) -> bool {
        *check = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::allocator_arg_t{}, alloc, fun, &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool(... be::stop_token)&& function with allocator",
           "[task_pool][submit][stop_token][allocator]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto                  future = pool.submit(
        std::allocator_arg_t{},
        alloc,
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
            *check = true;
            return true;
        },
        &called );
    auto actual = future.get();
    REQUIRE( called == true );
    REQUIRE( actual == true );
}

//
// Checking submit overloads with allocator - fail branch
//

TEST_CASE( "void()& function with allocator throws", "[task_pool][submit][allocator][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    auto                  fun = [&]() {
        called = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::allocator_arg_t{}, alloc, fun );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}
TEST_CASE( "void()&& function with allocator throws", "[task_pool][submit][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto                  future = pool.submit( std::allocator_arg_t{}, alloc, [&]() {
        called = true;
        throw test_exception{};
    } );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)& function with allocator throws",
           "[task_pool][submit][stop_token][allocator][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    auto                  fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
        *check = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::allocator_arg_t{}, alloc, fun, &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)&& function with allocator",
           "[task_pool][submit][stop_token][allocator]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto                  future = pool.submit(
        std::allocator_arg_t{},
        alloc,
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable { *check = true; },
        &called );
    future.wait();
    REQUIRE( called == true );
}

TEST_CASE( "void(... be::stop_token)&& function with allocator throws",
           "[task_pool][submit][stop_token][allocator][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto                  future = pool.submit(
        std::allocator_arg_t{},
        alloc,
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
            *check = true;
            throw test_exception{};
        },
        &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool()& function with allocator throws", "[task_pool][submit][allocator][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    auto                  fun = [&]() {
        called = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::allocator_arg_t{}, alloc, fun );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool()&& function with allocator throws", "[task_pool][submit][allocator][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto                  future = pool.submit( std::allocator_arg_t{}, alloc, [&]() {
        called = true;
        throw test_exception{};
    } );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool(... be::stop_token)& function with allocator throws",
           "[task_pool][submit][stop_token][allocator][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    auto                  fun = []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
        *check = true;
        throw test_exception{};
    };
    be::task_pool pool( 1 );
    auto          future = pool.submit( std::allocator_arg_t{}, alloc, fun, &called );
    REQUIRE_THROWS_AS( future.get(), test_exception );
    REQUIRE( called == true );
}

TEST_CASE( "bool(... be::stop_token)&& function with allocator throws",
           "[task_pool][submit][stop_token][allocator][throws]" )
{
    std::allocator< int > alloc;
    std::atomic_bool      called;
    be::task_pool         pool( 1 );
    auto                  future = pool.submit(
        std::allocator_arg_t{},
        alloc,
        []( std::atomic_bool* check, be::stop_token /*abort*/ ) mutable {
            *check = true;
            throw test_exception{};
        },
        &called );
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

TEST_CASE( "submit( f, future )->void", "[task_pool][submit]" )
{
    const int           expected = 42;
    std::atomic_int     actual{ 0 };
    be::task_pool       pool( 1 );
    auto                fun_a  = []( int x ) { return x; };
    auto                fun_b  = [&]( int x ) { actual = x; };
    std::future< int >  future = pool.submit( fun_a, expected );
    std::future< void > result = pool.submit( fun_b, std::move( future ) );
    result.wait();
    REQUIRE( expected == actual );
}

TEST_CASE( "submit( f, future )->int", "[task_pool][submit]" )
{
    const int          expected = 42;
    be::task_pool      pool( 1 );
    auto               fun    = []( int x ) { return x; };
    std::future< int > future = pool.submit( fun, expected );
    std::future< int > result = pool.submit( fun, std::move( future ) );
    result.wait();
    REQUIRE( result.get() == expected );
}

TEST_CASE( "submit( f, future, ... ) -> void", "[task_pool][submit]" )
{
    const int           X = 42;
    const int           Y = 42;
    std::atomic_int     actual{ 0 };
    be::task_pool       pool( 1 );
    auto                fun_a    = []( int x ) { return x; };
    std::future< int >  future_a = pool.submit( fun_a, X );
    auto                fun_b    = [&]( int x, int y ) { actual = x * y; };
    std::future< void > result   = pool.submit( fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( actual == X * Y );
}

TEST_CASE( "submit( f, future, ... ) -> int", "[task_pool][submit]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x ) { return x; };
    std::future< int > future_a = pool.submit( fun_a, X );
    auto               fun_b    = []( int x, int y ) { return x * y; };
    std::future< int > result   = pool.submit( fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( result.get() == X * Y );
}

TEST_CASE( "submit( f(stop_token), future, ... ) -> int", "[task_pool][submit][stop_token]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x, be::stop_token /*abort*/ ) { return x; };
    std::future< int > future_a = pool.submit( fun_a, X );
    auto               fun_b    = []( int x, int y, be::stop_token /*abort*/ ) { return x * y; };
    std::future< int > result   = pool.submit( fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( result.get() == X * Y );
}

TEST_CASE( "submit( f(stop_token), future, ... ) -> void", "[task_pool][submit][stop_token]" )
{
    const int           X = 42;
    const int           Y = 42;
    std::atomic_int     actual{ 0 };
    be::task_pool       pool( 1 );
    auto                fun_a    = []( int x, be::stop_token /*abort*/ ) { return x; };
    std::future< int >  future_a = pool.submit( fun_a, X );
    auto                fun_b  = [&]( int x, int y, be::stop_token /*abort*/ ) { actual = x * y; };
    std::future< void > result = pool.submit( fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( actual == X * Y );
}

TEST_CASE( "submit( allocator, f(), future, ... ) -> int", "[task_pool][submit][allocator]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                  fun_b    = []( int x, int y ) { return x * y; };
    std::future< int >    result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( result.get() == X * Y );
}

TEST_CASE( "submit( allocator, f(allocator), future, ... ) -> int",
           "[task_pool][submit][allocator]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                  fun_b =
        []( std::allocator_arg_t /*tag*/, std::allocator< int > const& /*alloc*/, int x, int y ) {
            return x * y;
        };
    std::future< int > result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( result.get() == X * Y );
}

TEST_CASE( "submit( allocator, f(allocator), future, ... ) -> int throws",
           "[task_pool][submit][allocator][throws]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                  fun_b    = []( std::allocator_arg_t /*tag*/,
                     std::allocator< int > const& /*alloc*/,
                     int /*x*/,
                     int /*y*/ ) -> int { throw test_exception{}; };
    std::future< int >    result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( allocator, f(), future, ... ) -> void", "[task_pool][submit]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    std::atomic_int       actual{ 0 };
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                  fun_b    = [&]( int x, int y ) { actual = x * y; };
    std::future< void >   result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( actual == X * Y );
}

TEST_CASE( "submit( allocator, f(allocator), future, ... ) -> void", "[task_pool][submit]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    std::atomic_int       actual{ 0 };
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                  fun_b =
        [&]( std::allocator_arg_t /*tag*/, std::allocator< int > const& /*alloc*/, int x, int y ) {
            actual = x * y;
        };
    std::future< void > result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( actual == X * Y );
}

TEST_CASE( "submit( allocator, f(allocator), future, ... ) -> void throws",
           "[task_pool][submit][throws]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                  fun_b    = [&]( std::allocator_arg_t /*tag*/,
                      std::allocator< int > const& /*alloc*/,
                      int /*x*/,
                      int /*y*/ ) -> void { throw test_exception{}; };
    std::future< void >   result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( allocator, f(stop_token), future, ... ) -> int",
           "[task_pool][submit][stop_token][allocator]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                  fun_b = []( int x, int y, be::stop_token /*unused*/ ) { return x * y; };
    std::future< int >    result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( result.get() == X * Y );
}

TEST_CASE( "submit( allocator, f(stop_token), future, ... ) -> void",
           "[task_pool][submit][stop_token][allocator]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    std::atomic_int       actual{ 0 };
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                fun_b = [&]( int x, int y, be::stop_token /*unused*/ ) { actual = x * y; };
    std::future< void > result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    result.wait();
    REQUIRE( actual == X * Y );
}

//
// Throwing variants
//

////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE( "submit( f, future )->void throws", "[task_pool][submit][throws]" )
{
    const int           expected = 42;
    be::task_pool       pool( 1 );
    auto                fun_a  = []( int x ) -> int { return x; };
    auto                fun_b  = [&]( int ) { throw test_exception{}; }; // NOLINT
    std::future< int >  future = pool.submit( fun_a, expected );
    std::future< void > result = pool.submit( fun_b, std::move( future ) );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( f, future )->int throws", "[task_pool][submit][throws]" )
{
    const int          expected = 42;
    be::task_pool      pool( 1 );
    auto               fun    = []( int ) -> int { throw test_exception{}; }; // NOLINT
    std::future< int > result = pool.submit( fun, expected );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( f, future, ... ) -> void throws", "[task_pool][submit][throws]" )
{
    const int           X = 42;
    const int           Y = 42;
    be::task_pool       pool( 1 );
    auto                fun_a    = []( int x ) { return x; };
    std::future< int >  future_a = pool.submit( fun_a, X );
    auto                fun_b    = [&]( int, int ) { throw test_exception{}; }; // NOLINT
    std::future< void > result   = pool.submit( fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( f, future, ... ) -> int throws", "[task_pool][submit][throws]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x ) { return x; };
    std::future< int > future_a = pool.submit( fun_a, X );
    auto               fun_b    = []( int, int ) -> int { throw test_exception{}; }; // NOLINT
    std::future< int > result   = pool.submit( fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( f(stop_token), future, ... ) -> int throws",
           "[task_pool][submit][stop_token][throws]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x, be::stop_token /*abort*/ ) { return x; };
    std::future< int > future_a = pool.submit( fun_a, X );
    auto               fun_b    = []( int, int, be::stop_token /*abort*/ ) -> int { // NOLINT
        throw test_exception{};
    };
    std::future< int > result = pool.submit( fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( f(stop_token), future, ... ) -> void throws",
           "[task_pool][submit][stop_token][throws]" )
{
    const int          X = 42;
    const int          Y = 42;
    be::task_pool      pool( 1 );
    auto               fun_a    = []( int x, be::stop_token /*abort*/ ) { return x; };
    std::future< int > future_a = pool.submit( fun_a, X );
    auto fun_b = [&]( int, int, be::stop_token /*abort*/ ) { throw test_exception{}; }; // NOLINT
    std::future< void > result = pool.submit( fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( allocator, f(), future, ... ) -> int throws",
           "[task_pool][submit][allocator][throws]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                  fun_b    = []( int, int ) -> int { throw test_exception{}; }; // NOLINT
    std::future< int >    result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( allocator, f(), future, ... ) -> void throws", "[task_pool][submit][throws]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                  fun_b    = [&]( int, int ) -> void { throw test_exception{}; }; // NOLINT
    std::future< void >   result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( allocator, f(stop_token), future, ... ) -> int throws",
           "[task_pool][submit][stop_token][allocator][throws]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto                  fun_b    = []( int, int, be::stop_token /*unused*/ ) -> int { // NOLINT
        throw test_exception{};
    };
    std::future< int > result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

TEST_CASE( "submit( allocator, f(stop_token), future, ... ) -> void throws",
           "[task_pool][submit][stop_token][allocator][throws]" )
{
    std::allocator< int > alloc;
    const int             X = 42;
    const int             Y = 42;
    be::task_pool         pool( 1 );
    auto                  fun_a    = []( int x ) { return x; };
    std::future< int >    future_a = pool.submit( std::allocator_arg_t{}, alloc, fun_a, X );
    auto fun_b = [&]( int, int, be::stop_token /*unused*/ ) { throw test_exception{}; }; // NOLINT
    std::future< void > result =
        pool.submit( std::allocator_arg_t{}, alloc, fun_b, std::move( future_a ), Y );
    REQUIRE_THROWS_AS( result.get(), test_exception );
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

//
// Input value types
//
TEST_CASE( "reference_wrapper to & argument", "[task_pool][submit]" )
{
    be::task_pool pool( 1 );
    pool.pause();
    std::atomic_int actual{ 0 };
    auto            task     = [&]( int& x ) { actual = x; };
    const int       value    = 42;
    int             expected = value;
    auto            future   = pool.submit( task, std::ref( expected ) );
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

//
// Wants allocator
//

TEST_CASE( "( allocator, ... ) -> void", "[task_pool][submit][allocator]" )
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
    };

    std::size_t const value_counts = 1000;
    be::task_pool     pool;
    auto              result =
        pool.submit( std::allocator_arg_t{}, allocator, make_data, std::size_t{ value_counts } );
    pool.wait_for_tasks();
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

    std::size_t const value_counts = 1000;
    be::task_pool     pool;
    auto              result =
        pool.submit( std::allocator_arg_t{}, allocator, make_data, std::size_t{ value_counts } );
    pool.wait_for_tasks();
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
    auto make_data  = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t count ) { return data_type( count, 1, alloc ); };

    std::size_t const value_counts = 1000;
    be::task_pool     pool;
    auto              result =
        pool.submit( std::allocator_arg_t{}, allocator, make_data, std::size_t{ value_counts } );
    pool.wait_for_tasks();
    REQUIRE( result.get().size() == value_counts ); // doh
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

    std::size_t const value_counts = 1000;
    be::task_pool     pool;
    auto              data =
        pool.submit( std::allocator_arg_t{}, allocator, make_data, std::size_t{ value_counts } );
    auto result = pool.submit( process_data, std::move( data ) );
    pool.wait_for_tasks();
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

    std::size_t const value_counts = 1000;
    be::task_pool     pool;
    auto              data =
        pool.submit( std::allocator_arg_t{}, allocator, make_data, std::size_t{ value_counts } );
    auto result = pool.submit( process_data, std::move( data ) );
    pool.wait_for_tasks();
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

    std::size_t const value_counts = 1000;
    be::task_pool     pool;
    auto              data =
        pool.submit( std::allocator_arg_t{}, allocator, make_data, std::size_t{ value_counts } );
    auto result = pool.submit( process_data, std::move( data ) );
    pool.wait_for_tasks();
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

    std::size_t const value_counts = 1000;
    be::task_pool     pool;
    auto              data =
        pool.submit( std::allocator_arg_t{}, allocator, make_data, std::size_t{ value_counts } );
    auto result = pool.submit( process_data, std::move( data ) );
    pool.wait_for_tasks();
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

    using data_type                = std::vector< int, counting_allocator< int > >;
    std::size_t const value_counts = 1000;
    auto              make_data    = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t count ) { return data_type( count, 1, alloc ); };

    auto process_data = []( data_type&& x ) { return x.size(); };

    {
        be::task_pool pool;
        auto          get_count = []() { return value_counts; };
        auto          value     = pool.submit( get_count );
        auto data = pool.submit( std::allocator_arg_t{}, allocator, make_data, std::move( value ) );
        auto result = pool.submit( process_data, std::move( data ) );
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
        be::task_pool pool;
        auto          get_count = []() { return value_counts; };
        auto          value     = pool.submit( get_count );
        auto data = pool.submit( std::allocator_arg_t{}, allocator, make_data, std::move( value ) );
        auto result = pool.submit( process_data, std::move( data ) );
        REQUIRE_THROWS_AS( result.get(), test_exception );
    }
}

TEST_CASE( "( allocator, future ) -> size_t throws #2", "[task_pool][submit][allocator][throws]" )
{
    counts                    allocations;
    counting_allocator< int > allocator( allocations );

    std::size_t const value_counts = 1000;
    auto              make_data = []( std::size_t count ) { return std::vector< int >( count ); };

    auto process_data = []( std::allocator_arg_t /*tag*/,
                            counting_allocator< int > const& /*alloc*/,
                            std::vector< int > /*data*/ ) -> size_t { // NOLINT
        throw test_exception{};
    }; // NOLINT

    {
        be::task_pool pool;
        auto          get_count = []() { return value_counts; };
        auto          value     = pool.submit( get_count );
        auto          data      = pool.submit( make_data, std::move( value ) );
        auto          result =
            pool.submit( std::allocator_arg_t{}, allocator, process_data, std::move( data ) );
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
        be::task_pool pool;
        auto          get_count = []() { return value_counts; };
        auto          value     = pool.submit( get_count );
        auto          data      = pool.submit( make_data, std::move( value ) );
        auto          result =
            pool.submit( std::allocator_arg_t{}, allocator, process_data, std::move( data ) );
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
        be::task_pool pool;
        auto          get_count = []() { return value_counts; };
        auto          value     = pool.submit( get_count );
        auto          data      = pool.submit( make_data, std::move( value ) );
        auto          result =
            pool.submit( std::allocator_arg_t{}, allocator, process_data, std::move( data ) );
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
                            std::vector< int > /*data*/, // NOLINT
                            be::stop_token /*token*/ ) { throw test_exception{}; }; // NOLINT

    {
        be::task_pool pool;
        auto          get_count = []() { return value_counts; };
        auto          value     = pool.submit( get_count );
        auto          data      = pool.submit( make_data, std::move( value ) );
        auto          result =
            pool.submit( std::allocator_arg_t{}, allocator, process_data, std::move( data ) );
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
    auto              get_count    = []() { return value_counts; };
    auto              make_data    = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t                      count,
                         be::stop_token /*token*/ ) { return data_type( count, 1, alloc ); };

    auto process_data = []( data_type&& x ) { return x.size(); };

    be::task_pool pool;
    auto          value = pool.submit( get_count );
    auto data   = pool.submit( std::allocator_arg_t{}, allocator, make_data, std::move( value ) );
    auto result = pool.submit( process_data, std::move( data ) );
    pool.wait_for_tasks();
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
    auto              get_count    = []() { return value_counts; };
    auto              make_data    = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t                      count,
                         be::stop_token /*token*/ ) { return data_type( count, 1, alloc ); };

    auto process_data = []( std::allocator_arg_t /*tag*/,
                            counting_allocator< int > const& /*alloc*/,
                            data_type&& /*x*/,
                            be::stop_token /*token*/ ) -> std::size_t { throw test_exception{}; };

    be::task_pool pool;
    auto          value = pool.submit( get_count );
    auto data   = pool.submit( std::allocator_arg_t{}, allocator, make_data, std::move( value ) );
    auto result = pool.submit( std::allocator_arg_t{}, allocator, process_data, std::move( data ) );
    pool.wait_for_tasks();
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
    auto              get_count    = []() { return value_counts; };

    auto make_data = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t count ) { return data_type( count, 1, alloc ); };

    auto process_data = []( data_type&& x ) { return x.clear(); };

    be::task_pool pool;
    auto          value = pool.submit( get_count );
    auto data   = pool.submit( std::allocator_arg_t{}, allocator, make_data, std::move( value ) );
    auto result = pool.submit( process_data, std::move( data ) );
    pool.wait_for_tasks();
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
    auto              get_count    = []() { return value_counts; };

    auto make_data = []( std::allocator_arg_t /*tag*/,
                         counting_allocator< int > const& alloc,
                         std::size_t count ) { return data_type( count, 1, alloc ); };

    auto process_data = []( std::allocator_arg_t /*tag*/,
                            counting_allocator< int > const& /*alloc*/,
                            data_type&& x,
                            be::stop_token /*token*/ ) { return x.clear(); };

    be::task_pool pool;
    auto          value = pool.submit( get_count );
    auto data   = pool.submit( std::allocator_arg_t{}, allocator, make_data, std::move( value ) );
    auto result = pool.submit( std::allocator_arg_t{}, allocator, process_data, std::move( data ) );
    pool.wait_for_tasks();
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
    auto          value  = pool.submit( make_data, counts );
    auto          result = pool.submit( check_values, std::move( value ) );
    pool.wait_for_tasks();
}

/*

submit( ... )
    void (...)
    void (..., stop_token )
    void (std::allocator_arg_t, allocator const&, ...)
    void (std::allocator_arg_t, allocator const&, ..., stop_token)

    R (...)
    R (..., stop_token )
    R (std::allocator_arg_t, allocator const&, ...)
    R (std::allocator_arg_t, allocator const&, ..., stop_token)

submit<promise>( ... )
    void (...)
    void (..., stop_token )
    void (std::allocator_arg_t, allocator const&, ...)
    void (std::allocator_arg_t, allocator const&, ..., stop_token)

    R (...)
    R (..., stop_token )
    R (std::allocator_arg_t, allocator const&, ...)
    R (std::allocator_arg_t, allocator const&, ..., stop_token)

*/

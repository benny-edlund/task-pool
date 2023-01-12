#pragma once

#include "trait_fallbacks.h"
#include <algorithm>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

namespace be {

/**
 * @brief Allows tasks to participate in cooperative cancellation
 *
 * @details Users may submit tasks that take a be::stop_token as its last
 * argument and the task_pool will provide a token for the function when
 * executing that may be used to break out of long running work.
 *
 * @code{.cpp}
 * pool.submit( []( be::stop_token token ){ while( !token ) { //process things }
 * );
 * @endcode
 *
 * stop_tokens are a means to abort running operations and as such do not fire
 * when the task_pool is trying to complete work using
 * `task_pool::wait_for_tasks` however it does fire on destruction.
 *
 * @code{.cpp}
 * {
 *     task_pool pool;
 *     auto f = pool.submit( []( be::stop_token token ){ while( !token ) {
 * std::this_thread::sleep_for(1ms); } );
 *     // pool.wait_for_tasks(); // <-- this would deadlock
 * }                             // <-- destruction fires the stop token
 * allowing task to complete
 * @endcode
 *
 */
struct stop_token
{
    std::atomic_bool const& token;
    explicit                operator bool() { return token.load(); }
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Move this stuff somewhere private

template< typename T, std::enable_if_t< !be::is_future< T >::value, bool > = true >
auto wrap_arg( T&& t )
{
    struct func_
    {
        T           value;
        T           operator()() { return std::forward< T >( value ); }
        static bool is_ready() { return true; }
    };
    return func_{ std::forward< T >( t ) };
}

template< typename T, std::enable_if_t< be::is_future< T >::value, bool > = true >
auto wrap_arg( T t )
{
    struct func_
    {
        T                       value;
        be::future_value_t< T > operator()() { return value.get(); }
        bool                    is_ready() const
        {
            return value.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready;
        }
    };
    return func_{ std::forward< T >( t ) };
}

template< typename Callable, typename Arguments, std::size_t... Is >
auto call_it( Callable& callable, Arguments& arguments, std::index_sequence< Is... > /*Is*/ )
{
    return callable( std::get< Is >( arguments )()... );
}

template< typename Callable, typename Arguments, std::size_t... Is >
auto call_it( stop_token token,
              Callable&  callable,
              Arguments& arguments,
              std::index_sequence< Is... > /*Is*/ )
{
    return callable( std::get< Is >( arguments )()..., token );
}

template< typename Arguments, std::size_t... Is >
bool check_it( Arguments& arguments, std::index_sequence< Is... > /*Is*/ )
{
    std::array< bool, sizeof...( Is ) > args_status{ std::get< Is >( arguments ).is_ready()... };
    return std::all_of(
        args_status.begin(), args_status.end(), []( auto value ) { return value; } );
}

template< typename T, typename... Args >
auto make_defered_task( T&& t, Args&&... args )
{
    auto arguments = std::make_tuple( wrap_arg( std::forward< Args >( args ) )... );
    struct Task
    {
        T                     func_;
        decltype( arguments ) arguments_;
        bool                  is_ready() const { return check_it( arguments_ ); }
        auto                  operator()()
        {
            return call_it( func_, arguments_, std::index_sequence_for< Args... >{} );
        }
    };

    return Task{ std::forward< T >( t ), std::move( arguments ) };
}
//
//////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief
 * A simple and portable thread pool with allocator support and cooperative
 * cancellation
 *
 * @details
 * task_pool instances are fixed size thread pools that are intended to execute
 * descreet tasks
 *
 * @code{.cpp}
 * // create a pool with four threads
 * be::task_pool pool(4);
 *
 * // define some tasks to run
 * auto task1 = []( std::string x) { std::cerr<< "hello " << x << '\n' ); };
 * auto task2 = []( int x ) { return x; };
 *
 * // Add tasks to the pool
 * pool.submit( task1, std::string("world") );
 *
 * // All task submitted return futures however tasks that return values
 * // must not be disgarded
 * auto result = pool.submit( task2, 42 );
 *
 * // Wait for all submitted tasks to complete
 * pool.wait_for_tasks();
 *
 * // Access result from task2
 * std::cerr << "The anwser is " << result.get();
 * @endcode
 *
 * task_pools support a simple form of cooperative cancellation by allowing
 * submitted tasks that take a special type, the stop_token, to check if
 * the owning pool needs finish work early.
 *
 * Tasks may use the token to abort active work allowing the pool to shutdown
 *
 * @code{.cpp}
 * {
 *     be::task_pool pool;
 *     pool.submit( []( be::stop_token abort ) {
 *         auto then = std::chrono::now()+10m;
 *         while( !abort && std::chrono::now()<then ) {
 *             std::this_thread::sleep_for(1ms);
 *         }
 *     ]);
 *     // The pool destructor will ask running tasks to cancel and if
 *     // our task did not check this token it would need to wait the
 *     // full 10 minutes it takes for the task to finish its work
 * }
 * @endcode
 *
 * Task may be submitted from various callable types including function pointers
 * member functions and mutable lambdas with captures.
 *
 * @code{.cpp}
 * be::task_pool pool;
 *
 * struct work_data { int value; };
 *
 * // Free functions that expect values
 * void work_item( work_data data, be::stop_token abort ) {
 *     if ( !abort ) {
 *         std::cerr<<"The awnser is "<< data.value<<'\n';
 *     }
 * }
 * pool.submit(&work_item,work_data{42});
 *
 * // Objects with templated call operators and auto return types
 * struct work
 * {
 *     template< typename WorkType>
 *     auto operator()( WorkType data ) {
 *         return data.value;
 *     }
 * };
 * auto result = pool.submit( work{}, work_data{ 42 } );
 * @endcode
 *
 * // Member functions can also be bound if their owning instance is
 * // rechable at execution time
 *
 * class special_work
 * {
 *     void do_something();
 * public:
 *     void run(be::stop_token abort) { while( !abort ) do_something(); }
 * };
 *
 * special_work task;
 * pool.submit(&special_work::run,&task);
 * @endcode
 *
 * Tasks submitted to task_pools require intermediate storage on the heap and
 * this can become a limiting factor to applications. To help task_pool supports
 * using custom allocators for storing tasks until executed as well as the
 * shared state of the std::futures used.
 *
 * @code{.cpp}
 * be::task_pool pool;
 *
 * // std::allocator_arg_t is used to disambiguate the overloads to
 * task_pool::submit pool.submit( std::allocator_arg_t{}, allocator, &fun );
 *
 * @endcode
 */
class task_pool
{
    /**
     * @brief Task storage with type erasure
     */
    struct task_proxy
    {
        bool ( *check_task )( void* );
        void ( *execute_task )( void* );
        std::unique_ptr< void, void ( * )( void* ) > storage;

        task_proxy() = delete;

        /**
         * @brief Constructs a proxy around a task.
         */
        template< typename Task >
        explicit task_proxy( Task* task )
            : check_task( []( void* x ) { return ( *static_cast< Task* >( x ) ).is_ready(); } )
            , execute_task( []( void* x ) { ( *static_cast< Task* >( x ) )(); } )
            , storage( task, []( void* x ) { delete static_cast< Task* >( x ); } ) // NOLINT
        {
        }
        /**
         * @brief Constructs a proxy around a task with allocator support
         */
        template< typename Task, template< typename > class Allocator >
        explicit task_proxy( std::allocator_arg_t /*x*/,
                             Allocator< Task > const& /* user_allocator*/,
                             Task* task )
            : check_task( []( void* x ) { return ( *static_cast< Task* >( x ) ).is_ready(); } )
            , execute_task( []( void* x ) { ( *static_cast< Task* >( x ) )(); } )
            , storage( task, []( void* x ) {
                Task*             tsk = static_cast< Task* >( x );
                Allocator< Task > alloc( tsk->alloc );
                std::allocator_traits< Allocator< Task > >::destroy( alloc, tsk );
                std::allocator_traits< Allocator< Task > >::deallocate( alloc, tsk, 1 );
            } )
        {
        }
        ~task_proxy()                              = default;
        task_proxy( task_proxy const& )            = delete;
        task_proxy& operator=( task_proxy const& ) = delete;
        task_proxy( task_proxy&& ) noexcept;
        task_proxy& operator=( task_proxy&& ) noexcept;
    };

    /**
     * @brief Creates a task from some callable as a new type
     */
    template< typename F, typename FuncType = std::remove_reference_t< std::remove_cv_t< F > > >
    static auto make_task( F&& task )
    {
        struct Task : FuncType
        {
            explicit Task( FuncType&& f )
                : FuncType( std::forward< F >( f ) )
            {
            }
            using FuncType::operator();
            static bool     is_ready() { return true; }
        };
        return task_proxy( new Task( std::forward< F >( task ) ) );
    }

    /**
     * @brief Creates a task from some callable as a new type with allocator
     * support
     */
    template< template< typename > class Allocator,
              typename F,
              typename FuncType = std::remove_reference_t< std::remove_cv_t< F > >,
              typename T >
    static auto make_task( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task )
    {
        struct Task : FuncType
        {
            explicit Task( Allocator< Task > const& a, F&& f )
                : FuncType( std::forward< F >( f ) )
                , alloc( a )
            {
            }
            using FuncType::  operator();
            static bool       is_ready() { return true; }
            Allocator< Task > alloc;
        };
        Allocator< Task > task_allocator( allocator );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct(
            task_allocator, typed_task, task_allocator, std::forward< F >( task ) );
        return task_proxy( x, task_allocator, typed_task );
    }

    template< typename F,
              typename... Args,
              typename R = be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >... >,
              std::enable_if_t< be_is_void_v< R >, bool > = true >
    auto make_defered_task( F&& task, Args&&... args )
    {
        using args_type = std::tuple< decltype( wrap_arg( std::forward< Args >( args ) ) )... >;
        struct Task
        {
            std::promise< R > promise_;
            F                 func_;
            args_type         arguments_;
            bool              is_ready() const
            {
                return check_it( arguments_, std::index_sequence_for< Args... >{} );
            }
            auto operator()()
            {
                try
                {
                    call_it( func_, arguments_, std::index_sequence_for< Args... >{} );
                    promise_.set_value();
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
        };
        std::promise< R > promise;
        auto              future = promise.get_future();
        push_task( task_proxy(
            new Task{ std::move( promise ),
                      std::forward< F >( task ),
                      std::make_tuple( wrap_arg( std::forward< Args >( args ) )... ) } ) );
        return future;
    }

    template< typename F,
              typename... Args,
              typename R =
                  be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >..., stop_token >,
              std::enable_if_t< be_is_void_v< R >, bool > = true >
    auto make_defered_task( stop_token token, F&& task, Args&&... args )
    {
        using args_type = std::tuple< decltype( wrap_arg( std::forward< Args >( args ) ) )... >;
        struct Task
        {
            stop_token        token_;
            std::promise< R > promise_;
            F                 func_;
            args_type         arguments_;
            bool              is_ready() const
            {
                return check_it( arguments_, std::index_sequence_for< Args... >{} );
            }
            auto operator()()
            {
                try
                {
                    call_it( token_, func_, arguments_, std::index_sequence_for< Args... >{} );
                    promise_.set_value();
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
        };
        std::promise< R > promise;
        auto              future = promise.get_future();
        push_task( task_proxy(
            new Task{ token,
                      std::move( promise ),
                      std::forward< F >( task ),
                      std::make_tuple( wrap_arg( std::forward< Args >( args ) )... ) } ) );
        return future;
    }

    template< typename F,
              typename... Args,
              typename R = be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >... >,
              std::enable_if_t< !be_is_void_v< R >, bool > = true >
    auto make_defered_task( F&& task, Args&&... args )
    {
        using args_type = std::tuple< decltype( wrap_arg( std::forward< Args >( args ) ) )... >;
        struct Task
        {
            std::promise< R > promise_;
            F                 func_;
            args_type         arguments_;
            bool              is_ready() const
            {
                return check_it( arguments_, std::index_sequence_for< Args... >{} );
            }
            auto operator()()
            {
                try
                {
                    promise_.set_value(
                        call_it( func_, arguments_, std::index_sequence_for< Args... >{} ) );
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
        };
        std::promise< R > promise;
        auto              future = promise.get_future();
        push_task( task_proxy(
            new Task{ std::move( promise ),
                      std::forward< F >( task ),
                      std::make_tuple( wrap_arg( std::forward< Args >( args ) )... ) } ) );
        return future;
    }

    template< typename F,
              typename... Args,
              typename R =
                  be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >..., stop_token >,
              std::enable_if_t< !be_is_void_v< R >, bool > = true >
    auto make_defered_task( stop_token token, F&& task, Args&&... args )
    {
        using args_type = std::tuple< decltype( wrap_arg( std::forward< Args >( args ) ) )... >;
        struct Task
        {
            stop_token        token_;
            std::promise< R > promise_;
            F                 func_;
            args_type         arguments_;
            bool              is_ready() const
            {
                return check_it( arguments_, std::index_sequence_for< Args... >{} );
            }
            auto operator()()
            {
                try
                {
                    promise_.set_value( call_it(
                        token_, func_, arguments_, std::index_sequence_for< Args... >{} ) );
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
        };
        std::promise< R > promise;
        auto              future = promise.get_future();
        push_task( task_proxy(
            new Task{ token,
                      std::move( promise ),
                      std::forward< F >( task ),
                      std::make_tuple( wrap_arg( std::forward< Args >( args ) )... ) } ) );
        return future;
    }

    template< template< typename > class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R = be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >... >,
              std::enable_if_t< be_is_void_v< R >, bool > = true >

    auto make_defered_task( std::allocator_arg_t  x,
                            Allocator< T > const& allocator,
                            F&&                   task,
                            Args&&... args )
    {
        using args_type = std::tuple< decltype( wrap_arg( std::forward< Args >( args ) ) )... >;
        struct Task
        {
            Allocator< Task > alloc;
            std::promise< R > promise_;
            F&&               func_;
            args_type         arguments_;
            Task( Allocator< Task > a, std::promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }

            bool is_ready() const
            {
                return check_it( arguments_, std::index_sequence_for< Args... >{} );
            }
            auto operator()()
            {
                try
                {
                    call_it( func_, arguments_, std::index_sequence_for< Args... >{} );
                    promise_.set_value();
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
        };
        std::promise< R > promise( x, allocator );
        auto              future = promise.get_future();
        Allocator< Task > task_allocator( allocator );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct(
            task_allocator,
            typed_task,
            task_allocator,
            std::move( promise ),
            std::forward< F >( task ),
            std::make_tuple( wrap_arg( std::forward< Args >( args ) )... ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

    template< template< typename > class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R = be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >... >,
              std::enable_if_t< !be_is_void_v< R >, bool > = true >

    auto make_defered_task( std::allocator_arg_t  x,
                            Allocator< T > const& allocator,
                            F&&                   task,
                            Args&&... args )
    {
        using args_type = std::tuple< decltype( wrap_arg( std::forward< Args >( args ) ) )... >;
        struct Task
        {
            Allocator< Task > alloc;
            std::promise< R > promise_;
            F&&               func_;
            args_type         arguments_;
            Task( Allocator< Task > a, std::promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }
            bool is_ready() const
            {
                return check_it( arguments_, std::index_sequence_for< Args... >{} );
            }
            auto operator()()
            {
                try
                {
                    promise_.set_value(
                        call_it( func_, arguments_, std::index_sequence_for< Args... >{} ) );
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
        };
        std::promise< R > promise( x, allocator );
        auto              future = promise.get_future();
        Allocator< Task > task_allocator( allocator );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct(
            task_allocator,
            typed_task,
            task_allocator,
            std::move( promise ),
            std::forward< F >( task ),
            std::make_tuple( wrap_arg( std::forward< Args >( args ) )... ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

    template< template< typename > class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R =
                  be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >..., stop_token >,
              std::enable_if_t< be_is_void_v< R >, bool > = true >

    auto make_defered_task( std::allocator_arg_t  x,
                            Allocator< T > const& allocator,
                            stop_token            token,
                            F&&                   task,
                            Args&&... args )
    {
        using args_type = std::tuple< decltype( wrap_arg( std::forward< Args >( args ) ) )... >;
        struct Task
        {
            Allocator< Task > alloc;
            stop_token        token_;
            std::promise< R > promise_;
            F                 func_;
            args_type         arguments_;
            Task( Allocator< Task > a, stop_token t, std::promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , token_( t )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }
            bool is_ready() const
            {
                return check_it( arguments_, std::index_sequence_for< Args... >{} );
            }
            auto operator()()
            {
                try
                {
                    call_it( token_, func_, arguments_, std::index_sequence_for< Args... >{} );
                    promise_.set_value();
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
        };
        std::promise< R > promise( x, allocator );
        auto              future = promise.get_future();
        Allocator< Task > task_allocator( allocator );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct(
            task_allocator,
            typed_task,
            task_allocator,
            token,
            std::move( promise ),
            std::forward< F >( task ),
            std::make_tuple( wrap_arg( std::forward< Args >( args ) )... ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

    template< template< typename > class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R =
                  be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >..., stop_token >,
              std::enable_if_t< !be_is_void_v< R >, bool > = true >

    auto make_defered_task( std::allocator_arg_t  x,
                            Allocator< T > const& allocator,
                            stop_token            token,
                            F&&                   task,
                            Args&&... args )
    {
        using args_type = std::tuple< decltype( wrap_arg( std::forward< Args >( args ) ) )... >;
        struct Task
        {
            Allocator< Task > alloc;
            stop_token        token_;
            std::promise< R > promise_;
            F                 func_;
            args_type         arguments_;
            Task( Allocator< Task > a, stop_token t, std::promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , token_( t )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }
            bool is_ready() const
            {
                return check_it( arguments_, std::index_sequence_for< Args... >{} );
            }
            auto operator()()
            {
                try
                {
                    promise_.set_value( call_it(
                        token_, func_, arguments_, std::index_sequence_for< Args... >{} ) );
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
        };
        std::promise< R > promise( x, allocator );
        auto              future = promise.get_future();
        Allocator< Task > task_allocator( allocator );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct(
            task_allocator,
            typed_task,
            task_allocator,
            token,
            std::move( promise ),
            std::forward< F >( task ),
            std::make_tuple( wrap_arg( std::forward< Args >( args ) )... ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

public:
    /**
     * @brief Constructs a task_pool with given amount of threads
     */
    explicit task_pool( const unsigned = 0 );

    /**
     * @brief Destroys the task_pool. Will attempt to cancel tasks that allow it
     * and join all threads.
     */
    ~task_pool();

    /**
     * @brief task_pool objects may not be copied
     */
    task_pool( task_pool const& ) = delete;

    /**
     * @brief task_pool objects may not be copied
     */
    task_pool& operator=( task_pool const& ) = delete;

    /**
     * @brief task_pool objects may be move constructed (even while running)
     */
    task_pool( task_pool&& ) noexcept;
    /**
     * @brief task_pool objects may be move assigned (even while running)
     */
    task_pool& operator=( task_pool&& ) noexcept;

    /**
     * @brief Resets the task_pool to the given amount of threads (completes all
     * tasks)
     */
    void reset( const unsigned = 0 );

    /**
     * @brief Sets the stop token and waits for all tasks to complete. Pool must
     * be reset before it can process tasks again
     */
    void abort();

    /**
     * @brief Returns the amount of tasks in the pool not currently running
     */
    BE_NODISGARD std::size_t get_tasks_queued() const;

    /**
     * @brief Returns the amount of tasks in the bool currently running
     */
    BE_NODISGARD std::size_t get_tasks_running() const;

    /**
     * @brief Returns the total amount of tasks in the pool
     */
    BE_NODISGARD std::size_t get_tasks_total() const;

    /**
     * @brief Returns the amount of threads in the pool
     */
    BE_NODISGARD unsigned get_thread_count() const;

    /**
     * @brief Returns if the pool has been paused
     */
    BE_NODISGARD bool is_paused() const;

    /**
     * @brief Pauses the pool. No tasked will be queued while the pool is paused
     */
    void pause();

    /**
     * @brief Resumes the enqueueing of tasks in the pool
     */
    void unpause();

    /**
     * @brief Blocks calling thread until all tasks have completed. No tasked may
     * be submitted while pool is waiting
     */
    void wait_for_tasks();

    /**
     * @brief Returns a stop token for the pool.
     */
    stop_token get_stop_token() const;

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< typename F,
              typename... A,
              typename R = be_invoke_result_t< std::decay_t< F >, std::decay_t< A >... >,
              std::enable_if_t< be_is_void_v< R >, bool > = true >
    std::future< R > submit( F&& task, A&&... args )
    {
        auto promise     = std::promise< R >();
        auto task_future = promise.get_future();
        push_task( make_task(
            [task_function = std::bind( std::forward< F >( task ), std::forward< A >( args )... ),
             task_promise  = std::move( promise )]() mutable {
                try
                {
                    task_function();
                    task_promise.set_value();
                }
                catch ( ... )
                {
                    task_promise.set_exception( std::current_exception() );
                }
            } ) );
        return task_future;
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template<
        typename F,
        typename... A,
        typename R = be_invoke_result_t< std::decay_t< F >, std::decay_t< A >..., stop_token >,
        std::enable_if_t< be_is_void_v< R > && wants_stop_token_v< F >, bool > = true >
    std::future< R > submit( F&& task, A&&... args )
    {
        auto promise     = std::promise< R >();
        auto task_future = promise.get_future();
        push_task( make_task( [task_function = std::bind( std::forward< F >( task ),
                                                          std::forward< A >( args )...,
                                                          get_stop_token() ),
                               task_promise  = std::move( promise )]() mutable {
            try
            {
                task_function();
                task_promise.set_value();
            }
            catch ( ... )
            {
                task_promise.set_exception( std::current_exception() );
            }
        } ) );
        return task_future;
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< typename F,
              typename... Args,
              typename R = be_invoke_result_t< std::decay_t< F >, std::decay_t< Args >... >,
              std::enable_if_t< !be_is_void_v< R >, bool > = true >
    BE_NODISGARD std::future< R > submit( F&& task, Args&&... args )
    {
        auto promise = std::promise< R >();
        auto future  = promise.get_future();
        push_task( make_task( [task_function = std::bind( std::forward< F >( task ),
                                                          std::forward< Args >( args )... ),
                               task_promise  = std::move( promise )]() mutable {
            try
            {
                task_promise.set_value( task_function() );
            }
            catch ( ... )
            {
                task_promise.set_exception( std::current_exception() );
            }
        } ) );
        return future;
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template<
        typename F,
        typename... Args,
        typename R = be_invoke_result_t< std::decay_t< F >, std::decay_t< Args >..., stop_token >,
        std::enable_if_t< !be_is_void_v< R > && wants_stop_token_v< F >, bool > = true >
    BE_NODISGARD std::future< R > submit( F&& task, Args&&... args )
    {
        auto promise = std::promise< R >();
        auto future  = promise.get_future();
        push_task( make_task( [task_function = std::bind( std::forward< F >( task ),
                                                          std::forward< Args >( args )...,
                                                          get_stop_token() ),
                               task_promise  = std::move( promise )]() mutable {
            try
            {
                task_promise.set_value( task_function() );
            }
            catch ( ... )
            {
                task_promise.set_exception( std::current_exception() );
            }
        } ) );
        return future;
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class UserAllocator,
              typename F,
              typename... A,
              typename R = be_invoke_result_t< std::decay_t< F >, std::decay_t< A >... >,
              typename T,
              std::enable_if_t< be_is_void_v< R >, bool > = true >
    BE_NODISGARD std::future< R >
    submit( std::allocator_arg_t x, UserAllocator< T > const& allocator, F&& task, A&&... args )
    {
        auto promise     = std::promise< R >( x, allocator );
        auto task_future = promise.get_future();
        push_task( make_task(
            x,
            allocator,
            [task_function = std::bind( std::forward< F >( task ), std::forward< A >( args )... ),
             task_promise  = std::move( promise )]() mutable {
                try
                {
                    task_function();
                    task_promise.set_value();
                }
                catch ( ... )
                {
                    task_promise.set_exception( std::current_exception() );
                }
            } ) );
        return task_future;
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class UserAllocator,
              typename F,
              typename... A,
              typename R =
                  be_invoke_result_t< std::decay_t< F >, std::decay_t< A >..., stop_token >,
              typename T,
              std::enable_if_t< be_is_void_v< R > && wants_stop_token_v< F >, bool > = true >
    BE_NODISGARD std::future< R >
    submit( std::allocator_arg_t x, UserAllocator< T > const& allocator, F&& task, A&&... args )
    {
        auto promise     = std::promise< R >( x, allocator );
        auto task_future = promise.get_future();
        push_task( make_task( x,
                              allocator,
                              [task_function = std::bind( std::forward< F >( task ),
                                                          std::forward< A >( args )...,
                                                          get_stop_token() ),
                               task_promise  = std::move( promise )]() mutable {
                                  try
                                  {
                                      task_function();
                                      task_promise.set_value();
                                  }
                                  catch ( ... )
                                  {
                                      task_promise.set_exception( std::current_exception() );
                                  }
                              } ) );
        return task_future;
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class UserAllocator,
              typename F,
              typename... Args,
              typename R        = be_invoke_result_t< std::decay_t< F >, std::decay_t< Args >... >,
              typename FuncType = std::remove_reference_t< std::remove_cv_t< F > >,
              typename T,
              std::enable_if_t< !be_is_void_v< R >, bool > = true >
    BE_NODISGARD std::future< R >
    submit( std::allocator_arg_t x, UserAllocator< T > const& allocator, F&& task, Args&&... args )
    {
        std::promise< R > promise( x, allocator );
        auto              future = promise.get_future();
        push_task( make_task( x,
                              allocator,
                              [task_function = std::bind( std::forward< F >( task ),
                                                          std::forward< Args >( args )... ),
                               task_promise  = std::move( promise )]() mutable {
                                  try
                                  {
                                      task_promise.set_value( task_function() );
                                  }
                                  catch ( ... )
                                  {
                                      task_promise.set_exception( std::current_exception() );
                                  }
                              } ) );
        return future;
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class UserAllocator,
              typename F,
              typename... Args,
              typename R =
                  be_invoke_result_t< std::decay_t< F >, std::decay_t< Args >..., stop_token >,
              typename FuncType = std::remove_reference_t< std::remove_cv_t< F > >,
              typename T,
              std::enable_if_t< !be_is_void_v< R > && wants_stop_token_v< F >, bool > = true >
    BE_NODISGARD std::future< R >
    submit( std::allocator_arg_t x, UserAllocator< T > const& allocator, F&& task, Args&&... args )
    {
        std::promise< R > promise( x, allocator );
        auto              future = promise.get_future();
        push_task( make_task( x,
                              allocator,
                              [task_function = std::bind( std::forward< F >( task ),
                                                          std::forward< Args >( args )...,
                                                          get_stop_token() ),
                               task_promise  = std::move( promise )]() mutable {
                                  try
                                  {
                                      task_promise.set_value( task_function() );
                                  }
                                  catch ( ... )
                                  {
                                      task_promise.set_exception( std::current_exception() );
                                  }
                              } ) );
        return future;
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< typename F,
              typename... A,
              typename R = be_invoke_result_t< std::decay_t< F >,
                                               future_argument_t< std::decay_t< A > >... >,
              std::enable_if_t< contains_future< std::decay_t< A >... >::value, bool > = true >
    std::future< R > submit( F&& task, A&&... args )
    {
        return make_defered_task( std::forward< F >( task ), std::forward< A >( args )... );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template<
        typename F,
        typename... A,
        typename R               = be_invoke_result_t< std::decay_t< F >,
                                         future_argument_t< std::decay_t< A > >...,
                                         stop_token >,
        std::enable_if_t< contains_future< std::decay_t< A >... >::value && wants_stop_token_v< F >,
                          bool > = true >
    std::future< R > submit( F&& task, A&&... args )
    {
        return make_defered_task(
            get_stop_token(), std::forward< F >( task ), std::forward< A >( args )... );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class UserAllocator,
              typename F,
              typename... A,
              typename R = be_invoke_result_t< std::decay_t< F >,
                                               future_argument_t< std::decay_t< A > >... >,
              typename T,
              std::enable_if_t< contains_future< std::decay_t< A >... >::value, bool > = true >
    BE_NODISGARD std::future< R >
    submit( std::allocator_arg_t x, UserAllocator< T > const& allocator, F&& task, A&&... args )
    {
        return make_defered_task(
            x, allocator, std::forward< F >( task ), std::forward< A >( args )... );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class UserAllocator,
              typename F,
              typename... A,
              typename R = be_invoke_result_t< std::decay_t< F >,
                                               future_argument_t< std::decay_t< A > >...,
                                               stop_token >,
              typename T,
              std::enable_if_t< contains_future< std::decay_t< A >... >::value &&
                                    wants_stop_token_v< F >,
                                bool > = true >
    BE_NODISGARD std::future< R >
    submit( std::allocator_arg_t x, UserAllocator< T > const& allocator, F&& task, A&&... args )
    {
        return make_defered_task( x,
                                  allocator,
                                  get_stop_token(),
                                  std::forward< F >( task ),
                                  std::forward< A >( args )... );
    }

private:
    void push_task( task_proxy&& );

    struct Impl;
    std::unique_ptr< Impl > impl_;
};

} // namespace be
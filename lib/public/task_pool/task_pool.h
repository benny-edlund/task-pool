#pragma once

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <task_pool/api.h>
#include <task_pool/fallbacks.h>
#include <task_pool/traits.h>
#include <type_traits>
#include <utility>
#include <vector>

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
struct TASKPOOL_API stop_token
{
    std::atomic_bool const& token;

    explicit operator bool();
};

/**
 * @brief
 * Args simple and portable thread pool with allocator support and cooperative
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

template< template< typename > class Allocator >
class TASKPOOL_API task_pool_t
{
public:
    task_pool_t()
        : task_pool_t( std::thread::hardware_concurrency() )
    {
    }
    explicit task_pool_t( const unsigned thread_count )
        : task_pool_t( std::chrono::microseconds( 1 ), thread_count )
    {
    }
    template< typename Duration >
    task_pool_t( Duration&& lazy_check_latency, const unsigned thread_count )
        : task_pool_t( std::chrono::duration_cast< std::chrono::nanoseconds >( lazy_check_latency ),
                       thread_count )
    {
    }

    template< typename Value >
    explicit task_pool_t( Allocator< Value > const& alloc )
        : task_pool_t( std::thread::hardware_concurrency(), alloc )
    {
    }
    /**
     * @brief Constructs a task_pool with given amount of threads
     */
    template< typename Value >
    explicit task_pool_t( const unsigned thread_count, Allocator< Value > const& alloc )
        : task_pool_t( std::chrono::microseconds( 1 ), thread_count, alloc )
    {
    }

    // template< typename Duration >
    // task_pool_t( Duration&& lazy_check_latency, const unsigned thread_count, Allocator<void>
    // const& alloc )
    //     : task_pool_t( std::chrono::duration_cast< std::chrono::nanoseconds >( lazy_check_latency
    //     ),
    //                    thread_count )
    // {
    // }
    /**
     * @brief Destroys the task_pool. Will attempt to cancel tasks that allow it
     * and join all threads.
     */
    ~task_pool_t() { runtime_.reset(); }

    /**
     * @brief task_pool objects may not be copied
     */
    task_pool_t( task_pool_t const& ) = delete;

    /**
     * @brief task_pool objects may not be copied
     */
    task_pool_t& operator=( task_pool_t const& ) = delete;

    /**
     * @brief task_pool objects may be move constructed (even while running)
     */
    task_pool_t( task_pool_t&& other ) noexcept
        : runtime_( std::move( other.runtime_ ) )
    {
    }
    /**
     * @brief task_pool objects may be move assigned (even while running)
     */
    task_pool_t& operator=( task_pool_t&& other ) noexcept
    {
        reset();
        std::swap( runtime_, other.runtime_ );
        return *this;
    }

    /**
     * @brief Resets the task_pool to the given amount of threads (completes all
     * tasks)
     */
    void reset( const unsigned requested_thread_count = 0 )
    {
        const bool was_paused = is_paused();
        pause();
        wait();
        auto latency = ( *runtime_ ).task_check_latency_;
        runtime_.reset( new pool_runtime( latency, requested_thread_count ) );
        if ( !was_paused )
        {
            unpause();
        }
    }

    /**
     * @brief
     *
     *
     * be reset before it can process tasks again
     */
    void abort() { runtime_.reset(); }

    /**
     * @brief Returns the amount of tasks in the pool not currently running
     */
    BE_NODISGARD std::size_t get_tasks_queued() const { return ( *runtime_ ).tasks_queued_; }

    /**
     * @brief Returns the amount of tasks in the bool currently running
     */
    BE_NODISGARD std::size_t get_tasks_running() const { return ( *runtime_ ).tasks_running_; }
    /**
     * @brief Returns the amount of tasks in the bool currently awaiting input arguments
     */
    BE_NODISGARD std::size_t get_tasks_waiting() const { return ( *runtime_ ).tasks_waiting_; }

    /**
     * @brief Returns the total amount of tasks in the pool
     */
    BE_NODISGARD std::size_t get_tasks_total() const
    {
        return ( *runtime_ ).tasks_queued_ + ( *runtime_ ).tasks_running_ +
               ( *runtime_ ).tasks_waiting_;
    }

    /**
     * @brief Returns the amount of threads in the pool
     */
    BE_NODISGARD unsigned get_thread_count() const { return ( *runtime_ ).thread_count_; }

    /**
     * @brief Returns if the pool has been paused
     */
    BE_NODISGARD bool is_paused() const { return ( *runtime_ ).paused_; }

    /**
     * @brief Pauses the pool. No tasked will be queued while the pool is paused
     */
    void pause() { ( *runtime_ ).paused_ = true; }

    /**
     * @brief Resumes the enqueueing of tasks in the pool
     */
    void unpause() { ( *runtime_ ).paused_ = false; }

    /**
     * @brief Blocks calling thread until all tasks have completed. No tasked may be submitted while
     * pool is waiting. If called while paused the function is a passthru
     */
    void wait() { ( *runtime_ ).wait(); }

    /**
     * @brief Returns a stop token for the pool.
     */
    stop_token get_stop_token() const { return stop_token{ ( *runtime_ ).abort_ }; };

    /**
     * @brief Get the maximum duration used to wait prior to checking lazy input arguments
     *
     * @return std::chrono::nanoseconds
     */
    std::chrono::nanoseconds get_check_latency() const { return ( *runtime_ ).task_check_latency_; }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     *
     * @param task A callable value type
     * void(...)
     * @param args Parameter pack of input arguments to task
     * @return Future<Return>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename FuncType = std::remove_cv_t< std::remove_reference_t< Func > >,
              typename Return   = be_invoke_result_t< std::decay_t< FuncType >, Args... >,
              typename Future   = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    be_is_void_v< Return > && !wants_allocator_v< Func >,
                                bool > = true >
    Future submit( Func&& task, Args&&... args )
    {
        Allocator< Return > allocator( allocator_ );
        Promise< Return >   promise( std::allocator_arg_t{}, allocator );
        auto                task_future = promise.get_future();
        ( *runtime_ )
            .push_task( make_task( [task_function = std::bind( std::forward< Func >( task ),
                                                               std::forward< Args >( args )... ),
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
     *
     * @param task A callable value type taking an allocator
     * void( std::allocator_arg_t, Allocator<T> const&, ... )
     * @return Future<Return>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename FuncType = std::remove_cv_t< std::remove_reference_t< Func > >,
              typename Return   = be_invoke_result_t<
                  std::decay_t< Func >,
                  std::allocator_arg_t,
                  Allocator< typename wants_allocator< FuncType >::value_type > const&,
                  Args... >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    be_is_void_v< Return > && wants_allocator_v< Func >,
                                bool > = true >
    Future submit( Func&& task, Args&&... args )
    {
        Allocator< Return > allocator( allocator_ );
        auto                promise     = Promise< Return >( std::allocator_arg_t{}, allocator );
        auto                task_future = promise.get_future();
        ( *runtime_ )
            .push_task( make_task(
                [task_function = std::bind(
                     std::forward< Func >( task ),
                     std::allocator_arg_t{},
                     Allocator< typename wants_allocator< FuncType >::value_type >( allocator_ ),
                     std::forward< Args >( args )... ),
                 task_promise = std::move( promise )]() mutable {
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
     *
     * @param task A callable value type that takes an allocator and a be::stop_token
     * void(std::allocator_arg_t, Allocator<T> const&, ..., be::stop_token )
     * @param args A parameter pack of input arguments to task
     * @return Future<void>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename Return =
                  be_invoke_result_t< std::decay_t< Func >,
                                      std::allocator_arg_t,
                                      Allocator< typename wants_allocator< Func >::value_type >,
                                      Args...,
                                      stop_token >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    be_is_void_v< Return > && wants_stop_token_v< Func > &&
                                    wants_allocator_v< Func >,
                                bool > = true >
    Future submit( Func&& task, Args&&... args )
    {
        Allocator< Return > allocator( allocator_ );
        auto                promise     = Promise< Return >( std::allocator_arg_t{}, allocator );
        auto                task_future = promise.get_future();
        ( *runtime_ )
            .push_task( make_task(
                [task_function = std::bind(
                     std::forward< Func >( task ),
                     std::allocator_arg_t{},
                     Allocator< typename wants_allocator< Func >::value_type >( allocator_ ),
                     std::forward< Args >( args )...,
                     get_stop_token() ),
                 task_promise = std::move( promise )]() mutable {
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
     *
     * @param task A callable value type that takes a be::stop_token
     * void( ..., be::stop_token )
     * @param args A parameter pack of input arguments to task
     * @return Future<void>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename Return = be_invoke_result_t< std::decay_t< Func >, Args..., stop_token >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    be_is_void_v< Return > && wants_stop_token_v< Func > &&
                                    !wants_allocator_v< Func >,
                                bool > = true >
    Future submit( Func&& task, Args&&... args )
    {
        Allocator< Return > allocator( allocator_ );
        auto                promise     = Promise< Return >( std::allocator_arg_t{}, allocator );
        auto                task_future = promise.get_future();
        ( *runtime_ )
            .push_task( make_task( [task_function = std::bind( std::forward< Func >( task ),
                                                               std::forward< Args >( args )...,
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
     *
     * @param task A callable value type
     * Return(Args...)
     * @param args A parameter pack of input arguments to task
     * @return Future<Return>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename Return   = be_invoke_result_t< std::decay_t< Func >, Args... >,
              typename FuncType = std::remove_reference_t< std::remove_cv_t< Func > >,
              typename Future   = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    !be_is_void_v< Return > && !wants_allocator_v< Func > &&
                                    !wants_stop_token_v< Func >,
                                bool > = true >
    BE_NODISGARD Future submit( Func&& task, Args&&... args )
    {
        Allocator< Return > allocator( allocator_ );
        Promise< Return >   promise( std::allocator_arg_t{}, allocator );
        auto                future = promise.get_future();
        ( *runtime_ )
            .push_task( make_task( [task_function = std::bind( std::forward< Func >( task ),
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
     *
     * @param task A callable value type that wants an allocator
     * Return (std::allocator_tag_t, Allocator<T>, ... )
     * @param args A parameter pack with input arguments for task
     * @return Future<Return>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename FuncType = std::remove_reference_t< std::remove_cv_t< Func > >,
              typename Return   = be_invoke_result_t<
                  std::decay_t< Func >,
                  std::allocator_arg_t,
                  Allocator< typename wants_allocator< FuncType >::value_type > const&,
                  Args... >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    !be_is_void_v< Return > && wants_allocator_v< Func >,
                                bool > = true >
    BE_NODISGARD Future submit( Func&& task, Args&&... args )
    {
        Allocator< Return > allocator( allocator_ );
        Promise< Return >   promise( std::allocator_arg_t{}, allocator );
        auto                future = promise.get_future();
        ( *runtime_ )
            .push_task( make_task(
                [task_function = std::bind(
                     std::forward< Func >( task ),
                     std::allocator_arg_t{},
                     Allocator< typename wants_allocator< Func >::value_type >( allocator_ ),
                     std::forward< Args >( args )... ),
                 task_promise = std::move( promise )]() mutable {
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
     *
     * @param task A callable value type taking a be::stop_token
     * Return(..., be::stop_token )
     * @param args A parameter pack of input arguments to token
     * @return Future<Return>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename Return   = be_invoke_result_t< std::decay_t< Func >, Args..., stop_token >,
              typename FuncType = std::remove_reference_t< std::remove_cv_t< Func > >,
              typename Future   = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    !be_is_void_v< Return > && wants_stop_token_v< Func > &&
                                    !wants_allocator_v< Func >,
                                bool > = true >
    BE_NODISGARD Future submit( Func&& task, Args&&... args )
    {
        Allocator< Return > allocator( allocator_ );
        Promise< Return >   promise( std::allocator_arg_t{}, allocator );
        auto                future = promise.get_future();
        ( *runtime_ )
            .push_task( make_task( [task_function = std::bind( std::forward< Func >( task ),
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
     *
     * @param task A callable value type taking an allocator and be::stop_token
     * Return(std::allocator_arg_t, Allocator<T> const&, ..., be::stop_token ) ]
     * @param args A parameter pack of input arguments to token
     * @return Future<Return>
     */

    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename FuncType = std::remove_reference_t< std::remove_cv_t< Func > >,
              typename Return =
                  be_invoke_result_t< std::decay_t< FuncType >,
                                      std::allocator_arg_t,
                                      Allocator< typename wants_allocator< FuncType >::value_type >,
                                      Args...,
                                      stop_token >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    !be_is_void_v< Return > && wants_stop_token_v< Func > &&
                                    wants_allocator_v< Func >,
                                bool > = true >
    BE_NODISGARD Future submit( Func&& task, Args&&... args )
    {
        Allocator< Return > allocator( allocator_ );
        Promise< Return >   promise( std::allocator_arg_t{}, allocator );
        auto                future = promise.get_future();
        ( *runtime_ )
            .push_task( make_task(
                [task_function = std::bind(
                     std::forward< Func >( task ),
                     std::allocator_arg_t{},
                     Allocator< typename wants_allocator< Func >::value_type >( allocator_ ),
                     std::forward< Args >( args )...,
                     get_stop_token() ),
                 task_promise = std::move( promise )]() mutable {
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
     *
     * @param task A callable task type,
     * Return( lazy...)
     * @param args A parameter pack containing one or more futures of values to task
     * @return Future<Return>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename Return = be_invoke_result_t< std::decay_t< Func >,
                                                    future_argument_t< std::decay_t< Args > >... >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    contains_future< std::decay_t< Args >... >::value,
                                bool > = true >
    BE_NODISGARD Future submit( Func&& task, Args&&... args )
    {
        Allocator< Return > alloc( allocator_ );
        auto                args_tuple =
            std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )... );
        return make_defered_task( Promise< Return >{ std::allocator_arg_t{}, alloc },
                                  std::forward< Func >( task ),
                                  std::move( args_tuple ) );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     *
     * @param task A callable task type wanting a allocator
     * Return(std::allocator_arg_t, Allocator<Value>, lazy... )
     * @param args A par
     * @return Future<Return>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename Return =
                  be_invoke_result_t< std::decay_t< Func >,
                                      std::allocator_arg_t,
                                      Allocator< typename wants_allocator< Func >::value_type >,
                                      future_argument_t< std::decay_t< Args > >... >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && wants_allocator_v< Func > &&
                                    !wants_stop_token_v< Func > &&
                                    contains_future< std::decay_t< Args >... >::value,
                                bool > = true >
    BE_NODISGARD Future submit( Func&& task, Args&&... args )
    {
        // note that if task is a member function it can not be forwared the allocator
        // since we use arg0 for the allocator...member functions should likely tie
        // custom allocators to the instance they are member of
        Allocator< Return > alloc( allocator_ );
        auto                args_tuple = std::make_tuple(
            wrap_future_argument( std::allocator_arg_t{} ),
            wrap_future_argument(
                Allocator< typename wants_allocator< Func >::value_type >( allocator_ ) ),
            wrap_future_argument( std::forward< Args >( args ) )... );
        return make_defered_task( Promise< Return >{ std::allocator_arg_t{}, alloc },
                                  std::forward< Func >( task ),
                                  std::move( args_tuple ) );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     *
     * @param task A callable task type that takes an allocator and a be::stop_token
     * Return( std::allocator_arg_t, Allocator<Value>, lazy..., be::stop_token )
     * @param args A parameter pack cont,aining one or more futures of values to task
     * @return Future<Return>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename Return =
                  be_invoke_result_t< std::decay_t< Func >,
                                      std::allocator_arg_t,
                                      Allocator< typename wants_allocator< Func >::value_type >,
                                      future_argument_t< std::decay_t< Args > >...,
                                      stop_token >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && wants_allocator_v< Func > &&
                                    wants_stop_token_v< Func > &&
                                    contains_future< std::decay_t< Args >... >::value,
                                bool > = true >
    BE_NODISGARD Future submit( Func&& task, Args&&... args )
    {
        // note that if task is a member function it can not be forwared the allocator
        // since we use arg0 for the allocator...member functions should likely tie
        // custom allocators to the instance they are member of
        Allocator< Return > alloc( allocator_ );
        auto                args_tuple = std::make_tuple(
            wrap_future_argument( std::allocator_arg_t{} ),
            wrap_future_argument(
                Allocator< typename wants_allocator< Func >::value_type >( allocator_ ) ),
            wrap_future_argument( std::forward< Args >( args ) )...,
            wrap_future_argument( get_stop_token() ) );
        return make_defered_task( Promise< Return >{ std::allocator_arg_t{}, alloc },
                                  std::forward< Func >( task ),
                                  std::move( args_tuple ) );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     *
     * @param task A callable task type that takes a be::stop_token,
     * Return( lazy..., be::stop_token )
     * @param args A parameter pack cont,aining one or more futures of values to task
     * @return Future<Return>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename Return = be_invoke_result_t< std::decay_t< Func >,
                                                    future_argument_t< std::decay_t< Args > >...,
                                                    stop_token >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && !wants_allocator_v< Func > &&
                                    wants_stop_token_v< Func > &&
                                    contains_future< std::decay_t< Args >... >::value,
                                bool > = true >
    BE_NODISGARD Future submit( Func&& task, Args&&... args )
    {
        Allocator< Return > alloc( allocator_ );
        auto args_tuple = std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )...,
                                           wrap_future_argument( get_stop_token() ) );
        return make_defered_task( Promise< Return >{ std::allocator_arg_t{}, alloc },
                                  std::forward< Func >( task ),
                                  std::move( args_tuple ) );
    }

private:
    /**
     * @brief Task storage with type erasure
     */
    struct TASKPOOL_HIDDEN task_proxy
    {
        bool ( *check_task )( void* );
        void ( *execute_task )( void* );
        std::unique_ptr< void, void ( * )( void* ) > storage;

        task_proxy() = delete;

        /**
         * @brief Constructs a proxy around a task with allocator support
         */
        template< typename Task >
        explicit task_proxy( Task* task )
            : check_task( []( void* x ) { return ( *static_cast< Task* >( x ) ).is_ready(); } )
            , execute_task( []( void* x ) { ( *static_cast< Task* >( x ) )(); } )
            , storage( task, []( void* x ) {
                Task* tsk = static_cast< Task* >( x );
                std::allocator_traits< Allocator< Task > >::destroy( tsk->alloc, tsk );
                std::allocator_traits< Allocator< Task > >::deallocate( tsk->alloc, tsk, 1 );
            } )
        {
        }
        ~task_proxy()                              = default;
        task_proxy( task_proxy const& )            = delete;
        task_proxy& operator=( task_proxy const& ) = delete;
        task_proxy( task_proxy&& other ) noexcept
            : check_task( other.check_task )
            , execute_task( other.execute_task )
            , storage( std::move( other.storage ) )
        {
            other.execute_task = nullptr;
            other.check_task   = nullptr;
        }
        task_proxy& operator=( task_proxy&& other ) noexcept
        {
            check_task         = other.check_task;
            execute_task       = other.execute_task;
            storage            = std::move( other.storage );
            other.execute_task = nullptr;
            other.check_task   = nullptr;
            return *this;
        }
    };

    /**
     * @brief Creates a task from some callable as a new type with allocator
     * support
     */
    template< typename Func,
              typename FuncType = std::remove_reference_t< std::remove_cv_t< Func > > >
    auto make_task( Func&& task )
    {
        struct TASKPOOL_HIDDEN Task : FuncType
        {
            Task( Allocator< Task > const& a, Func&& f )
                : FuncType( std::forward< Func >( f ) )
                , alloc( a )
            {
            }
            using FuncType::  operator();
            static bool       is_ready() { return true; }
            Allocator< Task > alloc;
            ~Task()                        = default;
            Task( Task const& )            = delete;
            Task& operator=( Task const& ) = delete;
            Task( Task&& ) noexcept        = delete;
            Task& operator=( Task&& )      = delete;
        };
        Allocator< Task > task_allocator( allocator_ );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct(
            task_allocator, typed_task, task_allocator, std::forward< Func >( task ) );
        return task_proxy( typed_task );
    }

    template< class Promise,
              typename Func,
              typename ArgsTuple,
              typename Future = promise_api::get_future_t< Promise >,
              typename Return = future_api::get_result_t< Future >,
              std::enable_if_t<
                  std::is_class< std::remove_reference_t< std::remove_cv_t< Func > > >::value,
                  bool > = true >

    Future make_defered_task( Promise promise, Func&& task, ArgsTuple args_tuple )
    {
        using FuncType = std::remove_reference_t< std::remove_cv_t< Func > >;
        struct TASKPOOL_HIDDEN Task : FuncType
        {
            Allocator< Task > alloc;
            Promise           promise_;
            ArgsTuple         arguments_;
            Task( Allocator< Task > const& a, Promise&& p, Func&& f, ArgsTuple&& arg )
                : FuncType( std::forward< Func >( f ) )
                , alloc( a )
                , promise_( std::move( p ) )
                , arguments_( std::move( arg ) )
            {
            }

            bool is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< ArgsTuple >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    invoke_deferred_task(
                        promise_,
                        std::mem_fn( &FuncType::operator() ),
                        this,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< ArgsTuple >{} >{} );
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
            ~Task()                        = default;
            Task( Task const& )            = delete;
            Task& operator=( Task const& ) = delete;
            Task( Task&& ) noexcept        = delete;
            Task& operator=( Task&& )      = delete;
        };
        auto              future = promise.get_future();
        Allocator< Task > task_allocator( allocator_ );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct( task_allocator,
                                                               typed_task,
                                                               task_allocator,
                                                               std::move( promise ),
                                                               std::forward< Func >( task ),
                                                               std::move( args_tuple ) );
        ( *runtime_ ).push_task( task_proxy( typed_task ) );
        return future;
    }

    template<
        class Promise,
        typename Func,
        typename ArgsTuple,
        typename Future = promise_api::get_future_t< Promise >,
        typename Return = future_api::get_result_t< Future >,
        std::enable_if_t< std::is_function< std::remove_pointer_t< Func > >::value, bool > = true >

    Future make_defered_task( Promise promise, Func&& task, ArgsTuple args_tuple )
    {
        using FuncType = std::remove_reference_t< std::remove_cv_t< Func > >;
        struct TASKPOOL_HIDDEN Task
        {
            Allocator< Task > alloc;
            FuncType          func_;
            Promise           promise_;
            ArgsTuple         arguments_;
            Task( Allocator< Task > const& a, Promise&& p, Func&& f, ArgsTuple&& arg )
                : FuncType( std::forward< Func >( f ) )
                , alloc( a )
                , promise_( std::move( p ) )
                , arguments_( std::move( arg ) )
            {
            }

            bool is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< ArgsTuple >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    invoke_deferred_task(
                        promise_,
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< ArgsTuple >{} >{} );
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
            ~Task()                        = default;
            Task( Task const& )            = delete;
            Task& operator=( Task const& ) = delete;
            Task( Task&& ) noexcept        = delete;
            Task& operator=( Task&& )      = delete;
        };
        auto              future = promise.get_future();
        Allocator< Task > task_allocator( allocator_ );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct( task_allocator,
                                                               typed_task,
                                                               task_allocator,
                                                               std::move( promise ),
                                                               std::forward< Func >( task ),
                                                               std::move( args_tuple ) );
        ( *runtime_ ).push_task( task_proxy( typed_task ) );
        return future;
    }

    task_pool_t( std::chrono::nanoseconds const check_task_latency, unsigned const requested_count )
        : runtime_( std::make_unique< pool_runtime >( check_task_latency, requested_count ) )
        , allocator_()
    {
    }

    template< typename ValueAllocator >
    task_pool_t( std::chrono::nanoseconds const check_task_latency,
                 unsigned const                 requested_count,
                 ValueAllocator const&          alloc )
        : runtime_( std::make_unique< pool_runtime >( check_task_latency, requested_count ) )
        , allocator_( alloc )
    {
    }

    struct pool_runtime
    {
        std::condition_variable          task_added_     = {};
        std::condition_variable          task_completed_ = {};
        mutable std::mutex               tasks_mutex_    = {};
        std::atomic< std::size_t >       tasks_queued_{ 0 };
        std::atomic< std::size_t >       tasks_waiting_{ 0 };
        std::atomic< std::size_t >       tasks_running_{ 0 };
        std::queue< task_proxy >         tasks_             = {};
        mutable std::mutex               check_tasks_mutex_ = {};
        std::vector< task_proxy >        tasks_to_check_    = {};
        std::atomic< bool >              waiting_{ false };
        std::atomic< bool >              paused_{ false };
        std::atomic< bool >              abort_{ false };
        unsigned                         thread_count_ = 0;
        std::unique_ptr< std::thread[] > threads_; //  NOLINT (c-arrays)
        std::chrono::nanoseconds         task_check_latency_ = std::chrono::microseconds( 1 );

        pool_runtime( std::chrono::nanoseconds latency, unsigned int requested_count )
            : thread_count_( compute_thread_count( requested_count ) )
            , threads_( std::make_unique< std::thread[] >( thread_count_ ) ) // NOLINT
                                                                             // (c-arrays)
            , task_check_latency_( latency )
        {
            create_threads();
        }
        ~pool_runtime() { destroy_threads(); }
        pool_runtime( pool_runtime const& )            = delete;
        pool_runtime& operator=( pool_runtime const& ) = delete;
        pool_runtime( pool_runtime&& )                 = delete;
        pool_runtime& operator=( pool_runtime&& )      = delete;

        void create_threads()
        {
            abort_   = false;
            threads_ = std::make_unique< std::thread[] >( thread_count_ ); // NOLINT (c-arrays)
            for ( unsigned i = 0; i < thread_count_; ++i )
            {
                threads_[i] = std::thread(
                    &task_pool_t::pool_runtime::thread_worker, this, task_check_latency_ );
            }
        }

        void destroy_threads()
        {
            {
                std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
                abort_ = true;
                task_added_.notify_all();
            }
            for ( unsigned i = 0; i < thread_count_; ++i )
            {
                if ( threads_[i].joinable() )
                {
                    threads_[i].join();
                }
            }
            threads_.reset();
            thread_count_ = 0;
        }
        static unsigned compute_thread_count( const unsigned thread_count )
        {
            // we need at least two threads to process work and check futures
            if ( thread_count > 0 )
            {
                return thread_count;
            }
            else
            {
                if ( std::thread::hardware_concurrency() > 0 )
                {
                    return std::thread::hardware_concurrency();
                }
                else
                {
                    return 1;
                }
            }
        }

        void wait()
        {
            waiting_ = true;
            std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
            task_added_.notify_all();
            task_completed_.wait( tasks_lock, [this] {
                if ( paused_ )
                {
                    return true;
                }
                return ( tasks_queued_ + tasks_running_ + tasks_waiting_ == 0 );
            } );
            waiting_ = false;
        }

        void push_task( task_proxy proxy )
        {
            if ( proxy.storage.get() == nullptr ) // NOLINT
            {
                throw std::invalid_argument{ "'add_task' called with invalid task_proxy" };
            }
            if ( proxy.check_task( proxy.storage.get() ) )
            {
                {
                    std::unique_lock< std::mutex > lock( tasks_mutex_ );
                    tasks_.push( std::move( proxy ) );
                }
                ++tasks_queued_;
            }
            else
            {
                std::unique_lock< std::mutex > lock( check_tasks_mutex_ );
                tasks_to_check_.push_back( std::move( proxy ) );
            }
            task_added_.notify_one();
        }

        // must run with waiting_task_mutex held
        std::vector< task_proxy > task_checker()
        {
            if ( abort_ )
            {
                return {};
            }
            std::vector< task_proxy > ready_tasks;
            {
                auto const start   = std::begin( tasks_to_check_ );
                auto const stop    = std::end( tasks_to_check_ );
                auto const removed = std::partition( start, stop, []( task_proxy const& proxy ) {
                    return !proxy.check_task( proxy.storage.get() );
                } );
                ready_tasks.insert( ready_tasks.end(),
                                    std::make_move_iterator( removed ),
                                    std::make_move_iterator( stop ) );
                tasks_to_check_.erase( removed, tasks_to_check_.end() );
            }
            return ready_tasks;
        }

        /**
         * @brief thread_worker thread task
         *
         * @details All threads run the thread_worker function to process tasks in the pool.
         * Additionally one thread may checking argument statuses since tasks in the pool are
         * allowed to take futures as arguments and potentially resubmitting work to the pool
         * the ready.
         *
         * At the head of the worker function we check if there are waiting tasks that need
         * checking and if so exactly one thread gets the job to update the statuses and
         * resubmit tasks that are ready for processing.
         *
         * The idea would be that there is always some thread waiting on the tasks_mutex_ so
         * there is probably no rush to get there so before we try to take it and start waiting
         * ourselves we spend some time checking the input args for tasks that uses futures.
         * Once we have checked the futures we wake up any waiting thread to be the next
         * task_checker .
         */

        void thread_worker( std::chrono::nanoseconds latency )
        {
            for ( ;; )
            {
                {
                    std::vector< task_proxy > ready_tasks;
                    // thread_workers first tries to become the next task_checker
                    std::unique_lock< std::mutex > lock( check_tasks_mutex_, std::try_to_lock );
                    if ( lock.owns_lock() && !tasks_to_check_.empty() )
                    {
                        ready_tasks = task_checker();
                    }
                    for ( task_proxy& proxy_ready : ready_tasks )
                    {
                        push_task( std::move( proxy_ready ) );
                    }
                }
                std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
                if ( abort_ )
                {
                    break;
                };
                using namespace std::chrono_literals;
                task_added_.wait_for(
                    tasks_lock, latency, [this] { return !tasks_.empty() || abort_; } );
                if ( tasks_.empty() )
                {
                    // we where woken to be the next task_checker
                    if ( waiting_ )
                    {
                        task_completed_.notify_one();
                    }
                    continue;
                }
                if ( paused_ )
                {
                    continue;
                }
                if ( abort_ )
                {
                    return;
                }
                task_proxy proxy( std::move( tasks_.front() ) );
                tasks_.pop();
                --tasks_queued_;
                ++tasks_running_;
                tasks_lock.unlock();
                proxy.execute_task( proxy.storage.get() );
                --tasks_running_;
                if ( waiting_ )
                {
                    task_completed_.notify_one();
                }
            }
        }
    };
    std::unique_ptr< pool_runtime > runtime_{};
    Allocator< void >               allocator_{};
};

using task_pool = task_pool_t< std::allocator >;
extern template class task_pool_t< std::allocator >;
} // namespace be

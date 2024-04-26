#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <queue>
#include <task_pool/api.h>
#include <task_pool/fallbacks.h>
#include <task_pool/traits.h>
#include <thread>
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
 * A simple and portable thread pool supporting pipe syntax, lazy parameters and cooperative
 * cancellation all with custom allocators.
 *
 * @details
 * task_pool instances are fixed size thread pools that are intended to execute
 * descreet tasks either independently or linked by futures.
 *
 */

// template< template< typename, typename... > class Allocator, typename Value, typename... Ts >
// class TASKPOOL_API task_pool_t< Allocator< Value, Ts... > >

template< typename Allocator >
class TASKPOOL_API task_pool_t
{
public:
    /**
     * @brief Construct a new task pool object
     *
     * @details The amount of threads will be set to `std::thread::hardware_concurrency` and lazy
     * argument checker latency will be 1us. Additionally default constructed pools must use default
     * constructable allocator types.
     *
     */
    explicit task_pool_t()
        : task_pool_t( std::thread::hardware_concurrency() )
    {
    }

    /**
     * @brief Construct a new task pool object
     *
     * @param thread_count - the desired amount of threads for the pool
     *
     * @details Construct a pool with a specific amount of threads. If the given amount of threads
     * is zero it will be translated as `std::thread::hardware_concurrency`. The lazy argument
     * checker latency will be 1us and the allocator used must be default construcable.
     */
    explicit task_pool_t( const unsigned thread_count )
        : task_pool_t( std::chrono::microseconds( 1 ), thread_count )
    {
    }

    /**
     * @brief Construct a new task pool object
     *
     * @tparam Duration - a std::chrono duration type
     * @param lazy_check_latency - latency duration of the lazy argument checker
     * @param thread_count       - the desired amount of threads for the pool
     *
     * @details Construct a pool with a specifc amount of threads and a specific duration for the
     * lazy argument checker. If the given amount of threads is zero it will be translated as
     * `std::thread::hardware_concurrency`. The specified allocator of the pool must be default
     * constructable.
     */
    template< typename Duration, std::enable_if_t< is_duration< Duration >::value, bool > = true >
    task_pool_t( Duration&& lazy_check_latency, const unsigned thread_count )
        : task_pool_t( std::chrono::duration_cast< std::chrono::nanoseconds >( lazy_check_latency ),
                       thread_count )
    {
    }

    /**
     * @brief Construct a new task pool object
     *
     * @tparam Value - Value type of the given allocator (unused)
     * @param alloc  - Allocator instance of the type specified for the pool.
     *
     * @details Constructs a pool with default thread count and lazy argument latency.
     */
    explicit task_pool_t( Allocator const& alloc )
        : task_pool_t( std::thread::hardware_concurrency(), alloc )
    {
    }

    /**
     * @brief Construct a new task pool object
     *
     * @tparam Value       - Value type of the given allocator (unused)
     * @param thread_count - The desired amount of threads for the pool
     * @param alloc        - Allocator<Value> instance for the type specified in the pool.
     *
     * @details Constructs a pool with a specfic amount of threads and a instance or the declared
     * allocator type. The lazy argument checker latency will be 1us and if the thread count is zero
     * it will be translated as std::thread::hardware_concurrency.
     */
    explicit task_pool_t( const unsigned thread_count, Allocator const& alloc )
        : task_pool_t( std::chrono::microseconds( 1 ), thread_count, alloc )
    {
    }
    /**
     * @brief Destroys the task_pool. Will attempt to cancel tasks that support it
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
        , allocator_( std::move( other.allocator_ ) )
    {
    }
    /**
     * @brief task_pool objects may be move assigned (even while running)
     */
    task_pool_t& operator=( task_pool_t&& other ) noexcept
    {
        reset();
        std::swap( runtime_, other.runtime_ );
        allocator_ = std::move( other.allocator_ );
        return *this;
    }

    /**
     * @brief Resets the task_pool to the given amount of threads (completes all
     * currently running tasks).
     *
     * @details Most methods have as a precondition that the pool_runtime will never be nullptr and
     * so if that is the post condition of this method we can not continue and std::terminate will
     * be called. This can only occure if `new` throws std::bad_alloc and as such the program has
     * pretty much done everything it can do.
     *
     */
    void reset( const unsigned requested_thread_count = 0 ) noexcept
    {
        const bool was_paused = is_paused();
        pause();
        wait();
        runtime_.reset( new ( std::nothrow )
                            pool_runtime( get_check_latency(), requested_thread_count ) );
        if ( !runtime_ )
        {
            // new reset() will only throw in the case of std::bad_alloc and since we have
            // a precondition to most methods that the runtime can never be nullptr we
            // simply can not continue in this situation and must terminate.
            std::terminate();
        }
        if ( !was_paused )
        {
            unpause();
        }
    }

    /**
     * @brief Sets the stop_token and waits for tasks to complete
     *
     * @details Most methods have as a precondition that the pool_runtime will never be nullptr and
     * so if that is the post condition of this method we can not continue and std::terminate will
     * be called. This can only occure if `new` throws std::bad_alloc and as such the program has
     * pretty much done everything it can do.
     */
    void abort() noexcept
    {
        auto thread_count = get_thread_count();
        auto latency      = get_check_latency();
        ( *runtime_ ).abort();
        runtime_.reset( new ( std::nothrow ) pool_runtime( latency, thread_count ) );
        if ( !runtime_ )
        {
            // new reset() will only throw in the case of std::bad_alloc and since we have
            // a precondition to most methods that the runtime can never be nullptr we
            // simply can not continue in this situation and must terminate.
            std::terminate();
        }
    }

    /**
     * @brief Returns the amount of tasks in the pool not currently running
     */
    BE_NODISGARD std::size_t get_tasks_queued() const noexcept
    {
        return ( *runtime_ ).tasks_queued_;
    }

    /**
     * @brief Returns the amount of tasks in the pool currently running
     */
    BE_NODISGARD std::size_t get_tasks_running() const noexcept
    {
        return ( *runtime_ ).tasks_running_;
    }
    /**
     * @brief Returns the amount of tasks in the pool currently awaiting input arguments
     */
    BE_NODISGARD std::size_t get_tasks_waiting() const noexcept
    {
        return ( *runtime_ ).tasks_waiting_;
    }

    /**
     * @brief Returns the total amount of tasks in the pool, queued, running and waiting
     */
    BE_NODISGARD std::size_t get_tasks_total() const noexcept
    {
        return ( *runtime_ ).tasks_queued_ + ( *runtime_ ).tasks_running_ +
               ( *runtime_ ).tasks_waiting_;
    }

    /**
     * @brief Returns the amount of threads in the pool
     */
    BE_NODISGARD unsigned get_thread_count() const noexcept { return ( *runtime_ ).thread_count_; }

    /**
     * @brief Returns if the pool has been paused
     */
    BE_NODISGARD bool is_paused() const noexcept { return ( *runtime_ ).paused_; }

    /**
     * @brief Pauses the pool. No tasked will be queued while the pool is paused
     */
    void pause() noexcept { ( *runtime_ ).paused_ = true; }

    /**
     * @brief Resumes the enqueueing of tasks in the pool
     */
    void unpause() noexcept { ( *runtime_ ).paused_ = false; }

    /**
     * @brief Part of the future-like api `get()` is simply an alias for `wait()`. As task_pools
     * have no inherent return value we simply return the boolean true to allow pools to be used
     * in pipelines
     */
    bool get() noexcept
    {
        wait();
        return true;
    }
    /**
     * @brief Blocks calling thread until all tasks have completed. No tasked may be submitted while
     * pool is waiting. If called while paused the function does nothing to avoid deadlocks
     */
    void wait() noexcept { ( *runtime_ ).wait(); }

    /**
     * @brief Part of the future-like api
     *
     * @tparam Duration
     * @param x
     * @return std::future_status
     */
    template< typename Duration >
    std::future_status wait_for( Duration&& x ) noexcept
    {
        return ( *runtime_ ).wait_for( std::forward< Duration >( x ) );
    }

    template< typename Timepoint >
    std::future_status wait_until( Timepoint&& x ) noexcept
    {
        return ( *runtime_ ).wait_until( std::forward< Timepoint >( x ) );
    }
    /**
     * @brief Returns a stop token for the pool.
     */
    stop_token get_stop_token() const noexcept { return stop_token{ ( *runtime_ ).abort_ }; };

    /**
     * @brief Get the maximum duration used to wait prior to checking lazy input arguments
     *
     * @return std::chrono::nanoseconds
     */
    std::chrono::nanoseconds get_check_latency() const noexcept
    {
        return ( *runtime_ ).task_check_latency_;
    }

    void invoke_deferred() { ( *runtime_ ).invoke_deferred(); }

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
    Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        Promise< Return > promise( std::allocator_arg_t{}, allocator_ );
        auto              task_future = promise.get_future();
        ( *runtime_ )
            .push_task( launch,
                        make_task( [task_function = std::bind( std::forward< Func >( task ),
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
              typename FunctionAllocator =
                  decltype( rebind_alloc< typename wants_allocator< FuncType >::value_type >(
                      std::declval< Allocator >() ) ),
              typename Return = be_invoke_result_t< std::decay_t< Func >,
                                                    std::allocator_arg_t,
                                                    FunctionAllocator const&,
                                                    Args... >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    be_is_void_v< Return > && wants_allocator_v< Func >,
                                bool > = true >
    Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        auto promise     = Promise< Return >( std::allocator_arg_t{}, allocator_ );
        auto task_future = promise.get_future();
        ( *runtime_ )
            .push_task( launch,
                        make_task( [task_function = std::bind( std::forward< Func >( task ),
                                                               std::allocator_arg_t{},
                                                               FunctionAllocator( allocator_ ),
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
     * @param task A callable value type that takes an allocator and a be::stop_token
     * void(std::allocator_arg_t, Allocator<T> const&, ..., be::stop_token )
     * @param args A parameter pack of input arguments to task
     * @return Future<void>
     */
    template< template< typename > class Promise = std::promise,
              typename Func,
              typename... Args,
              typename FuncType = std::remove_cv_t< std::remove_reference_t< Func > >,
              typename FunctionAllocator =
                  decltype( rebind_alloc< typename wants_allocator< FuncType >::value_type >(
                      std::declval< Allocator >() ) ),
              typename Return = be_invoke_result_t< std::decay_t< Func >,
                                                    std::allocator_arg_t,
                                                    FunctionAllocator,
                                                    Args...,
                                                    stop_token >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    be_is_void_v< Return > && wants_stop_token_v< Func > &&
                                    wants_allocator_v< Func >,
                                bool > = true >
    Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        auto promise     = Promise< Return >( std::allocator_arg_t{}, allocator_ );
        auto task_future = promise.get_future();
        ( *runtime_ )
            .push_task( launch,
                        make_task( [task_function = std::bind( std::forward< Func >( task ),
                                                               std::allocator_arg_t{},
                                                               FunctionAllocator( allocator_ ),
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
    Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        auto promise     = Promise< Return >( std::allocator_arg_t{}, allocator_ );
        auto task_future = promise.get_future();
        ( *runtime_ )
            .push_task( launch,
                        make_task( [task_function = std::bind( std::forward< Func >( task ),
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
    BE_NODISGARD Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        Promise< Return > promise( std::allocator_arg_t{}, allocator_ );
        auto              future = promise.get_future();
        ( *runtime_ )
            .push_task( launch,
                        make_task( [task_function = std::bind( std::forward< Func >( task ),
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
              typename FunctionAllocator =
                  decltype( rebind_alloc< typename wants_allocator< FuncType >::value_type >(
                      std::declval< Allocator >() ) ),
              typename Return = be_invoke_result_t< std::decay_t< Func >,
                                                    std::allocator_arg_t,
                                                    FunctionAllocator const&,
                                                    Args... >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    !be_is_void_v< Return > && wants_allocator_v< Func >,
                                bool > = true >
    BE_NODISGARD Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        Promise< Return > promise( std::allocator_arg_t{}, allocator_ );
        auto              future = promise.get_future();
        ( *runtime_ )
            .push_task( launch,
                        make_task( [task_function = std::bind( std::forward< Func >( task ),
                                                               std::allocator_arg_t{},
                                                               FunctionAllocator( allocator_ ),
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
    BE_NODISGARD Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        Promise< Return > promise( std::allocator_arg_t{}, allocator_ );
        auto              future = promise.get_future();
        ( *runtime_ )
            .push_task( launch,
                        make_task( [task_function = std::bind( std::forward< Func >( task ),
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
              typename FunctionAllocator =
                  decltype( rebind_alloc< typename wants_allocator< FuncType >::value_type >(
                      std::declval< Allocator >() ) ),
              typename Return = be_invoke_result_t< std::decay_t< FuncType >,
                                                    std::allocator_arg_t,
                                                    FunctionAllocator,
                                                    Args...,
                                                    stop_token >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< Return > >::value &&
                                    !be_is_void_v< Return > && wants_stop_token_v< Func > &&
                                    wants_allocator_v< Func >,
                                bool > = true >
    BE_NODISGARD Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        Promise< Return > promise( std::allocator_arg_t{}, allocator_ );
        auto              future = promise.get_future();
        ( *runtime_ )
            .push_task( launch,
                        make_task( [task_function = std::bind( std::forward< Func >( task ),
                                                               std::allocator_arg_t{},
                                                               FunctionAllocator( allocator_ ),
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
    BE_NODISGARD Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        return make_defered_task(
            launch,
            Promise< Return >{ std::allocator_arg_t{}, allocator_ },
            std::forward< Func >( task ),
            std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )... ) );
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
              typename FuncType = std::remove_reference_t< std::remove_cv_t< Func > >,
              typename FunctionAllocator =
                  decltype( rebind_alloc< typename wants_allocator< FuncType >::value_type >(
                      std::declval< Allocator >() ) ),
              typename Return = be_invoke_result_t< std::decay_t< Func >,
                                                    std::allocator_arg_t,
                                                    FunctionAllocator const&,
                                                    future_argument_t< std::decay_t< Args > >... >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && wants_allocator_v< Func > &&
                                    !wants_stop_token_v< Func > &&
                                    contains_future< std::decay_t< Args >... >::value,
                                bool > = true >
    BE_NODISGARD Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        // note that if task is a member function it can not be forwared the allocator
        // since we use arg0 for the allocator...member functions should likely tie
        // custom allocators to the instance they are member of
        auto args_tuple =
            std::make_tuple( wrap_future_argument( std::allocator_arg_t{} ),
                             wrap_future_argument( FunctionAllocator( allocator_ ) ),
                             wrap_future_argument( std::forward< Args >( args ) )... );
        return make_defered_task( launch,
                                  Promise< Return >{ std::allocator_arg_t{}, allocator_ },
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
              typename FuncType = std::remove_reference_t< std::remove_cv_t< Func > >,
              typename FunctionAllocator =
                  decltype( rebind_alloc< typename wants_allocator< FuncType >::value_type >(
                      std::declval< Allocator >() ) ),
              typename Return = be_invoke_result_t< std::decay_t< Func >,
                                                    std::allocator_arg_t,
                                                    FunctionAllocator const&,
                                                    future_argument_t< std::decay_t< Args > >...,
                                                    stop_token >,
              typename Future = decltype( std::declval< Promise< Return > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && wants_allocator_v< Func > &&
                                    wants_stop_token_v< Func > &&
                                    contains_future< std::decay_t< Args >... >::value,
                                bool > = true >
    BE_NODISGARD Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        // note that if task is a member function it can not be forwared the allocator
        // since we use arg0 for the allocator...member functions should likely tie
        // custom allocators to the instance they are member of
        auto args_tuple = std::make_tuple( wrap_future_argument( std::allocator_arg_t{} ),
                                           wrap_future_argument( FunctionAllocator( allocator_ ) ),
                                           wrap_future_argument( std::forward< Args >( args ) )...,
                                           wrap_future_argument( get_stop_token() ) );
        return make_defered_task( launch,
                                  Promise< Return >{ std::allocator_arg_t{}, allocator_ },
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
    BE_NODISGARD Future submit( std::launch launch, Func&& task, Args&&... args )
    {
        auto args_tuple = std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )...,
                                           wrap_future_argument( get_stop_token() ) );
        return make_defered_task( launch,
                                  Promise< Return >{ std::allocator_arg_t{}, allocator_ },
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
                std::allocator_traits< decltype( tsk->alloc ) >::destroy( tsk->alloc, tsk );
                std::allocator_traits< decltype( tsk->alloc ) >::deallocate( tsk->alloc, tsk, 1 );
            } )
        {
        }
        ~task_proxy()                   = default;
        task_proxy( task_proxy const& ) = delete;
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
            using TaskAllocator = decltype( rebind_alloc< Task >( std::declval< Allocator >() ) );
            Task( TaskAllocator const& a, Func&& f )
                : FuncType( std::forward< Func >( f ) )
                , alloc( a )
            {
            }
            using FuncType::operator();
            static bool     is_ready() { return true; }
            TaskAllocator   alloc;
            ~Task()             = default;
            Task( Task const& ) = delete;
            Task& operator=( Task const& ) = delete;
            Task( Task&& ) noexcept        = delete;
            Task& operator=( Task&& ) = delete;
        };

        typename Task::TaskAllocator task_allocator( allocator_ );
        Task*                        typed_task =
            std::allocator_traits< typename Task::TaskAllocator >::allocate( task_allocator, 1 );
        std::allocator_traits< typename Task::TaskAllocator >::construct(
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

    Future
    make_defered_task( std::launch launch, Promise promise, Func&& task, ArgsTuple args_tuple )
    {
        using FuncType = std::remove_reference_t< std::remove_cv_t< Func > >;
        struct TASKPOOL_HIDDEN Task : FuncType
        {
            using TaskAllocator = decltype( rebind_alloc< Task >( std::declval< Allocator >() ) );

            TaskAllocator alloc;
            Promise       promise_;
            ArgsTuple     arguments_;
            Task( TaskAllocator const& a, Promise&& p, Func&& f, ArgsTuple&& arg )
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
            ~Task()             = default;
            Task( Task const& ) = delete;
            Task& operator=( Task const& ) = delete;
            Task( Task&& ) noexcept        = delete;
            Task& operator=( Task&& ) = delete;
        };
        auto                         future = promise.get_future();
        typename Task::TaskAllocator task_allocator( allocator_ );
        Task*                        typed_task =
            std::allocator_traits< typename Task::TaskAllocator >::allocate( task_allocator, 1 );
        std::allocator_traits< typename Task::TaskAllocator >::construct(
            task_allocator,
            typed_task,
            task_allocator,
            std::move( promise ),
            std::forward< Func >( task ),
            std::move( args_tuple ) );
        ( *runtime_ ).push_task( launch, task_proxy( typed_task ) );
        return future;
    }

    template< class Promise,
              typename Func,
              typename ArgsTuple,
              typename Future = promise_api::get_future_t< Promise >,
              typename Return = future_api::get_result_t< Future >,
              std::enable_if_t< be::is_function_pointer_v< Func >, bool > = true >
    Future
    make_defered_task( std::launch launch, Promise promise, Func&& task, ArgsTuple args_tuple )
    {
        struct TASKPOOL_HIDDEN Task
        {
            using TaskAllocator = decltype( rebind_alloc< Task >( std::declval< Allocator >() ) );

            TaskAllocator alloc;
            Func          func_;
            Promise       promise_;
            ArgsTuple     arguments_;
            Task( TaskAllocator const& a, Promise&& p, Func&& f, ArgsTuple&& arg )
                : alloc( a )
                , func_( f )
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
            ~Task()             = default;
            Task( Task const& ) = delete;
            Task& operator=( Task const& ) = delete;
            Task( Task&& ) noexcept        = delete;
            Task& operator=( Task&& ) = delete;
        };
        auto                         future = promise.get_future();
        typename Task::TaskAllocator task_allocator( allocator_ );
        Task*                        typed_task =
            std::allocator_traits< typename Task::TaskAllocator >::allocate( task_allocator, 1 );
        std::allocator_traits< typename Task::TaskAllocator >::construct(
            task_allocator,
            typed_task,
            task_allocator,
            std::move( promise ),
            std::forward< Func >( task ),
            std::move( args_tuple ) );
        ( *runtime_ ).push_task( launch, task_proxy( typed_task ) );
        return future;
    }

    template< class Promise,
              typename Func,
              typename ArgsTuple,
              typename Future = promise_api::get_future_t< Promise >,
              typename Return = future_api::get_result_t< Future >,
              std::enable_if_t< std::is_member_function_pointer< Func >::value, bool > = true >

    Future make_defered_task( std::launch launch, Promise promise, Func task, ArgsTuple args_tuple )
    {
        struct TASKPOOL_HIDDEN Task
        {
            using TaskAllocator = decltype( rebind_alloc< Task >( std::declval< Allocator >() ) );

            TaskAllocator alloc;
            Func          func_;
            Promise       promise_;
            ArgsTuple     arguments_;
            Task( TaskAllocator const& a, Promise&& p, Func f, ArgsTuple&& arg )
                : alloc( a )
                , func_( f )
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
                    auto self = std::get< 0 >( arguments_ )();
                    auto args = tail( arguments_ );
                    invoke_deferred_task(
                        promise_,
                        std::mem_fn( func_ ),
                        self,
                        args,
                        std::make_index_sequence< std::tuple_size< decltype( args ) >{} >{} );
                }
                catch ( ... )
                {
                    promise_.set_exception( std::current_exception() );
                }
            }
            ~Task()             = default;
            Task( Task const& ) = delete;
            Task& operator=( Task const& ) = delete;
            Task( Task&& ) noexcept        = delete;
            Task& operator=( Task&& ) = delete;
        };
        auto                         future = promise.get_future();
        typename Task::TaskAllocator task_allocator( allocator_ );
        Task*                        typed_task =
            std::allocator_traits< typename Task::TaskAllocator >::allocate( task_allocator, 1 );
        std::allocator_traits< typename Task::TaskAllocator >::construct( task_allocator,
                                                                          typed_task,
                                                                          task_allocator,
                                                                          std::move( promise ),
                                                                          task,
                                                                          std::move( args_tuple ) );
        ( *runtime_ ).push_task( launch, task_proxy( typed_task ) );
        return future;
    }

    task_pool_t( std::chrono::nanoseconds const check_task_latency, unsigned const requested_count )
        : runtime_( std::make_unique< pool_runtime >( check_task_latency, requested_count ) )
        , allocator_()
    {
    }

    task_pool_t( std::chrono::nanoseconds const check_task_latency,
                 unsigned const                 requested_count,
                 Allocator const&               alloc )
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
        std::queue< task_proxy >         tasks_;
        mutable std::mutex               deferred_mutex_ = {};
        std::queue< task_proxy >         deferred_;
        std::atomic< std::size_t >       deferred_queued_{ 0 };
        mutable std::mutex               check_tasks_mutex_ = {};
        std::vector< task_proxy >        tasks_to_check_    = {};
        std::atomic< bool >              waiting_{ false };
        std::atomic< bool >              paused_{ false };
        std::atomic< bool >              abort_{ false };
        unsigned                         thread_count_ = 0;
        std::unique_ptr< std::thread[] > threads_; //  NOLINT (c-arrays)
        std::chrono::nanoseconds         task_check_latency_ = std::chrono::microseconds( 1 );

        explicit pool_runtime( std::chrono::nanoseconds latency, unsigned int requested_count )
            : thread_count_( compute_thread_count( requested_count ) )
            , threads_( std::make_unique< std::thread[] >( thread_count_ ) ) // NOLINT (c-arrays)
            , task_check_latency_( latency )
        {
            create_threads();
        }
        ~pool_runtime() { destroy_threads(); }
        pool_runtime( pool_runtime const& ) = delete;
        pool_runtime& operator=( pool_runtime const& ) = delete;
        pool_runtime( pool_runtime&& )                 = delete;
        pool_runtime& operator=( pool_runtime&& ) = delete;

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
            threads_.reset(); //  NOLINT (c-arrays)
            thread_count_ = 0;
        }

        void abort() { destroy_threads(); }

        static unsigned compute_thread_count( const unsigned thread_count ) noexcept
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

        void wait() noexcept
        {
            waiting_ = true;
            try
            {
                std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
                task_added_.notify_all();
                task_completed_.wait( tasks_lock, [this] {
                    if ( paused_ )
                    {
                        return true;
                    }
                    return ( tasks_queued_ + tasks_running_ + tasks_waiting_ == 0 );
                } );
            }
            catch ( std::system_error const& e ) // std::mutex::lock may throw
            {
                // TODO: implement user logging facility
            }
            waiting_ = false;
        }

        std::future_status wait_for( std::chrono::steady_clock::duration duration ) noexcept
        {
            return wait_until( std::chrono::steady_clock::now() + duration );
        }

        std::future_status wait_until( std::chrono::steady_clock::time_point deadline ) noexcept
        {
            waiting_ = true;
            try
            {
                std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
                task_added_.notify_all();
                auto stop_waiting = [this] {
                    if ( paused_ )
                    {
                        return true;
                    }
                    return ( tasks_queued_ + tasks_running_ + tasks_waiting_ == 0 );
                };
                while ( !stop_waiting() )
                {
                    switch ( task_completed_.wait_until( tasks_lock, deadline ) )
                    {
                    case std::cv_status::timeout: {
                        waiting_ = false;
                        return std::future_status::timeout;
                    }
                    default:
                        continue;
                    }
                }
            }
            catch ( std::system_error const& e ) // std::mutex::lock may throw
            {
                // TODO: implement user logging facility
            }
            waiting_ = false;
            return std::future_status::ready;
        }

        void push_task( std::launch launch, task_proxy proxy )
        {
            if ( proxy.storage.get() == nullptr ) // NOLINT
            {
                throw std::invalid_argument{ "'add_task' called with invalid task_proxy" };
            }
            if ( launch == std::launch::async )
            {
                if ( proxy.check_task( proxy.storage.get() ) )
                {
                    std::unique_lock< std::mutex > lock( tasks_mutex_ );
                    tasks_.push( std::move( proxy ) );
                    ++tasks_queued_;
                }
                else
                {
                    std::unique_lock< std::mutex > lock( check_tasks_mutex_ );
                    tasks_to_check_.push_back( std::move( proxy ) );
                    ++tasks_waiting_;
                }
                task_added_.notify_one();
            }
            else
            {
                std::unique_lock< std::mutex > lock( deferred_mutex_ );
                deferred_.push( std::move( proxy ) );
                ++deferred_queued_;
            }
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

        void invoke_deferred()
        {
            std::queue< task_proxy > tasks;
            {
                std::unique_lock< std::mutex > deferred_lock( deferred_mutex_ );
                std::swap( tasks, deferred_ );
                deferred_queued_ = 0;
            }
            while ( !tasks.empty() )
            {
                task_proxy proxy( std::move( tasks.front() ) );
                tasks.pop();
                if ( proxy.check_task( proxy.storage.get() ) )
                {
                    ++tasks_running_;
                    proxy.execute_task( proxy.storage.get() );
                    --tasks_running_;
                }
                else
                {
                    push_task( std::launch::deferred, std::move( proxy ) );
                }
            }
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
                    // thread_workers first tries to become the next task_checker
                    std::unique_lock< std::mutex > lock( check_tasks_mutex_, std::try_to_lock );
                    if ( lock.owns_lock() && ( tasks_waiting_.load() != 0U ) )
                    {
                        std::vector< task_proxy > ready_tasks = task_checker();
                        for ( task_proxy& proxy_ready : ready_tasks )
                        {
                            push_task( std::launch::async, std::move( proxy_ready ) );
                            --tasks_waiting_;
                        }
                    }
                }
                std::unique_lock< std::mutex > tasks_lock( tasks_mutex_ );
                if ( abort_ )
                {
                    break;
                }
                using namespace std::chrono_literals;
                if ( tasks_waiting_.load() != 0U )
                {
                    task_added_.wait_for(
                        tasks_lock, latency, [this] { return !tasks_.empty() || abort_; } );
                }
                else
                {
                    task_added_.wait_for( tasks_lock, std::chrono::milliseconds( 1 ), [this] {
                        bool has_tasks = !tasks_.empty();
                        return has_tasks || abort_;
                    } );
                }
                if ( abort_ )
                {
                    return;
                }
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
    Allocator                       allocator_{};
};

using task_pool = task_pool_t< std::allocator< void > >;
extern template class task_pool_t< std::allocator< void > >;
} // namespace be

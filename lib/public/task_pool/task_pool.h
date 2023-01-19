#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <task_pool/api.h>
#include <task_pool/traits.h>
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
struct TASKPOOL_API stop_token
{
    std::atomic_bool const& token;

    explicit operator bool();
};

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
class TASKPOOL_API task_pool
{
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
        struct TASKPOOL_HIDDEN Task : FuncType
        {
            explicit Task( FuncType&& f )
                : FuncType( std::forward< F >( f ) )
            {
            }
            using FuncType::operator();
            static bool     is_ready() { return true; }
            ~Task()                        = default;
            Task( Task const& )            = delete;
            Task& operator=( Task const& ) = delete;
            Task( Task&& ) noexcept        = delete;
            Task& operator=( Task&& )      = delete;
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
        struct TASKPOOL_HIDDEN Task : FuncType
        {
            Task( Allocator< Task > const& a, F&& f )
                : FuncType( std::forward< F >( f ) )
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
        Allocator< Task > task_allocator( allocator );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct(
            task_allocator, typed_task, task_allocator, std::forward< F >( task ) );
        return task_proxy( x, task_allocator, typed_task );
    }

    template< template< typename > class Promise,
              typename F,
              typename... Args,
              typename R = be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >... >,
              typename Future = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && be_is_void_v< R >, bool > = true >
    Future make_defered_task( F&& task, Args&&... args )
    {
        using args_type =
            std::tuple< decltype( wrap_future_argument( std::forward< Args >( args ) ) )... >;
        struct TASKPOOL_HIDDEN Task
        {
            Promise< R > promise_;
            F            func_;
            args_type    arguments_;
            bool         is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    invoke_deferred_task(
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} );
                    promise_.set_value();
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
        Promise< R > promise;
        auto         future = promise.get_future();
        push_task( task_proxy( new Task{
            std::move( promise ),
            std::forward< F >( task ),
            std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )... ) } ) );
        return future;
    }

    template< template< typename > class Promise,
              typename F,
              typename... Args,
              typename R =
                  be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >..., stop_token >,
              typename Future = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && be_is_void_v< R >, bool > = true >
    Future make_defered_task( stop_token token, F&& task, Args&&... args )
    {
        using args_type =
            std::tuple< decltype( wrap_future_argument( std::forward< Args >( args ) ) )... >;
        struct TASKPOOL_HIDDEN Task
        {
            stop_token   token_;
            Promise< R > promise_;
            F            func_;
            args_type    arguments_;
            bool         is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    invoke_deferred_task(
                        token_,
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} );
                    promise_.set_value();
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
        Promise< R > promise;
        auto         future = promise.get_future();
        push_task( task_proxy( new Task{
            token,
            std::move( promise ),
            std::forward< F >( task ),
            std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )... ) } ) );
        return future;
    }

    template< template< typename > class Promise,
              typename F,
              typename... Args,
              typename R = be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >... >,
              typename Future = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && !be_is_void_v< R >, bool > = true >
    Future make_defered_task( F&& task, Args&&... args )
    {
        using args_type =
            std::tuple< decltype( wrap_future_argument( std::forward< Args >( args ) ) )... >;
        struct TASKPOOL_HIDDEN Task
        {
            Promise< R > promise_;
            F            func_;
            args_type    arguments_;
            bool         is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    promise_.set_value( invoke_deferred_task(
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} ) );
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
        Promise< R > promise;
        auto         future = promise.get_future();
        push_task( task_proxy( new Task{
            std::move( promise ),
            std::forward< F >( task ),
            std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )... ) } ) );
        return future;
    }

    template< template< typename > class Promise,
              typename F,
              typename... Args,
              typename R =
                  be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >..., stop_token >,
              typename Future = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && !be_is_void_v< R >, bool > = true >
    Future make_defered_task( stop_token token, F&& task, Args&&... args )
    {
        using args_type =
            std::tuple< decltype( wrap_future_argument( std::forward< Args >( args ) ) )... >;
        struct TASKPOOL_HIDDEN Task
        {
            stop_token   token_;
            Promise< R > promise_;
            F            func_;
            args_type    arguments_;
            bool         is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    promise_.set_value( invoke_deferred_task(
                        token_,
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} ) );
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
        Promise< R > promise;
        auto         future = promise.get_future();
        push_task( task_proxy( new Task{
            token,
            std::move( promise ),
            std::forward< F >( task ),
            std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )... ) } ) );
        return future;
    }

    template< template< typename > class Promise,
              template< typename >
              class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R = be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >... >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< R > >::value &&
                                    be_is_void_v< R > && !wants_allocator_v< F >,
                                bool > = true >

    Future make_defered_task( std::allocator_arg_t  x,
                              Allocator< T > const& allocator,
                              F&&                   task,
                              Args&&... args )
    {
        using args_type =
            std::tuple< decltype( wrap_future_argument( std::forward< Args >( args ) ) )... >;
        struct TASKPOOL_HIDDEN Task
        {
            Allocator< Task > alloc;
            Promise< R >      promise_;
            F&&               func_;
            args_type         arguments_;
            Task( Allocator< Task > const& a, Promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }

            bool is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    invoke_deferred_task(
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} );
                    promise_.set_value();
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
        Promise< R >      promise( x, allocator );
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
            std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )... ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

    template< template< typename > class Promise,
              template< typename >
              class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R               = be_invoke_result_t< F,
                                               std::allocator_arg_t,
                                               Allocator< T >,
                                               future_argument_t< std::decay_t< Args > >... >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< R > >::value &&
                                    be_is_void_v< R > && wants_allocator_v< F >,
                                bool > = true >

    Future make_defered_task( std::allocator_arg_t  x,
                              Allocator< T > const& allocator,
                              F&&                   task,
                              Args&&... args )
    {
        auto args_tuple =
            std::make_tuple( wrap_future_argument( std::allocator_arg_t{} ),
                             wrap_future_argument( allocator ),
                             wrap_future_argument( std::forward< Args >( args ) )... );
        using args_type = decltype( args_tuple );
        struct TASKPOOL_HIDDEN Task
        {
            Allocator< Task > alloc;
            Promise< R >      promise_;
            F&&               func_;
            args_type         arguments_;
            Task( Allocator< Task > const& a, Promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }

            bool is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    invoke_deferred_task(
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} );
                    promise_.set_value();
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
        Promise< R >      promise( x, allocator );
        auto              future = promise.get_future();
        Allocator< Task > task_allocator( allocator );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct( task_allocator,
                                                               typed_task,
                                                               task_allocator,
                                                               std::move( promise ),
                                                               std::forward< F >( task ),
                                                               std::move( args_tuple ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

    template< template< typename > class Promise,
              template< typename >
              class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R = be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >... >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< R > >::value &&
                                    !be_is_void_v< R > && !wants_allocator_v< F >,
                                bool > = true >

    Future make_defered_task( std::allocator_arg_t  x,
                              Allocator< T > const& allocator,
                              F&&                   task,
                              Args&&... args )
    {
        using args_type =
            std::tuple< decltype( wrap_future_argument( std::forward< Args >( args ) ) )... >;
        struct TASKPOOL_HIDDEN Task
        {
            Allocator< Task > alloc;
            Promise< R >      promise_;
            F&&               func_;
            args_type         arguments_;
            Task( Allocator< Task > const& a, Promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }
            bool is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    promise_.set_value( invoke_deferred_task(
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} ) );
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
        Promise< R >      promise( x, allocator );
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
            std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )... ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

    template< template< typename > class Promise,
              template< typename >
              class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R               = be_invoke_result_t< F,
                                               std::allocator_arg_t,
                                               Allocator< T >,
                                               future_argument_t< std::decay_t< Args > >... >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< R > >::value &&
                                    !be_is_void_v< R > && wants_allocator_v< F >,
                                bool > = true >

    Future make_defered_task( std::allocator_arg_t  x,
                              Allocator< T > const& allocator,
                              F&&                   task,
                              Args&&... args )
    {
        auto args_tuple =
            std::make_tuple( wrap_future_argument( std::allocator_arg_t{} ),
                             wrap_future_argument( allocator ),
                             wrap_future_argument( std::forward< Args >( args ) )... );
        using args_type = decltype( args_tuple );
        struct TASKPOOL_HIDDEN Task
        {
            Allocator< Task > alloc;
            Promise< R >      promise_;
            F&&               func_;
            args_type         arguments_;
            Task( Allocator< Task > const& a, Promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )

            {
            }
            bool is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    promise_.set_value( invoke_deferred_task(
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} ) );
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
        Promise< R >      promise( x, allocator );
        auto              future = promise.get_future();
        Allocator< Task > task_allocator( allocator );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct( task_allocator,
                                                               typed_task,
                                                               task_allocator,
                                                               std::move( promise ),
                                                               std::forward< F >( task ),
                                                               std::move( args_tuple ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

    template< template< typename > class Promise,
              template< typename >
              class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R =
                  be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >..., stop_token >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< R > >::value &&
                                    be_is_void_v< R > && !wants_allocator_v< F >,
                                bool > = true >

    Future make_defered_task( std::allocator_arg_t  x,
                              Allocator< T > const& allocator,
                              stop_token            token,
                              F&&                   task,
                              Args&&... args )
    {
        using args_type =
            std::tuple< decltype( wrap_future_argument( std::forward< Args >( args ) ) )... >;
        struct TASKPOOL_HIDDEN Task
        {
            Allocator< Task > alloc;
            stop_token        token_;
            Promise< R >      promise_;
            F                 func_;
            args_type         arguments_;
            Task(
                Allocator< Task > const& a, stop_token t, Promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , token_( t )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }
            bool is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    invoke_deferred_task(
                        token_,
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} );
                    promise_.set_value();
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
        Promise< R >      promise( x, allocator );
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
            std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )... ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

    template< template< typename > class Promise,
              template< typename >
              class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R               = be_invoke_result_t< F,
                                               std::allocator_arg_t,
                                               Allocator< T >,
                                               future_argument_t< std::decay_t< Args > >...,
                                               stop_token >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< R > >::value &&
                                    be_is_void_v< R > && wants_allocator_v< F >,
                                bool > = true >

    Future make_defered_task( std::allocator_arg_t  x,
                              Allocator< T > const& allocator,
                              stop_token            token,
                              F&&                   task,
                              Args&&... args )
    {
        auto args_tuple =
            std::make_tuple( wrap_future_argument( std::allocator_arg_t{} ),
                             wrap_future_argument( allocator ),
                             wrap_future_argument( std::forward< Args >( args ) )... );
        using args_type = decltype( args_tuple );
        struct TASKPOOL_HIDDEN Task
        {
            Allocator< Task > alloc;
            stop_token        token_;
            Promise< R >      promise_;
            F                 func_;
            args_type         arguments_;
            Task(
                Allocator< Task > const& a, stop_token t, Promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , token_( t )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }
            bool is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    invoke_deferred_task(
                        token_,
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} );
                    promise_.set_value();
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
        Promise< R >      promise( x, allocator );
        auto              future = promise.get_future();
        Allocator< Task > task_allocator( allocator );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct( task_allocator,
                                                               typed_task,
                                                               task_allocator,
                                                               token,
                                                               std::move( promise ),
                                                               std::forward< F >( task ),
                                                               std::move( args_tuple ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }
    template< template< typename > class Promise,
              template< typename >
              class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R =
                  be_invoke_result_t< F, future_argument_t< std::decay_t< Args > >..., stop_token >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< R > >::value &&
                                    !be_is_void_v< R > && !wants_allocator_v< F >,
                                bool > = true >

    Future make_defered_task( std::allocator_arg_t  x,
                              Allocator< T > const& allocator,
                              stop_token            token,
                              F&&                   task,
                              Args&&... args )
    {
        using args_type =
            std::tuple< decltype( wrap_future_argument( std::forward< Args >( args ) ) )... >;
        struct TASKPOOL_HIDDEN Task
        {
            Allocator< Task > alloc;
            stop_token        token_;
            Promise< R >      promise_;
            F                 func_;
            args_type         arguments_;
            Task(
                Allocator< Task > const& a, stop_token t, Promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , token_( t )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }
            bool is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    promise_.set_value( invoke_deferred_task(
                        token_,
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} ) );
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
        Promise< R >      promise( x, allocator );
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
            std::make_tuple( wrap_future_argument( std::forward< Args >( args ) )... ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

    template< template< typename > class Promise,
              template< typename >
              class Allocator,
              typename F,
              typename T,
              typename... Args,
              typename R               = be_invoke_result_t< F,
                                               std::allocator_arg_t,
                                               Allocator< T >,
                                               future_argument_t< std::decay_t< Args > >...,
                                               stop_token >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< R > >::value &&
                                    !be_is_void_v< R > && wants_allocator_v< F >,
                                bool > = true >

    Future make_defered_task( std::allocator_arg_t  x,
                              Allocator< T > const& allocator,
                              stop_token            token,
                              F&&                   task,
                              Args&&... args )
    {
        auto args_tuple =
            std::make_tuple( wrap_future_argument( std::allocator_arg_t{} ),
                             wrap_future_argument( allocator ),
                             wrap_future_argument( std::forward< Args >( args ) )... );
        using args_type = decltype( args_tuple );
        struct TASKPOOL_HIDDEN Task
        {
            Allocator< Task > alloc;
            stop_token        token_;
            Promise< R >      promise_;
            F                 func_;
            args_type         arguments_;
            Task(
                Allocator< Task > const& a, stop_token t, Promise< R >&& p, F&& f, args_type&& arg )
                : alloc( a )
                , token_( t )
                , promise_( std::move( p ) )
                , func_( std::forward< F >( f ) )
                , arguments_( std::move( arg ) )
            {
            }
            bool is_ready() const
            {
                return check_argument_status(
                    arguments_, std::make_index_sequence< std::tuple_size< args_type >{} >{} );
            }
            auto operator()()
            {
                try
                {
                    promise_.set_value( invoke_deferred_task(
                        token_,
                        func_,
                        arguments_,
                        std::make_index_sequence< std::tuple_size< args_type >{} >{} ) );
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
        Promise< R >      promise( x, allocator );
        auto              future = promise.get_future();
        Allocator< Task > task_allocator( allocator );
        Task*             typed_task =
            std::allocator_traits< Allocator< Task > >::allocate( task_allocator, 1 );
        std::allocator_traits< Allocator< Task > >::construct( task_allocator,
                                                               typed_task,
                                                               task_allocator,
                                                               token,
                                                               std::move( promise ),
                                                               std::forward< F >( task ),
                                                               std::move( args_tuple ) );
        push_task( task_proxy( x, task_allocator, typed_task ) );
        return future;
    }

public:
    /**
     * @brief Constructs a task_pool with given amount of threads
     */
    explicit task_pool( const unsigned = 0 );

    template< typename Duration >
    task_pool( Duration&& lazy_check_latency, const unsigned thread_count )
        : task_pool( std::chrono::duration_cast< std::chrono::nanoseconds >( lazy_check_latency ),
                     thread_count )
    {
    }

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
     * @brief Get the maximum duration used to wait prior to checking lazy input arguments
     *
     * @return std::chrono::nanoseconds
     */
    std::chrono::nanoseconds get_check_latency() const;
    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class Promise = std::promise,
              typename F,
              typename... A,
              typename R      = be_invoke_result_t< std::decay_t< F >, A... >,
              typename Future = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && be_is_void_v< R >, bool > = true >
    Future submit( F&& task, A&&... args )
    {
        auto promise     = Promise< R >();
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
    template< template< typename > class Promise = std::promise,
              typename F,
              typename... A,
              typename R               = be_invoke_result_t< std::decay_t< F >, A..., stop_token >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && be_is_void_v< R > &&
                                    wants_stop_token_v< F >,
                                bool > = true >
    Future submit( F&& task, A&&... args )
    {
        auto promise     = Promise< R >();
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
    template< template< typename > class Promise = std::promise,
              typename F,
              typename... Args,
              typename R      = be_invoke_result_t< std::decay_t< F >, Args... >,
              typename Future = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && !be_is_void_v< R >, bool > = true >
    BE_NODISGARD Future submit( F&& task, Args&&... args )
    {
        auto promise = Promise< R >();
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
    template< template< typename > class Promise = std::promise,
              typename F,
              typename... Args,
              typename R      = be_invoke_result_t< std::decay_t< F >, Args..., stop_token >,
              typename Future = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && !be_is_void_v< R > &&
                                    wants_stop_token_v< F >,
                                bool > = true >
    BE_NODISGARD Future submit( F&& task, Args&&... args )
    {
        auto promise = Promise< R >();
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
    template< template< typename > class Promise = std::promise,
              template< typename >
              class Allocator,
              typename F,
              typename... A,
              typename R = be_invoke_result_t< std::decay_t< F >, A... >,
              typename T,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< R > >::value &&
                                    be_is_void_v< R > && !wants_allocator_v< F >,
                                bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, A&&... args )
    {
        auto promise     = Promise< R >( x, allocator );
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
    template<
        template< typename > class Promise = std::promise,
        template< typename >
        class Allocator,
        typename F,
        typename... A,
        typename T,
        typename R =
            be_invoke_result_t< std::decay_t< F >, std::allocator_arg_t, Allocator< T >, A... >,
        typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
        std::enable_if_t< be::is_promise_v< Promise > &&
                              be::is_allocator_constructible< Promise< R > >::value &&
                              be_is_void_v< R > && wants_allocator_v< F >,
                          bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, A&&... args )
    {
        auto promise     = Promise< R >( x, allocator );
        auto task_future = promise.get_future();
        push_task( make_task( x,
                              allocator,
                              [task_function = std::bind( std::forward< F >( task ),
                                                          std::allocator_arg_t{},
                                                          allocator,
                                                          std::forward< A >( args )... ),
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
        template< typename > class Promise = std::promise,
        template< typename >
        class Allocator,
        typename F,
        typename... A,
        typename R = be_invoke_result_t< std::decay_t< F >, A..., stop_token >,
        typename T,
        typename Future = decltype( std::declval< Promise< R > >().get_future() ),
        std::enable_if_t<
            be::is_promise_v< Promise > && be::is_allocator_constructible< Promise< R > >::value &&
                be_is_void_v< R > && wants_stop_token_v< F > && !wants_allocator_v< F >,
            bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, A&&... args )
    {
        auto promise     = Promise< R >( x, allocator );
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
    template<
        template< typename > class Promise = std::promise,
        template< typename >
        class Allocator,
        typename F,
        typename... A,
        typename T,
        typename R      = be_invoke_result_t< std::decay_t< F >,
                                         std::allocator_arg_t,
                                         Allocator< T >,
                                         A...,
                                         stop_token >,
        typename Future = decltype( std::declval< Promise< R > >().get_future() ),
        std::enable_if_t<
            be::is_promise_v< Promise > && be::is_allocator_constructible< Promise< R > >::value &&
                be_is_void_v< R > && wants_stop_token_v< F > && wants_allocator_v< F >,
            bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, A&&... args )
    {
        auto promise     = Promise< R >( x, allocator );
        auto task_future = promise.get_future();
        push_task( make_task( x,
                              allocator,
                              [task_function = std::bind( std::forward< F >( task ),
                                                          std::allocator_arg_t{},
                                                          allocator,
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
    template<
        template< typename > class Promise = std::promise,
        template< typename >
        class Allocator,
        typename F,
        typename... Args,
        typename R        = be_invoke_result_t< std::decay_t< F >, Args... >,
        typename FuncType = std::remove_reference_t< std::remove_cv_t< F > >,
        typename T,
        typename Future = decltype( std::declval< Promise< R > >().get_future() ),
        std::enable_if_t<
            be::is_promise_v< Promise > && be::is_allocator_constructible< Promise< R > >::value &&
                be::is_promise_v< Promise > && !be_is_void_v< R > && !wants_allocator_v< F >,
            bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, Args&&... args )
    {
        Promise< R > promise( x, allocator );
        auto         future = promise.get_future();
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
    template< template< typename > class Promise = std::promise,
              template< typename >
              class Allocator,
              typename F,
              typename... Args,
              typename T,
              typename R               = be_invoke_result_t< std::decay_t< F >,
                                               std::allocator_arg_t,
                                               Allocator< T > const&,
                                               Args... >,
              typename FuncType        = std::remove_reference_t< std::remove_cv_t< F > >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    be::is_allocator_constructible< Promise< R > >::value &&
                                    !be_is_void_v< R > && wants_allocator_v< F >,
                                bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, Args&&... args )
    {
        Promise< R > promise( x, allocator );
        auto         future = promise.get_future();
        push_task( make_task( x,
                              allocator,
                              [task_function = std::bind( std::forward< F >( task ),
                                                          std::allocator_arg_t{},
                                                          allocator,
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
        template< typename > class Promise = std::promise,
        template< typename >
        class Allocator,
        typename F,
        typename... Args,
        typename R        = be_invoke_result_t< std::decay_t< F >, Args..., stop_token >,
        typename FuncType = std::remove_reference_t< std::remove_cv_t< F > >,
        typename T,
        typename Future = decltype( std::declval< Promise< R > >().get_future() ),
        std::enable_if_t<
            be::is_promise_v< Promise > && be::is_allocator_constructible< Promise< R > >::value &&
                !be_is_void_v< R > && wants_stop_token_v< F > && !wants_allocator_v< F >,
            bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, Args&&... args )
    {
        Promise< R > promise( x, allocator );
        auto         future = promise.get_future();
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
    template<
        template< typename > class Promise = std::promise,
        template< typename >
        class Allocator,
        typename F,
        typename... Args,
        typename T,
        typename R        = be_invoke_result_t< std::decay_t< F >,
                                         std::allocator_arg_t,
                                         Allocator< T >,
                                         Args...,
                                         stop_token >,
        typename FuncType = std::remove_reference_t< std::remove_cv_t< F > >,
        typename Future   = decltype( std::declval< Promise< R > >().get_future() ),
        std::enable_if_t<
            be::is_promise_v< Promise > && be::is_allocator_constructible< Promise< R > >::value &&
                !be_is_void_v< R > && wants_stop_token_v< F > && wants_allocator_v< F >,
            bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, Args&&... args )
    {
        Promise< R > promise( x, allocator );
        auto         future = promise.get_future();
        push_task( make_task( x,
                              allocator,
                              [task_function = std::bind( std::forward< F >( task ),
                                                          std::allocator_arg_t{},
                                                          allocator,
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
    template< template< typename > class Promise = std::promise,
              typename F,
              typename... A,
              typename R               = be_invoke_result_t< std::decay_t< F >,
                                               future_argument_t< std::decay_t< A > >... >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    contains_future< std::decay_t< A >... >::value,
                                bool > = true >
    Future submit( F&& task, A&&... args )
    {
        return make_defered_task< Promise >( std::forward< F >( task ),
                                             std::forward< A >( args )... );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class Promise = std::promise,
              typename F,
              typename... A,
              typename R               = be_invoke_result_t< std::decay_t< F >,
                                               future_argument_t< std::decay_t< A > >...,
                                               stop_token >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    contains_future< std::decay_t< A >... >::value &&
                                    wants_stop_token_v< F >,
                                bool > = true >
    Future submit( F&& task, A&&... args )
    {
        return make_defered_task< Promise >(
            get_stop_token(), std::forward< F >( task ), std::forward< A >( args )... );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class Promise = std::promise,
              template< typename >
              class Allocator,
              typename F,
              typename... A,
              typename T,
              typename R               = be_invoke_result_t< std::decay_t< F >,
                                               future_argument_t< std::decay_t< A > >... >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > &&
                                    contains_future< std::decay_t< A >... >::value,
                                bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, A&&... args )
    {
        return make_defered_task< Promise >(
            x, allocator, std::forward< F >( task ), std::forward< A >( args )... );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class Promise = std::promise,
              template< typename >
              class Allocator,
              typename F,
              typename... A,
              typename T,
              typename R               = be_invoke_result_t< std::decay_t< F >,
                                               std::allocator_arg_t,
                                               Allocator< T >,
                                               future_argument_t< std::decay_t< A > >... >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && wants_allocator_v< F > &&
                                    !wants_stop_token_v< F > &&
                                    contains_future< std::decay_t< A >... >::value,
                                bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, A&&... args )
    {
        return make_defered_task< Promise >(
            x, allocator, std::forward< F >( task ), std::forward< A >( args )... );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class Promise = std::promise,
              template< typename >
              class Allocator,
              typename F,
              typename... A,
              typename T,
              typename R               = be_invoke_result_t< std::decay_t< F >,
                                               std::allocator_arg_t,
                                               Allocator< T >,
                                               future_argument_t< std::decay_t< A > >...,
                                               stop_token >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && wants_allocator_v< F > &&
                                    wants_stop_token_v< F > &&
                                    contains_future< std::decay_t< A >... >::value,
                                bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, A&&... args )
    {
        return make_defered_task< Promise >( x,
                                             allocator,
                                             get_stop_token(),
                                             std::forward< F >( task ),
                                             std::forward< A >( args )... );
    }

    /**
     * @brief Adds a callable to the task_pool returning a future with the result
     */
    template< template< typename > class Promise = std::promise,
              template< typename >
              class Allocator,
              typename F,
              typename... A,
              typename T,
              typename R               = be_invoke_result_t< std::decay_t< F >,
                                               future_argument_t< std::decay_t< A > >...,
                                               stop_token >,
              typename Future          = decltype( std::declval< Promise< R > >().get_future() ),
              std::enable_if_t< be::is_promise_v< Promise > && !wants_allocator_v< F > &&
                                    wants_stop_token_v< F > &&
                                    contains_future< std::decay_t< A >... >::value,
                                bool > = true >
    BE_NODISGARD Future
    submit( std::allocator_arg_t x, Allocator< T > const& allocator, F&& task, A&&... args )
    {
        return make_defered_task< Promise >( x,
                                             allocator,
                                             get_stop_token(),
                                             std::forward< F >( task ),
                                             std::forward< A >( args )... );
    }

private:
    task_pool( std::chrono::nanoseconds const, unsigned const );
    void push_task( task_proxy&& );
#if defined( _WIN32 )
#    pragma warning( push )
#    pragma warning( disable : 4251 )
#endif
    struct Impl;
    std::unique_ptr< Impl > impl_;
#if defined( _WIN32 )
#    pragma warning( pop )
#endif
};

} // namespace be

// your declarations that cause 4251

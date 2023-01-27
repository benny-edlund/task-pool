#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <task_pool/api.h>
#include <type_traits>
#include <utility>
#if __cplusplus < 201700
#    include <task_pool/fallbacks.h>
#endif

namespace be {

// Thank you Walter
template< typename T, typename... Ts >
struct is_one_of;
template< typename T >
struct is_one_of< T > : std::false_type
{
};
template< typename T, typename... Ts >
struct is_one_of< T, T, Ts... > : std::true_type
{
};
template< typename T, typename U, typename... Ts >
struct is_one_of< T, U, Ts... > : is_one_of< T, Ts... >
{
};

template< typename T, typename Functor = std::remove_reference_t< std::remove_cv_t< T > > >
struct arguments : public arguments< decltype( &Functor::operator() ) >
{
};

template< typename R, typename... Args >
struct arguments< R( Args... ) >
{
    using type = typename std::tuple< Args... >;
};

template< typename R, typename... Args >
struct arguments< R ( * )( Args... ) >
{
    using type = typename std::tuple< Args... >;
};

template< class C, typename R, typename... Args >
struct arguments< R ( C::* )( Args... ) >
{
    using type = typename std::tuple< Args... >;
};

template< class C, typename R, typename... Args >
struct arguments< R ( C::* )( Args... ) const >
{
    using type = typename std::tuple< Args... >;
};

struct stop_token;

template< typename... Ts >
struct contains_stop_token : is_one_of< stop_token, Ts... >
{
};

template< typename T, typename Functor = std::remove_reference_t< std::remove_cv_t< T > > >
struct wants_stop_token : public wants_stop_token< decltype( &Functor::operator() ) >
{
};

template< typename R, typename... Args >
struct wants_stop_token< R( Args... ) > : public contains_stop_token< Args... >
{
};

template< typename R, typename... Args >
struct wants_stop_token< R ( * )( Args... ) > : public contains_stop_token< Args... >
{
};

template< class C, typename R, typename... Args >
struct wants_stop_token< R ( C::* )( Args... ) > : public contains_stop_token< Args... >
{
};

template< class C, typename R, typename... Args >
struct wants_stop_token< R ( C::* )( Args... ) const > : public contains_stop_token< Args... >
{
};

template< typename T >
static constexpr bool wants_stop_token_v = wants_stop_token< T >::value;

template< typename... Ts >
struct contains_allocator_arg : is_one_of< std::allocator_arg_t, Ts... >
{
};

template< typename T, typename = void >
struct allocator_value
{
    using type = void;
};

template< typename T >
struct allocator_value< T, be_void_t< typename std::decay_t< T >::value_type > >
{
    using type = typename std::decay_t< T >::value_type;
};

template< std::size_t N,
          typename TupleArgs,
          std::enable_if_t< std::less_equal< std::size_t >{}(
                                std::tuple_size< std::decay_t< TupleArgs > >::value, N ),
                            bool > = true >
void nth_allocator_value( TupleArgs& );

template< std::size_t N,
          typename TupleArgs,
          std::enable_if_t< std::greater< std::size_t >{}(
                                std::tuple_size< std::decay_t< TupleArgs > >::value, N ),
                            bool > = true >
auto nth_allocator_value( TupleArgs& ) ->
    typename allocator_value< std::tuple_element_t< N, std::decay_t< TupleArgs > > >::type;

template< typename T, typename Functor = std::decay_t< T > >
struct wants_allocator : public wants_allocator< decltype( &Functor::operator() ) >
{
};

template< typename R, typename... Args >
struct wants_allocator< R( Args... ) > : public contains_allocator_arg< Args... >
{
    using value_type =
        decltype( nth_allocator_value< 1 >( std::declval< std::tuple< Args... >& >() ) );
};

template< typename R, typename... Args >
struct wants_allocator< R ( * )( Args... ) > : public contains_allocator_arg< Args... >
{
    using value_type =
        decltype( nth_allocator_value< 1 >( std::declval< std::tuple< Args... >& >() ) );
};

template< class C, typename R, typename... Args >
struct wants_allocator< R ( C::* )( Args... ) > : public contains_allocator_arg< Args... >
{
    using value_type =
        decltype( nth_allocator_value< 1 >( std::declval< std::tuple< Args... >& >() ) );
};

template< class C, typename R, typename... Args >
struct wants_allocator< R ( C::* )( Args... ) const > : public contains_allocator_arg< Args... >
{
    using value_type =
        decltype( nth_allocator_value< 1 >( std::declval< std::tuple< Args... >& >() ) );
};

template< typename T, typename = void, typename... Us >
struct is_allocator_constructible : std::false_type
{
};

template< typename T, typename... Us >
struct is_allocator_constructible< T,
                                   be_void_t< decltype( T( std::declval< std::allocator_arg_t >(),
                                                           std::declval< std::allocator< void > >(),
                                                           std::declval< Us >()... ) ) >,
                                   Us... > : std::true_type
{
};

template< typename T >
static constexpr bool wants_allocator_v = wants_allocator< T >::value;

namespace future_api {
template< typename Future >
using get_result_t = decltype( std::declval< Future >().get() );

template< typename Future >
using wait_result_t = decltype( std::declval< Future >().wait() );

template< typename Future >
using wait_for_result_t =
    decltype( std::declval< Future >().wait_for( std::declval< std::chrono::seconds >() ) );

template< typename Future >
using wait_until_result_t = decltype( std::declval< Future >().wait_until(
    std::declval< std::chrono::steady_clock::time_point >() ) );

template< typename T, typename = void >
struct is_future_status : std::false_type
{
};

template< typename T >
struct is_future_status<
    T,
    be_void_t< decltype( T::ready ), decltype( T::timeout ), decltype( T::deferred ) > >
    : std::is_enum< T >
{
};

template< typename T, typename = void >
struct is_supported : std::false_type
{
};

template< typename T >
struct is_supported< T,
                     be_void_t< get_result_t< T >,
                                wait_result_t< T >,
                                wait_for_result_t< T >,
                                wait_until_result_t< T > > >
    : std::conditional< std::is_same< void, wait_result_t< T > >::value &&
                            is_future_status< wait_for_result_t< T > >::value &&
                            is_future_status< wait_until_result_t< T > >::value,
                        std::true_type,
                        std::false_type >::type
{
};

} // namespace future_api

template< typename T >
struct is_future : future_api::is_supported< T >::type
{
};

template<>
struct is_future< double > : std::false_type
{
};

template< typename... Ts >
struct contains_future;
template< typename T >
struct contains_future< T > : is_future< T >
{
};
template< typename T, typename... Ts >
struct contains_future< T, Ts... >
    : std::conditional_t< is_future< T >::value, std::true_type, contains_future< Ts... > >
{
};

template< typename T >
using future_value_t = future_api::get_result_t< T >;

template< typename T, typename = void >
struct future_argument
{
    using type = T;
};

template< typename T >
struct future_argument< T, be_void_t< future_api::get_result_t< T > > >
{
    using type = future_value_t< T >;
};

template< typename T >
using future_argument_t = typename future_argument< T >::type;

namespace promise_api {

template< typename Promise >
using get_future_t = decltype( std::declval< Promise >().get_future() );

template< typename Promise >
using set_value_t =
    decltype( &Promise::set_value ); // not the best option but only one i could get working

template< typename Promise >
using set_exception_t =
    decltype( std::declval< Promise >().set_exception( std::declval< std::exception_ptr >() ) );

template< typename T, typename = void >
struct is_supported : std::false_type
{
};

template< template< typename > class T, typename V >
struct is_supported<
    T< V >,
    be_void_t< get_future_t< T< V > >, set_value_t< T< V > >, set_exception_t< T< V > > > >
    : std::
          conditional< is_future< get_future_t< T< V > > >::value, std::true_type, std::false_type >
{
};

} // namespace promise_api

template< template< typename > class T, typename V = void >
struct is_promise : promise_api::is_supported< T< V > >::type
{
};

template< template< typename > class T >
static constexpr bool is_promise_v = is_promise< T >::value;

template< typename T >
struct is_movable
    : std::conditional_t< std::is_move_constructible< T >::value ||
                              std::is_move_assignable< T >::value,
                          std::true_type,
                          std::false_type >::type
{
};

template< typename T, std::enable_if_t< !be::is_future< T >::value, bool > = true >
auto wrap_future_argument( T&& t )
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
auto wrap_future_argument( T&& t )
{
    using status_type =
        decltype( std::declval< T >().wait_for( std::declval< std::chrono::seconds >() ) );
    static_assert( future_api::is_future_status< status_type >::value,
                   "T::wait_for does not return a future_status-like enum" );
    struct func_
    {
        T                       value;
        be::future_value_t< T > operator()() { return value.get(); }
        bool                    is_ready() const
        {
            return value.wait_for( std::chrono::seconds( 0 ) ) == status_type::ready;
        }
    };
    return func_{ std::forward< T >( t ) };
}

template< typename Promise,
          typename Callable,
          typename Instance,
          typename Arguments,
          std::size_t... Is,
          std::enable_if_t<
              be_is_void_v< future_api::get_result_t< promise_api::get_future_t< Promise > > >,
              bool > = true >
void invoke_deferred_task( Promise&   promise,
                           Callable   callable,
                           Instance*  self,
                           Arguments& arguments,
                           std::index_sequence< Is... > /*Is*/ )
{
    callable( *self, std::get< Is >( arguments )()... );
    promise.set_value();
}

template< typename Promise,
          typename Callable,
          typename Instance,
          typename Arguments,
          std::size_t... Is,
          std::enable_if_t<
              !be_is_void_v< future_api::get_result_t< promise_api::get_future_t< Promise > > >,
              bool > = true >
void invoke_deferred_task( Promise&   promise,
                           Callable   callable,
                           Instance*  self,
                           Arguments& arguments,
                           std::index_sequence< Is... > /*Is*/ )
{
    promise.set_value( callable( *self, std::get< Is >( arguments )()... ) );
}

template< typename Arguments, std::size_t... Is >
bool check_argument_status( Arguments& arguments, std::index_sequence< Is... > /*Is*/ )
{
    std::array< bool, sizeof...( Is ) > args_status{ std::get< Is >( arguments ).is_ready()... };
    return std::all_of(
        args_status.begin(), args_status.end(), []( auto value ) { return value; } );
}

namespace pipe_api {

template< typename Pipe >
using pool_t = decltype( Pipe::pool_ );

template< typename Pipe >
using future_t = decltype( Pipe::future_ );

} // namespace pipe_api

template< typename T, typename = void >
struct is_pipe : std::false_type
{
};

template< template< typename > class Allocator = std::allocator >
class task_pool_t;
template< typename T >
struct is_pipe< T, be_void_t< pipe_api::pool_t< T >, pipe_api::future_t< T > > >
    : std::conditional_t< std::is_same< be::task_pool_t<>&, pipe_api::pool_t< T > >::value &&
                              std::is_move_constructible< T >::value,
                          std::true_type,
                          std::false_type >
{
};

} // namespace be
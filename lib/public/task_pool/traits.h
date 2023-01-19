#pragma once

#include <algorithm>
#include <chrono>
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

template< typename T, typename Functor = std::remove_reference_t< std::remove_cv_t< T > > >
struct wants_allocator : public wants_allocator< decltype( &Functor::operator() ) >
{
};

template< typename R, typename... Args >
struct wants_allocator< R( Args... ) > : public contains_allocator_arg< Args... >
{
};

template< typename R, typename... Args >
struct wants_allocator< R ( * )( Args... ) > : public contains_allocator_arg< Args... >
{
};

template< class C, typename R, typename... Args >
struct wants_allocator< R ( C::* )( Args... ) > : public contains_allocator_arg< Args... >
{
};

template< class C, typename R, typename... Args >
struct wants_allocator< R ( C::* )( Args... ) const > : public contains_allocator_arg< Args... >
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
                            std::is_same< std::future_status, wait_for_result_t< T > >::value &&
                            std::is_same< std::future_status, wait_until_result_t< T > >::value,
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
auto invoke_deferred_task( Callable&  callable,
                           Arguments& arguments,
                           std::index_sequence< Is... > /*Is*/ )
{
    return callable( std::get< Is >( arguments )()... );
}

template< typename Callable, typename Arguments, std::size_t... Is >
auto invoke_deferred_task( stop_token const& token,
                           Callable&         callable,
                           Arguments&        arguments,
                           std::index_sequence< Is... > /*Is*/ )
{
    return callable( std::get< Is >( arguments )()..., token );
}

template< typename Arguments, std::size_t... Is >
bool check_argument_status( Arguments& arguments, std::index_sequence< Is... > /*Is*/ )
{
    std::array< bool, sizeof...( Is ) > args_status{ std::get< Is >( arguments ).is_ready()... };
    return std::all_of(
        args_status.begin(), args_status.end(), []( auto value ) { return value; } );
}

} // namespace be
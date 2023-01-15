#pragma once
#include <task_pool/api.h>
#include <type_traits>
#include <utility>

#if __cplusplus < 201700
#    if !defined( BE_NODISGARD )
#        if _MSC_VER >= 1700
#            define BE_NODISGARD _Check_return_
#        else
#            define BE_NODISGARD __attribute__( ( warn_unused_result ) )
#        endif
#    endif
// std::invoke_result_t from cppreference.com
namespace detail {
template< class T >
struct is_reference_wrapper : std::false_type
{
};
template< class U >
struct is_reference_wrapper< std::reference_wrapper< U > > : std::true_type
{
};

template< class T >
struct invoke_impl
{
    template< class F, class... Args >
    static auto call( F&& f, Args&&... args )
        -> decltype( std::forward< F >( f )( std::forward< Args >( args )... ) );
};

template< class B, class MT >
struct invoke_impl< MT B::* >
{
    template< class T,
              class Td = typename std::decay< T >::type,
              class    = typename std::enable_if< std::is_base_of< B, Td >::value >::type >
    static auto get( T&& t ) -> T&&;

    template< class T,
              class Td = typename std::decay< T >::type,
              class    = typename std::enable_if< is_reference_wrapper< Td >::value >::type >
    static auto get( T&& t ) -> decltype( t.get() );

    template< class T,
              class Td = typename std::decay< T >::type,
              class    = typename std::enable_if< !std::is_base_of< B, Td >::value >::type,
              class    = typename std::enable_if< !is_reference_wrapper< Td >::value >::type >
    static auto get( T&& t ) -> decltype( *std::forward< T >( t ) );

    template< class T,
              class... Args,
              class MT1,
              class = typename std::enable_if< std::is_function< MT1 >::value >::type >
    static auto call( MT1 B::*pmf, T&& t, Args&&... args )
        -> decltype( ( invoke_impl::get( std::forward< T >( t ) ).*
                       pmf )( std::forward< Args >( args )... ) );

    template< class T >
    static auto call( MT B::*pmd, T&& t )
        -> decltype( invoke_impl::get( std::forward< T >( t ) ).*pmd );
};

template< class F, class... Args, class Fd = typename std::decay< F >::type >
auto INVOKE( F&& f, Args&&... args )
    -> decltype( invoke_impl< Fd >::call( std::forward< F >( f ),
                                          std::forward< Args >( args )... ) );

// Conforming C++14 implementation (is also a valid C++11 implementation):
template< typename AlwaysVoid, typename, typename... >
struct invoke_result
{
};
template< typename F, typename... Args >
struct invoke_result< decltype( void(
                          detail::INVOKE( std::declval< F >(), std::declval< Args >()... ) ) ),
                      F,
                      Args... >
{
    using type = decltype( detail::INVOKE( std::declval< F >(), std::declval< Args >()... ) );
};

} // namespace detail
#else
#    define BE_NODISGARD [[nodisguard]]
#endif

namespace be {

#if __cplusplus < 201700
template< class >
struct result_of;
template< class F, class... ArgTypes >
struct result_of< F( ArgTypes... ) > : detail::invoke_result< void, F, ArgTypes... >
{
};
template< class F, class... ArgTypes >
struct invoke_result : detail::invoke_result< void, F, ArgTypes... >
{
};
template< typename Fn, typename... Args >
using be_invoke_result_t = typename invoke_result< Fn, Args... >::type;
template< class T >
struct be_is_void : std::is_same< void, typename std::remove_cv< T >::type >
{
};
template< typename T >
constexpr bool be_is_void_v = be_is_void< T >::value;

template< typename... >
using be_void_t = void;

#else
template< typename Fn, typename... Args >
using be_invoke_result_t = std::invoke_result_t< Fn, Args... >;
template< typename T >
constexpr bool be_is_void_v = std::is_void_v< T >;

template< typename... Ts >
using be_void_t = void_t< Ts... >;

#endif

} // namespace be
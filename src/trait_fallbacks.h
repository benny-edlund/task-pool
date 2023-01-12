#pragma once
#include <chrono>
#include <future>
#include <mutex>
#include <type_traits>
#include <utility>

#if __cplusplus < 201700
#if !defined(BE_NODISGARD)
#if _MSC_VER >= 1700
#define BE_NODISGARD _Check_return_
#else
#define BE_NODISGARD __attribute__((warn_unused_result))
#endif
#endif
// std::invoke_result_t from cppreference.com
namespace detail {
template <class T> struct is_reference_wrapper : std::false_type {};
template <class U>
struct is_reference_wrapper<std::reference_wrapper<U>> : std::true_type {};

template <class T> struct invoke_impl {
  template <class F, class... Args>
  static auto call(F &&f, Args &&...args)
      -> decltype(std::forward<F>(f)(std::forward<Args>(args)...));
};

template <class B, class MT> struct invoke_impl<MT B::*> {
  template <
      class T, class Td = typename std::decay<T>::type,
      class = typename std::enable_if<std::is_base_of<B, Td>::value>::type>
  static auto get(T &&t) -> T &&;

  template <
      class T, class Td = typename std::decay<T>::type,
      class = typename std::enable_if<is_reference_wrapper<Td>::value>::type>
  static auto get(T &&t) -> decltype(t.get());

  template <
      class T, class Td = typename std::decay<T>::type,
      class = typename std::enable_if<!std::is_base_of<B, Td>::value>::type,
      class = typename std::enable_if<!is_reference_wrapper<Td>::value>::type>
  static auto get(T &&t) -> decltype(*std::forward<T>(t));

  template <class T, class... Args, class MT1,
            class = typename std::enable_if<std::is_function<MT1>::value>::type>
  static auto call(MT1 B::*pmf, T &&t, Args &&...args)
      -> decltype((invoke_impl::get(std::forward<T>(t)).*
                   pmf)(std::forward<Args>(args)...));

  template <class T>
  static auto call(MT B::*pmd, T &&t)
      -> decltype(invoke_impl::get(std::forward<T>(t)).*pmd);
};

template <class F, class... Args, class Fd = typename std::decay<F>::type>
auto INVOKE(F &&f, Args &&...args)
    -> decltype(invoke_impl<Fd>::call(std::forward<F>(f),
                                      std::forward<Args>(args)...));

// Conforming C++14 implementation (is also a valid C++11 implementation):
template <typename AlwaysVoid, typename, typename...> struct invoke_result {};
template <typename F, typename... Args>
struct invoke_result<decltype(void(detail::INVOKE(std::declval<F>(),
                                                  std::declval<Args>()...))),
                     F, Args...> {
  using type =
      decltype(detail::INVOKE(std::declval<F>(), std::declval<Args>()...));
};

} // namespace detail
#else
#define BE_NODISGARD [[nodisguard]]
#endif

namespace be {

#if __cplusplus < 201700
template <class> struct result_of;
template <class F, class... ArgTypes>
struct result_of<F(ArgTypes...)> : detail::invoke_result<void, F, ArgTypes...> {
};
template <class F, class... ArgTypes>
struct invoke_result : detail::invoke_result<void, F, ArgTypes...> {};
template <typename Fn, typename... Args>
using be_invoke_result_t = typename invoke_result<Fn, Args...>::type;
template <class T>
struct be_is_void : std::is_same<void, typename std::remove_cv<T>::type> {};
template <typename T> constexpr bool be_is_void_v = be_is_void<T>::value;

template <typename...> using be_void_t = void;

#else
template <typename Fn, typename... Args>
using be_invoke_result_t = std::invoke_result_t<Fn, Args...>;
template <typename T> constexpr bool be_is_void_v = std::is_void_v<T>;

template <typename... Ts> using be_void_t = void_t<Ts...>;

#endif

// Thank you Walter
template <typename T, typename... Ts> struct is_one_of;
template <typename T> struct is_one_of<T> : std::false_type {};
template <typename T, typename... Ts>
struct is_one_of<T, T, Ts...> : std::true_type {};
template <typename T, typename U, typename... Ts>
struct is_one_of<T, U, Ts...> : is_one_of<T, Ts...> {};

struct stop_token;

template <typename... Ts>
struct contains_stop_token : is_one_of<stop_token, Ts...> {};

template <typename T,
          typename Functor = std::remove_reference_t<std::remove_cv_t<T>>>
struct wants_stop_token
    : public wants_stop_token<decltype(&Functor::operator())> {};

template <typename R, typename... Args>
struct wants_stop_token<R(Args...)> : public contains_stop_token<Args...> {};

template <typename R, typename... Args>
struct wants_stop_token<R (*)(Args...)> : public contains_stop_token<Args...> {
};

template <class C, typename R, typename... Args>
struct wants_stop_token<R (C::*)(Args...)>
    : public contains_stop_token<Args...> {};

template <class C, typename R, typename... Args>
struct wants_stop_token<R (C::*)(Args...) const>
    : public contains_stop_token<Args...> {};

template <typename T>
static constexpr bool wants_stop_token_v = wants_stop_token<T>::value;

namespace future_api {
template <typename Future>
using get_result_t = decltype(std::declval<Future>().get());

template <typename Future>
using wait_result_t = decltype(std::declval<Future>().wait());

template <typename Future>
using wait_for_result_t = decltype(std::declval<Future>().wait_for(
    std::declval<std::chrono::seconds>()));

template <typename Future>
using wait_until_result_t = decltype(std::declval<Future>().wait_until(
    std::declval<std::chrono::steady_clock::time_point>()));

template <typename T, typename = void> struct is_supported : std::false_type {};

template <typename T>
struct is_supported<T, be_void_t<get_result_t<T>, wait_result_t<T>,
                                 wait_for_result_t<T>, wait_until_result_t<T>>>
    : std::conditional<
          std::is_same<void, wait_result_t<T>>::value &&
              std::is_same<std::future_status, wait_for_result_t<T>>::value &&
              std::is_same<std::future_status, wait_until_result_t<T>>::value,
          std::true_type, std::false_type>::type {};

} // namespace future_api

template <typename T> struct is_future : future_api::is_supported<T>::type {};

template <> struct is_future<double> : std::false_type {};

template <typename... Ts> struct contains_future;
template <typename T> struct contains_future<T> : is_future<T> {};
template <typename T, typename... Ts>
struct contains_future<T, Ts...>
    : std::conditional_t<is_future<T>::value, std::true_type,
                         contains_future<Ts...>> {};

template <typename T> using future_value_t = future_api::get_result_t<T>;

template <typename T, typename = void> struct future_argument {
  using type = T;
};

template <typename T>
struct future_argument<T, be_void_t<future_api::get_result_t<T>>> {
  using type = future_value_t<T>;
};

template <typename T>
using future_argument_t = typename future_argument<T>::type;

template <typename T>
struct is_movable : std::conditional_t<std::is_move_constructible<T>::value ||
                                           std::is_move_assignable<T>::value,
                                       std::true_type, std::false_type>::type {
};

} // namespace be